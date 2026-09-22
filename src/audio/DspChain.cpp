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
  const auto elapsed = Clock::now() - blockStart;
  block_histogram_.record(elapsed);
  controller_.record(std::chrono::duration<float, std::micro>(elapsed).count(),
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

  if (classifier_enabled_) pushAudio(frames, channels);
}

void DspChain::reset() noexcept {
  reader_.reset();
  reverb_.reset();
  mixer_.reset();
  limiter_.reset();
  last_applied_preset_ = 255;
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

void DspChain::pushAudio(uint32_t frames, uint32_t channels) noexcept {
  for (uint32_t offset = 0; offset < frames;) {
    const auto count = std::min(frames - offset, AudioChunk::kFrames);
    audio_chunk_.sequence = audio_sequence_++;
    audio_chunk_.frames = count;
    audio_chunk_.channels = channels;
    audio_chunk_.sampleRate = sample_rate_;
    std::copy_n(dryBuf_.data() + offset * channels, count * channels, audio_chunk_.samples.data());
    if (!audio_queue_.try_push(audio_chunk_)) audio_drops_.fetch_add(1, std::memory_order_relaxed);
    offset += count;
  }
}
