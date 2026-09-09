#include "audio/LimiterStage.h"

#include <cmath>
#include <cstddef>

void LimiterStage::process(const float* input, float* output, uint32_t frames,
                           uint32_t channels) noexcept {
  const std::size_t n = static_cast<std::size_t>(frames) * channels;
  const float g = makeupGain_;
  const float c = ceiling_;
  for (std::size_t i = 0; i < n; ++i) {
    float y = std::tanhf(input[i] * g);
    if (y > c) {
      y = c;
    } else if (y < -c) {
      y = -c;
    }
    output[i] = y;
  }
}