#include "PluginProcessor.h"
#include "PluginEditor.h"

HarmonizerProcessor::HarmonizerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool HarmonizerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void HarmonizerProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
}

void HarmonizerProcessor::setPreview (juce::AudioBuffer<float>&& mono, double fileSampleRate)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    preview = std::move (mono);
    previewRate = fileSampleRate;
    pos = 0.0;
}

void HarmonizerProcessor::startPreview()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pos = 0.0;
    playing = preview.getNumSamples() > 0;
}

void HarmonizerProcessor::stopPreview() { playing = false; }

float HarmonizerProcessor::previewProgress() const
{
    const auto n = preview.getNumSamples();
    return n > 0 ? (float) juce::jlimit (0.0, 1.0, pos / n) : 0.0f;
}

void HarmonizerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (! playing.load())
        return;                                   // pure pass-through

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;

    const int n = preview.getNumSamples();
    const float* src = preview.getReadPointer (0);
    const double step = previewRate / hostRate;   // simple linear resampling
    const int chans = buffer.getNumChannels();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int i0 = (int) pos;
        if (i0 + 1 >= n) { playing = false; break; }
        const float frac = (float) (pos - i0);
        const float s = src[i0] + frac * (src[i0 + 1] - src[i0]);
        for (int c = 0; c < chans; ++c)
            buffer.addSample (c, i, s);
        pos += step;
    }
}

juce::AudioProcessorEditor* HarmonizerProcessor::createEditor()
{
    return new HarmonizerEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonizerProcessor();
}
