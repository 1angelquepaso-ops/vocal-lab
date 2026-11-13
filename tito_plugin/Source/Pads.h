#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>

// 16-pad MPC-style sampler core. Pad 1 = MIDI note 36 ... pad 16 = note 51.

namespace tito
{
constexpr int numPads = 16;
constexpr int baseNote = 36;
constexpr int maxVoices = 64;

struct PadInfo { const char* key; const char* name; const char* type; };
inline const PadInfo padInfo[numPads] = {
    { "kick",    "Kick",       "kick" },    { "kick2",   "Kick 2",     "808" },
    { "snare",   "Snare",      "snare" },   { "clap",    "Clap",       "clap" },
    { "rim",     "Rim",        "rim" },     { "chh",     "Closed hat", "hat" },
    { "ohh",     "Open hat",   "openhat" }, { "phh",     "Pedal hat",  "hat" },
    { "tom_lo",  "Tom low",    "tom" },     { "tom_mid", "Tom mid",    "tom" },
    { "tom_hi",  "Tom high",   "tom" },     { "crash",   "Crash",      "crash" },
    { "ride",    "Ride",       "ride" },    { "perc1",   "Perc 1",     "perc" },
    { "perc2",   "Perc 2",     "perc" },    { "fx",      "FX",         "fx" } };

struct PadSample
{
    juce::AudioBuffer<float> audio;     // stereo
    double rate = 44100.0;
    juce::File file;
};

struct PadParams
{
    float start = 0.0f;     // 0..1 of the sample
    float length = 1.0f;    // 0..1 of what's left after start
    float pitch = 0.0f;     // semitones
    float decay = 1.0f;     // 1 = the sample's own decay, lower = shorter
    float volume = 0.0f;    // dB
    float pan = 0.0f;       // -1..1
    bool loop = false, reverse = false;
    int choke = 0;          // 0 none, 1..4 groups
    int out = 0;            // 0 main, 1..16 = pad outputs
};

struct Voice
{
    std::shared_ptr<const PadSample> s;
    int pad = -1;
    double pos = 0, step = 1, a = 0, b = 0;
    bool loop = false, reverse = false, active = false, releasing = false;
    float gl = 1, gr = 1, env = 1, decayCoef = 1, relCoef = 1;
    int64_t age = 0;
};

class PadEngine
{
public:
    std::array<std::shared_ptr<const PadSample>, numPads> samples;
    std::array<PadParams, numPads> params;
    std::array<std::atomic<int>, numPads> hits {};          // UI flash counters
    juce::SpinLock lock;                                     // guards samples + params

    void prepare (double sr) { hostRate = sr; }

    void setSample (int pad, std::shared_ptr<const PadSample> s)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        samples[(size_t) pad] = std::move (s);
    }

