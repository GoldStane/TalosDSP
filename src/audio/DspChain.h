#pragma once

#include "audio/ComplexityController.h"
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
// - Continuous 0-255 complexity control per stage
class DspChain {
 public:
  DspChain();

  void process(const float* input, float* output, uint32_t frames,
               uint32_t channels) noexcept;

  void reset() noexcept;

  // Watchdog / complexity control
  void setWatchdogEnabled(bool enabled);
  void setManualComplexity(uint8_t level);
  void setPIDGains(float Kp, float Ki, float Kd);
  void startWatchdog();
  void stopWatchdog();

  // Snapshot access for watchdog
  SPSCRingBuffer<FrameSnapshot, 4096>& snapshotQueue() noexcept { return snapshot_queue_; }

  PassthroughCallback& reader() noexcept { return reader_; }
  FreeverbStage& reverb() noexcept { return reverb_; }
  MixerStage& mixer() noexcept { return mixer_; }
  LimiterStage& limiter() noexcept { return limiter_; }

  std::atomic<uint8_t>& complexity() noexcept { return g_complexity_; }
  const std::atomic<uint8_t>& complexity() const noexcept { return g_complexity_; }

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

  FrameSnapshot computeSnapshot(const float* input, const float* output,
                                uint32_t frames, uint32_t channels) noexcept;
  void applyComplexity() noexcept;
};