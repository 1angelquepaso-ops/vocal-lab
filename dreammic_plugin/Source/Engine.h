#pragma once
// DREAMMIC real-time engine.
// LIVE: minimum-phase filters + 2x oversampled preamp (a few samples of latency).
// HQ:   the whole mic/proximity/high-pass curve as one linear-phase FIR + 8x
//       oversampled preamp (latency reported to the host, which compensates).
#include <juce_dsp/juce_dsp.h>
#include "Models.h"
#include <atomic>
#include <cmath>
#include <complex>

namespace dm
{
constexpr double PI = 3.14159265358979323846;

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
    // complex response at frequency f
    std::complex<double> response (double f, double sr) const
    {
        const auto z = std::polar (1.0, -2.0 * PI * f / sr);
        return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
    }
};

inline Biquad design (FType t, double f, double q, double gDb, double sr)
{
    Biquad c;
    const double A = std::pow (10.0, gDb / 40.0);
    const double w0 = 2.0 * PI * std::min (f, sr * 0.49) / sr;
    const double cw = std::cos (w0), sw = std::sin (w0), al = sw / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;
    switch (t)
    {
        case FType::Peak:
            b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case FType::LowShelf:
        {
            const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sa); a0 = (A + 1) + (A - 1) * cw + sa;
            a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; break;
        }
        case FType::HighShelf:
        {
            const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sa); a0 = (A + 1) - (A - 1) * cw + sa;
            a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; break;
        }
        case FType::HP:
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        case FType::LP:
        default:
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    }
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

// First-order section H(s) = (s + wz) / (s + wp), bilinear. Exact relative proximity.
inline Biquad firstOrderShelf (double wz, double wp, double sr)
{
    Biquad c;
    const double K = 2.0 * sr;
    c.b0 = (K + wz) / (K + wp);
    c.b1 = (wz - K) / (K + wp);
    c.b2 = 0;
    c.a1 = (wp - K) / (K + wp);
    c.a2 = 0;
    return c;
}

struct Params
{
    int source = 1, mic = 0, pattern = 0, pre = 0, mode = 0;   // pattern 0 = native
    double distanceCm = 15, warmth = 3, hpfHz = 20, airDb = 0, inDb = 0, outDb = 0;
};

