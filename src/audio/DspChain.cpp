#include "audio/DspChain.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

DspChain::DspChain() = default;

void DspChain::process(const float* input, float* output, uint32_t frames,
                       uint32_t channels) noexcept {
  std::size_t n = static_cast<std::size_t>(frames) * channels;
  if (n > kBufElems) {
    frames = static_cast<uint32_t>(kBufElems / channels);
    n = static_cast<std::size_t>(frames) * channels;
  }

  // Apply current complexity to all stages
  applyComplexity();

  auto t0 = Clock::now();
  reader_.process(input, dryBuf_.data(), frames, channels);
  reader_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  reverb_.process(input, wetBuf_.data(), frames, channels);
  reverb_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  mixer_.mix(dryBuf_.data(), wetBuf_.data(), mixBuf_.data(), frames, channels);
  mixer_.histogram().record(Clock::now() - t0);

  t0 = Clock::now();
  limiter_.process(mixBuf_.data(), output, frames, channels);
  limiter_.histogram().record(Clock::now() - t0);

  // Compute and push snapshot for watchdog
  FrameSnapshot snap = computeSnapshot(input, output, frames, channels);
  snapshot_queue_.try_push(snap);
}

void DspChain::reset() noexcept {
  reader_.reset();
  reverb_.reset();
  mixer_.reset();
  limiter_.reset();
}

void DspChain::applyComplexity() noexcept {
  uint8_t c = g_complexity_.load(std::memory_order_relaxed);
  reader_.setComplexity(c);
  reverb_.setComplexity(c);
  mixer_.setComplexity(c);
  limiter_.setComplexity(c);
}

FrameSnapshot DspChain::computeSnapshot(const float* input, const float* output,
                                        uint32_t frames, uint32_t channels) noexcept {
  FrameSnapshot snap{};
  std::size_t n = static_cast<std::size_t>(frames) * channels;
  if (n == 0) return snap;

  // Compute per-channel RMS, peak, ZCR
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

  // Zero out unused channel if mono
  if (channels == 1) {
    snap.rms_ch[1] = 0.0f;
    snap.peak_ch[1] = 0.0f;
    snap.zcr_ch[1] = 0.0f;
  }

  return snap;
}

void DspChain::setWatchdogEnabled(bool enabled) {
  watchdog_enabled_ = enabled;
}

void DspChain::setManualComplexity(uint8_t level) {
  g_complexity_.store(level, std::memory_order_relaxed);
  controller_.setManualOverride(true);
}

void DspChain::setPIDGains(float Kp, float Ki, float Kd) {
  controller_.setGains(Kp, Ki, Kd);
}

void DspChain::startWatchdog() {
  controller_.start();
}

void DspChain::stopWatchdog() {
  controller_.stop();
}