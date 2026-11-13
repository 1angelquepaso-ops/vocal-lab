#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessor::BusesProperties TitoProcessor::makeBuses()
{
    auto b = juce::AudioProcessor::BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 0; i < tito::numPads; ++i)
        b = b.withOutput ("Pad " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

TitoProcessor::TitoProcessor() : AudioProcessor (makeBuses())
{
    formats.registerBasicFormats();
    loadFactoryKit (true);
}

bool TitoProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& s = layouts.outputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo())
            return false;
    }
    return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
}

void TitoProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostRate = sampleRate;
    pads.prepare (sampleRate);
    scratch.setSize (2, juce::jmax (512, samplesPerBlock));
}

// ------------------------------------------------------------------ pads
juce::File TitoProcessor::factoryDir() const
{
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto d : { juce::File::getSpecialLocation (juce::File::currentApplicationFile).getChildFile ("Contents/Resources/kit"),
                    me.getParentDirectory().getParentDirectory().getChildFile ("Resources/kit"),
                    me.getParentDirectory().getChildFile ("kit"),
                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TITO PUENTE/factory") })
        if (d.isDirectory() && d.getNumberOfChildFiles (juce::File::findFiles, "*.wav") >= tito::numPads)
            return d;
    auto env = juce::SystemStats::getEnvironmentVariable ("TITO_KIT", {});
    return env.isNotEmpty() ? juce::File (env) : juce::File();
}

void TitoProcessor::loadFactoryKit (bool onlyEmpty)
{
    const auto dir = factoryDir();
    if (! dir.isDirectory()) return;
    for (int i = 0; i < tito::numPads; ++i)
    {
        if (onlyEmpty && pads.samples[(size_t) i] != nullptr) continue;
        if (meta[(size_t) i].locked) continue;
        auto f = dir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_" + tito::padInfo[i].key + ".wav");
        if (f.existsAsFile())
            loadPadFile (i, f, "factory");
    }
}

bool TitoProcessor::loadPadFile (int pad, const juce::File& f, const juce::String& source)
{
    std::unique_ptr<juce::AudioFormatReader> rd (formats.createReaderFor (f));
    if (rd == nullptr) return false;
    const auto len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
    if (len < 16) return false;
    auto s = std::make_shared<tito::PadSample>();
    s->audio.setSize (2, len + 2);
    s->audio.clear();
    rd->read (&s->audio, 0, len, 0, true, true);
    if (rd->numChannels == 1)
        s->audio.copyFrom (1, 0, s->audio, 0, 0, len);
    s->rate = rd->sampleRate;
    s->file = f;
    pads.setSample (pad, s);
    meta[(size_t) pad].source = source;
    return true;
}

juce::File TitoProcessor::padFile (int pad) const
{
    auto s = pads.samples[(size_t) pad];
    return s != nullptr ? s->file : juce::File();
}

// ------------------------------------------------------------------ preview
void TitoProcessor::setSequence (std::vector<Event> ev, double bpm, double lengthBeats, double t0)
{
    std::sort (ev.begin(), ev.end(), [] (const Event& a, const Event& b) { return a.beat < b.beat; });
    const juce::SpinLock::ScopedLockType sl (seqLock);
    seq = std::move (ev);
    seqBpm = juce::jmax (20.0, bpm);
    seqLength = juce::jmax (4.0, lengthBeats);
    seqT0 = t0;
    const double ph = playhead.load();
    nextEvent = (size_t) (std::lower_bound (seq.begin(), seq.end(), ph,
                                             [] (const Event& e, double b) { return e.beat < b; }) - seq.begin());
}

void TitoProcessor::setOriginal (juce::AudioBuffer<float>&& audio, double rate)
{
    const juce::SpinLock::ScopedLockType sl (origLock);
    original = std::move (audio);
    originalRate = rate;
    originalLoaded = original.getNumSamples() > 0;
}

void TitoProcessor::play (bool on)
{
    if (! on) pads.allOff();
    playing = on;
}

void TitoProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto b = pos->getBpm())
                hostBpm.store (*b);

    // destinations: main + enabled pad outputs
    std::array<juce::AudioBuffer<float>, tito::numPads + 1> views;
    std::array<juce::AudioBuffer<float>*, tito::numPads + 1> dest {};
    for (int bus = 0; bus <= tito::numPads && bus < getBusCount (false); ++bus)
    {
        auto* b = getBus (false, bus);
        if (b == nullptr || ! b->isEnabled()) continue;
        views[(size_t) bus] = getBusBuffer (buffer, false, bus);
        if (views[(size_t) bus].getNumChannels() > 0)
            dest[(size_t) bus] = &views[(size_t) bus];
    }
    if (dest[0] == nullptr) return;

    std::pair<int, float> a;
    while (auditionQueue.pop (a))
        pads.trigger (a.first, a.second);

    // FL's MIDI, sample-accurate
    int pos = 0;
    auto renderTo = [&] (int upTo)
    {
        if (upTo > pos) { pads.render (dest, pos, upTo - pos); pos = upTo; }
    };

    // preview sequencer events inside this block
    struct Hit { int at, pad; float vel; };
    struct HitList
    {
        std::array<Hit, 512> h {};
        size_t n = 0;
        void push_back (Hit x) { if (n < h.size()) h[n++] = x; }
        size_t size() const { return n; }
        Hit& operator[] (size_t i) { return h[i]; }
        Hit* begin() { return h.data(); }
        Hit* end() { return h.data() + n; }
    } seqHits;
    if (playing.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (seqLock);
        if (sl.isLocked() && ! seq.empty())
        {
            const double bps = seqBpm / 60.0;
            double ph = playhead.load();
            const double sk = seekTo.exchange (-1.0);
            if (sk >= 0)
            {
                ph = sk;
                pads.allOff();
                nextEvent = (size_t) (std::lower_bound (seq.begin(), seq.end(), ph,
                                     [] (const Event& e, double b) { return e.beat < b; }) - seq.begin());
            }
            const double perSample = bps / hostRate;
            const double blockStartBeat = ph;
            double t = ph;
            int done = 0;
            while (done < n)
            {
                const double segEnd = t + perSample * (n - done);
                const double limit = juce::jmin (segEnd, seqLength);
                while (nextEvent < seq.size() && seq[nextEvent].beat < limit)
                {
                    const auto& e = seq[nextEvent++];
                    if (e.beat >= t - 1e-9 && hear[juce::jlimit (0, 2, e.layer)].load())
                        seqHits.push_back ({ juce::jlimit (0, n - 1, done + (int) ((e.beat - t) / perSample)), e.pad, e.vel });
                }
                if (segEnd < seqLength) { t = segEnd; break; }
                done += juce::jmax (1, (int) ((seqLength - t) / perSample));
                t = 0.0;
                nextEvent = 0;
                if (! loopPreview.load()) { playing = false; break; }
            }
            playhead.store (t);
            const double startSecBeat = blockStartBeat;
            // original stem, aligned through t0
            if (hear[3].load())
            {
                const juce::SpinLock::ScopedTryLockType ol (origLock);
                if (ol.isLocked() && original.getNumSamples() > 0)
                {
                    const double startSec = seqT0 + startSecBeat / bps;
                    const double step = originalRate / hostRate;
                    double p = startSec * originalRate;
                    auto* L = dest[0]->getWritePointer (0);
                    auto* R = dest[0]->getWritePointer (juce::jmin (1, dest[0]->getNumChannels() - 1));
                    const auto* oL = original.getReadPointer (0);
                    const auto* oR = original.getReadPointer (juce::jmin (1, original.getNumChannels() - 1));
                    for (int i = 0; i < n; ++i, p += step)
                    {
                        const int i0 = (int) p;
                        if (i0 < 0 || i0 + 1 >= original.getNumSamples()) continue;
                        const float fr = (float) (p - i0);
                        L[i] += 0.8f * (oL[i0] + fr * (oL[i0 + 1] - oL[i0]));
                        if (R != L) R[i] += 0.8f * (oR[i0] + fr * (oR[i0 + 1] - oR[i0]));
                    }
                }
            }
        }
    }
    std::sort (seqHits.begin(), seqHits.end(), [] (const Hit& x, const Hit& y) { return x.at < y.at; });

    size_t hi = 0;
    for (const auto mm : midi)
    {
        const auto m = mm.getMessage();
        const int at = juce::jlimit (0, n - 1, mm.samplePosition);
        while (hi < seqHits.size() && seqHits[hi].at <= at)
        {
            renderTo (seqHits[hi].at);
            pads.trigger (seqHits[hi].pad, seqHits[hi].vel);
            ++hi;
        }
        renderTo (at);
        const int pad = m.getNoteNumber() - tito::baseNote;
        if (m.isNoteOn() && pad >= 0 && pad < tito::numPads)
            pads.trigger (pad, m.getFloatVelocity());
        else if (m.isNoteOff() && pad >= 0 && pad < tito::numPads)
            pads.noteOff (pad);
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            pads.allOff();
    }
    while (hi < seqHits.size())
    {
        renderTo (seqHits[hi].at);
        pads.trigger (seqHits[hi].pad, seqHits[hi].vel);
        ++hi;
    }
    renderTo (n);
}

