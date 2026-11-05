#include "../gtav/src/native/frame_metrics.h"
#include <cassert>
#include <limits>
using namespace gtav::native;
int main() {
  FrameMetrics m;
  assert(m.Take().count == 0);
  m.Add(0); m.Add(-1); m.Add(std::numeric_limits<double>::quiet_NaN());
  m.Add(std::numeric_limits<double>::infinity());
  assert(m.Take().count == 0);
  for (int i = 100; i > 0; --i) m.Add(i);
  auto s = m.Take();
  assert(s.count == 100 && s.mean == 50.5 && s.p50 == 50);
  assert(s.p95 == 95 && s.p99 == 99 && s.worst == 100 && s.over50 == 50);
  assert(m.Take().count == 0);
  m.Add(22.2); s = m.Take();
  assert(s.count == 1 && s.p50 == 22.2 && s.p99 == 22.2);
  for (int i = 0; i < 2000; ++i) m.Add(16);
  assert(m.Take().count == 1024);
  UploadRetention r;
  // A loading spike remains until a complete quiet window has passed.
  for (int i = 0; i < 119; ++i) assert(r.Complete(i == 0 ? 5 : 1, 5) == 5);
  assert(r.Complete(1, 5) == 5);
  for (int i = 0; i < 119; ++i) assert(r.Complete(1, 5) == 5);
  assert(r.Complete(1, 5) == 1);
  // Retain renewed demand at the end of a window, and never invent chunks.
  for (int i = 0; i < 119; ++i) r.Complete(1, 5);
  assert(r.Complete(4, 5) == 4);
  UploadRetention empty;
  for (int i = 0; i < 120; ++i) assert(empty.Complete(0, 0) == 0);
}
