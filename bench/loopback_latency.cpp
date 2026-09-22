#include "LoopbackProbe.h"
#include "audio/DspChain.h"
#include <portaudio.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

struct State { LoopbackProbe probe; DspChain chain; bool useChain;
  State(unsigned count,unsigned rate,bool full) : probe(count,rate),useChain(full) { chain.setSampleRate(static_cast<float>(rate)); chain.setManualPreset(0); }
};

static int callback(const void* input,void* output,unsigned long frames,const PaStreamCallbackTimeInfo*,PaStreamCallbackFlags flags,void* state) {
  auto& context=*static_cast<State*>(state);
  auto& probe=context.probe;
  if(flags) ++probe.xruns;
  probe.process(static_cast<const float*>(input),static_cast<float*>(output),static_cast<unsigned>(frames));
  if(context.useChain) context.chain.process(static_cast<const float*>(output),static_cast<float*>(output),static_cast<uint32_t>(frames),1);
  return probe.done() ? paComplete : paContinue;
}
static int selfTest() {
  LoopbackProbe probe(4,48000); std::vector<float> delay(137,0); unsigned head=0;
  while(!probe.done()) { float output=0,input=delay[head]; probe.process(&input,&output,1); delay[head]=output; head=(head+1)%137; }
  for(auto n:probe.delays) if(n!=137) return 1;
  LoopbackProbe missing(2,48000); while(!missing.done()) { float output; missing.process(nullptr,&output,1); }
  for(auto n:missing.delays) if(n!=-1) return 1;
  std::cout<<"loopback detector: known 137-frame delay and missing-return tests passed\n"; return 0;
}
int main(int argc,char** argv) {
  int device=-1,rate=48000,block=64,count=100; bool full=false;
  for(int i=1;i<argc;++i) {
    const std::string option=argv[i];
    if(option=="--self-test") return selfTest();
    if(option=="--help") { std::cout<<"loopback_latency --device N [--sr 48000 --blocksize 64 --count 100 --chain none|full]\nConnect output to input on the same duplex device; CSV is written to stdout.\n"; return 0; }
    if(i+1>=argc) return 2;
    if(option=="--chain") { const std::string mode=argv[++i]; if(mode!="none" && mode!="full") return 2; full=mode=="full"; continue; }
    std::size_t used=0; int value=0;
    try { const std::string text=argv[++i]; value=std::stoi(text,&used); if(used!=text.size()) return 2; } catch(...) { return 2; }
    if(option=="--device") device=value;
    else if(option=="--sr") rate=value;
    else if(option=="--blocksize") block=value;
    else if(option=="--count") count=value;
    else return 2;
  }
  if(device<0 || rate<8000 || rate>192000 || block<1 || block>4096 || count<1 || count>100000) return 2;
  PaError error=Pa_Initialize(); if(error!=paNoError) { std::cerr<<Pa_GetErrorText(error)<<'\n'; return 1; }
  const PaDeviceInfo* info=Pa_GetDeviceInfo(device);
  if(!info || info->maxInputChannels<1 || info->maxOutputChannels<1) {
    std::cerr<<"Choose one full-duplex device for synchronized input/output.\n"; Pa_Terminate(); return 2;
  }
  std::cerr<<"device="<<info->name<<" backend="<<Pa_GetHostApiInfo(info->hostApi)->name
           <<" rate="<<rate<<" block="<<block<<" probes="<<count<<" chain="<<(full ? "full" : "none")<<'\n';
  auto context=std::make_unique<State>(static_cast<unsigned>(count),static_cast<unsigned>(rate),full);
  auto& probe=context->probe;
  PaStreamParameters input{device,1,paFloat32,info->defaultLowInputLatency,nullptr};
  PaStreamParameters output{device,1,paFloat32,info->defaultLowOutputLatency,nullptr};
  PaStream* stream=nullptr;
  error=Pa_OpenStream(&stream,&input,&output,rate,static_cast<unsigned long>(block),paNoFlag,callback,context.get());
  if(error==paNoError) error=Pa_StartStream(stream);
  if(error==paNoError) {
    // Watchdog prevents a broken backend leaving the harness waiting forever.
    const auto maxPolls=static_cast<unsigned>(count)*25+500;
    unsigned polls=0; int active=1;
    while((active=Pa_IsStreamActive(stream))==1 && polls++<maxPolls) Pa_Sleep(10);
    if(active<0) error=active;
    else if(active==1) error=paTimedOut;
  }
  if(stream) { Pa_StopStream(stream); Pa_CloseStream(stream); }
  Pa_Terminate();
  if(error!=paNoError) { std::cerr<<Pa_GetErrorText(error)<<'\n'; return 1; }
  std::cout<<"probe,roundtrip_frames,roundtrip_ms\n";
  unsigned missing=0;
  for(std::size_t i=0;i<probe.delays.size();++i) {
    const auto n=probe.delays[i]; missing+=n<0;
    std::cout<<i<<','<<n<<','<<(n<0 ? -1.0 : static_cast<double>(n)*1000/rate)<<'\n';
  }
  std::cerr<<"missing="<<missing<<" xrun_callbacks="<<probe.xruns<<'\n';
  return missing || probe.xruns ? 1 : 0;
}
