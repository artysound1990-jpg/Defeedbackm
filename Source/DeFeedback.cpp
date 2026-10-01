// DeFeedback - กันเสียงหอน + จัดการเรโซแนนซ์ไมค์ (VST3 + โปรแกรมเดี่ยว)
//
//   ต่อช่อง:  Low Cut -> Resonance bells (dynamic, LTAS) -> Feedback notches
//
// Feedback  : peak แคบที่ค้างนาน -> วาง notch ลึก
// Resonance : ย่านกว้างที่โด่งตลอดในสเปกตรัมเฉลี่ย (สีของไมค์/proximity/ห้อง)
//             -> วาง bell กว้างกว่า ลดเฉพาะตอนที่ย่านนั้นเด้งขึ้นมาจริง
//
// รองรับหลายช่องพร้อมกัน แต่ละช่องมีตัวตรวจจับและฟิลเตอร์ของตัวเองแยกกันสนิท
// (สำหรับงาน insert หลายไมค์ เช่น X32 ผ่านการ์ด X-USB) รองรับสูงสุด 8 ช่อง
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <vector>

static constexpr int kMaxChannels = 8;    // 8 ช่องพอสำหรับงานไมค์ร้อง/พูด

//==================================================================
struct Biquad
{
    static constexpr int kMaxCh = 2;      // หนึ่ง engine คุมได้ไม่เกิน 2 ช่อง (กรณี linked stereo)
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[kMaxCh] {}, z2[kMaxCh] {};

    void reset() { for (int c = 0; c < kMaxCh; ++c) z1[c] = z2[c] = 0.0; }

    // NaN/Inf ที่หลุดเข้ามาจะค้างใน state ของ IIR ตลอดไป ทำให้เสียงเงียบถาวร
    bool stateIsSane() const
    {
        for (int c = 0; c < kMaxCh; ++c)
            if (! std::isfinite (z1[c]) || ! std::isfinite (z2[c])) return false;
        return true;
    }

    inline float process (float xin, int ch) noexcept   // DF2T
    {
        const double x = xin, y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return (float) y;
    }

    void setNotch (double f, double q, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0), a0 = 1.0 + alpha;
        b0 = 1.0 / a0; b1 = -2.0 * c / a0; b2 = 1.0 / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }

    void setPeak (double f, double q, double gainDb, double sr)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double alpha = std::sin (w0) / (2.0 * q), c = std::cos (w0);
        const double a0 = 1.0 + alpha / A;
        b0 = (1.0 + alpha * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - alpha * A) / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha / A) / a0;
    }

    void setHighpass (double f, double sr)
    {
        const double w0 = juce::MathConstants<double>::twoPi * f / sr;
        const double alpha = std::sin (w0) / (2.0 * 0.7071), c = std::cos (w0), a0 = 1.0 + alpha;
        b0 = (1.0 + c) * 0.5 / a0; b1 = -(1.0 + c) / a0; b2 = (1.0 + c) * 0.5 / a0;
        a1 = -2.0 * c / a0; a2 = (1.0 - alpha) / a0;
    }
};

//==================================================================
// ค่าที่ใช้จริงใน block นี้ - โหมด Auto คำนวณให้เอง โหมด Manual เอาจากปุ่ม
struct Eff
{
    float thresh = 15.f, hold = 110.f, q = 35.f, lowCut = 80.f;
    float resThr = 4.f, resDepth = 60.f, resQ = 6.f;
    float minLvlOffset = 14.f;        // บวกจากพื้นเสียงที่วัดได้ (โหมด Auto)
    float minLvlAbs = -45.f;          // ค่าตายตัว (โหมด Manual)
    bool  autoLevel = true;
    int   maxN = 10, maxB = 4;
    bool  resOn = true, makeFixed = false, floatRelease = true;
};

