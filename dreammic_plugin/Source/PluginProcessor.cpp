#include "PluginProcessor.h"
#include "PluginEditor.h"

DreamProcessor::DreamProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "DREAMMIC", layout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout DreamProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    StringArray mics, sources, pres, pats, comps;
    for (auto& m : dm::MICS) mics.add (m.name);
    sources.add ("Flat / unknown mic"); sources.add ("Samson C01U Pro (measured)"); sources.add ("Mojave MA-50");
    for (int s = 3; s < dm::NUM_SOURCES; ++s)
        sources.add (juce::String (dm::MICS[dm::sourceMicIndex (s)].style).replace (" style", ""));
    for (auto& p : dm::PRES) pres.add (p.name);
    for (auto* p : dm::PATTERN_NAMES) pats.add (p);
    for (auto* c : dm::COMP_NAMES) comps.add (c);
    // v1 parameters (IDs and ranges unchanged so old projects load the same)
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "source", 1 }, "Your mic", sources, 1));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mic", 1 }, "Mic", mics, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern", pats, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "pre", 1 }, "Preamp", pres, 0));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Quality", StringArray { "LIVE", "HQ" }, 0));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "distance", 1 }, "Distance",
                     NormalisableRange<float> (2.0f, 60.0f, 0.1f, 0.5f), 15.0f, AudioParameterFloatAttributes().withLabel ("cm")));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "warmth", 1 }, "Warmth",
                     NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "hpf", 1 }, "High-pass",
                     NormalisableRange<float> (20.0f, 300.0f, 1.0f, 0.5f), 20.0f, AudioParameterFloatAttributes().withLabel ("Hz")));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "air", 1 }, "Air",
                     NormalisableRange<float> (0.0f, 8.0f, 0.01f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
                     NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                     NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    // v2 (defaults = off, so a v1 project sounds exactly like before)
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "lowcut", 2 }, "Mic low cut", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "presence", 2 }, "Mic presence", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "comptype", 2 }, "Comp", comps, 0));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "comp", 2 }, "Comp amount",
                     NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "deess", 2 }, "De-ess",
                     NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "autoin", 2 }, "Auto input", false));
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { "match", 2 }, "Level match", false));
    return { v.begin(), v.end() };
}

dm::Params DreamProcessor::currentParams() const
{
    dm::Params p;
    auto get = [this] (const char* id) { return state.getRawParameterValue (id)->load(); };
    p.source = (int) get ("source"); p.mic = (int) get ("mic"); p.pattern = (int) get ("pattern");
    p.pre = (int) get ("pre"); p.mode = (int) get ("mode");
    p.distanceCm = get ("distance"); p.warmth = get ("warmth"); p.hpfHz = get ("hpf");
    p.airDb = get ("air"); p.inDb = get ("input"); p.outDb = get ("output");
    p.lowCut = get ("lowcut") > 0.5f; p.presence = get ("presence") > 0.5f;
    p.comp = (int) get ("comptype"); p.compAmt = get ("comp"); p.deess = get ("deess");
    p.autoIn = get ("autoin") > 0.5f; p.match = get ("match") > 0.5f;
    return p;
}

bool DreamProcessor::sameSound (const dm::Params& a, const dm::Params& b)
{
    return a.source == b.source && a.mic == b.mic && a.pattern == b.pattern && a.pre == b.pre
        && a.lowCut == b.lowCut && a.presence == b.presence && a.comp == b.comp && a.autoIn == b.autoIn
        && a.distanceCm == b.distanceCm && a.warmth == b.warmth && a.hpfHz == b.hpfHz && a.airDb == b.airDb
        && a.inDb == b.inDb && a.compAmt == b.compAmt && a.deess == b.deess;
}

bool DreamProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void DreamProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    engine.setParams (currentParams());
    prepared = true;
    matchValid = false;
    rebuildNow();
}

void DreamProcessor::rebuildNow()
{
    if (! prepared) return;
    const auto p = currentParams();
    if (p.mode == 1) engine.rebuildFir (p);
    if (p.match && (! matchValid || ! sameSound (p, matchedFor)))
    {
        engine.matchDb.store ((float) dm::Engine::measureMatchDb (p, rate));
        matchedFor = p;
        matchValid = true;
    }
    const int lat = engine.latencyFor (p.mode);
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }
}

void DreamProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto p = currentParams();
    engine.setParams (p);
    if (engine.needsFir (p) || engine.latencyFor (p.mode) != reportedLatency
        || (p.match && (! matchValid || ! sameSound (p, matchedFor))))
        triggerAsyncUpdate();
    engine.process (buffer);
}

// ------------------------------------------------------------------ presets
// Everything except Quality (LIVE/HQ is how you work, not the sound).
static const char* SOUND_IDS[] = { "source", "mic", "pattern", "pre", "distance", "warmth", "hpf", "air", "input", "output",
                                   "lowcut", "presence", "comptype", "comp", "deess", "autoin", "match" };

