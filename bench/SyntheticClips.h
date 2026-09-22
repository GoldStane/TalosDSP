#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

// Deterministic labeled diagnostic clips, not a real-world accuracy corpus.
inline std::vector<float> syntheticClip(unsigned label, unsigned rate, unsigned variant) {
  std::vector<float> audio(rate);
  uint32_t random=1234+variant;
  for(unsigned i=0;i<rate;++i) {
    const float t=static_cast<float>(i)/static_cast<float>(rate);
    if(label==0) {
      random=1664525u*random+1013904223u;
      const float noise=static_cast<float>(random>>8)/8388608.0f-1.0f;
      audio[i]=.3f*noise*std::exp(-8.0f*std::fmod(t*4.0f,1.0f));
    } else {
      const float hz=label==1 ? 2200.0f+static_cast<float>(variant)*100 : 90.0f+static_cast<float>(variant)*10;
      audio[i]=.2f*std::sin(6.28318530718f*hz*t);
    }
  }
  return audio;
}
