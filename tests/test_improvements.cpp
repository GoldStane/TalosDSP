#include "audio/DspChain.h"
#include "audio/FeatureStream.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <vector>
#include <thread>
#include <cmath>
#include <limits>

TEST_CASE("SPSC wraparound and full preserve FIFO") {
  SPSCRingBuffer<unsigned,8> q;
  REQUIRE(q.capacity()==7);
  for (unsigned round=0;round<20;++round) {
    for (unsigned i=0;i<7;++i) REQUIRE(q.try_push(round*7+i));
    REQUIRE_FALSE(q.try_push(123));
    for (unsigned i=0;i<7;++i) { unsigned x=0; REQUIRE(q.try_pop(x)); REQUIRE(x==round*7+i); }
    unsigned x; REQUIRE_FALSE(q.try_pop(x));
  }
}
TEST_CASE("SPSC producer consumer stress preserves order") {
  SPSCRingBuffer<unsigned,256> q;
  std::atomic<bool> valid{true};
  std::thread producer([&] { for(unsigned i=0;i<100000;++i) while(!q.try_push(i)) std::this_thread::yield(); });
  std::thread consumer([&] { for(unsigned i=0;i<100000;++i) { unsigned x; while(!q.try_pop(x)) std::this_thread::yield(); if(x!=i) valid.store(false); } });
  producer.join(); consumer.join(); REQUIRE(valid.load());
}
TEST_CASE("Feature windows independent of chunk size and reset across gaps") {
  auto extract=[](uint32_t size) {
    FeatureStream stream; std::vector<AudioFeatures> windows;
    for(uint32_t at=0,seq=0;at<4096;++seq) {
      AudioChunk c; c.sequence=seq; c.frames=std::min(size,4096-at); c.channels=2;
      for(uint32_t i=0;i<c.frames;++i) { c.samples[2*i]=std::sin(float(at+i)*.04f); c.samples[2*i+1]=0; }
      AudioFeatures f; if(stream.consume(c,f)) windows.push_back(f);
      at+=c.frames;
    }
    REQUIRE(stream.partialFrames()==0); return windows;
  };
  const auto a=extract(256), b=extract(73);
  REQUIRE(a.size()==4); REQUIRE(b.size()==a.size());
  for(std::size_t i=0;i<a.size();++i) {
    REQUIRE(a[i].rms==b[i].rms); REQUIRE(a[i].centroid==b[i].centroid);
    REQUIRE(a[i].band_energy==b[i].band_energy);
  }
  FeatureStream stream; AudioChunk c; c.frames=256; c.samples.fill(1);
  AudioFeatures f; REQUIRE_FALSE(stream.consume(c,f));
  c.sequence=2; c.samples.fill(0); REQUIRE_FALSE(stream.consume(c,f));
  REQUIRE(stream.gaps()==1); REQUIRE(stream.partialFrames()==256);
  for(int i=3;i<=5;++i) { c.sequence=static_cast<uint64_t>(i); stream.consume(c,f); }
  REQUIRE(f.rms==0.0f); for(float e:f.band_energy) REQUIRE(e==0.0f);
}
TEST_CASE("Stalled classifier drops rather than blocking callback") {
  DspChain chain; chain.setClassifierEnabled(true); // Deliberately no consumer.
  std::vector<float> input(64,.1f), output(64);
  for(unsigned i=0;i<300;++i) chain.process(input.data(),output.data(),64,1);
  REQUIRE(chain.audioDrops()==45);
  REQUIRE(std::isfinite(output[0]));
}
TEST_CASE("Classifier holds silence and requires three consistent windows") {
  AudioQueue queue; std::atomic<uint8_t> preset{1}; Classifier classifier(&queue,&preset);
  uint64_t sequence=0;
  auto window=[&](bool silent) {
    for(int block=0;block<4;++block) {
      AudioChunk c; c.sequence=sequence++; c.frames=256;
      for(unsigned i=0;i<256;++i) c.samples[i]=silent ? 0.0f : .2f*std::sin(float((sequence-1)*256+i)*.01f);
      REQUIRE(queue.try_push(c));
    }
    classifier.poll();
  };
  window(true); window(true); window(true); REQUIRE(preset.load()==1);
  window(false); window(false); REQUIRE(preset.load()==1);
  window(false); REQUIRE(preset.load()==2);
}
TEST_CASE("Classifier lifecycle reads audio off thread") {
  DspChain chain;
  for(int run=0;run<3;++run) {
    chain.startClassifier();
    std::vector<float> input(4096,.1f), output(4096);
    chain.process(input.data(),output.data(),4096,1);
    chain.stopClassifier();
    REQUIRE(chain.classifier().windows()==4);
    REQUIRE(chain.preset().load()==2);
  }
}
TEST_CASE("Watchdog rejects missing data and recovers only after hysteresis") {
  std::atomic<uint8_t> level{128}; ComplexityController c(&level);
  c.update(900,1000); REQUIRE(level.load()==96);
  for(int i=0;i<9;++i) c.update(100,1000);
  REQUIRE(level.load()==96); c.update(100,1000); REQUIRE(level.load()==100);
  for(int i=0;i<100;++i) c.update(2000,1000);
  REQUIRE(level.load()==0);
  c.poll(); // Empty poll must clear recovery history.
  for(int i=0;i<9;++i) c.update(0,1000);
  REQUIRE(level.load()==0); c.update(0,1000); REQUIRE(level.load()<=4);
  c.setGains(std::numeric_limits<float>::infinity(),-3,1e30f);
  c.update(100,1000); REQUIRE(level.load()<=4);
}
TEST_CASE("Windowed telemetry handles budgets and reports dropped samples") {
  std::atomic<uint8_t> level{128}; ComplexityController c(&level);
  for(int i=0;i<5000;++i) c.record(2000,1000);
  REQUIRE(c.drops()==905); REQUIRE(c.missed()==5000);
  c.poll(); REQUIRE(c.windowSamples()==4095); REQUIRE(c.p99Us()==2000.0f); REQUIRE(c.p999Us()==2000.0f);
  REQUIRE(level.load()<128);
  c.poll(); REQUIRE(c.windowSamples()==0);
  c.record(100,200); c.record(1000,2000); c.poll(); // Both use 50% of budget.
  REQUIRE(level.load()==96);
}
TEST_CASE("Histograms retain maximum and overflow information") {
  StageHistogram h; h.record(std::chrono::milliseconds(10));
  REQUIRE(h.maxNs()==10000000); REQUIRE(h.overflow()==1); REQUIRE(h.p999Ns()==StageHistogram::kMaxNs);
}
TEST_CASE("Mixer transitions are bounded and settle after ten milliseconds") {
  MixerStage mixer; float in=1, wet=0, out=0;
  mixer.mix(&in,&wet,&out,1,1); float previous=out;
  mixer.setWet(1);
  for(int i=0;i<480;++i) {
    mixer.mix(&in,&wet,&out,1,1);
    REQUIRE(std::abs(out-previous)<.002f); previous=out;
  }
  REQUIRE(out==0.0f);
}
TEST_CASE("Every preset and complexity transition is bounded on sine and impulse") {
  for(int waveform=0;waveform<2;++waveform) {
    DspChain chain; float previous=0;
    for(uint8_t preset=0;preset<3;++preset) for(uint8_t level : {uint8_t(0),uint8_t(85),uint8_t(170),uint8_t(255),uint8_t(0)}) {
      chain.setManualPreset(preset); chain.setManualComplexity(level);
      for(int i=0;i<4096;++i) {
        const float input=waveform==0 ? .1f*std::sin(float(i)*.01f) : (i==100 ? .1f : 0.0f);
        float output; chain.process(&input,&output,1,1);
        REQUIRE(std::isfinite(output)); REQUIRE(std::abs(output)<=1.0f);
        REQUIRE(std::abs(output-previous)<.15f); previous=output;
      }
    }
  }
}
TEST_CASE("Biquad lowpass reference DC and Nyquist rejection") {
  Biquad filter; float b0,b1,b2,a1,a2;
  BiquadDesign::butterworthLowpass(1000,48000,b0,b1,b2,a1,a2); filter.setCoefficients(b0,b1,b2,a1,a2);
  float y=0; for(int i=0;i<4096;++i) y=filter.process(1);
  REQUIRE(y==Catch::Approx(1).margin(1e-4));
  filter.reset(); for(int i=0;i<4096;++i) y=filter.process(i%2 ? -1.0f : 1.0f);
  REQUIRE(std::abs(y)<1e-5f);
}
TEST_CASE("Limiter transfer is odd monotone and agrees with analytical tanh") {
  LimiterStage limiter;
  float previous=-1;
  for(int i=-100;i<=100;++i) {
    float input=float(i)/10, output; limiter.process(&input,&output,1,1);
    REQUIRE(output>=previous); REQUIRE(output==Catch::Approx(std::tanh(input)).margin(1e-6)); previous=output;
  }
}

