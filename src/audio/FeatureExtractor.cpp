#include "audio/FeatureExtractor.h"

#include <algorithm>
#include <cmath>

void FeatureExtractor::init(float sample_rate, uint32_t window_frames) noexcept {
  sample_rate_ = sample_rate;
  window_frames_ = std::max<uint32_t>(window_frames, 1);
  filterbank_.init(sample_rate);
  reset();
}

void FeatureExtractor::reset() noexcept {
  filterbank_.reset();
  clearAccumulators();
}

void FeatureExtractor::clearAccumulators() noexcept {
  frames_accumulated_ = 0;
  sum_sq_ch[0] = sum_sq_ch[1] = 0.0f;
  zcr_count_ch[0] = zcr_count_ch[1] = 0.0f;
  prev_sample_ch[0] = prev_sample_ch[1] = 0.0f;
  for (auto& arr : band_sum_sq_ch) arr.fill(0.0f);
  spectral_sum_ = 0.0f;
  spectral_weighted_sum_ = 0.0f;
  energy_cumulative_ = 0.0f;
  rolloff_threshold_ = 0.0f;
  rolloff_found_ = false;
}

void FeatureExtractor::process(const float* input, uint32_t frames, uint32_t channels) noexcept {
  if (!input || channels == 0 || channels > 2) return;
  channels_ = std::min<uint32_t>(channels, 2);
  for (uint32_t i = 0; i < frames; ++i) {
    processFrame(input + i * channels, channels);
    ++frames_accumulated_;
  }
}

void FeatureExtractor::processFrame(const float* frame, uint32_t channels) noexcept {
  // Process through filterbank
  std::array<float, 4> bands_l, bands_r;

  if (channels == 1) {
    bands_l = filterbank_.process(frame[0]);
    bands_r = bands_l;  // duplicate for mono
  } else {
    // Stereo: process each channel
    bands_l = filterbank_.process(frame[0]);
    bands_r = filterbank_.process(frame[1], 1);
  }

  float in_l = frame[0];
  float in_r = (channels == 2) ? frame[1] : frame[0];

  // Channel 0
  sum_sq_ch[0] += in_l * in_l;
  if (frames_accumulated_ > 0 && ((prev_sample_ch[0] > 0.0f) != (in_l > 0.0f))) {
    zcr_count_ch[0] += 1.0f;
  }
  prev_sample_ch[0] = in_l;
  for (int b = 0; b < 4; ++b) {
    band_sum_sq_ch[0][b] += bands_l[b] * bands_l[b];
  }

  // Channel 1
  sum_sq_ch[1] += in_r * in_r;
  if (frames_accumulated_ > 0 && ((prev_sample_ch[1] > 0.0f) != (in_r > 0.0f))) {
    zcr_count_ch[1] += 1.0f;
  }
  prev_sample_ch[1] = in_r;
  for (int b = 0; b < 4; ++b) {
    band_sum_sq_ch[1][b] += bands_r[b] * bands_r[b];
  }

  // Spectral features from combined
  float band_center_freqs[4] = {filterbank_.fc1() * 0.5f,
    (filterbank_.fc1() + filterbank_.fc2()) * 0.5f,
    (filterbank_.fc2() + filterbank_.fc3()) * 0.5f,
    (filterbank_.fc3() + sample_rate_ * 0.5f) * 0.5f};
  for (int b = 0; b < 4; ++b) {
    float e_l = bands_l[b] * bands_l[b];
    float e_r = bands_r[b] * bands_r[b];
    float e = (e_l + e_r) * 0.5f;
    spectral_sum_ += e;
    spectral_weighted_sum_ += band_center_freqs[b] * e;
  }
}

bool FeatureExtractor::finalize(AudioFeatures& out) noexcept {
  if (frames_accumulated_ < window_frames_) return false;
  computeFeatures(out);
  clearAccumulators();
  return true;
}

void FeatureExtractor::computeFeatures(AudioFeatures& out) noexcept {
  out.clear();
  float n = static_cast<float>(frames_accumulated_);
  if (n <= 0.0f) return;

  // RMS per channel
  for (int ch = 0; ch < 2; ++ch) {
    if (ch < static_cast<int>(channels_)) {
      out.rms_ch[ch] = std::sqrt(sum_sq_ch[ch] / n);
      out.zcr_ch[ch] = zcr_count_ch[ch] / n;
      for (int b = 0; b < 4; ++b) {
        out.band_energy_ch[ch][b] = band_sum_sq_ch[ch][b] / n;
      }
    }
  }

  // Overall (average of channels if stereo)
  if (channels_ == 1) {
    out.rms = out.rms_ch[0];
    out.zcr = out.zcr_ch[0];
    out.band_energy = out.band_energy_ch[0];
  } else {
    out.rms = std::sqrt((sum_sq_ch[0] + sum_sq_ch[1]) / (2.0f * n));
    out.zcr = (out.zcr_ch[0] + out.zcr_ch[1]) * 0.5f;
    for (int b = 0; b < 4; ++b) {
      out.band_energy[b] = (out.band_energy_ch[0][b] + out.band_energy_ch[1][b]) * 0.5f;
    }
  }

  // Spectral centroid
  if (spectral_sum_ > 1e-10f) {
    out.centroid = spectral_weighted_sum_ / spectral_sum_;
  }

  // 85% rolloff - simplified: find first band where cumulative > 85%
  float total_energy = 0.0f;
  for (int b = 0; b < 4; ++b) total_energy += out.band_energy[b];
  if (total_energy == 0.0f) return;
  float cumulative = 0.0f;
  float band_center_freqs[4] = {filterbank_.fc1() * 0.5f,
    (filterbank_.fc1() + filterbank_.fc2()) * 0.5f,
    (filterbank_.fc2() + filterbank_.fc3()) * 0.5f,
    (filterbank_.fc3() + sample_rate_ * 0.5f) * 0.5f};
  for (int b = 0; b < 4; ++b) {
    cumulative += out.band_energy[b];
    if (cumulative >= 0.85f * total_energy) {
      out.rolloff_85 = band_center_freqs[b];
      break;
    }
  }
  if (out.rolloff_85 == 0.0f) out.rolloff_85 = band_center_freqs[3];
}