//==================================================================
// หนึ่ง engine = หนึ่งช่องอิสระ (หรือ stereo คู่หนึ่งที่ใช้ฟิลเตอร์ร่วมกัน)
class ChannelEngine
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kHop = 512;
    static constexpr int kMaxNotch = 16, kMaxBell = 8, kMaxPeaks = 3;

    void prepare (double sampleRate, const juce::dsp::FFT* sharedFft, const std::vector<float>* sharedWindow)
    {
        // โฮสต์บางตัวส่ง sampleRate เป็น 0 หรือค่าเพี้ยนตอนสแกน ต้องกันไว้
        // ไม่งั้น kmin/kmax ที่คำนวณจากมันจะกลายเป็นขยะแล้วอ่านทะลุ array
        sr     = (sampleRate > 1000.0 && sampleRate < 1000000.0) ? sampleRate : 48000.0;
        fft    = sharedFft;
        window = sharedWindow;

        fifo.assign (kN, 0.f);
        fftData.assign (2 * kN, 0.f);
        const size_t nb = (size_t) kN / 2 + 1;
        mag.assign (nb, -120.f);
        ltas.assign (nb, -120.f);
        excessL.assign (nb, -120.f);
        excessN.assign (nb, -120.f);
        narrowS.assign (nb, -120.f);
        wideS.assign (nb, -120.f);
        prefix.assign (nb + 1, 0.f);

        kmin = juce::jlimit (2, kN / 2 - 2, (int) (80.0 * kN / sr));
        kmax = juce::jlimit (kmin, kN / 2 - 2, (int) (std::min (12000.0, sr * 0.45) * kN / sr));
        rmin = juce::jlimit (2, kN / 2 - 2, (int) (120.0 * kN / sr));
        rmax = juce::jlimit (rmin, kN / 2 - 2, (int) (std::min (9000.0, sr * 0.45) * kN / sr));

        resetAll();
        ready = true;
    }

    void release() { ready = false; }

    void resetAll()
    {
        for (auto& n : notches) n = Notch {};
        for (auto& b : bells)   b = Bell {};
        std::fill (ltas.begin(), ltas.end(), -120.f);
        std::fill (fifo.begin(), fifo.end(), 0.f);
        hpf.reset();
        fifoPos = hopCount = nCand = scanCount = ltasFrames = releaseHops = 0;
        ltasReady = false;
        noiseFloor = -60.f;
        lastQ = lastLowCut = -1.f;
    }

    bool isReady() const
    {
        return ready && fifo.size() == (size_t) kN && mag.size() == (size_t) kN / 2 + 1
            && fft != nullptr && window != nullptr && window->size() == (size_t) kN;
    }

    int   activeNotchCount() const { return countActive(); }
    float getNoiseFloor()    const { return noiseFloor; }

    // ประมวลผลในที่: chans[0..nch-1] แต่ละตัวยาว ns ตัวอย่าง
    // nch = 1 คือช่องเดี่ยว, nch = 2 คือ stereo ที่ใช้ชุดฟิลเตอร์เดียวกัน
    void process (float* const* chans, int nch, int ns, const Eff& e)
    {
        if (! isReady() || nch <= 0 || ns <= 0) return;
        nch = std::min (nch, Biquad::kMaxCh);

        if (e.q != lastQ)
        {
            lastQ = e.q;
            for (auto& n : notches) if (n.on) n.bq.setNotch (n.f, e.q, sr);
        }

        hpfOn = e.lowCut > 21.f;
        if (hpfOn && e.lowCut != lastLowCut) { lastLowCut = e.lowCut; hpf.setHighpass (e.lowCut, sr); }

        while (countActive() > e.maxN) { if (auto* o = oldest (false)) o->on = false; else break; }

        // โหมด Live: ทยอยปล่อยฟิลเตอร์เก่าคืนทีละตัว ทุก ๆ ราว 25 วินาที
        // ถ้าความถี่นั้นยังหอนอยู่จริง จะถูกจับกลับมาภายในเสี้ยววินาที
        // ถ้าไม่หอนแล้ว ก็ได้ช่องว่างคืนไว้ใช้กับความถี่อื่น เล่นยาว ๆ จะได้ไม่ตัน
        if (e.floatRelease && ++releaseHops >= (int) (sr * 25.0 / (double) kHop))
        {
            releaseHops = 0;
            if (countActive() >= 3) if (auto* o = oldest (false)) o->on = false;
        }

        for (int i = 0; i < ns; ++i)
        {
            float mono = 0.f;
            for (int ch = 0; ch < nch; ++ch)
            {
                float x = chans[ch][i];
                if (! std::isfinite (x)) x = 0.0f;   // กันค่าเสียจากไดรเวอร์/ปลั๊กอินตัวหน้า
                if (hpfOn) x = hpf.process (x, ch);
                if (e.resOn) for (auto& b : bells) if (b.on) x = b.bq.process (x, ch);
                for (auto& n : notches) if (n.on) x = n.bq.process (x, ch);
                if (! std::isfinite (x)) x = 0.0f;
                chans[ch][i] = x;
                mono += x;
            }
            fifo[(size_t) fifoPos] = mono / (float) nch;
            fifoPos = (fifoPos + 1) & (kN - 1);
            if (++hopCount >= kHop) { hopCount = 0; analyse (e); }
        }

        sanitiseStates (e);
    }

