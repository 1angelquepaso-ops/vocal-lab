#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include "Dsp.h"
#include "Shape.h"
#include "Params.h"

namespace wb
{
struct SampleData
{
    juce::AudioBuffer<float> buf;   // always 2 channels
    double fs = 44100.0;
    juce::String name, path;
    double detectedNote = -1;
};

// Plain parameter snapshot, filled once per block by the processor.
struct Params
{
    int source = 4; float smpStart = 0, smpLength = 1; bool smpLoop = true; float fadeIn = 1, fadeOut = 10;
    bool reverse = false; int tune = 0; float fine = 0; int rootKey = 60;
    int voiceMode = 0; float glide = 40, attack = 2, decay = 300, sustain = 1, release = 40; int unison = 2; float detune = 0.25f;
    int fltType = 0; float cutoff = 180, reso = 0.45f, drive = 0.35f; bool hq = true;
    float lowCut = 20, lowGain = 0, midGain = 0, midFreq = 1000, highGain = 0, highCut = 20000;
    float boostFreq = 900, boostGain = 0, boostQ = 2;
    bool subLock = true; float subFreq = 90, subLevel = 0.7f; int subOct = 0, subOut = 0;
    float width = 1; bool mono = false; float volume = -6; bool clip = true;
    int rate = 8, lfoMode = 0, quantize = 0; float freeRate = 3, resync = 0.5f, smooth = 0.1f, chaos = 0; int seed = 1; float velDepth = 0;
    float dCutoff = 0.75f, dVolume = 0, dPitch = 0, dDrive = 0, dBoost = 0;
    float xyX = 0, xyY = 0; int xTarget = 0, yTarget = 0; bool xySnap = true; float xyGlide = 30;
};

struct HostInfo
{
    bool playing = false, hasPpq = false;
    double bpm = 120, ppq = 0;
};

class Engine
{
public:
    static constexpr int maxVoices = 8, polyLimit = 6, chunk = 256;

    // display / test hooks
    std::atomic<float> dispPos { 0 }, dispValue { 0 };
    std::vector<float>* lfoTrace = nullptr;

    float table[Shape::tableSize] {};
    float loopStart = 0, loopEnd = 1;

    Engine() { Shape::preset ("Sine").compile (table); }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        os.reset (new juce::dsp::Oversampling<float> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false));
        os->initProcessing ((size_t) chunk);
        work.setSize (2, chunk);
        for (auto& v : voices) v = Voice();
        held.clear();
        resetFilters();
        firstBlock = true;
    }

    void setSample (const SampleData* s) { sample = s; }
    const SampleData* currentSample() const { return sample; }

    void process (juce::AudioBuffer<float>& out, juce::AudioBuffer<float>* subBus, const juce::MidiBuffer& midi,
                  const Params& p, const HostInfo& host)
    {
        const int total = out.getNumSamples();
        out.clear();
        if (subBus) subBus->clear();
        beginBlock (p, host, total);

        auto it = midi.begin();
        for (int start = 0; start < total; start += chunk)
        {
            const int n = juce::jmin (chunk, total - start);
            work.clear();
            for (int i = 0; i < n; ++i)
            {
                const int pos = start + i;
                while (it != midi.end() && (*it).samplePosition <= pos)
                {
                    handleMidi ((*it).getMessage(), p);
                    ++it;
                }
                controlTick (p, host, pos, i);
                renderVoices (p, i);
            }
            driveStage (p, n);
            finishStage (p, out, subBus, start, n);
        }
        while (it != midi.end()) { handleMidi ((*it).getMessage(), p); ++it; }
        endBlock (host, total);
    }

