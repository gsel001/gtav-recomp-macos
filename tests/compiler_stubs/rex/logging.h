#pragma once
#include <cstdio>
#include <atomic>
#include <string>
inline std::atomic<unsigned> test_log_lines{0};
template <typename... Args> void TestLog(const char*, const Args&...) { ++test_log_lines; }
#define REXLOG_INFO(...) TestLog(__VA_ARGS__)
#define REXLOG_WARN(...) TestLog(__VA_ARGS__)
namespace fmt {
inline std::string format(const char*, const char* prefix, uint64_t hash) {
  char buffer[64]; std::snprintf(buffer, sizeof(buffer), "%s_%016llX", prefix, static_cast<unsigned long long>(hash));
  return buffer;
}
}
