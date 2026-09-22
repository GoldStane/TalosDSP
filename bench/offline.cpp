#include "audio/DspChain.h"
#include "SyntheticClips.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
static void word(std::ofstream& out,uint32_t x,int bytes) {
  for(int i=0;i<bytes;++i) out.put(static_cast<char>((x>>(8*i))&255));
}
static void wav(const std::string& path,const std::vector<float>& data,unsigned rate) {
  std::ofstream out(path,std::ios::binary); if(!out) throw std::runtime_error("Cannot write "+path);
  out.write("RIFF",4); word(out,36+static_cast<uint32_t>(data.size())*2,4); out.write("WAVEfmt ",8);
  word(out,16,4); word(out,1,2); word(out,1,2); word(out,rate,4); word(out,rate*2,4); word(out,2,2); word(out,16,2);
  out.write("data",4); word(out,static_cast<uint32_t>(data.size())*2,4);
  for(float v:data) word(out,static_cast<uint16_t>(static_cast<int16_t>(v*32768)),2);
}
static void stageCost() {
  std::cout<<"comb_count,classifier_enabled,block,iteration,cost_us,audio_drops\n";
  for(unsigned combs=1;combs<=4;++combs) for(bool enabled:{false,true}) for(unsigned block:{64u,256u}) {
    DspChain chain; chain.setManualComplexity(static_cast<uint8_t>((combs-1)*85));
    chain.setManualPreset(1);
    if(enabled) { chain.startClassifier(); chain.setManualPreset(1); }
    std::vector<float> input(block),output(block);
    for(unsigned i=0;i<block;++i) input[i]=.1f*std::sin(static_cast<float>(i)*.04f);
    for(unsigned i=0;i<500;++i) chain.process(input.data(),output.data(),block,1);
    std::vector<double> costs(2000);
    // No stdout/file operations within the measured loop.
    for(auto& cost:costs) {
      const auto start=Clock::now(); chain.process(input.data(),output.data(),block,1);
      cost=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
    }
    chain.stopClassifier();
    for(std::size_t i=0;i<costs.size();++i)
      std::cout<<combs<<','<<enabled<<','<<block<<','<<i<<','<<costs[i]<<','<<chain.audioDrops()<<'\n';
  }
}
static void watchdog() {
  std::cout<<"poll,injected_us,budget_us,complexity,p99_us,missed\n";
  std::atomic<uint8_t> level{255}; ComplexityController controller(&level);
  DspChain chain; std::array<float,64> input{},output{}; input.fill(.1f);
  for(int poll=0;poll<120;++poll) {
    const unsigned injected=poll>=10 && poll<30 ? 1200u : 0u;
    chain.setManualComplexity(level.load());
    for(int i=0;i<8;++i) {
      const auto start=Clock::now();
      chain.process(input.data(),output.data(),64,1);
      if(injected) while(Clock::now()-start<std::chrono::microseconds(injected)) {}
      const float cost=std::chrono::duration<float,std::micro>(Clock::now()-start).count();
      controller.record(cost,1000);
    }
    controller.poll();
    std::cout<<poll<<','<<injected<<",1000,"<<static_cast<unsigned>(level.load())<<','<<controller.p99Us()<<','<<controller.missed()<<'\n';
  }
}
static void classifierEval(const std::string& directory) {
  std::cout<<"rate,label,variant,predicted,windows,correct_windows,pass_us,wav\n";
  for(unsigned rate:{44100u,48000u,96000u}) for(unsigned label=0;label<3;++label) for(unsigned variant=0;variant<5;++variant) {
    auto samples=syntheticClip(label,rate,variant);
    // Evaluate exactly the PCM16 values written to the labeled WAV artifacts.
    for(auto& sample:samples) sample=static_cast<float>(static_cast<int16_t>(sample*32767))/32768.0f;
    const auto name=std::to_string(rate)+"-"+std::to_string(label)+"-"+std::to_string(variant)+".wav";
    if(!directory.empty()) wav(directory+"/"+name,samples,rate);
    FeatureStream stream; std::array<unsigned,3> votes{}; unsigned windows=0,correct=0; uint64_t seq=0;
    const auto start=Clock::now();
    for(std::size_t offset=0;offset<samples.size();) {
      AudioChunk chunk; chunk.sequence=seq++; chunk.sampleRate=static_cast<float>(rate);
      chunk.frames=static_cast<uint32_t>(std::min<std::size_t>(256,samples.size()-offset));
      std::copy_n(samples.data()+offset,chunk.frames,chunk.samples.data()); offset+=chunk.frames;
      AudioFeatures features;
      if(stream.consume(chunk,features) && features.rms>=1e-4f) {
        const auto predicted=Classifier::classify(features); ++votes[predicted]; ++windows; correct+=predicted==label;
      }
    }
    const auto us=std::chrono::duration<double,std::micro>(Clock::now()-start).count();
    const auto predicted=std::distance(votes.begin(),std::max_element(votes.begin(),votes.end()));
    std::cout<<rate<<','<<label<<','<<variant<<','<<predicted<<','<<windows<<','<<correct<<','<<us<<','<<name<<'\n';
  }
}
static void featureAccuracy() {
  std::cout<<"rate,tone_hz,centroid_hz,centroid_error_hz,rolloff85_hz\n";
  for(unsigned rate:{44100u,48000u,96000u}) for(unsigned hz:{100u,750u,1500u,2500u,4000u,7000u,10000u,15000u}) {
    FeatureExtractor extractor; extractor.init(static_cast<float>(rate),rate);
    std::vector<float> signal(rate);
    for(unsigned i=0;i<rate;++i) signal[i]=.2f*std::sin(6.28318530718f*static_cast<float>(hz)*static_cast<float>(i)/static_cast<float>(rate));
    extractor.process(signal.data(),rate,1); AudioFeatures features; extractor.finalize(features);
    std::cout<<rate<<','<<hz<<','<<features.centroid<<','<<features.centroid-static_cast<float>(hz)<<','<<features.rolloff_85<<'\n';
  }
}
int main(int argc,char** argv) {
  const std::string mode=argc>1 ? argv[1] : "";
  if(mode=="stage") stageCost();
  else if(mode=="watchdog") watchdog();
  else if(mode=="features") featureAccuracy();
  else if(mode=="classifier") classifierEval(argc>2 ? argv[2] : "");
  else { std::cerr<<"Usage: talos_bench stage|watchdog|classifier|features [existing-clip-directory]\n"; return 2; }
}
