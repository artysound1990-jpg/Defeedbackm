// วัดว่าหนึ่ง instance กิน CPU เท่าไหร่ เพื่อประเมินว่ารันพร้อมกันได้กี่ช่อง
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <chrono>
#include <random>
#include <vector>
#include <memory>
#include "../Source/DeFeedback.cpp"

int main(){
  juce::ScopedJuceInitialiser_GUI init;
  juce::MidiBuffer midi;
  std::mt19937 rng(7);
  std::uniform_real_distribution<float> nd(-0.25f,0.25f);

  const double sr=48000; const int bs=64;      // buffer เล็กแบบงานไลฟ์
  const int blocks=(int)(sr*20/bs);            // 20 วินาที

  // ตัว processor ตัวเดียว หลายช่องในนั้น (แบบที่ใช้งานจริง)
  for(int nch : {1, 2, 4, 8}){
    DeFeedbackProcessor p;
    for (auto* par : p.getParameters())
      if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(par))
        if (r->paramID == "chmode") r->setValueNotifyingHost (r->convertTo0to1 (2.f));  // Independent
    p.setPlayConfigDetails(nch,nch,sr,bs);
    p.prepareToPlay(sr,bs);
    juce::AudioBuffer<float> b(nch,bs);
    std::vector<double> ph((size_t)nch,0.0);
    auto t0=std::chrono::steady_clock::now();
    for(int n=0;n<blocks;n++){
      for(int c=0;c<nch;c++)
        for(int k=0;k<bs;k++){
          b.setSample(c,k,(float)(0.3*std::sin(ph[(size_t)c]))+nd(rng));
          ph[(size_t)c]+=juce::MathConstants<double>::twoPi*(1200.0+c*370.0)/sr;
        }
      p.processBlock(b,midi);
    }
    auto t1=std::chrono::steady_clock::now();
    double secs=std::chrono::duration<double>(t1-t0).count();
    printf("%2d ช่อง (buffer %d) : %.2f s ต่อเสียง 20 s  ->  %.1f%% ของ 1 core  (%.2f%% ต่อช่อง)\n",
           nch, bs, secs, 100.0*secs/20.0, 100.0*secs/20.0/nch);
  }
  return 0;
}
