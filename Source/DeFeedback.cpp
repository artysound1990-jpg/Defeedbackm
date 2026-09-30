// DeFeedback - VST3 automatic feedback + microphone resonance suppressor
//
//   Low Cut  ->  Resonance bells (dynamic, LTAS-guided)  ->  Feedback notches
//
// Feedback  : peak แคบที่ค้างนาน -> วาง notch ลึกถาวร
// Resonance : peak ที่โผล่ซ้ำ ๆ ในสเปกตรัมเฉลี่ยระยะยาว (สีของไมค์/proximity/ห้อง)
//             -> วาง bell กว้างกว่า ลดเฉพาะตอนที่ย่านนั้นเด้งขึ้นมาจริง (dynamic EQ)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

//==================================================================
struct Biquad
{
    static constexpr int kMaxCh = 2;
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
class DeFeedbackProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kOrder = 12, kN = 1 << kOrder, kHop = 512;
    static constexpr int kMaxNotch = 16, kMaxBell = 8, kMaxCh = 2, kMaxPeaks = 3;

    DeFeedbackProcessor()
        : AudioProcessor (BusesProperties()
              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout())
    {
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
            // --- Feedback ---
            std::make_unique<AudioParameterFloat> (ParameterID { "thresh", 1 }, "FB Peak Threshold (dB)",
                NormalisableRange<float> (6.f, 30.f, 0.5f), 15.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "hold", 1 }, "FB Detect Time (ms)",
                NormalisableRange<float> (30.f, 500.f, 1.f), 110.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "q", 1 }, "FB Notch Q",
                NormalisableRange<float> (10.f, 100.f, 1.f), 35.f),
            std::make_unique<AudioParameterInt>   (ParameterID { "max", 1 }, "FB Max Notches", 1, kMaxNotch, 8),
            std::make_unique<AudioParameterFloat> (ParameterID { "minlvl", 1 }, "Min Level (dBFS)",
                NormalisableRange<float> (-70.f, -20.f, 1.f), -45.f),
            std::make_unique<AudioParameterBool>  (ParameterID { "reset", 1 }, "Reset All", false),
            // --- Resonance / mic tone ---
            std::make_unique<AudioParameterBool>  (ParameterID { "reson", 1 }, "Resonance On", true),
            std::make_unique<AudioParameterFloat> (ParameterID { "resthr", 1 }, "Res Threshold (dB)",
                NormalisableRange<float> (2.f, 12.f, 0.5f), 4.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "resdepth", 1 }, "Res Depth (%)",
                NormalisableRange<float> (0.f, 100.f, 1.f), 60.f),
            std::make_unique<AudioParameterFloat> (ParameterID { "resq", 1 }, "Res Q",
                NormalisableRange<float> (2.f, 16.f, 0.1f), 6.f),
            std::make_unique<AudioParameterInt>   (ParameterID { "resmax", 1 }, "Res Max Bands", 1, kMaxBell, 4),
            // --- Utility ---
            std::make_unique<AudioParameterFloat> (ParameterID { "lowcut", 1 }, "Low Cut (Hz)",
                NormalisableRange<float> (20.f, 300.f, 1.f, 0.4f), 80.f)
        };
    }

    //==============================================================
    void prepareToPlay (double sampleRate, int) override
    {
        // โฮสต์บางตัวส่ง sampleRate เป็น 0 หรือค่าเพี้ยนตอนสแกน ต้องกันไว้
        // ไม่งั้น kmin/kmax ที่คำนวณจากมันจะกลายเป็นขยะแล้วอ่านทะลุ array
        sr = (sampleRate > 1000.0 && sampleRate < 1000000.0) ? sampleRate : 48000.0;
        fifo.assign (kN, 0.f);
        fftData.assign (2 * kN, 0.f);
        const size_t nb = (size_t) kN / 2 + 1;
        mag.assign (nb, -120.f);
        ltas.assign (nb, -120.f);
        base.assign (nb, -120.f);
        baseNow.assign (nb, -120.f);
        narrowS.assign (nb, -120.f);
        wideS.assign (nb, -120.f);
        prefix.assign (nb + 1, 0.f);
        window.resize (kN);
        for (int i = 0; i < kN; ++i)
            window[(size_t) i] = 0.5f * (1.f - std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kN - 1)));

        fifoPos = hopCount = nCand = scanCount = ltasFrames = 0;
        ltasReady = false;
        for (auto& n : notches) n = Notch {};
        for (auto& b : bells)   b = Bell {};
        lastQ = lastLowCut = -1.f;
        hpf.reset();
        kmin = juce::jlimit (2, kN / 2 - 2, (int) (80.0 * kN / sr));
        kmax = juce::jlimit (kmin, kN / 2 - 2, (int) (std::min (12000.0, sr * 0.45) * kN / sr));
        rmin = juce::jlimit (2, kN / 2 - 2, (int) (120.0 * kN / sr));
        rmax = juce::jlimit (rmin, kN / 2 - 2, (int) (std::min (9000.0, sr * 0.45) * kN / sr));

        prepared = true;
    }

    void releaseResources() override { prepared = false; }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        auto out = l.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
            && out == l.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        // กันโฮสต์ที่เรียก processBlock ก่อน prepareToPlay (Cubase ทำตอนสแกนปลั๊กอิน)
        // ถ้าบัฟเฟอร์ยังไม่ถูกจอง ปล่อยเสียงผ่านไปเฉย ๆ อย่าแตะอะไรทั้งนั้น
        if (! prepared || fifo.size() != (size_t) kN || mag.size() != (size_t) kN / 2 + 1)
            return;

        const int nch = std::min (buffer.getNumChannels(), kMaxCh);
        const int ns  = buffer.getNumSamples();
        if (nch <= 0 || ns <= 0) return;

        const bool rs = pReset->load() > 0.5f;
        if (rs && ! lastReset)
        {
            for (auto& n : notches) n.on = false;
            for (auto& b : bells)   b = Bell {};
            std::fill (ltas.begin(), ltas.end(), -120.f);
            ltasReady = false;
            ltasFrames = 0;
        }
        lastReset = rs;

        const float q = pQ->load();
        if (q != lastQ) { lastQ = q; for (auto& n : notches) if (n.on) n.bq.setNotch (n.f, q, sr); }

        const float lc = pLowCut->load();
        hpfOn = lc > 21.f;
        if (hpfOn && lc != lastLowCut) { lastLowCut = lc; hpf.setHighpass (lc, sr); }

        const int maxN = (int) pMax->load();
        while (activeNotches() > maxN) { if (auto* o = oldestNotch()) o->on = false; else break; }

        const bool resOn = pResOn->load() > 0.5f;

        for (int i = 0; i < ns; ++i)
        {
            float mono = 0.f;
            for (int ch = 0; ch < nch; ++ch)
            {
                float x = buffer.getSample (ch, i);
                if (! std::isfinite (x)) x = 0.0f;      // กันค่าเสียจากไดรเวอร์/ปลั๊กอินตัวหน้า
                if (hpfOn) x = hpf.process (x, ch);
                if (resOn) for (auto& b : bells)   if (b.on) x = b.bq.process (x, ch);
                for (auto& n : notches) if (n.on) x = n.bq.process (x, ch);
                if (! std::isfinite (x)) x = 0.0f;
                buffer.setSample (ch, i, x);
                mono += x;
            }
            fifo[(size_t) fifoPos] = mono / (float) nch;
            fifoPos = (fifoPos + 1) & (kN - 1);
            if (++hopCount >= kHop) { hopCount = 0; analyse (resOn); }
        }

        sanitiseStates();
    }

    // เรียกท้ายทุก block: ถ้าฟิลเตอร์ตัวไหน state เสีย ล้างเฉพาะตัวนั้น
    // ราคาถูกมาก (ไม่กี่สิบการเปรียบเทียบต่อ block) แต่กันเสียงตายถาวร
    void sanitiseStates()
    {
        if (! hpf.stateIsSane()) hpf.reset();
        for (auto& b : bells)   if (b.on && ! b.bq.stateIsSane()) b.bq.reset();
        for (auto& n : notches) if (n.on && ! n.bq.stateIsSane()) n.bq.reset();

        for (auto& v : fifo) if (! std::isfinite (v)) v = 0.f;
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

private:
    struct Notch { bool on = false; float f = 0.f; juce::uint64 stamp = 0; Biquad bq; };
    struct Bell  { bool on = false; float f = 0.f; int bin = 0; float cur = 0.f, applied = 999.f; Biquad bq; };
    struct Cand  { int bin = 0, count = 0; };

    int activeNotches() const
    {
        return (int) std::count_if (notches.begin(), notches.end(), [] (const Notch& n) { return n.on; });
    }

    Notch* oldestNotch()
    {
        Notch* o = nullptr;
        for (auto& n : notches) if (n.on && (o == nullptr || n.stamp < o->stamp)) o = &n;
        return o;
    }

    void addNotch (float f)
    {
        for (auto& n : notches) if (n.on && std::abs (n.f - f) < f * 0.02f) return;

        Notch* slot = nullptr;
        if (activeNotches() >= (int) pMax->load()) slot = oldestNotch();
        else for (auto& n : notches) if (! n.on) { slot = &n; break; }
        if (slot == nullptr) return;

        *slot = Notch {};
        slot->on = true;
        slot->f = f;
        slot->stamp = ++stampCounter;
        slot->bq.setNotch (f, lastQ, sr);
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

    void placeBells()
    {
        const float thr = pResThr->load();
        const int maxB = (int) pResMax->load();
        computeExcess (ltas, base);

        // เก็บ peak ที่เด่นสุดใน LTAS โดยเว้นระยะกัน ~1/3 octave
        std::array<std::pair<float, int>, kMaxBell> picks {};
        int np = 0;
        for (int k = rmin + 1; k < rmax; ++k)
        {
            const float ex = base[(size_t) k];
            if (ex <= thr) continue;
            if (base[(size_t) k] < base[(size_t) k - 1] || base[(size_t) k] < base[(size_t) k + 1]) continue;
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
        np = std::min (np, maxB);

        // ของเดิมที่ยังตรงกับ peak -> เก็บไว้ (กันความถี่กระโดด), ที่เหลือปล่อย
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
                    b.on = true;
                    b.bin = picks[(size_t) p].second;
                    b.f = (float) (b.bin * sr / kN);
                    break;
                }
        }
        while ((int) std::count_if (bells.begin(), bells.end(), [] (const Bell& b) { return b.on; }) > maxB)
            for (auto it = bells.rbegin(); it != bells.rend(); ++it) if (it->on) { it->on = false; break; }
    }

    void updateBells()
    {
        const float thr = pResThr->load(), depth = pResDepth->load() * 0.01f, rq = pResQ->load();
        for (auto& b : bells)
        {
            if (! b.on) continue;
            const float ex = baseNow[(size_t) b.bin];
            const float tgt = -juce::jlimit (0.f, 18.f, ex - thr) * depth;
            b.cur += (tgt - b.cur) * (tgt < b.cur ? 0.5f : 0.12f);   // attack เร็ว / release ช้า
            if (std::abs (b.cur - b.applied) > 0.1f)
            {
                b.applied = b.cur;
                b.bq.setPeak (b.f, rq, b.cur, sr);
            }
        }
    }

    void analyse (bool resOn)
    {
        for (int i = 0; i < kN; ++i)
            fftData[(size_t) i] = fifo[(size_t) ((fifoPos + i) & (kN - 1))] * window[(size_t) i];
        std::fill (fftData.begin() + kN, fftData.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

        const float scale = 4.f / (float) kN;
        for (int k = 0; k <= kN / 2; ++k)
            mag[(size_t) k] = 20.f * std::log10 (fftData[(size_t) k] * scale + 1e-9f);

        const float minLvl = pMinLvl->load();

        // ---------- Resonance ----------
        if (resOn)
        {
            float frameLvl = -120.f;
            for (int k = rmin; k <= rmax; ++k) frameLvl = std::max (frameLvl, mag[(size_t) k]);

            if (frameLvl > minLvl)   // เก็บ LTAS เฉพาะตอนมีสัญญาณจริง (tau ~ 3 s)
            {
                const float a = juce::jlimit (0.001f, 0.2f, (float) (kHop / (3.0 * sr)));
                for (int k = 0; k <= kN / 2; ++k)
                    ltas[(size_t) k] = ltasReady ? ltas[(size_t) k] + (mag[(size_t) k] - ltas[(size_t) k]) * a
                                                 : mag[(size_t) k];
                if (! ltasReady && ++ltasFrames > 8) ltasReady = true;
                if (++scanCount >= (int) (sr / kHop)) { scanCount = 0; if (ltasReady) placeBells(); }
            }
            computeExcess (mag, baseNow);
            updateBells();
        }

        // ---------- Feedback ----------
        const float thresh = pThresh->load();
        std::array<std::pair<float, int>, kMaxPeaks> peaks {};
        int np = 0;
        std::array<float, 129> nb {};
        for (int k = kmin; k <= kmax; ++k)
        {
            const float m = mag[(size_t) k];
            if (m < minLvl || m < mag[(size_t) k - 1] || m < mag[(size_t) k + 1]) continue;

            const int mw = juce::jlimit (16, 63, (int) ((float) k * 0.10f));
            const int lo = std::max (0, k - mw), hi = std::min (kN / 2, k + mw);
            const int cnt = hi - lo + 1;
            std::copy (mag.begin() + lo, mag.begin() + hi + 1, nb.begin());
            std::nth_element (nb.begin(), nb.begin() + cnt / 2, nb.begin() + cnt);
            if (m - nb[(size_t) cnt / 2] <= thresh) continue;

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
        cand = next;
        nCand = np;

        const int holdHops = std::max (1, (int) (pHold->load() * 0.001 * sr / kHop));
        for (int j = 0; j < nCand; ++j)
        {
            if (cand[(size_t) j].count < holdHops) continue;
            addNotch (refine (cand[(size_t) j].bin));
            cand[(size_t) j].count = 0;
        }
    }

    float refine (int k) const
    {
        const float a = mag[(size_t) k - 1], b = mag[(size_t) k], c = mag[(size_t) k + 1];
        const float d = 0.5f * (a - c) / (a - 2.f * b + c + 1e-9f);
        return juce::jlimit (20.f, (float) (sr * 0.49), (float) ((k + d) * sr / kN));
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> *pThresh = nullptr, *pHold = nullptr, *pQ = nullptr, *pMax = nullptr,
                       *pMinLvl = nullptr, *pReset = nullptr, *pLowCut = nullptr, *pResOn = nullptr,
                       *pResThr = nullptr, *pResDepth = nullptr, *pResQ = nullptr, *pResMax = nullptr;

    juce::dsp::FFT fft { kOrder };
    std::vector<float> fifo, fftData, mag, ltas, base, baseNow, narrowS, wideS, prefix, window;
    std::array<Notch, kMaxNotch> notches {};
    std::array<Bell,  kMaxBell>  bells {};
    std::array<Cand,  kMaxPeaks> cand {};
    Biquad hpf;
    double sr = 48000.0;
    int fifoPos = 0, hopCount = 0, nCand = 0, scanCount = 0, ltasFrames = 0;
    int kmin = 2, kmax = 1024, rmin = 2, rmax = 1024;
    float lastQ = -1.f, lastLowCut = -1.f;
    bool lastReset = false, hpfOn = true, ltasReady = false, prepared = false;
    juce::uint64 stampCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DeFeedbackProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DeFeedbackProcessor(); }
