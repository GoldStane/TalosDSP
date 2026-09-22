#include "audio/LimiterStage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

void LimiterStage::setComplexity(uint8_t level) noexcept {
  complexity_ = level;
}

void LimiterStage::process(const float* input, float* output, uint32_t frames,
                           uint32_t channels) noexcept {
  const std::size_t n = static_cast<std::size_t>(frames) * channels;
  const float g = makeupGain_;
  const float c = ceiling_;
  for (std::size_t i = 0; i < n; ++i) {
    float y = std::tanhf(input[i] * g);
    // Soft knee blend: at knee_blend_=1, use softer transition
    if (knee_blend_ > 0.0f) {
      // Soft knee: smooth transition using a polynomial
      float x = input[i] * g;
      float soft = x / (1.0f + std::fabs(x));
      y = (1.0f - knee_blend_) * y + knee_blend_ * soft;
    }
    if (y > c) {
      y = c;
    } else if (y < -c) {
      y = -c;
    }
    output[i] = y;
  }
}