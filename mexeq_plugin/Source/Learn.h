#pragma once
// LEARN: listens to the input, finds problem frequencies and proposes dynamic cut bands.
//  - ringing / resonant peaks: frequencies that stick out of the long-term spectrum
//  - harsh / spiky build-ups: frequencies whose level jumps far above their normal level (esses, harshness)
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <vector>

namespace mx
{
struct LearnResult { double freq, q, rangeDb, score; bool dynamicKind; };

class Learner
{
public:
    static constexpr int ORDER = 12, N = 1 << ORDER, HOP = N / 2;
    static constexpr double FLO = 40.0, FHI = 18000.0;
    static constexpr int BPO = 24;     // bands per octave

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ring.assign (1 << 18, 0.0f);
        nb = (int) std::floor (BPO * std::log2 (FHI / FLO));
        frames.clear(); frames.reserve (2000);
        window.resize (N);
        for (int i = 0; i < N; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (N - 1));
        reset();
    }
    void reset() { wpos.store (0); rpos = 0; frames.clear(); collectedSec = 0; }

    // audio thread
    void push (const float* l, const float* r, int n)
    {
        if (! learning.load()) return;
        auto w = wpos.load (std::memory_order_relaxed);
        const size_t mask = ring.size() - 1;
        for (int i = 0; i < n; ++i) ring[(w + (size_t) i) & mask] = r ? 0.5f * (l[i] + r[i]) : l[i];
        wpos.store (w + (size_t) n, std::memory_order_release);
    }

    // background / message thread: consume available audio into spectral frames
    void pump()
    {
        const auto w = wpos.load (std::memory_order_acquire);
        const size_t mask = ring.size() - 1;
        if (w - rpos > ring.size()) rpos = w - ring.size() + N;          // overrun: skip
        std::vector<float> buf ((size_t) (2 * N));
        while (w - rpos >= (size_t) N)
        {
            double ms = 0;
            for (int i = 0; i < N; ++i) { const float x = ring[(rpos + (size_t) i) & mask]; ms += (double) x * x; buf[(size_t) i] = x * window[(size_t) i]; }
            std::fill (buf.begin() + N, buf.end(), 0.0f);
            if (10.0 * std::log10 (ms / N + 1e-20) > -60.0)        // skip near-silent frames
            {
                fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                addFrame (buf);
            }
            rpos += HOP;
            collectedSec += HOP / sr;
        }
    }

    double secondsCollected() const { return collectedSec; }
    int usableFrames() const { return (int) frames.size(); }

