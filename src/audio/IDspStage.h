#pragma once

#include "audio/StageHistogram.h"

#include <cstdint>

// The Phase 1 DSP stage contract. Every stage is:
//   - process(in, out, frames, channels): in-place or copy, no allocation
//   - reset(): clear internal state (RT-safe, no allocation)
//   - name(): for telemetry
//   - histogram(): this stage's per-call cost, written from the RT thread
//   - setComplexity(level): continuous 0-255 complexity control
//   - getComplexity(): current complexity level
//
// Virtual dispatch is RT-safe (no allocation, no locking). Stages must never
// allocate, lock, or block in any method reachable from the audio callback.
class IDspStage {
 public:
  // Stages are owned by value; deletion through the interface is unsupported.
 protected:
  ~IDspStage() = default;
 public:

  virtual void process(const float* input, float* output, uint32_t frames,
                       uint32_t channels) noexcept = 0;

  virtual void reset() noexcept = 0;

  virtual const char* name() const noexcept = 0;

  virtual StageHistogram& histogram() noexcept = 0;

  virtual void setComplexity(uint8_t level) noexcept = 0;
  virtual uint8_t getComplexity() const noexcept = 0;
};