TEST_CASE("Reverb impulse first arrival agrees with comb and allpass reference") {
  FreeverbStage reverb;
  std::vector<float> input(1200,0),output(1200);
  input[0]=1; reverb.process(input.data(),output.data(),1200,1);
  for(unsigned i=0;i<1116;++i) REQUIRE(output[i]==0.0f);
  REQUIRE(output[1116]==Catch::Approx(.25f*.6f*.6f*.5f).margin(1e-6));
}
TEST_CASE("Audio queue overflow recovers with a feature gap") {
  AudioQueue q; FeatureStream stream; AudioFeatures f;
  AudioChunk chunk; chunk.frames=64;
  for(unsigned i=0;i<256;++i) { chunk.sequence=i; q.try_push(chunk); }
  while(q.try_pop(chunk)) stream.consume(chunk,f);
  REQUIRE(stream.gaps()==0);
  chunk.sequence=256; chunk.samples.fill(0);
  REQUIRE(q.try_push(chunk)); REQUIRE(q.try_pop(chunk));
  stream.consume(chunk,f); REQUIRE(stream.gaps()==1); REQUIRE(stream.partialFrames()==64);
}

TEST_CASE("Reenabled combs cannot replay frozen tails") {
  FreeverbStage reverb; reverb.setComplexity(255);
  std::vector<float> input(4096,1.0f), output(4096);
  reverb.process(input.data(),output.data(),4096,1);
  reverb.setComplexity(0); input.assign(4096,0);
  for(int i=0;i<50;++i) reverb.process(input.data(),output.data(),4096,1);
  reverb.setComplexity(255); reverb.process(input.data(),output.data(),4096,1);
  for(float x:output) REQUIRE(std::abs(x)<1e-4f);
}
