#pragma once
// MEX-EQ filter design.
// Every band is first described as a list of ANALOG sections H(s) (exactly what we measured / the
// textbook shapes). Sections are then turned into digital biquads with "matched magnitude" design
// (poles mapped exactly, numerator fitted to the analog magnitude at DC, Nyquist and the section's own
// frequency) so curves keep their analog shape right up to 20 kHz (no cramping), with zero latency.
#include <array>
#include <cmath>
#include <complex>
#include <vector>
#include <algorithm>
#include "GData.h"

namespace mx
{
constexpr double PI = 3.14159265358979323846;
using cpx = std::complex<double>;

enum class Type { Disabled = 0, LowPass, BandPass, HighPass, BandStop, LowShelf, Peak, HighShelf };
enum class Order { Gentle8 = 0, Gentle6, Gentle4, Two, Steep4, Steep6, Steep8, Steep12, Steep16, Brickwall };
enum class Shape { Auto = 0, LMF, LMFdiv3, HMF, HMFx3 };

inline const char* const TYPE_NAMES[] = { "Disabled", "Low pass", "Band pass", "High pass", "Band stop", "Low shelf", "Peaking", "High shelf" };
inline const char* const ORDER_NAMES[] = { "Gentle 8", "Gentle 6", "Gentle 4", "2", "Steep 4", "Steep 6", "Steep 8", "Steep 12", "Steep 16", "Brickwall" };
inline const char* const SHAPE_NAMES[] = { "Auto", "LMF", "LMF \xc3\xb7" "3", "HMF", "HMF \xc3\x97" "3" };
inline const char* const STEREO_NAMES[] = { "Stereo", "Left", "Right", "Mid", "Side" };

constexpr float G_MAX_DB = 21.5f;      // real SSL G range (Angel: keep at 21.5)
constexpr float CLEAN_MAX_DB = 24.0f;

// Analog section: (b2 s^2 + b1 s + b0) / (a2 s^2 + a1 s + a0), s in rad/s
struct ASec
{
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    cpx at (double w) const
    {
        const cpx s (0.0, w);
        return (b2 * s * s + b1 * s + b0) / (a2 * s * s + a1 * s + a0);
    }
};

struct BandSpec
{
    Type type = Type::Peak;
    Order order = Order::Two;
    bool gChar = false;      // G (SSL) character
    Shape shape = Shape::Auto;
    double freq = 1000, gainDb = 0, q = 0.7071;
};

// ---------- small helpers ----------
inline double lerpTable (const float* xs, const float* ys, int n, double x, bool logx = false)
{
    auto X = [&] (int i) { return logx ? std::log ((double) xs[i]) : (double) xs[i]; };
    const double xv = logx ? std::log (std::max (x, 1e-9)) : x;
    if (xv <= X (0)) return ys[0];
    if (xv >= X (n - 1)) return ys[n - 1];
    int i = 0;
    while (i < n - 2 && xv > X (i + 1)) ++i;
    const double t = (xv - X (i)) / (X (i + 1) - X (i));
    return ys[i] * (1 - t) + ys[i + 1] * t;
}

// normalised second-order pieces (w0 in rad/s)
inline ASec secLP2 (double w0, double Q) { ASec s; s.b0 = w0 * w0; s.a2 = 1; s.a1 = w0 / Q; s.a0 = w0 * w0; return s; }
inline ASec secHP2 (double w0, double Q) { ASec s; s.b2 = 1; s.b0 = 0; s.a2 = 1; s.a1 = w0 / Q; s.a0 = w0 * w0; return s; }
inline ASec secLP1 (double w0) { ASec s; s.b0 = w0; s.a1 = 1; s.a0 = w0; return s; }
inline ASec secHP1 (double w0) { ASec s; s.b1 = 1; s.b0 = 0; s.a1 = 1; s.a0 = w0; return s; }
inline ASec secBell (double w0, double gDb, double Q)
{
    const double A = std::pow (10.0, gDb / 40.0);
    ASec s; s.b2 = 1; s.b1 = A * w0 / Q; s.b0 = w0 * w0; s.a2 = 1; s.a1 = w0 / (A * Q); s.a0 = w0 * w0; return s;
}
// textbook analog shelves (RBJ analog prototypes)
inline ASec secLowShelf (double w0, double gDb, double Q)
{
    const double A = std::pow (10.0, gDb / 40.0), sa = std::sqrt (A) / Q;
    ASec s; s.b2 = A; s.b1 = A * sa * w0; s.b0 = A * A * w0 * w0; s.a2 = A; s.a1 = sa * w0; s.a0 = w0 * w0; return s;
}
inline ASec secHighShelf (double w0, double gDb, double Q)
{
    const double A = std::pow (10.0, gDb / 40.0), sa = std::sqrt (A) / Q;
    ASec s; s.b2 = A * A; s.b1 = A * sa * w0; s.b0 = A * w0 * w0; s.a2 = 1; s.a1 = sa * w0; s.a0 = A * w0 * w0; return s;
}

// Butterworth-type higher-order shelf / peak prototype (Parametric EQ 2 Steep orders).
// Prototype in p: sections (p^2 + a r p + r^2)/(p^2 + a p + 1), first-order (p + r)/(p + 1), r = G^(1/M).
inline void protoSections (int M, double r, double qs, std::vector<std::array<double, 6>>& out)
{
    // each entry: numerator n2 n1 n0, denominator d2 d1 d0 in p
    for (int k = 0; k < M / 2; ++k)
    {
        const double a = 2.0 * std::sin ((2 * k + 1) * PI / (2.0 * M)) / qs;
        out.push_back ({ 1.0, a * r, r * r, 1.0, a, 1.0 });
    }
    if (M % 2) out.push_back ({ 0.0, 1.0, r, 0.0, 1.0, 1.0 });
}

inline int shelfM (Order o)
{
    switch (o) { case Order::Steep4: return 4; case Order::Steep6: return 6; case Order::Steep8: return 8;
                 case Order::Steep12: return 12; case Order::Steep16: case Order::Brickwall: return 16; default: return 2; }
}
inline int shelfIdx (Order o)
{
    switch (o) { case Order::Steep4: return 1; case Order::Steep6: return 2; case Order::Steep8: return 3;
                 case Order::Steep12: return 4; case Order::Steep16: case Order::Brickwall: return 5; default: return 0; }
}

// p = c * s / w0  ->  sections in s
inline void shelfFromProto (bool high, double w0, double gDb, Order o, double qUser, std::vector<ASec>& out)
{
    const int M = shelfM (o);
    const double qs = gdata::SHELF_QS[shelfIdx (o)] * (qUser / 0.70710678);
    const double r = std::pow (10.0, gDb / 20.0 / M), c = std::sqrt (r);
    std::vector<std::array<double, 6>> ps; protoSections (M, r, qs, ps);
    for (auto& q : ps)
    {
        ASec s;
        if (! high)
        {   // p = c s / w0  :  n2 c^2 s^2/w0^2 + n1 c s/w0 + n0
            const double k2 = c * c / (w0 * w0), k1 = c / w0;
            s.b2 = q[0] * k2; s.b1 = q[1] * k1; s.b0 = q[2]; s.a2 = q[3] * k2; s.a1 = q[4] * k1; s.a0 = q[5];
        }
        else
        {   // p = c w0 / s  -> multiply by s^2: n2 c^2 w0^2 + n1 c w0 s + n0 s^2
            s.b2 = q[2]; s.b1 = q[1] * c * w0; s.b0 = q[0] * c * c * w0 * w0;
            s.a2 = q[5]; s.a1 = q[4] * c * w0; s.a0 = q[3] * c * c * w0 * w0;
            if (q[0] == 0.0 && q[3] == 0.0) // first-order: (p + r)/(p + 1) -> (c w0 + r s)/(c w0 + s)
            { s.b2 = 0; s.b1 = q[2]; s.b0 = q[1] * c * w0; s.a2 = 0; s.a1 = q[5]; s.a0 = q[4] * c * w0; }
        }
        out.push_back (s);
    }
}

inline int peakIdx (Order o)
{
    switch (o) { case Order::Steep4: return 1; case Order::Steep6: return 2; case Order::Steep8: return 3;
                 case Order::Steep12: return 4; case Order::Steep16: case Order::Brickwall: return 5; default: return 0; }
}
inline int peakM (Order o) { const int M[] = { 1, 2, 3, 4, 6, 8 }; return M[peakIdx (o)]; }

// Peak order M: p = c Qe (s/w0 + w0/s), c = sqrt(r). Each prototype biquad -> 4th order in s, split into 2 biquads.
inline void peakFromProto (double w0, double gDb, Order o, double qUser, std::vector<ASec>& out)
{
    const int pi = peakIdx (o), M = peakM (o);
    if (M == 1) { out.push_back (secBell (w0, gDb, qUser)); return; }
    const double qs = gdata::PEAK_QS[pi], Qe = qUser * gdata::PEAK_QM[pi];
    const double r = std::pow (10.0, gDb / 20.0 / M), c = std::sqrt (r) * Qe;
    std::vector<std::array<double, 6>> ps; protoSections (M, r, qs, ps);
    // map each prototype root p0 to the two s roots of  c (s^2 + w0^2) - p0 w0 s = 0
    auto mapRoots = [&] (cpx p0, cpx& s1, cpx& s2)
    {
        const cpx B = -p0 * w0 / c, C = w0 * w0;
        const cpx d = std::sqrt (B * B - 4.0 * C);
        s1 = (-B + d) * 0.5; s2 = (-B - d) * 0.5;
    };
    auto quadRoots = [] (double a2, double a1, double a0, cpx& r1, cpx& r2)
    {
        if (a2 == 0.0) { r1 = cpx (-a0 / a1, 0); r2 = cpx (std::nan (""), 0); return; }
        const cpx d = std::sqrt (cpx (a1 * a1 - 4 * a2 * a0, 0));
        r1 = (-a1 + d) / (2 * a2); r2 = (-a1 - d) / (2 * a2);
    };
    for (auto& q : ps)
    {
        cpx zn[2], pd[2];
        quadRoots (q[0], q[1], q[2], zn[0], zn[1]);
        quadRoots (q[3], q[4], q[5], pd[0], pd[1]);
        const bool first = (q[0] == 0.0);
        // first-order prototype -> one biquad in s ; second-order -> two biquads (conjugate pairing)
        if (first)
        {
            cpx a, b, c1, d; mapRoots (zn[0], a, b); mapRoots (pd[0], c1, d);
            ASec s; // (s - a)(s - b) / (s - c1)(s - d)
            s.b2 = 1; s.b1 = -(a + b).real(); s.b0 = (a * b).real();
            s.a2 = 1; s.a1 = -(c1 + d).real(); s.a0 = (c1 * d).real();
            out.push_back (s);
        }
        else
        {
            cpx z[4], p[4];
            mapRoots (zn[0], z[0], z[1]); mapRoots (zn[1], z[2], z[3]);
            mapRoots (pd[0], p[0], p[1]); mapRoots (pd[1], p[2], p[3]);
            // pair each root with its conjugate
            auto pairUp = [] (cpx* r, cpx out[2][2])
            {
                bool used[4] = {}; int k = 0;
                for (int i = 0; i < 4 && k < 2; ++i)
                {
                    if (used[i]) continue;
                    int best = -1; double bd = 1e300;
                    for (int j = 0; j < 4; ++j)
                        if (j != i && ! used[j]) { const double dd = std::abs (r[j] - std::conj (r[i])); if (dd < bd) { bd = dd; best = j; } }
                    used[i] = used[best] = true; out[k][0] = r[i]; out[k][1] = r[best]; ++k;
                }
            };
            cpx zp[2][2], pp[2][2]; pairUp (z, zp); pairUp (p, pp);
            for (int k = 0; k < 2; ++k)
            {
                ASec s;
                s.b2 = 1; s.b1 = -(zp[k][0] + zp[k][1]).real(); s.b0 = (zp[k][0] * zp[k][1]).real();
                s.a2 = 1; s.a1 = -(pp[k][0] + pp[k][1]).real(); s.a0 = (pp[k][0] * pp[k][1]).real();
                out.push_back (s);
            }
        }
    }
}

// Pass filters (Parametric EQ 2 orders): stage Q multipliers
inline void passStageQs (Order o, double q, std::vector<double>& qs)
{
    using namespace gdata;
    auto butter = [&] (int N) { for (int k = 0; k < N / 2; ++k) qs.push_back ((q / 0.70710678) / (2.0 * std::sin ((2 * k + 1) * PI / (2.0 * N)))); };
    switch (o)
    {
        case Order::Gentle8: for (int i = 0; i < 4; ++i) qs.push_back (q * GENTLE_Q[2]); break;
        case Order::Gentle6: for (int i = 0; i < 3; ++i) qs.push_back (q * GENTLE_Q[1]); break;
        case Order::Gentle4: for (int i = 0; i < 2; ++i) qs.push_back (q * GENTLE_Q[0]); break;
        case Order::Two: qs.push_back (q); break;
        case Order::Steep4: for (auto m : STEEP4_Q) qs.push_back (q * m); break;
        case Order::Steep6: for (auto m : STEEP6_Q) qs.push_back (q * m); break;
        case Order::Steep8: for (auto m : STEEP8_Q) qs.push_back (q * m); break;
        case Order::Steep12: butter (12); break;
        case Order::Steep16: butter (16); break;
        case Order::Brickwall: butter (32); break;
    }
}

// ---------- G (measured SSL) models ----------
inline double gBellDrift (Shape sh, double f)
{
    using namespace gdata;
    switch (sh)
    {
        case Shape::LMF:     return lerpTable (DRIFT_LMF_F, DRIFT_LMF_D, 3, f, true);
        case Shape::LMFdiv3: return lerpTable (DRIFT_LMF_F, DRIFT_LMF_D, 3, f / 0.235, true);
        case Shape::HMF:     return lerpTable (DRIFT_HMF_F, DRIFT_HMF_D, 2, f, true);
        case Shape::HMFx3:   return lerpTable (DRIFT_HMF_F, DRIFT_HMF_D, 2, f / 3.3, true);
        default:             return lerpTable (DRIFT_AUTO_F, DRIFT_AUTO_D, 5, f, true);
    }
}

inline void gBell (const BandSpec& b, std::vector<ASec>& out)
{
    using namespace gdata;
    const double g = std::clamp (b.gainDb, (double) -G_MAX_DB, (double) G_MAX_DB), a = std::abs (g);
    const double h = g >= 0 ? lerpTable (H_BOOST_G, H_BOOST_V, 8, a) : lerpTable (H_CUT_G, H_CUT_V, 5, a);
    const double qf = g >= 0 ? lerpTable (QF_BOOST_G, QF_BOOST_V, 7, a) : lerpTable (QF_CUT_G, QF_CUT_V, 8, a);
    double qShape = 1.0;
    if (b.shape == Shape::LMFdiv3) qShape = lerpTable (DIV3_F, DIV3_Q, 4, b.freq, true);
    if (b.shape == Shape::HMFx3)   qShape = lerpTable (X3_F, X3_Q, 9, b.freq, true);
    const double f = b.freq * (1.0 + gBellDrift (b.shape, b.freq) * h);
    out.push_back (secBell (2 * PI * f, g, b.q * qf * qShape));
}

// shelf table lookup: params (9) at (k, g)
inline void gShelfParams (bool high, double k, double g, double p[9])
{
    using namespace gdata;
    const int NK = high ? HS_NK : LS_NK, NG = high ? HS_NG : LS_NG;
    const float* K = high ? HS_K : LS_K; const float* Gs = high ? HS_G : LS_G; const float* P = high ? HS_P : LS_P;
    k = std::clamp (k, (double) K[0], (double) K[NK - 1]);
    int i = 0; while (i < NK - 2 && k > K[i + 1]) ++i;
    const double a = (k - K[i]) / (K[i + 1] - K[i]);
    // gain side
    int lo = 0, hi = NG - 1;
    if (g < 0) { hi = 0; while (hi < NG && Gs[hi] < 0) ++hi; hi -= 1; }
    else { lo = 0; while (lo < NG && Gs[lo] < 0) ++lo; }
    const double gmin = Gs[lo], gmax = Gs[hi];
    g = std::clamp (g, gmin, gmax);
    int j = lo; while (j < hi - 1 && g > Gs[j + 1]) ++j;
    const double bb = (g - Gs[j]) / (Gs[j + 1] - Gs[j]);
    for (int n = 0; n < 9; ++n)
    {
        auto at = [&] (int ki, int gj) { return (double) P[(ki * NG + gj) * 9 + n]; };
        p[n] = (at (i, j) * (1 - a) + at (i + 1, j) * a) * (1 - bb) + (at (i, j + 1) * (1 - a) + at (i + 1, j + 1) * a) * bb;
    }
}

inline void gShelf (const BandSpec& b, std::vector<ASec>& out)
{
    using namespace gdata;
    const bool high = b.type == Type::HighShelf;
    const float* FN = high ? HS_FN : LS_FN; const int nfn = 10;
    // position k from the nominal corner (half-gain point at low gain)
    double k, scale = 1.0;
    if (b.freq <= FN[0]) { k = 0; scale = b.freq / FN[0]; }
    else if (b.freq >= FN[nfn - 1]) { k = nfn - 1; scale = b.freq / FN[nfn - 1]; }
    else
    {
        int i = 0; while (i < nfn - 2 && b.freq > FN[i + 1]) ++i;
        k = i + std::log (b.freq / FN[i]) / std::log ((double) FN[i + 1] / FN[i]);
    }
    double g = std::clamp (b.gainDb, (double) -G_MAX_DB, (double) G_MAX_DB), gs = 1.0;
    const double gTiny = 0.25;
    if (std::abs (g) < gTiny) { gs = std::abs (g) / gTiny; g = g < 0 ? -gTiny : gTiny; }
    double p[9]; gShelfParams (high, k, g, p);
    const double w = 2 * PI * scale;
    const double f0 = std::exp (p[0]), Q = std::exp (p[2]);
    out.push_back (high ? secHighShelf (w * f0, p[1] * gs, Q) : secLowShelf (w * f0, p[1] * gs, Q));
    out.push_back (secBell (w * std::exp (p[3]), p[4] * gs, std::exp (p[5])));
    out.push_back (secBell (w * std::exp (p[6]), p[7] * gs, std::exp (p[8])));
}

inline void gLowPass (const BandSpec& b, std::vector<ASec>& out)
{
    using namespace gdata;
    // rows: f3, f1, q1, fz, qz, fp, qp ; interpolate in log f3
    const int N = LP_N; auto R = [&] (int i, int c) { return (double) LP_T[i * 7 + c]; };
    double row[7]; double scale = 1.0, topScale = 1.0; const double f = b.freq;
    if (f <= R (0, 0)) { for (int c = 0; c < 7; ++c) row[c] = R (0, c); scale = f / R (0, 0); }
    else if (f >= R (N - 1, 0)) { for (int c = 0; c < 7; ++c) row[c] = R (N - 1, c); scale = f / R (N - 1, 0); topScale = scale; }
    else
    {
        int i = 0; while (i < N - 2 && f > R (i + 1, 0)) ++i;
        const double t = std::log (f / R (i, 0)) / std::log (R (i + 1, 0) / R (i, 0));
        for (int c = 0; c < 7; ++c) row[c] = std::exp (std::log (R (i, c)) * (1 - t) + std::log (R (i + 1, c)) * t);
    }
    out.push_back (secLP2 (2 * PI * row[1] * scale, row[2]));
    // top pair (zero pair / pole pair): fixed inside the measured range, moves up above it
    const double wz = 2 * PI * row[3] * topScale, wp = 2 * PI * row[5] * topScale;
    ASec s; s.b2 = 1 / (wz * wz); s.b1 = 1 / (wz * row[4]); s.b0 = 1; s.a2 = 1 / (wp * wp); s.a1 = 1 / (wp * row[6]); s.a0 = 1;
    out.push_back (s);
}

inline void gHighPass (const BandSpec& b, std::vector<ASec>& out)
{   // measured: exact 3rd-order Butterworth, 18 dB/oct
    const double w = 2 * PI * b.freq;
    out.push_back (secHP1 (w));
    out.push_back (secHP2 (w, 1.0));
}

inline bool gCapable (Type t)
{
    return t == Type::Peak || t == Type::LowShelf || t == Type::HighShelf || t == Type::HighPass || t == Type::LowPass;
}

// ---------- full band -> analog sections ----------
inline void designBand (const BandSpec& b, std::vector<ASec>& out)
{
    out.clear();
    if (b.type == Type::Disabled) return;
    const double w0 = 2 * PI * b.freq;
    if (b.gChar && gCapable (b.type))
    {
        switch (b.type)
        {
            case Type::Peak:      gBell (b, out); return;
            case Type::LowShelf:
            case Type::HighShelf: gShelf (b, out); return;
            case Type::HighPass:  gHighPass (b, out); return;
            case Type::LowPass:   gLowPass (b, out); return;
            default: break;
        }
    }
    const double g = std::clamp (b.gainDb, (double) -CLEAN_MAX_DB, (double) CLEAN_MAX_DB);
    switch (b.type)
    {
        case Type::Peak:
            if (b.order == Order::Two || b.order <= Order::Gentle4) out.push_back (secBell (w0, g, b.q));
            else peakFromProto (w0, g, b.order, b.q, out);
            break;
        case Type::LowShelf:
        case Type::HighShelf:
        {
            const bool high = b.type == Type::HighShelf;
            if (b.order <= Order::Two) out.push_back (high ? secHighShelf (w0, g, b.q) : secLowShelf (w0, g, b.q));
            else shelfFromProto (high, w0, g, b.order, b.q, out);
            break;
        }
        case Type::HighPass:
        case Type::LowPass:
        {
            std::vector<double> qs; passStageQs (b.order, b.q, qs);
            for (auto q : qs) out.push_back (b.type == Type::HighPass ? secHP2 (w0, q) : secLP2 (w0, q));
            break;
        }
        case Type::BandPass:
        case Type::BandStop:
        {
            std::vector<double> qs; passStageQs (b.order, 0.70710678, qs);
            const int stages = (int) qs.size();
            // stage Q stays the user Q for order 2; higher orders stack stages (sharper skirts)
            for (int i = 0; i < stages; ++i)
            {
                const double q = b.q * qs[(size_t) i] / 0.70710678;
                ASec s;
                if (b.type == Type::BandPass) { s.b1 = w0 / q; s.b0 = 0; s.b2 = 0; }
                else { s.b2 = 1; s.b1 = 0; s.b0 = w0 * w0; }
                s.a2 = 1; s.a1 = w0 / q; s.a0 = w0 * w0;
                out.push_back (s);
            }
            break;
        }
        default: break;
    }
}

inline cpx analogResponse (const std::vector<ASec>& secs, double fHz)
{
    cpx h (1, 0); const double w = 2 * PI * fHz;
    for (auto& s : secs) h *= s.at (w);
    return h;
}

// ---------- digital biquad + matched-magnitude conversion ----------
struct BQ
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    cpx at (double f, double sr) const
    {
        const auto z = std::polar (1.0, -2.0 * PI * f / sr);
        return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
    }
};

