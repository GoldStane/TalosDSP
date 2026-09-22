#include "audio/LimiterStage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

void LimiterStage::setComplexity(uint8_t level) noexcept {
  complexity_ = level;
}

void LimiterStage::process(const float* input, float* output, uint32_t frames,
                           uint32_t channels) noexcept {
  if (!primed_) { kneeRamp_.snap(knee_blend_); primed_=true; }
  kneeRamp_.target(knee_blend_,rampFrames_);
  const float g = makeupGain_;
  const float c = ceiling_;
  for (uint32_t f=0;f<frames;++f) {
    const float knee=kneeRamp_.next();
    for (uint32_t ch=0;ch<channels;++ch) {
    const auto i=static_cast<std::size_t>(f)*channels+ch;
    float y = std::tanhf(input[i] * g);
    // Soft knee blend: at knee_blend_=1, use softer transition
    if (knee > 0.0f) {
      // Soft knee: smooth transition using a polynomial
      float x = input[i] * g;
      float soft = x / (1.0f + std::fabs(x));
      y = (1.0f - knee) * y + knee * soft;
    }
    if (y > c) {
      y = c;
    } else if (y < -c) {
      y = -c;
    }
    output[i] = y;
    }
  }
}