    std::vector<LearnResult> analyse (int maxBands = 6) const
    {
        std::vector<LearnResult> out;
        const int F = (int) frames.size();
        if (F < 20) return out;
        std::vector<double> mean ((size_t) nb, 0), p90 ((size_t) nb), med ((size_t) nb);
        std::vector<float> col ((size_t) F);
        for (int b = 0; b < nb; ++b)
        {
            double s = 0;
            for (int f = 0; f < F; ++f) { col[(size_t) f] = frames[(size_t) f][(size_t) b]; s += col[(size_t) f]; }
            mean[(size_t) b] = s / F;
            std::sort (col.begin(), col.end());
            med[(size_t) b] = col[(size_t) (F / 2)];
            p90[(size_t) b] = col[(size_t) std::min (F - 1, (int) (0.9 * F))];
        }
        auto smooth = [&] (const std::vector<double>& x, int half)
        {
            std::vector<double> y (x.size());
            for (int i = 0; i < nb; ++i)
            {
                double s = 0; int c = 0;
                for (int j = std::max (0, i - half); j <= std::min (nb - 1, i + half); ++j) { s += x[(size_t) j]; ++c; }
                y[(size_t) i] = s / c;
            }
            return y;
        };
        std::vector<double> spread ((size_t) nb);
        for (int b = 0; b < nb; ++b) spread[(size_t) b] = p90[(size_t) b] - med[(size_t) b];
        const auto ms = smooth (mean, BPO / 2);
        // reference for "jumps above its normal level": the typical spread over the whole spectrum
        std::vector<double> sp2;
        for (int b = 0; b < nb; ++b) if (bandFreq (b) > 150 && bandFreq (b) < 16000) sp2.push_back (spread[(size_t) b]);
        std::nth_element (sp2.begin(), sp2.begin() + (long) sp2.size() / 2, sp2.end());
        const double spreadRef = sp2.empty() ? 0.0 : sp2[sp2.size() / 2];
        std::vector<double> score ((size_t) nb);
        std::vector<char> dynKind ((size_t) nb, 0);
        for (int b = 0; b < nb; ++b)
        {
            const double f = bandFreq (b);
            const double es = mean[(size_t) b] - ms[(size_t) b];
            const double ed = spread[(size_t) b] - spreadRef;
            double sc = std::max (es, 0.8 * ed);
            dynKind[(size_t) b] = (0.8 * ed > es) ? 1 : 0;
            if (f < 100) sc *= 0.6; else if (f > 12000) sc *= 0.8;
            score[(size_t) b] = sc;
        }
        // peak picking
        std::vector<int> idx;
        for (int b = 1; b < nb - 1; ++b)
            if (score[(size_t) b] > 2.5 && score[(size_t) b] >= score[(size_t) (b - 1)] && score[(size_t) b] >= score[(size_t) (b + 1)]) idx.push_back (b);
        std::sort (idx.begin(), idx.end(), [&] (int a, int b) { return score[(size_t) a] > score[(size_t) b]; });
        std::vector<int> picked;
        for (int b : idx)
        {
            bool ok = true;
            for (int p : picked) if (std::abs (p - b) < BPO / 3) ok = false;
            if (ok) picked.push_back (b);
            if ((int) picked.size() >= maxBands) break;
        }
        for (int b : picked)
        {
            const double pk = score[(size_t) b];
            int l = b, r = b;
            while (l > 0 && score[(size_t) (l - 1)] > pk / 2) --l;
            while (r < nb - 1 && score[(size_t) (r + 1)] > pk / 2) ++r;
            const double oct = std::max (1.0, (double) (r - l + 1)) / BPO;
            const double p2 = std::pow (2.0, oct);
            const double q = std::clamp (std::sqrt (p2) / (p2 - 1), 1.5, 16.0);
            out.push_back ({ bandFreq (b), q, -std::clamp (1.2 * pk, 3.0, 12.0), pk, dynKind[(size_t) b] != 0 });
        }
        return out;
    }

    std::atomic<bool> learning { false };

private:
    double sr = 44100;
    int nb = 200;
    std::vector<float> ring, window;
    std::atomic<size_t> wpos { 0 };
    size_t rpos = 0;
    double collectedSec = 0;
    std::vector<std::vector<float>> frames;
    juce::dsp::FFT fft { ORDER };

    double bandFreq (int b) const { return FLO * std::pow (2.0, (b + 0.5) / BPO); }

    void addFrame (const std::vector<float>& mag)
    {
        std::vector<float> fr ((size_t) nb);
        for (int b = 0; b < nb; ++b)
        {
            const double f0 = FLO * std::pow (2.0, (double) b / BPO), f1 = FLO * std::pow (2.0, (b + 1.0) / BPO);
            int k0 = (int) std::floor (f0 * N / sr), k1 = std::max (k0 + 1, (int) std::ceil (f1 * N / sr));
            k1 = std::min (k1, N / 2);
            double p = 0;
            for (int k = k0; k < k1; ++k) p += (double) mag[(size_t) k] * mag[(size_t) k];
            p /= std::max (1, k1 - k0);
            fr[(size_t) b] = (float) (10.0 * std::log10 (p + 1e-20));
        }
        frames.push_back (std::move (fr));
    }
};
} // namespace mx
