// Small owned-process probe used during preparation; not timed matrix data.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
int main() {
  auto start = std::chrono::steady_clock::now();
  std::thread a([] { std::system("sleep 1"); });
  std::thread b([] { std::system("sleep 1"); });
  a.join(); b.join();
  std::printf("two system(sleep 1) calls: %.3f seconds\n",
              std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
}
