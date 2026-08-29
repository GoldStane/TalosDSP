#include "audio/AudioEngine.h"

#include <portaudio.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

struct Options {
  int deviceIndex = -1;
  int sampleRate = 0;
  int framesPerBuffer = 64;
  int channels = 1;
  int durationSeconds = 600;
  int statusIntervalSeconds = 10;
};

void usage(const char* argv0) {
  std::printf(
      "Usage: %s [options]\n"
      "\n"
      "  --device N             device index (see --list). Default: default\n"
      "                         input/output device.\n"
      "  --blocksize N          frames per buffer (default 64)\n"
      "  --sr N                 sample rate Hz (default: device native rate)\n"
      "  --channels N           channels (default 1)\n"
      "  --duration N           run for N seconds (default 600)\n"
      "  --status-interval N    print status every N seconds (default 10)\n"
      "  --list                 list audio devices and exit\n"
      "  --help                 this message\n"
      "\n"
      "Exit code is 0 iff the run completes with zero xruns.\n",
      argv0);
}

bool parseInt(const char* s, int& out) {
  char* end = nullptr;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0') {
    return false;
  }
  out = static_cast<int>(v);
  return true;
}

bool parseOptions(int argc, char** argv, Options& opts, bool& listOnly) {
  listOnly = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--list") {
      listOnly = true;
    } else if (arg == "--help" || arg == "-h") {
      usage(argv[0]);
      std::exit(0);
    } else if (arg == "--device" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.deviceIndex) || opts.deviceIndex < 0) {
        return false;
      }
    } else if (arg == "--blocksize" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.framesPerBuffer) || opts.framesPerBuffer <= 0) {
        return false;
      }
    } else if (arg == "--sr" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.sampleRate) || opts.sampleRate <= 0) {
        return false;
      }
    } else if (arg == "--channels" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.channels) || opts.channels <= 0) {
        return false;
      }
    } else if (arg == "--duration" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.durationSeconds) || opts.durationSeconds <= 0) {
        return false;
      }
    } else if (arg == "--status-interval" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.statusIntervalSeconds) ||
          opts.statusIntervalSeconds <= 0) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

void printDevices() {
  const auto devices = AudioEngine::listDevices();
  if (devices.empty()) {
    std::printf("No audio devices found.\n");
    return;
  }
  std::printf("%-5s %-8s %-8s %-12s %s\n", "Idx", "InCh", "OutCh", "DefaultSR",
              "Name");
  for (const auto& d : devices) {
    std::printf("%-5d %-8d %-8d %-12.0f %s\n", d.index, d.maxInputChannels,
                d.maxOutputChannels, d.defaultSampleRate, d.name.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options opts;
  bool listOnly = false;
  if (!parseOptions(argc, argv, opts, listOnly)) {
    usage(argv[0]);
    return 2;
  }

  const PaError initErr = Pa_Initialize();
  if (initErr != paNoError) {
    std::fprintf(stderr, "Pa_Initialize failed: %s\n", Pa_GetErrorText(initErr));
    return 1;
  }

  if (listOnly) {
    printDevices();
    Pa_Terminate();
    return 0;
  }

  AudioEngine::Config config;
  config.deviceIndex = opts.deviceIndex;
  config.sampleRate = opts.sampleRate;
  config.framesPerBuffer =
      static_cast<unsigned long>(opts.framesPerBuffer);
  config.channels = opts.channels;

  AudioEngine engine;
  if (!engine.open(config)) {
    std::fprintf(stderr, "Failed to open stream: %s\n",
                 engine.lastError().c_str());
    Pa_Terminate();
    return 1;
  }

  std::printf("TalosDSP passthrough: %d Hz, %lu frames/block (%0.2f ms), %d ch\n",
              config.sampleRate, config.framesPerBuffer,
              static_cast<double>(config.framesPerBuffer) / config.sampleRate *
                  1000.0,
              config.channels);

  if (!engine.start()) {
    std::fprintf(stderr, "Failed to start stream: %s\n",
                 engine.lastError().c_str());
    engine.close();
    Pa_Terminate();
    return 1;
  }

  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  const auto deadline = start + std::chrono::seconds(opts.durationSeconds);
  auto nextStatus = start + std::chrono::seconds(opts.statusIntervalSeconds);

  while (clock::now() < deadline) {
    if (clock::now() >= nextStatus) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start)
              .count();
      std::printf("[%3lld s] steady xruns: %llu / total %llu "
                  "(inOverflow=%llu outUnderflow=%llu "
                  "inUnderflow=%llu outOverflow=%llu)  rt-boost=%s\n",
                  static_cast<long long>(elapsed),
                  static_cast<unsigned long long>(engine.xruns().steadyTotal()),
                  static_cast<unsigned long long>(engine.xruns().total()),
                  static_cast<unsigned long long>(
                      engine.xruns().steadyInputOverflow()),
                  static_cast<unsigned long long>(
                      engine.xruns().steadyOutputUnderflow()),
                  static_cast<unsigned long long>(
                      engine.xruns().steadyInputUnderflow()),
                  static_cast<unsigned long long>(
                      engine.xruns().steadyOutputOverflow()),
                  engine.rtBoostApplied() ? "yes" : "no");
      nextStatus += std::chrono::seconds(opts.statusIntervalSeconds);
      std::fflush(stdout);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  engine.stop();
  engine.close();

  const auto steady = engine.xruns().steadyTotal();
  const auto total = engine.xruns().total();
  std::printf("done after %d s: %llu steady-state xruns "
              "(%llu including startup transient)\n",
              opts.durationSeconds,
              static_cast<unsigned long long>(steady),
              static_cast<unsigned long long>(total));
  std::fflush(stdout);

  Pa_Terminate();
  return steady == 0 ? 0 : 1;
}