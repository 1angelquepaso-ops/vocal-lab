#pragma once
// MIRRORWALL vocal chain — C++ twin of mirrorwall/dsp.py.
// Same formulas, constants and order as the Python reference so a preset
// fitted in Python renders the same here. Every stage is causal and
// sample-by-sample, so the same code can run live later.
#include <juce_core/juce_core.h>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mw
{
using Buf = std::vector<double>;
constexpr double SR = 44100.0;
constexpr double PI = 3.14159265358979323846;
constexpr double IN_REF_DB = -20.0;

struct BQ { double b0, b1, b2, a1, a2; };

inline BQ biquad (const juce::String& kind, double f, double q, double gainDb, double sr = SR)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * PI * std::min (f, sr * 0.49) / sr;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;
    if (kind == "peak")
    {
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
    }
    else if (kind == "lowshelf")
    {
        const double sa = 2 * std::sqrt (A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + sa);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - sa);
        a0 = (A + 1) + (A - 1) * cw + sa;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - sa;
    }
    else if (kind == "highshelf")
    {
        const double sa = 2 * std::sqrt (A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + sa);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - sa);
        a0 = (A + 1) - (A - 1) * cw + sa;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - sa;
    }
    else if (kind == "hp")
    {
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    }
    else // lp
    {
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

inline void runBiquad (Buf& x, const BQ& c)
{
    double z1 = 0, z2 = 0;
    for (auto& v : x)
    {
        const double xi = v;
        const double yi = c.b0 * xi + z1;
        z1 = c.b1 * xi - c.a1 * yi + z2;
        z2 = c.b2 * xi - c.a2 * yi;
        v = yi;
    }
}

struct Band { const char* kind; double f, q; };
static const Band EQ_BANDS[] = {
    { "lowshelf", 100.0, 0.707 }, { "peak", 160.0, 1.4 }, { "peak", 250.0, 1.4 },
    { "peak", 400.0, 1.4 }, { "peak", 630.0, 1.4 }, { "peak", 1000.0, 1.4 },
    { "peak", 1600.0, 1.4 }, { "peak", 2500.0, 1.4 }, { "peak", 4000.0, 1.4 },
    { "peak", 6300.0, 1.4 }, { "peak", 9000.0, 1.4 }, { "highshelf", 12500.0, 0.707 } };
constexpr int NUM_BANDS = 12;
constexpr int NUM_DETAIL = 26;
constexpr double DETAIL_Q = 4.32;

inline double num (const juce::var& v, const char* k, double d)
{
    return v.hasProperty (k) ? (double) v[k] : d;
}

inline void runEq (Buf& x, const juce::var& eq)
{
    if (! eq.isObject()) return;
    const double hpf = num (eq, "hpf", 0.0);
    const int order = (int) num (eq, "hpf_order", 2.0);
    for (int i = 0; i < order; ++i)
        if (hpf > 20) runBiquad (x, biquad ("hp", hpf, 0.707, 0));
    if (auto* g = eq["gains"].getArray())
        for (int i = 0; i < juce::jmin (NUM_BANDS, g->size()); ++i)
            runBiquad (x, biquad (EQ_BANDS[i].kind, EQ_BANDS[i].f, EQ_BANDS[i].q, (double) (*g)[i]));
    const double lpf = num (eq, "lpf", 0.0);
    if (lpf > 0 && lpf < 20000) runBiquad (x, biquad ("lp", lpf, 0.707, 0));
    if (auto* d = eq["detail"].getArray())
        for (int i = 0; i < juce::jmin (NUM_DETAIL, d->size()); ++i)
        {
            const double g = (double) (*d)[i];
            if (g != 0.0)
                runBiquad (x, biquad ("peak", 50.0 * std::pow (2.0, i / 3.0), DETAIL_Q, g));
        }
}

// ---------------------------------------------------------------- preamp
inline void tube (Buf& x, double driveDb, double mix, double bias)
{
    const double d = std::pow (10.0, driveDb / 20.0), tb = std::tanh (bias), k = 1.0 / (1.0 - tb * tb);
    for (auto& v : x)
    {
        const double z = (std::tanh (d * v + bias) - tb) / d * k;
        v = (1 - mix) * v + mix * z;
    }
}

inline void console (Buf& x, double driveDb, double mix)
{
    const double d = std::pow (10.0, driveDb / 20.0);
    for (auto& v : x)
    {
        const double u = d * v;
        const double z = std::tanh (u + 0.15 * u * u * u / (1.0 + u * u)) / d;
        v = (1 - mix) * v + mix * z;
    }
    runBiquad (x, biquad ("lowshelf", 90.0, 0.707, 1.5));
    runBiquad (x, biquad ("highshelf", 15000.0, 0.707, -1.0));
}

// ---------------------------------------------------------------- dynamics
inline void opto (Buf& x, double thr, double ratio, double attackMs, double relFastMs, double relSlowMs)
{
    const double ea = std::exp (-1.0 / (SR * 0.001)), er = std::exp (-1.0 / (SR * 0.050));
    const double ga = std::exp (-1.0 / (SR * attackMs / 1000.0));
    const double rf = std::exp (-1.0 / (SR * relFastMs / 1000.0));
    const double rs = std::exp (-1.0 / (SR * relSlowMs / 1000.0));
    const double ls = std::exp (-1.0 / (SR * 2.0));
    double env = 0, grs = 0, hist = 0;
    for (auto& v : x)
    {
        const double lv = std::abs (v);
        env = lv > env ? ea * env + (1 - ea) * lv : er * env + (1 - er) * lv;
        const double db = 20.0 * std::log10 (env + 1e-9);
        const double over = db - thr;
        const double gr = over > 0 ? over * (1.0 - 1.0 / ratio) : 0.0;
        hist = ls * hist + (1 - ls) * gr;
        if (gr > grs) grs = ga * grs + (1 - ga) * gr;
        else
        {
            const double r = rf + (rs - rf) * std::min (1.0, hist / 6.0);
            grs = r * grs + (1 - r) * gr;
        }
        v *= std::pow (10.0, -grs / 20.0);
    }
}

inline void fet (Buf& x, double thr, double ratio, double attackMs, double releaseMs)
{
    const double a = std::exp (-1.0 / (SR * attackMs / 1000.0));
    const double r = std::exp (-1.0 / (SR * releaseMs / 1000.0));
    double env = 0;
    for (auto& v : x)
    {
        const double lv = std::abs (v);
        env = lv > env ? a * env + (1 - a) * lv : r * env + (1 - r) * lv;
        const double over = 20.0 * std::log10 (env + 1e-9) - thr;
        const double gr = over > 0 ? over * (1.0 - 1.0 / ratio) : 0.0;
        v *= std::pow (10.0, -gr / 20.0);
    }
}

inline void deess (Buf& x, double thr, double depth)
{
    Buf band = x;
    runBiquad (band, biquad ("hp", 4500.0, 0.707, 0));
    runBiquad (band, biquad ("hp", 4500.0, 0.707, 0));
    runBiquad (band, biquad ("lp", 11000.0, 0.707, 0));
    const double as = std::exp (-1.0 / (SR * 0.004)), ab = std::exp (-1.0 / (SR * 0.030)),
                 ag = std::exp (-1.0 / (SR * 0.001));
    double ps = 0, pb = 0, g = 1;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double s = band[i];
        ps = as * ps + (1 - as) * s * s;
        pb = ab * pb + (1 - ab) * x[i] * x[i];
        const double rel = 10.0 * std::log10 ((ps + 1e-18) / (pb + 1e-18));
        const double gr = juce::jlimit (0.0, 24.0, rel + thr);
        const double tg = std::pow (10.0, -gr * depth / 20.0);
        g = ag * g + (1 - ag) * tg;
        x[i] = (x[i] - s) + s * g;
    }
}

