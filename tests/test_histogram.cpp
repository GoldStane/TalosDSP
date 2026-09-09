#include "audio/StageHistogram.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

TEST_CASE("StageHistogram records total and bucket counts") {
  StageHistogram h;
  REQUIRE(h.total() == 0);

  h.record(std::chrono::nanoseconds(10));
  h.record(std::chrono::nanoseconds(10));
  h.record(std::chrono::nanoseconds(StageHistogram::kBucketNs * 5));

  REQUIRE(h.total() == 3);
  REQUIRE(h.bucket(0) == 2);
  REQUIRE(h.bucket(5) == 1);
}

TEST_CASE("StageHistogram clamps above-ceiling values into the last bucket") {
  StageHistogram h;
  h.record(std::chrono::nanoseconds(StageHistogram::kMaxNs * 10));
  REQUIRE(h.bucket(StageHistogram::kNumBuckets - 1) == 1);
  REQUIRE(h.total() == 1);
}

TEST_CASE("StageHistogram percentiles are zero with no samples") {
  StageHistogram h;
  REQUIRE(h.medianNs() == 0);
  REQUIRE(h.p99Ns() == 0);
}

TEST_CASE("StageHistogram median lands in the right bucket") {
  StageHistogram h;
  // 100 samples all at bucket 4's center.
  for (int i = 0; i < 100; ++i) {
    h.record(std::chrono::nanoseconds(
        static_cast<long long>(StageHistogram::kBucketNs) * 4 +
        StageHistogram::kBucketNs / 2));
  }
  const std::uint64_t median = h.medianNs();
  REQUIRE(median >= StageHistogram::kBucketNs * 4);
  REQUIRE(median <= StageHistogram::kBucketNs * 5);
}