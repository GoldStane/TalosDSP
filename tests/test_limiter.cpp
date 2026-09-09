#include "audio/LimiterStage.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <vector>

TEST_CASE("Limiter identity at zero and soft-clips large signals") {
  LimiterStage lim;
  std::vector<float> input = {0.0f, 0.5f, 100.0f, -100.0f};
  std::vector<float> output(input.size(), -1.0f);
  lim.process(input.data(), output.data(),
              static_cast<uint32_t>(input.size()), 1);

  REQUIRE(output[0] == Catch::Approx(0.0f));
  REQUIRE(output[1] == Catch::Approx(std::tanhf(0.5f)));
  REQUIRE(output[2] == Catch::Approx(1.0f));   // clamped to ceiling
  REQUIRE(output[3] == Catch::Approx(-1.0f));  // clamped to ceiling
}

TEST_CASE("Limiter never exceeds the ceiling for arbitrary input") {
  constexpr uint32_t frames = 2000;
  std::vector<float> input(frames);
  for (std::size_t i = 0; i < input.size(); ++i) {
    input[i] = std::sin(static_cast<float>(i) * 3.1f) * 1e4f;
  }
  std::vector<float> output(frames, -1.0f);
  LimiterStage lim;
  lim.process(input.data(), output.data(), frames, 1);

  for (float v : output) {
    REQUIRE(std::isfinite(v));
    REQUIRE(v >= -1.0f);
    REQUIRE(v <= 1.0f);
  }
}

TEST_CASE("Limiter makeup gain scales before clipping") {
  LimiterStage lim;
  lim.setMakeupGain(0.0f);
  std::vector<float> output(1, -1.0f);
  const float in = 0.5f;
  lim.process(&in, output.data(), 1, 1);
  REQUIRE(output[0] == Catch::Approx(0.0f));
}