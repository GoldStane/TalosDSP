#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>

// Lock-free per-stage cost histogram. The RT thread calls record() once per
// stage invocation with the elapsed wall time; a control thread later reads
// total()/bucket()/medianNs()/p99Ns() (off the RT path) to report cost.
//
// Fixed bucket count, no allocation, no locking. Buckets cover [0, kMaxNs);
// anything above the ceiling lands in the last bucket.
class StageHistogram {
 public:
  static constexpr std::size_t kNumBuckets = 32;
  static constexpr std::uint64_t kMaxNs = 2'000'000;  // 2 ms ceiling
  static constexpr std::uint64_t kBucketNs = kMaxNs / kNumBuckets;

  void record(std::chrono::nanoseconds dt) noexcept {
    std::uint64_t ns = static_cast<std::uint64_t>(dt.count());
    std::size_t idx = ns / kBucketNs;
    if (idx >= kNumBuckets) idx = kNumBuckets - 1;
    buckets_[idx].fetch_add(1, std::memory_order_relaxed);
    total_.fetch_add(1, std::memory_order_relaxed);
  }

  std::uint64_t total() const noexcept {
    return total_.load(std::memory_order_relaxed);
  }

  std::uint64_t bucket(std::size_t i) const noexcept {
    return buckets_[i].load(std::memory_order_relaxed);
  }

  // Off-RT helpers: bucket-mean estimates of percentiles.
  std::uint64_t medianNs() const noexcept;
  std::uint64_t p99Ns() const noexcept;

 private:
  std::array<std::atomic<std::uint64_t>, kNumBuckets> buckets_{};
  std::atomic<std::uint64_t> total_{0};
};