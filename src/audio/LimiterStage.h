#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <cstdint>

// Brick-wall-safe limiter: tanh soft-clip followed by a hard ceiling clamp.
// `tanh` is RT-safe (no allocation, no locking); the clamp guarantees the
// output never exceeds `ceiling_` even on pathological input.
class LimiterStage : public IDspStage {
 public:
  LimiterStage() = default;

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept override;

  void setMakeupGain(float g) noexcept { makeupGain_ = g; }
  float makeupGain() const noexcept { return makeupGain_; }

  void reset() noexcept override {}

  const char* name() const noexcept override { return "Limiter"; }

  StageHistogram& histogram() noexcept override { return hist_; }

 private:
  float makeupGain_ = 1.0f;
  float ceiling_ = 1.0f;
  StageHistogram hist_;
};