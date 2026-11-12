#pragma once
// MEX-EQ real-time core: up to 36 bands, per-band stereo placement, dynamic EQ, noise-free Drive.
// The same core runs at 1x (LIVE / linear-phase dynamics) or 2x (HQ Natural) sample rate.
#include "Design.h"
#include <atomic>
#include <cstring>

namespace mx
{
constexpr int NBANDS = 36;
constexpr int MAXSEC = 18;
constexpr int SUB = 32;          // coefficient update period (samples)

enum class Place { Stereo = 0, Left, Right, Mid, Side };

struct BandParams
{
    bool used = false;
    BandSpec spec;
    Place place = Place::Stereo;
    bool dyn = false, autoThr = true, extSC = false, learned = false, bypass = false;
    float range = -6.0f, thrDb = -24.0f, autoOffset = 4.0f, attackMs = 10.0f, releaseMs = 120.0f;
    bool active() const { return used && ! bypass && spec.type != Type::Disabled; }
};

struct GlobalParams
{
    float driveAmt = 0.0f;     // 0..10
    float outDb = 0.0f;
    float strength = 1.0f;     // scales learned bands' dynamic range
};

// ---- one band at run time
struct BandRT
{
    BandSpec cur;              // smoothed spec actually used
    bool initd = false;
    std::vector<ASec> asecs; std::vector<BQ> bq;
    int nsec = 0;
    BQ c[MAXSEC];
    double z1[MAXSEC][2] = {}, z2[MAXSEC][2] = {};
    // dynamics
    BQ det; double dz1[2] = {}, dz2[2] = {};
    double env = 0.0;          // mean-square envelope
    double slowDb = -60.0;     // auto threshold tracker
    double dynOffset = 0.0, lastDesignedOffset = 1e9;
    std::atomic<float> uiOffset { 0.0f };

    void resetState() { std::memset (z1, 0, sizeof (z1)); std::memset (z2, 0, sizeof (z2)); dz1[0] = dz1[1] = dz2[0] = dz2[1] = 0; env = 0; }
};

class Core
{
public:
    void prepare (double rate)
    {
        sr = rate;
        for (auto& b : rt) { b.initd = false; b.resetState(); b.asecs.reserve (MAXSEC); b.bq.reserve (MAXSEC); }
        smoothA = 1.0 - std::exp (-(double) SUB / (0.02 * sr));       // 20 ms parameter glide
        driveX1[0] = driveX1[1] = 0;
    }

    // staticOnly: process only non-dynamic bands; dynOnly: only dynamic bands (linear-phase modes)
    void process (float* const* ch, int nch, int n, const BandParams* bp, const GlobalParams& gp,
                  const float* const* sc, bool skipStatic)
    {
        for (int start = 0; start < n; start += SUB)
        {
            const int len = std::min (SUB, n - start);
            for (int i = 0; i < NBANDS; ++i)
            {
                const auto& p = bp[i];
                auto& r = rt[i];
                if (! p.active()) { if (r.initd) { r.initd = false; r.resetState(); r.uiOffset.store (0); } continue; }
                if (skipStatic && ! p.dyn) continue;
                updateBand (r, p, gp, ch, sc, nch, start, len);
                runBand (r, p, ch, nch, start, len);
            }
        }
        applyDriveAndGain (ch, nch, n, gp);
    }

    BandRT rt[NBANDS];
    double sr = 44100.0;

private:
    double smoothA = 0.1;
    double driveX1[2] = {};

    static double lnorm (double x) { return std::log (std::max (x, 1e-9)); }

    void design (BandRT& r, const BandSpec& s)
    {
        designBand (s, r.asecs);
        toDigital (r.asecs, sr, r.bq);
        const int n = std::min ((int) r.bq.size(), MAXSEC);
        if (n != r.nsec) { std::memset (r.z1, 0, sizeof (r.z1)); std::memset (r.z2, 0, sizeof (r.z2)); }
        r.nsec = n;
        for (int k = 0; k < n; ++k) r.c[k] = r.bq[(size_t) k];
    }

