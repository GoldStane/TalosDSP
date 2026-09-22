#pragma once

#include "audio/Biquad.h"
#include <array>
#include <cstdint>

// Four analysis bands, each crossover edge uses two second-order Butterworth
// sections (LR4, 24 dB/oct). This independent band analysis bank is not a
// phase-compensated, flat-summing audio reconstruction crossover.

class Filterbank {
 public:
  Filterbank() = default;

  // Initialize with sample rate and crossover frequencies
  void init(float sample_rate,
            float fc1 = 1500.0f,   // Low / Low-Mid crossover
            float fc2 = 4000.0f,   // Low-Mid / High-Mid crossover
            float fc3 = 10000.0f) noexcept; // High-Mid / High crossover

  // Reset all filter states
  void reset() noexcept;

  // Process single frame (all 4 bands, one channel)
  // Returns: band outputs in order [low, low-mid, high-mid, high]
  std::array<float, 4> process(float input, uint32_t channel = 0) noexcept;

  // Process block (single channel)
  void processBlock(const float* input,
                    float* band0, float* band1,
                    float* band2, float* band3,
                    uint32_t frames) noexcept;

  // Process interleaved stereo block
  void processBlockStereo(const float* input,
                          float* band0_l, float* band1_l,
                          float* band2_l, float* band3_l,
                          float* band0_r, float* band1_r,
                          float* band2_r, float* band3_r,
                          uint32_t frames) noexcept;

  float sampleRate() const noexcept { return sample_rate_; }
  float fc1() const noexcept { return fc1_; }
  float fc2() const noexcept { return fc2_; }
  float fc3() const noexcept { return fc3_; }

 private:
  float sample_rate_ = 48000.0f;
  float fc1_ = 1500.0f, fc2_ = 4000.0f, fc3_ = 10000.0f;

  struct ChannelFilters {
    // Band 0: Lowpass LR4 (fc1)
    Biquad lp_bq1, lp_bq2;

    // Band 1: Bandpass LR4 (fc1 - fc2)
    Biquad bp1_hp_bq1, bp1_hp_bq2;  // HP2 at fc1
    Biquad bp1_lp_bq1, bp1_lp_bq2;  // LP2 at fc2

    // Band 2: Bandpass LR4 (fc2 - fc3)
    Biquad bp2_hp_bq1, bp2_hp_bq2;  // HP2 at fc2
    Biquad bp2_lp_bq1, bp2_lp_bq2;  // LP2 at fc3

    // Band 3: Highpass LR4 (fc3)
    Biquad hp_bq1, hp_bq2;
  };

  ChannelFilters ch_[2];

  void designFilters() noexcept;
  void resetChannel(ChannelFilters& ch) noexcept;
  std::array<float, 4> processChannel(ChannelFilters& ch, float input) noexcept;
};