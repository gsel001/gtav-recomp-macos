// gtav - background compiler for shaders the precompiled set lacks.
//
// The SPIR-V shipped next to the exe covers the shaders seen while the census was taken; the game
// streams in more as the world loads (hundreds outdoors). A shader created without SPIR-V is
// written to <exe>/shader_cache/<name>.bin and compiled here exactly like the offline census
// (tools/native/shader_census.py): XenosRecomp -> HLSL -> DXC -> SPIR-V into the SPIR-V dir.
// The backend retries missing shaders whenever CompileGeneration() moves, so a draw that was
// skipped renders a few hundred milliseconds later and the result persists for later runs.

#include "shader_compiler.h"

#include "paths.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// iOS cannot start processes: every shader comes precompiled from the Mac (spirv dir).
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define GTAV_NATIVE_COMPILE_DEFAULT false
#else
#define GTAV_NATIVE_COMPILE_DEFAULT true
#endif
REXCVAR_DEFINE_BOOL(gtav_native_compile, GTAV_NATIVE_COMPILE_DEFAULT, "GTAV",
                    "Compile shaders that have no precompiled SPIR-V in the background (tools/ next to the exe)");
// iOS: shaders without SPIR-V are still written to shader_cache/<name>.bin, for the Mac to translate
// (tools/ios: the iPhone step pulls them and pushes the SPIR-V back).
REXCVAR_DEFINE_BOOL(gtav_native_save_missing_shaders, !GTAV_NATIVE_COMPILE_DEFAULT, "GTAV",
                    "Without --gtav_native_compile: still save shaders that have no SPIR-V to shader_cache/");
REXCVAR_DECLARE(std::string, gtav_native_spirv_dir);

namespace gtav::native {
namespace fs = std::filesystem;

namespace {

std::atomic<uint64_t> g_generation{0};
std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<std::string> g_queue;  // shader names ("ps_<HASH>")
std::unordered_set<std::string> g_seen;
std::unordered_set<std::string> g_prioritized;  // requested by a draw, once per shader
std::unordered_set<std::string> g_present;  // .spv on disk: found at intake or compiled since
std::atomic<uint32_t> g_ok{0}, g_failed{0};
bool g_started = false;
// New shaders arrive on game threads (the D3D create hooks). The disk checks and the container
// write happen on an intake thread: on the busy USB disk each exists() / fopen took ~50 ms, on
// the game thread, while streaming in a new area created dozens of shaders.
struct Intake {
  std::string name;
  std::vector<uint8_t> container;
};
std::deque<Intake> g_intake;
std::condition_variable g_intake_cv;
bool g_intake_started = false;

fs::path ToolsDir() { return ExeDir() / "tools"; }
fs::path SpirvDirPath() {
  const std::string& dir = REXCVAR_GET(gtav_native_spirv_dir);
  return dir.empty() ? fs::path(DefaultSpirvDir()) : fs::path(dir);
}
// Next to the SPIR-V dir: build/shader_cache in a dev tree, data/shader_cache in a release install.
#if defined(__APPLE__) && TARGET_OS_IPHONE
fs::path CacheDir() { return ExeDir() / "shader_cache"; }  // Documents: the bundle is read-only
#else
fs::path CacheDir() { return SpirvDirPath().parent_path() / "shader_cache"; }
#endif

// Runs a command line without a console window; true on exit code 0.
bool Run(const std::vector<std::string>& args) {
#ifdef _WIN32
  std::wstring cmd;
  for (const auto& a : args) {
    if (!cmd.empty()) cmd += L' ';
    cmd += L'"' + fs::path(a).wstring() + L'"';
  }
  STARTUPINFOW si{sizeof(si)};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                      nullptr, nullptr, &si, &pi))
    return false;
  WaitForSingleObject(pi.hProcess, 120000);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return code == 0;
#elif defined(__APPLE__) && TARGET_OS_IPHONE
  (void)args;
  return false;
#else
  std::string cmd;
  for (const auto& a : args) cmd += "'" + a + "' ";
  cmd += ">/dev/null 2>&1";
  return std::system(cmd.c_str()) == 0;
#endif
}

