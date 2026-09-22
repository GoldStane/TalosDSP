#include "audio/AudioEngine.h"
#include "audio/RtThreadBoost.h"

int AudioEngine::paCallback(const void* input, void* output,
                            unsigned long frameCount,
                            const PaStreamCallbackTimeInfo* /*timeInfo*/,
                            PaStreamCallbackFlags flags, void* userData) {
  return static_cast<AudioEngine*>(userData)->callbackRun(input, output,
                                                          frameCount, flags);
}

int AudioEngine::callbackRun(const void* input, void* output,
                             unsigned long frameCount,
                             PaStreamCallbackFlags flags) {
  if (!boostAttempted_) {
    boostAttempted_ = true;
    rtBoostApplied_.store(boostCurrentThreadForRt(), std::memory_order_relaxed);
  }

  if (!firstCallbackSeen_.load(std::memory_order_relaxed)) {
    firstCallbackSeen_.store(true, std::memory_order_relaxed);
    firstCallbackAt_ = std::chrono::steady_clock::now();
  }
  const bool inStartupGrace =
      std::chrono::steady_clock::now() - firstCallbackAt_ <
      std::chrono::milliseconds(config_.startupGraceMs);

  if (config_.useChain) {
    chain_.process(static_cast<const float*>(input),
                   static_cast<float*>(output),
                   static_cast<unsigned int>(frameCount),
                   static_cast<unsigned int>(config_.channels));
  } else {
    passthrough_.process(static_cast<const float*>(input),
                         static_cast<float*>(output),
                         static_cast<unsigned int>(frameCount),
                         static_cast<unsigned int>(config_.channels));
  }

  xruns_.record((flags & paInputOverflow) != 0, (flags & paOutputUnderflow) != 0,
                (flags & paInputUnderflow) != 0, (flags & paOutputOverflow) != 0,
                inStartupGrace);
  return paContinue;
}