// 3-point "matched" design (Vicanek): poles mapped exactly, numerator matches |H| at DC, Nyquist and wm
inline BQ matched3 (const ASec& a, double sr)
{
    BQ d;
    const double T = 1.0 / sr, nyq = PI * sr;
    const bool firstOrder = (a.a2 == 0.0);
    double pw = 0;
    if (firstOrder)
    {
        const double p = -a.a0 / a.a1; d.a1 = -std::exp (p * T); d.a2 = 0; pw = std::abs (p);
    }
    else
    {
        const double A = a.a1 / a.a2, B = a.a0 / a.a2, disc = A * A - 4 * B;
        if (disc < 0)
        {
            const double sig = -A / 2, om = std::sqrt (-disc) / 2;
            d.a1 = -2 * std::exp (sig * T) * std::cos (om * T); d.a2 = std::exp (2 * sig * T);
        }
        else
        {
            const double p1 = (-A + std::sqrt (disc)) / 2, p2 = (-A - std::sqrt (disc)) / 2;
            d.a1 = -(std::exp (p1 * T) + std::exp (p2 * T)); d.a2 = std::exp ((p1 + p2) * T);
        }
        pw = std::sqrt (std::abs (B));
    }
    auto Ha2 = [&] (double w) { return std::norm (a.at (w)); };
    if (a.b2 == 0.0 && firstOrder)
    {
        const double s0 = std::sqrt (Ha2 (0.0)) * (1 + d.a1), sn = std::sqrt (Ha2 (nyq)) * (1 - d.a1);
        d.b0 = (s0 + sn) / 2; d.b1 = (s0 - sn) / 2; d.b2 = 0;
        return d;
    }
    const double A0 = std::pow (1 + d.a1 + d.a2, 2), A1 = std::pow (1 - d.a1 + d.a2, 2), A2 = -4 * d.a2;
    double wm = std::clamp (pw, 0.02 * nyq, 0.9 * nyq);
    const double s2 = std::pow (std::sin (wm * T / 2), 2);
    const double phi0 = 1 - s2, phi1 = s2, phi2 = 4 * phi0 * phi1;
    const double B0 = Ha2 (0.0) * A0, B1 = Ha2 (nyq) * A1;
    const double B2 = (Ha2 (wm) * (A0 * phi0 + A1 * phi1 + A2 * phi2) - B0 * phi0 - B1 * phi1) / phi2;
    const double sB0 = std::sqrt (std::max (B0, 0.0)), sB1 = std::sqrt (std::max (B1, 0.0)), W = 0.5 * (sB0 + sB1);
    const double b0 = 0.5 * (W + std::sqrt (std::max (W * W + B2, 0.0)));
    d.b0 = b0; d.b1 = 0.5 * (sB0 - sB1); d.b2 = b0 > 1e-30 ? -B2 / (4 * b0) : 0.0;
    return d;
}

