#pragma once

#include "audio/IDspStage.h"
#include "audio/StageHistogram.h"

#include <array>
#include <cstdint>

// Schroeder/Freeverb-style reverb: 4 parallel feedback comb filters feeding 2
// series all-pass filters, per channel. All state is fixed-size (no heap), and
// coefficients are compile-time constants, so process() uses only multiply /
// accumulate + a one-pole damping filter -- no transcendentals in the RT path.
//
// Reference: J. A. Moorer / Schroeder reverberation; the canonical Freeverb
// tuning (comb lengths, feedback/damping/all-pass gains) as popularized by
// Jezar's "Freeverb".
class FreeverbStage : public IDspStage {
 public:
  FreeverbStage();

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept override;

  void reset() noexcept override;

  const char* name() const noexcept override { return "Freeverb"; }

  StageHistogram& histogram() noexcept override { return hist_; }

  void setComplexity(uint8_t level) noexcept override;
  uint8_t getComplexity() const noexcept override { return complexity_; }

 private:
  struct Model {
    static constexpr int kNumCombs = 4;
    static constexpr int kNumAllpass = 2;
    static constexpr int kMaxDelay = 2048;

    std::array<std::array<float, kMaxDelay>, kNumCombs> combBuf{};
    std::array<int, kNumCombs> combLen{1116, 1188, 1277, 1356};
    std::array<int, kNumCombs> combIdx{};
    std::array<float, kNumCombs> combLp{};

    std::array<std::array<float, kMaxDelay>, kNumAllpass> apBuf{};
    std::array<int, kNumAllpass> apLen{556, 441};
    std::array<int, kNumAllpass> apIdx{};

    float feedback = 0.84f;
    float damping = 0.2f;
    float allpassG = 0.6f;
    float wet = 0.5f;

    float processChannel(float input) noexcept;
    void resetState() noexcept;
  };

  static constexpr int kMaxChannels = 2;
  std::array<Model, kMaxChannels> models_{};
  StageHistogram hist_;
  uint8_t complexity_{128};
};