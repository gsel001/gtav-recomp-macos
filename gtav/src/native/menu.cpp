#include "menu.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "touch_controls.h"

#include "paths.h"

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace gtav::native::menu {

namespace {

struct Input {
  enum Type { kMove, kButton, kWheel, kKey } type;
  float x = 0, y = 0;
  int code = 0;
  bool down = false;
};

std::mutex g_input_mutex;
std::vector<Input> g_input;
std::atomic<bool> g_open{false};

ImGuiContext* g_context = nullptr;
bool g_font_uploaded = false;
float g_scale = 1.0f;

// Settings shown by the menu or set by its presets. Saved as name=value.
constexpr const char* kSettings[] = {
    "gtav_fps_limit",
    "gtav_native_vsync",
    "gtav_native_fps",
    "gtav_native_tile_penumbra_all",
    "gtav_native_async_pipelines",
    "gtav_native_pipeline_wait_ms",
    "gtav_native_texture_budget_mb",
    "gtav_native_buffer_budget_mb",
    "gpu_skip_rendering_lag_ms",
    "gtav_native_exact_cache",
    "gtav_native_texture_recheck_frames",
    "gtav_touch_controls",
};

std::filesystem::path SettingsPath() { return ExeDir() / "gtav_settings.ini"; }

void SaveSettings() {
  std::ofstream out(SettingsPath());
  out << "# GTA V native renderer settings (Esc menu). name=value, applied over the command line.\n";
  for (const char* name : kSettings) out << name << '=' << rex::cvar::GetFlagByName(name) << '\n';
}

bool GetBool(const char* name) { return rex::cvar::GetFlagByName(name) == "true"; }
void SetBool(const char* name, bool value) { rex::cvar::SetFlagByName(name, value ? "true" : "false"); }

// Presets: the two ends of the trade-off. "Best image" draws everything exactly as soon as it
// appears (new pipelines are built on the spot, a short hitch the first time an effect shows up)
// with the per-pixel sun shadows; "Max performance" keeps the frame moving (new effects are
// skipped for a frame or two while they compile), lets the game skip shadow work per tile and
// lets the emulated GPU trail the game less.
// How far the emulated GPU trails the game's kicks (ms): 6 is the tested value (foliage stopped
// flickering while driving); 2 flickered. 4 is ~10% faster on Windows (88 -> 98 fps running
// around, 10-29) and no worse than 6 in a headless flicker-catch A/B; not yet checked driving.
constexpr const char* kSafeGpuLag = "6";
constexpr const char* kFastGpuLag = "4";

void ApplyPreset(bool best) {
#ifdef __ANDROID__
  // Phones: the per-pixel sun tiles cost no measurable fps (same-run A/B on an Adreno 730), and
  // without them the game's per-tile shortcut shows gray 16x16 blocks at object edges.
  SetBool("gtav_native_tile_penumbra_all", true);
#else
  SetBool("gtav_native_tile_penumbra_all", best);
#endif
  SetBool("gtav_native_async_pipelines", !best);
  rex::cvar::SetFlagByName("gpu_skip_rendering_lag_ms", best ? kSafeGpuLag : kFastGpuLag);
#ifdef __ANDROID__
  // Phones: "Best image" is also the steady one, locked at 30 fps with vsync (sustained play heats
  // the phone less and throttles later); "Max performance" runs uncapped.
  rex::cvar::SetFlagByName("gtav_fps_limit", best ? "30" : "0");
  SetBool("gtav_native_vsync", best);
#else
  if (!best) {
    rex::cvar::SetFlagByName("gtav_fps_limit", "0");
    SetBool("gtav_native_vsync", false);
  }
#endif
  SaveSettings();
}

#if defined(__APPLE__) && !TARGET_OS_IPHONE
// Opt-in settings tested on an 8 GB M1. Keep accurate sun shadows while limiting
// pipeline waits and idle cache growth; a fixed 45 fps target leaves more headroom
// than 60 in scenes whose GPU time is already around 20-22 ms. Streaming can still hitch.
void ApplySteady45Preset() {
  rex::cvar::SetFlagByName("gtav_fps_limit", "45");
  SetBool("gtav_native_vsync", false);
  SetBool("gtav_native_tile_penumbra_all", true);
  SetBool("gtav_native_async_pipelines", true);
  rex::cvar::SetFlagByName("gpu_skip_rendering_lag_ms", kSafeGpuLag);
  rex::cvar::SetFlagByName("gtav_native_pipeline_wait_ms", "2");
  rex::cvar::SetFlagByName("gtav_native_texture_budget_mb", "256");
  rex::cvar::SetFlagByName("gtav_native_buffer_budget_mb", "96");
  SaveSettings();
}
#endif

void EnsureContext(uint32_t h) {
  if (g_context) return;
  IMGUI_CHECKVERSION();
  ImGuiContext* previous = ImGui::GetCurrentContext();
  g_context = ImGui::CreateContext();
  ImGui::SetCurrentContext(g_context);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  g_scale = std::max(1.0f, float(h) / 720.0f);
#if defined(__APPLE__) && TARGET_OS_IPHONE
  g_scale *= 1.6f;  // a 5.4-inch screen driven by fingers: larger text and controls
#endif
  ImFontConfig font;
  font.SizePixels = 15.0f * g_scale;
  io.Fonts->AddFontDefault(&font);
  ImGui::StyleColorsDark();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.ScaleAllSizes(g_scale);
  if (previous) ImGui::SetCurrentContext(previous);
}

void Push(const Input& in) {
  if (!g_open) return;
  std::lock_guard lock(g_input_mutex);
  if (g_input.size() < 1024) g_input.push_back(in);
}

ImGuiKey ToImGui(Key key) {
  switch (key) {
    case Key::kUp: return ImGuiKey_UpArrow;
    case Key::kDown: return ImGuiKey_DownArrow;
    case Key::kLeft: return ImGuiKey_LeftArrow;
    case Key::kRight: return ImGuiKey_RightArrow;
    case Key::kEnter: return ImGuiKey_Enter;
    case Key::kSpace: return ImGuiKey_Space;
    case Key::kTab: return ImGuiKey_Tab;
  }
  return ImGuiKey_None;
}

void Build(uint32_t fps, double frame_ms) {
#if defined(__APPLE__) && TARGET_OS_IPHONE
  // Centred: the screen's sides are under the rounded corners and the notch.
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
#else
  ImGui::SetNextWindowPos(ImVec2(40.0f * g_scale, 40.0f * g_scale), ImGuiCond_FirstUseEver);
#endif
  ImGui::SetNextWindowSize(ImVec2(520.0f * g_scale, 0.0f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (!ImGui::Begin("Settings (Esc / B to close)", &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }
  ImGui::Text("%u fps  (%.1f ms per frame)", fps, frame_ms);
  ImGui::Separator();

  ImGui::TextUnformatted("Preset");
  if (ImGui::Button("Best image")) ApplyPreset(true);
  ImGui::SameLine();
  if (ImGui::Button("Max performance")) ApplyPreset(false);
#if defined(__APPLE__) && !TARGET_OS_IPHONE
  ImGui::SameLine();
  if (ImGui::Button("Steady 45 FPS")) ApplySteady45Preset();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Prioritize steady frame delivery on an 8 GB Mac.\n"
                      "New effects may appear late while compiling; loading pauses can still occur.");
#endif
  ImGui::TextDisabled("Compare the fps above after switching.");
  ImGui::Separator();

  bool changed = false;
  int limit = std::atoi(rex::cvar::GetFlagByName("gtav_fps_limit").c_str());
  int choice = limit == 30 ? 0 : limit == 45 ? 1 : limit == 60 ? 2 : limit <= 0 ? 3 : 4;
  const char* limits[] = {"30 (the game's own limiter)", "45", "60", "Uncapped", "Custom (command line)"};
  const char* values[] = {"30", "45", "60", "0"};
  ImGui::SetNextItemWidth(260.0f * g_scale);
  if (ImGui::Combo("Frame rate limit", &choice, limits, choice == 4 ? 5 : 4) && choice < 4) {
    rex::cvar::SetFlagByName("gtav_fps_limit", values[choice]);
    changed = true;
  }

  auto check = [&](const char* label, const char* name, const char* help) {
    bool v = GetBool(name);
    if (ImGui::Checkbox(label, &v)) {
      SetBool(name, v);
      changed = true;
    }
    if (help && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
  };
  check("VSync", "gtav_native_vsync", "Wait for the display refresh: no tearing, fps capped at the refresh rate.");
  check("FPS counter", "gtav_native_fps", "Frames per second in the top-left corner.");
  ImGui::Separator();
  ImGui::TextUnformatted("Accuracy");
  check("Per-pixel sun shadows", "gtav_native_tile_penumbra_all",
        "On: every screen tile computes sun shadows per pixel (no blocky shadows or car reflections).\n"
        "Off: the game's own per-tile shortcut, which goes blocky on this renderer (gray 16x16 blocks).");
  check("Compile new effects in the background", "gtav_native_async_pipelines",
        "On: no hitch when a new effect first appears; it is skipped for a frame or two instead.\n"
        "Off: it is drawn from its first frame, with a short hitch while it is built.");
  {
    bool fast = std::atoi(rex::cvar::GetFlagByName("gpu_skip_rendering_lag_ms").c_str()) < std::atoi(kSafeGpuLag);
    if (ImGui::Checkbox("Low GPU latency", &fast)) {
      rex::cvar::SetFlagByName("gpu_skip_rendering_lag_ms", fast ? kFastGpuLag : kSafeGpuLag);
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("On: the emulated GPU trails the game by 2 ms instead of 6 (about 20%% more fps).\n"
                        "Off: the tested setting; with On, watch foliage and distant meshes while driving.");
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Textures");
  check("Exact texture checks", "gtav_native_exact_cache",
        "On: every texture is fully re-checked each frame: no stale gray blocks, but more CPU time.\n"
        "Off: large textures are spot-checked between full checks (faster).");
  {
    int frames = std::atoi(rex::cvar::GetFlagByName("gtav_native_texture_recheck_frames").c_str());
    ImGui::SetNextItemWidth(200.0f * g_scale);
    if (ImGui::SliderInt("Full re-check every N frames", &frames, 1, 30)) {
      rex::cvar::SetFlagByName("gtav_native_texture_recheck_frames", std::to_string(frames));
      changed = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Lower: textures the game streams in show up sooner (fewer gray blocks), more CPU.");
  }
  ImGui::Separator();
  check("Touch controls", "gtav_touch_controls", "The on-screen gamepad (hidden while a controller is connected).");
  ImGui::TextDisabled("Controller: RB + Back opens/closes this menu; D-pad + A to change, B to close.");
  if (changed) SaveSettings();
  ImGui::End();
  if (!open) g_open = false;
}

}  // namespace

void OnEscape() {
  bool was_open = g_open.exchange(!g_open.load());
  if (!was_open) {
    std::lock_guard lock(g_input_mutex);
    g_input.clear();
  }
}
bool IsOpen() { return g_open.load(); }
void OnMouseMove(float x, float y) { Push({Input::kMove, x, y}); }
void OnMouseButton(int button, bool down) { Push({Input::kButton, 0, 0, button, down}); }
void OnMouseWheel(float dy) { Push({Input::kWheel, 0, dy}); }
void OnKey(Key key, bool down) { Push({Input::kKey, 0, 0, int(key), down}); }

void TestSet(const std::string& name, const std::string& value) {
  REXLOG_INFO("[native] menu test: {} = {}", name, value);
  rex::cvar::SetFlagByName(name, value);
  if (std::getenv("GTAV_IGNORE_SETTINGS")) return;  // benchmark runs leave gtav_settings.ini alone
  SaveSettings();
  REXLOG_INFO("[native] menu test: saved");
}

void LoadSettings() {
  // GTAV_IGNORE_SETTINGS=1: command-line settings only (benchmark runs).
  if (std::getenv("GTAV_IGNORE_SETTINGS")) return;
  std::ifstream in(SettingsPath());
  if (!in) return;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string name = line.substr(0, eq), value = line.substr(eq + 1);
    if (std::find_if(std::begin(kSettings), std::end(kSettings), [&](const char* s) { return name == s; }) ==
        std::end(kSettings))
      continue;
    if (rex::cvar::SetFlagByName(name, value)) REXLOG_INFO("[native] setting {} = {} (gtav_settings.ini)", name, value);
  }
}

ImDrawData* Frame(uint32_t w, uint32_t h, uint32_t fps, double frame_ms) {
  const bool touch_overlay = touch::Visible();
  if (!g_open && !touch_overlay) return nullptr;
  EnsureContext(h);
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(g_context);
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(float(w), float(h));
  io.DeltaTime = 1.0f / 60.0f;
  {
    std::lock_guard lock(g_input_mutex);
    for (const auto& in : g_input) {
      switch (in.type) {
        case Input::kMove: io.AddMousePosEvent(in.x, in.y); break;
        case Input::kButton: io.AddMouseButtonEvent(in.code, in.down); break;
        case Input::kWheel: io.AddMouseWheelEvent(0.0f, in.y); break;
        case Input::kKey: io.AddKeyEvent(ToImGui(Key(in.code)), in.down); break;
      }
    }
    g_input.clear();
  }
  if (!g_font_uploaded) {  // legacy atlas: the backend uploads it before the first draw
    unsigned char* pixels;
    int fw, fh;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
  }
  ImGui::NewFrame();
  if (g_open) Build(fps, frame_ms);
  if (touch_overlay) touch::Draw(ImGui::GetForegroundDrawList(), float(w), float(h));
  ImGui::Render();
  ImDrawData* data = ImGui::GetDrawData();
  if (previous && previous != g_context) ImGui::SetCurrentContext(previous);
  return data;
}

bool TakeFontAtlas(const uint8_t** pixels, int* width, int* height) {
  if (g_font_uploaded || !g_context) return false;
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(g_context);
  unsigned char* p;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&p, width, height);
  *pixels = p;
  if (previous && previous != g_context) ImGui::SetCurrentContext(previous);
  return true;
}

void SetFontTexture(uint32_t descriptor) {
  ImGuiContext* previous = ImGui::GetCurrentContext();
  ImGui::SetCurrentContext(g_context);
  ImGui::GetIO().Fonts->SetTexID(ImTextureID(descriptor));
  g_font_uploaded = true;
  if (previous && previous != g_context) ImGui::SetCurrentContext(previous);
}

}  // namespace gtav::native::menu
