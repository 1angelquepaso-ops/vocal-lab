#pragma once
// DREAMMIC 2.0 real-time engine.
// LIVE: MEX-EQ matched-magnitude filters (analog curve kept right up to 20 kHz, zero added latency)
//       + 2x oversampled, anti-aliased preamp (a few samples of latency).
// HQ:   the whole mic/proximity/high-pass curve as one linear-phase FIR built straight from the
//       ANALOG curve + 8x oversampled preamp (latency reported to the host, which compensates).
// Then (v2, optional): tracking compressor + de-esser shared with MIRRORWALL 2.0 (Live.h, fitted
// to our clean captures), Auto input rider, and Level match (output loudness = input loudness).
#include <juce_dsp/juce_dsp.h>
#include "Models.h"
#include "Live.h"          // MIRRORWALL 2.0 stages + MEX-EQ design (mirrorwall_plugin/Source)
#include <atomic>
#include <cmath>
#include <complex>

namespace dm
{
constexpr double PI = 3.14159265358979323846;
constexpr double AUTO_REF_DB = -18.0;   // Auto input aims a sung line here (pro "0 VU" level)

// 2-channel TDF-II biquad holding MEX-EQ (matched) coefficients
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1[2] = {}, z2[2] = {};
    inline double tick (double x, int ch)
    {
        const double y = b0 * x + z1[ch];
        z1[ch] = b1 * x - a1 * y + z2[ch];
        z2[ch] = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
};

// ---------- gear table entries -> analog sections ----------
inline void addSections (const F& f, double gainSign, std::vector<mx::ASec>& out)
{
    mx::BandSpec b;
    b.order = mx::Order::Two;
    b.freq = f.f; b.q = f.q; b.gainDb = f.g * gainSign;
    switch (f.t)
    {
        case FType::HP: b.type = mx::Type::HighPass; break;
        case FType::LP: b.type = mx::Type::LowPass; break;
        case FType::Peak: b.type = mx::Type::Peak; break;
        case FType::LowShelf: b.type = mx::Type::LowShelf; break;
        case FType::HighShelf: b.type = mx::Type::HighShelf; break;
    }
    if ((f.t == FType::Peak || f.t == FType::LowShelf || f.t == FType::HighShelf) && std::abs (b.gainDb) < 1e-9) return;
    std::vector<mx::ASec> s;
    mx::designBand (b, s);
    out.insert (out.end(), s.begin(), s.end());
}

struct Params
{
    int source = 1, mic = 0, pattern = 0, pre = 0, mode = 0;   // pattern 0 = native
    double distanceCm = 15, warmth = 3, hpfHz = 20, airDb = 0, inDb = 0, outDb = 0;
    // v2
    bool lowCut = false, presence = false, autoIn = false, match = false;
    int comp = 0;
    double compAmt = 4, deess = 0;
};

static const double PATTERN_A[] = { -1.0, 0.5, 0.63, 0.0, 1.0 };   // native, cardioid, super, omni, fig-8
static const char* PATTERN_NAMES[] = { "Native", "Cardioid", "Supercardioid", "Omni", "Figure-8" };

// The static (linear) part as analog sections: source correction, target mic (+ its switches), proximity, high-pass.
inline std::vector<mx::ASec> linearAnalogFor (const Params& p)
{
    std::vector<mx::ASec> v;
    const int src = juce::jlimit (0, NUM_SOURCES - 1, p.source);
    // undo the source mic
    if (src == 1)
        for (int i = 0; i < 12; ++i) addSections ({ EQ12[i].t, EQ12[i].f, EQ12[i].q, C01U_TO_MA50[i] }, 1.0, v);
    if (const int sm = sourceMicIndex (src); sm >= 0)
    {
        const auto& m = MICS[sm];
        for (int i = 0; i < m.numFilters; ++i)
            if (m.filters[i].t != FType::HP && m.filters[i].t != FType::LP)
                addSections (m.filters[i], -1.0, v);
    }
    // target mic
    const auto& m = MICS[juce::jlimit (0, NUM_MICS - 1, p.mic)];
    for (int i = 0; i < m.numFilters; ++i) addSections (m.filters[i], 1.0, v);
    if (p.lowCut) for (int i = 0; i < m.lowCut.n; ++i) addSections (m.lowCut.f[i], 1.0, v);
    if (p.presence) for (int i = 0; i < m.presence.n; ++i) addSections (m.presence.f[i], 1.0, v);
    // proximity relative to how it was recorded (15 cm, source pattern): H = (s + k a c / r_new) / (s + k a c / r_ref)
    const double c = 343.0;
    const double aSrc = sourcePatternA (src), kSrc = sourceProxScale (src);
    const double aNew = p.pattern == 0 ? aSrc : PATTERN_A[juce::jlimit (0, 4, p.pattern)];
    const double kNew = m.proxScale;
    const double rNew = juce::jlimit (0.02, 1.0, p.distanceCm / 100.0), rRef = 0.15;
    const double floorW = 2 * PI * 5.0;
    const double wz = std::max (floorW, kNew * aNew * c / rNew), wp = std::max (floorW, kSrc * aSrc * c / rRef);
    if (std::abs (wz - wp) > 1e-6)
    {
        mx::ASec s; s.b2 = 0; s.b1 = 1; s.b0 = wz; s.a2 = 0; s.a1 = 1; s.a0 = wp;
        v.push_back (s);
    }
    // high-pass
    if (p.hpfHz > 21) addSections ({ FType::HP, p.hpfHz, 0.707, 0 }, 1.0, v);
    return v;
}

// Post-preamp tone: preamp tilt + Air, and a DC blocker (tube bias can leave a tiny offset)
inline std::vector<mx::ASec> postAnalogFor (const Params& p)
{
    std::vector<mx::ASec> v;
    const auto& pm = PRES[juce::jlimit (0, NUM_PRES - 1, p.pre)];
    for (int i = 0; i < pm.numTilt; ++i) addSections (pm.tilt[i], 1.0, v);
    if (p.airDb > 0.05) addSections ({ FType::HighShelf, 14000, 0.5, p.airDb }, 1.0, v);
    v.push_back (mx::secHP1 (2 * PI * 5.0));
    return v;
}

inline std::vector<Biquad> toBiquads (const std::vector<mx::ASec>& secs, double sr)
{
    std::vector<mx::BQ> d; mx::toDigital (secs, sr, d);
    std::vector<Biquad> v;
    for (auto& q : d) { Biquad b; b.b0 = q.b0; b.b1 = q.b1; b.b2 = q.b2; b.a1 = q.a1; b.a2 = q.a2; v.push_back (b); }
    return v;
}

// One anti-aliased memoryless stage (MIRRORWALL 2.0 trick): band-limited average of the curve over the last
// sample step minus the same average of a straight line -> only the harmonics are smoothed, the clean part is exact.
struct AAStage
{
    double xp = 0;
    template <typename Fn> inline double p (double x, Fn&& f)
    {
        const double mid = 0.5 * (x + xp);
        const double avg = (f (xp) + 4.0 * f (mid) + f (x)) / 6.0;
        xp = x;
        return x + (avg - mid);
    }
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate;
        chans = juce::jlimit (1, 2, numChannels);
        block = maxBlock;
        osLive = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) chans, 1,
                   juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        osHQ = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) chans, 3,
                   juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        osLive->initProcessing ((size_t) maxBlock);
        osHQ->initProcessing ((size_t) maxBlock);
        firSize = sr > 50000 ? 8192 : 4096;
        conv.reset (new juce::dsp::Convolution());
        conv->prepare ({ sr, (juce::uint32) maxBlock, (juce::uint32) chans });
        lastFirKey.clear();
        dirty = true; dynDirty = true;
        for (auto& s : pre) s = {};
        for (auto& s : aaMic) s = {};
        for (auto& s : aaPre) s = {};
        smoothIn.reset (sr, 0.05); smoothOut.reset (sr, 0.05);
        smoothIn.setCurrentAndTargetValue (1.0f); smoothOut.setCurrentAndTargetValue (1.0f);
        riderA = mwl::coef (400.0, sr);
        riderP = 0; autoDb = 0; compGain = 1;
    }

    void setParams (const Params& np)
    {
        const bool linChange = np.source != p.source || np.mic != p.mic || np.pattern != p.pattern
                               || np.lowCut != p.lowCut || np.presence != p.presence
                               || std::abs (np.distanceCm - p.distanceCm) > 1e-6 || std::abs (np.hpfHz - p.hpfHz) > 1e-6;
        const bool postChange = np.pre != p.pre || std::abs (np.airDb - p.airDb) > 1e-6;
        if (linChange || postChange || np.mode != p.mode) dirty = true;
        if (np.comp != p.comp || std::abs (np.compAmt - p.compAmt) > 1e-6 || std::abs (np.deess - p.deess) > 1e-6) dynDirty = true;
        p = np;
        smoothIn.setTargetValue ((float) juce::Decibels::decibelsToGain (p.inDb));
        const double mk = p.match ? (double) matchDb.load() : 0.0;
        smoothOut.setTargetValue ((float) juce::Decibels::decibelsToGain (p.outDb + mk));
    }

    int latencyFor (int mode) const
    {
        if (mode == 1) return firSize / 2 + (int) std::round (osHQ ? osHQ->getLatencyInSamples() : 0);
        return (int) std::round (osLive ? osLive->getLatencyInSamples() : 0);
    }

    // magnitude (dB) of the whole static curve (exact analog), for the display
    double curveDb (double f) const
    {
        const auto h = mx::analogResponse (linearAnalogFor (p), f) * mx::analogResponse (postAnalogFor (p), f);
        return 20 * std::log10 (std::abs (h) + 1e-12);
    }
    // what LIVE actually plays (digital), for tests
    double liveCurveDb (double f) const
    {
        std::vector<mx::BQ> a, b; mx::toDigital (linearAnalogFor (p), sr, a); mx::toDigital (postAnalogFor (p), sr, b);
        return 20 * std::log10 (std::abs (mx::digitalResponse (a, f, sr) * mx::digitalResponse (b, f, sr)) + 1e-12);
    }

    // call off the audio thread when HQ is on and the curve changed
    void rebuildFir (const Params& q)
    {
        const auto key = firKeyFor (q);
        if (key == lastFirKey) return;
        lastFirKey = key;
        const int N = firSize;
        const auto chain = linearAnalogFor (q);
        juce::dsp::FFT fft ((int) std::log2 (N));
        std::vector<std::complex<float>> spec ((size_t) N), time ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = (double) k * sr / N;
            const float mag = (float) std::abs (mx::analogResponse (chain, std::max (f, 1.0)));
            spec[(size_t) k] = { mag, 0.0f };
            if (k > 0 && k < N / 2) spec[(size_t) (N - k)] = { mag, 0.0f };
        }
        fft.perform (spec.data(), time.data(), true);
        juce::AudioBuffer<float> ir (1, N);
        for (int n = 0; n < N; ++n)
        {
            const int src = (n + N / 2) % N;                   // centre the zero-phase response
            const double w = 0.42 - 0.5 * std::cos (2 * PI * n / (N - 1)) + 0.08 * std::cos (4 * PI * n / (N - 1));
            ir.setSample (0, n, time[(size_t) src].real() * (float) w);
        }
        conv->loadImpulseResponse (std::move (ir), sr, juce::dsp::Convolution::Stereo::no,
                                   juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        firReady = true;
    }

    bool needsFir (const Params& q) const { return q.mode == 1 && firKeyFor (q) != lastFirKey; }

    void process (juce::AudioBuffer<float>& buf)
    {
        const int n = buf.getNumSamples();
        const int nc = juce::jmin (chans, buf.getNumChannels());
        if (dirty) rebuildFilters();
        if (dynDirty) rebuildDynamics();

        // input gain (knob or Auto rider) + meter
        for (int i = 0; i < n; ++i)
        {
            float g = smoothIn.getNextValue();
            if (p.autoIn)
            {
                double x = 0;
                for (int c = 0; c < nc; ++c) x += buf.getReadPointer (c)[i];
                x /= nc;
                riderP = riderA * riderP + (1 - riderA) * x * x;
                const double lvl = 10.0 * std::log10 (riderP + 1e-18) + 3.0;      // ~RMS of a sung line
                if (lvl > -60.0)
                {
                    const double want = juce::jlimit (-24.0, 36.0, AUTO_REF_DB - lvl);
                    autoDb += juce::jlimit (-1.5, 1.5, want - autoDb) * (3.0 / sr);  // max ~3 dB per second
                }
                g = (float) mwl::dbToLin (autoDb);
            }
            for (int c = 0; c < nc; ++c) buf.getWritePointer (c)[i] *= g;
        }
        inPeak.store (std::max (inPeak.load(), buf.getMagnitude (0, n)));
        autoGainDb.store ((float) autoDb);

        // linear part
        if (p.mode == 1)
        {
            juce::dsp::AudioBlock<float> b (buf);
            auto sub = b.getSubsetChannelBlock (0, (size_t) nc);
            conv->process (juce::dsp::ProcessContextReplacing<float> (sub));
        }
        else runFilters (lin, buf, nc, n);

        // nonlinear part (mic electronics + preamp), oversampled + anti-aliased
        auto& os = p.mode == 1 ? *osHQ : *osLive;
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, (size_t) nc);
        auto up = os.processSamplesUp (sub);
        saturate (up, sr * (double) os.getOversamplingFactor());
        os.processSamplesDown (sub);

        // post: preamp tilt + air
        runFilters (post, buf, nc, n);

        // tracking compressor (linked: one detector on the mono sum) + de-esser
        double grPeak = 0;
        if (p.comp > 0 || p.deess > 0.01)
        {
            for (int i = 0; i < n; ++i)
            {
                if (p.comp > 0)
                {
                    double m = 0;
                    for (int c = 0; c < nc; ++c) m += buf.getReadPointer (c)[i];
                    m /= nc;
                    double y = m;
                    switch (p.comp)
                    {
                        case 1: y = opto.p (m, 0.0); break;
                        case 2: y = fet.p (m, 0.0); break;
                        case 3: y = vca.p (m, 0.0); break;
                        default: y = lev.p (m, 0.0); break;
                    }
                    if (std::abs (m) > 1e-9) compGain = y / m;      // these stages are pure gain: y = m * g
                    const double g = compGain * compMakeup;
                    grPeak = std::max (grPeak, -mwl::linToDb (compGain * (p.comp == 4 ? 1.0 / levMakeup : 1.0)));
                    for (int c = 0; c < nc; ++c) buf.getWritePointer (c)[i] = (float) (buf.getReadPointer (c)[i] * g);
                }
                if (p.deess > 0.01)
                    for (int c = 0; c < nc; ++c)
                        buf.getWritePointer (c)[i] = (float) deess[c].p (buf.getReadPointer (c)[i], 1.0);
            }
        }
        compGrDb.store ((float) std::max (grPeak, 0.0));

        for (int i = 0; i < n; ++i)
        {
            const float g = smoothOut.getNextValue();
            for (int c = 0; c < nc; ++c) buf.getWritePointer (c)[i] *= g;
        }
        if (buf.getNumChannels() > nc)
            for (int c = nc; c < buf.getNumChannels(); ++c) buf.copyFrom (c, 0, buf, 0, 0, n);
        outPeak.store (std::max (outPeak.load(), buf.getMagnitude (0, n)));
    }

    // Level match: run 4 s of speech-shaped, syllable-gated noise at -18 dBFS through a fresh LIVE copy of these
    // settings; return the make-up (dB) that makes the output as LOUD as the input. Loudness = K-weighted RMS
    // (the LUFS weighting), so a brighter mic isn't judged louder or quieter than it sounds.
    static double measureMatchDb (Params q, double rate)
    {
        q.mode = 0; q.match = false; q.outDb = 0;
        Engine e;
        const int bs = 512;
        e.prepare (rate, bs, 1);
        e.setParams (q);
        const int n = (int) (4.0 * rate);
        juce::AudioBuffer<float> x (1, n);
        juce::Random rng (7);
        // long-term vocal spectrum: rolls off below 100 Hz and ~6 dB/oct above 2 kHz (bright, rap-style)
        auto shape = toBiquads ({ mx::secHP2 (2 * PI * 100.0, 0.707), mx::secLP1 (2 * PI * 2000.0) }, rate);
        double e2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = std::fmod (3.0 * i / rate, 1.0);                 // 3 syllables a second with short gaps
            const double env = ph < 0.8 ? std::sin (PI * ph / 0.8) : 0.0;
            double v = rng.nextFloat() * 2.0 - 1.0;
            for (auto& b : shape) v = b.tick (v, 0);
            x.setSample (0, i, (float) (v * env));
            e2 += (double) x.getSample (0, i) * x.getSample (0, i);
        }
        x.applyGain ((float) (mwl::dbToLin (AUTO_REF_DB) / std::sqrt (e2 / n + 1e-20)));
        if (q.autoIn) e.autoDb = 0.0;                                 // rider starts settled on the reference
        e.smoothIn.setCurrentAndTargetValue ((float) juce::Decibels::decibelsToGain (q.inDb));
        e.smoothOut.setCurrentAndTargetValue (1.0f);
        auto kIn = kWeighting (rate), kOut = kWeighting (rate);
        double sIn = 0, sOut = 0;
        for (int st = 0; st < n; st += bs)
        {
            const int len = juce::jmin (bs, n - st);
            juce::AudioBuffer<float> b (1, len);
            b.copyFrom (0, 0, x, 0, st, len);
            e.process (b);
            for (int i = 0; i < len; ++i)
            {
                double a = x.getSample (0, st + i), o = b.getSample (0, i);
                for (auto& f : kIn) a = f.tick (a, 0);
                for (auto& f : kOut) o = f.tick (o, 0);
                if (st >= n / 2) { sIn += a * a; sOut += o * o; }
            }
        }
        return juce::jlimit (-30.0, 30.0, 10.0 * std::log10 ((sIn + 1e-20) / (sOut + 1e-20)));
    }

    // ITU-R BS.1770 K-weighting (pre-filter shelf + RLB high-pass), as analog shapes
    static std::vector<Biquad> kWeighting (double rate)
    {
        return toBiquads ({ mx::secHighShelf (2 * PI * 1681.97, 4.0, 0.7071), mx::secHP2 (2 * PI * 38.13, 0.5003) }, rate);
    }

    std::atomic<float> inPeak { 0 }, outPeak { 0 }, compGrDb { 0 }, autoGainDb { 0 }, matchDb { 0 };
    Params p;

