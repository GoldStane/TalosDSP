#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <cstdint>

// Dry/wet crossfade between the original (dry) signal and the reverb (wet)
// output. The chain calls mix(dry, wet, out, ...); process(in, out, ...) is the
// IDspStage adaptation (wet == dry) used for uniform testing.
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

  void reset() noexcept override {}

  const char* name() const noexcept override { return "Mixer"; }

  StageHistogram& histogram() noexcept override { return hist_; }

 private:
  float wetAmount_ = 0.5f;
  StageHistogram hist_;
};