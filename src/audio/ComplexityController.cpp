#include "audio/ComplexityController.h"

#include <algorithm>
#include <chrono>

using namespace std::chrono;

ComplexityController::ComplexityController(std::atomic<uint8_t>* target,
                                           float Kp, float Ki, float Kd)
    : target_(target), Kp_(Kp), Ki_(Ki), Kd_(Kd) {
    last_time_ = steady_clock::now();
}

ComplexityController::~ComplexityController() {
    stop();
}

void ComplexityController::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;
    last_time_ = steady_clock::now();
    integral_ = 0.0f;
    prev_error_ = 0.0f;
    watchdog_ = std::thread(&ComplexityController::run, this);
}

void ComplexityController::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    if (watchdog_.joinable()) watchdog_.join();
}

void ComplexityController::update(float current_p99_us, float block_time_us) {
    if (manual_override_.load(std::memory_order_relaxed)) return;

    float target = target_p99_fraction_ * block_time_us;
    float error = target - current_p99_us;

    auto now = steady_clock::now();
    float dt = duration<float>(now - last_time_).count();
    last_time_ = now;

    if (dt <= 0.0f) return;

    integral_ += error * dt;
    integral_ = std::clamp(integral_, -255.0f, 255.0f);

    float derivative = (error - prev_error_) / dt;
    prev_error_ = error;

    float output = Kp_ * error + Ki_ * integral_ + Kd_ * derivative;
    int new_level = int(target_->load(std::memory_order_relaxed) + output);
    new_level = std::clamp(new_level, 0, 255);
    target_->store(static_cast<uint8_t>(new_level), std::memory_order_relaxed);
}

void ComplexityController::run() {
    while (running_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}