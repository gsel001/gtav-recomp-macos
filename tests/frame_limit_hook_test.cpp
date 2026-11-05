// Exercise the production hook with synthetic flags, sparse guest memory and a stub guest call.
// No game code, game library, timing benchmark or real PPC context is required.
#include <rex/hook.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sys/mman.h>
#include <unistd.h>

extern int32_t FLAGS_gtav_fps_limit;
extern bool FLAGS_gtav_audio_unsync;
REX_EXTERN(sub_827B5878);
constexpr size_t kInterval = 0x8391D9D4, kAudio = 0x83D0D4CD;
static uint32_t expected_interval;
static uint8_t expected_audio;

extern "C" void __imp__sub_827B5878(PPCContext& ctx, uint8_t* base) {
  // Verify the guest sees restored state BEFORE it executes its frame wait.
  const uint32_t interval = uint32_t(base[kInterval]) << 24 | uint32_t(base[kInterval + 1]) << 16 |
                            uint32_t(base[kInterval + 2]) << 8 | base[kInterval + 3];
  if (interval != expected_interval) {
    std::fprintf(stderr, "Guest wait received interval %u; expected %u\n", interval, expected_interval);
    std::exit(2);
  }
  assert(base[kAudio] == expected_audio);
  ++ctx.calls;
}

int main() {
  constexpr size_t size = 0x84000000ull;
  auto* base = static_cast<uint8_t*>(mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
  assert(base != MAP_FAILED);
  const size_t page = size_t(sysconf(_SC_PAGESIZE));
  for (size_t address : {kInterval, kAudio})
    assert(mprotect(base + address / page * page, page, PROT_READ | PROT_WRITE) == 0);
  PPCContext ctx;
  base[kInterval - 1] = 0xA5; base[kInterval + 4] = 0x5A;
  base[kAudio] = 0;
  FLAGS_gtav_audio_unsync = true;
  // Includes uncapped startup, repeated 30 FPS, and both custom-cap transitions.
  for (int limit : {0, 30, 30, 60, 30, 45, 30, 0, 30}) {
    FLAGS_gtav_fps_limit = limit;
    expected_interval = limit == 30 ? 2 : 0;
    expected_audio = 1;
    sub_827B5878(ctx, base);
    assert(base[kInterval - 1] == 0xA5 && base[kInterval + 4] == 0x5A);
  }
  assert(ctx.calls == 9);
  // The original wait may change its interval: each 30 FPS call must restore it again.
  base[kInterval + 3] = 1;
  sub_827B5878(ctx, base);
  // Preserve the explicit audio-sync preference outside 30 FPS.
  FLAGS_gtav_audio_unsync = false; FLAGS_gtav_fps_limit = 0;
  base[kAudio] = 0; expected_interval = 0; expected_audio = 0;
  sub_827B5878(ctx, base);
  assert(ctx.calls == 11);
  assert(munmap(base, size) == 0);
  std::puts("Frame limiter: startup, 60/45 to 30 transitions, repeated restoration and audio preference passed");
}
