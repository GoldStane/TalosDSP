#include "audio/Classifier.h"

#include <algorithm>
#include <chrono>
#include <thread>



void Classifier::start() {
  if (running_.load()) return;
  AudioChunk stale;
  while (audio_queue_->try_pop(stale)) {}
  stream_.reset(); candidate_ = 255; streak_ = 0;
  windows_.store(0); gaps_.store(0);
  running_.store(true);
  try { thread_ = std::thread(&Classifier::run, this); }
  catch (...) { running_.store(false); throw; }
}

void Classifier::poll() {
  AudioChunk chunk;
  AudioFeatures features;
  // Bounded batch even if producer keeps filling the queue.
  for (std::size_t i = 0; i < audio_queue_->capacity() && audio_queue_->try_pop(chunk); ++i) {
    const auto previousGaps = stream_.gaps();
    const bool ready = stream_.consume(chunk, features);
    if (stream_.gaps() != previousGaps) { candidate_ = 255; streak_ = 0; }
    if (!ready) continue;
    if (manual_override_.load(std::memory_order_relaxed) || features.rms < 1e-4f) {
      candidate_ = 255; streak_ = 0; continue; // Silence holds the current preset.
    }
    const uint8_t next = classify(features);
    if (next != candidate_) { candidate_ = next; streak_ = 0; }
    if (++streak_ >= 3) { preset_->store(next, std::memory_order_relaxed); streak_ = 3; }
  }
  windows_.store(stream_.windows(), std::memory_order_relaxed);
  gaps_.store(stream_.gaps(), std::memory_order_relaxed);
}

void Classifier::run() {
  while (running_.load(std::memory_order_relaxed)) {
    poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms_));
  }
  poll(); // Drain the final bounded batch after the producer has stopped.
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