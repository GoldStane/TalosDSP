#pragma once
#include "audio/AudioChunk.h"
#include "audio/FeatureExtractor.h"
#include <algorithm>
#include <cmath>

// Consumer-owned window assembly. At most one 1024-frame window completes
// per 256-frame chunk; remaining frames continue into the next window.
class FeatureStream {
 public:
  void reset() noexcept { initialized_ = false; gaps_ = windows_ = 0; }
  bool consume(const AudioChunk& chunk, AudioFeatures& result) noexcept {
    if (!chunk.frames || chunk.frames > AudioChunk::kFrames ||
        chunk.channels < 1 || chunk.channels > 2 ||
        !std::isfinite(chunk.sampleRate) || chunk.sampleRate < 8000) return false;
    if (!initialized_ || chunk.sequence != next_ || rate_ != chunk.sampleRate || channels_ != chunk.channels) {
      if (initialized_) ++gaps_;
      extractor_.init(chunk.sampleRate, 1024);
      initialized_ = true;
      rate_ = chunk.sampleRate; channels_ = chunk.channels;
    }
    next_ = chunk.sequence + 1;
    bool ready = false;
    for (uint32_t offset = 0; offset < chunk.frames;) {
      const auto count = std::min(chunk.frames - offset, 1024 - extractor_.framesAccumulated());
      extractor_.process(chunk.samples.data() + offset * chunk.channels, count, chunk.channels);
      offset += count;
      if (extractor_.finalize(result)) { ready = true; ++windows_; }
    }
    return ready;
  }
  uint64_t gaps() const noexcept { return gaps_; }
  uint64_t windows() const noexcept { return windows_; }
  uint32_t partialFrames() const noexcept { return extractor_.framesAccumulated(); }
 private:
  FeatureExtractor extractor_;
  bool initialized_{false};
  uint64_t next_{}, gaps_{}, windows_{};
  float rate_{};
  uint32_t channels_{};
};
