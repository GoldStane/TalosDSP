#include "audio/DspChain.h"

DspChain::DspChain() = default;
DspChain::~DspChain() = default;

void DspChain::setWatchdogEnabled(bool enabled) {
  watchdog_enabled_ = enabled;
}

void DspChain::setManualComplexity(uint8_t level) {
  g_complexity_.store(level, std::memory_order_relaxed);
  controller_.setManualOverride(true);
}

void DspChain::setPIDGains(float Kp, float Ki, float Kd) {
  controller_.setGains(Kp, Ki, Kd);
}

void DspChain::startWatchdog() {
  if (watchdog_enabled_) controller_.start();
}

void DspChain::stopWatchdog() {
  controller_.stop();
}

void DspChain::setClassifierEnabled(bool enabled) {
  classifier_enabled_ = enabled;
}

void DspChain::setManualPreset(uint8_t preset) {
  if (preset < static_cast<uint8_t>(Preset::COUNT)) {
    classifier_.setManualOverride(true);
    g_preset_id_.store(preset, std::memory_order_relaxed);
  }
}

void DspChain::startClassifier() {
  feature_extractor_.init(sample_rate_, 1024);
  classifier_.setManualOverride(false);
  classifier_.start();
  classifier_enabled_ = true;
}

void DspChain::stopClassifier() {
  classifier_.stop();
  classifier_enabled_ = false;
}

