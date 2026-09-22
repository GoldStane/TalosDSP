# TalosDSP

**A parallel, adaptive, low-latency audio pipeline with built-in performance telemetry and a lightweight on-device ML classifier for dynamic effect switching.**

Named after the bronze automaton of Greek myth — a single tireless guardian, always watching, always adjusting. TalosDSP is built the same way: one real-time audio path doing the work, with a ring of background sentinels (a latency watchdog and an audio classifier) that observe and adapt it without ever touching the critical path.

C++17 · MIT licensed · target end-to-end latency **< 5 ms**

---

## What it does

TalosDSP takes microphone input, runs it through a cascade of DSP stages (delay/reverb → adaptive preset → limiter/mixer), and outputs it in real time. Two background threads make it *adaptive*:

- **Watchdog** — polls per-block timing telemetry and dials DSP complexity up or down to hold a P99 latency budget under load.
- **Classifier** — consumes lightweight audio features (RMS, zero-crossing rate, 4-band energy) every 50 ms, runs them through an on-device decision tree (or a tiny hand-exported model), and switches effect presets based on whether the input is percussive, tonal, or ambient.

Controllers communicate through lock-free atomics and bounded SPSC queues. Feature extraction currently runs in the callback only when classification is enabled; moving it off-thread is the first item in [IMPROVEMENTS.md](IMPROVEMENTS.md). Lifecycle/configuration calls require the audio stream to be stopped.

## Why "parallel" doesn't mean "one stage per core"

An earlier draft of this design pipelined each DSP stage on its own core, connected by SPSC queues. It looked good on a diagram and was wrong for the stated goal: queueing between stages adds at least one block period of latency *per hop*, so a 4-stage queue pipeline at a 64-sample block (1.33 ms @ 48kHz) burns over 5 ms just moving data around — before any actual DSP.

TalosDSP's parallelism lives in the **control plane, not the data plane**:

```
AUDIO THREAD (best-effort platform RT priority) — the ONLY thread touching live audio
  PortAudio callback:
    Reader → Delay/Reverb → Preset Apply → Limiter/Mixer → Output
    (sequential, in-place, zero allocation, zero locks, zero queue hops)
    reads:  g_complexity_level, g_preset_id      (atomic, lock-free)
    writes: per-block timing histogram           (atomic)
    writes: full-block timing and audio features (bounded SPSC, drop-on-full)

Watchdog thread (100 ms poll)
  reads recent block utilization → P99 PID → adjusts complexity

Classifier thread (50 ms poll)
  reads feature queue → decision tree
  → writes g_preset_id
```

One real-time thread owns the entire signal chain sequentially, so the audio path is provably free of cross-thread stalls. Background controllers never wait on the audio thread. Scheduling and device behavior still require measurement.

## Design principles

- **Real-time safety is enforced, not assumed.** No `malloc`/`new`, no locks, no blocking syscalls in any function reachable from the audio callback. Checked with sanitizer tests and an object-symbol audit (see `tools/rt-safety/`). The audit is not a complete call-graph proof.
- **Graceful degradation over glitching.** Under load, the watchdog reduces DSP complexity (one to four active reverb combs) before it lets latency blow the budget.
- **Inference stays local.** The classifier runs entirely on-device — no network calls anywhere near the audio path, or anywhere in the runtime at all. If a cloud service is ever used, it is strictly for *offline* model training; inference is a self-contained C++ routine.
- **Every latency claim is measured, not asserted.** See `BENCHMARKS.md` for the loopback methodology and raw histograms.

## Status

The sequential `Reader → Freeverb → Mixer → Limiter` chain, PID watchdog and baseline decision-tree classifier are implemented. Presets control sound parameters; complexity controls active reverb comb count. Feature extraction uses the negotiated stream sample rate and independent stereo filter state. Limiter lookahead and oversampling are not implemented.

Offline tests cover core stages, buffer boundaries, preset behavior, controller response and classifier handoff. Historical live-run notes are not a reproducible benchmark for this version; the **<5 ms latency is a target**, pending the harnesses in `BENCHMARKS.md`. See [IMPROVEMENTS.md](IMPROVEMENTS.md) for the next implementation steps.

## Building

```bash
git clone https://github.com/GoldStane/TalosDSP.git
cd TalosDSP
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires a C++17 compiler and PortAudio (fetched automatically via CMake `FetchContent`).

Validate the passthrough (exit code 0 iff zero steady-state xruns):

```bash
cmake --build build --target check     # unit tests + rt-safety harness
./build/talosdsp --blocksize 64 --sr 48000
./build/talosdsp --list    # enumerate audio devices
```

Run the full DSP chain (default) or fall back to plain passthrough:

```bash
./build/talosdsp --chain full --wet 0.4
./build/talosdsp --chain none     # Phase 0 passthrough regression
```

Press 'q' + Enter to stop. At the end of a `--chain full` run, per-stage median/P99 cost (RT-thread, `steady_clock`) is printed.

## License

MIT — see [LICENSE](LICENSE).
