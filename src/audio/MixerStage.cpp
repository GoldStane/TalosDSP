#include "audio/MixerStage.h"

#include <cstddef>

void MixerStage::mix(const float* dry, const float* wet, float* out,
                     uint32_t frames, uint32_t channels) noexcept {
  const float w = wetAmount_;
  const float d = 1.0f - w;
  const std::size_t n = static_cast<std::size_t>(frames) * channels;
  for (std::size_t i = 0; i < n; ++i) {
    out[i] = d * dry[i] + w * wet[i];
  }
}

void MixerStage::process(const float* input, float* output, uint32_t frames,
                         uint32_t channels) noexcept {
  // Degenerate adaptation: dry == wet == input.
  mix(input, input, output, frames, channels);
}