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

  // ---------- โหมด Auto: ต้องทำงานได้โดยไม่ต้องตั้งค่าอะไรเลย ----------
  auto setParam=[&](DeFeedbackProcessor& p, const juce::String& id, float v){
    for (auto* par : p.getParameters())
      if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(par))
        if (r->paramID == id) { r->setValueNotifyingHost (r->convertTo0to1 (v)); return; }
  };

  // ป้อนเสียงหอนที่ระดับต่าง ๆ แล้ววัดว่ากดลงได้กี่ dB โดยไม่แตะปุ่มใด ๆ
  auto feedbackTest=[&](float level, int modeIdx, double seconds){
    DeFeedbackProcessor p;
    setParam(p, "mode", (float) modeIdx);
    p.prepareToPlay(48000, 512);
    juce::AudioBuffer<float> b(1,512);
    double ph=0; const double w=juce::MathConstants<double>::twoPi*3150.0/48000.0;
    const int blocks=(int)(48000.0*seconds/512);
    float firstPk=0, lastPk=0;
    for(int n=0;n<blocks;n++){
      for(int i=0;i<512;i++){
        b.setSample(0,i,(float)(level*std::sin(ph))+noise(rng)*level*0.1f); ph+=w;
      }
      p.processBlock(b,midi);
      float pk=0; for(int i=0;i<512;i++) pk=std::max(pk,std::abs(b.getSample(0,i)));
      if(n==2) firstPk=pk;
      if(n==blocks-1) lastPk=pk;
    }
    return 20.0f*std::log10((lastPk+1e-9f)/(firstPk+1e-9f));   // ลดลงกี่ dB
  };

  printf("[9] Auto - Live: จับเสียงหอนโดยไม่ตั้งค่าอะไรเลย\n");
  for(float lv : {0.5f, 0.05f, 0.005f}){
    float red = feedbackTest(lv, 0, 6.0);
    char msg[110]; snprintf(msg,sizeof msg,"ระดับ input %.3f -> กดลง %.1f dB", lv, -red);
    check(red < -20.f, msg);
  }

  printf("[10] Auto - Ring Out ต้องจับได้เร็วกว่า Live\n");
  {
    // วัด "เวลาที่ใช้จับ" ไม่ใช่ผลลัพธ์สุดท้าย (ปล่อยนาน ๆ เดี๋ยวก็เท่ากันทั้งคู่)
    auto timeToCatch=[&](int modeIdx){
      DeFeedbackProcessor p;
      setParam(p, "mode", (float) modeIdx);
      p.prepareToPlay(48000,512);
      juce::AudioBuffer<float> b(1,512);
      double ph=0; const double w=juce::MathConstants<double>::twoPi*3150.0/48000.0;
      float ref=0;
      for(int n=0;n<300;n++){
        for(int i=0;i<512;i++){ b.setSample(0,i,(float)(0.3*std::sin(ph))); ph+=w; }
        p.processBlock(b,midi);
        float pk=0; for(int i=0;i<512;i++) pk=std::max(pk,std::abs(b.getSample(0,i)));
        if(n==1) ref=pk;
        if(n>1 && ref>0 && pk < ref*0.5f) return n*512*1000.0/48000.0;   // ms
      }
      return 1e9;
    };
    double live = timeToCatch(0), ring = timeToCatch(1);
    char msg[120]; snprintf(msg,sizeof msg,"Live จับที่ %.0f ms, Ring Out จับที่ %.0f ms", live, ring);
    check(ring < live, msg);
  }

  printf("[11] Auto - Live: ไม่ตัดเสียงพูด/noise ที่ไม่ใช่เสียงหอน\n");
  {
    DeFeedbackProcessor p;
    setParam(p, "mode", 0.f);
    p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> b(1,512);
    float sumIn=0,sumOut=0;
    for(int n=0;n<560;n++){                       // ~6 วินาที
      float si=0,so=0;
      for(int i=0;i<512;i++){ float v=noise(rng); b.setSample(0,i,v); si+=v*v; }
      p.processBlock(b,midi);
      for(int i=0;i<512;i++){ float v=b.getSample(0,i); so+=v*v; }
      if(n>400){ sumIn+=si; sumOut+=so; }
    }
    float lossDb = 10.f*std::log10((sumOut+1e-12f)/(sumIn+1e-12f));
    char msg[110]; snprintf(msg,sizeof msg,"noise กว้าง ๆ ถูกลดไปแค่ %.2f dB", -lossDb);
    check(lossDb > -3.f, msg);
  }

  // ---------- หลายช่อง: ต้องแยกกันจริง ----------
  printf("[12] หลายช่อง: หอนคนละความถี่ ต้องกดลงทุกช่องโดยไม่กวนกัน\n");
  {
    const int NCH=8;
    DeFeedbackProcessor p;
    setParam(p, "mode", 0.f);
    setParam(p, "chmode", 2.f);              // Independent
    p.setPlayConfigDetails(NCH,NCH,48000,512);
    p.prepareToPlay(48000,512);

    char m0[110]; snprintf(m0,sizeof m0,"สร้าง engine แยก %d ตัว", p.getEngineCount());
    check(p.getEngineCount()==NCH, m0);

    // แต่ละช่องหอนคนละความถี่ ไล่จาก 800 Hz ขึ้นไป
    double freq[NCH], ph[NCH]={0};
    for(int c=0;c<NCH;c++) freq[c]=800.0*std::pow(2.0,c*0.45);

    juce::AudioBuffer<float> b(NCH,512);
    float first[NCH]={0}, last[NCH]={0};
    const int blocks=(int)(48000.0*6/512);
    for(int n=0;n<blocks;n++){
      for(int c=0;c<NCH;c++)
        for(int i=0;i<512;i++){
          b.setSample(c,i,(float)(0.4*std::sin(ph[c]))+noise(rng)*0.02f);
          ph[c]+=juce::MathConstants<double>::twoPi*freq[c]/48000.0;
        }
      p.processBlock(b,midi);
      for(int c=0;c<NCH;c++){
        float pk=0; for(int i=0;i<512;i++) pk=std::max(pk,std::abs(b.getSample(c,i)));
        if(n==2) first[c]=pk;
        if(n==blocks-1) last[c]=pk;
      }
    }
    bool allDown=true; float worst=-1e9f;   // ช่องที่กดลงได้น้อยที่สุด
    for(int c=0;c<NCH;c++){
      float red=20.f*std::log10((last[c]+1e-9f)/(first[c]+1e-9f));
      if(red>-20.f) allDown=false;
      worst=std::max(worst,red);
    }
    char m1[150]; snprintf(m1,sizeof m1,"ทั้ง %d ช่องถูกกดลง >20 dB (แย่สุด %.1f dB)", NCH, -worst);
    check(allDown, m1);
  }

  printf("[13] หลายช่อง: ช่องที่เงียบต้องไม่โดนฟิลเตอร์ของช่องที่หอน\n");
  {
    const int NCH=4;
    DeFeedbackProcessor p;
    setParam(p, "mode", 0.f);
    setParam(p, "chmode", 2.f);
    p.setPlayConfigDetails(NCH,NCH,48000,512);
    p.prepareToPlay(48000,512);

    juce::AudioBuffer<float> b(NCH,512);
    double ph=0;
    for(int n=0;n<560;n++){
      for(int i=0;i<512;i++){
        float howl=(float)(0.4*std::sin(ph)); ph+=juce::MathConstants<double>::twoPi*3150.0/48000.0;
        b.setSample(0,i,howl);               // ช่อง 0 หอน
        for(int c=1;c<NCH;c++) b.setSample(c,i,noise(rng));   // ที่เหลือเป็น noise เฉย ๆ
      }
      p.processBlock(b,midi);
    }
    int n0=p.notchesOnEngine(0), n1=p.notchesOnEngine(1), n2=p.notchesOnEngine(2);
    char m2[150]; snprintf(m2,sizeof m2,"ช่องที่หอนมี %d notch, ช่องเงียบมี %d และ %d", n0,n1,n2);
    check(n0>=1 && n1==0 && n2==0, m2);
  }

  printf("[14] stereo โหมด Auto ต้องลิงก์กัน (ฟิลเตอร์ชุดเดียว ภาพเสียงไม่เบี้ยว)\n");
  {
    DeFeedbackProcessor p;
    setParam(p, "chmode", 0.f);              // Auto
    p.setPlayConfigDetails(2,2,48000,512);
    p.prepareToPlay(48000,512);
    char m3[110]; snprintf(m3,sizeof m3,"2 ช่อง -> linked=%s, engine %d ตัว",
                           p.isLinked()?"ใช่":"ไม่", p.getEngineCount());
    check(p.isLinked() && p.getEngineCount()==1, m3);

    DeFeedbackProcessor q;
    setParam(q, "chmode", 0.f);
    q.setPlayConfigDetails(4,4,48000,512);
    q.prepareToPlay(48000,512);
    char m4[110]; snprintf(m4,sizeof m4,"4 ช่อง -> linked=%s, engine %d ตัว",
                           q.isLinked()?"ใช่":"ไม่", q.getEngineCount());
    check(!q.isLinked() && q.getEngineCount()==4, m4);
  }

  printf("\n%s\n", failures ? "*** มีข้อที่ FAIL ***" : "ผ่านทั้งหมด");
  return failures;
}
