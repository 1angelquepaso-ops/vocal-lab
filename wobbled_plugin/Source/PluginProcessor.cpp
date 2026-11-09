#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace wb;

WobbleProcessor::WobbleProcessor()
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Sub", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "WOBBLED", createLayout())
{
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            raw[wp->getParameterID()] = state.getRawParameterValue (wp->getParameterID());
    shape = Shape::preset ("Sine");
    shapeChanged();
}

bool WobbleProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (l.outputBuses.size() > 1)
    {
        const auto& aux = l.outputBuses.getReference (1);
        if (! aux.isDisabled() && aux != juce::AudioChannelSet::stereo()) return false;
    }
    for (auto& in : l.inputBuses) if (! in.isDisabled()) return false;
    return true;
}

void WobbleProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    engineTableVersion = 0;
}

Params WobbleProcessor::snapshot() const
{
    Params p;
    p.source = (int) rv (id::source);
    p.smpStart = rv (id::smpStart); p.smpLength = rv (id::smpLength); p.smpLoop = rv (id::smpLoop) > 0.5f;
    p.fadeIn = rv (id::smpFadeIn); p.fadeOut = rv (id::smpFadeOut); p.reverse = rv (id::smpReverse) > 0.5f;
    p.tune = (int) rv (id::tune); p.fine = rv (id::fine); p.rootKey = (int) rv (id::rootKey);
    p.voiceMode = (int) rv (id::voiceMode); p.glide = rv (id::glide); p.attack = rv (id::attack);
    p.decay = rv (id::decay); p.sustain = rv (id::sustain); p.release = rv (id::release);
    p.unison = (int) rv (id::unison); p.detune = rv (id::detune);
    p.fltType = (int) rv (id::fltType); p.cutoff = rv (id::cutoff); p.reso = rv (id::reso); p.drive = rv (id::drive);
    p.hq = rv (id::hq) > 0.5f;
    p.lowCut = rv (id::lowCut); p.lowGain = rv (id::lowGain); p.midGain = rv (id::midGain); p.midFreq = rv (id::midFreq);
    p.highGain = rv (id::highGain); p.highCut = rv (id::highCut);
    p.boostFreq = rv (id::boostFreq); p.boostGain = rv (id::boostGain); p.boostQ = rv (id::boostQ);
    p.subLock = rv (id::subLock) > 0.5f; p.subFreq = rv (id::subFreq); p.subLevel = rv (id::subLevel);
    p.subOct = (int) rv (id::subOct); p.subOut = (int) rv (id::subOut);
    p.width = rv (id::width); p.mono = rv (id::mono) > 0.5f; p.volume = rv (id::volume); p.clip = rv (id::clip) > 0.5f;
    p.rate = (int) rv (id::rate); p.lfoMode = (int) rv (id::lfoMode); p.freeRate = rv (id::freeRate);
    p.quantize = (int) rv (id::quantize); p.resync = rv (id::resync); p.smooth = rv (id::smooth);
    p.chaos = rv (id::chaos); p.seed = (int) rv (id::seed); p.velDepth = rv (id::velDepth);
    p.dCutoff = rv (id::dCutoff); p.dVolume = rv (id::dVolume); p.dPitch = rv (id::dPitch);
    p.dDrive = rv (id::dDrive); p.dBoost = rv (id::dBoost);
    p.xyX = rv (id::xyX); p.xyY = rv (id::xyY); p.xTarget = (int) rv (id::xTarget); p.yTarget = (int) rv (id::yTarget);
    p.xySnap = rv (id::xySnap) > 0.5f; p.xyGlide = rv (id::xyGlide);
    return p;
}

void WobbleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    HostInfo h;
    auto* ph = testPlayHead != nullptr ? testPlayHead : getPlayHead();
    if (ph != nullptr)
        if (auto pos = ph->getPosition())
        {
            h.playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) h.bpm = *b;
            if (auto q = pos->getPpqPosition()) { h.ppq = *q; h.hasPpq = true; }
        }

    // shape table hand-off
    if (tableVersion.load() != engineTableVersion)
    {
        const juce::SpinLock::ScopedTryLockType tl (tableLock);
        if (tl.isLocked())
        {
            std::copy (pendingTable, pendingTable + Shape::tableSize, engine.table);
            engine.loopStart = pendingLoopStart; engine.loopEnd = pendingLoopEnd;
            engineTableVersion = tableVersion.load();
        }
    }
    // sample hand-off
    {
        const juce::SpinLock::ScopedTryLockType sl (sampleLock);
        if (sl.isLocked() && sampleDirty)
        {
            engine.setSample (pendingSample);
            ackSample = pendingSample;
            sampleDirty = false;
        }
    }

    auto mainBuf = getBusBuffer (buffer, false, 0);
    juce::AudioBuffer<float> subBuf;
    juce::AudioBuffer<float>* subPtr = nullptr;
    if (getBusCount (false) > 1 && getBus (false, 1) != nullptr && getBus (false, 1)->isEnabled())
    {
        const int ch0 = getChannelIndexInProcessBlockBuffer (false, 1, 0);
        if (ch0 >= 0 && ch0 + 1 < buffer.getNumChannels())
        {
            subBuf.setDataToReferTo (buffer.getArrayOfWritePointers() + ch0, 2, buffer.getNumSamples());
            subPtr = &subBuf;
        }
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    engine.process (mainBuf, subPtr, midi, snapshot(), h);
    midi.clear();
}

// ------------------------------------------------------------------ shape
void WobbleProcessor::shapeChanged()
{
    float tmp[Shape::tableSize];
    shape.compile (tmp);
    const juce::SpinLock::ScopedLockType l (tableLock);
    std::copy (tmp, tmp + Shape::tableSize, pendingTable);
    pendingLoopStart = shape.loopStart; pendingLoopEnd = shape.loopEnd;
    ++tableVersion;
}

// ------------------------------------------------------------------ sample
juce::File WobbleProcessor::sampleFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("WOBBLE-D").getChildFile ("Samples");
    d.createDirectory();
    return d;
}

void WobbleProcessor::publishSample (std::unique_ptr<SampleData> s)
{
    const juce::SpinLock::ScopedLockType l (sampleLock);
    pendingSample = s.get();
    latestSample = s.get();
    sampleDirty = true;
    sampleStore.erase (std::remove_if (sampleStore.begin(), sampleStore.end(),
                                       [&] (auto& p) { return p.get() != ackSample && p.get() != pendingSample; }),
                       sampleStore.end());
    if (s) sampleStore.push_back (std::move (s));
}

void WobbleProcessor::clearSample()
{
    publishSample (nullptr);
    state.state.removeChild (state.state.getChildWithName ("SAMPLE"), nullptr);
}

// Decode + trim + normalise + detect root + copy to Documents\WOBBLE-D\Samples. Pure work: safe on any thread.
std::unique_ptr<SampleData> WobbleProcessor::prepareSample (const juce::File& fileIn, bool trim, bool norm, juce::String& error)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (fileIn));
    if (! r) { error = "Can't read " + fileIn.getFileName(); return nullptr; }
    const auto maxLen = (juce::int64) (r->sampleRate * 60.0);
    const int n = (int) juce::jmin (r->lengthInSamples, maxLen);
    if (n < 64) { error = "Sample is too short"; return nullptr; }

    auto s = std::make_unique<SampleData>();
    s->fs = r->sampleRate;
    juce::AudioBuffer<float> tmp ((int) juce::jmax (1u, r->numChannels), n);
    r->read (&tmp, 0, n, 0, true, true);
    s->buf.setSize (2, n);
    s->buf.copyFrom (0, 0, tmp, 0, 0, n);
    s->buf.copyFrom (1, 0, tmp, tmp.getNumChannels() > 1 ? 1 : 0, 0, n);

    if (trim)
    {
        const float th = 0.001f;
        int a = 0, b = n - 1;
        auto loud = [&] (int i) { return std::abs (s->buf.getSample (0, i)) > th || std::abs (s->buf.getSample (1, i)) > th; };
        while (a < n - 1 && ! loud (a)) ++a;
        while (b > a && ! loud (b)) --b;
        a = juce::jmax (0, a - (int) (0.002 * s->fs));
        b = juce::jmin (n - 1, b + (int) (0.010 * s->fs));
        if (b - a > 64 && (a > 0 || b < n - 1))
        {
            juce::AudioBuffer<float> t (2, b - a + 1);
            for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, s->buf, c, a, b - a + 1);
            s->buf = std::move (t);
        }
    }
    if (norm)
    {
        const float pk = s->buf.getMagnitude (0, s->buf.getNumSamples());
        if (pk > 1.0e-5f) s->buf.applyGain (0.891f / pk);
    }
    {
        const int m = s->buf.getNumSamples();
        std::vector<float> mono ((size_t) m);
        for (int i = 0; i < m; ++i) mono[(size_t) i] = 0.5f * (s->buf.getSample (0, i) + s->buf.getSample (1, i));
        s->detectedNote = detectMidiNote (mono.data(), m, s->fs);
    }
    // keep a copy in Documents\WOBBLE-D\Samples so presets never lose the file
    juce::File stored = fileIn;
    const auto folder = sampleFolder();
    if (! fileIn.isAChildOf (folder))
    {
        auto dest = folder.getChildFile (fileIn.getFileName());
        if (dest.existsAsFile() && dest.getSize() != fileIn.getSize())
            dest = folder.getNonexistentChildFile (fileIn.getFileNameWithoutExtension(), fileIn.getFileExtension(), false);
        if (dest.existsAsFile() || fileIn.copyFileTo (dest)) stored = dest;
    }
    s->name = fileIn.getFileNameWithoutExtension();
    s->path = stored.getFullPathName();
    return s;
}