const std::vector<DreamProcessor::Factory>& DreamProcessor::factoryPresets()
{
    // mic / pre indices: see Models.h (append-only)
    static const std::vector<Factory> f = {
        { "Rap lead gloss",    { { "mic", 0 }, { "pre", 0 }, { "warmth", 3.5f }, { "hpf", 80 }, { "air", 2.0f }, { "comptype", 2 }, { "comp", 4.0f }, { "deess", 5.0f } } },
        { "Classic warm",      { { "mic", 1 }, { "pre", 1 }, { "warmth", 5.0f }, { "hpf", 70 }, { "comptype", 1 }, { "comp", 4.0f }, { "deess", 3.0f } } },
        { "Dark dynamic",      { { "mic", 10 }, { "pre", 1 }, { "warmth", 6.0f }, { "lowcut", 1 }, { "presence", 1 }, { "hpf", 60 }, { "comptype", 2 }, { "comp", 5.0f } } },
        { "Silky R&B",         { { "mic", 5 }, { "pre", 4 }, { "warmth", 3.0f }, { "hpf", 80 }, { "air", 1.5f }, { "comptype", 1 }, { "comp", 3.0f }, { "deess", 6.0f } } },
        { "Bright modern",     { { "mic", 11 }, { "pre", 9 }, { "warmth", 2.0f }, { "hpf", 90 }, { "air", 1.0f }, { "comptype", 3 }, { "comp", 4.0f }, { "deess", 4.0f } } },
        { "Abbey Road vintage", { { "mic", 6 }, { "pre", 7 }, { "warmth", 6.0f }, { "hpf", 70 }, { "comptype", 1 }, { "comp", 5.0f } } },
        { "Radio voice",       { { "mic", 13 }, { "pre", 3 }, { "warmth", 4.0f }, { "distance", 8.0f }, { "hpf", 70 }, { "comptype", 4 }, { "comp", 5.0f }, { "deess", 3.0f } } },
        { "Ribbon smooth",     { { "mic", 18 }, { "pre", 2 }, { "warmth", 4.0f }, { "distance", 12.0f }, { "hpf", 60 }, { "air", 3.0f }, { "comptype", 1 }, { "comp", 3.0f } } },
        { "Clean & true",      { { "mic", 3 }, { "pre", 10 }, { "warmth", 0.0f }, { "hpf", 60 } } },
        { "Demo grit",         { { "mic", 15 }, { "pre", 8 }, { "warmth", 7.0f }, { "hpf", 100 }, { "comptype", 2 }, { "comp", 6.0f } } },
    };
    return f;
}

juce::File DreamProcessor::userPresetDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DREAMMIC").getChildFile ("Presets");
}

juce::StringArray DreamProcessor::userPresetNames() const
{
    juce::StringArray s;
    for (auto& f : userPresetDir().findChildFiles (juce::File::findFiles, false, "*.dmpreset"))
        s.add (f.getFileNameWithoutExtension());
    s.sortNatural();
    return s;
}

void DreamProcessor::setParam (const juce::String& id, float value)
{
    if (auto* prm = state.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
}

juce::ValueTree DreamProcessor::soundTree() const
{
    juce::ValueTree t ("DREAMMIC_PRESET");
    t.setProperty ("version", 2, nullptr);
    for (auto* id : SOUND_IDS) t.setProperty (id, state.getRawParameterValue (id)->load(), nullptr);
    return t;
}

void DreamProcessor::applySoundTree (const juce::ValueTree& t)
{
    for (auto* id : SOUND_IDS)
        if (t.hasProperty (id)) setParam (id, (float) t[id]);
}

void DreamProcessor::loadFactory (int index)
{
    const auto& f = factoryPresets();
    if (index < 0 || index >= (int) f.size()) return;
    // start from the defaults (keep Your mic + Input: they belong to your recording, not the preset)
    for (auto* id : SOUND_IDS)
    {
        const juce::String s (id);
        if (s == "source" || s == "input" || s == "autoin" || s == "match") continue;
        if (auto* prm = state.getParameter (s)) setParam (s, prm->convertFrom0to1 (prm->getDefaultValue()));
    }
    for (auto& [id, v] : f[(size_t) index].values) setParam (id, v);
    presetName = f[(size_t) index].name;
}

bool DreamProcessor::loadUser (const juce::String& name)
{
    auto xml = juce::XmlDocument::parse (userPresetDir().getChildFile (name + ".dmpreset"));
    if (! xml) return false;
    applySoundTree (juce::ValueTree::fromXml (*xml));
    presetName = name;
    return true;
}

bool DreamProcessor::saveUser (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty()) return false;
    auto dir = userPresetDir();
    dir.createDirectory();
    auto xml = soundTree().createXml();
    if (! xml || ! xml->writeTo (dir.getChildFile (name + ".dmpreset"))) return false;
    presetName = name;
    return true;
}

// ------------------------------------------------------------------ project state
void DreamProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = state.copyState();
    st.setProperty ("presetName", presetName, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void DreamProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType()))
        {
            auto t = juce::ValueTree::fromXml (*xml);
            presetName = t.getProperty ("presetName", "").toString();
            state.replaceState (t);
            matchValid = false;
            if (prepared) triggerAsyncUpdate();
        }
}

juce::AudioProcessorEditor* DreamProcessor::createEditor() { return new DreamEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreamProcessor(); }
