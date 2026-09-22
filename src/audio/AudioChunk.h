#pragma once
#include "audio/SPSCRingBuffer.h"
#include <array>
#include <cstdint>

// One producer (callback), one consumer (classifier). Sequence increments even
// on drops, so no window can silently span missing audio.
struct AudioChunk {
  static constexpr uint32_t kFrames = 256;
  std::array<float, kFrames * 2> samples{};
  uint64_t sequence{};
  float sampleRate{48000};
  uint32_t frames{};
  uint32_t channels{1};
};
using AudioQueue = SPSCRingBuffer<AudioChunk, 256>;