void WobbleProcessor::setParamWithGesture (const char* pid, float plainValue)
{
    if (auto* p = state.getParameter (pid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

// Message thread. touchParams = false when FL is reopening a project (never change parameters then).
void WobbleProcessor::finishLoad (std::unique_ptr<SampleData> s, bool touchParams)
{
    const double note = s->detectedNote;
    auto t = state.state.getOrCreateChildWithName ("SAMPLE", nullptr);
    t.setProperty ("path", s->path, nullptr);
    publishSample (std::move (s));
    if (! touchParams) return;
    setParamWithGesture (id::source, 0.0f);
    if (note > 0) setParamWithGesture (id::rootKey, (float) juce::jlimit (0, 127, (int) std::lround (note)));
}

bool WobbleProcessor::loadSample (const juce::File& fileIn, juce::String& error, bool touchParams)
{
    auto s = prepareSample (fileIn, autoTrim, autoNormalize, error);
    if (! s) return false;
    finishLoad (std::move (s), touchParams);
    return true;
}

// Used by the editor: heavy work on a background thread, then the hand-off happens later on the message
// thread - never inside FL's drag-and-drop or file-dialog callback (that crashed FL 24.2).
void WobbleProcessor::loadSampleAsync (const juce::File& f, std::function<void (bool, juce::String)> done)
{
    loadingSample = true;
    auto aliveFlag = alive;
    const bool trim = autoTrim, norm = autoNormalize;
    juce::Thread::launch ([this, aliveFlag, f, trim, norm, done]
    {
        juce::String err;
        std::shared_ptr<SampleData> holder (prepareSample (f, trim, norm, err).release());
        juce::MessageManager::callAsync ([this, aliveFlag, holder, err, done]
        {
            if (! aliveFlag->load()) return;
            loadingSample = false;
            if (holder) finishLoad (std::make_unique<SampleData> (std::move (*holder)), true);
            if (done) done (holder != nullptr, err);
        });
    });
}

// ------------------------------------------------------------------ patch actions
void WobbleProcessor::initPatch()
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    shape = Shape::preset ("Sine");
    shapeChanged();
    clearSample();
}

void WobbleProcessor::rerollChaos()
{
    if (auto* p = state.getParameter (id::seed))
    {
        const int cur = (int) p->convertFrom0to1 (p->getValue());
        setParamWithGesture (id::seed, (float) ((cur + 1 + juce::Random::getSystemRandom().nextInt (97)) % 10000));
    }
}

// ------------------------------------------------------------------ state
void WobbleProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto st = state.copyState();
    st.removeChild (st.getChildWithName ("LFO"), nullptr);
    st.appendChild (shape.toTree(), nullptr);
    st.setProperty ("autoTrim", autoTrim, nullptr);
    st.setProperty ("autoNormalize", autoNormalize, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary (*xml, dest);
}

void WobbleProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (! xml) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.hasType (state.state.getType())) return;
    shape = Shape::fromTree (st.getChildWithName ("LFO"));
    shapeChanged();
    autoTrim = (bool) st.getProperty ("autoTrim", true);
    autoNormalize = (bool) st.getProperty ("autoNormalize", true);
    const auto path = st.getChildWithName ("SAMPLE").getProperty ("path").toString();
    state.replaceState (st);
    if (path.isNotEmpty())
    {
        // reload the sample but never touch parameters while FL is opening a project
        juce::String err;
        juce::File f (path);
        if (f.existsAsFile()) loadSample (f, err, false);
    }
    else publishSample (nullptr);
}

juce::AudioProcessorEditor* WobbleProcessor::createEditor() { return new WobbleEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new WobbleProcessor(); }
