// gtav - frame-rate limit (--gtav_fps_limit).
//
// The game paces itself in sub_827B5878: while less than N/60 s (N = the int at 0x8391D9D4,
// 2 on the 360) passed since the last frame (timebase), it sleeps. N <= 0 skips the wait.
// --gtav_fps_limit=30 (default) restores the game's N = 2 limiter; any other value sets N = 0
// and, unless 0 (uncapped), paces frames here to that rate instead. The game itself is not
// the only limit: with the emulated GPU's vblank at 60 Hz (--vsync=true) other waits still
// tie it to the display, so uncapped play also wants --vsync=false.

#include <rex/cvar.h>
#include <rex/hook.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

REXCVAR_DEFINE_INT32(gtav_fps_limit, 30, "GTAV",
                     "Frame-rate limit: 30 = restore the game's two-vblank limiter, 0 = uncapped, other = that rate");
REXCVAR_DEFINE_BOOL(gtav_audio_unsync, true, "GTAV",
                    "Above 30 fps: stop the audio update waiting for the next 16 ms audio frame "
                    "(otherwise the game cannot exceed 62.5 fps)");
REXCVAR_DEFINE_BOOL(gtav_no_power_throttling, true, "GTAV",
                    "Windows: opt out of power throttling (EcoQoS / efficiency cores, ignored timer "
                    "resolution) while the window is hidden, minimized or in the background");

namespace {

constexpr uint32_t kFrameIntervalVblanks = 0x8391D9D4;  // N: frame time in 1/60 s units
// The audio update (sub_831D8A68, run by the thread in sub_82488170 that the main loop waits on
// every frame) polls up to 1 s for the audio engine's next frame (768 samples at 48 kHz = 16 ms);
// the main loop therefore never exceeds 62.5 fps. When that poll times out the game sets this
// byte and stops waiting from then on: its own no-audio-sync fallback, set here instead.
constexpr uint32_t kAudioSyncTimedOut = 0x83D0D4CD;

void Store32(uint8_t* base, uint32_t addr, int32_t value) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(uint32_t(value) >> 24), p[1] = uint8_t(value >> 16), p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

#ifdef _WIN32
// Windows rounds every host sleep (this limiter's and the guest's) up to the 15.6 ms system tick
// unless the process asks for 1 ms; at 60 fps that alone costs whole frames.
const bool g_timer_resolution = [] { return timeBeginPeriod(1) == TIMERR_NOERROR; }();

// Windows 11 power throttling: a process whose windows are hidden or minimized (or just not in
// front) may get EcoQoS (efficiency cores on hybrid CPUs, lower clocks) and has its timer
// resolution request ignored, so every guest sleep rounds up to 15.6 ms again. Opt out of both
// (--gtav_no_power_throttling; checked every frame so an A/B can flip it in one run).
void ApplyPowerThrottling() {
  static int applied = -1;
  int off = REXCVAR_GET(gtav_no_power_throttling) ? 1 : 0;
  if (off == applied) return;
  applied = off;
  PROCESS_POWER_THROTTLING_STATE state = {};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask =
      off ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION : 0;
  state.StateMask = 0;  // with ControlMask 0: the system decides again
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
}
#else
void ApplyPowerThrottling() {}
#endif

}  // namespace

REX_EXTERN(__imp__sub_827B5878);
REX_HOOK_RAW(sub_827B5878) {
  ApplyPowerThrottling();
  int32_t limit = REXCVAR_GET(gtav_fps_limit);
  if (limit == 30) {
    // Startup or a previous uncapped/custom limit may have cleared this guest interval.
    // Delegating without restoring it left the nominal 30 FPS setting effectively uncapped.
    Store32(base, kFrameIntervalVblanks, 2);
    __imp__sub_827B5878(ctx, base);
    return;
  }
  Store32(base, kFrameIntervalVblanks, 0);
  if (REXCVAR_GET(gtav_audio_unsync)) base[kAudioSyncTimedOut] = 1;
  __imp__sub_827B5878(ctx, base);
  if (limit <= 0) return;
  using clock = std::chrono::steady_clock;
  static clock::time_point next = clock::now();
  auto period = std::chrono::nanoseconds(1000000000 / limit);
  auto now = clock::now();
  if (next > now) std::this_thread::sleep_until(next);
  next = std::max(next + period, now);  // no catch-up after a long frame
}
