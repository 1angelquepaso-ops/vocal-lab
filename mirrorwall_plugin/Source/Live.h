#pragma once
// MIRRORWALL 2.0 real-time engine.
// One stateful chain used for BOTH modes: LIVE (FL audio, block by block) and HQ (whole file, offline).
// Every stage keeps its own state, works at any sample rate and any block size.
//
// What changed vs the 1.x offline chain (Chain.h, kept only for the parity tool):
//   * input level: Input knob + Auto rider instead of whole-file trim
//   * EQ: MEX-EQ matched-magnitude design (no cramping near Nyquist, any sample rate)
//   * preamp: anti-aliased saturation (only the distortion part is band-limited, so the clean signal is untouched)
//   * compressors fitted to clean captures (Nov 2026):
//       opto  = CLA-2A (LA-2A): 10 ms attack, two-stage program release, soft knee
//       fet   = CLA-76 (1176): sub-ms attack, hard-ish knee, ratios bite harder than labelled
//       vca   = SSL G channel: very wide soft knee (~30 dB), auto 30 ms / fast 3 ms attack
//       lev   = RVox: leveller with automatic make-up, optional downward expander
//   * de-esser listens 4.2-10 kHz (male S 4.28-5.6 kHz, female S 4.8-6.6 kHz, plus bright t/s up to 10 kHz)
//   * reverb: room FDN or plate (diffused, steel-plate high-frequency damping from the Abbey Road Plates capture)
//   * output: fixed make-up per preset, calibrated once when the preset loads (no whole-file normalising)
//   * limiter: 2 ms look-ahead, reported as latency
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include "Design.h"

