#include "audio/PassthroughCallback.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

TEST_CASE("Passthrough copies input to output exactly") {
  constexpr uint32_t frames = 64;
  constexpr uint32_t channels = 2;
  std::vector<float> input(static_cast<std::size_t>(frames) * channels);
  for (std::size_t i = 0; i < input.size(); ++i) {
    input[i] = std::sin(static_cast<float>(i) * 0.01f);
  }

  std::vector<float> output(input.size(), -1.0f);
  PassthroughCallback cb;
  cb.process(input.data(), output.data(), frames, channels);

  REQUIRE(output == input);
}

TEST_CASE("Passthrough writes silence when input is missing") {
  constexpr uint32_t frames = 32;
  constexpr uint32_t channels = 1;
  std::vector<float> output(frames * channels, 1.0f);

  PassthroughCallback cb;
  cb.process(nullptr, output.data(), frames, channels);

  for (float v : output) {
    REQUIRE(v == 0.0f);
  }
}

TEST_CASE("Passthrough is a no-op for zero frames or channels") {
  std::vector<float> input = {1.0f, 2.0f, 3.0f};
  std::vector<float> output = {-1.0f, -1.0f, -1.0f};

  PassthroughCallback cb;
  cb.process(input.data(), output.data(), 0, 1);
  cb.process(input.data(), output.data(), 3, 0);

  for (float v : output) {
    REQUIRE(v == -1.0f);
  }
}

TEST_CASE("Passthrough is deterministic across calls") {
  constexpr uint32_t frames = 48;
  constexpr uint32_t channels = 1;
  std::vector<float> input(frames, 0.5f);
  std::vector<float> a(frames, -1.0f);
  std::vector<float> b(frames, -1.0f);

  PassthroughCallback cb;
  cb.process(input.data(), a.data(), frames, channels);
  cb.process(input.data(), b.data(), frames, channels);

  REQUIRE(a == b);
  REQUIRE(a == input);
}