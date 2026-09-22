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
  if (!primed) {
    feedbackRamp.snap(feedback); dampingRamp.snap(damping); allpassRamp.snap(allpassG);
    for (int c=0;c<kNumCombs;++c) weights[c].snap(c<activeCombs ? 1.0f : 0.0f);
    previousCombs=activeCombs; primed=true;
  }
  feedbackRamp.target(feedback,rampFrames);
  dampingRamp.target(damping,rampFrames);
  allpassRamp.target(allpassG,rampFrames);
  const float fb=feedbackRamp.next(), damp=dampingRamp.next(), ap=allpassRamp.next();
  if (activeCombs!=previousCombs) {
    for (int c=0;c<kNumCombs;++c) {
      // New combs overwrite a complete delay before reading old storage.
      if (c>=previousCombs && c<activeCombs && weights[c].value()==0) {
        fresh[c]=combLen[c]; combLp[c]=0;
      }
      weights[c].target(c<activeCombs ? 1.0f : 0.0f,rampFrames);
    }
    previousCombs=activeCombs;
  }
  float out = 0.0f, normalization=0.0f;
  for (int c = 0; c < kNumCombs; ++c) {
    const float weight=weights[c].next();
    normalization+=weight;
    if (weight==0) continue;
    const int idx = combIdx[c];
    const float delayed = fresh[c] ? 0.0f : combBuf[c][idx];
    if (fresh[c]) --fresh[c];
    // One-pole low-pass on the feedback path (damping).
    const float damped = (1.0f - damp) * delayed + damp * combLp[c];
    combLp[c] = damped;
    combBuf[c][idx] = input + fb * damped;
    combIdx[c] = nextIndex(idx, combLen[c]);
    out += weight * delayed;
  }
  out /= normalization;

  for (int a = 0; a < kNumAllpass; ++a) {
    const int idx = apIdx[a];
    const float delayed = apBuf[a][idx];
    apBuf[a][idx] = out + delayed * ap;
    apIdx[a] = nextIndex(idx, apLen[a]);
    out = delayed - ap * apBuf[a][idx];
  }
  return out * wet;
}

void FreeverbStage::Model::resetState() noexcept {
  primed=false; fresh.fill(0);
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
  for (auto& m : models_) m.activeCombs = 1 + (static_cast<int>(level) * 3 / 255);
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