private:
    // ------------------------------------------------------------------ voices
    struct Voice
    {
        bool active = false, gate = false, killing = false;
        int note = 0; float vel = 1;
        juce::ADSR env;
        double pitch = 60, pitchTarget = 60;
        double ph[4] {}, ph2[4] {}, smp[4] {};
        bool smpDone[4] {};
        double subPh = 0;
        float kill = 1, fadeCount = 0;
        uint64_t age = 0;
    };

    double fs = 44100;
    std::array<Voice, maxVoices> voices;
    int monoVoice = -1;
    std::vector<int> held;
    uint64_t ageCounter = 0;
    float lastVelocity = 1;
    float bend = 0; Smooth bendS;
    const SampleData* sample = nullptr;

    // per-block derived
    double bps = 0;                 // beats per sample
    int regA = 0, regLen = 1; double smpRatioBase = 1;
    float fadeInSamples = 48, fadeOutSrc = 480, xfSrc = 0;
    float glideC = 1, killStep = 0.01f;

    // per-sample control arrays (one chunk)
    float cutHz[chunk] {}, resK[chunk] {}, drv[chunk] {}, volG[chunk] {}, boostHz[chunk] {}, subBuf[chunk] {};
    float pitchMod = 0;
    juce::AudioBuffer<float> work;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;

    // smoothers
    Smooth sCut, sRes, sDrive, sDCut, sDVol, sDPitch, sDDrive, sDBoost, sBoostF, sBoostG, sWidth, sVol, sSub, sChaos;
    Smooth xAmt[4], yAmt[4];
    float eqLowCut = 20, eqLow = 0, eqMid = 0, eqMidF = 1000, eqHigh = 0, eqHighCut = 20000, eqSubF = 90, eqBoostQ = 2;

    // filters (index 0 = left, 1 = right)
    Svf flt1[2], flt2[2], subHp1[2], subHp2[2], lowCutF[2], lowSh[2], midB[2], highSh[2], highCutF[2], boostB[2];

    // wobble clock
    double ph = 0, cyclePos = 0, appliedBeats = 0.5, internalBeats = 0, prevGridPos = 0;
    double lastPpqEnd = 0;
    bool wasPlaying = false, firstBlock = true, hardSetPending = false, phraseReset = false;
    float slewState = 0.5f, slewState2 = 0.5f, lastFinal = 0.5f;
    double xfOldPos = 0, xfOldCps = 0, lastCps = 0; int xfCount = 1 << 30, xfLen = 384;
    double blockPpq = 0;

    void resetFilters()
    {
        for (int c = 0; c < 2; ++c)
            for (auto* f : { &flt1[c], &flt2[c], &subHp1[c], &subHp2[c], &lowCutF[c], &lowSh[c], &midB[c], &highSh[c], &highCutF[c], &boostB[c] })
                f->reset();
    }

    static float detuneOffset (int u, int U)
    {
        static const float t[5][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { -1, 1, 0, 0 }, { -1, 0, 1, 0 }, { -1, -1.0f / 3, 1.0f / 3, 1 } };
        return t[U][u];
    }
    static float panPos (int u, int U)
    {
        static const float t[5][4] = { { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, { -0.7f, 0.7f, 0, 0 }, { -0.8f, 0, 0.8f, 0 }, { -0.9f, -0.3f, 0.3f, 0.9f } };
        return t[U][u];
    }

    // ------------------------------------------------------------------ block setup
    void beginBlock (const Params& p, const HostInfo& host, int total)
    {
        juce::ignoreUnused (total);
        const double bpm = host.bpm > 1 ? host.bpm : 120.0;
        bps = bpm / 60.0 / fs;

        const float knobMs = 15.0f;
        for (auto* s : { &sCut, &sRes, &sDrive, &sDCut, &sDVol, &sDPitch, &sDDrive, &sDBoost, &sBoostF, &sBoostG, &sWidth, &sVol, &sSub, &sChaos })
            s->setTime (knobMs, fs);
        bendS.setTime (10.0f, fs);
        sCut.target = std::log2 (p.cutoff);   sRes.target = p.reso;  sDrive.target = p.drive;
        sDCut.target = p.dCutoff; sDVol.target = p.dVolume; sDPitch.target = p.dPitch; sDDrive.target = p.dDrive; sDBoost.target = p.dBoost;
        sBoostF.target = std::log2 (p.boostFreq); sBoostG.target = p.boostGain;
        sWidth.target = p.mono ? 0.0f : p.width;
        sVol.target = juce::Decibels::decibelsToGain (p.volume);
        sSub.target = p.subLock ? p.subLevel : 0.0f;
        sChaos.target = p.chaos;
        for (int t = 0; t < 4; ++t)
        {
            const bool xs = p.xTarget == t, ys = p.yTarget == t;
            xAmt[t].target = xs ? p.xyX : 0.0f;  xAmt[t].setTime (xs ? p.xyGlide : juce::jmax (p.xyGlide, 50.0f), fs);
            yAmt[t].target = ys ? p.xyY : 0.0f;  yAmt[t].setTime (ys ? p.xyGlide : juce::jmax (p.xyGlide, 50.0f), fs);
        }
        if (firstBlock)
        {
            for (auto* s : { &sCut, &sRes, &sDrive, &sDCut, &sDVol, &sDPitch, &sDDrive, &sDBoost, &sBoostF, &sBoostG, &sWidth, &sVol, &sSub, &sChaos })
                s->reset (s->target);
            for (int t = 0; t < 4; ++t) { xAmt[t].reset (xAmt[t].target); yAmt[t].reset (yAmt[t].target); }
            appliedBeats = rateBeats[juce::jlimit (0, 12, p.rate)];
            eqLowCut = p.lowCut; eqLow = p.lowGain; eqMid = p.midGain; eqMidF = p.midFreq; eqHigh = p.highGain;
            eqHighCut = p.highCut; eqSubF = p.subFreq; eqBoostQ = p.boostQ;
        }
        // static EQ: glide per block
        auto glideTo = [] (float& v, float t, float a) { v += (t - v) * a; };
        const float a = firstBlock ? 1.0f : 0.35f;
        glideTo (eqLowCut, p.lowCut, a); glideTo (eqLow, p.lowGain, a); glideTo (eqMid, p.midGain, a);
        glideTo (eqMidF, p.midFreq, a); glideTo (eqHigh, p.highGain, a); glideTo (eqHighCut, p.highCut, a);
        glideTo (eqSubF, p.subFreq, a); glideTo (eqBoostQ, p.boostQ, a);
        for (int c = 0; c < 2; ++c)
        {
            lowCutF[c].setLowHigh (eqLowCut, fs, 1.41421f);
            lowSh[c].setLowShelf (120.0, fs, eqLow);
            midB[c].setBell (eqMidF, fs, 0.9f, eqMid);
            highSh[c].setHighShelf (6000.0, fs, eqHigh);
            highCutF[c].setLowHigh (eqHighCut, fs, 1.41421f);
            subHp1[c].setLowHigh (eqSubF, fs, 1.41421f);
            subHp2[c].setLowHigh (eqSubF, fs, 1.41421f);
        }

        // voices
        juce::ADSR::Parameters ap { p.attack * 0.001f, p.decay * 0.001f, p.sustain, p.release * 0.001f };
        for (auto& v : voices) { v.env.setSampleRate (fs); v.env.setParameters (ap); }
        glideC = p.glide <= 0.5f ? 1.0f : (float) (1.0 - std::exp (-1.0 / (0.3 * p.glide * 0.001 * fs)));
        killStep = (float) (1.0 / (0.005 * fs));
        fadeInSamples = (float) juce::jmax (1.0, p.fadeIn * 0.001 * fs);
        xfLen = juce::jmax (8, (int) (0.008 * fs));

        // sample region
        if (sample != nullptr && sample->buf.getNumSamples() > 64)
        {
            const int N = sample->buf.getNumSamples();
            regA = juce::jlimit (0, N - 64, (int) (p.smpStart * N));
            regLen = juce::jmax (64, (int) (p.smpLength * (N - regA)));
            smpRatioBase = sample->fs / fs;
            fadeOutSrc = (float) juce::jmax (1.0, p.fadeOut * 0.001 * sample->fs);
            xfSrc = juce::jmin (fadeOutSrc, regLen * 0.5f);
        }

        // transport handling for the wobble clock
        const bool songLock = p.lfoMode == 0 && host.playing && host.hasPpq;
        blockPpq = host.ppq;
        if (songLock)
        {
            if (! wasPlaying || firstBlock) hardSetPending = true;
            else if (std::abs (host.ppq - lastPpqEnd) > 1.0e-3) hardSetPending = true;   // loop / jump in the playlist
        }
        firstBlock = false;
    }

    void endBlock (const HostInfo& host, int total)
    {
        wasPlaying = host.playing && host.hasPpq;
        lastPpqEnd = host.ppq + total * bps;
    }

    // ------------------------------------------------------------------ MIDI
    int freeVoice()
    {
        for (int i = 0; i < maxVoices; ++i) if (! voices[(size_t) i].active) return i;
        int best = 0; uint64_t a = UINT64_MAX;
        for (int i = 0; i < maxVoices; ++i) if (voices[(size_t) i].age < a) { a = voices[(size_t) i].age; best = i; }
        return best;
    }

    void startVoice (int idx, int note, float vel, const Params& p)
    {
        auto& v = voices[(size_t) idx];
        v = Voice();
        v.env.setSampleRate (fs);
        v.env.setParameters ({ p.attack * 0.001f, p.decay * 0.001f, p.sustain, p.release * 0.001f });
        v.active = v.gate = true;
        v.note = note; v.vel = vel;
        v.pitch = v.pitchTarget = note;
        for (int u = 0; u < 4; ++u) { v.ph[u] = std::fmod (u * 0.27, 1.0); v.ph2[u] = std::fmod (u * 0.27 + 0.5, 1.0); }
        v.age = ++ageCounter;
        v.env.noteOn();
    }

    void handleMidi (const juce::MidiMessage& m, const Params& p)
    {
        if (m.isNoteOn())
        {
            const int note = m.getNoteNumber();
            const float vel = m.getFloatVelocity();
            const bool phraseStart = held.empty();
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            held.push_back (note);
            lastVelocity = vel;
            if (phraseStart) phraseReset = true;

            if (p.voiceMode == 0)
            {
                if (monoVoice >= 0 && voices[(size_t) monoVoice].active && voices[(size_t) monoVoice].gate)
                {
                    auto& v = voices[(size_t) monoVoice];
                    v.pitchTarget = note; v.note = note; v.vel = vel;
                    if (p.glide <= 0.5f) v.pitch = note;
                }
                else
                {
                    if (monoVoice >= 0 && voices[(size_t) monoVoice].active) voices[(size_t) monoVoice].killing = true;
                    const int idx = freeVoice();
                    startVoice (idx, note, vel, p);
                    monoVoice = idx;
                }
            }
            else
            {
                int count = 0;
                for (auto& v : voices) if (v.active && ! v.killing) ++count;
                if (count >= polyLimit)
                {
                    int o = -1; uint64_t a = UINT64_MAX;
                    for (int i = 0; i < maxVoices; ++i)
                        if (voices[(size_t) i].active && ! voices[(size_t) i].killing && voices[(size_t) i].age < a) { a = voices[(size_t) i].age; o = i; }
                    if (o >= 0) voices[(size_t) o].killing = true;
                }
                startVoice (freeVoice(), note, vel, p);
            }
        }
        else if (m.isNoteOff())
        {
            const int note = m.getNoteNumber();
            held.erase (std::remove (held.begin(), held.end(), note), held.end());
            if (p.voiceMode == 0)
            {
                if (monoVoice >= 0 && voices[(size_t) monoVoice].active && voices[(size_t) monoVoice].gate)
                {
                    auto& v = voices[(size_t) monoVoice];
                    if (! held.empty()) { v.pitchTarget = held.back(); v.note = held.back(); if (p.glide <= 0.5f) v.pitch = v.pitchTarget; }
                    else { v.gate = false; v.env.noteOff(); }
                }
            }
            else
            {
                for (auto& v : voices)
                    if (v.active && v.gate && v.note == note) { v.gate = false; v.env.noteOff(); }
            }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.clear();
            for (auto& v : voices) if (v.active) { v.gate = false; v.env.noteOff(); }
        }
        else if (m.isPitchWheel())
        {
            bendS.target = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
        }
    }

    // ------------------------------------------------------------------ wobble clock + targets (per sample)
    float lookup (double x) const
    {
        const double fi = x * Shape::tableSize;
        int i0 = (int) fi; const float fr = (float) (fi - i0);
        i0 = ((i0 % Shape::tableSize) + Shape::tableSize) % Shape::tableSize;
        const int i1 = (i0 + 1) % Shape::tableSize;
        return table[i0] + (table[i1] - table[i0]) * fr;
    }

    float shapeValue (double cyc, float xSwing, float xShape, float c, int seed, double& xsOut) const
    {
        // swing (pairs of cycles), loop region, shape
        const double p2 = cyc - 2.0 * std::floor (cyc * 0.5);
        const double m = 1.0 + 0.33 * xSwing;
        double local = p2 < m ? p2 / m : (p2 - m) / (2.0 - m);
        local -= std::floor (local);
        const double xs = loopStart + local * (loopEnd - loopStart);
        xsOut = xs;
        float v = lookup (xs);

        // XY shape bend: + sharper/squarer, - rounder/sine-like
        if (xShape > 0.001f)
        {
            const float k = 12.0f * xShape;
            v = 0.5f + 0.5f * std::tanh (k * (v - 0.5f) * 2.0f) / std::tanh (k);
        }
        else if (xShape < -0.001f)
        {
            const float sine = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * local));
            v = v + (sine - v) * (-xShape);
        }

        // chaos (deterministic: same every play / render)
        if (c > 0.001f)
        {
            const int64_t si = (int64_t) std::floor (cyc * 8.0);
            const float r = hashNoise (si, seed), r2 = hashNoise (si, seed + 7919);
            v = v + (((r + 1.0f) * 0.5f) - v) * 0.5f * c;
            if (r2 > 1.0f - 0.8f * c) v = 0.0f;
        }
        else if (c < -0.001f)
        {
            const float a = -c;
            const float n1 = smoothNoise (cyc * 0.5, seed), n2 = smoothNoise (cyc * 1.7, seed + 13);
            v = 0.5f + (v - 0.5f) * (1.0f + 0.7f * a * n1) + 0.2f * a * n2;
        }
        return juce::jlimit (0.0f, 1.0f, v);
    }

    void controlTick (const Params& p, const HostInfo& host, int pos, int i)
    {
        for (int t = 0; t < 4; ++t) { xAmt[t].next(); yAmt[t].next(); }
        const float xRate = xAmt[0].v, xShape = xAmt[1].v, xSwing = xAmt[2].v, xPitch = xAmt[3].v;
        const float yDepth = yAmt[0].v, yChaos = yAmt[1].v, yFilter = yAmt[2].v, yDrive = yAmt[3].v;

        const bool songLock = p.lfoMode == 0 && host.playing && host.hasPpq;
        const double ppqNow = blockPpq + pos * bps;
        const double mult = xyRateMultiplier (xRate, p.xySnap);
        const double targetBeats = rateBeats[juce::jlimit (0, 12, p.rate)] / mult;
        const double q = quantizeBeats[juce::jlimit (0, 4, p.quantize)];

        bool hardSet = false;
        if (hardSetPending && songLock) { hardSet = true; hardSetPending = false; appliedBeats = targetBeats; }

        // phrase restart (Note mode, or Song-lock while FL is stopped)
        if (phraseReset)
        {
            phraseReset = false;
            if (p.lfoMode == 1 || (p.lfoMode == 0 && ! songLock))
            {
                ph = 0; internalBeats = 0; prevGridPos = 0; appliedBeats = targetBeats;
                hardSet = true;
            }
        }

        // which beats-per-cycle is in force
        double beats = targetBeats;
        if (q > 0.0)
        {
            const double gridPos = songLock ? ppqNow : internalBeats;
            const bool crossed = std::floor ((gridPos + 1e-9) / q) != std::floor ((prevGridPos + 1e-9) / q);
            if (crossed && appliedBeats != targetBeats)
            {
                appliedBeats = targetBeats;
                if (songLock) hardSet = true;
            }
            prevGridPos = gridPos;
            beats = appliedBeats;
        }
        else appliedBeats = targetBeats;
        internalBeats += bps;

        // advance: speed changes, position never jumps
        const double prevCyclePos = cyclePos;
        const double cpsNow = p.lfoMode == 2 ? p.freeRate * mult / fs : bps / beats;
        ph += cpsNow;

        if (songLock)
        {
            const double target = ppqNow / beats;
            if (hardSet) ph = target;
            else
            {
                const double d = target - ph;
                const double err = d - std::round (d);
                ph += err * juce::jmin (1.0, bps / juce::jmax (0.01, (double) p.resync));   // slide back onto the grid
            }
            cyclePos = ph + std::round (target - ph);
        }
        else cyclePos = ph;

        // after a hard re-sync (quantize, playlist loop, phrase restart) the old wobble keeps running
        // for a few ms and crossfades into the new one, so nothing ever jumps
        if (hardSet) { xfOldPos = prevCyclePos; xfOldCps = lastCps; xfCount = 0; }
        lastCps = cpsNow;

        const float c = juce::jlimit (-1.0f, 1.0f, sChaos.next() + yChaos);
        double xs = 0;
        float v = shapeValue (cyclePos, xSwing, xShape, c, p.seed, xs);
        if (xfCount < xfLen)
        {
            xfOldPos += xfOldCps;
            double xsOld = 0;
            const float vOld = shapeValue (xfOldPos, xSwing, xShape, c, p.seed, xsOld);
            const float w = (float) (0.5 + 0.5 * std::cos (kPi * xfCount / xfLen));
            v = v + (vOld - v) * w;
            ++xfCount;
        }

        // smooth: two-pole slew (min 1 ms per pole) so even hard steps / gates never pop
        const float slewMs = 1.0f + p.smooth * 20.0f;
        const float sc = (float) (1.0 - std::exp (-1.0 / (slewMs * 0.001 * fs)));
        slewState += (v - slewState) * sc;
        slewState2 += (slewState - slewState2) * sc;
        float L = slewState2;
        L = juce::jlimit (0.0f, 1.0f, L);
        lastFinal = L;
        if (lfoTrace) lfoTrace->push_back (L);
        if (pos == 0) { dispPos = (float) xs; dispValue = L; }

        // targets
        const float vel = lastVelocity;
        const float depth = (1.0f + yDepth) * (1.0f + (vel - 1.0f) * p.velDepth);
        const float cutOct = sDCut.next() * 6.0f * L * depth + yFilter * 3.0f;
        const float baseCut = std::exp2 (sCut.next());
        cutHz[i] = juce::jlimit (20.0f, (float) (fs * 0.45), baseCut * std::exp2 (cutOct));
        resK[i] = juce::jlimit (0.0f, 1.0f, sRes.next() + juce::jmax (0.0f, yFilter) * 0.15f);
        drv[i] = juce::jlimit (0.0f, 1.0f, sDrive.next() + sDDrive.next() * L * depth + yDrive * 0.5f);
        volG[i] = 1.0f - juce::jlimit (0.0f, 1.0f, sDVol.next() * depth) * (1.0f - L);
        boostHz[i] = juce::jlimit (30.0f, 16000.0f, std::exp2 (sBoostF.next()) * std::exp2 (sDBoost.next() * 4.0f * L * depth));
        pitchMod = sDPitch.next() * L * depth + xPitch * 12.0f;
    }

    // ------------------------------------------------------------------ voices (per sample)
    inline float readSample (int ch, double u, bool rev) const
    {
        const auto& b = sample->buf;
        const int N = b.getNumSamples();
        double idx = rev ? (regA + regLen - 1 - u) : (regA + u);
        idx = juce::jlimit (0.0, (double) (N - 1), idx);
        const int i1 = (int) idx; const float t = (float) (idx - i1);
        const float* d = b.getReadPointer (ch);
        const float y0 = d[juce::jmax (0, i1 - 1)], y1 = d[i1], y2 = d[juce::jmin (N - 1, i1 + 1)], y3 = d[juce::jmin (N - 1, i1 + 2)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void renderVoices (const Params& p, int i)
    {
        const float bendNow = bendS.next();
        float L = 0, R = 0, S = 0;
        const int U = juce::jlimit (1, 4, p.unison);
        const float uGain = 1.0f / std::sqrt ((float) U);
        const bool useSample = p.source == 0 && sample != nullptr && sample->buf.getNumSamples() > 64;

        for (auto& v : voices)
        {
            if (! v.active) continue;
            float e = v.env.getNextSample();
            if (v.killing)
            {
                v.kill -= killStep;
                if (v.kill <= 0.0f) { v.active = false; continue; }
                e *= v.kill;
            }
            if (! v.env.isActive()) { v.active = false; continue; }

            v.pitch += (v.pitchTarget - v.pitch) * glideC;
            const double n = v.pitch + bendNow + p.tune + p.fine * 0.01 + pitchMod;
            const float amp = e * (0.6f + 0.4f * v.vel);
            const float fadeIn = juce::jmin (1.0f, v.fadeCount / fadeInSamples);
            v.fadeCount += 1.0f;

            for (int u = 0; u < U; ++u)
            {
                const double det = detuneOffset (u, U) * p.detune * 0.5;      // semitones (max +-0.5)
                const float pan = panPos (u, U);
                const float gl = std::cos ((pan + 1.0f) * 0.25f * (float) kPi) * 1.41421f;
                const float gr = std::sin ((pan + 1.0f) * 0.25f * (float) kPi) * 1.41421f;
                float sl = 0, sr = 0;
                if (useSample)
                {
                    if (v.smpDone[u]) continue;
                    const double ratio = std::exp2 ((n + det - p.rootKey) / 12.0) * smpRatioBase;
                    double u0 = v.smp[u];
                    float g = fadeIn;
                    if (p.smpLoop)
                    {
                        // tail fades into the head of the region; after the wrap playback continues just after the head
                        if (xfSrc > 1.0f && u0 > regLen - xfSrc)
                        {
                            const double h = u0 - (regLen - xfSrc);
                            const float w = (float) (h / xfSrc);
                            const float a = std::cos (w * 0.5f * (float) kPi), b = std::sin (w * 0.5f * (float) kPi);
                            sl = readSample (0, u0, p.reverse) * a + readSample (0, h, p.reverse) * b;
                            sr = readSample (1, u0, p.reverse) * a + readSample (1, h, p.reverse) * b;
                        }
                        else { sl = readSample (0, u0, p.reverse); sr = readSample (1, u0, p.reverse); }
                        u0 += ratio;
                        if (u0 >= regLen) u0 = u0 - regLen + xfSrc;
                    }
                    else
                    {
                        g *= juce::jlimit (0.0f, 1.0f, (float) ((regLen - u0) / fadeOutSrc));
                        sl = readSample (0, u0, p.reverse) * g; sr = readSample (1, u0, p.reverse) * g;
                        g = 1.0f;
                        u0 += ratio;
                        if (u0 >= regLen) v.smpDone[u] = true;
                    }
                    v.smp[u] = u0;
                    sl *= g; sr *= g;
                }
                else
                {
                    const double f = 440.0 * std::exp2 ((n + det - 69.0) / 12.0);
                    const double dt = juce::jlimit (0.0, 0.49, f / fs);
                    double& phs = v.ph[u];
                    float s = 0;
                    switch (p.source)
                    {
                        case 1: s = (float) std::sin (2.0 * kPi * phs); break;
                        case 3:
                        {
                            double q = phs < 0.5 ? 1.0 : -1.0;
                            q += polyBlep (phs, dt);
                            q -= polyBlep (std::fmod (phs + 0.5, 1.0), dt);
                            s = (float) q * 0.8f; break;
                        }
                        case 4:
                        {
                            const double dt2 = dt * std::exp2 (0.14 / 12.0);
                            double& p2 = v.ph2[u];
                            const double a = 2.0 * phs - 1.0 - polyBlep (phs, dt);
                            const double b = 2.0 * p2 - 1.0 - polyBlep (p2, dt2);
                            s = (float) (a + b) * 0.5f;
                            p2 += dt2; if (p2 >= 1.0) p2 -= 1.0;
                            break;
                        }
                        default: s = (float) (2.0 * phs - 1.0 - polyBlep (phs, dt)); break;
                    }
                    phs += dt; if (phs >= 1.0) phs -= 1.0;
                    sl = sr = s * fadeIn;
                }
                L += sl * gl * uGain * amp;
                R += sr * gr * uGain * amp;
            }

            // clean mono sub (Sub Lock)
            if (p.subLock)
            {
                const double fsub = 440.0 * std::exp2 ((n - 69.0) / 12.0) * (p.subOct == 1 ? 0.5 : 1.0);
                S += (float) std::sin (2.0 * kPi * v.subPh) * e * (0.6f + 0.4f * v.vel);
                v.subPh += fsub / fs; if (v.subPh >= 1.0) v.subPh -= 1.0;
            }
        }
        work.setSample (0, i, L * 0.5f);
        work.setSample (1, i, R * 0.5f);
        subBuf[i] = S;
    }

    // ------------------------------------------------------------------ drive
    static inline float shaper (float x, float d)
    {
        const float g = 1.0f + 30.0f * d * d;
        return std::tanh (g * x) * (0.5f / std::tanh (0.5f * g));
    }

    void driveStage (const Params& p, int n)
    {
        if (p.hq && os)
        {
            juce::dsp::AudioBlock<float> blk (work.getArrayOfWritePointers(), 2, (size_t) n);
            auto up = os->processSamplesUp (blk);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                float* d = up.getChannelPointer (c);
                for (size_t j = 0; j < up.getNumSamples(); ++j) d[j] = shaper (d[j], drv[j >> 1]);
            }
            os->processSamplesDown (blk);
        }
        else
        {
            for (int c = 0; c < 2; ++c)
            {
                float* d = work.getWritePointer (c);
                for (int j = 0; j < n; ++j) d[j] = shaper (d[j], drv[j]);
            }
        }
    }

    // ------------------------------------------------------------------ filter, eq, width, sub, out
    void finishStage (const Params& p, juce::AudioBuffer<float>& out, juce::AudioBuffer<float>* subBus, int start, int n)
    {
        const bool doLowCut = eqLowCut > 20.5f, doHighCut = eqHighCut < 19900.0f;
        const bool doLow = std::abs (eqLow) > 0.01f, doMid = std::abs (eqMid) > 0.01f, doHigh = std::abs (eqHigh) > 0.01f;
        const bool separateSub = p.subOut == 1 && subBus != nullptr && subBus->getNumChannels() >= 2;
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;

        for (int j = 0; j < n; ++j)
        {
            // filter coefficient (shared by both channels)
            const float g = (float) std::tan (kPi * cutHz[j] / fs);
            const float r = resK[j];
            float s[2] = { work.getSample (0, j), work.getSample (1, j) };
            const float bg = sBoostG.next();
            const bool doBoost = bg > 0.01f;
            if (doBoost) for (int c = 0; c < 2; ++c) boostB[c].setBell (boostHz[j], fs, eqBoostQ, bg);

            for (int c = 0; c < 2; ++c)
            {
                float x = s[c];
                switch (p.fltType)
                {
                    case 0:
                        flt1[c].k = 1.848f;                      flt1[c].setG (g);
                        flt2[c].k = 0.765f + (0.05f - 0.765f) * r; flt2[c].setG (g);
                        x = flt2[c].lp (flt1[c].lp (x)); break;
                    case 1:
                        flt1[c].k = 1.414f + (0.05f - 1.414f) * r; flt1[c].setG (g);
                        x = flt1[c].lp (x); break;
                    case 2:
                        flt1[c].k = 1.414f + (0.1f - 1.414f) * r; flt1[c].setG (g);
                        x = flt1[c].bp (x) * flt1[c].k * 1.5f; break;
                    default:
                        flt1[c].k = 1.414f + (0.05f - 1.414f) * r; flt1[c].setG (g);
                        x = flt1[c].hp (x); break;
                }
                if (p.subLock) x = subHp2[c].hp (subHp1[c].hp (x));
                if (doLowCut) x = lowCutF[c].hp (x);
                if (doLow) x = lowSh[c].lowShelf (x);
                if (doMid) x = midB[c].bell (x);
                if (doHigh) x = highSh[c].highShelf (x);
                if (doHighCut) x = highCutF[c].lp (x);
                if (doBoost) x = boostB[c].bell (x);
                s[c] = x * volG[j];
            }
            // width / mono
            const float w = sWidth.next();
            const float M = 0.5f * (s[0] + s[1]), Sd = 0.5f * (s[0] - s[1]) * w;
            float l = M + Sd, rr = M - Sd;
            const float og = sVol.next();
            const float sub = subBuf[j] * sSub.next() * 0.7f;
            l *= og; rr *= og;
            if (separateSub)
            {
                float so = sub * og;
                if (p.clip) so = softClip (so);
                subBus->setSample (0, start + j, so);
                subBus->setSample (1, start + j, so);
            }
            else { l += sub * og; rr += sub * og; }
            if (p.clip) { l = softClip (l); rr = softClip (rr); }
            oL[start + j] = l;
            if (oR) oR[start + j] = rr;
        }
    }
};
} // namespace wb
