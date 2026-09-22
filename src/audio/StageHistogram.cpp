#include "audio/StageHistogram.h"
#include <cmath>
std::uint64_t StageHistogram::medianNs() const noexcept { return percentile(.5); }
std::uint64_t StageHistogram::p99Ns() const noexcept { return percentile(.99); }
std::uint64_t StageHistogram::percentile(double q) const noexcept {
  const auto n=total();
  if (!n) return 0;
  const auto rank=static_cast<std::uint64_t>(std::ceil(q*static_cast<double>(n)));
  std::uint64_t cumulative=0;
  for (std::size_t i=0;i<kNumBuckets;++i) {
    cumulative+=bucket(i);
    if (cumulative>=rank) {
      // An overflowed final bucket is a lower bound, not a fake exact latency.
      if (i==kNumBuckets-1 && overflow()) return kMaxNs;
      return i*kBucketNs+kBucketNs/2;
    }
  }
  return kMaxNs;
}
