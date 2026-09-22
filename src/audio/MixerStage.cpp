#include "audio/MixerStage.h"

#include <cmath>
#include <cstddef>

void MixerStage::setComplexity(uint8_t level) noexcept {
  complexity_ = level;
}

void MixerStage::mix(const float* dry, const float* wet, float* out,
                     uint32_t frames, uint32_t channels) noexcept {
  const float angle=wetAmount_*1.57079632679f;
  // Trigonometry once per block, not per sample.
  const float d=(1-crossfade_mode_)*(1-wetAmount_)+crossfade_mode_*std::cos(angle);
  const float w=(1-crossfade_mode_)*wetAmount_+crossfade_mode_*std::sin(angle);
  if (!primed_) { dryGain_.snap(d); wetGain_.snap(w); primed_=true; }
  dryGain_.target(d,rampFrames_); wetGain_.target(w,rampFrames_);
  for (uint32_t f=0;f<frames;++f) {
    const float dg=dryGain_.next(), wg=wetGain_.next();
    for (uint32_t ch=0;ch<channels;++ch) {
      const auto i=static_cast<std::size_t>(f)*channels+ch;
      out[i]=dg*dry[i]+wg*wet[i];
    }
  }
}

void MixerStage::process(const float* input, float* output, uint32_t frames,
                         uint32_t channels) noexcept {
  // Degenerate adaptation: dry == wet == input.
  mix(input, input, output, frames, channels);
}