// HLSL -> <spirv>/<name>.spv with the same DXC arguments as the offline census.
bool CompileHlsl(const fs::path& hlsl, const std::string& name, bool pixel) {
  const fs::path tools = ToolsDir(), spirv = SpirvDirPath();
  const fs::path tmp = spirv / (name + ".spv.tmp"), out = spirv / (name + ".spv");
#ifdef _WIN32
  const fs::path dxc = tools / "dxc.exe";
#else
  const fs::path dxc = tools / "dxc";
#endif
  std::vector<std::string> args = {dxc.string(), "-T", pixel ? "ps_6_0" : "vs_6_0", "-HV", "2021",
                                   "-all-resources-bound", "-spirv", "-fvk-use-dx-layout",
                                   "-Wno-ignored-attributes", "-Qstrip_debug"};
  if (!pixel) args.push_back("-fvk-invert-y");
  args.insert(args.end(), {"-Fo", tmp.string(), hlsl.string()});
  if (!Run(args)) return false;
  // Vertex shaders with computed-index fetches need the raw streams (see Backend::Shader).
  if (!pixel) {
    std::ifstream f(hlsl);
    std::stringstream ss;
    ss << f.rdbuf();
    if (ss.str().find("= vfetchIndirect(") != std::string::npos) std::ofstream(spirv / (name + ".indirect"));
  }
  std::error_code ec;
  fs::rename(tmp, out, ec);  // the marker exists before the backend can see the .spv
  return !ec;
}

// Variants of game shaders, made on the player's machine from their own shader: XenosRecomp's
// translation with one line replaced, compiled to <shader><suffix>.spv. Nothing translated from
// the game ships with the exe; only the rule does.
struct Variant {
  const char* shader;
  const char* suffix;
  const char* find;
  const char* replace;
  const char* find2 = nullptr;  // optional second line
  const char* replace2 = nullptr;
};
constexpr Variant kVariants[] = {
    // Tile classification (--gtav_native_tile_penumbra_all, Backend::TileOverride). The game adds
    // 64 (the penumbra flag) only when the 2x2 shadow-mask alphas differ; a 1/255 filtering
    // difference from the Xbox flips it and the 16x16 tile takes the cheap lit/shadowed variant
    // (blocky shadows and car reflections). The variant always sets the flag, so every non-sky
    // tile takes the full per-pixel path, which is right for uniform tiles too.
    // The per-pixel sun draw also skips tiles whose largest sun visibility (oC0.z) is 0.05 or less,
    // which the uniform draw covered: with every tile flagged, fully shadowed tiles got no sun pass
    // and kept the target's old contents (light 16x16 squares in shade, often on the phone, whose
    // point-sampled shadows give more exact zeros). The second line keeps non-sky tiles above it.
    // "_tiles2": made again over an older "_tiles" variant.
    {"ps_D258EBE32001470C", "_tiles2", "r0.y = select(r0.y > 0.0, c255.x, c255.y);", "r0.y = c255.x;",
     "oC0.z = select(r0.w > 0.0, r1.x, c255.z);", "oC0.z = select(r0.w > 0.0, max(r1.x, 0.0625), c255.z);"},
};

bool VariantsOnDisk(const std::string& name) {
  std::error_code ec;
  for (const Variant& v : kVariants) {
    if (name == v.shader && !fs::exists(SpirvDirPath() / (name + v.suffix + ".spv"), ec)) return false;
  }
  return true;
}

bool CompileVariants(const std::string& name, const fs::path& hlsl) {
  bool ok = true;
  for (const Variant& v : kVariants) {
    if (name != v.shader) continue;
    std::ifstream f(hlsl);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    size_t at = text.find(v.find);
    if (at == std::string::npos || text.find(v.find, at + 1) != std::string::npos) {
      REXLOG_WARN("[native] shader variant {}{}: the line to replace was not found exactly once", name, v.suffix);
      ok = false;
      continue;
    }
    text.replace(at, std::strlen(v.find), v.replace);
    if (v.find2) {
      size_t at2 = text.find(v.find2);
      if (at2 == std::string::npos || text.find(v.find2, at2 + 1) != std::string::npos) {
        REXLOG_WARN("[native] shader variant {}{}: the second line to replace was not found exactly once", name, v.suffix);
        ok = false;
        continue;
      }
      text.replace(at2, std::strlen(v.find2), v.replace2);
    }
    const fs::path out = CacheDir() / (name + v.suffix + ".hlsl");
    std::ofstream(out) << text;
    ok = CompileHlsl(out, name + v.suffix, name.rfind("ps_", 0) == 0) && ok;
  }
  return ok;
}

bool Compile(const std::string& name) {
  const fs::path tools = ToolsDir(), cache = CacheDir();
  const bool pixel = name.rfind("ps_", 0) == 0;
  const fs::path bin = cache / (name + ".bin"), hlsl = cache / (name + ".hlsl");
#ifdef _WIN32
  const fs::path xr = tools / "XenosRecomp.exe";
#else
  const fs::path xr = tools / "XenosRecomp";
#endif
  if (!Run({xr.string(), bin.string(), hlsl.string(), (tools / "shader_common.h").string()}) || !fs::exists(hlsl))
    return false;
  // Variants first: the backend retries every missing shader once the base .spv appears.
  if (!CompileVariants(name, hlsl)) return false;
  return CompileHlsl(hlsl, name, pixel);
}

