#include "audio/StageHistogram.h"

#include <cmath>

std::uint64_t StageHistogram::medianNs() const noexcept {
  const std::uint64_t total = total_.load(std::memory_order_relaxed);
  if (total == 0) return 0;
  const std::uint64_t target = total / 2;
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < kNumBuckets; ++i) {
    cumulative += buckets_[i].load(std::memory_order_relaxed);
    if (cumulative >= target) {
      // Bucket center as a point estimate.
      return (i * kBucketNs) + kBucketNs / 2;
    }
  }
  return kMaxNs;
}

std::uint64_t StageHistogram::p99Ns() const noexcept {
  const std::uint64_t total = total_.load(std::memory_order_relaxed);
  if (total == 0) return 0;
  const std::uint64_t target =
      static_cast<std::uint64_t>(std::ceil(static_cast<double>(total) * 0.99));
  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < kNumBuckets; ++i) {
    cumulative += buckets_[i].load(std::memory_order_relaxed);
    if (cumulative >= target) {
      return (i * kBucketNs) + kBucketNs / 2;
    }
  }
  return kMaxNs;
}