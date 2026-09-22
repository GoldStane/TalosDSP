#pragma once
#include <cstdint>

class LinearRamp {
 public:
  LinearRamp() noexcept = default;
  explicit LinearRamp(float value) noexcept : value_(value), target_(value) {}
  void snap(float value) noexcept { value_=target_=value; remaining_=0; }
  void target(float value, uint32_t frames) noexcept {
    if (value==target_) return;
    target_=value; remaining_=frames ? frames : 1;
    step_=(target_-value_)/static_cast<float>(remaining_);
  }
  float next() noexcept {
    if (remaining_ && --remaining_ == 0) value_=target_;
    else if (remaining_) value_+=step_;
    return value_;
  }
  float value() const noexcept { return value_; }
 private:
  float value_{0}, target_{0}, step_{0};
  uint32_t remaining_{0};
};