private:
    struct Notch { bool on = false, fixed = false; float f = 0.f; juce::uint64 stamp = 0; Biquad bq; };
    struct Bell  { bool on = false; float f = 0.f; int bin = 0; float cur = 0.f, applied = 999.f; Biquad bq; };
    struct Cand  { int bin = 0, count = 0; };

    // เรียกท้ายทุก block: ถ้าฟิลเตอร์ตัวไหน state เสีย ล้างเฉพาะตัวนั้น
    // ราคาถูกมาก แต่กันเสียงตายถาวรจากไดรเวอร์กระตุกครั้งเดียว
    void sanitiseStates (const Eff& e)
    {
        if (! hpf.stateIsSane()) hpf.reset();
        if (e.resOn) for (auto& b : bells) if (b.on && ! b.bq.stateIsSane()) b.bq.reset();
        for (auto& n : notches) if (n.on && ! n.bq.stateIsSane()) n.bq.reset();
        for (auto& v : fifo) if (! std::isfinite (v)) v = 0.f;
    }

    int countActive() const
    {
        return (int) std::count_if (notches.begin(), notches.end(), [] (const Notch& n) { return n.on; });
    }

    // includeFixed=false จะไม่แตะฟิลเตอร์ที่ตั้งไว้ตอน Ring Out
    Notch* oldest (bool includeFixed)
    {
        Notch* o = nullptr;
        for (auto& n : notches)
            if (n.on && (includeFixed || ! n.fixed) && (o == nullptr || n.stamp < o->stamp)) o = &n;
        if (o == nullptr && ! includeFixed) return oldest (true);
        return o;
    }

    void addNotch (float f, const Eff& e)
    {
        for (auto& n : notches) if (n.on && std::abs (n.f - f) < f * 0.02f) return;

        Notch* slot = nullptr;
        for (auto& n : notches) if (! n.on) { slot = &n; break; }
        if (slot == nullptr || countActive() >= e.maxN) slot = oldest (false);
        if (slot == nullptr) return;

        *slot = Notch {};
        slot->on    = true;
        slot->fixed = e.makeFixed;
        slot->f     = f;
        slot->stamp = ++stampCounter;
        slot->bq.setNotch (f, e.q, sr);
    }

    // ค่าเฉลี่ยเคลื่อนที่บนแกน log ความถี่: หน้าต่าง = k * 2^(+-oct)
    // ใช้ log ไม่ใช่ linear เพราะหน้าต่าง linear จะเบี้ยวตามความชันของสเปกตรัม
    void logSmooth (const std::vector<float>& src, std::vector<float>& dst, double oct)
    {
        const int nb = kN / 2 + 1;
        prefix[0] = 0.f;
        for (int k = 0; k < nb; ++k) prefix[(size_t) k + 1] = prefix[(size_t) k] + src[(size_t) k];

        const double lm = std::pow (2.0, -oct), hm = std::pow (2.0, oct);
        for (int k = 0; k < nb; ++k)
        {
            int lo = std::max (1, (int) std::floor (k * lm));
            int hi = std::min (nb - 1, (int) std::ceil (k * hm));
            if (hi - lo < 6) { lo = std::max (1, k - 3); hi = std::min (nb - 1, k + 3); }
            dst[(size_t) k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
        }
    }

    // ส่วนที่โด่งเกินเส้นฐาน: smooth แคบ (1/12 oct) ลบ smooth กว้าง (3/4 oct)
    // tone แคบ ๆ จะถูกเฉลี่ยจนแทบหาย ส่วน resonance กว้าง ๆ ยังโด่งอยู่
    void computeExcess (const std::vector<float>& src, std::vector<float>& dst)
    {
        logSmooth (src, narrowS, 1.0 / 12.0);
        logSmooth (src, wideS, 0.75);
        for (size_t i = 0; i < dst.size(); ++i) dst[i] = narrowS[i] - wideS[i];
    }

    bool nearActiveNotch (float f) const
    {
        for (const auto& n : notches) if (n.on && std::abs (n.f - f) < f * 0.06f) return true;
        return false;
    }

    void placeBells (const Eff& e)
    {
        computeExcess (ltas, excessL);

        std::array<std::pair<float, int>, kMaxBell> picks {};
        int np = 0;
        for (int k = rmin + 1; k < rmax; ++k)
        {
            const float ex = excessL[(size_t) k];
            if (ex <= e.resThr) continue;
            if (excessL[(size_t) k] < excessL[(size_t) k - 1] || excessL[(size_t) k] < excessL[(size_t) k + 1]) continue;
            if (nearActiveNotch ((float) (k * sr / kN))) continue;   // ปล่อยให้ notch จัดการ

            bool merged = false;
            for (int p = 0; p < np; ++p)
                if (std::abs (std::log2 ((float) k / (float) picks[(size_t) p].second)) < 0.33f)
                {
                    if (ex > picks[(size_t) p].first) picks[(size_t) p] = { ex, k };
                    merged = true;
                    break;
                }
            if (merged) continue;

            if (np < kMaxBell) picks[(size_t) np++] = { ex, k };
            else
            {
                auto mn = std::min_element (picks.begin(), picks.end());
                if (ex > mn->first) *mn = { ex, k };
            }
        }
        std::sort (picks.begin(), picks.begin() + np, std::greater<std::pair<float, int>>());
        np = std::min (np, e.maxB);

        std::array<bool, kMaxBell> taken {};
        for (auto& b : bells)
        {
            if (! b.on) continue;
            bool keep = false;
            for (int p = 0; p < np; ++p)
                if (! taken[(size_t) p] && std::abs ((float) b.bin - (float) picks[(size_t) p].second) <= (float) b.bin * 0.06f)
                { taken[(size_t) p] = true; keep = true; break; }
            if (! keep) b.on = false;
        }
        for (int p = 0; p < np; ++p)
        {
            if (taken[(size_t) p]) continue;
            for (auto& b : bells)
                if (! b.on)
                {
                    b = Bell {};
                    b.on  = true;
                    b.bin = picks[(size_t) p].second;
                    b.f   = (float) (b.bin * sr / kN);
                    break;
                }
        }
        while ((int) std::count_if (bells.begin(), bells.end(), [] (const Bell& b) { return b.on; }) > e.maxB)
            for (auto it = bells.rbegin(); it != bells.rend(); ++it) if (it->on) { it->on = false; break; }
    }

    void updateBells (const Eff& e)
    {
        const float depth = e.resDepth * 0.01f;
        for (auto& b : bells)
        {
            if (! b.on) continue;
            const float ex  = excessN[(size_t) b.bin];
            const float tgt = -juce::jlimit (0.f, 18.f, ex - e.resThr) * depth;
            b.cur += (tgt - b.cur) * (tgt < b.cur ? 0.5f : 0.12f);   // attack เร็ว / release ช้า
            if (std::abs (b.cur - b.applied) > 0.1f)
            {
                b.applied = b.cur;
                b.bq.setPeak (b.f, e.resQ, b.cur, sr);
            }
        }
    }

    void analyse (const Eff& e)
    {
        const std::vector<float>& win = *window;
        for (int i = 0; i < kN; ++i)
            fftData[(size_t) i] = fifo[(size_t) ((fifoPos + i) & (kN - 1))] * win[(size_t) i];
        std::fill (fftData.begin() + kN, fftData.end(), 0.f);
        fft->performFrequencyOnlyForwardTransform (fftData.data(), true);

        const float scale = 4.f / (float) kN;
        for (int k = 0; k <= kN / 2; ++k)
            mag[(size_t) k] = 20.f * std::log10 (fftData[(size_t) k] * scale + 1e-9f);

        // ติดตามพื้นเสียงของห้อง: ลงเร็ว ขึ้นช้า
        // ค่านี้คือสิ่งที่ทำให้โหมด Auto ไม่ต้องให้ผู้ใช้ตั้ง Min Level เอง
        float frameLvl = -120.f;
        for (int k = kmin; k <= kmax; ++k) frameLvl = std::max (frameLvl, mag[(size_t) k]);
        if (std::isfinite (frameLvl))
            noiseFloor += (frameLvl - noiseFloor) * (frameLvl < noiseFloor ? 0.25f : 0.0008f);
        noiseFloor = juce::jlimit (-100.f, 0.f, noiseFloor);

        const float minLvl = e.autoLevel ? juce::jlimit (-65.f, -18.f, noiseFloor + e.minLvlOffset)
                                         : e.minLvlAbs;

        // ---------- Resonance ----------
        if (e.resOn)
        {
            if (frameLvl > minLvl)   // เก็บ LTAS เฉพาะตอนมีสัญญาณจริง (tau ~ 3 s)
            {
                const float a = juce::jlimit (0.001f, 0.2f, (float) (kHop / (3.0 * sr)));
                for (int k = 0; k <= kN / 2; ++k)
                    ltas[(size_t) k] = ltasReady ? ltas[(size_t) k] + (mag[(size_t) k] - ltas[(size_t) k]) * a
                                                 : mag[(size_t) k];
                if (! ltasReady && ++ltasFrames > 8) ltasReady = true;
                if (++scanCount >= (int) (sr / kHop)) { scanCount = 0; if (ltasReady) placeBells (e); }
            }
            computeExcess (mag, excessN);
            updateBells (e);
        }

        // ---------- Feedback ----------
        std::array<std::pair<float, int>, kMaxPeaks> peaks {};
        int np = 0;
        std::array<float, 129> nb {};
        for (int k = kmin; k <= kmax; ++k)
        {
            const float m = mag[(size_t) k];
            if (m < minLvl || m < mag[(size_t) k - 1] || m < mag[(size_t) k + 1]) continue;

            const int mw  = juce::jlimit (16, 63, (int) ((float) k * 0.10f));
            const int lo  = std::max (0, k - mw), hi = std::min (kN / 2, k + mw);
            const int cnt = hi - lo + 1;
            std::copy (mag.begin() + lo, mag.begin() + hi + 1, nb.begin());
            std::nth_element (nb.begin(), nb.begin() + cnt / 2, nb.begin() + cnt);
            if (m - nb[(size_t) cnt / 2] <= e.thresh) continue;

            if (np < kMaxPeaks) peaks[(size_t) np++] = { m, k };
            else
            {
                auto mn = std::min_element (peaks.begin(), peaks.end());
                if (m > mn->first) *mn = { m, k };
            }
        }

        std::array<Cand, kMaxPeaks> next {};
        for (int p = 0; p < np; ++p)
        {
            const int k = peaks[(size_t) p].second;
            int c = 1;
            for (int j = 0; j < nCand; ++j)
                if (std::abs (cand[(size_t) j].bin - k) <= 2) { c = cand[(size_t) j].count + 1; break; }
            next[(size_t) p] = { k, c };
        }
        cand  = next;
        nCand = np;

        const int holdHops = std::max (1, (int) (e.hold * 0.001 * sr / kHop));
        for (int j = 0; j < nCand; ++j)
        {
            if (cand[(size_t) j].count < holdHops) continue;
            addNotch (refine (cand[(size_t) j].bin), e);
            cand[(size_t) j].count = 0;
        }
    }

    float refine (int k) const   // parabolic interpolation หาความถี่จริงระหว่าง bin
    {
        const float a = mag[(size_t) k - 1], b = mag[(size_t) k], c = mag[(size_t) k + 1];
        const float d = 0.5f * (a - c) / (a - 2.f * b + c + 1e-9f);
        return juce::jlimit (20.f, (float) (sr * 0.49), (float) ((k + d) * sr / kN));
    }

    const juce::dsp::FFT*     fft    = nullptr;   // ใช้ร่วมกันทุก engine (perform เป็น const)
    const std::vector<float>* window = nullptr;

    std::vector<float> fifo, fftData, mag, ltas, excessL, excessN, narrowS, wideS, prefix;
    std::array<Notch, kMaxNotch> notches {};
    std::array<Bell,  kMaxBell>  bells {};
    std::array<Cand,  kMaxPeaks> cand {};
    Biquad hpf;
    double sr = 48000.0;
    int fifoPos = 0, hopCount = 0, nCand = 0, scanCount = 0, ltasFrames = 0, releaseHops = 0;
    int kmin = 2, kmax = 1024, rmin = 2, rmax = 1024;
    float lastQ = -1.f, lastLowCut = -1.f, noiseFloor = -60.f;
    bool hpfOn = true, ltasReady = false, ready = false;
    juce::uint64 stampCounter = 0;
};

