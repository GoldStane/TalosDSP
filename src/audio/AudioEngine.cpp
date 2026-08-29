#include "audio/AudioEngine.h"

#include "audio/RtThreadBoost.h"

#include <portaudio.h>

#include <cstdio>

namespace {

constexpr int kPaNoError = paNoError;

}  // namespace

std::vector<AudioEngine::DeviceInfo> AudioEngine::listDevices() {
  std::vector<DeviceInfo> devices;
  const PaDeviceIndex count = Pa_GetDeviceCount();
  if (count < 0) {
    return devices;
  }
  devices.reserve(static_cast<std::size_t>(count));
  for (PaDeviceIndex i = 0; i < count; ++i) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
    if (info == nullptr) {
      continue;
    }
    devices.push_back(
        DeviceInfo{i, info->name, info->maxInputChannels,
                   info->maxOutputChannels, info->defaultSampleRate});
  }
  return devices;
}

bool AudioEngine::open(Config& config) {
  if (stream_ != nullptr) {
    lastError_ = "stream already open";
    return false;
  }

  const PaDeviceIndex inputDevice =
      config.deviceIndex >= 0 ? config.deviceIndex : Pa_GetDefaultInputDevice();
  const PaDeviceIndex outputDevice =
      config.deviceIndex >= 0 ? config.deviceIndex : Pa_GetDefaultOutputDevice();
  if (inputDevice == paNoDevice && outputDevice == paNoDevice) {
    lastError_ = "no input or output device available";
    return false;
  }

  int sampleRate = config.sampleRate;
  if (sampleRate == 0) {
    const PaDeviceIndex rateRef =
        inputDevice != paNoDevice ? inputDevice : outputDevice;
    const PaDeviceInfo* info = Pa_GetDeviceInfo(rateRef);
    if (info == nullptr) {
      lastError_ = "unable to query device sample rate";
      return false;
    }
    sampleRate = static_cast<int>(info->defaultSampleRate);
  }

  const double latency =
      config.suggestedLatency > 0.0
          ? config.suggestedLatency
          : static_cast<double>(config.framesPerBuffer) / sampleRate;

  PaStreamParameters inputParams{};
  PaStreamParameters outputParams{};
  PaStreamParameters* input = nullptr;
  PaStreamParameters* output = nullptr;

  if (inputDevice != paNoDevice) {
    inputParams.device = inputDevice;
    inputParams.channelCount = config.channels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = latency;
    inputParams.hostApiSpecificStreamInfo = nullptr;
    input = &inputParams;
  }
  if (outputDevice != paNoDevice) {
    outputParams.device = outputDevice;
    outputParams.channelCount = config.channels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = latency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    output = &outputParams;
  }

    config.sampleRate = sampleRate;
  config_ = config;
  passthrough_.setChannels(static_cast<unsigned int>(config.channels));

  const PaError err = Pa_OpenStream(
      &stream_, input, output, sampleRate, config.framesPerBuffer,
      paNoFlag, &AudioEngine::PaCallback, this);
  if (err != kPaNoError) {
    lastError_ = Pa_GetErrorText(err);
    stream_ = nullptr;
    return false;
  }
  return true;
}

bool AudioEngine::start() {
  if (stream_ == nullptr) {
    lastError_ = "stream is not open";
    return false;
  }
  const PaError err = Pa_StartStream(stream_);
  if (err != kPaNoError) {
    lastError_ = Pa_GetErrorText(err);
    return false;
  }
  return true;
}

void AudioEngine::stop() {
  if (stream_ == nullptr) {
    return;
  }
  const PaError err = Pa_StopStream(stream_);
  if (err != kPaNoError) {
    lastError_ = Pa_GetErrorText(err);
  }
}

void AudioEngine::close() {
  if (stream_ == nullptr) {
    return;
  }
  const PaError err = Pa_CloseStream(stream_);
  if (err != kPaNoError) {
    lastError_ = Pa_GetErrorText(err);
  }
  stream_ = nullptr;
}

int AudioEngine::PaCallback(const void* input, void* output,
                            unsigned long frameCount,
                            const PaStreamCallbackTimeInfo* /*timeInfo*/,
                            PaStreamCallbackFlags flags, void* userData) {
  return static_cast<AudioEngine*>(userData)->callbackRun(input, output,
                                                          frameCount, flags);
}

int AudioEngine::callbackRun(const void* input, void* output,
                             unsigned long frameCount,
                             PaStreamCallbackFlags flags) {
  static thread_local bool boosted = false;
  if (!boosted) {
    boosted = BoostCurrentThreadForRt();
    rtBoostApplied_.store(boosted, std::memory_order_relaxed);
  }

  if (!firstCallbackSeen_.load(std::memory_order_relaxed)) {
    firstCallbackSeen_.store(true, std::memory_order_relaxed);
    firstCallbackAt_ = std::chrono::steady_clock::now();
  }
  const bool inStartupGrace =
      std::chrono::steady_clock::now() - firstCallbackAt_ <
      std::chrono::milliseconds(config_.startupGraceMs);

  passthrough_.process(static_cast<const float*>(input),
                       static_cast<float*>(output),
                       static_cast<unsigned int>(frameCount),
                       static_cast<unsigned int>(config_.channels));

  xruns_.record((flags & paInputOverflow) != 0, (flags & paOutputUnderflow) != 0,
                (flags & paInputUnderflow) != 0, (flags & paOutputOverflow) != 0,
                inStartupGrace);
  return paContinue;
}