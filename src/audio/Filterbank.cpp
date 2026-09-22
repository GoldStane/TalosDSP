#include "audio/Filterbank.h"

#include <algorithm>
#include <cmath>

using namespace BiquadDesign;

void Filterbank::init(float sample_rate,
                      float fc1, float fc2, float fc3) noexcept {
  sample_rate_ = std::isfinite(sample_rate) && sample_rate > 0 ? sample_rate : 48000.0f;
  const float limit = sample_rate_ * 0.45f;
  fc1_ = std::clamp(fc1, limit * 0.001f, limit * 0.8f);
  fc2_ = std::clamp(fc2, fc1_ * 1.01f, limit * 0.9f);
  fc3_ = std::clamp(fc3, fc2_ * 1.01f, limit);
  designFilters();
  reset();
}

void Filterbank::designFilters() noexcept {
  // Design coefficients for each filter section
  float b0, b1, b2, a1, a2;

  // Band 0: Lowpass LR4 at fc1 (cascade two LP2)
  butterworthLowpass(fc1_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].lp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].lp_bq2.setCoefficients(b0, b1, b2, a1, a2);

  butterworthLowpass(fc1_, sample_rate_, b0, b1, b2, a1, a2);

  // Band 1: Bandpass fc1-fc2
  // HP2 at fc1
  butterworthHighpass(fc1_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].bp1_hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].bp1_hp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp1_hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp1_hp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  // LP2 at fc2
  butterworthLowpass(fc2_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].bp1_lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].bp1_lp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp1_lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp1_lp_bq2.setCoefficients(b0, b1, b2, a1, a2);

  // Band 2: Bandpass fc2-fc3
  // HP2 at fc2
  butterworthHighpass(fc2_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].bp2_hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].bp2_hp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp2_hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp2_hp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  // LP2 at fc3
  butterworthLowpass(fc3_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].bp2_lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].bp2_lp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp2_lp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].bp2_lp_bq2.setCoefficients(b0, b1, b2, a1, a2);

  // Band 3: Highpass LR4 at fc3
  butterworthHighpass(fc3_, sample_rate_, b0, b1, b2, a1, a2);
  ch_[0].hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[0].hp_bq2.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].hp_bq1.setCoefficients(b0, b1, b2, a1, a2);
  ch_[1].hp_bq2.setCoefficients(b0, b1, b2, a1, a2);

  butterworthHighpass(fc3_, sample_rate_, b0, b1, b2, a1, a2);
}

void Filterbank::reset() noexcept {
  resetChannel(ch_[0]);
  resetChannel(ch_[1]);
}

void Filterbank::resetChannel(ChannelFilters& ch) noexcept {
  ch.lp_bq1.reset(); ch.lp_bq2.reset();
  ch.bp1_hp_bq1.reset(); ch.bp1_hp_bq2.reset();
  ch.bp1_lp_bq1.reset(); ch.bp1_lp_bq2.reset();
  ch.bp2_hp_bq1.reset(); ch.bp2_hp_bq2.reset();
  ch.bp2_lp_bq1.reset(); ch.bp2_lp_bq2.reset();
  ch.hp_bq1.reset(); ch.hp_bq2.reset();
}

std::array<float, 4> Filterbank::process(float input, uint32_t channel) noexcept {
  // Process channel 0 only for mono
  return processChannel(ch_[channel < 2 ? channel : 0], input);
}

std::array<float, 4> Filterbank::processChannel(ChannelFilters& ch, float input) noexcept {
  // Band 0: Lowpass LR4 (two second-order Butterworth sections)
  float b0 = ch.lp_bq1.process(input);
  b0 = ch.lp_bq2.process(b0);

  // Band 1: Bandpass (HP2 at fc1 -> LP2 at fc2)
  float b1 = ch.bp1_hp_bq1.process(input);
  b1 = ch.bp1_hp_bq2.process(b1);
  b1 = ch.bp1_lp_bq1.process(b1);
  b1 = ch.bp1_lp_bq2.process(b1);

  // Band 2: Bandpass (HP2 at fc2 -> LP2 at fc3)
  float b2 = ch.bp2_hp_bq1.process(input);
  b2 = ch.bp2_hp_bq2.process(b2);
  b2 = ch.bp2_lp_bq1.process(b2);
  b2 = ch.bp2_lp_bq2.process(b2);

  // Band 3: Highpass LR4 (two second-order Butterworth sections)
  float b3 = ch.hp_bq1.process(input);
  b3 = ch.hp_bq2.process(b3);

  return {b0, b1, b2, b3};
}

void Filterbank::processBlock(const float* input,
                              float* band0, float* band1,
                              float* band2, float* band3,
                              uint32_t frames) noexcept {
  for (uint32_t i = 0; i < frames; ++i) {
    auto bands = processChannel(ch_[0], input[i]);
    band0[i] = bands[0];
    band1[i] = bands[1];
    band2[i] = bands[2];
    band3[i] = bands[3];
  }
}

void Filterbank::processBlockStereo(const float* input,
                                    float* band0_l, float* band1_l,
                                    float* band2_l, float* band3_l,
                                    float* band0_r, float* band1_r,
                                    float* band2_r, float* band3_r,
                                    uint32_t frames) noexcept {
  for (uint32_t i = 0; i < frames; ++i) {
    float in_l = input[2 * i];
    float in_r = input[2 * i + 1];

    auto bands_l = processChannel(ch_[0], in_l);
    auto bands_r = processChannel(ch_[1], in_r);

    band0_l[i] = bands_l[0];
    band1_l[i] = bands_l[1];
    band2_l[i] = bands_l[2];
    band3_l[i] = bands_l[3];

    band0_r[i] = bands_r[0];
    band1_r[i] = bands_r[1];
    band2_r[i] = bands_r[2];
    band3_r[i] = bands_r[3];
  }
}