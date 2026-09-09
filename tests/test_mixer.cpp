#include "audio/MixerStage.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstddef>
#include <vector>

TEST_CASE("Mixer crossfade follows the wet amount") {
  MixerStage mixer;
  constexpr uint32_t frames = 16;
  std::vector<float> dry(frames, 1.0f);
  std::vector<float> wet(frames, 0.0f);
  std::vector<float> out(frames, -1.0f);

  mixer.setWet(0.0f);
  mixer.mix(dry.data(), wet.data(), out.data(), frames, 1);
  for (float v : out) REQUIRE(v == Catch::Approx(1.0f));

  mixer.setWet(1.0f);
  mixer.mix(dry.data(), wet.data(), out.data(), frames, 1);
  for (float v : out) REQUIRE(v == Catch::Approx(0.0f));

  mixer.setWet(0.25f);
  mixer.mix(dry.data(), wet.data(), out.data(), frames, 1);
  for (float v : out) REQUIRE(v == Catch::Approx(0.75f));
}

TEST_CASE("Mixer wet amount clamps to [0,1]") {
  MixerStage mixer;
  mixer.setWet(2.0f);
  REQUIRE(mixer.wetAmount() == Catch::Approx(1.0f));
  mixer.setWet(-1.0f);
  REQUIRE(mixer.wetAmount() == Catch::Approx(0.0f));
}

TEST_CASE("Mixer respects channel count") {
  MixerStage mixer;
  constexpr uint32_t frames = 4;
  constexpr uint32_t channels = 2;
  std::vector<float> dry(static_cast<std::size_t>(frames) * channels, 1.0f);
  std::vector<float> wet(static_cast<std::size_t>(frames) * channels, 0.3f);
  std::vector<float> out(dry.size(), -1.0f);
  mixer.setWet(0.5f);
  mixer.mix(dry.data(), wet.data(), out.data(), frames, channels);
  for (float v : out) REQUIRE(v == Catch::Approx(0.65f));
}