#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <array>
#include <cstdint>

// Brick-wall-safe limiter: tanh soft-clip followed by a hard ceiling clamp.
// `tanh` is RT-safe (no allocation, no locking); the clamp guarantees the
// output never exceeds `ceiling_` even on pathological input.
//
// Complexity mapping (0-255):
//   Lookahead: 0.0 ms -> 2.0 ms (linear)
//   Oversampling: 1x -> 4x (discrete steps)
//   Knee: tanh (hard) -> soft knee (linear blend)
class LimiterStage : public IDspStage {
 public:
  LimiterStage() = default;

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept override;

  void setMakeupGain(float g) noexcept { makeupGain_ = g; }
  float makeupGain() const noexcept { return makeupGain_; }

  void setComplexity(uint8_t level) noexcept override;
  uint8_t getComplexity() const noexcept override { return complexity_; }

  void reset() noexcept override {}

  const char* name() const noexcept override { return "Limiter"; }

  StageHistogram& histogram() noexcept override { return hist_; }

 private:
  float makeupGain_ = 1.0f;
  float ceiling_ = 1.0f;
  StageHistogram hist_;

  // Complexity-controlled parameters
  float lookahead_ms_ = 0.0f;
  int oversampling_ = 1;
  float knee_blend_ = 0.0f;  // 0 = tanh (hard), 1 = soft knee
  uint8_t complexity_{128};
};