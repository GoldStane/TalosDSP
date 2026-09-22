#pragma once

#include "audio/SPSCRingBuffer.h"
#include <atomic>
#include <chrono>
#include <thread>

class ComplexityController {
    std::atomic<uint8_t>* target_;
    std::atomic<bool> running_{false};
    std::atomic<bool> manual_override_{false};
    std::thread watchdog_;

    float Kp_{0.5f};
    float Ki_{0.01f};
    float Kd_{0.1f};
    float integral_{0.0f};
    float prev_error_{0.0f};
    std::chrono::steady_clock::time_point last_time_;

    struct Timing { float cost_us; float budget_us; };
    SPSCRingBuffer<Timing, 4096> timings_;
    uint32_t poll_ms_{100};
    float target_p99_fraction_{0.6f};

public:
    ComplexityController(std::atomic<uint8_t>* target,
                         float Kp = 0.5f, float Ki = 0.01f, float Kd = 0.1f);
    ~ComplexityController();

    ComplexityController(const ComplexityController&) = delete;
    ComplexityController& operator=(const ComplexityController&) = delete;

    void start();
    void stop();

    void setManualOverride(bool on) { manual_override_.store(on, std::memory_order_relaxed); }
    void setGains(float Kp, float Ki, float Kd) {
        Kp_ = Kp; Ki_ = Ki; Kd_ = Kd;
    }
    void setPollInterval(uint32_t ms) { poll_ms_ = ms; }
    void setTargetFraction(float frac) { target_p99_fraction_ = frac; }

    void record(float cost, float budget) noexcept { timings_.try_push({cost, budget}); }
    void poll();
    void update(float current_p99_us, float block_time_us);

private:
    void run();
};