#include "audio/DspChain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace



void DspChain::process(const float* input, float* output, uint32_t frames,
                       uint32_t channels) noexcept {
  if (!output || !frames || channels == 0 || channels > 2) return;
  const auto blockStart = Clock::now();
  const uint32_t originalFrames = frames;
  while (frames) {
    const uint32_t count = std::min<uint32_t>(frames, kMaxFrames);
    processChunk(input, output, count, channels);
    if (input) input += static_cast<std::size_t>(count) * channels;
    output += static_cast<std::size_t>(count) * channels;
    frames -= count;
  }
  controller_.record(std::chrono::duration<float, std::micro>(Clock::now() - blockStart).count(),
                     static_cast<float>(originalFrames) / sample_rate_ * 1e6f);
}

void DspChain::processChunk(const float* input, float* output, uint32_t frames,
                          uint32_t channels) noexcept {

  // Apply preset params only when preset changes
  applyPreset();

  // Apply current complexity to all stages
  applyComplexity();

  auto t0 = Clock::now();
  reader_.process(input, dryBuf_.data(), frames, channels);
  reader_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  reverb_.process(dryBuf_.data(), wetBuf_.data(), frames, channels);
  reverb_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  mixer_.mix(dryBuf_.data(), wetBuf_.data(), mixBuf_.data(), frames, channels);
  mixer_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  limiter_.process(mixBuf_.data(), output, frames, channels);
  limiter_.histogram().record(Clock::now() - t0);

  // Compute and push snapshot for watchdog
  FrameSnapshot snap = computeSnapshot(output, frames, channels);
  snapshot_queue_.try_push(snap);

  // Accumulate features for classifier
  if (classifier_enabled_) {
    for (uint32_t offset = 0; offset < frames;) {
      const uint32_t count = std::min(frames - offset,
          feature_extractor_.windowFrames() - feature_extractor_.framesAccumulated());
      feature_extractor_.process(dryBuf_.data() + offset * channels, count, channels);
      maybePushFeatures();
      offset += count;
    }
  }
}

void DspChain::reset() noexcept {
  reader_.reset();
  reverb_.reset();
  mixer_.reset();
  limiter_.reset();
  feature_extractor_.reset();
}

void DspChain::applyComplexity() noexcept {
  uint8_t c = g_complexity_.load(std::memory_order_relaxed);
  reader_.setComplexity(c);
  reverb_.setComplexity(c);
  mixer_.setComplexity(c);
  limiter_.setComplexity(c);
}

void DspChain::applyPreset() noexcept {
  uint8_t preset = g_preset_id_.load(std::memory_order_relaxed);
  if (preset >= static_cast<uint8_t>(Preset::COUNT)) return;
  if (preset == last_applied_preset_) return;
  last_applied_preset_ = preset;

  const PresetParams& p = PRESETS[preset];
  reverb_.setFeedback(p.fb_feedback);
  reverb_.setDamping(p.fb_damping);
  reverb_.setAllpassG(p.fb_allpassG);

  limiter_.setKneeBlend(p.lm_knee_blend);

  mixer_.setCrossfadeMode(p.mx_crossfade_mode);
}

FrameSnapshot DspChain::computeSnapshot(const float* output,
                                        uint32_t frames, uint32_t channels) noexcept {
  FrameSnapshot snap{};
  std::size_t n = static_cast<std::size_t>(frames) * channels;
  if (n == 0) return snap;

  for (uint32_t ch = 0; ch < channels && ch < 2; ++ch) {
    float sum_sq = 0.0f;
    float peak = 0.0f;
    float zcr = 0.0f;
    float prev = 0.0f;

    for (uint32_t f = 0; f < frames; ++f) {
      std::size_t idx = static_cast<std::size_t>(f) * channels + ch;
      float out = output[idx];

      sum_sq += out * out;
      float abs_out = std::fabs(out);
      if (abs_out > peak) peak = abs_out;

      if (f > 0 && ((prev > 0.0f) != (out > 0.0f))) {
        zcr += 1.0f;
      }
      prev = out;
    }

    float frames_f = static_cast<float>(frames);
    snap.rms_ch[ch] = std::sqrt(sum_sq / frames_f);
    snap.peak_ch[ch] = peak;
    snap.zcr_ch[ch] = zcr / frames_f;
  }

  if (channels == 1) {
    snap.rms_ch[1] = 0.0f;
    snap.peak_ch[1] = 0.0f;
    snap.zcr_ch[1] = 0.0f;
  }

  return snap;
}

void DspChain::maybePushFeatures() noexcept {
  if (!classifier_enabled_) return;

  AudioFeatures features;
  if (feature_extractor_.finalize(features)) {
    feature_queue_.try_push(features);
  }
}