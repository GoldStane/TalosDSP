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
  if (config.channels < 1 || config.channels > 2 || config.framesPerBuffer == 0 || config.sampleRate < 0) {
    lastError_ = "expected mono/stereo, positive block size and nonnegative sample rate";
    return false;
  }
  if (stream_ != nullptr) {
    lastError_ = "stream already open";
    return false;
  }

  // Resolve input device: prefer explicit inputDeviceIndex, then legacy deviceIndex, then default
  PaDeviceIndex inputDevice = paNoDevice;
  if (config.inputDeviceIndex >= 0) {
    inputDevice = config.inputDeviceIndex;
  } else if (config.deviceIndex >= 0) {
    inputDevice = config.deviceIndex;
  } else {
    inputDevice = Pa_GetDefaultInputDevice();
  }

  // Resolve output device: prefer explicit outputDeviceIndex, then legacy deviceIndex, then default
  PaDeviceIndex outputDevice = paNoDevice;
  if (config.outputDeviceIndex >= 0) {
    outputDevice = config.outputDeviceIndex;
  } else if (config.deviceIndex >= 0) {
    outputDevice = config.deviceIndex;
  } else {
    outputDevice = Pa_GetDefaultOutputDevice();
  }

  if (inputDevice == paNoDevice && outputDevice == paNoDevice) {
    lastError_ = "no input or output device available";
    return false;
  }

  // Validate input device supports requested channels
  if (inputDevice != paNoDevice) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(inputDevice);
    if (info == nullptr || info->maxInputChannels < config.channels) {
      lastError_ = "input device does not support requested channel count";
      return false;
    }
  }

  // Validate output device supports requested channels
  if (outputDevice != paNoDevice) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(outputDevice);
    if (info == nullptr || info->maxOutputChannels < config.channels) {
      lastError_ = "output device does not support requested channel count";
      return false;
    }
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

  if (sampleRate < 8000 || sampleRate > 192000) {
    lastError_ = "sample rate must be between 8000 and 192000 Hz";
    return false;
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

  chain_.setSampleRate(static_cast<float>(sampleRate));
  config.sampleRate = sampleRate;
  config.inputDeviceIndex = inputDevice;
  config.outputDeviceIndex = outputDevice;
  config_ = config;
  passthrough_.setChannels(static_cast<unsigned int>(config.channels));

  const PaError err = Pa_OpenStream(
      &stream_, input, output, sampleRate, config.framesPerBuffer,
      paNoFlag, &AudioEngine::paCallback, this);
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
  boostAttempted_ = false;
  firstCallbackSeen_.store(false, std::memory_order_relaxed);
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