static const double PATTERN_A[] = { -1.0, 0.5, 0.63, 0.0, 1.0 };   // native, cardioid, super, omni, fig-8
static const char* PATTERN_NAMES[] = { "Native", "Cardioid", "Supercardioid", "Omni", "Figure-8" };

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
        dirty = true;
        for (auto& s : pre) s = {};
        for (auto& s : micTube) s = {};
        smoothIn.reset (sr, 0.05); smoothOut.reset (sr, 0.05);
        smoothIn.setCurrentAndTargetValue (1.0f); smoothOut.setCurrentAndTargetValue (1.0f);
    }

    void setParams (const Params& np)
    {
        const bool linChange = np.source != p.source || np.mic != p.mic || np.pattern != p.pattern
                               || std::abs (np.distanceCm - p.distanceCm) > 1e-6 || std::abs (np.hpfHz - p.hpfHz) > 1e-6;
        const bool postChange = np.pre != p.pre || std::abs (np.airDb - p.airDb) > 1e-6;
        if (linChange || postChange || np.mode != p.mode) dirty = true;
        p = np;
        smoothIn.setTargetValue ((float) juce::Decibels::decibelsToGain (p.inDb));
        smoothOut.setTargetValue ((float) juce::Decibels::decibelsToGain (p.outDb));
    }

    int latencyFor (int mode) const
    {
        if (mode == 1) return firSize / 2 + (int) std::round (osHQ ? osHQ->getLatencyInSamples() : 0);
        return (int) std::round (osLive ? osLive->getLatencyInSamples() : 0);
    }

    int latencySamples() const
    {
        if (p.mode == 1) return firSize / 2 + (int) std::round (osHQ ? osHQ->getLatencyInSamples() : 0);
        return (int) std::round (osLive ? osLive->getLatencyInSamples() : 0);
    }

    // The static (linear) part: source correction, target mic, proximity, high-pass.
    std::vector<Biquad> linearChain (double rate) const { return linearChainFor (p, rate); }

    static std::vector<Biquad> linearChainFor (const Params& p, double rate)
    {
        std::vector<Biquad> v;
        // undo the source mic
        if (p.source == 1)
            for (int i = 0; i < 12; ++i)
                v.push_back (design (EQ12[i].t, EQ12[i].f, EQ12[i].q, C01U_TO_MA50[i], rate));
        if (p.source == 1 || p.source == 2)
        {
            const auto& m = MICS[MJ50_INDEX];
            for (int i = 0; i < m.numFilters; ++i)
                if (m.filters[i].t != FType::HP && m.filters[i].t != FType::LP)
                    v.push_back (design (m.filters[i].t, m.filters[i].f, m.filters[i].q, -m.filters[i].g, rate));
        }
        // target mic
        const auto& m = MICS[juce::jlimit (0, NUM_MICS - 1, p.mic)];
        for (int i = 0; i < m.numFilters; ++i)
            v.push_back (design (m.filters[i].t, m.filters[i].f, m.filters[i].q, m.filters[i].g, rate));
        // proximity relative to how it was recorded (15 cm, source pattern)
        const double c = 343.0;
        const double aSrc = SOURCE_PATTERN_A[juce::jlimit (0, NUM_SOURCES - 1, p.source)];
        const double aNew = p.pattern == 0 ? aSrc : PATTERN_A[juce::jlimit (0, 4, p.pattern)];
        const double rNew = juce::jlimit (0.02, 1.0, p.distanceCm / 100.0), rRef = 0.15;
        const double floorW = 2 * PI * 5.0;
        const double wz = std::max (floorW, aNew * c / rNew), wp = std::max (floorW, aSrc * c / rRef);
        if (std::abs (wz - wp) > 1e-6)
            v.push_back (firstOrderShelf (wz, wp, rate));
        // high-pass
        if (p.hpfHz > 21)
            v.push_back (design (FType::HP, p.hpfHz, 0.707, 0, rate));
        return v;
    }

    std::vector<Biquad> postChain (double rate) const
    {
        std::vector<Biquad> v;
        const auto& pm = PRES[juce::jlimit (0, NUM_PRES - 1, p.pre)];
        for (int i = 0; i < pm.numTilt; ++i)
            v.push_back (design (pm.tilt[i].t, pm.tilt[i].f, pm.tilt[i].q, pm.tilt[i].g, rate));
        if (p.airDb > 0.05)
            v.push_back (design (FType::HighShelf, 14000, 0.5, p.airDb, rate));
        return v;
    }

    // magnitude (dB) of the whole static curve, for the display
    double curveDb (double f) const
    {
        std::complex<double> h (1, 0);
        for (auto& b : linearChain (sr)) h *= b.response (f, sr);
        for (auto& b : postChain (sr)) h *= b.response (f, sr);
        return 20 * std::log10 (std::abs (h) + 1e-12);
    }

    // call off the audio thread when HQ is on and the curve changed
    void rebuildFir (const Params& q)
    {
        const auto key = firKeyFor (q);
        if (key == lastFirKey) return;
        lastFirKey = key;
        const int N = firSize;
        const auto chain = linearChainFor (q, sr);
        juce::dsp::FFT fft ((int) std::log2 (N));
        std::vector<std::complex<float>> spec ((size_t) N), time ((size_t) N);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = (double) k * sr / N;
            std::complex<double> h (1, 0);
            for (auto& b : chain) h *= b.response (std::max (f, 1.0), sr);
            const float mag = (float) std::abs (h);
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

        // input gain + meter
        for (int i = 0; i < n; ++i)
        {
            const float g = smoothIn.getNextValue();
            for (int c = 0; c < nc; ++c) buf.getWritePointer (c)[i] *= g;
        }
        inPeak.store (std::max (inPeak.load(), buf.getMagnitude (0, n)));

        // linear part
        if (p.mode == 1)
        {
            juce::dsp::AudioBlock<float> b (buf);
            auto sub = b.getSubsetChannelBlock (0, (size_t) nc);
            conv->process (juce::dsp::ProcessContextReplacing<float> (sub));
        }
        else
        {
            for (int c = 0; c < nc; ++c)
            {
                float* d = buf.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    double x = d[i];
                    for (auto& bq : lin) x = bq.tick (x, c);
                    d[i] = (float) x;
                }
            }
        }

        // nonlinear part (mic electronics + preamp), oversampled
        auto& os = p.mode == 1 ? *osHQ : *osLive;
        juce::dsp::AudioBlock<float> block (buf);
        auto sub = block.getSubsetChannelBlock (0, (size_t) nc);
        auto up = os.processSamplesUp (sub);
        const double osr = sr * (double) os.getOversamplingFactor();
        saturate (up, osr);
        os.processSamplesDown (sub);

        // post: preamp tilt + air, output gain
        for (int c = 0; c < nc; ++c)
        {
            float* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                double x = d[i];
                for (auto& bq : post) x = bq.tick (x, c);
                d[i] = (float) x;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const float g = smoothOut.getNextValue();
            for (int c = 0; c < nc; ++c) buf.getWritePointer (c)[i] *= g;
        }
        if (buf.getNumChannels() > nc)
            for (int c = nc; c < buf.getNumChannels(); ++c) buf.copyFrom (c, 0, buf, 0, 0, n);
        outPeak.store (std::max (outPeak.load(), buf.getMagnitude (0, n)));
    }

    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    Params p;