inline void gate (Buf& x, double thr, double rangeDb, double holdMs, double attackMs, double releaseMs)
{
    const double ae = std::exp (-1.0 / (SR * 0.010));
    const double aa = std::exp (-1.0 / (SR * attackMs / 1000.0));
    const double ar = std::exp (-1.0 / (SR * releaseMs / 1000.0));
    const double floor = std::pow (10.0, -rangeDb / 20.0);
    const int hold = (int) (holdMs / 1000.0 * SR);
    double p = 0, g = 0;
    int cnt = 0;
    for (auto& v : x)
    {
        p = ae * p + (1 - ae) * v * v;
        const double db = 10.0 * std::log10 (p + 1e-18);
        if (db > thr) cnt = hold;
        else if (cnt > 0) --cnt;
        const double tg = cnt > 0 ? 1.0 : floor;
        const double c = tg > g ? aa : ar;
        g = c * g + (1 - c) * tg;
        v *= g;
    }
}

// ---------------------------------------------------------------- space
inline void doubler (const Buf& x, Buf& L, Buf& R, double wet, double side,
                     double baseL = 14.0, double baseR = 22.0, double depthMs = 2.5, double rate = 0.35)
{
    const int maxd = (int) ((std::max (baseL, baseR) + depthMs + 2) / 1000.0 * SR) + 2;
    Buf buf ((size_t) maxd, 0.0);
    int w = 0;
    double ph = 0;
    const double inc = 2 * PI * rate / SR;
    for (size_t i = 0; i < x.size(); ++i)
    {
        buf[(size_t) w] = x[i];
        double taps[2];
        for (int ch = 0; ch < 2; ++ch)
        {
            const double base = ch == 0 ? baseL : baseR;
            const double off = ch == 1 ? PI : 0.0;
            const double d = (base + depthMs * std::sin (ph + off)) / 1000.0 * SR;
            double rp = w - d;
            while (rp < 0) rp += maxd;
            int i0 = (int) rp;
            const double fr = rp - i0;
            int i1 = i0 + 1;
            if (i1 >= maxd) i1 -= maxd;
            taps[ch] = buf[(size_t) i0] * (1 - fr) + buf[(size_t) i1] * fr;
        }
        const double mid = 0.5 * (taps[0] + taps[1]);
        L[i] += wet * (taps[0] - side * mid);
        R[i] += wet * (taps[1] - side * mid);
        ph += inc;
        if (ph > 2 * PI) ph -= 2 * PI;
        if (++w >= maxd) w = 0;
    }
}

