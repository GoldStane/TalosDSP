#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Measures callback sample-index round trip, including host I/O buffering.
// Use a quiet wired/virtual loopback, not acoustic speaker/microphone coupling.
struct LoopbackProbe {
  std::vector<int64_t> delays;
  uint64_t frame=0, sentAt=0;
  unsigned interval, timeout, emitted=0;
  bool pending=false;
  float previous=0;
  uint64_t xruns=0;
  LoopbackProbe(unsigned count,unsigned rate) : delays(count,-1),interval(rate/4),timeout(rate/5) {}
  void process(const float* input,float* output,unsigned frames) noexcept {
    for(unsigned i=0;i<frames;++i,++frame) {
      const float sample=input ? input[i] : 0;
      if(pending && frame>sentAt && frame-sentAt<=timeout &&
          std::fabs(sample)>=.08f && std::fabs(previous)<.08f) {
        delays[emitted-1]=static_cast<int64_t>(frame-sentAt); pending=false;
      }
      if(pending && frame-sentAt>timeout) pending=false;
      previous=sample;
      output[i]=0;
      if(frame%interval==0 && emitted<delays.size()) {
        output[i]=.2f; sentAt=frame; ++emitted; pending=true;
      }
    }
  }
  bool done() const noexcept { return emitted==delays.size() && frame>sentAt+interval; }
};
