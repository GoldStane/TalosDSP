#pragma once

#include "audio/FeatureExtractor.h"
#include "audio/SPSCRingBuffer.h"

#include <atomic>
#include <cstdint>
#include <thread>

// Preset IDs
enum class Preset : uint8_t {
  PERCUSSIVE = 0,
  TONAL = 1,
  AMBIENT = 2,
  COUNT = 3
};

// Preset parameter tables
struct PresetParams {
  // Freeverb
  float fb_feedback;
  float fb_damping;
  float fb_allpassG;
  // Limiter
  float lm_knee_blend;
  // Mixer
  float mx_crossfade_mode;
};

constexpr PresetParams PRESETS[static_cast<size_t>(Preset::COUNT)] = {
  // PERCUSSIVE: tight, dry, fast
  {0.70f, 0.40f, 0.50f, 0.0f,  0.0f},
  // TONAL: balanced, moderate
  {0.85f, 0.20f, 0.60f, 0.5f,  0.5f},
  // AMBIENT: lush, wet, soft
  {0.95f, 0.10f, 0.70f, 1.0f,  1.0f},
};

class Classifier {
  SPSCRingBuffer<AudioFeatures, 64>* feature_queue_;
  std::atomic<uint8_t>* preset_;
  std::atomic<bool> running_{false};
  std::atomic<bool> manual_override_{false};
  std::thread thread_;
  uint32_t poll_ms_ = 50;

public:
  Classifier(SPSCRingBuffer<AudioFeatures, 64>* queue, std::atomic<uint8_t>* preset) noexcept
      : feature_queue_(queue), preset_(preset) {}

  ~Classifier() { stop(); }

  Classifier(const Classifier&) = delete;
  Classifier& operator=(const Classifier&) = delete;

  void start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    thread_ = std::thread(&Classifier::run, this);
  }

  void stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    if (thread_.joinable()) thread_.join();
  }

  void setManualOverride(bool on) { manual_override_.store(on, std::memory_order_relaxed); }
  void setManualPreset(uint8_t preset) noexcept {
    if (preset < static_cast<uint8_t>(Preset::COUNT)) {
      preset_->store(preset, std::memory_order_relaxed);
    }
  }

private:
  void run();
  static uint8_t classify(const AudioFeatures& f) noexcept;
};