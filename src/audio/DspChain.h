#pragma once

#include "audio/LimiterStage.h"
#include "audio/MixerStage.h"
#include "audio/PassthroughCallback.h"
#include "audio/FreeverbStage.h"
#include "audio/StageHistogram.h"

#include <array>
#include <cstdint>

// Owns the Phase 1 signal chain by value (no heap) and runs it in order:
//   Reader -> Freeverb (wet) -> Mixer (dry + wet) -> Limiter -> output
// Each stage is timed with steady_clock around its call; the elapsed time is
// pushed into that stage's StageHistogram. Fixed ping-pong scratch buffers
// keep the whole chain allocation-free on the RT thread.
class DspChain {
 public:
  DspChain();

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept;

  void reset() noexcept;

  PassthroughCallback& reader() noexcept { return reader_; }
  FreeverbStage& reverb() noexcept { return reverb_; }
  MixerStage& mixer() noexcept { return mixer_; }
  LimiterStage& limiter() noexcept { return limiter_; }

 private:
  static constexpr std::size_t kMaxFrames = 4096;
  static constexpr std::size_t kMaxChannels = 2;
  static constexpr std::size_t kBufElems = kMaxFrames * kMaxChannels;

  PassthroughCallback reader_;
  FreeverbStage reverb_;
  MixerStage mixer_;
  LimiterStage limiter_;

  std::array<float, kBufElems> dryBuf_{};
  std::array<float, kBufElems> wetBuf_{};
  std::array<float, kBufElems> mixBuf_{};
};