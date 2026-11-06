// Standalone investigation only. Not linked into, installed with, or used by GTA.
#include "shader_compile_policy.h"
#include <spawn.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

extern char** environ;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
void check(bool ok, const char* name) { if (!ok) throw std::runtime_error(name); }
std::string hex(const std::string& value) {
  std::ostringstream out;
  for (unsigned char c : value) out << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
  return out.str();
}
bool null_fd(int fd) {
  struct stat a{}, b{};
  return fstat(fd, &a) == 0 && stat("/dev/null", &b) == 0 && S_ISCHR(a.st_mode) && a.st_rdev == b.st_rdev;
}
struct Receipt {
  double start{}, end{}; int nice{}, out{}, err{}, environment{};
  std::vector<std::string> args;
};
Receipt read_receipt(const fs::path& path) {
  Receipt r; std::ifstream f(path); check(bool(f), "missing receipt");
  f >> r.start >> r.end >> r.nice >> r.out >> r.err >> r.environment;
  std::string line; std::getline(f, line);
  while (std::getline(f, line)) r.args.push_back(line);
  return r;
}
struct Result { int error = 0, status = -1; bool timeout = false; long peak_rss = 0; };
Result direct(std::vector<std::string> args, bool low, int timeout_ms = 1500) {
  Result result;
  if (args.empty()) { result.error = EINVAL; return result; }
  if (low) args.insert(args.begin(), {"/usr/bin/nice", "-n", "5"});
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  posix_spawn_file_actions_t actions;
  result.error = posix_spawn_file_actions_init(&actions);
  if (result.error) return result;
  result.error = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  if (!result.error) result.error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
  pid_t pid = -1;
  if (!result.error) result.error = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (result.error) return result;
  const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
  rusage usage{};
  for (;;) {
    pid_t waited = wait4(pid, &result.status, WNOHANG, &usage);
    if (waited == pid) break;
    if (waited == -1 && errno != EINTR) { result.error = errno; return result; }
    if (Clock::now() >= deadline) {
      result.timeout = true;
      kill(pid, SIGKILL);  // only this probe's directly owned child, never game/system processes
      do { waited = wait4(pid, &result.status, 0, &usage); } while (waited == -1 && errno == EINTR);
      if (waited != pid) result.error = errno;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  result.peak_rss = usage.ru_maxrss;
  return result;
}
bool success(Result r) { return !r.error && !r.timeout && WIFEXITED(r.status) && WEXITSTATUS(r.status) == 0; }

int main(int argc, char** argv) {
  if (argc >= 5 && std::string(argv[1]) == "--child") {
    double begin = now();
    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[3])));
    std::ofstream receipt(argv[2]);
    const char* token = std::getenv("GTAV_SPAWN_PROBE_TOKEN");
    receipt << std::setprecision(17) << begin << ' ' << now() << ' ' << getpriority(PRIO_PROCESS, 0)
            << ' ' << null_fd(1) << ' ' << null_fd(2) << ' ' << (token && std::string(token) == "owned-only") << '\n';
    for (int i = 0; i < argc; ++i) receipt << hex(argv[i]) << '\n';
    receipt.close();
    std::cout << "stdout must be redirected\n";
    std::cerr << "stderr must be redirected\n";
    if (std::string(argv[4]) == "signal") { raise(SIGTERM); return 99; }
    return std::stoi(argv[4]);
  }
  fs::path root = fs::temp_directory_path() / ("gtav-owned-spawn-probe-" + std::to_string(getpid()));
  try {
    fs::create_directory(root);
    fs::permissions(root, fs::perms::owner_all);
    auto tool = root / "tool 'with spaces;$";
    fs::create_symlink(fs::canonical(argv[0]), tool);
    setenv("GTAV_SPAWN_PROBE_TOKEN", "owned-only", 1);
    int parent_nice = getpriority(PRIO_PROCESS, 0);
    auto args = [&](std::string name, int ms = 1, std::string code = "0") {
      return std::vector<std::string>{tool.string(), "--child", (root/name).string(), std::to_string(ms), code,
                                      "space quote'\" ; $() `literal`", "line\nbreak", "UTF-8: café"};
    };
    auto literal = args("literal");
    check(success(direct(literal, false)), "literal argv launch");
    auto receipt = read_receipt(root/"literal");
    std::vector<std::string> expected;
    for (const auto& arg : literal) expected.push_back(hex(arg));
    check(receipt.args == expected && receipt.nice == parent_nice && receipt.out && receipt.err && receipt.environment,
          "argv/environment/redirection/inherited nice");
    check(success(direct(args("nice"), true)), "nice wrapper launch");
    check(read_receipt(root/"nice").nice == parent_nice + 5 && getpriority(PRIO_PROCESS, 0) == parent_nice,
          "child nice or parent priority changed");
    auto nonzero = direct(args("nonzero", 1, "7"), false);
    check(!success(nonzero) && WIFEXITED(nonzero.status) && WEXITSTATUS(nonzero.status) == 7, "exit 7");
    auto signaled = direct(args("signal", 1, "signal"), false);
    check(!success(signaled) && WIFSIGNALED(signaled.status) && WTERMSIG(signaled.status) == SIGTERM, "signal status");
    check(direct({(root/"missing").string()}, false).error == ENOENT, "missing executable");
    std::ofstream(root/"not-executable") << "owned test fixture";
    check(direct({(root/"not-executable").string()}, false).error == EACCES, "non executable");
    auto timeout = direct(args("timeout", 2000), false, 100);
    check(timeout.timeout && WIFSIGNALED(timeout.status), "bounded child timeout");
    check(success(direct(args("recovery"), false)), "recovery after failed launch");
    double begin = now();
    auto a = std::async(std::launch::async, [&] { return direct(args("direct-a", 200), false); });
    auto b = std::async(std::launch::async, [&] { return direct(args("direct-b", 200), false); });
    Result ar = a.get(), br = b.get(); check(success(ar) && success(br), "two direct owned children");
    double direct_wall = now()-begin;
    auto ra = read_receipt(root/"direct-a"), rb = read_receipt(root/"direct-b");
    bool direct_overlap = std::max(ra.start, rb.start) < std::min(ra.end, rb.end);
    begin = now();
    auto c = std::async(std::launch::async, [&] { return std::system(gtav::native::PosixShaderCommand(args("system-a", 200), false).c_str()); });
    auto d = std::async(std::launch::async, [&] { return std::system(gtav::native::PosixShaderCommand(args("system-b", 200), false).c_str()); });
    check(c.get() == 0 && d.get() == 0, "two system owned children");
    double system_wall = now()-begin;
    auto rc = read_receipt(root/"system-a"), rd = read_receipt(root/"system-b");
    bool system_overlap = std::max(rc.start, rd.start) < std::min(rc.end, rd.end);
    std::cout << std::setprecision(9) << "{\"status\":\"PASS\",\"semantic_checks\":8,\"max_owned_children\":2,"
              << "\"direct_child_intervals_overlap\":" << (direct_overlap ? "true" : "false")
              << ",\"system_child_intervals_overlap\":" << (system_overlap ? "true" : "false")
              << ",\"direct_two_child_wall_s\":" << direct_wall << ",\"system_two_child_wall_s\":" << system_wall
              << ",\"max_direct_child_rss_bytes\":" << std::max(ar.peak_rss, br.peak_rss)
              << ",\"parent_nice\":" << parent_nice << ",\"actual_low_priority_child_nice\":" << read_receipt(root/"nice").nice
              << ",\"not_gameplay_evidence\":true}\n";
    fs::remove_all(root);  // owned synthetic fixture directory only
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << "; fixtures retained at " << root << '\n';
    return 1;
  }
}
