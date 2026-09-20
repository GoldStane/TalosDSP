#include "audio/MixerStage.h"

#include <cmath>
#include <cstddef>

void MixerStage::setComplexity(uint8_t level) noexcept {
  crossfade_mode_ = level / 255.0f;
  complexity_ = level;
}

void MixerStage::mix(const float* dry, const float* wet, float* out,
                     uint32_t frames, uint32_t channels) noexcept {
  const float w = wetAmount_;
  const float d = 1.0f - w;
  const std::size_t n = static_cast<std::size_t>(frames) * channels;
  const float half_pi = static_cast<float>(M_PI) * 0.5f;
  
  if (crossfade_mode_ <= 0.0f) {
    // Linear crossfade
    for (std::size_t i = 0; i < n; ++i) {
      out[i] = d * dry[i] + w * wet[i];
    }
  } else if (crossfade_mode_ >= 1.0f) {
    // Equal-power (cosine) crossfade
    for (std::size_t i = 0; i < n; ++i) {
      float angle = w * half_pi;
      float cos_w = std::cos(angle);
      float sin_w = std::sin(angle);
      out[i] = cos_w * dry[i] + sin_w * wet[i];
    }
  } else {
    // Blend between linear and equal-power
    for (std::size_t i = 0; i < n; ++i) {
      float angle = w * half_pi;
      float cos_w = std::cos(angle);
      float sin_w = std::sin(angle);
      float linear = d * dry[i] + w * wet[i];
      float equal_power = cos_w * dry[i] + sin_w * wet[i];
      out[i] = (1.0f - crossfade_mode_) * linear + crossfade_mode_ * equal_power;
    }
  }
}

void MixerStage::process(const float* input, float* output, uint32_t frames,
                         uint32_t channels) noexcept {
  // Degenerate adaptation: dry == wet == input.
  mix(input, input, output, frames, channels);
}