private:
    double sr = 44100;
    int chans = 2, block = 512, firSize = 4096;
    std::unique_ptr<juce::dsp::Oversampling<float>> osLive, osHQ;
    std::unique_ptr<juce::dsp::Convolution> conv;
    std::vector<Biquad> lin, post;
    bool dirty = true, dynDirty = true, firReady = false;
    juce::String lastFirKey;
    juce::SmoothedValue<float> smoothIn, smoothOut;
    struct PreState { double lp = 0; };
    PreState pre[2];
    AAStage aaMic[2], aaPre[2];
    // v2 dynamics (MIRRORWALL 2.0 stages)
    mwl::Opto opto; mwl::Fet fet; mwl::Vca vca; mwl::Leveller lev;
    mwl::DeEsser deess[2];
    double compGain = 1, compMakeup = 1, levMakeup = 1;
    double riderA = 0, riderP = 0, autoDb = 0;

    juce::String firKeyFor (const Params& q) const
    {
        return juce::String (q.source) + "|" + juce::String (q.mic) + "|" + juce::String (q.pattern) + "|"
               + juce::String (q.distanceCm, 2) + "|" + juce::String (q.hpfHz, 1) + "|" + juce::String ((int) q.lowCut)
               + juce::String ((int) q.presence) + "|" + juce::String (sr);
    }

    static void runFilters (std::vector<Biquad>& v, juce::AudioBuffer<float>& buf, int nc, int n)
    {
        for (int c = 0; c < nc; ++c)
        {
            float* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                double x = d[i];
                for (auto& bq : v) x = bq.tick (x, c);
                d[i] = (float) x;
            }
        }
    }

    void rebuildFilters()
    {
        // keep filter state when only coefficients change
        auto keepState = [] (std::vector<Biquad>& oldV, std::vector<Biquad> newV)
        {
            if (oldV.size() == newV.size())
                for (size_t i = 0; i < newV.size(); ++i)
                    for (int c = 0; c < 2; ++c) { newV[i].z1[c] = oldV[i].z1[c]; newV[i].z2[c] = oldV[i].z2[c]; }
            oldV = std::move (newV);
        };
        keepState (lin, toBiquads (linearAnalogFor (p), sr));
        keepState (post, toBiquads (postAnalogFor (p), sr));
        dirty = false;
    }

    // Comp knob 0..10 -> threshold. Offsets measured so that Comp 5 gives ~4-5 dB of gain reduction on a
    // -18 dBFS sung line for every type (see CATALOGUE/14_DREAMMIC.md, "Tracking comp" test).
    void rebuildDynamics()
    {
        auto obj = [] (std::initializer_list<std::pair<const char*, juce::var>> kv)
        {
            auto* o = new juce::DynamicObject();
            for (auto& [k, v] : kv) o->setProperty (k, v);
            return juce::var (o);
        };
        const double a = juce::jlimit (0.0, 10.0, p.compAmt);
        const double optoThr = -6.0 - 2.6 * a, fetThr = -8.0 - 2.6 * a;   // peak-detecting types
        const double vcaThr = -5.0 - 2.6 * a, levThr = -7.0 - 2.6 * a;    // RMS-detecting types (SSL G VCA, RVox)
        opto.build (obj ({ { "thr", optoThr }, { "ratio", 4.0 } }), sr);
        fet.build (obj ({ { "thr", fetThr }, { "ratio", 4.0 }, { "attack", 0.5 }, { "release", 90.0 } }), sr);
        vca.build (obj ({ { "thr", vcaThr + 15.0 }, { "ratio", 4.0 } }), sr);          // panel value: real curve sits 15 dB lower
        lev.build (obj ({ { "comp", levThr + 12.0 } }), sr);                         // panel value: real curve sits 12 dB lower
        // static make-up ~ the gain computer's reduction on a -18 dBFS line (70 %), RVox makes up by itself
        const double peakLvl = AUTO_REF_DB + 9.0;
        double mu = 0;
        switch (p.comp)
        {
            case 1: mu = mwl::grDb (peakLvl - optoThr, 4.0, 8.0); break;
            case 2: mu = mwl::grDb (peakLvl - fetThr, 6.0, 4.0); break;
            case 3: mu = mwl::grDb (AUTO_REF_DB - vcaThr, 4.0, 45.0); break;
            default: mu = 0; break;
        }
        compMakeup = mwl::dbToLin (0.7 * mu);
        levMakeup = mwl::dbToLin (-(levThr + 12.0) * (1.0 - 1.0 / 6.0) * 0.85);   // = Leveller's own make-up
        compGain = 1;
        for (auto& d : deess) d.build (obj ({ { "thr", 4.0 + 1.2 * juce::jlimit (0.0, 10.0, p.deess) }, { "depth", 1.0 },
                                             { "freq", 4200.0 } }), sr);
        dynDirty = false;
    }

    void saturate (juce::dsp::AudioBlock<float>& b, double osr)
    {
        const auto& pm = PRES[juce::jlimit (0, NUM_PRES - 1, p.pre)];
        const auto& mm = MICS[juce::jlimit (0, NUM_MICS - 1, p.mic)];
        // Warmth 0..10: from nearly clean (-12 dB into the stage) to the preamp's full drive
        const double driveDb = -12.0 + (p.warmth / 10.0) * (pm.maxDriveDb + 6.0);
        const double d = std::pow (10.0, driveDb / 20.0);
        const double bias = pm.bias, tb = std::tanh (bias), kTube = 1.0 / (1.0 - tb * tb);
        const double lpA = std::exp (-2 * PI * 120.0 / osr);
        const double xg = d * (0.5 + 2.0 * pm.xfmr);
        const double mt = mm.tube, md = std::pow (10.0, (-6.0 + 6.0 * mt) / 20.0);
        const double mb = 0.15, mtb = std::tanh (mb), mk = 1.0 / (1.0 - mtb * mtb);
        const double knee = pm.knee;
        // mic's own tube/transformer colour (fixed, gentle)
        auto micFn = [=] (double v) { return (1 - mt) * v + mt * (std::tanh (md * v + mb) - mtb) / md * mk; };
        // preamp gain stage, normalised to unity small-signal gain
        auto preFn = [=] (double v)
        {
            const double u = d * v;
            if (bias > 0.1) return (std::tanh (u + bias) - tb) * kTube / d;               // tube: even harmonics
            const double soft = std::tanh (u + bias) - tb;
            const double hard = u / std::pow (1.0 + std::pow (std::abs (u), 4.0), 0.25);
            return ((1 - knee) * soft + knee * hard) / d;                                 // solid state: odd, firmer knee
        };
        for (size_t c = 0; c < b.getNumChannels(); ++c)
        {
            float* x = b.getChannelPointer (c);
            auto& st = pre[c];
            auto& am = aaMic[c];
            auto& ap = aaPre[c];
            for (size_t i = 0; i < b.getNumSamples(); ++i)
            {
                double v = x[i];
                if (mt > 0) v = am.p (v, micFn);
                // input transformer: lows saturate first
                st.lp = lpA * st.lp + (1 - lpA) * v;
                if (pm.xfmr > 0) v = v - st.lp + std::tanh (st.lp * xg) / xg;
                x[i] = (float) ap.p (v, preFn);
            }
        }
    }
};
} // namespace dm
