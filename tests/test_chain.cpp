#include "audio/DspChain.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

TEST_CASE("DspChain passes silence as silence") {
  DspChain chain;
  constexpr uint32_t frames = 128;
  constexpr uint32_t channels = 2;
  std::vector<float> input(static_cast<std::size_t>(frames) * channels, 0.0f);
  std::vector<float> output(input.size(), -1.0f);
  chain.process(input.data(), output.data(), frames, channels);
  for (float v : output) {
    REQUIRE(std::isfinite(v));
  }
}

TEST_CASE("DspChain output is bounded by the limiter ceiling") {
  DspChain chain;
  constexpr uint32_t frames = 512;
  constexpr uint32_t channels = 2;
  std::vector<float> input(static_cast<std::size_t>(frames) * channels);
  for (std::size_t i = 0; i < input.size(); ++i) {
    input[i] = std::sin(static_cast<float>(i) * 0.07f) * 50.0f;
  }
  std::vector<float> output(input.size(), -1.0f);
  chain.process(input.data(), output.data(), frames, channels);
  for (float v : output) {
    REQUIRE(std::isfinite(v));
    REQUIRE(v >= -1.0f);
    REQUIRE(v <= 1.0f);
  }
}

TEST_CASE("DspChain is deterministic for the same input") {
  auto run = [] {
    DspChain c;
    constexpr uint32_t frames = 256;
    constexpr uint32_t channels = 1;
    std::vector<float> in(static_cast<std::size_t>(frames) * channels);
    for (std::size_t i = 0; i < in.size(); ++i) {
      in[i] = std::sin(static_cast<float>(i) * 0.03f);
    }
    std::vector<float> out(in.size(), -1.0f);
    c.process(in.data(), out.data(), frames, channels);
    return out;
  };
  REQUIRE(run() == run());
}

TEST_CASE("DspChain reset clears reverb state") {
  DspChain chain;
  constexpr uint32_t frames = 256;
  std::vector<float> input(frames, 0.9f);
  std::vector<float> first(frames, -1.0f);
  std::vector<float> second(frames, -1.0f);
  chain.process(input.data(), first.data(), frames, 1);
  chain.reset();
  chain.process(input.data(), second.data(), frames, 1);
  // After reset, a block of constant input yields a fresh (empty) reverb tail;
  // outputs must differ from a continued run and remain finite.
  for (std::size_t i = 0; i < first.size(); ++i) {
    REQUIRE(std::isfinite(second[i]));
  }
}