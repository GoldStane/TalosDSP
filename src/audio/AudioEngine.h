#pragma once

#include "audio/PassthroughCallback.h"
#include "audio/XrunCounter.h"

#include <portaudio.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Owns the PortAudio stream lifecycle. Lives off the RT thread: everything
// here may allocate and block freely. The RT code (PassthroughCallback,
// XrunCounter, RtThreadBoost) is separate and lives in the talos_rt library
// which the rt-safety harness audits.
class AudioEngine {
 public:
  struct Config {
    int deviceIndex = -1;            // -1 => default input/output device
    int sampleRate = 0;              // 0 => device's native default sample rate
    unsigned long framesPerBuffer = 64;
    int channels = 1;
    double suggestedLatency = 0.0;   // 0 => framesPerBuffer / sampleRate
    // Xruns reported in the first <startupGraceMs> ms of a stream are treated
    // as start-up transients (CoreAudio input overflow on first block) and
    // excluded from the steady-state count the exit criteria is judged on.
    int startupGraceMs = 500;
  };

  struct DeviceInfo {
    int index;
    std::string name;
    int maxInputChannels;
    int maxOutputChannels;
    double defaultSampleRate;
  };

  // Resolves the device + sample rate and stores the resolved values back into
  // `config` so the caller can report what actually ran.
  bool open(Config& config);
  bool start();
  void stop();
  void close();

  bool isOpen() const noexcept { return stream_ != nullptr; }
  bool rtBoostApplied() const noexcept { return rtBoostApplied_.load(); }

  const XrunCounter& xruns() const noexcept { return xruns_; }
  XrunCounter& xruns() noexcept { return xruns_; }

  std::string lastError() const noexcept { return lastError_; }

  static std::vector<DeviceInfo> listDevices();

 private:
  static int PaCallback(const void* input, void* output,
                        unsigned long frameCount,
                        const PaStreamCallbackTimeInfo* timeInfo,
                        PaStreamCallbackFlags flags, void* userData);

  int callbackRun(const void* input, void* output, unsigned long frameCount,
                  PaStreamCallbackFlags flags);

  PaStream* stream_ = nullptr;
  Config config_;
  PassthroughCallback passthrough_;
  XrunCounter xruns_;
  std::atomic<bool> rtBoostApplied_{false};
  std::atomic<bool> firstCallbackSeen_{false};
  std::chrono::steady_clock::time_point firstCallbackAt_{};
  std::string lastError_;
};