namespace mwl
{
constexpr double PI = 3.14159265358979323846;
constexpr double IN_REF_DB = -20.0;

inline double num (const juce::var& v, const char* k, double d) { return v.hasProperty (k) ? (double) v[k] : d; }
inline bool on (const juce::var& v) { return v.isObject() && (! v.hasProperty ("on") || (bool) v["on"]); }
inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
inline double linToDb (double x) { return 20.0 * std::log10 (x + 1e-12); }
inline double coef (double ms, double sr) { return ms <= 0 ? 0.0 : std::exp (-1.0 / (sr * ms / 1000.0)); }

struct Macros
{
    double comp = 1.0, reverb = 1.0, delay = 1.0, fx = 1.0, deess = 1.0, drive = 0.0, out = 0.0, gate = 1.0;
    double tape = 0.0, air = 0.0;      // 2.1 colour knobs, 0 = off
    bool tape30 = false;               // tape speed: false = 15 ips (more bump/HF loss), true = 30 ips
};

// ------------------------------------------------------------------ biquad (TDF-II)
struct Biq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set (const mx::BQ& q) { b0 = q.b0; b1 = q.b1; b2 = q.b2; a1 = q.a1; a2 = q.a2; }
    inline double p (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

inline void addBand (std::vector<Biq>& out, mx::Type t, double f, double q, double g, double sr)
{
    mx::BandSpec b;
    b.type = t; b.order = mx::Order::Two; b.freq = std::min (f, 0.45 * sr); b.q = q; b.gainDb = g;
    std::vector<mx::ASec> secs; mx::designBand (b, secs);
    std::vector<mx::BQ> d; mx::toDigital (secs, sr, d);
    for (auto& s : d) { Biq x; x.set (s); out.push_back (x); }
}

// ------------------------------------------------------------------ EQ (same bands as the fitter)
struct Eq
{
    std::vector<Biq> s;
    void build (const juce::var& eq, double sr)
    {
        s.clear();
        if (! eq.isObject()) return;
        static const struct { mx::Type t; double f, q; } BANDS[] = {
            { mx::Type::LowShelf, 100.0, 0.707 }, { mx::Type::Peak, 160.0, 1.4 }, { mx::Type::Peak, 250.0, 1.4 },
            { mx::Type::Peak, 400.0, 1.4 }, { mx::Type::Peak, 630.0, 1.4 }, { mx::Type::Peak, 1000.0, 1.4 },
            { mx::Type::Peak, 1600.0, 1.4 }, { mx::Type::Peak, 2500.0, 1.4 }, { mx::Type::Peak, 4000.0, 1.4 },
            { mx::Type::Peak, 6300.0, 1.4 }, { mx::Type::Peak, 9000.0, 1.4 }, { mx::Type::HighShelf, 12500.0, 0.707 } };
        const double hpf = num (eq, "hpf", 0.0);
        const int order = (int) num (eq, "hpf_order", 2.0);
        if (hpf > 20) for (int i = 0; i < order; ++i) addBand (s, mx::Type::HighPass, hpf, 0.707, 0, sr);
        if (auto* g = eq["gains"].getArray())
            for (int i = 0; i < juce::jmin (12, g->size()); ++i)
                if ((double) (*g)[i] != 0.0) addBand (s, BANDS[i].t, BANDS[i].f, BANDS[i].q, (double) (*g)[i], sr);
        const double lpf = num (eq, "lpf", 0.0);
        if (lpf > 0 && lpf < 20000) addBand (s, mx::Type::LowPass, lpf, 0.707, 0, sr);
        if (auto* d = eq["detail"].getArray())
            for (int i = 0; i < juce::jmin (26, d->size()); ++i)
                if ((double) (*d)[i] != 0.0)
                    addBand (s, mx::Type::Peak, 50.0 * std::pow (2.0, i / 3.0), 4.32, (double) (*d)[i], sr);
    }
    inline double p (double x) { for (auto& b : s) x = b.p (x); return x; }
};

// ------------------------------------------------------------------ preamp (anti-aliased distortion part)
struct Preamp
{
    int type = 0;           // 0 clean, 1 tube, 2 console
    double baseDrive = 6, mix = 0.5, bias = 0.12, xp = 0;
    Biq lowShelf, highShelf;
    void build (const juce::var& pre, double sr)
    {
        const auto t = pre["type"].toString();
        type = t == "tube" ? 1 : t == "console" ? 2 : 0;
        baseDrive = num (pre, "drive", 6.0);
        mix = num (pre, "mix", 0.5);
        std::vector<Biq> v;
        addBand (v, mx::Type::LowShelf, 90.0, 0.707, 1.5, sr);
        addBand (v, mx::Type::HighShelf, 15000.0, 0.707, -1.0, sr);
        lowShelf = v[0]; highShelf = v[1];
    }
    inline double shape (double x, double d) const
    {
        if (type == 1)
        {
            const double tb = std::tanh (bias), k = 1.0 / (1.0 - tb * tb);
            return (std::tanh (d * x + bias) - tb) / d * k;
        }
        const double u = d * x;
        return std::tanh (u + 0.15 * u * u * u / (1.0 + u * u)) / d;
    }
    inline double p (double x, double extraDb)
    {
        if (type == 0) return x;
        const double d = dbToLin (baseDrive + extraDb);
        // band-limited average of the curve over the last sample step (Simpson = 1st-order ADAA),
        // minus the same average of a straight line -> only the harmonics are smoothed, the clean part stays exact
        const double mid = 0.5 * (x + xp);
        const double avg = (shape (xp, d) + 4.0 * shape (mid, d) + shape (x, d)) / 6.0;
        xp = x;
        double y = x + mix * (avg - mid);
        if (type == 2) y = highShelf.p (lowShelf.p (y));
        return y;
    }
};

// ------------------------------------------------------------------ gain computer with soft knee
inline double grDb (double overDb, double ratio, double knee)
{
    const double s = 1.0 - 1.0 / std::max (1.0, ratio);
    if (knee <= 0) return overDb > 0 ? overDb * s : 0.0;
    if (2 * overDb < -knee) return 0.0;
    if (2 * std::abs (overDb) <= knee) { const double t = overDb + knee / 2; return s * t * t / (2 * knee); }
    return overDb * s;
}

// CLA-2A / LA-2A style opto
struct Opto
{
    bool enabled = false;
    double thr = -24, ratio = 3, ea = 0, er = 0, ga = 0, rf = 0, rs = 0, ls = 0, env = 0, grs = 0, hist = 0;
    void build (const juce::var& o, double sr)
    {
        enabled = on (o);
        thr = num (o, "thr", -24); ratio = num (o, "ratio", 3);
        ea = coef (1.0, sr); er = coef (50.0, sr);
        ga = coef (num (o, "attack", 10), sr);
        rf = coef (num (o, "rel_fast", 60), sr); rs = coef (num (o, "rel_slow", 1500), sr); ls = coef (2000.0, sr);
    }
    inline double p (double v, double shift)
    {
        if (! enabled) return v;
        const double lv = std::abs (v);
        env = lv > env ? ea * env + (1 - ea) * lv : er * env + (1 - er) * lv;
        const double gr = grDb (linToDb (env) - (thr + shift), ratio, 8.0);
        hist = ls * hist + (1 - ls) * gr;
        if (gr > grs) grs = ga * grs + (1 - ga) * gr;
        else { const double r = rf + (rs - rf) * std::min (1.0, hist / 6.0); grs = r * grs + (1 - r) * gr; }
        return v * dbToLin (-grs);
    }
};

// CLA-76 / 1176 style FET
struct Fet
{
    bool enabled = false, all = false;
    double thr = -18, ratio = 6, a = 0, r = 0, env = 0, grs = 0, aOver = 0, last = 0;
    void build (const juce::var& f, double sr)
    {
        enabled = on (f);
        thr = num (f, "thr", -18); ratio = num (f, "ratio", 6);
        all = f.hasProperty ("all") && (bool) f["all"];
        a = coef (num (f, "attack", 0.4), sr); r = coef (num (f, "release", 80), sr);
        aOver = coef (all ? 15.0 : 1e9, sr);
    }
    inline double p (double v, double shift)
    {
        if (! enabled) return v;
        const double lv = std::abs (v);
        env = lv > env ? a * env + (1 - a) * lv : r * env + (1 - r) * lv;
        double gr = grDb (linToDb (env) - (thr + shift), all ? 40.0 : ratio, 4.0);
        if (all) { grs = gr > grs ? gr : aOver * grs + (1 - aOver) * gr; gr = grs + 0.15 * std::max (0.0, gr); }
        last = gr;
        return v * dbToLin (-gr);
    }
};

// SSL G-channel style VCA (RMS detector, wide knee)
struct Vca
{
    bool enabled = false;
    double thr = -20, ratio = 4, knee = 30, ed = 0, ga = 0, gr_ = 0, p_ = 0, grs = 0;
    void build (const juce::var& c, double sr)
    {
        enabled = on (c);
        // "thr" is the SSL panel value; measured: the real curve sits ~15 dB lower with a ~45 dB soft knee
        thr = num (c, "thr", -20) - 15.0; ratio = num (c, "ratio", 4); knee = num (c, "knee", 45);
        ed = coef (5.0, sr);
        ga = coef ((c.hasProperty ("fast") && (bool) c["fast"]) ? 3.0 : 30.0, sr);
        gr_ = coef (num (c, "release", 300), sr);
    }
    inline double p (double v, double shift)
    {
        if (! enabled) return v;
        p_ = ed * p_ + (1 - ed) * v * v;
        const double gr = grDb (10.0 * std::log10 (p_ + 1e-18) - (thr + shift), ratio, knee);
        const double c = gr > grs ? ga : gr_;
        grs = c * grs + (1 - c) * gr;
        return v * dbToLin (-grs);
    }
};

// RVox style leveller: compression + automatic make-up + optional gate/expander
struct Leveller
{
    bool enabled = false;
    double thr = -20, ratio = 8, knee = 12, gateDb = -90, ed = 0, ga = 0, gr_ = 0, eg = 0, p_ = 0, grs = 0, gg = 1, makeup = 1;
    void build (const juce::var& l, double sr)
    {
        enabled = on (l);
        // "comp" is the RVox panel value; measured against the Nov 2026 capture: curve ~12 dB lower, ~6:1, 24 dB knee
        thr = num (l, "comp", -20) - 12.0; gateDb = num (l, "gate", -90);
        ratio = num (l, "ratio", 6); knee = num (l, "knee", 24);
        ed = coef (5.0, sr); ga = coef (5.0, sr); gr_ = coef (250.0, sr); eg = coef (20.0, sr);
        makeup = dbToLin (-(thr + 12.0) * (1.0 - 1.0 / ratio) * 0.85);
    }
    inline double p (double v, double shift)
    {
        if (! enabled) return v;
        p_ = ed * p_ + (1 - ed) * v * v;
        const double lvl = 10.0 * std::log10 (p_ + 1e-18);
        const double gr = grDb (lvl - (thr + shift), ratio, knee);
        const double c = gr > grs ? ga : gr_;
        grs = c * grs + (1 - c) * gr;
        const double under = gateDb - lvl;               // downward expander 1:2 below the gate level
        const double tg = under > 0 ? dbToLin (-std::min (40.0, under)) : 1.0;
        gg = eg * gg + (1 - eg) * tg;
        return v * dbToLin (-grs) * makeup * gg;
    }
};

// ------------------------------------------------------------------ de-esser (4.2-10 kHz listener)
struct DeEsser
{
    bool enabled = false;
    double thr = 10, depth = 1, as = 0, ab = 0, ag = 0, ps = 0, pb = 0, g = 1;
    Biq h1, h2, l1;
    void build (const juce::var& d, double sr)
    {
        enabled = on (d);
        thr = num (d, "thr", 10); depth = num (d, "depth", 1.0);
        std::vector<Biq> v;
        const double lo = num (d, "freq", 4200.0);
        addBand (v, mx::Type::HighPass, lo, 0.707, 0, sr);
        addBand (v, mx::Type::HighPass, lo, 0.707, 0, sr);
        addBand (v, mx::Type::LowPass, std::min (10000.0, 0.45 * sr), 0.707, 0, sr);
        h1 = v[0]; h2 = v[1]; l1 = v[2];
        as = coef (4.0, sr); ab = coef (30.0, sr); ag = coef (1.0, sr);
    }
    inline double p (double x, double knob)
    {
        if (! enabled) return x;
        const double s = l1.p (h2.p (h1.p (x)));
        ps = as * ps + (1 - as) * s * s;
        pb = ab * pb + (1 - ab) * x * x;
        const double rel = 10.0 * std::log10 ((ps + 1e-18) / (pb + 1e-18));
        const double gr = juce::jlimit (0.0, 24.0, rel + thr + (knob - 1.0) * 6.0);
        g = ag * g + (1 - ag) * dbToLin (-gr * depth);
        return (x - s) + s * g;
    }
};

// ------------------------------------------------------------------ gate
struct Gate
{
    bool enabled = false;
    double thr = -50, range = 60, ae = 0, aa = 0, ar = 0, p_ = 0, g = 0;
    int hold = 0, cnt = 0;
    void build (const juce::var& gt, double sr)
    {
        enabled = on (gt);
        thr = num (gt, "thr", -50); range = num (gt, "range", 60);
        ae = coef (10.0, sr); aa = coef (num (gt, "attack", 2), sr); ar = coef (num (gt, "release", 70), sr);
        hold = (int) (num (gt, "hold", 90) / 1000.0 * sr);
    }
    inline double p (double v, double knob)
    {
        if (! enabled || knob <= 0) return v;
        p_ = ae * p_ + (1 - ae) * v * v;
        if (10.0 * std::log10 (p_ + 1e-18) > thr) cnt = hold; else if (cnt > 0) --cnt;
        const double tg = cnt > 0 ? 1.0 : dbToLin (-range * knob);
        const double c = tg > g ? aa : ar;
        g = c * g + (1 - c) * tg;
        return v * g;
    }
};

// ------------------------------------------------------------------ delay line helper (moved up for tape)
struct Line0
{
    std::vector<double> b; int w = 0;
    void init (int n) { b.assign ((size_t) std::max (2, n), 0.0); w = 0; }
    inline void push (double x) { b[(size_t) w] = x; if (++w >= (int) b.size()) w = 0; }
    inline double tap (double d) const
    {
        double rp = w - 1 - d; const int n = (int) b.size();
        while (rp < 0) rp += n;
        const int i0 = (int) rp; const double fr = rp - i0; const int i1 = (i0 + 1) % n;
        return b[(size_t) i0] * (1 - fr) + b[(size_t) i1] * fr;
    }
};

// ------------------------------------------------------------------ tape (2.1)
// Light real-time tape colour based on the analog tape research (Chowdhury DAFx 2019):
//   saturation (soft, level dependent, anti-aliased like the preamp), head bump, playback HF loss
//   (spacing loss ~ e^(-2 pi f d / v)), and wow/flutter. 15 ips = more bump + more HF loss than 30 ips.
struct Tape
{
    double sr = 44100, amt = -1, xp = 0, ph1 = 0, ph2 = 0;
    bool speed30 = false, built = false;
    Biq bump, hf;
    Line0 ln;
    void prepare (double rate) { sr = rate; ln.init ((int) (0.004 * sr)); built = false; }
    void update (double a, bool fast)
    {
        if (built && std::abs (a - amt) < 0.005 && fast == speed30) return;
        amt = a; speed30 = fast; built = true;
        std::vector<Biq> v;
        addBand (v, mx::Type::Peak, fast ? 110.0 : 60.0, 1.0, a * (fast ? 1.5 : 2.5), sr);
        // spacing loss at 10 kHz: d ~1.5 um -> ~2 dB at 15 ips, ~1 dB at 30 ips (doubled toward 20 kHz by the shelf)
        addBand (v, mx::Type::HighShelf, 12000.0, 0.707, -a * (fast ? 2.0 : 4.0), sr);
        const double z1 = bump.z1, z2 = bump.z2, h1 = hf.z1, h2 = hf.z2;
        bump = v[0]; hf = v[1];
        bump.z1 = z1; bump.z2 = z2; hf.z1 = h1; hf.z2 = h2;      // keep state: no clicks while turning the knob
    }
    static inline double sat (double x, double d) { return std::tanh (d * x) / d; }
    inline double p (double x)
    {
        ln.push (x);
        if (amt <= 0.0) { xp = x; return x; }
        // wow (0.6 Hz) + flutter (7 Hz)
        ph1 += 2 * PI * 0.6 / sr; if (ph1 > 2 * PI) ph1 -= 2 * PI;
        ph2 += 2 * PI * 7.0 / sr; if (ph2 > 2 * PI) ph2 -= 2 * PI;
        const double dms = 0.5 + amt * (0.12 * std::sin (ph1) + 0.02 * std::sin (ph2));
        const double y0 = ln.tap (dms / 1000.0 * sr);
        const double d = dbToLin (amt * 12.0);
        const double mid = 0.5 * (y0 + xp);
        const double avg = (sat (xp, d) + 4.0 * sat (mid, d) + sat (y0, d)) / 6.0;
        xp = y0;
        double y = y0 + (avg - mid);
        return hf.p (bump.p (y));
    }
};

// ------------------------------------------------------------------ Air (2.1): Aphex Aural Exciter style
// From US patent 4150253: 2-pole 4 kHz high-pass -> one-sided soft clip (transients only, low-order
// even + odd harmonics) -> mixed back under the dry signal.
struct Air
{
    Biq hp, post;
    void prepare (double sr)
    {
        std::vector<Biq> v;
        addBand (v, mx::Type::HighPass, 4000.0, 0.707, 0, sr);
        addBand (v, mx::Type::HighPass, 2000.0, 0.707, 0, sr);   // keeps the clip's DC / low products out
        hp = v[0]; post = v[1];
    }
    inline double p (double x, double amount)
    {
        const double h = hp.p (x);
        const double k = 8.0;
        const double c = post.p (h > 0 ? std::tanh (k * h) / k : h);    // one-sided: odd + even harmonics
        return amount <= 0 ? x : x + amount * 0.6 * c;
    }
};

// ------------------------------------------------------------------ delay line helper
struct Line
{
    std::vector<double> b; int w = 0;
    void init (int n) { b.assign ((size_t) std::max (2, n), 0.0); w = 0; }
    inline void push (double x) { b[(size_t) w] = x; if (++w >= (int) b.size()) w = 0; }
    inline double tap (double d) const       // d samples back (fractional)
    {
        double rp = w - 1 - d; const int n = (int) b.size();
        while (rp < 0) rp += n;
        const int i0 = (int) rp; const double fr = rp - i0; const int i1 = (i0 + 1) % n;
        return b[(size_t) i0] * (1 - fr) + b[(size_t) i1] * fr;
    }
};

struct Doubler
{
    double wet = 0, side = 0, ph = 0, inc = 0, sr = 44100;
    Line ln;
    void build (const juce::var& d, double rate)
    {
        sr = rate; wet = num (d, "wet", 0.0); side = num (d, "side", 0.0);
        ln.init ((int) (0.030 * sr)); inc = 2 * PI * 0.35 / sr; ph = 0;
    }
    inline void p (double x, double& L, double& R, double fx)
    {
        ln.push (x);
        const double w = wet * fx;
        if (w <= 0) return;
        const double k = 1 - 0.5 * std::min (w, 1.0);
        L *= k; R *= k;
        const double tl = ln.tap ((14.0 + 2.5 * std::sin (ph)) / 1000.0 * sr);
        const double tr = ln.tap ((22.0 + 2.5 * std::sin (ph + PI)) / 1000.0 * sr);
        const double mid = 0.5 * (tl + tr);
        L += w * (tl - side * mid); R += w * (tr - side * mid);
        ph += inc; if (ph > 2 * PI) ph -= 2 * PI;
    }
};

struct Slap
{
    double wet = 0, fb = 0; int d = 1;
    std::vector<double> buf; int w = 0;
    Biq hp, lp;
    void build (const juce::var& s, double sr)
    {
        wet = num (s, "wet", 0.0); fb = num (s, "fb", 0.0);
        d = std::max (1, (int) (num (s, "ms", 120) / 1000.0 * sr));
        buf.assign ((size_t) d, 0.0); w = 0;
        std::vector<Biq> v;
        addBand (v, mx::Type::HighPass, 300.0, 0.707, 0, sr);
        addBand (v, mx::Type::LowPass, 6000.0, 0.707, 0, sr);
        hp = v[0]; lp = v[1];
    }
    inline void p (double x, double& L, double& R, double knob)
    {
        const double v = buf[(size_t) w];
        const double sw = wet * knob;
        R += v * sw; L += v * sw * 0.25;
        buf[(size_t) w] = lp.p (hp.p (x)) + v * fb;
        if (++w >= d) w = 0;
    }
};

// 8-line FDN. Plate mode: input diffusion + steel-plate HF damping and a brighter, denser tail.
struct Reverb
{
    static constexpr int N = 8;
    bool plate = false;
    double wet = 0, width = 1, dc = 0, in = 0.35;
    int lens[N] {}, idx[N] {}, pd = 1, pw = 0;
    double gains[N] {}, lp[N] {};
    std::vector<double> bufs[N], pbuf;
    Biq hp;
    struct AP { std::vector<double> b; int w = 0; double g = 0.7; inline double p (double x)
        { const double d = b[(size_t) w]; const double y = -g * x + d; b[(size_t) w] = x + g * y; if (++w >= (int) b.size()) w = 0; return y; } };
    AP ap[4];
    void build (const juce::var& v, double sr, int type)
    {
        plate = type == 1 || v["type"].toString() == "plate";
        wet = num (v, "wet", 0.0); width = num (v, "width", 1.0);
        const double rt60 = std::max (0.2, num (v, "rt60", 1.6));
        const double size = plate ? num (v, "size", 1.0) * 0.55 : num (v, "size", 1.0);
        static const double DEL[N] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
        for (int k = 0; k < N; ++k)
        {
            lens[k] = std::max (8, (int) (DEL[k] * size * sr / 44100.0));
            bufs[k].assign ((size_t) lens[k] + 1, 0.0); idx[k] = 0; lp[k] = 0;
            gains[k] = std::pow (10.0, -3.0 * (lens[k] / sr) / rt60);
        }
        // plate: steel plate highs die ~8x faster than 1 kHz (measured: 1 kHz ~9 s, 8 kHz ~1.2 s at damper 0)
        const double damp = plate ? std::min (num (v, "damp", 7000), 3200.0) : num (v, "damp", 7000);
        dc = std::exp (-2 * PI * damp / sr);
        pd = std::max (1, (int) (num (v, "predelay", plate ? 0 : 25) / 1000.0 * sr));
        pbuf.assign ((size_t) pd, 0.0); pw = 0;
        std::vector<Biq> f; addBand (f, mx::Type::HighPass, plate ? 120.0 : 250.0, 0.707, 0, sr); hp = f[0];
        static const double AD[4] = { 142, 107, 379, 277 };
        for (int i = 0; i < 4; ++i) { ap[i].b.assign ((size_t) std::max (2, (int) (AD[i] * sr / 44100.0)), 0.0); ap[i].w = 0; ap[i].g = plate ? 0.7 : 0.0; }
        in = plate ? 0.3 : 0.35;
    }
    inline void p (double x, double& L, double& R, double knob)
    {
        const double w = wet * knob;
        double s0 = hp.p (x);
        if (plate) for (auto& a : ap) s0 = a.p (s0);
        const double inp = pbuf[(size_t) pw];
        pbuf[(size_t) pw] = s0; if (++pw >= pd) pw = 0;
        double outs[N], s = 0;
        for (int k = 0; k < N; ++k)
        {
            const double v = bufs[k][(size_t) idx[k]];
            lp[k] = (1 - dc) * v + dc * lp[k];
            outs[k] = lp[k] * gains[k];
            s += outs[k];
        }
        s *= 2.0 / N;
        double l = 0, r = 0;
        for (int k = 0; k < N; ++k)
        {
            bufs[k][(size_t) idx[k]] = inp * in + (outs[k] - s);
            if (++idx[k] >= lens[k]) idx[k] = 0;
            if (k % 2 == 0) l += outs[k]; else r += outs[k];
        }
        if (w <= 0) return;
        const double m = 0.5 * (l + r);
        L += w * (m + width * (l - m)); R += w * (m + width * (r - m));
    }
};

// ------------------------------------------------------------------ look-ahead limiter
struct Limiter
{
    double c = 0.89, r = 0, g = 1;
    int la = 88, w = 0;
    std::vector<double> dl, dr, need;
    bool zero = false;
    void build (double ceilingDb, double sr, bool lowLatency = false)
    {
        c = dbToLin (ceilingDb); r = coef (60.0, sr); g = 1; zero = lowLatency;
        la = std::max (1, (int) (0.002 * sr));
        dl.assign ((size_t) la, 0.0); dr.assign ((size_t) la, 0.0); need.assign ((size_t) la, 1.0); w = 0;
    }
    inline void p (double& L, double& R)
    {
        const double pk = std::max (std::abs (L), std::abs (R));
        if (zero)                                   // no look-ahead: instant attack, nothing delayed
        {
            const double m = pk > c ? c / pk : 1.0;
            g = m < g ? m : r * g + (1 - r) * m;
            L *= g; R *= g;
            return;
        }
        need[(size_t) w] = pk > c ? c / pk : 1.0;
        double m = 1.0;
        for (double n : need) m = std::min (m, n);
        g = m < g ? m : r * g + (1 - r) * m;
        const double ol = dl[(size_t) w], orr = dr[(size_t) w];
        dl[(size_t) w] = L; dr[(size_t) w] = R;
        if (++w >= la) w = 0;
        L = ol * g; R = orr * g;
    }
    int latency() const { return zero ? 0 : la; }
};

// ------------------------------------------------------------------ the chain
class Engine
{
public:
    // verbType: 0 = as preset (room), 1 = plate
    void prepare (double sampleRate, const juce::var& P, int verbType, bool lowLatency = false)
    {
        sr = sampleRate;
        tape.prepare (sr);
        air.prepare (sr);
        micEq.build (P["mic_eq"], sr);
        pre.build (P["pre"], sr);
        opto.build (P["opto"], sr);
        fet.build (P["fet"], sr);
        vca.build (P["vca"], sr);
        lev.build (P["lev"], sr);
        eq.build (P["eq"], sr);
        deess.build (P["deess"], sr);
        gate.build (P["gate"], sr);
        dbl.build (P["dbl"], sr);
        slap.build (P["slap"], sr);
        verb.build (P["verb"], sr, verbType);
        lim.build (num (P, "ceiling", -1.0), sr, lowLatency);
        outTargetDb = num (P, "out_rms_db", -18.0);
        riderA = coef (400.0, sr);
        calibrate (P, verbType);
    }

    int latency() const { return lim.latency(); }
    double inputGainDb() const { return autoDb; }
    // meters (audio thread writes, owner copies out after each block)
    double meterInDb = -100, meterGrDb = 0, meterOutDb = -100;

    // Start the Auto rider at a known level (HQ: the file's own active level)
    void seedAuto (double levelDb) { autoDb = juce::jlimit (-24.0, 36.0, IN_REF_DB - levelDb); }

    // mono in -> stereo out. inGainDb used when autoIn is false.
    void process (const float* in, float* L, float* R, int n, const Macros& m, double inGainDb, bool autoIn,
                  bool useLimiter = true)
    {
        const double shift = (1.0 - m.comp) * 10.0;
        const double outG = dbToLin (makeupDb + m.out);
        tape.update (juce::jlimit (0.0, 1.0, m.tape), m.tape30);
        double pkIn = 0, pkOut = 0;
        for (int i = 0; i < n; ++i)
        {
            double x = in[i];
            // input stage
            if (autoIn)
            {
                riderP = riderA * riderP + (1 - riderA) * x * x;
                const double lvl = 10.0 * std::log10 (riderP + 1e-18) + 3.0;   // ~RMS of a sung line
                if (lvl > -60.0)
                {
                    const double want = juce::jlimit (-24.0, 36.0, IN_REF_DB - lvl);
                    autoDb += juce::jlimit (-1.5, 1.5, want - autoDb) * (3.0 / sr);    // max ~3 dB per second
                }
                x *= dbToLin (autoDb);
            }
            else x *= dbToLin (inGainDb);

            pkIn = std::max (pkIn, std::abs (x));
            x = micEq.p (x);
            x = pre.p (x, m.drive);
            x = opto.p (x, shift);
            x = fet.p (x, shift);
            x = vca.p (x, shift);
            x = lev.p (x, shift);
            x = eq.p (x);
            x = tape.p (x);
            x = deess.p (x, m.deess);
            x = gate.p (x, m.gate);
            x = air.p (x, m.air);

            double l = x, r = x;
            dbl.p (x, l, r, m.fx);
            slap.p (x, l, r, m.delay);
            verb.p (x, l, r, m.reverb);
            l *= outG; r *= outG;
            if (useLimiter) lim.p (l, r);
            pkOut = std::max (pkOut, std::max (std::abs (l), std::abs (r)));
            L[i] = (float) l; R[i] = (float) r;
        }
        meterInDb = linToDb (pkIn);
        meterOutDb = linToDb (pkOut);
        meterGrDb = (opto.enabled ? opto.grs : 0.0) + (fet.enabled ? fet.last : 0.0)
                  + (vca.enabled ? vca.grs : 0.0) + (lev.enabled ? lev.grs : 0.0);
    }

private:
    double sr = 44100, makeupDb = 0, outTargetDb = -18, autoDb = 0, riderP = 0, riderA = 0;
    Eq micEq, eq;
    Preamp pre;
    Opto opto; Fet fet; Vca vca; Leveller lev;
    DeEsser deess; Gate gate;
    Doubler dbl; Slap slap; Reverb verb;
    Tape tape; Air air;
    Limiter lim;

    // Fixed output gain for this preset: run 4 s of vocal-band noise at the reference input level through a
    // fresh copy of the chain and set make-up so the output lands on the preset's loudness target.
    void calibrate (const juce::var& P, int verbType)
    {
        Engine probe;
        probe.sr = sr;
        probe.micEq = micEq; probe.pre = pre; probe.opto = opto; probe.fet = fet; probe.vca = vca; probe.lev = lev;
        probe.eq = eq; probe.deess = deess; probe.gate = gate; probe.dbl = dbl; probe.slap = slap; probe.verb = verb;
        probe.lim = lim; probe.makeupDb = 0; probe.tape = tape; probe.air = air;
        juce::ignoreUnused (P, verbType);
        const int n = (int) (4.0 * sr);
        std::vector<float> x ((size_t) n), L ((size_t) n), R ((size_t) n);
        juce::Random rng (7);
        Biq hp, lp; { std::vector<Biq> v; addBand (v, mx::Type::HighPass, 120.0, 0.707, 0, sr); addBand (v, mx::Type::LowPass, 5000.0, 0.707, 0, sr); hp = v[0]; lp = v[1]; }
        double e = 0;
        for (int i = 0; i < n; ++i)
        {
            // syllable-like envelope so gates and comps behave as on a vocal
            const double env = 0.55 + 0.45 * std::sin (2 * PI * 3.0 * i / sr);
            x[(size_t) i] = (float) (lp.p (hp.p (rng.nextFloat() * 2.0 - 1.0)) * env);
            e += (double) x[(size_t) i] * x[(size_t) i];
        }
        const double g = dbToLin (IN_REF_DB) / std::sqrt (e / n + 1e-20);
        for (auto& v : x) v = (float) (v * g);
        Macros m;
        probe.process (x.data(), L.data(), R.data(), n, m, 0.0, false, false);
        double s = 0; int cnt = 0;
        for (int i = n / 2; i < n; ++i) { const double mm = 0.5 * ((double) L[(size_t) i] + R[(size_t) i]); s += mm * mm; ++cnt; }
        const double outDb = 10.0 * std::log10 (s / std::max (1, cnt) + 1e-20);
        makeupDb = juce::jlimit (-30.0, 30.0, outTargetDb - outDb);
    }
};

// whole-file active level (median-and-up 50 ms frames), used to seed Auto in HQ
inline double activeRmsDb (const float* x, int n, double sr)
{
    const int hop = std::max (1, (int) (0.05 * sr));
    const int k = n / hop;
    if (k == 0) return -120.0;
    std::vector<double> fr ((size_t) k);
    for (int j = 0; j < k; ++j)
    {
        double s = 0;
        for (int i = j * hop; i < (j + 1) * hop; ++i) s += (double) x[i] * x[i];
        fr[(size_t) j] = s / hop;
    }
    std::sort (fr.begin(), fr.end());
    double s = 0;
    for (int j = k / 2; j < k; ++j) s += fr[(size_t) j];
    return 10.0 * std::log10 (s / (double) (k - k / 2) + 1e-20);
}
} // namespace mwl
