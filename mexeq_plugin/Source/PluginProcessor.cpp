#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mx;

static const char* const BAND_FIELDS[18] = { "used", "type", "order", "freq", "gain", "q", "char", "shape", "stereo",
                                              "dyn", "range", "thr", "autothr", "attack", "release", "sc", "learned", "toff" };

MexProcessor::MexProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      state (*this, nullptr, "MEXEQ", layout())
{
    for (int b = 0; b < NBANDS; ++b)
        for (int f = 0; f < 18; ++f) ptrs[b].v[f] = state.getRawParameterValue (pid (b, BAND_FIELDS[f]));
    pQuality = state.getRawParameterValue ("quality");
    pPhase = state.getRawParameterValue ("phase");
    pDrive = state.getRawParameterValue ("drive");
    pOut = state.getRawParameterValue ("output");
    pStrength = state.getRawParameterValue ("strength");
    anPre.assign (AN, 0.0f); anPost.assign (AN, 0.0f);
}

juce::AudioProcessorValueTreeState::ParameterLayout MexProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    StringArray types, orders, shapes, places;
    for (auto* s : TYPE_NAMES) types.add (s);
    for (auto* s : ORDER_NAMES) orders.add (s);
    for (auto* s : SHAPE_NAMES) shapes.add (String::fromUTF8 (s));
    for (auto* s : STEREO_NAMES) places.add (s);
    auto hz = [] (float lo, float hi) { NormalisableRange<float> r (lo, hi); r.setSkewForCentre (std::sqrt (lo * hi)); return r; };
    auto fHz = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        { return v >= 1000.0f ? String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : String (v, v < 100.0f ? 1 : 0) + " Hz"; });
    auto fDb = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return (v > 0 ? "+" : "") + String (v, 1) + " dB"; });
    auto fQ = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 2 : 1); });
    auto fMs = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " ms"; });
    for (int b = 0; b < NBANDS; ++b)
    {
        const String n = "B" + String (b + 1) + " ";
        auto id = [b] (const char* f) { return ParameterID { pid (b, f), 1 }; };
        const float defF = 40.0f * std::pow (2.0f, b * 0.25f);
        v.push_back (std::make_unique<AudioParameterBool> (id ("used"), n + "On", false));
        v.push_back (std::make_unique<AudioParameterChoice> (id ("type"), n + "Type", types, (int) Type::Peak));
        v.push_back (std::make_unique<AudioParameterChoice> (id ("order"), n + "Order", orders, (int) Order::Two));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("freq"), n + "Freq", hz (10.0f, 30000.0f), std::min (defF, 16000.0f), fHz));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("gain"), n + "Gain", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, fDb));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("q"), n + "Q", hz (0.025f, 100.0f), 0.7071f, fQ));
        v.push_back (std::make_unique<AudioParameterChoice> (id ("char"), n + "Character", StringArray { "Clean", "G" }, 1));
        v.push_back (std::make_unique<AudioParameterChoice> (id ("shape"), n + "SSL shape", shapes, 0));
        v.push_back (std::make_unique<AudioParameterChoice> (id ("stereo"), n + "Stereo", places, 0));
        v.push_back (std::make_unique<AudioParameterBool> (id ("dyn"), n + "Dynamic", false));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("range"), n + "Dyn range", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), -6.0f, fDb));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("thr"), n + "Threshold", NormalisableRange<float> (-80.0f, 0.0f, 0.1f), -30.0f, fDb));
        v.push_back (std::make_unique<AudioParameterBool> (id ("autothr"), n + "Auto threshold", true));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("attack"), n + "Attack", hz (0.1f, 200.0f), 10.0f, fMs));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("release"), n + "Release", hz (5.0f, 2000.0f), 120.0f, fMs));
        v.push_back (std::make_unique<AudioParameterBool> (id ("sc"), n + "External sidechain", false));
        v.push_back (std::make_unique<AudioParameterBool> (id ("learned"), n + "Learned", false));
        v.push_back (std::make_unique<AudioParameterFloat> (id ("toff"), n + "Auto threshold offset", NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 4.0f, fDb));
    }
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "quality", 1 }, "Quality", StringArray { "LIVE", "HQ", "AUTO" }, 2));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { "phase", 1 }, "HQ phase",
                                                         StringArray { "Natural", "Linear Low", "Linear Medium", "Linear High", "Linear Max" }, 0));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0.0f, 10.0f, 0.01f), 0.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, fDb));
    v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "strength", 1 }, "Learn strength", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f,
                                                        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String ((int) v) + "%"; })));
    return { v.begin(), v.end() };
}

