# Improvement plan

This plan follows the September 2026 correctness fixes. The implementation below is now present. Offline evidence is recorded in BENCHMARKS.md; live loopback and external CI verification are still pending.

## 1. Move feature extraction to the classifier thread

Replace callback-side feature extraction and the unused summary snapshot queue with a bounded SPSC queue of fixed-size audio chunks. Include sample rate, channel count and a sequence number. The callback copies input and drops on full; the consumer owns the filterbank and window assembly. Detect gaps and reset filter/window state so missing audio is not treated as continuous. Publish queue-drop counts without callback logging.

Acceptance: identical features for contiguous input under arbitrary block sizes; deliberate consumer stalls cannot block the callback; queue overflow and recovery are tested. Benchmark callback cost with classification enabled and disabled.

## 2. Improve telemetry and controller stability

Keep full-block timing, which now includes feature extraction. Replace the coarse stage histogram with finer or logarithmic buckets, explicit overflow accounting and maximum observed cost. Report windowed P99/P999 and deadlines missed; do not add stage percentiles to estimate whole-block P99. Bound controller gain inputs, specify PID units and deterministic time steps, add anti-windup and recovery hysteresis, and expose timing-queue drops.

Acceptance: synthetic overload drives complexity down; recovery is bounded and does not oscillate. Test variable block budgets, missing telemetry and sustained overload. Publish measured callback-cost curves for every comb-count level.

## 3. Smooth effect and complexity transitions

Ramp preset parameters and crossfade reverb topology changes. Currently complexity changes the number of active combs; inactive comb state can reappear when complexity increases. Define whether tails are cleared, advanced or crossfaded. Hoist mixer trigonometry outside sample loops and benchmark before claiming a speedup.

Acceptance: transitions remain bounded and have no abrupt sample discontinuities above a documented threshold; impulse and sine reference tests cover every preset and complexity transition.

## 4. Expand DSP and classifier validation

Add analytical/reference tests for biquad frequency response, LR4 crossover gain, reverb impulse response and limiter transfer curves. The analysis filterbank is not a phase-compensated reconstruction crossover; document its approximation error. Add labeled percussive/tonal/ambient clips, silence handling and preset hysteresis, then report confusion matrices across supported sample rates.

Lookahead and oversampling were removed as nonfunctional settings. Implement them only with actual delay buffers and interpolation/antialias filters, documented latency and alias-rejection tests.

Acceptance: reproducible numerical tolerances, channel isolation and reset tests, plus a labeled classifier accuracy report. Test SPSC wraparound/full behavior and producer-consumer stress; add ThreadSanitizer lifecycle runs where supported.

## 5. Measure end-to-end behavior and maintain the checks

Build the planned physical/virtual loopback harness and a timed noninteractive runner. Record hardware, backend, sample rate, block size, xruns and raw latency samples. Re-enable automatic CI when repository policy permits; it remains manual-only. Add installed-PortAudio discovery and restrict project warning flags to project targets.

Acceptance: one documented command reproduces benchmark artifacts from a clean checkout; Linux and macOS CI pass tests, sanitizer builds and positive/negative RT symbol-check fixtures. Only then replace the <5 ms target with a measured claim.

## Implementation evidence

- AudioChunk/FeatureStream move extraction to the consumer and reset on metadata/sequence gaps. Overflow, chunk-boundary equivalence and lifecycle tests pass.
- Windowed timing, drop/deadline counters, bounded deterministic PID, ten-poll recovery, 1 µs histograms and maximum/overflow reporting are implemented.
- Ten-millisecond preset ramps and comb fades are implemented; inactive delay contents are overwritten before reuse. Transition and stale-tail regressions pass.
- DSP reference, SPSC stress, classifier hysteresis/silence and compiled symbol-audit fixtures are covered. The generated 45-clip diagnostic corpus and confusion matrices are reproducible.
- Offline benchmark runner, optional wired/virtual loopback probe, timed application mode, installed PortAudio discovery, scoped warnings and manual CI jobs are implemented.

## Current boundaries

The real-world labeled corpus, physical device loopback measurements, and Linux CI execution remain unverified here. CI stays manual-only per repository policy. Lookahead and oversampling remain optional future effects, not silently reintroduced placeholders. Generated clips establish diagnostic behavior only; they do not establish accuracy on real music or speech. No <5 ms end-to-end claim is made. Configuration/lifecycle methods require stopped audio and stopped controller workers; poll/reset helpers must not be called concurrently with their worker.
