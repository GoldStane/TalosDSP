#include "audio/ComplexityController.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

ComplexityController::ComplexityController(std::atomic<uint8_t>* target, float kp, float ki, float kd)
    : target_(target) { setGains(kp, ki, kd); }
ComplexityController::~ComplexityController() { stop(); }
void ComplexityController::setGains(float kp, float ki, float kd) {
  auto bound = [](float x) { return std::isfinite(x) ? std::clamp(x, 0.0f, 2.0f) : 0.0f; };
  kp_=bound(kp); ki_=bound(ki); kd_=bound(kd);
}
void ComplexityController::setPollInterval(uint32_t ms) { pollMs_=std::clamp(ms, 10u, 1000u); }
void ComplexityController::setTargetFraction(float x) {
  fraction_=std::isfinite(x) ? std::clamp(x, .1f, .9f) : .6f;
}
void ComplexityController::resetControl() { integral_=previous_=0; recovery_=0; havePrevious_=false; }
void ComplexityController::start() {
  if (running_.load()) return;
  Timing stale{}; while (timings_.try_pop(stale)) {}
  resetControl(); seenDrops_=drops();
  running_.store(true);
  try { thread_=std::thread(&ComplexityController::run,this); }
  catch (...) { running_.store(false); throw; }
}
void ComplexityController::stop() {
  if (!running_.exchange(false)) return;
  if (thread_.joinable()) thread_.join();
}
void ComplexityController::update(float cost, float budget) {
  if (manual_.load() || !std::isfinite(cost) || !std::isfinite(budget) || budget<=0 || cost<0) {
    resetControl(); return;
  }
  // PID error is fraction of deadline. dt is the configured poll period in
  // seconds; output is complexity levels. Clamp integral and per-poll slew.
  const float error=fraction_-std::clamp(cost/budget,0.0f,10.0f);
  const float dt=static_cast<float>(pollMs_)/1000.0f;
  const float derivative=havePrevious_ ? (error-previous_)/dt : 0.0f;
  previous_=error; havePrevious_=true;
  const int current=target_->load();
  if (!((current==0 && error<0) || (current==255 && error>0)))
    integral_=std::clamp(integral_+error*dt,-1.0f,1.0f);
  const float effort=255.0f*(kp_*error+ki_*integral_+kd_*derivative);
  int delta=0;
  if (error<0) {
    recovery_=0;
    delta=static_cast<int>(std::floor(std::clamp(effort,-32.0f,-1.0f)));
  } else if (error>.1f) {
    if (++recovery_>=10) {
      delta=static_cast<int>(std::ceil(std::clamp(effort,1.0f,4.0f)));
      recovery_=0; integral_=0;
    }
  } else { recovery_=0; integral_=0; }
  target_->store(static_cast<uint8_t>(std::clamp(current+delta,0,255)));
}
void ComplexityController::poll() {
  std::array<float,4096> costs{}, ratios{};
  std::size_t n=0;
  Timing sample{};
  // Snapshot batch size prevents a fast producer extending the poll forever.
  const auto available=timings_.available();
  for (std::size_t i=0;i<available && timings_.try_pop(sample);++i) {
    if (std::isfinite(sample.cost) && std::isfinite(sample.budget) && sample.cost>=0 && sample.budget>0) {
      costs[n]=sample.cost; ratios[n++]=sample.cost/sample.budget;
    }
  }
  samples_.store(static_cast<uint32_t>(n));
  if (!n) { p99_.store(0); p999_.store(0); resetControl(); return; }
  std::sort(costs.begin(),costs.begin()+static_cast<std::ptrdiff_t>(n));
  std::sort(ratios.begin(),ratios.begin()+static_cast<std::ptrdiff_t>(n));
  const auto rank=[n](double q) { return static_cast<std::size_t>(std::ceil(q*static_cast<double>(n)))-1; };
  p99_.store(costs[rank(.99)]); p999_.store(costs[rank(.999)]);
  // A truncated window cannot justify raising complexity.
  const auto dropped=drops();
  if (dropped!=seenDrops_) { resetControl(); seenDrops_=dropped; }
  update(ratios[rank(.99)],1.0f);
}
void ComplexityController::run() {
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(pollMs_));
    poll();
  }
}