void MexProcessor::setParam (const juce::String& id, float realValue)
{
    if (auto* p = state.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (realValue));
}

void MexProcessor::readBand (int i, BandParams& o) const
{
    auto g = [&] (int f) { return ptrs[i].v[f]->load(); };
    o.used = g (0) > 0.5f;
    o.spec.type = (Type) (int) g (1);
    o.spec.order = (Order) (int) g (2);
    o.spec.freq = g (3); o.spec.gainDb = g (4); o.spec.q = g (5);
    o.spec.gChar = g (6) > 0.5f;
    o.spec.shape = (Shape) (int) g (7);
    o.place = (Place) (int) g (8);
    o.dyn = g (9) > 0.5f; o.range = g (10); o.thrDb = g (11); o.autoThr = g (12) > 0.5f;
    o.attackMs = g (13); o.releaseMs = g (14); o.extSC = g (15) > 0.5f; o.learned = g (16) > 0.5f; o.autoOffset = g (17);
}

void MexProcessor::readGlobals (GlobalParams& g) const
{
    g.driveAmt = pDrive->load(); g.outDb = pOut->load(); g.strength = pStrength->load() / 100.0f;
}

int MexProcessor::findFreeBand() const
{
    for (int i = 0; i < NBANDS; ++i) if (ptrs[i].v[0]->load() < 0.5f) return i;
    return -1;
}

void MexProcessor::resetBand (int i)
{
    for (auto* f : BAND_FIELDS)
        if (auto* p = bandParam (i, f)) p->setValueNotifyingHost (p->getDefaultValue());
}

bool MexProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (l.getMainInputChannelSet() != out) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

int MexProcessor::currentMode() const
{
    const int q = (int) pQuality->load();
    if (q == 0) return 0;
    if (q == 2) return isNonRealtime() ? 1 : 3;
    return (int) pPhase->load() == 0 ? 1 : 2;
}

juce::String MexProcessor::modeName() const
{
    switch (currentMode())
    {
        case 0: return "LIVE (zero latency)";
        case 1: return "HQ Natural phase";
        case 2: return "HQ Linear phase";
        default: return "AUTO (LIVE now, HQ on export)";
    }
}

int MexProcessor::firSizeFor (int phase) const
{
    const int base[] = { 0, 4096, 8192, 16384, 32768 };
    int n = base[juce::jlimit (0, 4, phase)];
    if (sampleRateHz > 50000) n *= 2;
    if (sampleRateHz > 100000) n *= 2;
    return n;
}

void MexProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate; maxBlock = samplesPerBlock;
    const int nch = juce::jmax (1, getTotalNumOutputChannels());
    coreBase.prepare (sampleRate);
    os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) nch, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
    os->initProcessing ((size_t) samplesPerBlock);
    osLatency = (int) std::round (os->getLatencyInSamples());
    coreOS.prepare (sampleRate * 2.0);
    scUp.setSize (2, samplesPerBlock * 2 + 8);
    delayBuf.setSize (2, osLatency + 1); delayBuf.clear(); delayPos = 0;
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    convLR.prepare (spec); convMS.prepare (spec);
    learner.prepare (sampleRate);
    builtHash = -1; builtFirN = 0;
    prepared = true;
    rebuildNow();
    startTimerHz (10);
}

