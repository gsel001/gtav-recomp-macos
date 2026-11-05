// Bounded CPU-side timing samples: present calls, not on-screen display timestamps.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace gtav::native {
class FrameMetrics {
 public:
  struct Summary {
    size_t count = 0, over50 = 0;
    double mean = 0, p50 = 0, p95 = 0, p99 = 0, worst = 0;
  };
  void Add(double ms) {
    if (std::isfinite(ms) && ms > 0 && count_ < samples_.size()) samples_[count_++] = ms;
  }
  Summary Take() {
    Summary s;
    s.count = count_;
    if (!count_) return s;
    std::sort(samples_.begin(), samples_.begin() + count_);
    for (size_t i = 0; i < count_; ++i) {
      s.mean += samples_[i];
      s.over50 += samples_[i] > 50;
    }
    s.mean /= count_;
    auto percentile = [&](size_t percent) { return samples_[(count_ * percent + 99) / 100 - 1]; };
    s.p50 = percentile(50); s.p95 = percentile(95); s.p99 = percentile(99);
    s.worst = samples_[count_ - 1];
    count_ = 0;
    return s;
  }
 private:
  std::array<double, 1024> samples_{};
  size_t count_ = 0;
};

// Observe only after the slot's GPU fence completes. Retain each window's largest demand.
// A released trailing chunk was unused for the whole window, avoiding per-frame churn.
class UploadRetention {
 public:
  size_t Complete(size_t used, size_t capacity) {
    peak_ = std::max(peak_, used);
    if (++frames_ < 120) return capacity;
    const size_t keep = std::min(capacity, std::max(size_t{1}, peak_));
    peak_ = frames_ = 0;
    return keep;
  }
 private:
  size_t peak_ = 0, frames_ = 0;
};
}  // namespace gtav::native