private:
    double sr = 44100;
    int chans = 2, block = 512, firSize = 4096;
    std::unique_ptr<juce::dsp::Oversampling<float>> osLive, osHQ;
    std::unique_ptr<juce::dsp::Convolution> conv;
    std::vector<Biquad> lin, post;
    bool dirty = true, firReady = false;
    juce::String lastFirKey;
    juce::SmoothedValue<float> smoothIn, smoothOut;
    struct PreState { double lp = 0; };
    PreState pre[2], micTube[2];

    juce::String firKeyFor (const Params& p) const
    {
        return juce::String (p.source) + "|" + juce::String (p.mic) + "|" + juce::String (p.pattern) + "|"
               + juce::String (p.distanceCm, 2) + "|" + juce::String (p.hpfHz, 1) + "|" + juce::String (sr);
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
        keepState (lin, linearChain (sr));
        keepState (post, postChain (sr));
        dirty = false;
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
        for (size_t c = 0; c < b.getNumChannels(); ++c)
        {
            float* x = b.getChannelPointer (c);
            auto& st = pre[c];
            for (size_t i = 0; i < b.getNumSamples(); ++i)
            {
                double v = x[i];
                // mic's own tube/transformer colour (fixed, gentle)
                if (mt > 0)
                {
                    const double z = (std::tanh (md * v + mb) - mtb) / md * mk;
                    v = (1 - mt) * v + mt * z;
                }
                // input transformer: lows saturate first
                st.lp = lpA * st.lp + (1 - lpA) * v;
                if (pm.xfmr > 0)
                    v = v - st.lp + std::tanh (st.lp * xg) / xg;
                // gain stage
                const double u = d * v;
                double y;
                if (bias > 0.1)
                    y = (std::tanh (u + bias) - tb) * kTube;                       // tube: even harmonics
                else
                {
                    const double soft = std::tanh (u + bias) - tb;
                    const double hard = u / std::pow (1.0 + std::pow (std::abs (u), 4.0), 0.25);
                    y = (1 - pm.knee) * soft + pm.knee * hard;                     // solid state: odd, firmer knee
                }
                x[i] = (float) (y / d);
            }
        }
    }
};
} // namespace dm
