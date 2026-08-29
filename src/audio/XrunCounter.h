#pragma once

#include <atomic>
#include <cstdint>

// Real-time-safe underflow/overflow counters.
//
// Lives in the RT core: only std::atomic loads/stores, no allocation,
// no locking. The PortAudio callback flags are folded into these counters
// once per block and drained by the control thread between polls.
//
// Xruns observed during a startup grace window (CoreAudio's first-block
// input overflow transient) are bucketed separately so that "glitch-free"
// validation measures steady-state behavior, not stream-start noise.
class XrunCounter {
 public:
  void record(bool inputOverflow, bool outputUnderflow, bool inputUnderflow,
              bool outputOverflow, bool inStartupGrace) noexcept {
    record_(inputOverflow, inStartupGrace, inputOverflow_, startupInputOverflow_);
    record_(outputUnderflow, inStartupGrace, outputUnderflow_,
            startupOutputUnderflow_);
    record_(inputUnderflow, inStartupGrace, inputUnderflow_,
            startupInputUnderflow_);
    record_(outputOverflow, inStartupGrace, outputOverflow_,
            startupOutputOverflow_);
  }

  std::uint64_t inputOverflow() const noexcept {
    return inputOverflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t outputUnderflow() const noexcept {
    return outputUnderflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t inputUnderflow() const noexcept {
    return inputUnderflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t outputOverflow() const noexcept {
    return outputOverflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t total() const noexcept {
    return inputOverflow() + outputUnderflow() + inputUnderflow() +
           outputOverflow();
  }

  // Steady-state counts exclude the startup grace window.
  std::uint64_t steadyInputOverflow() const noexcept {
    return inputOverflow() - startupInputOverflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t steadyOutputUnderflow() const noexcept {
    return outputUnderflow() -
           startupOutputUnderflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t steadyInputUnderflow() const noexcept {
    return inputUnderflow() - startupInputUnderflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t steadyOutputOverflow() const noexcept {
    return outputOverflow() - startupOutputOverflow_.load(std::memory_order_relaxed);
  }

  std::uint64_t steadyTotal() const noexcept {
    return total() - startupTotal_.load(std::memory_order_relaxed);
  }

 private:
  void record_(bool set, bool startup, std::atomic<std::uint64_t>& total,
               std::atomic<std::uint64_t>& startupBucket) noexcept {
    if (set) {
      total.fetch_add(1, std::memory_order_relaxed);
      if (startup) {
        startupBucket.fetch_add(1, std::memory_order_relaxed);
        startupTotal_.fetch_add(1, std::memory_order_relaxed);
      }
    }
  }

  std::atomic<std::uint64_t> inputOverflow_{0};
  std::atomic<std::uint64_t> outputUnderflow_{0};
  std::atomic<std::uint64_t> inputUnderflow_{0};
  std::atomic<std::uint64_t> outputOverflow_{0};
  std::atomic<std::uint64_t> startupInputOverflow_{0};
  std::atomic<std::uint64_t> startupOutputUnderflow_{0};
  std::atomic<std::uint64_t> startupInputUnderflow_{0};
  std::atomic<std::uint64_t> startupOutputOverflow_{0};
  std::atomic<std::uint64_t> startupTotal_{0};
};