inline void slap (const Buf& x, Buf& L, Buf& R, double ms, double wet, double fb)
{
    Buf s = x;
    runBiquad (s, biquad ("hp", 300.0, 0.707, 0));
    runBiquad (s, biquad ("lp", 6000.0, 0.707, 0));
    const int d = std::max (1, (int) (ms / 1000.0 * SR));
    Buf buf ((size_t) d, 0.0);
    int w = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double v = buf[(size_t) w];
        R[i] += v * wet;
        L[i] += v * wet * 0.25;
        buf[(size_t) w] = s[i] + v * fb;
        if (++w >= d) w = 0;
    }
}

inline void fdn (const Buf& xin, Buf& L, Buf& R, double rt60, double predelayMs, double dampHz,
                 double size, double wet, double width)
{
    static const double DEL[8] = { 1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116 };
    Buf x = xin;
    runBiquad (x, biquad ("hp", 250.0, 0.707, 0));
    constexpr int N = 8;
    int lens[N], maxl = 0;
    for (int k = 0; k < N; ++k) { lens[k] = (int) (DEL[k] * size * SR / 44100.0); maxl = std::max (maxl, lens[k]); }
    std::vector<Buf> bufs (N, Buf ((size_t) maxl + 1, 0.0));
    int idx[N] = {};
    double gains[N], lp[N] = {}, outs[N];
    for (int k = 0; k < N; ++k) gains[k] = std::pow (10.0, -3.0 * (lens[k] / SR) / rt60);
    const double dc = std::exp (-2 * PI * dampHz / SR);
    const int pd = std::max (1, (int) (predelayMs / 1000.0 * SR));
    Buf pbuf ((size_t) pd, 0.0);
    int pw = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double in = pbuf[(size_t) pw];
        pbuf[(size_t) pw] = x[i];
        if (++pw >= pd) pw = 0;
        double s = 0;
        for (int k = 0; k < N; ++k)
        {
            const double v = bufs[(size_t) k][(size_t) idx[k]];
            lp[k] = (1 - dc) * v + dc * lp[k];
            outs[k] = lp[k] * gains[k];
            s += outs[k];
        }
        s *= 2.0 / N;
        double l = 0, r = 0;
        for (int k = 0; k < N; ++k)
        {
            bufs[(size_t) k][(size_t) idx[k]] = in * 0.35 + (outs[k] - s);
            if (++idx[k] >= lens[k]) idx[k] = 0;
            if (k % 2 == 0) l += outs[k]; else r += outs[k];
        }
        const double m = 0.5 * (l + r);
        L[i] += wet * (m + width * (l - m));
        R[i] += wet * (m + width * (r - m));
    }
}

inline void limiter (Buf& L, Buf& R, double ceilingDb, double releaseMs)
{
    const size_t n = L.size();
    const int la = (int) (0.002 * SR);
    const double c = std::pow (10.0, ceilingDb / 20.0);
    const double r = std::exp (-1.0 / (SR * releaseMs / 1000.0));
    Buf need (n, 1.0);
    for (size_t i = 0; i < n; ++i)
    {
        const double pk = std::max (std::abs (L[i]), std::abs (R[i]));
        need[i] = pk > c ? c / pk : 1.0;
    }
    double g = 1.0;
    for (size_t i = 0; i < n; ++i)
    {
        double m = 1.0;
        for (size_t j = i; j <= i + (size_t) la && j < n; ++j) m = std::min (m, need[j]);
        g = m < g ? m : r * g + (1 - r) * m;
        L[i] *= g;
        R[i] *= g;
    }
}

