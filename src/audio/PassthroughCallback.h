#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <cstdint>

// Pure passthrough of an interleaved float stream. This is the Phase 0
// "does nothing but loopback input to output" stage, now also the chain's
// "Reader" stage. It copies the input into the chain's dry buffer; it is where
// future format conversion would live.
//
// RT-safe: copy only, no allocation, no locking, deterministic. The PortAudio
// callback and the unit tests exercise it directly.
class PassthroughCallback : public IDspStage {
 public:
  void process(const float* input, float* output, std::uint32_t frames,
               std::uint32_t channels) noexcept override;

  void setChannels(std::uint32_t channels) noexcept { channels_ = channels; }

  void reset() noexcept override {}

  const char* name() const noexcept override { return "Reader"; }

  StageHistogram& histogram() noexcept override { return hist_; }

  void setComplexity(uint8_t level) noexcept override { complexity_ = level; }
  uint8_t getComplexity() const noexcept override { return complexity_; }

 private:
  std::uint32_t channels_ = 0;
  StageHistogram hist_;
  uint8_t complexity_{128};
};