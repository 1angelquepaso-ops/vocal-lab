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
    StringArray mics, sources, pres, pats;
    for (auto& m : dm::MICS) mics.add (m.name);
    for (auto* s : dm::SOURCE_NAMES) sources.add (s);
    for (auto& p : dm::PRES) pres.add (p.name);
    for (auto* p : dm::PATTERN_NAMES) pats.add (p);
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
    return p;
}

bool DreamProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return l.getMainInputChannelSet() == out;
}

void DreamProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    engine.setParams (currentParams());
    prepared = true;
    rebuildNow();
}

void DreamProcessor::rebuildNow()
{
    if (! prepared) return;
    const auto p = currentParams();
    if (p.mode == 1) engine.rebuildFir (p);
    const int lat = p.mode == 1 ? engine.latencyFor (1) : engine.latencyFor (0);
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
    if (engine.needsFir (p) || (p.mode == 1 ? engine.latencyFor (1) : engine.latencyFor (0)) != reportedLatency)
        triggerAsyncUpdate();
    engine.process (buffer);
}

void DreamProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void DreamProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType())) state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* DreamProcessor::createEditor() { return new DreamEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DreamProcessor(); }
