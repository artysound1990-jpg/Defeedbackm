// ตัวทดสอบ: จำลองสิ่งที่โฮสต์ทำตอนสแกนและตอนเล่นจริง
// คอมไพล์พร้อม AddressSanitizer เพื่อจับการเขียนทะลุ buffer
#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdio>
#include <cmath>
#include <limits>
#include <random>

#include "../Source/DeFeedback.cpp"

static int failures = 0;
static void check(bool ok, const char* what){
  printf("  %-58s %s\n", what, ok ? "ok" : "*** FAIL ***");
  if(!ok) failures++;
}

static bool finiteBuf(const juce::AudioBuffer<float>& b){
  for(int c=0;c<b.getNumChannels();++c)
    for(int i=0;i<b.getNumSamples();++i)
      if(!std::isfinite(b.getSample(c,i))) return false;
  return true;
}

int main(){
  juce::ScopedJuceInitialiser_GUI init;
  juce::MidiBuffer midi;
  std::mt19937 rng(1234);
  std::uniform_real_distribution<float> noise(-0.3f,0.3f);

  printf("[1] processBlock ก่อน prepareToPlay (บางโฮสต์ทำตอนสแกน)\n");
  {
    DeFeedbackProcessor p;
    juce::AudioBuffer<float> b(2,512); b.clear();
    p.processBlock(b, midi);            // ถ้าไม่มีการ์ด ตรงนี้จะเขียนทะลุ vector ว่าง
    check(finiteBuf(b), "ไม่ crash และ output ยัง finite");
  }

  printf("[2] prepareToPlay ด้วย sampleRate ผิดปกติ\n");
  for(double sr : {0.0, -1.0, 1.0, 8000.0, 44100.0, 48000.0, 96000.0, 192000.0}){
    DeFeedbackProcessor p;
    p.prepareToPlay(sr, 512);
    juce::AudioBuffer<float> b(2,512);
    for(int c=0;c<2;c++) for(int i=0;i<512;i++) b.setSample(c,i,noise(rng));
    for(int n=0;n<20;n++) p.processBlock(b, midi);
    char msg[96]; snprintf(msg,sizeof msg,"sampleRate=%.0f ผ่าน 20 block", sr);
    check(finiteBuf(b), msg);
  }

  printf("[3] block size แปลก ๆ และ mono\n");
  {
    DeFeedbackProcessor p;
    p.prepareToPlay(48000, 1024);
    for(int bs : {1, 7, 32, 512, 1024, 4096, 8192}){
      juce::AudioBuffer<float> b(1,bs);
      for(int i=0;i<bs;i++) b.setSample(0,i,noise(rng));
      p.processBlock(b, midi);
      char msg[96]; snprintf(msg,sizeof msg,"mono block=%d", bs);
      check(finiteBuf(b), msg);
    }
  }

  printf("[4] buffer 0 channel / 0 sample\n");
  {
    DeFeedbackProcessor p; p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> z(0,0);
    p.processBlock(z, midi);
    juce::AudioBuffer<float> z2(2,0);
    p.processBlock(z2, midi);
    check(true, "ไม่ crash");
  }

  printf("[5] สัญญาณหอนจริง 10 วินาที (ต้องวาง notch ได้ ไม่พัง)\n");
  {
    DeFeedbackProcessor p; p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> b(2,512);
    double ph=0; const double w=juce::MathConstants<double>::twoPi*2500.0/48000.0;
    bool quiet=false;
    for(int n=0;n<938;n++){
      for(int i=0;i<512;i++){
        float s=(float)(0.5*std::sin(ph))+noise(rng)*0.05f; ph+=w;
        b.setSample(0,i,s); b.setSample(1,i,s);
      }
      p.processBlock(b,midi);
      if(n>600){ // หลัง 6 วิ ควรโดน notch จนเบาลงมาก
        float pk=0; for(int i=0;i<512;i++) pk=std::max(pk,std::abs(b.getSample(0,i)));
        if(n==937) quiet = pk < 0.1f;
      }
    }
    check(finiteBuf(b), "ไม่ crash ตลอด 10 วินาที");
    check(quiet, "เสียงหอน 2500 Hz ถูกกดลงจริง");
  }

  printf("[6] input โหด: NaN, Inf, ค่ามหาศาล, DC\n");
  {
    DeFeedbackProcessor p; p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> b(2,512);
    auto fill=[&](float v){ for(int c=0;c<2;c++) for(int i=0;i<512;i++) b.setSample(c,i,v); };
    fill(std::numeric_limits<float>::quiet_NaN()); p.processBlock(b,midi);
    fill(std::numeric_limits<float>::infinity());  p.processBlock(b,midi);
    fill(1e30f);                                   p.processBlock(b,midi);
    fill(1.0f);                                    p.processBlock(b,midi);
    fill(0.0f);
    for(int n=0;n<200;n++) p.processBlock(b,midi);   // ต้องฟื้นกลับมาเงียบสนิท
    check(finiteBuf(b), "ฟื้นกลับมาเป็นค่า finite หลังเจอ NaN/Inf");
  }

  printf("[7] เรียกซ้ำ prepareToPlay / releaseResources สลับกัน\n");
  {
    DeFeedbackProcessor p;
    juce::AudioBuffer<float> b(2,256);
    for(int r=0;r<5;r++){
      p.prepareToPlay(44100+r*1000,256);
      for(int c=0;c<2;c++) for(int i=0;i<256;i++) b.setSample(c,i,noise(rng));
      for(int n=0;n<30;n++) p.processBlock(b,midi);
      p.releaseResources();
    }
    check(finiteBuf(b), "ไม่ crash");
  }

  printf("[8] state save/load และ editor\n");
  {
    DeFeedbackProcessor p; p.prepareToPlay(48000,512);
    juce::MemoryBlock mb; p.getStateInformation(mb);
    p.setStateInformation(mb.getData(),(int)mb.getSize());
    auto* ed = p.createEditor(); delete ed;
    check(mb.getSize()>0, "state เซฟ/โหลด และสร้าง editor ได้");
  }

  printf("\n%s\n", failures ? "*** มีข้อที่ FAIL ***" : "ผ่านทั้งหมด");
  return failures;
}
