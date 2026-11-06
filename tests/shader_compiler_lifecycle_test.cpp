// Production intake, duplicate suppression, promotion and worker paths with fake tools.
// No GPU, game libraries, game shaders or installed files.
#include "../gtav/src/native/shader_compiler.cpp"
#include <cassert>
#include <chrono>
#include <numeric>
#include <random>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
unsigned ThreadCount() {
  thread_act_array_t threads; mach_msg_type_number_t count;
  assert(task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS);
  for (unsigned i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(threads), count * sizeof(thread_t));
  return count;
}
#endif
using namespace gtav::native;
int main() {
#ifdef __APPLE__
  const unsigned initial_threads = ThreadCount();
#endif
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
    for (int n = 0; n < 6000; ++n) {
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
  // Burst/duplicate/mixed-outcome stress through the actual production intake and workers.
  // Each round submits 200 unique markers twice, shuffled reproducibly; no real shaders.
  std::ofstream stress_tool(tools / "XenosRecomp");
  stress_tool << "#!/bin/sh\n";
  if (kShaderDesktopMac) {
    stress_tool << "base=$(dirname \"$0\")\n"
        "if mkdir \"$base/active1\" 2>/dev/null; then slot=\"$base/active1\"; "
        "elif mkdir \"$base/active2\" 2>/dev/null; then slot=\"$base/active2\"; touch \"$base/peak-two\"; "
        "else touch \"$base/too-many\"; exit 1; fi\n"
        "trap 'rmdir \"$slot\"' EXIT\nsleep .01\n";
  }
  stress_tool << "case \"$1\" in *F.bin) exit 1;; esac\nprintf synthetic > \"$2\"\n";
  stress_tool.close();
  std::mt19937 rng(20261105);
  for (int round = 0; round < 3; ++round) {
    std::vector<uint64_t> hashes(200);
    std::iota(hashes.begin(), hashes.end(), uint64_t(1000 + round * 200));
    std::shuffle(hashes.begin(), hashes.end(), rng);
    auto prior = g_counts.Read(); uint64_t cached = 0, failed = 0, completed = 0;
    for (auto hash : hashes) {
      const auto name = fmt::format("{}_{:016X}", "ps", hash);
      if (hash % 3 == 0) { std::ofstream(root / "spirv" / (name + ".spv")) << "synthetic"; ++cached; }
      else if ((hash & 15) == 15) ++failed;
      else ++completed;
      PrioritizeShader(hash, true); PrioritizeShader(hash, true);
      OnShaderCreated(&marker, 1, hash, true); OnShaderCreated(&marker, 1, hash, true);
    }
    wait([&] {
      auto now = g_counts.Read();
      return now.pending == 0 && now.cached == prior.cached + cached &&
             now.failed == prior.failed + failed && now.completed == prior.completed + completed;
    });
    assert(g_counts.Read().skipped == 1);
  }
  if (kShaderDesktopMac) {
    assert(!fs::exists(tools / "too-many"));
#ifdef __APPLE__
    // Two compiler threads plus the intake thread. OS scheduling need not make
    // two particular short subprocesses overlap, so do not assert that timing.
    assert(ThreadCount() == initial_threads + 3);
#endif
  }
  fs::remove_all(tools);
  fs::remove_all(root);
  // Production workers are process-lifetime detached threads; avoid static destruction races.
  std::_Exit(0);
}