    void setParams (int pad, const PadParams& p)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        params[(size_t) pad] = p;
    }

    PadParams getParams (int pad)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        return params[(size_t) pad];
    }

    // audio thread
    void trigger (int pad, float velocity)
    {
        if (pad < 0 || pad >= numPads) return;
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;
        auto s = samples[(size_t) pad];
        const auto& p = params[(size_t) pad];
        if (s == nullptr || s->audio.getNumSamples() < 2) return;
        hits[(size_t) pad].fetch_add (1);

        if (p.choke > 0)
            for (auto& v : voices)
                if (v.active && v.pad >= 0 && params[(size_t) v.pad].choke == p.choke)
                    release (v, 0.004f);

        auto& v = freeVoice();
        const double n = (double) s->audio.getNumSamples();
        v.s = s;
        v.pad = pad;
        v.a = juce::jlimit (0.0, n - 2.0, (double) p.start * n);
        v.b = juce::jlimit (v.a + 1.0, n - 1.0, v.a + (double) p.length * (n - v.a));
        v.reverse = p.reverse;
        v.loop = p.loop;
        v.pos = v.reverse ? v.b - 1.0 : v.a;
        v.step = (s->rate / hostRate) * std::pow (2.0, (double) p.pitch / 12.0);
        // -9 dB of headroom: every pad sound is normalised to full scale, and kick + 808 + crash stack up
        const float g = 0.35f * juce::Decibels::decibelsToGain (p.volume) * std::pow (juce::jlimit (0.0f, 1.0f, velocity), 1.4f);
        const float ang = (p.pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        v.gl = g * std::cos (ang) * juce::MathConstants<float>::sqrt2;
        v.gr = g * std::sin (ang) * juce::MathConstants<float>::sqrt2;
        v.env = 1.0f;
        v.releasing = false;
        v.relCoef = 1.0f;
        if (p.decay >= 0.999f)
            v.decayCoef = 1.0f;
        else
        {
            const double tau = 0.015 + (double) p.decay * p.decay * 1.6;
            v.decayCoef = (float) std::exp (-1.0 / (tau * hostRate));
        }
        v.age = ++clock;
        v.active = true;
    }

    void noteOff (int pad)
    {
        for (auto& v : voices)
            if (v.active && v.pad == pad && v.loop)
                release (v, 0.03f);
    }

    void allOff() { for (auto& v : voices) if (v.active) release (v, 0.01f); }

    // Render into per-pad destinations: dest[out] (out 0 = main). Missing outs fall back to main.
    void render (std::array<juce::AudioBuffer<float>*, numPads + 1>& dest, int start, int num)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const int out = juce::jlimit (0, numPads, v.pad >= 0 ? params[(size_t) v.pad].out : 0);
            auto* buf = dest[(size_t) out] != nullptr ? dest[(size_t) out] : dest[0];
            if (buf == nullptr) { v.active = false; continue; }
            const auto& au = v.s->audio;
            const float* L = au.getReadPointer (0);
            const float* R = au.getReadPointer (juce::jmin (1, au.getNumChannels() - 1));
            float* oL = buf->getWritePointer (0, start);
            float* oR = buf->getWritePointer (juce::jmin (1, buf->getNumChannels() - 1), start);
            const bool mono = buf->getNumChannels() < 2;
            for (int i = 0; i < num; ++i)
            {
                if (v.reverse ? v.pos < v.a : v.pos >= v.b - 1.0)
                {
                    if (v.loop) v.pos = v.reverse ? v.b - 1.0 : v.a;
                    else { v.active = false; break; }
                }
                const int i0 = (int) v.pos;
                const float fr = (float) (v.pos - i0);
                const float l = L[i0] + fr * (L[i0 + 1] - L[i0]);
                const float r = R[i0] + fr * (R[i0 + 1] - R[i0]);
                // short fades where the region cuts into the sample, so moved start/end never click
                const double played = v.reverse ? (v.b - 1.0 - v.pos) : (v.pos - v.a);
                const double left = v.reverse ? (v.pos - v.a) : (v.b - 1.0 - v.pos);
                const bool cutIn = v.reverse || v.a > 0.5, cutOut = v.reverse || v.loop || v.b < (double) au.getNumSamples() - 1.5;
                float edge = 1.0f;
                if (cutIn)  edge = juce::jmin (edge, (float) (played / 32.0 + 1.0 / 32.0));
                if (cutOut) edge = juce::jmin (edge, (float) (left / 64.0));
                const float e = v.env * juce::jlimit (0.0f, 1.0f, edge);
                if (mono) oL[i] += 0.5f * e * (l * v.gl + r * v.gr);
                else { oL[i] += e * l * v.gl; oR[i] += e * r * v.gr; }
                v.env *= v.releasing ? v.relCoef : v.decayCoef;
                v.pos += v.reverse ? -v.step : v.step;
                if (v.env < 1.0e-4f) { v.active = false; break; }
            }
            if (! v.active) v.s.reset();
        }
    }

private:
    std::array<Voice, maxVoices> voices;
    double hostRate = 44100.0;
    int64_t clock = 0;

    void release (Voice& v, float seconds)
    {
        v.releasing = true;
        v.relCoef = (float) std::exp (-1.0 / (juce::jmax (0.001, (double) seconds) * hostRate / 4.0));
    }

    Voice& freeVoice()
    {
        for (auto& v : voices)
            if (! v.active) return v;
        auto* oldest = &voices[0];
        for (auto& v : voices)
            if (v.age < oldest->age) oldest = &v;
        oldest->s.reset();
        return *oldest;
    }
};
} // namespace tito
