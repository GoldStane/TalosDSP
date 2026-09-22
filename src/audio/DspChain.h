#pragma once

#include "audio/Classifier.h"
#include "audio/ComplexityController.h"
#include "audio/FeatureExtractor.h"
#include "audio/LimiterStage.h"
#include "audio/MixerStage.h"
#include "audio/PassthroughCallback.h"
#include "audio/FreeverbStage.h"
#include "audio/SPSCRingBuffer.h"
#include "audio/StageHistogram.h"

#include <array>
#include <atomic>
#include <cstdint>

// Owns the Phase 1 signal chain by value (no heap) and runs it in order:
//   Reader -> Freeverb (wet) -> Mixer (dry + wet) -> Limiter -> output
// Each stage is timed with steady_clock around its call; the elapsed time is
// pushed into that stage's StageHistogram. Fixed ping-pong scratch buffers
// keep the whole chain allocation-free on the RT thread.
//
// Phase 2 additions:
// - SPSCRingBuffer for audio snapshots (watchdog/classifier)
// - ComplexityController (PID watchdog thread)
// - 0-255 complexity selects one to four active reverb combs
//
// Phase 3 additions:
// - FeatureExtractor (1024-frame window, extracts RMS, ZCR, band energy, centroid, rolloff)
// - Classifier thread (reads features, writes the per-chain preset atomic)
// - Preset system (percussive/tonal/ambient parameter tables)
class DspChain {
 public:
  DspChain();
  ~DspChain();
  // Lifecycle/configuration methods require the audio stream to be stopped.
  void setSampleRate(float rate) noexcept { sample_rate_ = rate; }

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept;

  void reset() noexcept;

  // Watchdog / complexity control
  void setWatchdogEnabled(bool enabled);
  void setManualComplexity(uint8_t level);
  void setPIDGains(float Kp, float Ki, float Kd);
  void startWatchdog();
  void stopWatchdog();

  // Classifier / preset control
  void setClassifierEnabled(bool enabled);
  void setManualPreset(uint8_t preset);
  void startClassifier();
  void stopClassifier();

  // Snapshot access for watchdog
  SPSCRingBuffer<FrameSnapshot, 4096>& snapshotQueue() noexcept { return snapshot_queue_; }

  // Feature queue for classifier
  SPSCRingBuffer<AudioFeatures, 64>& featureQueue() noexcept { return feature_queue_; }

  PassthroughCallback& reader() noexcept { return reader_; }
  FreeverbStage& reverb() noexcept { return reverb_; }
  MixerStage& mixer() noexcept { return mixer_; }
  LimiterStage& limiter() noexcept { return limiter_; }
  FeatureExtractor& featureExtractor() noexcept { return feature_extractor_; }

  std::atomic<uint8_t>& complexity() noexcept { return g_complexity_; }
  const std::atomic<uint8_t>& complexity() const noexcept { return g_complexity_; }

  std::atomic<uint8_t>& preset() noexcept { return g_preset_id_; }
  const std::atomic<uint8_t>& preset() const noexcept { return g_preset_id_; }

 private:
  static constexpr std::size_t kMaxFrames = 4096;
  static constexpr std::size_t kMaxChannels = 2;
  static constexpr std::size_t kBufElems = kMaxFrames * kMaxChannels;

  PassthroughCallback reader_;
  FreeverbStage reverb_;
  MixerStage mixer_;
  LimiterStage limiter_;

  std::array<float, kBufElems> dryBuf_{};
  std::array<float, kBufElems> wetBuf_{};
  std::array<float, kBufElems> mixBuf_{};

  // Phase 2: complexity control
  std::atomic<uint8_t> g_complexity_{128};
  SPSCRingBuffer<FrameSnapshot, 4096> snapshot_queue_;
  ComplexityController controller_{&g_complexity_};
  bool watchdog_enabled_{false};

  // Phase 3: classifier / preset
  std::atomic<uint8_t> g_preset_id_{static_cast<uint8_t>(Preset::TONAL)};
  FeatureExtractor feature_extractor_;
  SPSCRingBuffer<AudioFeatures, 64> feature_queue_;
  Classifier classifier_{&feature_queue_, &g_preset_id_};
  bool classifier_enabled_{false};
  float sample_rate_{48000.0f};
  uint8_t last_applied_preset_{255};

  FrameSnapshot computeSnapshot(const float* output,
                                uint32_t frames, uint32_t channels) noexcept;
  void processChunk(const float*, float*, uint32_t, uint32_t) noexcept;
  void applyComplexity() noexcept;
  void applyPreset() noexcept;
  void maybePushFeatures() noexcept;
};