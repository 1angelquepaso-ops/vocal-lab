#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// WOBBLE-D parameters.
// RULE: once shipped, an ID below never changes and is never removed, or FL automation clips break.
// New parameters may only be ADDED (at the end of a group).
namespace wb
{
namespace id
{
    // source / sampler
    inline constexpr const char* source = "src";
    inline constexpr const char* smpStart = "smpStart";
    inline constexpr const char* smpLength = "smpLength";
    inline constexpr const char* smpLoop = "smpLoop";
    inline constexpr const char* smpFadeIn = "smpFadeIn";
    inline constexpr const char* smpFadeOut = "smpFadeOut";
    inline constexpr const char* smpReverse = "smpReverse";
    inline constexpr const char* tune = "tune";
    inline constexpr const char* fine = "fine";
    inline constexpr const char* rootKey = "rootKey";
    // voice
    inline constexpr const char* voiceMode = "voiceMode";
    inline constexpr const char* glide = "glide";
    inline constexpr const char* attack = "attack";
    inline constexpr const char* decay = "decay";
    inline constexpr const char* sustain = "sustain";
    inline constexpr const char* release = "release";
    inline constexpr const char* unison = "unison";
    inline constexpr const char* detune = "detune";
    // filter
    inline constexpr const char* fltType = "fltType";
    inline constexpr const char* cutoff = "cutoff";
    inline constexpr const char* reso = "reso";
    inline constexpr const char* drive = "drive";
    inline constexpr const char* hq = "hq";
    // eq
    inline constexpr const char* lowCut = "lowCut";
    inline constexpr const char* lowGain = "lowGain";
    inline constexpr const char* midGain = "midGain";
    inline constexpr const char* midFreq = "midFreq";
    inline constexpr const char* highGain = "highGain";
    inline constexpr const char* highCut = "highCut";
    inline constexpr const char* boostFreq = "boostFreq";
    inline constexpr const char* boostGain = "boostGain";
    inline constexpr const char* boostQ = "boostQ";
    // sub + output
    inline constexpr const char* subLock = "subLock";
    inline constexpr const char* subFreq = "subFreq";
    inline constexpr const char* subLevel = "subLevel";
    inline constexpr const char* subOct = "subOct";
    inline constexpr const char* subOut = "subOut";
    inline constexpr const char* width = "width";
    inline constexpr const char* mono = "mono";
    inline constexpr const char* volume = "volume";
    inline constexpr const char* clip = "clip";
    // wobble
    inline constexpr const char* rate = "rate";
    inline constexpr const char* lfoMode = "lfoMode";
    inline constexpr const char* freeRate = "freeRate";
    inline constexpr const char* quantize = "quantize";
    inline constexpr const char* resync = "resync";
    inline constexpr const char* smooth = "smooth";
    inline constexpr const char* chaos = "chaos";
    inline constexpr const char* seed = "seed";
    inline constexpr const char* velDepth = "velDepth";
    // wobble targets
    inline constexpr const char* dCutoff = "dCutoff";
    inline constexpr const char* dVolume = "dVolume";
    inline constexpr const char* dPitch = "dPitch";
    inline constexpr const char* dDrive = "dDrive";
    inline constexpr const char* dBoost = "dBoost";
    // XY pad
    inline constexpr const char* xyX = "xyX";
    inline constexpr const char* xyY = "xyY";
    inline constexpr const char* xTarget = "xTarget";
    inline constexpr const char* yTarget = "yTarget";
    inline constexpr const char* xySnap = "xySnap";
    inline constexpr const char* xySpring = "xySpring";
    inline constexpr const char* xyGlide = "xyGlide";
}

inline const juce::StringArray sourceNames { "Sample", "Sine", "Saw", "Square", "Reese" };
inline const juce::StringArray rateNames { "2 Bars", "1 Bar", "1/2", "1/2T", "1/4D", "1/4", "1/4T",
                                           "1/8D", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
inline constexpr double rateBeats[] = { 8.0, 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 2.0 / 3.0,
                                        0.75, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
inline const juce::StringArray lfoModeNames { "Song-lock", "Note", "Free" };
inline const juce::StringArray quantizeNames { "Touch", "1/16", "1/8", "1/4", "Bar" };
inline constexpr double quantizeBeats[] = { 0.0, 0.25, 0.5, 1.0, 4.0 };
inline const juce::StringArray fltNames { "LP 24", "LP 12", "BP", "HP" };
inline const juce::StringArray voiceModeNames { "Mono", "Poly" };
inline const juce::StringArray subOctNames { "0 oct", "-1 oct" };
inline const juce::StringArray subOutNames { "Main", "Separate" };
inline const juce::StringArray xTargetNames { "RATE", "SHAPE", "SWING", "PITCH" };
inline const juce::StringArray yTargetNames { "DEPTH", "CHAOS", "FILTER", "DRIVE" };

// XY rate steps when Snap is on: pad X -1..+1 maps onto these multipliers
inline constexpr double rateSteps[] = { 0.25, 1.0 / 3.0, 0.5, 2.0 / 3.0, 1.0, 1.5, 2.0, 3.0, 4.0 };
inline const juce::StringArray rateStepNames { "/4", "/3", "/2", "/1.5", "x1", "x1.5", "x2", "x3", "x4" };

inline double xyRateMultiplier (double x, bool snap)
{
    x = juce::jlimit (-1.0, 1.0, x);
    if (snap)
        return rateSteps[juce::jlimit (0, 8, (int) std::lround ((x + 1.0) * 4.0))];
    return std::pow (4.0, x);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pid = [] (const char* s) { return ParameterID { s, 1 }; };

    auto fl = [&] (const char* i, const char* name, NormalisableRange<float> r, float def, const char* unit = "")
    {
        l.add (std::make_unique<AudioParameterFloat> (pid (i), name, r, def, AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto ch = [&] (const char* i, const char* name, const StringArray& c, int def)
    {
        l.add (std::make_unique<AudioParameterChoice> (pid (i), name, c, def));
    };
    auto bo = [&] (const char* i, const char* name, bool def)
    {
        l.add (std::make_unique<AudioParameterBool> (pid (i), name, def));
    };
    auto in = [&] (const char* i, const char* name, int lo, int hi, int def)
    {
        l.add (std::make_unique<AudioParameterInt> (pid (i), name, lo, hi, def));
    };
    auto skewed = [] (float lo, float hi, float centre)
    {
        NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    // source / sampler
    ch (id::source, "Source", sourceNames, 4);
    fl (id::smpStart, "Sample Start", { 0.0f, 1.0f }, 0.0f);
    fl (id::smpLength, "Sample Length", { 0.0f, 1.0f }, 1.0f);
    bo (id::smpLoop, "Sample Loop", true);
    fl (id::smpFadeIn, "Fade In", skewed (0.0f, 500.0f, 40.0f), 1.0f, "ms");
    fl (id::smpFadeOut, "Fade Out", skewed (0.0f, 1000.0f, 80.0f), 10.0f, "ms");
    bo (id::smpReverse, "Reverse", false);
    in (id::tune, "Tune", -24, 24, 0);
    fl (id::fine, "Fine", { -100.0f, 100.0f }, 0.0f, "ct");
    l.add (std::make_unique<AudioParameterInt> (pid (id::rootKey), "Root Key", 0, 127, 60,
               AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); })
                                            .withValueFromStringFunction ([] (const String& t) { return t.getIntValue(); })));
    // voice
    ch (id::voiceMode, "Voice Mode", voiceModeNames, 0);
    fl (id::glide, "Glide", skewed (0.0f, 1000.0f, 120.0f), 40.0f, "ms");
    fl (id::attack, "Attack", skewed (0.5f, 2000.0f, 60.0f), 2.0f, "ms");
    fl (id::decay, "Decay", skewed (1.0f, 4000.0f, 300.0f), 300.0f, "ms");
    fl (id::sustain, "Sustain", { 0.0f, 1.0f }, 1.0f);
    fl (id::release, "Release", skewed (1.0f, 4000.0f, 200.0f), 40.0f, "ms");
    in (id::unison, "Unison", 1, 4, 2);
    fl (id::detune, "Detune", { 0.0f, 1.0f }, 0.25f);
    // filter
    ch (id::fltType, "Filter Type", fltNames, 0);
    fl (id::cutoff, "Filter Cutoff", skewed (20.0f, 20000.0f, 600.0f), 180.0f, "Hz");
    fl (id::reso, "Filter Reso", { 0.0f, 1.0f }, 0.45f);
    fl (id::drive, "Drive", { 0.0f, 1.0f }, 0.35f);
    bo (id::hq, "HQ Drive", true);
    // eq
    fl (id::lowCut, "Low Cut", skewed (20.0f, 1000.0f, 120.0f), 20.0f, "Hz");
    fl (id::lowGain, "EQ Low", { -12.0f, 12.0f }, 0.0f, "dB");
    fl (id::midGain, "EQ Mid", { -12.0f, 12.0f }, 0.0f, "dB");
    fl (id::midFreq, "EQ Mid Freq", skewed (200.0f, 5000.0f, 1000.0f), 1000.0f, "Hz");
    fl (id::highGain, "EQ High", { -12.0f, 12.0f }, 0.0f, "dB");
    fl (id::highCut, "High Cut", skewed (1000.0f, 20000.0f, 6000.0f), 20000.0f, "Hz");
    fl (id::boostFreq, "Boost Freq", skewed (100.0f, 8000.0f, 1000.0f), 900.0f, "Hz");
    fl (id::boostGain, "Boost Gain", { 0.0f, 18.0f }, 0.0f, "dB");
    fl (id::boostQ, "Boost Q", skewed (0.5f, 8.0f, 2.0f), 2.0f);
    // sub + out
    bo (id::subLock, "Sub Lock", true);
    fl (id::subFreq, "Sub Freq", { 40.0f, 200.0f }, 90.0f, "Hz");
    fl (id::subLevel, "Sub Level", { 0.0f, 1.0f }, 0.7f);
    ch (id::subOct, "Sub Octave", subOctNames, 0);
    ch (id::subOut, "Sub Output", subOutNames, 0);
    fl (id::width, "Width", { 0.0f, 2.0f }, 1.0f);
    bo (id::mono, "Mono", false);
    fl (id::volume, "Volume", { -36.0f, 6.0f }, -6.0f, "dB");
    bo (id::clip, "Clipper", true);
    // wobble
    ch (id::rate, "Wobble Rate", rateNames, 8);
    ch (id::lfoMode, "Wobble Mode", lfoModeNames, 0);
    fl (id::freeRate, "Free Rate", skewed (0.05f, 30.0f, 3.0f), 3.0f, "Hz");
    ch (id::quantize, "Rate Quantize", quantizeNames, 0);
    fl (id::resync, "Re-sync", skewed (0.125f, 4.0f, 0.5f), 0.5f, "beats");
    fl (id::smooth, "Smooth", { 0.0f, 1.0f }, 0.1f);
    fl (id::chaos, "Chaos", { -1.0f, 1.0f }, 0.0f);
    in (id::seed, "Chaos Seed", 0, 9999, 1);
    fl (id::velDepth, "Velocity Depth", { 0.0f, 1.0f }, 0.0f);
    // wobble targets
    fl (id::dCutoff, "Wobble Cutoff", { -1.0f, 1.0f }, 0.75f);
    fl (id::dVolume, "Wobble Volume", { 0.0f, 1.0f }, 0.0f);
    fl (id::dPitch, "Wobble Pitch", { -24.0f, 24.0f }, 0.0f, "st");
    fl (id::dDrive, "Wobble Drive", { -1.0f, 1.0f }, 0.0f);
    fl (id::dBoost, "Wobble Boost", { -1.0f, 1.0f }, 0.0f);
    // XY pad
    fl (id::xyX, "XY X", { -1.0f, 1.0f }, 0.0f);
    fl (id::xyY, "XY Y", { -1.0f, 1.0f }, 0.0f);
    ch (id::xTarget, "XY X Target", xTargetNames, 0);
    ch (id::yTarget, "XY Y Target", yTargetNames, 0);
    bo (id::xySnap, "XY Snap", true);
    bo (id::xySpring, "XY Spring", false);
    fl (id::xyGlide, "XY Glide", skewed (0.0f, 500.0f, 60.0f), 30.0f, "ms");
    return l;
}
} // namespace wb