    void updateBand (BandRT& r, const BandParams& p, const GlobalParams& gp, float* const* ch, const float* const* sc,
                     int nch, int start, int len)
    {
        BandSpec tgt = p.spec;
        tgt.freq = std::clamp (tgt.freq, 10.0, 0.49 * sr > 30000 ? 30000.0 : 0.49 * sr);
        bool changed = false;
        if (! r.initd)
        {
            r.cur = tgt; r.initd = true; changed = true; r.resetState(); r.slowDb = -40.0;
            r.det = BQ(); r.lastDesignedOffset = 1e9;
        }
        else
        {
            // glide frequency (log), gain, Q (log); discrete fields switch immediately
            const bool discrete = r.cur.type != tgt.type || r.cur.order != tgt.order || r.cur.gChar != tgt.gChar || r.cur.shape != tgt.shape;
            if (discrete) { r.cur = tgt; changed = true; }
            else
            {
                const double lf = lnorm (r.cur.freq), lq = lnorm (r.cur.q);
                const double nf = lf + (lnorm (tgt.freq) - lf) * smoothA, nq = lq + (lnorm (tgt.q) - lq) * smoothA;
                const double ng = r.cur.gainDb + (tgt.gainDb - r.cur.gainDb) * smoothA;
                if (std::abs (nf - lf) > 1e-5 || std::abs (nq - lq) > 1e-5 || std::abs (ng - r.cur.gainDb) > 1e-4)
                {
                    r.cur.freq = std::abs (nf - lnorm (tgt.freq)) < 1e-5 ? tgt.freq : std::exp (nf);
                    r.cur.q = std::abs (nq - lnorm (tgt.q)) < 1e-5 ? tgt.q : std::exp (nq);
                    r.cur.gainDb = std::abs (ng - tgt.gainDb) < 1e-4 ? tgt.gainDb : ng;
                    changed = true;
                }
            }
        }
        // ---- dynamics
        double offset = 0.0;
        if (p.dyn)
        {
            if (changed || r.det.b0 == 1.0)
            {   // detector: constant-peak band-pass at the band frequency
                const double w0 = 2 * PI * std::min (r.cur.freq, 0.45 * sr) / sr, q = std::clamp (r.cur.q, 0.3, 20.0);
                const double al = std::sin (w0) / (2 * q), a0 = 1 + al;
                r.det.b0 = al / a0; r.det.b1 = 0; r.det.b2 = -al / a0; r.det.a1 = -2 * std::cos (w0) / a0; r.det.a2 = (1 - al) / a0;
            }
            const float* const* key = (p.extSC && sc != nullptr) ? sc : (const float* const*) ch;
            const double att = std::exp (-1.0 / (std::max (0.1f, p.attackMs) * 0.001 * sr));
            const double rel = std::exp (-1.0 / (std::max (5.0f, p.releaseMs) * 0.001 * sr));
            for (int i = start; i < start + len; ++i)
            {
                double m = 0;
                for (int c = 0; c < std::min (nch, 2); ++c)
                {
                    const double x = key[c][i];
                    const double y = r.det.b0 * x + r.dz1[c];
                    r.dz1[c] = r.det.b1 * x - r.det.a1 * y + r.dz2[c];
                    r.dz2[c] = r.det.b2 * x - r.det.a2 * y;
                    m = std::max (m, y * y);
                }
                const double co = m > r.env ? att : rel;
                r.env = co * r.env + (1 - co) * m;
            }
            const double lev = 10 * std::log10 (r.env * 2.0 + 1e-12);     // sine-referenced level (dBFS)
            // auto threshold: slow average of the band level + offset, silence ignored
            if (lev > -70) r.slowDb += (lev - r.slowDb) * (1 - std::exp (-(double) len / (2.5 * sr)));
            const double thr = p.autoThr ? r.slowDb + p.autoOffset : p.thrDb;
            const double range = p.range * (p.learned ? gp.strength : 1.0f);
            const double over = std::clamp ((lev - thr) / 10.0, 0.0, 1.0);   // 10 dB transfer zone
            offset = range * over;
        }
        r.dynOffset = offset;
        r.uiOffset.store ((float) offset);
        if (changed || std::abs (offset - r.lastDesignedOffset) > 0.02)
        {
            BandSpec s = r.cur; s.gainDb += offset;
            design (r, s);
            r.lastDesignedOffset = offset;
        }
    }

