#pragma once

// Boosts the calling thread to real-time-class scheduling so it can own the
// audio callback without being preempted by normal-priority work.
//
// Called exactly once from the audio callback's first invocation, on the
// thread PortAudio created for it (CoreAudio's I/O thread on macOS). The
// callback keeps running on that same thread for the stream's lifetime, which
// is the "one RT thread owns the whole signal chain" property from the README.
//
// Platform semantics:
//   macOS   - THREAD_TIME_CONSTRAINT_POLICY (mach), with QoS as a fallback.
//             CoreAudio's I/O thread is already time-constrained; this is
//             belt-and-braces so we hold the budget if CoreAudio downgrades.
//   Linux   - SCHED_FIFO priority 80 via pthread_setschedparam.
//   Windows - no-op for now (QoS via MMCSS is a later phase).
//
// RT-safe: called once, only pthread/mach syscalls, no allocation, no locks.
bool BoostCurrentThreadForRt() noexcept;