//==================================================================
class DeFeedbackProcessor : public juce::AudioProcessor
{
public:
    DeFeedbackProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout())
    {
        pMode     = apvts.getRawParameterValue ("mode");
        pSens     = apvts.getRawParameterValue ("sens");
        pChMode   = apvts.getRawParameterValue ("chmode");
        pThresh   = apvts.getRawParameterValue ("thresh");
        pHold     = apvts.getRawParameterValue ("hold");
        pQ        = apvts.getRawParameterValue ("q");
        pMax      = apvts.getRawParameterValue ("max");
        pMinLvl   = apvts.getRawParameterValue ("minlvl");
        pReset    = apvts.getRawParameterValue ("reset");
        pLowCut   = apvts.getRawParameterValue ("lowcut");
        pResOn    = apvts.getRawParameterValue ("reson");
        pResThr   = apvts.getRawParameterValue ("resthr");
        pResDepth = apvts.getRawParameterValue ("resdepth");
        pResQ     = apvts.getRawParameterValue ("resq");
        pResMax   = apvts.getRawParameterValue ("resmax");
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        return {
            // --- ปกติแตะแค่สามตัวนี้พอ ---
            std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                StringArray { "Auto - Live", "Auto - Ring Out", "Manual" }, 0),
            std::make_unique<AudioParameterChoice> (ParameterID { "sens", 1 }, "Sensitivity",
                StringArray { "Low", "Normal", "High" }, 1),
            std::make_unique<AudioParameterChoice> (ParameterID { "chmode", 1 }, "Channel Mode",
                StringArray { "Auto", "Linked (stereo)", "Independent (multi-mic)" }, 0),
            // --- ด้านล่างนี้ใช้เฉพาะโหมด Manual ---
            std::make_unique<AudioParameterFloat> (ParameterID { "thresh", 1 }, "FB Peak Threshold (dB)",
                NormalisableRange<float> (6.f, 30.f, 0.5f), 15.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "hold", 1 }, "FB Detect Time (ms)",
                NormalisableRange<float> (30.f, 500.f, 1.f), 110.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "q", 1 }, "FB Notch Q",
                NormalisableRange<float> (10.f, 100.f, 1.f), 35.f),
            std::make_unique<AudioParameterInt>   (ParameterID { "max", 1 }, "FB Max Notches",
                1, ChannelEngine::kMaxNotch, 8),
            std::make_unique<AudioParameterFloat> (ParameterID { "minlvl", 1 }, "Min Level (dBFS)",
                NormalisableRange<float> (-70.f, -20.f, 1.f), -45.f),
            std::make_unique<AudioParameterBool>  (ParameterID { "reset", 1 }, "Reset All", false),
            std::make_unique<AudioParameterBool>  (ParameterID { "reson", 1 }, "Resonance On", true),
            std::make_unique<AudioParameterFloat> (ParameterID { "resthr", 1 }, "Res Threshold (dB)",
                NormalisableRange<float> (2.f, 12.f, 0.5f), 4.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "resdepth", 1 }, "Res Depth (%)",
                NormalisableRange<float> (0.f, 100.f, 1.f), 60.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "resq", 1 }, "Res Q",
                NormalisableRange<float> (2.f, 16.f, 0.1f), 6.f),
            std::make_unique<AudioParameterInt>   (ParameterID { "resmax", 1 }, "Res Max Bands",
                1, ChannelEngine::kMaxBell, 4),
            std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low Cut (Hz)",
                NormalisableRange<float> (20.f, 300.f, 1.f, 0.4f), 80.f)
        };
    }

    //==============================================================
    void prepareToPlay (double sampleRate, int) override
    {
        sr = (sampleRate > 1000.0 && sampleRate < 1000000.0) ? sampleRate : 48000.0;

        const int kN = ChannelEngine::kN;
        window.resize ((size_t) kN);
        for (int i = 0; i < kN; ++i)
            window[(size_t) i] = 0.5f * (1.f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kN - 1)));

        rebuildEngines (std::max (1, std::min (getTotalNumInputChannels(), kMaxChannels)));
        prepared = true;
    }

    void releaseResources() override
    {
        prepared = false;
        for (auto& e : engines) e->release();
    }

    // รับได้ทั้ง mono, stereo และ discrete หลายช่อง (สูงสุด 8) ขอแค่เข้าเท่าออก
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        const auto in  = l.getMainInputChannelSet();
        if (out != in || out.isDisabled()) return false;
        const int n = out.size();
        return n >= 1 && n <= kMaxChannels;
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        // กันโฮสต์ที่เรียก processBlock ก่อน prepareToPlay (Cubase ทำตอนสแกนปลั๊กอิน)
        // ถ้ายังไม่พร้อม ปล่อยเสียงผ่านไปเฉย ๆ อย่าแตะอะไรทั้งนั้น
        if (! prepared) return;

        const int nch = std::min (buffer.getNumChannels(), kMaxChannels);
        const int ns  = buffer.getNumSamples();
        if (nch <= 0 || ns <= 0) return;

        const int chMode = (int) pChMode->load();
        if (nch != lastChannelCount || chMode != lastChMode) rebuildEngines (nch);
        if (engines.empty()) return;

        const bool rs = pReset->load() > 0.5f;
        if (rs && ! lastReset) for (auto& eng : engines) eng->resetAll();
        lastReset = rs;

        const Eff e = effective();

        int engineIdx = 0;
        int ch = 0;
        if (linked && nch >= 2)
        {
            float* pair[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
            engines[0]->process (pair, 2, ns, e);
            engineIdx = 1;
            ch = 2;
        }
        for (; ch < nch && (size_t) engineIdx < engines.size(); ++ch, ++engineIdx)
        {
            float* one[1] = { buffer.getWritePointer (ch) };
            engines[(size_t) engineIdx]->process (one, 1, ns, e);
        }
    }

    //==============================================================
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DeFeedback"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    // สำหรับการทดสอบและแสดงผล
    int  getEngineCount() const { return (int) engines.size(); }
    bool isLinked()       const { return linked; }
    int  notchesOnEngine (int i) const
    {
        return ((size_t) i < engines.size()) ? engines[(size_t) i]->activeNotchCount() : -1;
    }

