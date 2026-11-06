#include "../gtav/src/native/shader_compile_policy.h"
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sys/resource.h>
#include <unistd.h>
using namespace gtav::native;
int main(int argc, char** argv) {
  if (argc == 3) { std::ofstream(argv[2]) << getpriority(PRIO_PROCESS, 0); return 0; }
  for (unsigned cores : {0u, 1u, 8u, 16u, 64u}) assert(ShaderCompilerWorkers(cores, true) == 2);
  assert(ShaderCompilerWorkers(8, false) == 2 && ShaderCompilerWorkers(16, false) == 5);
  ShaderCompileCounters counts;
  counts.Accept(); counts.Accept(); counts.Accept(); counts.Accept();
  assert(counts.Read().pending == 4);
  counts.Finish(ShaderCompileCounters::Outcome::Cached);
  counts.Finish(ShaderCompileCounters::Outcome::Skipped);
  counts.Finish(ShaderCompileCounters::Outcome::Compiled);
  counts.Finish(ShaderCompileCounters::Outcome::Failed);
  auto s = counts.Read();
  assert(s.pending == 0 && s.cached == 1 && s.skipped == 1 && s.completed == 1 && s.failed == 1);
  std::vector<std::thread> threads;
  for (int i = 0; i < 2; ++i) threads.emplace_back([&] {
    for (int n = 0; n < 10000; ++n) { counts.Accept(); counts.Finish(ShaderCompileCounters::Outcome::Compiled); }
  });
  for (auto& thread : threads) thread.join();
  assert(counts.Read().completed == 20001 && counts.Read().pending == 0);
  auto path = std::filesystem::temp_directory_path() /
              ("shader policy " + std::to_string(getpid()) + " '$();.txt");
  const int original = getpriority(PRIO_PROCESS, 0);
  for (bool low : {false, true}) {
    assert(std::system(PosixShaderCommand({std::filesystem::absolute(argv[0]).string(), "--child", path.string()}, low).c_str()) == 0);
    int observed = -99; std::ifstream(path) >> observed;
    std::fprintf(stderr, "low=%d parent nice=%d child nice=%d\n", low, original, observed);
    assert(observed == (low ? std::min(19, original + 5) : original));
    std::filesystem::remove(path);
  }
}