// ------------------------------------------------------------------ state
void TitoProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree st ("TITO");
    st.setProperty ("v", 1, nullptr);
    st.setProperty ("session", sessionDir.getFullPathName(), nullptr);
    st.setProperty ("stem", stemFile.getFullPathName(), nullptr);
    st.setProperty ("soundFolder", soundFolder.getFullPathName(), nullptr);
    st.setProperty ("analysis", juce::JSON::toString (analysis, true), nullptr);
    st.setProperty ("build", juce::JSON::toString (build, true), nullptr);
    st.setProperty ("bpmOverride", bpmOverride, nullptr);
    st.setProperty ("sensitivity", sensitivity, nullptr);
    st.setProperty ("feel", feel, nullptr);
    st.setProperty ("qgrid", qgrid, nullptr);
    st.setProperty ("style", style, nullptr);
    st.setProperty ("grid", grid, nullptr);
    st.setProperty ("fillLen", fillLen, nullptr);
    st.setProperty ("seed", seed, nullptr);
    st.setProperty ("swing", swing, nullptr);
    st.setProperty ("human", human, nullptr);
    st.setProperty ("intensity", intensity, nullptr);
    st.setProperty ("crash", crash, nullptr);
    st.setProperty ("room", room, nullptr);
    st.setProperty ("linkPads", linkPads, nullptr);
    juce::String l, w;
    for (bool b : lay) l << (b ? "1" : "0");
    for (bool b : fillWhere) w << (b ? "1" : "0");
    st.setProperty ("layers", l, nullptr);
    st.setProperty ("fills", w, nullptr);
    for (int i = 0; i < tito::numPads; ++i)
    {
        juce::ValueTree p ("PAD");
        const auto pp = pads.getParams (i);
        p.setProperty ("i", i, nullptr);
        p.setProperty ("file", padFile (i).getFullPathName(), nullptr);
        p.setProperty ("source", meta[(size_t) i].source, nullptr);
        p.setProperty ("chop", meta[(size_t) i].chopFile, nullptr);
        p.setProperty ("locked", meta[(size_t) i].locked, nullptr);
        p.setProperty ("start", pp.start, nullptr);
        p.setProperty ("length", pp.length, nullptr);
        p.setProperty ("pitch", pp.pitch, nullptr);
        p.setProperty ("decay", pp.decay, nullptr);
        p.setProperty ("volume", pp.volume, nullptr);
        p.setProperty ("pan", pp.pan, nullptr);
        p.setProperty ("loop", pp.loop, nullptr);
        p.setProperty ("reverse", pp.reverse, nullptr);
        p.setProperty ("choke", pp.choke, nullptr);
        p.setProperty ("out", pp.out, nullptr);
        st.appendChild (p, nullptr);
    }
    if (auto xml = st.createXml())
        copyXmlToBinary (*xml, dest);
}

void TitoProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto st = juce::ValueTree::fromXml (*xml);
    if (! st.isValid()) return;
    sessionDir = juce::File (st["session"].toString());
    stemFile = juce::File (st["stem"].toString());
    soundFolder = juce::File (st["soundFolder"].toString());
    analysis = juce::JSON::parse (st["analysis"].toString());
    build = juce::JSON::parse (st["build"].toString());
    bpmOverride = st["bpmOverride"];
    sensitivity = st.getProperty ("sensitivity", 0.5);
    feel = st["feel"]; qgrid = st["qgrid"]; style = st["style"]; grid = st["grid"];
    fillLen = st.getProperty ("fillLen", 2); seed = st.getProperty ("seed", 1);
    swing = st["swing"]; human = st.getProperty ("human", 0.6); intensity = st.getProperty ("intensity", 0.6);
    crash = st.getProperty ("crash", true); room = st.getProperty ("room", true);
    linkPads = st.getProperty ("linkPads", true);
    readLinks (analysis);
    const auto l = st["layers"].toString(), w = st["fills"].toString();
    for (int i = 0; i < 5; ++i) lay[i] = l[i] == '1';
    for (int i = 0; i < 3; ++i) fillWhere[i] = w[i] == '1';
    for (const auto& p : st)
    {
        if (! p.hasType ("PAD")) continue;
        const int i = p["i"];
        if (i < 0 || i >= tito::numPads) continue;
        tito::PadParams pp;
        pp.start = p["start"]; pp.length = p.getProperty ("length", 1.0f); pp.pitch = p["pitch"];
        pp.decay = p.getProperty ("decay", 1.0f); pp.volume = p["volume"]; pp.pan = p["pan"];
        pp.loop = p["loop"]; pp.reverse = p["reverse"]; pp.choke = p["choke"]; pp.out = p["out"];
        pads.setParams (i, pp);
        meta[(size_t) i].chopFile = p["chop"].toString();
        meta[(size_t) i].locked = p["locked"];
        const juce::File f (p["file"].toString());
        if (f.existsAsFile())
            loadPadFile (i, f, p["source"].toString());
    }
    loadFactoryKit (true);
    // preview sequence from the saved build
    if (build.isObject())
    {
        std::vector<Event> ev;
        int layer = 0;
        for (auto* k : { "song", "layers", "fills" })
        {
            if (auto* arr = build["notes"][k].getArray())
                for (auto& n : *arr)
                    ev.push_back ({ (double) n[0], (int) n[1], (float) (int) n[2] / 127.0f, layer });
            ++layer;
        }
        setSequence (std::move (ev), (double) build["bpm"], (double) build["length_beats"], (double) build["t0"]);
    }
}

juce::AudioProcessorEditor* TitoProcessor::createEditor() { return new TitoEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TitoProcessor(); }
