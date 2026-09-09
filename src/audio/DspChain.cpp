#include "audio/DspChain.h"

#include <chrono>
#include <cstddef>

namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

DspChain::DspChain() = default;

void DspChain::process(const float* input, float* output, uint32_t frames,
                       uint32_t channels) noexcept {
  std::size_t n = static_cast<std::size_t>(frames) * channels;
  if (n > kBufElems) {
    // Safety clamp: process only what fits the scratch buffers.
    frames = static_cast<uint32_t>(kBufElems / channels);
    n = static_cast<std::size_t>(frames) * channels;
  }

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
}

void DspChain::reset() noexcept {
  reader_.reset();
  reverb_.reset();
  mixer_.reset();
  limiter_.reset();
}