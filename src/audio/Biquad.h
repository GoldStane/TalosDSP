#pragma once

#include <cmath>
#include <cstdint>

// Transposed Form II biquad section.
// Minimal state (2 delays), good numerical stability, single feedback multiply.
// y[n] = b0*x[n] + z1;  z1 = b1*x[n] - a1*y[n] + z2;  z2 = b2*x[n] - a2*y[n]
class Biquad {
 public:
  Biquad() = default;

  // Set coefficients directly (a0 assumed 1.0)
  void setCoefficients(float b0, float b1, float b2, float a1, float a2) noexcept {
    b0_ = b0; b1_ = b1; b2_ = b2; a1_ = a1; a2_ = a2;
  }

  // Reset internal state to zero
  void reset() noexcept { z1_ = 0.0f; z2_ = 0.0f; }

  // Process single sample
  float process(float x) noexcept {
    float y = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * y + z2_;
    z2_ = b2_ * x - a2_ * y;
    return y;
  }

  // Process block (interleaved channels not supported; call per channel)
  void processBlock(const float* input, float* output, uint32_t frames) noexcept {
    for (uint32_t i = 0; i < frames; ++i) {
      output[i] = process(input[i]);
    }
  }

 private:
  float b0_ = 1.0f, b1_ = 0.0f, b2_ = 0.0f;
  float a1_ = 0.0f, a2_ = 0.0f;
  float z1_ = 0.0f, z2_ = 0.0f;
};

// Coefficient calculators for common filter types (a0 normalized to 1.0)
namespace BiquadDesign {

// Butterworth lowpass, order 2 (12 dB/oct)
inline void butterworthLowpass(float fc, float fs, float& b0, float& b1, float& b2,
                               float& a1, float& a2) noexcept {
  float wc = 2.0f * 3.14159265359f * fc / fs;
  float k = tanf(wc * 0.5f);
  float k2 = k * k;
  float norm = 1.0f / (1.0f + 1.41421356237f * k + k2);
  b0 = k2 * norm;
  b1 = 2.0f * b0;
  b2 = b0;
  a1 = 2.0f * (k2 - 1.0f) * norm;
  a2 = (1.0f - 1.41421356237f * k + k2) * norm;
}

// Butterworth highpass, order 2
inline void butterworthHighpass(float fc, float fs, float& b0, float& b1, float& b2,
                                float& a1, float& a2) noexcept {
  float wc = 2.0f * 3.14159265359f * fc / fs;
  float k = tanf(wc * 0.5f);
  float k2 = k * k;
  float norm = 1.0f / (1.0f + 1.41421356237f * k + k2);
  b0 = 1.0f * norm;
  b1 = -2.0f * norm;
  b2 = 1.0f * norm;
  a1 = 2.0f * (k2 - 1.0f) * norm;
  a2 = (1.0f - 1.41421356237f * k + k2) * norm;
}

// LR4 uses two identical second-order Butterworth sections.

}  // namespace BiquadDesign