juce::int64 MexProcessor::computeStaticHash() const
{
    juce::int64 h = 1469598103934665603LL;
    auto mix = [&] (double x) { h ^= (juce::int64) std::llround (x * 1000.0); h *= 1099511628211LL; };
    for (int i = 0; i < NBANDS; ++i)
    {
        BandParams b; readBand (i, b);
        if (! b.active() || b.dyn) { mix (-1); continue; }
        mix ((int) b.spec.type); mix ((int) b.spec.order); mix (b.spec.freq); mix (b.spec.gainDb); mix (b.spec.q);
        mix (b.spec.gChar); mix ((int) b.spec.shape); mix ((int) b.place);
    }
    mix (pPhase->load()); mix (sampleRateHz);
    return h;
}

// Linear-phase FIRs from the exact analog curve of all static bands (L/R pair, then M/S pair)
void MexProcessor::designFir (int N)
{
    const int order = (int) std::round (std::log2 ((double) N));
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) N * 2);
    juce::AudioBuffer<float> irLR (2, N), irMS (2, N);
    std::vector<ASec> secs;
    std::vector<double> mag[4];
    for (auto& m : mag) m.assign ((size_t) N / 2 + 1, 1.0);
    for (int i = 0; i < NBANDS; ++i)
    {
        BandParams b; readBand (i, b);
        if (! b.active() || b.dyn) continue;
        designBand (b.spec, secs);
        int targets[2] = { -1, -1 };
        switch (b.place) { case Place::Stereo: targets[0] = 0; targets[1] = 1; break; case Place::Left: targets[0] = 0; break;
                           case Place::Right: targets[0] = 1; break; case Place::Mid: targets[0] = 2; break; case Place::Side: targets[0] = 3; break; }
        for (int k = 0; k <= N / 2; ++k)
        {
            const double f = std::max (1.0, k * sampleRateHz / N);
            const double m = std::abs (analogResponse (secs, f));
            for (int t : targets) if (t >= 0) mag[t][(size_t) k] *= m;
        }
    }
    auto make = [&] (const std::vector<double>& m, float* dst)
    {
        // zero-phase spectrum -> real impulse, centred at N/2, Blackman window
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int k = 0; k <= N / 2; ++k)
        {
            const double sgn = (k % 2) ? -1.0 : 1.0;            // shift by N/2 samples
            buf[(size_t) (2 * k)] = (float) (m[(size_t) k] * sgn);
            buf[(size_t) (2 * k + 1)] = 0.0f;
        }
        fft.performRealOnlyInverseTransform (buf.data());
        for (int n = 0; n < N; ++n)
        {
            const double w = 0.42 - 0.5 * std::cos (2 * PI * n / (N - 1)) + 0.08 * std::cos (4 * PI * n / (N - 1));
            dst[n] = (float) (buf[(size_t) n] * w);
        }
    };
    make (mag[0], irLR.getWritePointer (0)); make (mag[1], irLR.getWritePointer (1));
    make (mag[2], irMS.getWritePointer (0)); make (mag[3], irMS.getWritePointer (1));
    convLR.loadImpulseResponse (std::move (irLR), sampleRateHz, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                juce::dsp::Convolution::Normalise::no);
    convMS.loadImpulseResponse (std::move (irMS), sampleRateHz, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                juce::dsp::Convolution::Normalise::no);
}

void MexProcessor::rebuildNow()
{
    if (! prepared) return;
    const int q = (int) pQuality->load(), ph = (int) pPhase->load();
    int lat = 0;
    if (q == 2) lat = osLatency;                       // AUTO: always report the HQ latency (LIVE path is delay-matched)
    else if (q == 1) lat = ph == 0 ? osLatency : firSizeFor (ph);   // two FIRs of N/2 delay each
    if (q == 1 && ph > 0)
    {
        const auto h = computeStaticHash();
        const int N = firSizeFor (ph);
        if (h != builtHash || N != builtFirN) { designFir (N); builtHash = h; builtFirN = N; }
    }
    if (lat != reportedLatency)
    {
        reportedLatency = lat;
        setLatencySamples (lat);
        updateHostDisplay (ChangeDetails().withLatencyChanged (true));
    }
}

