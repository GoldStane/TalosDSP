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
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "RT counters require lock-free atomics");
 public:
  static constexpr std::size_t kNumBuckets = 4096;
  static constexpr std::uint64_t kMaxNs = 4'096'000;  // 1 us buckets, overflow explicitly counted
  static constexpr std::uint64_t kBucketNs = kMaxNs / kNumBuckets;

  void record(std::chrono::nanoseconds dt) noexcept {
    const std::uint64_t ns = dt.count() < 0 ? 0 : static_cast<std::uint64_t>(dt.count());
    // One RT writer: bounded store, no compare/exchange retry loop.
    if (ns > maximum_.load(std::memory_order_relaxed)) maximum_.store(ns, std::memory_order_relaxed);
    if (ns >= kMaxNs) overflow_.fetch_add(1, std::memory_order_relaxed);
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
  std::uint64_t p999Ns() const noexcept { return percentile(.999); }
  std::uint64_t maxNs() const noexcept { return maximum_.load(); }
  std::uint64_t overflow() const noexcept { return overflow_.load(); }

 private:
  std::uint64_t percentile(double q) const noexcept;
  std::atomic<std::uint64_t> maximum_{0}, overflow_{0};
  std::array<std::atomic<std::uint64_t>, kNumBuckets> buckets_{};
  std::atomic<std::uint64_t> total_{0};
};