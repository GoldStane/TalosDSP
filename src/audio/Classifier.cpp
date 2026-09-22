#include "audio/Classifier.h"

#include <algorithm>
#include <chrono>
#include <thread>



void Classifier::run() {
  using namespace std::chrono;
  AudioFeatures features;
  steady_clock::time_point last_process = steady_clock::now();

  while (running_.load(std::memory_order_relaxed)) {
    // Buffer multiple windows - drain queue, keep latest
    bool got_features = false;
    while (feature_queue_->try_pop(features)) {
      got_features = true;
    }

    if (got_features && !manual_override_.load(std::memory_order_relaxed)) {
      uint8_t preset = classify(features);
      preset_->store(preset, std::memory_order_relaxed);
    }

    auto now = steady_clock::now();
    auto elapsed = duration_cast<milliseconds>(now - last_process);
    if (elapsed.count() < static_cast<long long>(poll_ms_)) {
      std::this_thread::sleep_for(milliseconds(poll_ms_) - elapsed);
    }
    last_process = steady_clock::now();
  }
}

uint8_t Classifier::classify(const AudioFeatures& f) noexcept {
  // High ZCR + high centroid → percussive (drums, transients)
  if (f.zcr > 0.35f && f.centroid > 4000.0f) {
    return static_cast<uint8_t>(Preset::PERCUSSIVE);
  }

  // Low ZCR + low centroid → ambient (sustained, low freq)
  if (f.zcr < 0.1f && f.centroid < 1500.0f) {
    return static_cast<uint8_t>(Preset::AMBIENT);
  }

  // High low-band energy → tonal (bass, vocals)
  float total_energy = f.rms * f.rms;
  if (total_energy > 1e-6f && f.band_energy[0] > 0.4f * total_energy) {
    return static_cast<uint8_t>(Preset::TONAL);
  }

  // Default: tonal
  return static_cast<uint8_t>(Preset::TONAL);
}