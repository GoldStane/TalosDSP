#include "audio/FreeverbStage.h"

#include <algorithm>
#include <cstddef>

namespace {

// Bounds-checked modulo for the delay-line pointer (avoids % on hot path
// being a concern; small positive divisors so it's cheap).
inline int nextIndex(int idx, int len) noexcept {
  int n = idx + 1;
  while (n >= len) n -= len;
  return n;
}

}  // namespace

float FreeverbStage::Model::processChannel(float input) noexcept {
  float out = 0.0f;
  for (int c = 0; c < kNumCombs; ++c) {
    const int idx = combIdx[c];
    const float delayed = combBuf[c][idx];
    // One-pole low-pass on the feedback path (damping).
    const float damped = (1.0f - damping) * delayed + damping * combLp[c];
    combLp[c] = damped;
    combBuf[c][idx] = input + feedback * damped;
    combIdx[c] = nextIndex(idx, combLen[c]);
    out += delayed;
  }
  out *= 0.25f;  // normalize the 4-comb sum

  for (int a = 0; a < kNumAllpass; ++a) {
    const int idx = apIdx[a];
    const float delayed = apBuf[a][idx];
    apBuf[a][idx] = out + delayed * allpassG;
    apIdx[a] = nextIndex(idx, apLen[a]);
    out = delayed - allpassG * apBuf[a][idx];
  }
  return out * wet;
}

void FreeverbStage::Model::resetState() noexcept {
  for (int c = 0; c < kNumCombs; ++c) {
    for (int i = 0; i < kMaxDelay; ++i) combBuf[c][i] = 0.0f;
    combIdx[c] = 0;
    combLp[c] = 0.0f;
  }
  for (int a = 0; a < kNumAllpass; ++a) {
    for (int i = 0; i < kMaxDelay; ++i) apBuf[a][i] = 0.0f;
    apIdx[a] = 0;
  }
}

FreeverbStage::FreeverbStage() = default;

void FreeverbStage::setComplexity(uint8_t level) noexcept {
  float t = level / 255.0f;
  // Feedback: 0.70 -> 0.95
  // Damping: 0.40 -> 0.10 (less damping = brighter tail)
  // Allpass gain: 0.50 -> 0.70
  for (auto& m : models_) {
    m.feedback = 0.70f + t * 0.25f;
    m.damping = 0.40f - t * 0.30f;
    m.allpassG = 0.50f + t * 0.20f;
  }
  complexity_ = level;
}

void FreeverbStage::process(const float* input, float* output,
                            uint32_t frames, uint32_t channels) noexcept {
  for (uint32_t f = 0; f < frames; ++f) {
    for (uint32_t ch = 0; ch < channels; ++ch) {
      const float s = input[static_cast<std::size_t>(f) * channels + ch];
      const uint32_t m = (ch < kMaxChannels) ? ch : (ch % kMaxChannels);
      output[static_cast<std::size_t>(f) * channels + ch] =
          models_[m].processChannel(s);
    }
  }
}

void FreeverbStage::reset() noexcept {
  for (auto& m : models_) m.resetState();
}