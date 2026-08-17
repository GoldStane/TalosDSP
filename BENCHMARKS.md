# TalosDSP Benchmarks

This document is the evidence behind every latency claim TalosDSP makes. It is populated as Phase 4 of the [roadmap](ROADMAP.md) lands — until then, the sections below define the methodology so results are comparable and reproducible from day one.

**No numbers should be added here that weren't produced by the harnesses described below, on the hardware described below.** A benchmarks file with unverifiable numbers is worse than no benchmarks file.

---

## Methodology

### 1. Loopback latency (end-to-end)

Measures true microphone-to-speaker latency, not just processing time.

- **Setup:** physical audio loopback cable from output to input, or a virtual loopback device on CI
- **Method:** inject a single-sample impulse into the input stream, timestamp its arrival; timestamp the impulse's detection at the output (onset threshold crossing); round trip = output timestamp − input timestamp
- **Sample size:** N ≥ 10,000 impulses per run, spaced to avoid overlap
- **Reported:** min / median / P95 / P99 / P999 / max, plus a full histogram

Harness: `bench/loopback_latency.cpp` (planned)

### 2. Per-stage processing cost

Measures CPU time spent inside each DSP stage, in isolation from I/O.

- **Method:** `std::chrono::steady_clock` (or `rdtsc` with calibrated TSC frequency) wrapped around each stage's `process()` call inside the real audio callback, written to a lock-free histogram, drained by the watchdog thread
- **Reported:** per-stage min / median / P99, and sum vs. total block budget

### 3. Watchdog reaction time

Measures how quickly the complexity controller responds to injected load.

- **Method:** synthetically inflate one stage's cost (busy-loop of known duration) at a known timestamp; record how many watchdog polls elapse before `g_complexity_level` changes
- **Reported:** polls-to-react (target: within the documented 3-poll-to-decrement / 10-poll-to-increment design), and whether P99 latency stayed within budget during the transition

### 4. Classifier accuracy & runtime cost

- **Accuracy:** evaluated offline against a labeled test set (percussive / tonal / ambient clips), reported as a confusion matrix
- **Runtime cost:** wall-clock time of one classification pass (feature extraction + inference), measured on the classifier thread — this number matters for thread scheduling headroom, not for audio-path latency, since the classifier never touches the RT thread directly

---

## Reference hardware

*(to be filled in per benchmark run — CPU model, core count, OS + kernel version, whether running bare-metal or virtualized, and any RT kernel patches applied)*

| Field | Value |
|---|---|
| CPU | — |
| Cores/threads used | — |
| OS | — |
| Kernel / RT patch | — |
| Audio backend | — |
| Block size | — |
| Sample rate | — |

---

## Results

*No results yet — this section is populated in Phase 4 once the loopback harness and instrumentation land. Each result set should include the table above (hardware/config) alongside the numbers so results from different runs are never compared apples-to-oranges.*

### End-to-end loopback latency

| Metric | Value |
|---|---|
| Min | — |
| Median | — |
| P95 | — |
| P99 | — |
| P999 | — |
| Max | — |

### Per-stage cost (at default complexity level)

| Stage | Median | P99 |
|---|---|---|
| Reader / format conv | — | — |
| Delay/Reverb | — | — |
| Preset apply | — | — |
| Limiter/Mixer | — | — |

### Watchdog reaction

| Scenario | Polls to react | P99 during transition |
|---|---|---|
| Injected load spike | — | — |
| Load removed | — | — |

### Classifier

| Metric | Value |
|---|---|
| Accuracy (test set) | — |
| Per-pass runtime | — |

---

## Reproducing these results

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DTALOSDSP_BUILD_BENCHMARKS=ON
cmake --build build
./build/bench/loopback_latency
./build/bench/stage_cost
./build/bench/watchdog_reaction
./build/bench/classifier_eval
```

*(Benchmark targets are added incrementally as each harness is built — see the [roadmap](ROADMAP.md) Phase 4.)*
