#pragma once
#include "audio/SPSCRingBuffer.h"
#include <atomic>
#include <cstdint>
#include <thread>

// Single timing producer; poll() is called only by the worker or offline tests.
// Configuration/start/stop require stopped audio and a stopped worker.
class ComplexityController {
  static_assert(std::atomic<uint64_t>::is_always_lock_free, "RT counters require lock-free atomics");
 public:
  explicit ComplexityController(std::atomic<uint8_t>* target, float kp=.5f, float ki=.01f, float kd=.1f);
  ~ComplexityController();
  ComplexityController(const ComplexityController&) = delete;
  ComplexityController& operator=(const ComplexityController&) = delete;
  void start();
  void stop();
  void setManualOverride(bool on) { manual_.store(on); }
  void setGains(float kp, float ki, float kd);
  void setPollInterval(uint32_t ms);
  void setTargetFraction(float fraction);
  void record(float cost, float budget) noexcept {
    if (cost > budget) missed_.fetch_add(1, std::memory_order_relaxed);
    if (!timings_.try_push({cost, budget})) drops_.fetch_add(1, std::memory_order_relaxed);
  }
  void poll();
  void update(float cost, float budget);
  uint64_t drops() const noexcept { return drops_.load(); }
  uint64_t missed() const noexcept { return missed_.load(); }
  float p99Us() const noexcept { return p99_.load(); }
  float p999Us() const noexcept { return p999_.load(); }
  uint32_t windowSamples() const noexcept { return samples_.load(); }
 private:
  struct Timing { float cost, budget; };
  SPSCRingBuffer<Timing, 4096> timings_;
  std::atomic<uint8_t>* target_;
  std::atomic<bool> running_{false}, manual_{false};
  std::thread thread_;
  std::atomic<uint64_t> drops_{0}, missed_{0};
  std::atomic<float> p99_{0}, p999_{0};
  std::atomic<uint32_t> samples_{0};
  float kp_{.5f}, ki_{.01f}, kd_{.1f}, integral_{0}, previous_{0}, fraction_{.6f};
  uint32_t pollMs_{100}, recovery_{0};
  bool havePrevious_{false};
  uint64_t seenDrops_{0};
  void run();
  void resetControl();
};
