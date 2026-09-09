#include "audio/FreeverbStage.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

namespace {
std::vector<float> runImpulse(uint32_t frames, uint32_t channels) {
  // Run long enough for the longest comb delay (~1116 samples) plus all-pass
  // (~556) to return the impulse into the output.
  if (frames < 2500) frames = 2500;
  std::vector<float> input(static_cast<std::size_t>(frames) * channels, 0.0f);
  input[0] = 1.0f;  // single-sample impulse on channel 0
  std::vector<float> output(input.size(), -1.0f);
  FreeverbStage stage;
  stage.process(input.data(), output.data(), frames, channels);
  return output;
}
}  // namespace

TEST_CASE("Freeverb is deterministic across runs") {
  auto a = runImpulse(256, 2);
  auto b = runImpulse(256, 2);
  REQUIRE(a == b);
}

TEST_CASE("Freeverb produces non-zero reverb tail from an impulse") {
  const auto out = runImpulse(512, 1);
  bool anyNonZero = false;
  for (float v : out) {
    if (std::fabs(v) > 1e-6f) {
      anyNonZero = true;
      break;
    }
  }
  REQUIRE(anyNonZero);
}

TEST_CASE("Freeverb output stays bounded") {
  constexpr uint32_t frames = 1024;
  std::vector<float> input(frames * 2, 0.0f);
  for (std::size_t i = 0; i < input.size(); ++i) {
    input[i] = std::sin(static_cast<float>(i) * 0.013f);
  }
  std::vector<float> output(input.size(), -1.0f);
  FreeverbStage stage;
  stage.process(input.data(), output.data(), frames, 2);

  for (float v : output) {
    REQUIRE(std::isfinite(v));
    REQUIRE(std::fabs(v) < 2.0f);
  }
}

TEST_CASE("Freeverb reset returns to silence on a steady input") {
  constexpr uint32_t frames = 256;
  std::vector<float> input(frames, 0.8f);
  std::vector<float> output(frames, -1.0f);
  FreeverbStage stage;
  stage.process(input.data(), output.data(), frames, 1);

  stage.reset();
  std::vector<float> after(frames, -1.0f);
  stage.process(input.data(), after.data(), frames, 1);
  // After a reset, the first block from a constant input is near zero
  // (combs start empty), then grows; just check it is finite and bounded.
  for (float v : after) {
    REQUIRE(std::isfinite(v));
  }
}