void Worker() {
  for (;;) {
    std::string name;
    {
      std::unique_lock lock(g_mutex);
      g_cv.wait(lock, [] { return !g_queue.empty(); });
      name = std::move(g_queue.front());
      g_queue.pop_front();
    }
    if (Compile(name)) {
      {
        std::lock_guard lock(g_mutex);
        g_present.insert(name);
      }
      g_generation.fetch_add(1, std::memory_order_release);
      uint32_t n = g_ok.fetch_add(1) + 1;
      if (n <= 5 || n % 50 == 0) REXLOG_INFO("[native] compiled {} new shaders (latest {})", n, name);
    } else {
      uint32_t n = g_failed.fetch_add(1) + 1;
      REXLOG_WARN("[native] shader {} failed to compile ({} failures); its draws stay skipped", name, n);
    }
  }
}

void Enqueue(const std::string& name) {
  std::lock_guard lock(g_mutex);
  if (!g_started) {
    g_started = true;
    // Low-priority workers: two were enough to catch up after a streaming burst with the
    // precompiled set; a first run with no SPIR-V compiles every shader the game creates, so
    // machines with more cores get more (a third of them).
    int workers = std::max(2, int(std::thread::hardware_concurrency()) / 3);
    for (int i = 0; i < workers; i++) std::thread(Worker).detach();
  }
  if (g_prioritized.count(name))
    g_queue.push_front(name);
  else
    g_queue.push_back(name);
  g_cv.notify_one();
}

}  // namespace

uint64_t CompileGeneration() { return g_generation.load(std::memory_order_acquire); }

bool ShaderOnDisk(const std::string& name) {
  std::lock_guard lock(g_mutex);
  return g_present.count(name) != 0;
}

void PrioritizeShader(uint64_t hash, bool pixel) {
  if (!REXCVAR_GET(gtav_native_compile) || !hash) return;
  const std::string name = fmt::format("{}_{:016X}", pixel ? "ps" : "vs", hash);
  std::lock_guard lock(g_mutex);
  if (!g_prioritized.insert(name).second) return;
  // The game creates large batches of shaders before using them. A fullscreen draw can
  // otherwise wait behind the entire batch, leaving only audio until its shaders compile.
  // Promote both stages: the request may arrive before the container reaches the compiler.
  auto intake = std::find_if(g_intake.begin(), g_intake.end(),
                             [&](const Intake& item) { return item.name == name; });
  if (intake != g_intake.end()) std::rotate(g_intake.begin(), intake, std::next(intake));
  auto queued = std::find(g_queue.begin(), g_queue.end(), name);
  if (queued != g_queue.end()) std::rotate(g_queue.begin(), queued, std::next(queued));
}

static void IntakeWorker() {
  for (;;) {
    Intake item;
    {
      std::unique_lock lock(g_mutex);
      g_intake_cv.wait(lock, [] { return !g_intake.empty(); });
      item = std::move(g_intake.front());
      g_intake.pop_front();
    }
    std::error_code ec;
    // A fresh install starts with an empty (or no) SPIR-V dir: every shader is compiled here.
    fs::create_directories(SpirvDirPath(), ec);
    if (fs::exists(SpirvDirPath() / (item.name + ".spv"), ec) && VariantsOnDisk(item.name)) {
      std::lock_guard lock(g_mutex);
      g_present.insert(item.name);
      continue;
    }
    fs::create_directories(CacheDir(), ec);
    fs::path bin = CacheDir() / (item.name + ".bin");
    if (!fs::exists(bin, ec)) {
      if (FILE* f = std::fopen(bin.string().c_str(), "wb")) {
        std::fwrite(item.container.data(), 1, item.container.size(), f);
        std::fclose(f);
      }
    }
    if (REXCVAR_GET(gtav_native_compile)) Enqueue(item.name);
  }
}

void OnShaderCreated(const uint8_t* container, size_t size, uint64_t hash, bool pixel) {
  if (!REXCVAR_GET(gtav_native_compile) && !REXCVAR_GET(gtav_native_save_missing_shaders)) return;
  std::string name = fmt::format("{}_{:016X}", pixel ? "ps" : "vs", hash);
  std::lock_guard lock(g_mutex);
  if (!g_seen.insert(name).second) return;
  if (!g_intake_started) {
    g_intake_started = true;
    std::thread(IntakeWorker).detach();
  }
  Intake item{std::move(name), std::vector<uint8_t>(container, container + size)};
  if (g_prioritized.count(item.name))
    g_intake.push_front(std::move(item));
  else
    g_intake.push_back(std::move(item));
  g_intake_cv.notify_one();
}

}  // namespace gtav::native
