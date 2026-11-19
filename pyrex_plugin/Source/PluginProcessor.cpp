#include "PluginProcessor.h"
#include "PluginEditor.h"

PyrexProcessor::PyrexProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool PyrexProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && l.getMainInputChannelSet() == out;
}

void PyrexProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    // audio passes straight through untouched; we only read the project tempo
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                hostBpm.store (*b);
}

void PyrexProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree st ("PYREX");
    st.setProperty ("v", 1, nullptr);
    st.setProperty ("stems", stems.joinIntoString ("\n"), nullptr);
    st.setProperty ("session", sessionDir.getFullPathName(), nullptr);
    st.setProperty ("analysis", juce::JSON::toString (analysis, true), nullptr);
    st.setProperty ("render", juce::JSON::toString (render, true), nullptr);
    st.setProperty ("bpmOverride", bpmOverride, nullptr);
    st.setProperty ("targetBpm", targetBpm, nullptr);
    st.setProperty ("bpmLock", bpmLock, nullptr);
    st.setProperty ("keyTranspose", keyTranspose, nullptr);
    st.setProperty ("keyName", keyName, nullptr);
    st.setProperty ("transpose", transpose, nullptr);
    st.setProperty ("nudge", nudge, nullptr);
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, dest);
}

void PyrexProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.isValid()) return;
    stems = juce::StringArray::fromLines (st["stems"].toString());
    stems.removeEmptyStrings();
    sessionDir = juce::File (st["session"].toString());
    analysis = juce::JSON::parse (st["analysis"].toString());
    render = juce::JSON::parse (st["render"].toString());
    bpmOverride = st.getProperty ("bpmOverride", 0.0);
    targetBpm = st.getProperty ("targetBpm", 0.0);
    bpmLock = st.getProperty ("bpmLock", true);
    keyTranspose = st.getProperty ("keyTranspose", false);
    keyName = st["keyName"].toString();
    transpose = st.getProperty ("transpose", 0);
    nudge = st.getProperty ("nudge", 0);
    if (analysis.isObject()) status = "Restored " + analysis["title"].toString() + ".";
}

juce::AudioProcessorEditor* PyrexProcessor::createEditor() { return new PyrexEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PyrexProcessor(); }
