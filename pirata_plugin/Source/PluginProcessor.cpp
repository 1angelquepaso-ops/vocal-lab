#include "PluginProcessor.h"
#include "PluginEditor.h"

PirataProcessor::PirataProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool PirataProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void PirataProcessor::prepareToPlay (double sampleRate, int)
{
    hostRate = sampleRate;
}

void PirataProcessor::setPreview (juce::AudioBuffer<float>&& audio, double fileSampleRate)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    preview = std::move (audio);
    previewRate = fileSampleRate;
    pos = juce::jmin (pos, (double) preview.getNumSamples());   // keep the playhead
}

void PirataProcessor::stopPreview() { playing = false; }

void PirataProcessor::startPreviewFromHere()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    playing = preview.getNumSamples() > 0;
}

void PirataProcessor::seek (float fraction)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pos = juce::jlimit (0.0, 1.0, (double) fraction) * preview.getNumSamples();
}

float PirataProcessor::previewProgress() const
{
    const auto n = preview.getNumSamples();
    return n > 0 ? (float) juce::jlimit (0.0, 1.0, pos / n) : 0.0f;
}

void PirataProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    // FL's tempo: dragged MIDI follows it, so the window warns when it isn't the sample's
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm.store (*bpm);
    if (! playing.load())
        return;                                   // pure pass-through

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;

    const int n = preview.getNumSamples();
    const int pc = preview.getNumChannels();
    const double step = previewRate / hostRate;   // simple linear resampling
    const int chans = buffer.getNumChannels();

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const int i0 = (int) pos;
        if (i0 + 1 >= n)
        {
            if (loop.load() && n > 1) { pos = 0.0; continue; }
            playing = false;
            break;
        }
        const float frac = (float) (pos - i0);
        for (int c = 0; c < chans; ++c)
        {
            const float* src = preview.getReadPointer (juce::jmin (c, pc - 1));
            buffer.addSample (c, i, src[i0] + frac * (src[i0 + 1] - src[i0]));
        }
        pos += step;
    }
}

juce::AudioProcessorEditor* PirataProcessor::createEditor()
{
    return new PirataEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PirataProcessor();
}