private:
    void rebuildEngines (int nch)
    {
        nch = juce::jlimit (1, kMaxChannels, nch);

        const int chMode = (int) pChMode->load();   // 0=Auto 1=Linked 2=Independent
        // Auto: 2 ช่องถือเป็น stereo คู่เดียว, มากกว่านั้นถือว่าเป็นไมค์คนละตัว
        linked = (chMode == 1) || (chMode == 0 && nch == 2);

        const int need = (linked && nch >= 2) ? (nch - 1) : nch;

        engines.clear();
        engines.reserve ((size_t) need);
        for (int i = 0; i < need; ++i)
        {
            engines.push_back (std::make_unique<ChannelEngine>());
            engines.back()->prepare (sr, &fft, &window);
        }
        lastChannelCount = nch;
        lastChMode = chMode;
    }

    Eff effective() const
    {
        const int mode = (int) pMode->load();        // 0=Live 1=RingOut 2=Manual
        Eff e {};
        e.lowCut = pLowCut->load();

        if (mode == 2)
        {
            e.thresh    = pThresh->load();
            e.hold      = pHold->load();
            e.q         = pQ->load();
            e.autoLevel = false;
            e.minLvlAbs = pMinLvl->load();
            e.resThr    = pResThr->load();
            e.resDepth  = pResDepth->load();
            e.resQ      = pResQ->load();
            e.maxN      = (int) pMax->load();
            e.maxB      = (int) pResMax->load();
            e.resOn     = pResOn->load() > 0.5f;
            e.makeFixed = true;
            e.floatRelease = false;
            return e;
        }

        const int sens = juce::jlimit (0, 2, (int) pSens->load());
        //                           Low    Normal  High
        static const float TH[3] = { 18.f,  15.f,   12.f };
        static const float HD[3] = { 170.f, 120.f,  80.f };
        static const float QQ[3] = { 42.f,  35.f,   28.f };
        static const float RT[3] = {  6.f,   4.f,    3.f };
        static const float RD[3] = { 40.f,  60.f,   75.f };

        e.thresh    = TH[sens];
        e.hold      = HD[sens];
        e.q         = QQ[sens];
        e.resThr    = RT[sens];
        e.resDepth  = RD[sens];
        e.resQ      = 6.f;
        e.maxN      = 10;
        e.maxB      = 4;
        e.resOn     = true;
        e.autoLevel = true;

        // Ring Out = ไล่หอนก่อนงาน จับไวกว่า ฟิลเตอร์ที่ได้ค้างถาวร
        // Live     = ระหว่างงาน ระวังกว่า ฟิลเตอร์ลอยได้ ปล่อยคืนเมื่อไม่ได้ใช้
        if (mode == 1)
        {
            e.thresh      -= 3.f;
            e.hold         = 60.f;
            e.makeFixed    = true;
            e.floatRelease = false;
            e.maxN         = ChannelEngine::kMaxNotch;
        }
        else
        {
            e.makeFixed    = false;
            e.floatRelease = true;
        }
        return e;
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> *pMode = nullptr, *pSens = nullptr, *pChMode = nullptr,
                       *pThresh = nullptr, *pHold = nullptr, *pQ = nullptr, *pMax = nullptr,
                       *pMinLvl = nullptr, *pReset = nullptr, *pLowCut = nullptr, *pResOn = nullptr,
                       *pResThr = nullptr, *pResDepth = nullptr, *pResQ = nullptr, *pResMax = nullptr;

    juce::dsp::FFT fft { ChannelEngine::kOrder };   // ใช้ร่วมกันทุก engine
    std::vector<float> window;
    std::vector<std::unique_ptr<ChannelEngine>> engines;

    double sr = 48000.0;
    int lastChannelCount = 0, lastChMode = -1;
    bool prepared = false, lastReset = false, linked = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeFeedbackProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeFeedbackProcessor(); }
