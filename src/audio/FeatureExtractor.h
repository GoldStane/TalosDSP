#pragma once

#include "audio/Filterbank.h"
#include <array>
#include <cstdint>

struct AudioFeatures {
  // Overall
  float rms = 0.0f;           // Root mean square (loudness)
  float zcr = 0.0f;           // Zero-crossing rate (0-1)

  // Per-band energy (4 bands from LR4 filterbank)
  std::array<float, 4> band_energy{};  // Mean square per band

  // Spectral features
  float centroid = 0.0f;      // Spectral centroid (Hz) - "brightness"
  float rolloff_85 = 0.0f;    // 85% energy rolloff frequency (Hz)

  // Channel-specific (if stereo)
  std::array<float, 2> rms_ch{};
  std::array<float, 2> zcr_ch{};
  std::array<std::array<float, 4>, 2> band_energy_ch{};

  void clear() noexcept {
    rms = zcr = centroid = rolloff_85 = 0.0f;
    band_energy.fill(0.0f);
    rms_ch.fill(0.0f);
    zcr_ch.fill(0.0f);
    for (auto& arr : band_energy_ch) arr.fill(0.0f);
  }
};

class FeatureExtractor {
 public:
  FeatureExtractor() = default;

  void init(float sample_rate, uint32_t window_frames = 1024) noexcept;

  // Process interleaved audio block, accumulate features
  // Consumer thread only; FeatureStream assembles exact windows.
  void process(const float* input, uint32_t frames, uint32_t channels) noexcept;

  // Finalize and compute features from accumulated data
  // Returns true if enough frames accumulated
  bool finalize(AudioFeatures& out) noexcept;

  void reset() noexcept;

  uint32_t windowFrames() const noexcept { return window_frames_; }
  uint32_t framesAccumulated() const noexcept { return frames_accumulated_; }

 private:
  float sample_rate_ = 48000.0f;
  uint32_t window_frames_ = 1024;
  uint32_t frames_accumulated_ = 0;
  uint32_t channels_ = 1;

  Filterbank filterbank_;

  // Accumulators
  std::array<float, 2> sum_sq_ch{};
  std::array<float, 2> zcr_count_ch{};
  std::array<float, 2> prev_sample_ch{};
  std::array<std::array<float, 4>, 2> band_sum_sq_ch{};

  // For spectral features
  float spectral_sum_ = 0.0f;
  float spectral_weighted_sum_ = 0.0f;
  float energy_cumulative_ = 0.0f;
  float rolloff_threshold_ = 0.0f;
  bool rolloff_found_ = false;

  void clearAccumulators() noexcept;
  void processFrame(const float* frame, uint32_t channels) noexcept;
  void computeFeatures(AudioFeatures& out) noexcept;
};