// Production intake, duplicate suppression, promotion and worker paths with fake tools.
// No GPU, game libraries, game shaders or installed files.
#include "../gtav/src/native/shader_compiler.cpp"
#include <cassert>
#include <chrono>
#include <unistd.h>
using namespace gtav::native;
int main() {
  const auto root = fs::temp_directory_path() / ("shader-lifecycle-" + std::to_string(getpid()));
  fs::create_directories(root / "spirv");
  FLAGS_gtav_native_spirv_dir = (root / "spirv").string();
  FLAGS_gtav_native_shader_low_priority = false;
  // Deterministic queue checks before background threads exist.
  g_queue = {"ps_0000000000000001", "ps_0000000000000002", "ps_0000000000000003"};
  PrioritizeShader(2, true); PrioritizeShader(2, true);
  assert(g_queue.front() == "ps_0000000000000002" && g_queue.size() == 3);
  assert(g_queue.back() == "ps_0000000000000003"); g_queue.clear();
  PrioritizeShader(4, true); assert(g_prioritized.count("ps_0000000000000004"));
  const auto before = test_log_lines.load();
  ReportShaderCompilation(); assert(test_log_lines == before);
  FLAGS_gtav_native_shader_report = true;
  ReportShaderCompilation(); assert(test_log_lines == before + 1);
  FLAGS_gtav_native_shader_report = false;
  auto wait = [](auto predicate) {
    for (int n = 0; n < 1000; ++n) {
      if (predicate()) return;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::abort();
  };
  // Cache hit + duplicate must finish exactly once.
  std::ofstream(root / "spirv/ps_0000000000000001.spv") << "synthetic cache marker";
  const uint8_t marker = 0;
  OnShaderCreated(&marker, 1, 1, true); OnShaderCreated(&marker, 1, 1, true);
  wait([] { return g_counts.Read().cached == 1 && g_counts.Read().pending == 0; });
  assert(ShaderOnDisk("ps_0000000000000001"));
  // Disabled compilation with save-missing enabled must not leak pending or count failure.
  FLAGS_gtav_native_compile = false; FLAGS_gtav_native_save_missing_shaders = true;
  OnShaderCreated(&marker, 1, 2, true);
  wait([] { return g_counts.Read().skipped == 1 && g_counts.Read().pending == 0; });
  FLAGS_gtav_native_compile = true;
  // No fake XenosRecomp exists next to this test binary: exercises actual failed-child path.
  OnShaderCreated(&marker, 1, 3, true);
  wait([] { return g_counts.Read().failed == 1 && g_counts.Read().pending == 0; });
  const auto tools = ToolsDir(); assert(!fs::exists(tools)); fs::create_directory(tools);
  std::ofstream(tools / "XenosRecomp") << "#!/bin/sh\nprintf synthetic > \"$2\"\n";
  std::ofstream(tools / "dxc") << "#!/bin/sh\nwhile [ \"$#\" -gt 0 ]; do if [ \"$1\" = -Fo ]; then printf synthetic > \"$2\"; exit 0; fi; shift; done\nexit 1\n";
  for (const auto& name : {"XenosRecomp", "dxc"})
    fs::permissions(tools / name, fs::perms::owner_all);
  OnShaderCreated(&marker, 1, 4, true); OnShaderCreated(&marker, 1, 4, true);
  wait([] { return g_counts.Read().completed == 1 && g_counts.Read().pending == 0; });
  assert(CompileGeneration() == 1 && ShaderOnDisk("ps_0000000000000004"));
  assert(g_counts.Read().cached == 1 && g_counts.Read().skipped == 1 && g_counts.Read().failed == 1);
  fs::remove_all(tools);
  fs::remove_all(root);
  // Production workers are process-lifetime detached threads; avoid static destruction races.
  std::_Exit(0);
}
