# TalosDSP benchmarks

The <5 ms end-to-end latency remains a **target**, not a measured claim. Offline DSP cost is not device latency.

## Reproduction

```sh
python3 bench/run.py
```

This configures a Release build, runs the tests and RT audit, checks the loopback detector against a simulated 137-frame delay and missing returns, and writes `build-bench/results/`:

- `metadata.json`: OS, architecture, available CPU information, compiler, revision/dirty flag and source SHA-256 hashes.
- `stage.csv`: 2,000 raw whole-chain cost samples per comb-count/block-size/classifier combination, with audio-drop counts.
- `watchdog.csv`: actual busy-wait load injection, measured P99, complexity and missed deadlines for 120 deterministic control polls.
- `features.csv`: narrowband tone references quantifying the four-band centroid approximation.
- `classifier.csv`: labels, predictions, window counts and extraction/inference time for 45 generated PCM16 WAV clips.
- `summary.json`: cost summaries and classifier confusion matrices (rows=true, columns=predicted).
- `clips/`: reproducible labeled WAV files; filenames encode sample rate, class and variant. Labels 0/1/2 mean percussive/tonal/ambient.

Use `--build build --offline-deps` to reuse already downloaded dependencies. `--skip-build` requires an already built and validated checkout. All application/benchmark output paths are local; no audio device is opened by the default runner.

## Recorded offline run — 2026-09-22

Raw evidence and metadata are archived in [bench/results/2026-09-22](bench/results/2026-09-22). The large raw stage CSV is gzip-compressed; the runner emits it uncompressed. Generated WAVs are reproduced by the runner rather than stored in Git.

Host: macOS 26.6.2, arm64, 10 reported logical CPUs, Apple Clang 21.0.0. CPU model querying was denied by the host sandbox; metadata records this limitation. The source was the working tree based on `ed75812`, with per-source hashes in metadata. These are exploratory, unpinned, unpaced measurements; host load and clock frequency are uncontrolled.

At 48 kHz with 64-frame mono blocks and classification disabled, measured median / P99 whole-chain cost was:

- One comb: 0.542 / 0.584 µs.
- Two combs: 0.792 / 0.917 µs.
- Three combs: 1.250 / 1.375 µs.
- Four combs: 1.500 / 1.667 µs.

The complete curves, including 256-frame blocks and classification enabled, are in `summary.json` and `stage.csv.gz`. The enabled runs deliberately execute faster than real time and saturate the audio queue. Their drop counts are recorded; they measure callback behavior under consumer backlog, not sustained live classifier throughput. Differences between rows are not proof of a speedup without repeated controlled runs. No mixer optimization speedup is claimed.

The watchdog benchmark injects approximately 1,200 µs of busy-wait work against a 1,000 µs budget for polls 10–29. Complexity fell from 255 to 223 on the first overloaded poll, reached zero during sustained overload, and first recovered to four after ten clear polls. All 160 intentionally overloaded blocks missed the deadline: this demonstrates controller reaction and bounded recovery, not prevention of overload that exceeds the available DSP savings. The logical PID interval is 100 ms; benchmark polls run without wall-clock sleeps.

The baseline classifier correctly labeled all **45 synthetic clips** by majority window vote: five clips per class at each of 44.1, 48 and 96 kHz. Each rate's 3×3 confusion matrix has diagonal entries `[5, 5, 5]` and all other entries zero. Window-level results and runtime are in `classifier.csv`. The evaluator analyzes the same quantized PCM16 samples written to disk. These simple seeded noise bursts and sustained tones are diagnostic fixtures, not a held-out real-world accuracy evaluation.

The analysis bank is not a flat-summing reconstruction crossover. Its centroid is a weighted average of four band centers, not an FFT estimate: the archived `features.csv` measures bias against known pure tones. Low-frequency tones within the first band are represented near 750 Hz regardless of their exact frequency; this can bias a 100 Hz tone by roughly 650 Hz. Rolloff is quantized to band centers. Use these descriptors as coarse classifier inputs, not precision spectral measurements.

## Real-time telemetry

Stage and complete-block histograms use 1 µs buckets through 4.096 ms, record overflow counts and retain the maximum observed cost. Percentiles in an overflow bucket are lower bounds, never exact estimates above the histogram ceiling. Stage percentiles must not be summed to infer whole-block P99.

The watchdog uses its own recent timing queue. Every poll reports sample count, P99/P999 cost, cumulative deadline misses and timing drops. Control uses P99 *utilization* (cost / that block's budget), so varying block sizes are compared correctly. Empty windows do not cause recovery. Queue drops prevent prior recovery history from being trusted.

## Transition and numerical validation

Preset parameters and mixer gains ramp over 10 ms at the negotiated sample rate. Comb weights fade over the same period. Disabled combs stop consuming compute after fade-out; on reenable, their delay slots are overwritten before old contents can be read. The stale-tail regression checks that output remains below 1e-4 after the active tail has decayed.

Tests cover chunk-size-independent features, stereo isolation, queue gaps, full/wraparound/stress behavior, silence/preset hysteresis, missing telemetry, control saturation/recovery, LR4 -6 dB crossover amplitude, biquad DC/Nyquist response, analytical limiter transfer, reverb impulse first arrival and transition bounds. The mixer step bound is 0.002 per sample for the tested unit dry signal; the sine/impulse transition fixture uses a 0.15 full-scale adjacent-sample bound at 0.1 input amplitude. These fixture bounds are not a universal perceptual click guarantee.

Validation on this host: 53 Release tests and 53 ASan/UBSan tests passed; the three selected SPSC/lifecycle tests passed under ThreadSanitizer. The RT audit checks nine callback/DSP objects and 32 undefined-symbol references, including internal resolutions. Compiled safe/forbidden object fixtures validate the checker itself.

## Hardware or virtual loopback

Connect output to input on the **same full-duplex interface**, or select an equivalent synchronized virtual device. Use a quiet direct route: unrelated input transients can cause false detections. The probe emits 0.2-full-scale impulses at 250 ms intervals, detects threshold crossings at 0.08 within 200 ms, and records missing returns as -1. It measures callback sample-index round trip, including host buffering. It does not use wall-clock callback execution time as a substitute for I/O latency.

```sh
./build-bench/talosdsp --list
python3 bench/run.py --skip-build --loopback-device DEVICE_INDEX --count 10000 --chain none
# Repeat through the percussive DSP preset:
python3 bench/run.py --skip-build --output build-bench/full-loopback --loopback-device DEVICE_INDEX --count 10000 --chain full
```

The device index must be replaced with an actual full-duplex device from the list. Live runs also save `loopback.csv`, device/backend information, missing counts, xrun-callback counts and min/median/P95/P99/P999/max summaries. Runs with missing returns, backend failure or xruns exit nonzero and retain their evidence. The application independently supports `--duration 30` for noninteractive xrun checks.

No physical/virtual loopback run was performed in this environment. Linux CI and a real-world labeled corpus remain external validation tasks. CI remains manual-only under the existing repository policy.
