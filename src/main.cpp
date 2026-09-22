#include "audio/AudioEngine.h"

#include <portaudio.h>

#include <chrono>
#include <cmath>
#include <cerrno>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>
#include <thread>
#include <sys/select.h>
#include <unistd.h>

static std::atomic<bool> g_stopRequested{false};

static void signalHandler(int) {
  g_stopRequested = true;
}

namespace {

struct Options {
  int deviceIndex = -1;          // Legacy: used for both input and output
  int inputDeviceIndex = -1;     // Input device index (preferred)
  int outputDeviceIndex = -1;    // Output device index (preferred)
  int sampleRate = 0;
  int framesPerBuffer = 64;
  int channels = 1;
  int statusIntervalSeconds = 10;
  bool useChain = true;          // false => plain passthrough (Phase 0 mode)
  float wet = -1.0f;             // -1 => mixer default (0.5)
  // Phase 2: watchdog / complexity
  bool watchdogEnabled = true;
  int manualComplexity = -1;     // -1 = auto (watchdog), 0-255 = manual
  float pidKp = 0.5f;
  float pidKi = 0.01f;
  float pidKd = 0.1f;
  // Phase 3: classifier / preset
  bool classifierEnabled = true;
  int manualPreset = -1;         // -1 = auto (classifier), 0-2 = manual
};

void usage(const char* argv0) {
  std::printf(
      "Usage: %s [options]\n"
      "\n"
      "  --device N             device index for BOTH input and output (see --list).\n"
      "                         Requires full-duplex device. Default: default devices.\n"
      "  --input-device N       input device index (see --list). Overrides --device.\n"
      "  --output-device N      output device index (see --list). Overrides --device.\n"
      "  --blocksize N          frames per buffer (default 64)\n"
      "  --sr N                 sample rate Hz (default: device native rate)\n"
      "  --channels N           channels (default 1)\n"
      "  --status-interval N    print status every N seconds (default 10)\n"
      "  --chain none|full      DSP chain: 'full' (Reader->Reverb->Mixer->\n"
      "                         Limiter) or 'none' (plain passthrough). "
      "Default full.\n"
      "  --wet N                reverb wet mix 0..1 (default 0.5)\n"
      "  --watchdog on|off      enable PID watchdog (default on)\n"
      "  --complexity N         manual complexity 0-255 (default auto)\n"
      "  --pid-kp N             PID proportional gain (default 0.5)\n"
      "  --pid-ki N             PID integral gain (default 0.01)\n"
      "  --pid-kd N             PID derivative gain (default 0.1)\n"
      "  --classifier on|off    enable classifier thread (default on)\n"
      "  --preset N             manual preset 0=percussive 1=tonal 2=ambient (default auto)\n"
      "  --list                 list audio devices and exit\n"
      "  --help                 this message\n"
      "\n"
      "Press 'q' + Enter to stop. Exit code is 0 only with zero steady-state xruns.\n",
      argv0);
}

bool parseInt(const char* s, int& out) {
  char* end = nullptr;
  errno = 0;
  const long v = std::strtol(s, &end, 10);
  if (end == s || *end != '\0' || errno == ERANGE || v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) {
    return false;
  }
  out = static_cast<int>(v);
  return true;
}

bool parseFloat(const char* s, float& out) {
  char* end = nullptr;
  errno = 0;
  const float v = std::strtof(s, &end);
  if (end == s || *end != '\0' || errno == ERANGE || !std::isfinite(v)) {
    return false;
  }
  out = v;
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
    } else if (arg == "--input-device" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.inputDeviceIndex) || opts.inputDeviceIndex < 0) {
        return false;
      }
    } else if (arg == "--output-device" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.outputDeviceIndex) || opts.outputDeviceIndex < 0) {
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
      if (!parseInt(argv[++i], opts.channels) || opts.channels <= 0 || opts.channels > 2) {
        return false;
      }
    } else if (arg == "--status-interval" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.statusIntervalSeconds) ||
          opts.statusIntervalSeconds <= 0) {
        return false;
      }
    } else if (arg == "--chain" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "full" && value != "none") return false;
      opts.useChain = value == "full";
    } else if (arg == "--wet" && i + 1 < argc) {
      if (!parseFloat(argv[++i], opts.wet) || opts.wet < 0 || opts.wet > 1) {
        return false;
      }
    } else if (arg == "--watchdog" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "on" && value != "off") return false;
      opts.watchdogEnabled = value == "on";
    } else if (arg == "--complexity" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.manualComplexity) ||
          opts.manualComplexity < 0 || opts.manualComplexity > 255) {
        return false;
      }
    } else if (arg == "--pid-kp" && i + 1 < argc) {
      if (!parseFloat(argv[++i], opts.pidKp)) return false;
    } else if (arg == "--pid-ki" && i + 1 < argc) {
      if (!parseFloat(argv[++i], opts.pidKi)) return false;
    } else if (arg == "--pid-kd" && i + 1 < argc) {
      if (!parseFloat(argv[++i], opts.pidKd)) return false;
    } else if (arg == "--classifier" && i + 1 < argc) {
      const std::string value = argv[++i];
      if (value != "on" && value != "off") return false;
      opts.classifierEnabled = value == "on";
    } else if (arg == "--preset" && i + 1 < argc) {
      if (!parseInt(argv[++i], opts.manualPreset) ||
          opts.manualPreset < 0 || opts.manualPreset > 2) {
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
  config.inputDeviceIndex = opts.inputDeviceIndex;
  config.outputDeviceIndex = opts.outputDeviceIndex;
  config.sampleRate = opts.sampleRate;
  config.framesPerBuffer =
      static_cast<unsigned long>(opts.framesPerBuffer);
  config.channels = opts.channels;
  config.useChain = opts.useChain;

  AudioEngine engine;
  if (!engine.open(config)) {
    std::fprintf(stderr, "Failed to open stream: %s\n",
                 engine.lastError().c_str());
    Pa_Terminate();
    return 1;
  }
  if (opts.useChain && opts.wet >= 0.0f) {
    engine.chain().mixer().setWet(opts.wet);
  }

  std::printf(
      "TalosDSP %s: %d Hz, %lu frames/block (%0.2f ms), %d ch\n",
      opts.useChain ? "chain(reverb)"
                    : "passthrough",
      config.sampleRate, config.framesPerBuffer,
      static_cast<double>(config.framesPerBuffer) / config.sampleRate * 1000.0,
      config.channels);

  // Phase 2: Watchdog / complexity setup
  if (opts.useChain) {
    if (opts.manualComplexity >= 0) {
      engine.chain().setManualComplexity(static_cast<uint8_t>(opts.manualComplexity));
      engine.chain().setWatchdogEnabled(false);
    } else if (opts.watchdogEnabled) {
      engine.chain().setPIDGains(opts.pidKp, opts.pidKi, opts.pidKd);
      engine.chain().setWatchdogEnabled(true);
      engine.chain().startWatchdog();
    }

    // Phase 3: Classifier / preset setup
    if (opts.manualPreset >= 0) {
      engine.chain().setManualPreset(static_cast<uint8_t>(opts.manualPreset));
      engine.chain().setClassifierEnabled(false);
    } else if (opts.classifierEnabled) {
      engine.chain().setClassifierEnabled(true);
      engine.chain().startClassifier();
    }
  }

  if (!engine.start()) {
    std::fprintf(stderr, "Failed to start stream: %s\n",
                 engine.lastError().c_str());
    engine.close();
    Pa_Terminate();
    return 1;
  }

  // Install signal handlers for graceful shutdown
  std::signal(SIGINT, signalHandler);   // Ctrl+C
  std::signal(SIGTERM, signalHandler);  // kill command

  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  auto nextStatus = start + std::chrono::seconds(opts.statusIntervalSeconds);

  bool stopRequested = false;

  while (!stopRequested && !g_stopRequested) {
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

    // Check for stdin input (press 'q' + Enter to stop)
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv = {0, 100000};  // 100ms timeout
    int ret = select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv);
    if (ret > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
      char c;
      if (read(STDIN_FILENO, &c, 1) > 0 && (c == 'q' || c == 'Q')) {
        stopRequested = true;
      }
    }
    stopRequested = stopRequested || g_stopRequested.load();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  engine.stop();
  if (opts.useChain) {
    engine.chain().stopClassifier();
    engine.chain().stopWatchdog();
  }
  engine.close();

  const auto elapsedSec = std::chrono::duration_cast<std::chrono::seconds>(clock::now() - start).count();
  const auto steady = engine.xruns().steadyTotal();
  const auto total = engine.xruns().total();
  std::printf("done after %lld s: %llu steady-state xruns "
              "(%llu including startup transient)\n",
              static_cast<long long>(elapsedSec),
              static_cast<unsigned long long>(steady),
              static_cast<unsigned long long>(total));

  if (opts.useChain) {
    auto printHist = [](const char* name, const StageHistogram& h) {
      if (h.total() == 0) return;
      std::printf("    %-9s median=%llu ns  p99=%llu ns  (n=%llu)\n", name,
                  static_cast<unsigned long long>(h.medianNs()),
                  static_cast<unsigned long long>(h.p99Ns()),
                  static_cast<unsigned long long>(h.total()));
    };
    std::printf("per-stage cost (RT thread, steady_clock):\n");
    auto& ch = engine.chain();
    printHist("Reader", ch.reader().histogram());
    printHist("Freeverb", ch.reverb().histogram());
    printHist("Mixer", ch.mixer().histogram());
    printHist("Limiter", ch.limiter().histogram());
  }
  std::fflush(stdout);

  Pa_Terminate();
  return steady == 0 ? 0 : 1;
}