// Least-squares design: poles mapped (clamped below Nyquist), numerator |N|^2 fitted over the whole band
inline BQ matchedLS (const ASec& a, double sr)
{
    BQ d;
    const double T = 1.0 / sr, nyq = PI * sr;
    if (a.a2 == 0.0) { const double p = -a.a0 / a.a1; d.a1 = -std::exp (p * T); d.a2 = 0; }
    else
    {
        const double A = a.a1 / a.a2, B = a.a0 / a.a2, disc = A * A - 4 * B;
        double w0 = std::sqrt (std::abs (B));
        if (disc < 0)
        {
            const double zeta = A / (2 * w0);
            w0 = std::min (w0, 0.9 * nyq);
            const double sig = -zeta * w0, om = w0 * std::sqrt (std::max (1 - zeta * zeta, 0.0));
            d.a1 = -2 * std::exp (sig * T) * std::cos (om * T); d.a2 = std::exp (2 * sig * T);
        }
        else
        {
            const double p1 = std::max ((-A + std::sqrt (disc)) / 2, -2.7 * nyq), p2 = std::max ((-A - std::sqrt (disc)) / 2, -2.7 * nyq);
            d.a1 = -(std::exp (p1 * T) + std::exp (p2 * T)); d.a2 = std::exp ((p1 + p2) * T);
        }
    }
    const double A0 = std::pow (1 + d.a1 + d.a2, 2), A1 = std::pow (1 - d.a1 + d.a2, 2), A2 = -4 * d.a2;
    const bool first = (a.b2 == 0.0 && a.a2 == 0.0);
    double M[3][3] = {}, v[3] = {};
    const int K = 40;
    for (int k = 0; k <= K; ++k)
    {
        const double w = k == 0 ? 0.0 : 2 * PI * 10.0 * std::pow (0.499 * sr / 10.0, (k - 1) / (double) (K - 1));
        const double st = std::pow (std::sin (w * T / 2), 2), p0 = 1 - st, p1 = st, p2 = 4 * p0 * p1;
        const double Ad = A0 * p0 + A1 * p1 + A2 * p2;
        const double tgt = std::norm (a.at (w)) * Ad, wt = 1.0 / (tgt + 1e-12);
        const double ph[3] = { p0, p1, p2 };
        for (int i = 0; i < 3; ++i) { v[i] += wt * ph[i] * tgt; for (int j = 0; j < 3; ++j) M[i][j] += wt * ph[i] * ph[j]; }
    }
    double B[3] = { 0, 0, 0 };
    const int n = first ? 2 : 3;
    // solve n x n (Gaussian elimination)
    double Mm[3][4];
    for (int i = 0; i < n; ++i) { for (int j = 0; j < n; ++j) Mm[i][j] = M[i][j]; Mm[i][3] = v[i]; }
    for (int c = 0; c < n; ++c)
    {
        int piv = c; for (int r = c + 1; r < n; ++r) if (std::abs (Mm[r][c]) > std::abs (Mm[piv][c])) piv = r;
        for (int j = 0; j < 4; ++j) std::swap (Mm[c][j], Mm[piv][j]);
        if (std::abs (Mm[c][c]) < 1e-300) return matched3 (a, sr);
        for (int r = 0; r < n; ++r) if (r != c) { const double f = Mm[r][c] / Mm[c][c]; for (int j = c; j < 4; ++j) Mm[r][j] -= f * Mm[c][j]; }
    }
    for (int i = 0; i < n; ++i) B[i] = Mm[i][3] / Mm[i][i];
    const double sB0 = std::sqrt (std::max (B[0], 0.0)), sB1 = std::sqrt (std::max (B[1], 0.0)), W = 0.5 * (sB0 + sB1);
    const double b0 = 0.5 * (W + std::sqrt (std::max (W * W + B[2], 0.0)));
    d.b0 = b0; d.b1 = 0.5 * (sB0 - sB1); d.b2 = b0 > 1e-30 ? -B[2] / (4 * b0) : 0.0;
    return d;
}