    void runBand (BandRT& r, const BandParams& p, float* const* ch, int nch, int start, int len)
    {
        auto filt = [&] (double x, int c)
        {
            for (int k = 0; k < r.nsec; ++k)
            {
                const auto& q = r.c[k];
                const double y = q.b0 * x + r.z1[k][c];
                r.z1[k][c] = q.b1 * x - q.a1 * y + r.z2[k][c];
                r.z2[k][c] = q.b2 * x - q.a2 * y;
                x = y;
            }
            return x;
        };
        const bool stereo = nch >= 2;
        switch (stereo ? p.place : Place::Stereo)
        {
            case Place::Stereo:
                for (int c = 0; c < nch; ++c) for (int i = start; i < start + len; ++i) ch[c][i] = (float) filt (ch[c][i], c);
                break;
            case Place::Left:  for (int i = start; i < start + len; ++i) ch[0][i] = (float) filt (ch[0][i], 0); break;
            case Place::Right: for (int i = start; i < start + len; ++i) ch[1][i] = (float) filt (ch[1][i], 1); break;
            case Place::Mid:
            case Place::Side:
                for (int i = start; i < start + len; ++i)
                {
                    double m = 0.5 * (ch[0][i] + ch[1][i]), s = 0.5 * (ch[0][i] - ch[1][i]);
                    if (p.place == Place::Mid) m = filt (m, 0); else s = filt (s, 0);
                    ch[0][i] = (float) (m + s); ch[1][i] = (float) (m - s);
                }
                break;
        }
    }

    // Drive: console-style soft saturation, mostly odd harmonics with a touch of 2nd. First-order
    // anti-aliasing (ADAA). Adds no noise: silence in = silence out. Output level-compensated.
    void applyDriveAndGain (float* const* ch, int nch, int n, const GlobalParams& gp)
    {
        const double outG = std::pow (10.0, gp.outDb / 20.0);
        if (gp.driveAmt <= 0.001f)
        {
            if (std::abs (outG - 1.0) > 1e-9) for (int c = 0; c < nch; ++c) for (int i = 0; i < n; ++i) ch[c][i] *= (float) outG;
            return;
        }
        const double d = gp.driveAmt / 10.0;
        const double pre = std::pow (10.0, (d * 18.0) / 20.0) * 0.5, b = 0.12 * d;
        const double sech2 = 1.0 / std::pow (std::cosh (b), 2);
        const double post = outG / (pre * sech2), tb = std::tanh (b);
        auto F = [&] (double x) { const double u = x + b; return std::log (std::cosh (u)) - tb * x; };   // antiderivative
        auto f = [&] (double x) { return std::tanh (x + b) - tb; };
        for (int c = 0; c < std::min (nch, 2); ++c)
        {
            double x1 = driveX1[c];
            for (int i = 0; i < n; ++i)
            {
                const double x = ch[c][i] * pre;
                const double dx = x - x1;
                const double y = std::abs (dx) > 1e-5 ? (F (x) - F (x1)) / dx : f (0.5 * (x + x1));
                x1 = x;
                ch[c][i] = (float) (y * post);
            }
            driveX1[c] = x1;
        }
    }
};
} // namespace mx
