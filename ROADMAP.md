# TalosDSP Roadmap

This roadmap tracks the phased build-out of TalosDSP. Each phase should land as one or more PRs with tests/benchmarks attached — the goal is a repo history that itself demonstrates the engineering process, not just a finished artifact.

Status legend: `[ ]` not started · `[~]` in progress · `[x]` done

---

## Phase 0 — Skeleton

Goal: a running, pinned, real-time-safe audio thread that does nothing but loopback input to output.

- [x] CMake project (`CMakeLists.txt`, `FetchContent` for PortAudio, optional vcpkg toolchain file)
- [x] MIT `LICENSE` at repo root
- [x] Real-time audio thread: PortAudio callback wired to a dedicated pinned thread with `SCHED_FIFO` (Linux) / equivalent QoS (macOS/Windows)
- [x] Pure passthrough (input → output, no processing) to validate zero xruns at target block size
- [x] GitHub Actions CI: build matrix (Linux + macOS), clang-tidy pass, sanitizer build (ASan/UBSan) on non-RT test code
- [x] `tools/rt-safety/`: clang-tidy check or sanitizer harness that flags allocation/locking reachable from the audio callback

**Exit criteria:** passthrough build runs glitch-free for 10+ minutes at 64-sample blocks on reference hardware.

## Phase 1 — DSP Stages

Goal: the full sequential signal chain, each stage independently testable offline.

- [x] `IDspStage` interface (process-in-place, no allocation, deterministic)
- [x] Delay/Reverb stage (Schroeder/Freeverb-style to start)
- [x] Limiter/Mixer stage
- [x] Per-stage cost instrumentation (`steady_clock` or `rdtsc`, written to atomic histograms)
- [x] Offline unit tests: known impulse responses / reference outputs per stage
- [ ] Stretch: partitioned convolution reverb (SIMD showcase)

**Exit criteria:** full chain runs in the callback with measured per-stage cost, still glitch-free. Repeat live verification after the control-plane changes; historical runs do not establish current latency.

## Phase 2 — Control Plane

Goal: the watchdog loop and the lock-free channel it needs.

- [x] Complexity atomic + watchdog thread (100 ms poll, P99-based up/down adjustment)
- [x] Wire complexity into active reverb comb count; preset parameters remain independent
- [x] Bounded SPSC audio chunks: callback copies, classifier extracts; sequence gaps and drops are tracked
- [x] Synthetic busy-wait load benchmark and deterministic controller response tests

**Exit criteria:** watchdog demonstrably prevents sustained P99 latency breaches under injected load, verified by test, not eyeballing.

## Phase 3 — Classifier

Goal: on-device audio content classification driving preset selection.

- [x] Feature extraction: RMS, zero-crossing rate, 4-band energy (biquad filterbank)
- [x] Hardcoded decision tree (percussive / tonal / ambient) as the baseline
- [x] Per-chain preset atomic feeds parameter selection in the RT thread
- [ ] Stretch: tiny offline-trained model (Python/PyTorch), weights exported to a plain C array, hand-written inference in C++ (no runtime ML framework dependency)
- [x] Synthetic labeled diagnostic clips + offline confusion matrices (real-world corpus still pending)

**Exit criteria:** classifier switches presets correctly on a labeled test set and never allocates or blocks in its runtime path.

## Phase 4 — Measurement & Proof

Goal: the evidence behind every latency claim in the README.

- [x] Wired/virtual loopback harness and simulated-delay tests (hardware run pending)
- [ ] P99/P999 jitter histograms under normal and synthetic-load conditions
- [ ] `BENCHMARKS.md` populated with real numbers, hardware specs, and methodology
- [x] Reproducibility: offline benchmarks and optional loopback runnable via bench/run.py and manual CI

**Exit criteria:** `BENCHMARKS.md` numbers are reproducible by a third party from a clean checkout.

## Phase 5 — Polish

Goal: portfolio-ready presentation.

- [ ] README architecture diagram finalized
- [ ] Short terminal recording / GIF of the watchdog adapting live
- [ ] "Design decisions & tradeoffs" write-up (the queue-vs-single-thread reasoning, why inference stays local, etc.)
- [ ] Tag a `v0.1.0` release

---

## Non-goals (for now)

- Cross-platform mobile builds (iOS/Android)
- A GUI/plugin (VST/AU) wrapper — may become a future phase once the core is stable
- Multi-channel/surround support beyond stereo

## Correctness follow-up

See [IMPROVEMENTS.md](IMPROVEMENTS.md) for the ordered plan, acceptance criteria and remaining measurement work after the September 2026 fixes. Feature extraction now runs on the classifier thread. See BENCHMARKS.md for offline evidence and explicitly pending hardware measurements.
