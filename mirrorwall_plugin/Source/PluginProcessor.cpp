#include "PluginProcessor.h"
#include "PluginEditor.h"

MirrorProcessor::MirrorProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

bool MirrorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

mwl::Macros MirrorProcessor::macros() const
{
    mwl::Macros m;
    m.comp = knobComp; m.reverb = knobReverb; m.delay = knobDelay; m.fx = knobFx;
    m.deess = knobDeess; m.drive = knobDrive; m.out = knobOut; m.gate = knobGate;
    return m;
}

void MirrorProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    const auto n = (size_t) juce::jmax (512, samplesPerBlock);
    mono.assign (n, 0.0f); outL.assign (n, 0.0f); outR.assign (n, 0.0f);
    rebuildEngine();
}

void MirrorProcessor::rebuildEngine()
{
    std::unique_ptr<mwl::Engine> fresh;
    if (livePresetJson.isNotEmpty())
    {
        const auto P = juce::JSON::parse (livePresetJson);
        if (P.isObject())
        {
            fresh = std::make_unique<mwl::Engine>();
            fresh->prepare (hostRate, P, verbType);
        }
    }
    {
        const juce::SpinLock::ScopedLockType sl (engineLock);
        std::swap (engine, fresh);
    }
    setLatencySamples (liveMode.load() && engine != nullptr ? engine->latency() : 0);
}   // the old chain is destroyed here, outside the audio lock

void MirrorProcessor::setLivePreset (const juce::var& P)
{
    livePresetJson = juce::JSON::toString (P, true);
    rebuildEngine();
}

void MirrorProcessor::setLiveMode (bool shouldBeLive)
{
    if (shouldBeLive) stopPreview();
    liveMode = shouldBeLive;
    setLatencySamples (shouldBeLive && engine != nullptr ? engine->latency() : 0);
}

void MirrorProcessor::setPreview (juce::AudioBuffer<float>&& audio, double fileSampleRate)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    preview = std::move (audio);
    previewRate = fileSampleRate;
    pos = juce::jmin (pos, (double) preview.getNumSamples());   // keep the playhead
}

void MirrorProcessor::stopPreview() { playing = false; }

void MirrorProcessor::startPreviewFromHere()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    playing = preview.getNumSamples() > 0;
}

void MirrorProcessor::seek (float fraction)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pos = juce::jlimit (0.0, 1.0, (double) fraction) * preview.getNumSamples();
}

float MirrorProcessor::previewProgress() const
{
    const auto n = preview.getNumSamples();
    return n > 0 ? (float) juce::jlimit (0.0, 1.0, pos / n) : 0.0f;
}

void MirrorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int chans = buffer.getNumChannels();
    const int total = buffer.getNumSamples();

    if (liveMode.load())
    {
        const juce::SpinLock::ScopedTryLockType el (engineLock);
        if (! el.isLocked() || engine == nullptr) return;          // chain is being swapped: pass through
        const auto m = macros();
        const bool autoIn = autoInput.load();
        const double gain = inGainDb.load();
        for (int start = 0; start < total; start += (int) mono.size())
        {
            const int n = juce::jmin ((int) mono.size(), total - start);
            for (int i = 0; i < n; ++i)
            {
                float s = 0;
                for (int c = 0; c < chans; ++c) s += buffer.getSample (c, start + i);
                mono[(size_t) i] = s / (float) juce::jmax (1, chans);
            }
            engine->process (mono.data(), outL.data(), outR.data(), n, m, gain, autoIn);
            if (chans >= 2)
            {
                buffer.copyFrom (0, start, outL.data(), n);
                buffer.copyFrom (1, start, outR.data(), n);
            }
            else
                for (int i = 0; i < n; ++i) buffer.setSample (0, start + i, 0.5f * (outL[(size_t) i] + outR[(size_t) i]));
        }
        liveIn = autoIn ? engine->inputGainDb() : gain;
        return;
    }

    if (! playing.load())
        return;                                   // HQ idle: pure pass-through

    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;

    const int n = preview.getNumSamples();
    const int pc = preview.getNumChannels();
    const double step = previewRate / hostRate;   // simple linear resampling

    for (int i = 0; i < total; ++i)
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

void MirrorProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("MIRRORWALL");
    t.setProperty ("version", 2, nullptr);
    t.setProperty ("live", liveMode.load(), nullptr);
    t.setProperty ("preset", presetName, nullptr);
    t.setProperty ("livePreset", livePresetJson, nullptr);
    t.setProperty ("inGain", inGainDb.load(), nullptr);
    t.setProperty ("autoIn", autoInput.load(), nullptr);
    t.setProperty ("verb", verbType, nullptr);
    t.setProperty ("mic", micIndex, nullptr);
    t.setProperty ("comp", knobComp.load(), nullptr);
    t.setProperty ("reverb", knobReverb.load(), nullptr);
    t.setProperty ("delay", knobDelay.load(), nullptr);
    t.setProperty ("fx", knobFx.load(), nullptr);
    t.setProperty ("deess", knobDeess.load(), nullptr);
    t.setProperty ("drive", knobDrive.load(), nullptr);
    t.setProperty ("out", knobOut.load(), nullptr);
    t.setProperty ("gate", knobGate.load(), nullptr);
    juce::MemoryOutputStream mo (dest, false);
    t.writeToStream (mo);
}

void MirrorProcessor::setStateInformation (const void* data, int size)
{
    const auto t = juce::ValueTree::readFromData (data, (size_t) size);
    if (! t.isValid() || t.getType().toString() != "MIRRORWALL") return;
    presetName = t.getProperty ("preset", presetName).toString();
    livePresetJson = t.getProperty ("livePreset", {}).toString();
    inGainDb = (double) t.getProperty ("inGain", 0.0);
    autoInput = (bool) t.getProperty ("autoIn", true);
    verbType = (int) t.getProperty ("verb", 0);
    micIndex = (int) t.getProperty ("mic", micIndex);
    knobComp = (double) t.getProperty ("comp", 1.0);
    knobReverb = (double) t.getProperty ("reverb", 1.0);
    knobDelay = (double) t.getProperty ("delay", 1.0);
    knobFx = (double) t.getProperty ("fx", 1.0);
    knobDeess = (double) t.getProperty ("deess", 1.0);
    knobDrive = (double) t.getProperty ("drive", 0.0);
    knobOut = (double) t.getProperty ("out", 0.0);
    knobGate = (double) t.getProperty ("gate", 1.0);
    rebuildEngine();
    setLiveMode ((bool) t.getProperty ("live", false));
}

juce::AudioProcessorEditor* MirrorProcessor::createEditor()
{
    return new MirrorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MirrorProcessor();
}
