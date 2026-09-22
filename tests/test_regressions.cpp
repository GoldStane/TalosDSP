#include "audio/DspChain.h"
#include <catch2/catch_test_macros.hpp>
#include <vector>
#include <cmath>
#include <thread>

TEST_CASE("Chain handles missing input and writes every large-block sample") {
  DspChain chain;
  std::vector<float> output(18000, 123.0f);
  chain.process(nullptr, output.data(), 9000, 2);
  for (float v : output) REQUIRE(v == 0.0f);
  chain.process(nullptr, nullptr, 64, 1);
}

TEST_CASE("Presets remain audible at the same complexity") {
  DspChain a, b;
  a.setManualPreset(0); b.setManualPreset(2);
  std::vector<float> input(9000, 0.2f), x(9000), y(9000);
  a.process(input.data(), x.data(), 9000, 1);
  b.process(input.data(), y.data(), 9000, 1);
  REQUIRE(x != y);
  a.reset();
  std::vector<float> reset(9000);
  a.process(input.data(), reset.data(), 9000, 1);
  REQUIRE(reset == x);
}

TEST_CASE("Stereo features do not leak into a silent channel") {
  FeatureExtractor f; f.init(44100);
  std::vector<float> input(2048);
  for (int i = 0; i < 1024; ++i) input[2*i] = std::sin(static_cast<float>(i)*0.1f);
  f.process(input.data(), 1024, 2);
  AudioFeatures features;
  REQUIRE(f.finalize(features));
  REQUIRE(features.rms_ch[1] == 0.0f);
  for (float e : features.band_energy_ch[1]) REQUIRE(e == 0.0f);
}

TEST_CASE("Watchdog responds to recent timing and honors manual mode") {
  std::atomic<uint8_t> level{128};
  ComplexityController controller(&level, 0.5f, 0, 0);
  for (int i=0; i<100; ++i) controller.record(900, 1000);
  controller.poll();
  REQUIRE(level.load() < 128);
  controller.setManualOverride(true);
  const auto previous = level.load();
  controller.record(0, 1000); controller.poll();
  REQUIRE(level.load() == previous);
}

TEST_CASE("Classifier thread consumes features and writes supplied preset") {
  SPSCRingBuffer<AudioFeatures, 64> queue;
  std::atomic<uint8_t> preset{1};
  Classifier classifier(&queue, &preset);
  AudioFeatures features; features.zcr = 0.5f; features.centroid = 8000;
  REQUIRE(queue.try_push(features));
  classifier.start();
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (preset.load() == 1 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  classifier.stop();
  REQUIRE(preset.load() == 0);
}

TEST_CASE("Feature extractor retains non-aligned block samples") {
  FeatureExtractor f; f.init(48000, 1024);
  std::vector<float> input(1200, 1.0f);
  f.process(input.data(), 600, 1); f.process(input.data()+600, 600, 1);
  REQUIRE(f.framesAccumulated() == 1200);
  AudioFeatures features; REQUIRE(f.finalize(features));
  REQUIRE(features.rms == 1.0f);
}

TEST_CASE("Chain classifier handoff preserves partial feature windows") {
  DspChain chain;
  chain.setSampleRate(44100);
  chain.startClassifier();
  std::vector<float> input(1200, 0.0f), output(1200);
  chain.process(input.data(), output.data(), 1200, 1);
  REQUIRE(chain.featureExtractor().framesAccumulated() == 176);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (chain.preset().load() == 1 && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  chain.stopClassifier();
  REQUIRE(chain.preset().load() == static_cast<uint8_t>(Preset::AMBIENT));
}

TEST_CASE("LR4 lowpass has half amplitude at crossover") {
  Filterbank bank; bank.init(48000);
  double inputEnergy = 0, outputEnergy = 0;
  for (int i = 0; i < 8192; ++i) {
    const float x = std::sin(static_cast<float>(i) * 2.0f * 3.14159265359f * 1500.0f / 48000.0f);
    const float y = bank.process(x)[0];
    if (i >= 4096) { inputEnergy += x*x; outputEnergy += y*y; }
  }
  REQUIRE(std::abs(std::sqrt(outputEnergy/inputEnergy) - 0.5) < 0.001);
}

TEST_CASE("Histogram median includes the only observation") {
  StageHistogram histogram;
  histogram.record(std::chrono::nanoseconds(StageHistogram::kBucketNs * 5));
  REQUIRE(histogram.medianNs() >= StageHistogram::kBucketNs * 5);
}