void MexProcessor::timerCallback()
{
    // LEARN progress (pump spectral frames, auto-stop after 30 s of audio)
    if (learner.learning.load())
    {
        learner.pump();
        learnSec.store (learner.secondsCollected());
        if (learner.secondsCollected() >= 30.0) finishLearn();
    }
    // linear-phase rebuild when static bands changed (debounced by the timer rate)
    if ((int) pQuality->load() == 1 && (int) pPhase->load() > 0 && computeStaticHash() != builtHash) triggerAsyncUpdate();
}

void MexProcessor::startLearn()
{
    learner.reset();
    learnSec.store (0);
    learner.learning.store (true);
    lastLearnSummary = "Listening...";
}

void MexProcessor::finishLearn()
{
    learner.learning.store (false);
    learner.pump();
    const auto res = learner.analyse (6);
    if (learner.usableFrames() < 40)
    {
        lastLearnSummary = "Not enough audio heard - play 10-30 s, then press LEARN again.";
        return;
    }
    // replace previously learned bands
    for (int i = 0; i < NBANDS; ++i)
        if (ptrs[i].v[16]->load() > 0.5f) resetBand (i);
    int added = 0;
    juce::StringArray parts;
    for (auto& r : res)
    {
        const int i = findFreeBand();
        if (i < 0) break;
        setParam (pid (i, "type"), (float) Type::Peak);
        setParam (pid (i, "order"), (float) Order::Two);
        setParam (pid (i, "freq"), (float) r.freq);
        setParam (pid (i, "gain"), 0.0f);
        setParam (pid (i, "q"), (float) r.q);
        setParam (pid (i, "char"), 0.0f);           // surgical: Clean
        setParam (pid (i, "dyn"), 1.0f);
        setParam (pid (i, "range"), (float) r.rangeDb);
        setParam (pid (i, "autothr"), 1.0f);
        setParam (pid (i, "toff"), r.dynamicKind ? 3.0f : -10.0f);   // esses: only on bursts; resonances: held down
        setParam (pid (i, "attack"), r.dynamicKind ? 2.0f : 10.0f);
        setParam (pid (i, "release"), r.dynamicKind ? 60.0f : 150.0f);
        setParam (pid (i, "learned"), 1.0f);
        setParam (pid (i, "used"), 1.0f);
        parts.add (juce::String (r.freq >= 1000 ? juce::String (r.freq / 1000.0, 2) + " kHz" : juce::String ((int) r.freq) + " Hz"));
        ++added;
    }
    lastLearnSummary = added == 0 ? "No problem frequencies found - sounds clean."
                                  : "Learned " + juce::String (added) + " dynamic cuts: " + parts.joinIntoString (", ");
}

void MexProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainBuf = getBusBuffer (buffer, true, 0);
    const int n = mainBuf.getNumSamples(), nch = juce::jmin (2, mainBuf.getNumChannels());
    if (n == 0 || nch == 0) return;

    for (int i = 0; i < NBANDS; ++i) readBand (i, bp[i]);
    GlobalParams gp; readGlobals (gp);

    // sidechain (optional)
    const float* scPtr[2] = { nullptr, nullptr };
    bool haveSc = false;
    if (getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        auto scBuf = getBusBuffer (buffer, true, 1);
        if (scBuf.getNumChannels() > 0)
        {
            scPtr[0] = scBuf.getReadPointer (0);
            scPtr[1] = scBuf.getReadPointer (juce::jmin (1, scBuf.getNumChannels() - 1));
            haveSc = true;
        }
    }

    // analyzer + learn: input
    learner.push (mainBuf.getReadPointer (0), nch > 1 ? mainBuf.getReadPointer (1) : nullptr, n);
    const int p0 = anPos.load();
    for (int i = 0; i < n; ++i)
        anPre[(size_t) ((p0 + i) & (AN - 1))] = nch > 1 ? 0.5f * (mainBuf.getSample (0, i) + mainBuf.getSample (1, i)) : mainBuf.getSample (0, i);

    const int mode = currentMode();
    float* ch[2] = { mainBuf.getWritePointer (0), mainBuf.getWritePointer (nch > 1 ? 1 : 0) };

    if (mode == 0 || mode == 3)
    {
        coreBase.process (ch, nch, n, bp, gp, haveSc ? scPtr : nullptr, false);
        if (mode == 3 && osLatency > 0)
        {   // delay-match the LIVE path to the HQ latency reported in AUTO
            const int D = osLatency + 1;
            for (int i = 0; i < n; ++i)
            {
                for (int c = 0; c < nch; ++c)
                {
                    float* d = delayBuf.getWritePointer (c);
                    const float in = ch[c][i];
                    ch[c][i] = d[(delayPos + 1) % D];
                    d[delayPos] = in;
                }
                delayPos = (delayPos + 1) % D;
            }
        }
    }
    else if (mode == 1)
    {
        juce::dsp::AudioBlock<float> block (mainBuf);
        auto up = os->processSamplesUp (block.getSubsetChannelBlock (0, (size_t) nch));
        const int un = (int) up.getNumSamples();
        float* uch[2] = { up.getChannelPointer (0), up.getChannelPointer (nch > 1 ? 1 : 0) };
        const float* usc[2] = { nullptr, nullptr };
        if (haveSc)
        {
            for (int c = 0; c < 2; ++c)
            {
                float* d = scUp.getWritePointer (c);
                for (int i = 0; i < n; ++i) d[2 * i] = d[2 * i + 1] = scPtr[c][i];
                usc[c] = d;
            }
        }
        coreOS.process (uch, nch, un, bp, gp, haveSc ? usc : nullptr, false);
        auto down = block.getSubsetChannelBlock (0, (size_t) nch);
        os->processSamplesDown (down);
    }
    else
    {
        if (nch == 2)
        {
            juce::dsp::AudioBlock<float> block (mainBuf);
            auto sub = block.getSubsetChannelBlock (0, 2);
            convLR.process (juce::dsp::ProcessContextReplacing<float> (sub));
            for (int i = 0; i < n; ++i) { const float l = ch[0][i], r = ch[1][i]; ch[0][i] = 0.5f * (l + r); ch[1][i] = 0.5f * (l - r); }
            convMS.process (juce::dsp::ProcessContextReplacing<float> (sub));
            for (int i = 0; i < n; ++i) { const float m = ch[0][i], s = ch[1][i]; ch[0][i] = m + s; ch[1][i] = m - s; }
        }
        coreBase.process (ch, nch, n, bp, gp, haveSc ? scPtr : nullptr, true);
    }

    for (int i = 0; i < n; ++i)
        anPost[(size_t) ((p0 + i) & (AN - 1))] = nch > 1 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
    anPos.store ((p0 + n) & (AN - 1));
    if (nch == 1 && buffer.getNumChannels() > 1 && mainBuf.getNumChannels() > 1) mainBuf.copyFrom (1, 0, mainBuf, 0, 0, n);

    // latency changes (mode switches) are applied on the message thread
    const int q = (int) pQuality->load(), ph = (int) pPhase->load();
    const int want = q == 2 ? osLatency : q == 1 ? (ph == 0 ? osLatency : firSizeFor (ph)) : 0;
    if (want != reportedLatency) triggerAsyncUpdate();
}

void MexProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void MexProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (state.state.getType())) state.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* MexProcessor::createEditor() { return new MexEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MexProcessor(); }
