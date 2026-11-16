// Authored benchmark workloads. No game, GPU backend or production renderer changes.
#import <Foundation/Foundation.h>
#include "../../gtav/src/native/shader_compile_policy.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <unistd.h>
using Clock = std::chrono::steady_clock;
static uint64_t work(uint64_t n, uint64_t x) {
  for (uint64_t i = 0; i < n; ++i) { x ^= x >> 12; x ^= x << 25; x ^= x >> 27; x *= 2685821657736338717ull; }
  return x;
}
static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }
static void resource(std::ostream& out, int who) {
  rusage r{}; getrusage(who, &r);
  out << ",\"cpu_s\":" << r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6 + r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6
      << ",\"peak_rss_bytes\":" << r.ru_maxrss;
}
int main(int argc, char** argv) {
  if (argc < 2) return 2;
  std::cout << std::setprecision(12);
  std::string mode = argv[1];
  if (mode == "env") {
    @autoreleasepool {
      auto p = [NSProcessInfo processInfo];
      std::cout << "{\"thermal\":" << (long)p.thermalState << ",\"low_power\":" << (p.lowPowerModeEnabled ? "true" : "false") << "}\n";
    } return 0;
  }
  if (mode == "calibrate") {
    uint64_t n = 100000, x = 20261105;
    double elapsed = 0;
    while (elapsed < .04) { n *= 2; auto t = Clock::now(); x = work(n, x); elapsed = seconds(Clock::now() - t); }
    std::cout << "{\"iterations\":" << uint64_t(n * .008 / elapsed) << ",\"calibration_s\":" << elapsed << ",\"checksum\":" << x << "}\n";
    return 0;
  }
  if (mode == "launch" && argc >= 6) {
    auto start = Clock::now();
    std::vector<std::string> args(argv + 4, argv + argc);
    int result = std::system(gtav::native::PosixShaderCommand(args, std::string(argv[2]) == "1").c_str());
    std::ofstream out(argv[3]); out << std::setprecision(12) << "{\"system_status\":" << result << ",\"wall_s\":" << seconds(Clock::now() - start);
    resource(out, RUSAGE_CHILDREN); out << "}\n"; return result == 0 ? 0 : 1;
  }
  if (mode == "exec" && argc >= 5) {
    { std::ofstream out(argv[2]); out << "{\"nice\":" << getpriority(PRIO_PROCESS, 0) << ",\"pid\":" << getpid() << "}\n"; }
    execvp(argv[3], argv + 3); return 127;
  }
  if (mode == "burn" && argc == 5) {
    const double duration = std::stod(argv[2]), duty = std::stod(argv[3]);
    if (duration <= 0 || duration > 180 || duty <= 0 || duty > 1) return 2;
    const auto start = Clock::now(); uint64_t checksum = 20261105, chunks = 0;
    { std::ofstream out(argv[4]); out << "{\"nice\":" << getpriority(PRIO_PROCESS, 0) << ",\"pid\":" << getpid() << "}\n"; }
    while (seconds(Clock::now() - start) < duration) {
      auto phase = Clock::now();
      while (seconds(Clock::now() - phase) < duty) { checksum = work(100000, checksum); ++chunks; }
      if (duty < 1) std::this_thread::sleep_until(phase + std::chrono::seconds(1));
    }
    std::ostringstream result; result << std::setprecision(12) << "{\"chunks\":" << chunks << ",\"checksum\":" << checksum << ",\"wall_s\":" << seconds(Clock::now() - start);
    resource(result, RUSAGE_SELF); result << "}\n";
    std::ofstream(std::string(argv[4])+".result") << result.str();
    std::cout << result.str(); return 0;
  }
  if (mode == "foreground" && argc == 7) {
    const double duration = std::stod(argv[2]); const uint64_t iterations = std::stoull(argv[3]);
    const bool instrument = std::string(argv[4]) == "1";
    const int injected_stall = std::stoi(argv[6]);
    if (duration <= 0 || duration > 120 || iterations == 0 || iterations > 100000000) return 2;
    struct Sample { double target, wake, finish; };
    std::vector<Sample> samples; if (instrument) samples.reserve(4000);
    const auto start = Clock::now(); const double period = 1.0 / 30;
    uint64_t checksum = 20261105, frames = 0, skipped = 0, slot = 0;
    while (double(slot) * period < duration) {
      const double target = double(slot) * period;
      std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(target)));
      auto wake = Clock::now(); checksum = work(iterations, checksum);
      if (injected_stall && frames == 10) std::this_thread::sleep_for(std::chrono::milliseconds(injected_stall));
      auto finish = Clock::now(); ++frames;
      if (instrument) samples.push_back({target, seconds(wake - start), seconds(finish - start)});
      ++slot;
      while (double(slot) * period < seconds(finish - start) && double(slot) * period < duration) { ++slot; ++skipped; }
    }
    { std::ofstream csv(argv[5]); csv << std::setprecision(12) << "target_s,wake_s,finish_s\n";
      for (auto s : samples) csv << s.target << ',' << s.wake << ',' << s.finish << '\n'; }
    std::cout << "{\"frames\":" << frames << ",\"skipped_slots\":" << skipped << ",\"checksum\":" << checksum
              << ",\"wall_s\":" << seconds(Clock::now() - start) << ",\"nice\":" << getpriority(PRIO_PROCESS, 0);
    resource(std::cout, RUSAGE_SELF); std::cout << "}\n"; return 0;
  }
  return 2;
}
