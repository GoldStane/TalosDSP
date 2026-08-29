#pragma once

#include <cstdint>

// Pure passthrough of an interleaved float stream. This is the Phase 0
// "does nothing but loopback input to output" stage.
//
// RT-safe: in-place, no allocation, no locking, deterministic. The PortAudio
// callback calls this for every block; unit tests exercise it directly.
class PassthroughCallback {
 public:
  void process(const float* input, float* output, std::uint32_t frames,
               std::uint32_t channels) noexcept;

  void setChannels(std::uint32_t channels) noexcept { channels_ = channels; }

 private:
  std::uint32_t channels_ = 0;
};