// Small, dependency-free policies used by the production background compiler.
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace gtav::native {
inline unsigned ShaderCompilerWorkers(unsigned hardware_threads, bool desktop_mac) {
  return desktop_mac ? 2u : std::max(2u, hardware_threads / 3);
}

inline std::string PosixShaderCommand(const std::vector<std::string>& args, bool low_priority) {
  std::string command = low_priority ? "/usr/bin/nice -n 5 " : "";
  for (const auto& arg : args) {
    command += '\'';
    for (char c : arg) command += c == '\'' ? "'\\''" : std::string(1, c);
    command += "' ";
  }
  return command + ">/dev/null 2>&1";
}

// Counters cover unique accepted shaders, including intake. No names, sorting,
// disk reads or extra mutex. Snapshots are approximate while transitions run.
// Cheap updates stay active so enabling reporting mid-session remains meaningful.
class ShaderCompileCounters {
 public:
  enum class Outcome { Compiled, Failed, Cached, Skipped };
  struct Snapshot { uint64_t pending, completed, failed, cached, skipped; };
  void Accept() { pending_.fetch_add(1, std::memory_order_relaxed); }
  void Finish(Outcome outcome) {
    auto* count = outcome == Outcome::Compiled ? &completed_ :
                  outcome == Outcome::Failed ? &failed_ :
                  outcome == Outcome::Cached ? &cached_ : &skipped_;
    count->fetch_add(1, std::memory_order_relaxed);
    pending_.fetch_sub(1, std::memory_order_relaxed);
  }
  Snapshot Read() const {
    return {pending_.load(std::memory_order_relaxed), completed_.load(std::memory_order_relaxed),
            failed_.load(std::memory_order_relaxed), cached_.load(std::memory_order_relaxed),
            skipped_.load(std::memory_order_relaxed)};
  }
 private:
  std::atomic<uint64_t> pending_{0}, completed_{0}, failed_{0}, cached_{0}, skipped_{0};
};
}  // namespace gtav::native