inline double activeRmsDb (const Buf& L, const Buf* R = nullptr)
{
    const size_t hop = (size_t) (0.05 * SR);
    const size_t n = L.size() / hop;
    if (n == 0) return -120.0;
    std::vector<double> fr (n);
    for (size_t k = 0; k < n; ++k)
    {
        double s = 0;
        for (size_t i = k * hop; i < (k + 1) * hop; ++i)
        {
            const double m = R ? 0.5 * (L[i] + (*R)[i]) : L[i];
            s += m * m;
        }
        fr[k] = s / (double) hop;
    }
    std::sort (fr.begin(), fr.end());
    double s = 0;
    for (size_t k = n / 2; k < n; ++k) s += fr[k];
    return 10.0 * std::log10 (s / (double) (n - n / 2) + 1e-20);
}

// Tweak knobs (1.0 = as fitted)
struct Macros
{
    double comp = 1.0, reverb = 1.0, delay = 1.0, fx = 1.0, deess = 1.0, drive = 0.0, out = 0.0, gate = 1.0;
};

// x: mono (already tuned). Writes stereo L/R.
inline void process (Buf x, const juce::var& P, const Macros& m, Buf& L, Buf& R,
                     std::function<bool()> cancelled = {})
{
    auto stop = [&] { return cancelled && cancelled(); };
    const double trim = std::pow (10.0, (IN_REF_DB - activeRmsDb (x)) / 20.0);
    for (auto& v : x) v *= trim;
    if (P["mic_eq"].isObject()) runEq (x, P["mic_eq"]);
    const auto pre = P["pre"];
    const auto type = pre["type"].toString();
    if (type == "tube") tube (x, num (pre, "drive", 6.0) + m.drive, num (pre, "mix", 0.5), 0.12);
    else if (type == "console") console (x, num (pre, "drive", 6.0) + m.drive, num (pre, "mix", 0.5));
    if (stop()) return;
    const double shift = (1.0 - m.comp) * 10.0;
    const auto o = P["opto"];
    if (o.isObject() && (! o.hasProperty ("on") || (bool) o["on"]))
        opto (x, num (o, "thr", -24) + shift, num (o, "ratio", 3), num (o, "attack", 10),
              num (o, "rel_fast", 60), num (o, "rel_slow", 1500));
    const auto f = P["fet"];
    if (f.isObject() && (! f.hasProperty ("on") || (bool) f["on"]))
        fet (x, num (f, "thr", -18) + shift, num (f, "ratio", 6), num (f, "attack", 0.4), num (f, "release", 80));
    if (stop()) return;
    runEq (x, P["eq"]);
    const auto d = P["deess"];
    if (d.isObject() && (! d.hasProperty ("on") || (bool) d["on"]))
        deess (x, num (d, "thr", 10) + (m.deess - 1.0) * 6.0, num (d, "depth", 1.0));
    const auto g = P["gate"];
    if (g.isObject() && (! g.hasProperty ("on") || (bool) g["on"]) && m.gate > 0)
        gate (x, num (g, "thr", -50), num (g, "range", 60) * m.gate, num (g, "hold", 90),
              num (g, "attack", 2), num (g, "release", 70));
    if (stop()) return;

    L = x; R = x;
    const auto db = P["dbl"];
    const double dw = num (db, "wet", 0.0) * m.fx;
    if (dw > 0)
    {
        const double k = 1 - 0.5 * std::min (dw, 1.0);
        for (size_t i = 0; i < x.size(); ++i) { L[i] *= k; R[i] *= k; }
        doubler (x, L, R, dw, num (db, "side", 0.0));
    }
    const auto s = P["slap"];
    const double sw = num (s, "wet", 0.0) * m.delay;
    if (sw > 0) slap (x, L, R, num (s, "ms", 120), sw, num (s, "fb", 0.0));
    const auto v = P["verb"];
    const double vw = num (v, "wet", 0.0) * m.reverb;
    if (vw > 0) fdn (x, L, R, num (v, "rt60", 1.6), num (v, "predelay", 25), num (v, "damp", 7000),
                     num (v, "size", 1.0), vw, num (v, "width", 1.0));
    if (stop()) return;
    const double gain = std::pow (10.0, (num (P, "out_rms_db", -18.0) - activeRmsDb (L, &R) + m.out) / 20.0);
    for (size_t i = 0; i < L.size(); ++i) { L[i] *= gain; R[i] *= gain; }
    limiter (L, R, num (P, "ceiling", -1.0), 60.0);
}
} // namespace mw
