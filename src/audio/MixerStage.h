#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <cstdint>

// Dry/wet crossfade between the original (dry) signal and the reverb (wet)
// output. The chain calls mix(dry, wet, out, ...); process(in, out, ...) is the
// IDspStage adaptation (wet == dry) used for uniform testing.
//
// Complexity mapping (0-255):
//   Crossfade curve: linear (t=0) -> equal-power cosine (t=1)
class MixerStage : public IDspStage {
 public:
  MixerStage() = default;

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept override;

  void mix(const float* dry, const float* wet, float* out, uint32_t frames,
           uint32_t channels) noexcept;

  void setWet(float wet) noexcept {
    if (wet < 0.0f) wet = 0.0f;
    if (wet > 1.0f) wet = 1.0f;
    wetAmount_ = wet;
  }
  float wetAmount() const noexcept { return wetAmount_; }

  void setComplexity(uint8_t level) noexcept override;
  uint8_t getComplexity() const noexcept override { return complexity_; }

  void reset() noexcept override {}

  const char* name() const noexcept override { return "Mixer"; }

  StageHistogram& histogram() noexcept override { return hist_; }

 private:
  float wetAmount_ = 0.5f;
  StageHistogram hist_;
  float crossfade_mode_{0.0f};  // 0 = linear, 1 = equal-power
  uint8_t complexity_{128};
};