// When the zeros are sharper than the poles (cuts) or the poles sit above Nyquist, design the inverse
// section (sharp part mapped exactly) and flip it.
template <typename Fn>
inline BQ withInversion (const ASec& a, double sr, Fn design)
{
    const double nyq = PI * sr;
    if (a.b2 != 0 && a.a2 != 0 && a.b0 > 0 && a.a0 > 0)
    {
        const double wz = std::sqrt (a.b0 / a.b2), wp = std::sqrt (a.a0 / a.a2);
        const double zz = a.b1 / (2 * std::sqrt (a.b0 * a.b2)), zp = a.a1 / (2 * std::sqrt (a.a0 * a.a2));
        if (zz < zp || (wp > 0.9 * nyq && wz < wp))
        {
            ASec inv; inv.b0 = a.a0; inv.b1 = a.a1; inv.b2 = a.a2; inv.a0 = a.b0; inv.a1 = a.b1; inv.a2 = a.b2;
            const BQ q = design (inv, sr);
            BQ d; d.b0 = 1 / q.b0; d.b1 = q.a1 / q.b0; d.b2 = q.a2 / q.b0; d.a1 = q.b1 / q.b0; d.a2 = q.b2 / q.b0;
            return d;
        }
    }
    return design (a, sr);
}

// Best of both designs, judged against the analog curve 20 Hz - 20 kHz
inline BQ matched (const ASec& a, double sr)
{
    const BQ x = withInversion (a, sr, matched3), y = withInversion (a, sr, matchedLS);
    double ex = 0, ey = 0;
    for (int k = 0; k < 20; ++k)
    {
        const double f = std::min (20.0 * std::pow (1000.0, k / 19.0), 0.49 * sr);
        const double an = 20 * std::log10 (std::abs (a.at (2 * PI * f)) + 1e-15);
        ex = std::max (ex, std::abs (20 * std::log10 (std::abs (x.at (f, sr)) + 1e-15) - an));
        ey = std::max (ey, std::abs (20 * std::log10 (std::abs (y.at (f, sr)) + 1e-15) - an));
    }
    const bool okx = std::isfinite (x.b0) && std::isfinite (x.a1), oky = std::isfinite (y.b0) && std::isfinite (y.a1);
    if (! oky) return x;
    if (! okx) return y;
    return ex <= ey ? x : y;
}

inline void toDigital (const std::vector<ASec>& secs, double sr, std::vector<BQ>& out)
{
    out.clear();
    for (auto& s : secs) out.push_back (matched (s, sr));
}

inline cpx digitalResponse (const std::vector<BQ>& bq, double f, double sr)
{
    cpx h (1, 0); for (auto& b : bq) h *= b.at (f, sr); return h;
}
} // namespace mx
