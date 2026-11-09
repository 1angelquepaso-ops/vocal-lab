#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <cstdint>

namespace wb
{
constexpr double kPi = 3.14159265358979323846;

// One-pole smoother. Set the time once per block, call next() per sample.
struct Smooth
{
    float v = 0, target = 0, c = 1;
    void reset (float x) { v = target = x; }
    void setTime (float ms, double fs) { c = ms <= 0.01f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (ms * 0.001 * fs))); }
    float next() { v += (target - v) * c; return v; }
};

// Zero-delay-feedback state-variable filter (Simper / Cytomic). Stable under fast cutoff modulation.
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float a1 = 0, a2 = 0, a3 = 0, k = 1.414f, A = 1;
    void reset() { ic1 = ic2 = 0; }

    void setLowHigh (double fc, double fs, float kk)
    {
        const double g = std::tan (kPi * juce::jlimit (5.0, fs * 0.47, fc) / fs);
        k = kk; setG ((float) g);
    }
    void setBell (double fc, double fs, float q, float dB)
    {
        A = std::pow (10.0f, dB / 40.0f);
        const double g = std::tan (kPi * juce::jlimit (5.0, fs * 0.47, fc) / fs);
        k = 1.0f / (q * A); setG ((float) g);
    }
    void setLowShelf (double fc, double fs, float dB)
    {
        A = std::pow (10.0f, dB / 40.0f);
        const double g = std::tan (kPi * juce::jlimit (5.0, fs * 0.47, fc) / fs) / std::sqrt (A);
        k = 1.41421f; setG ((float) g);
    }
    void setHighShelf (double fc, double fs, float dB)
    {
        A = std::pow (10.0f, dB / 40.0f);
        const double g = std::tan (kPi * juce::jlimit (5.0, fs * 0.47, fc) / fs) * std::sqrt (A);
        k = 1.41421f; setG ((float) g);
    }
    void setG (float g)
    {
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    // returns v1 (band) and v2 (low) through refs
    inline void tick (float v0, float& v1, float& v2)
    {
        const float v3 = v0 - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
    }
    inline float lp (float x)   { float b, l; tick (x, b, l); return l; }
    inline float hp (float x)   { float b, l; tick (x, b, l); return x - k * b - l; }
    inline float bp (float x)   { float b, l; tick (x, b, l); return b; }
    inline float bell (float x) { float b, l; tick (x, b, l); return x + k * (A * A - 1.0f) * b; }
    inline float lowShelf (float x)  { float b, l; tick (x, b, l); return x + k * (A - 1.0f) * b + (A * A - 1.0f) * l; }
    inline float highShelf (float x) { float b, l; tick (x, b, l); return A * A * x + k * (1.0f - A) * A * b + (1.0f - A * A) * l; }
};

inline double polyBlep (double t, double dt)
{
    if (t < dt)        { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt)  { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

// Deterministic hash -> -1..1 (same input, same output, every play and every render)
inline float hashNoise (int64_t i, int seed)
{
    uint64_t x = (uint64_t) i * 0x9E3779B97F4A7C15ull ^ ((uint64_t) (uint32_t) seed * 0xC2B2AE3D27D4EB4Full);
    x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 27; x *= 0x94D049BB133111EBull; x ^= x >> 31;
    return (float) ((double) (x >> 11) / (double) (1ull << 53) * 2.0 - 1.0);
}

// Smooth value noise over a continuous index (cosine interpolated between hashed points)
inline float smoothNoise (double t, int seed)
{
    const double f = std::floor (t);
    const float a = hashNoise ((int64_t) f, seed), b = hashNoise ((int64_t) f + 1, seed);
    const float u = (float) (0.5 - 0.5 * std::cos (kPi * (t - f)));
    return a + (b - a) * u;
}

inline float softClip (float x)
{
    const float ax = std::abs (x);
    if (ax <= 0.8f) return x;
    const float y = 0.8f + 0.2f * std::tanh ((ax - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}

// Pitch (MIDI note) of a mono sample, YIN-style. Returns -1 if no clear pitch.
inline double detectMidiNote (const float* x, int n, double fs)
{
    if (n < 2048) return -1;
    // analyse the loudest region
    const int win = juce::jmin (n, (int) (fs * 0.2));
    int best = 0; double bestE = -1;
    for (int s = 0; s + win <= n; s += win / 2)
    {
        double e = 0; for (int i = 0; i < win; ++i) e += (double) x[s + i] * x[s + i];
        if (e > bestE) { bestE = e; best = s; }
    }
    const int minLag = (int) (fs / 1200.0), maxLag = juce::jmin (win / 2, (int) (fs / 25.0));
    const int W = win - maxLag;
    if (W < 256 || maxLag <= minLag + 2) return -1;
    std::vector<double> d ((size_t) maxLag + 1, 0.0), cmnd ((size_t) maxLag + 1, 1.0);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (int i = 0; i < W; ++i) { const double t = (double) x[best + i] - x[best + i + lag]; s += t * t; }
        d[(size_t) lag] = s;
    }
    double run = 0;
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        run += d[(size_t) lag];
        cmnd[(size_t) lag] = run > 0 ? d[(size_t) lag] * lag / run : 1.0;
    }
    int lag = -1;
    for (int t = minLag; t < maxLag; ++t)
        if (cmnd[(size_t) t] < 0.15)
        {
            while (t + 1 < maxLag && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
            lag = t; break;
        }
    if (lag < 0)
    {
        double m = 1e9;
        for (int t = minLag; t < maxLag; ++t) if (cmnd[(size_t) t] < m) { m = cmnd[(size_t) t]; lag = t; }
        if (m > 0.35) return -1;
    }
    double fl = lag;
    if (lag > 1 && lag < maxLag)
    {
        const double a = cmnd[(size_t) lag - 1], b = cmnd[(size_t) lag], c = cmnd[(size_t) lag + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) fl = lag + 0.5 * (a - c) / den;
    }
    const double f0 = fs / fl;
    return 69.0 + 12.0 * std::log2 (f0 / 440.0);
}
} // namespace wb
