#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0f1014), panel (0xff191b21), panel2 (0xff22252d),
                       accent (0xff9fd3e6), gold (0xffe0b25a), text (0xffe8e8ea), dim (0xff868a94),
                       rawCol (0xff8f939c), mixCol (0xff9fd3e6), refCol (0xffe0b25a);

    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const char* scaleIds[]  = { "major", "minor", "harmonic minor", "phrygian", "chromatic" };
    const char* scaleNames[] = { "Major", "Minor", "Harmonic minor", "Phrygian", "Chromatic" };

    juce::var makeError (const juce::String& msg)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", msg);
        return juce::var (o);
    }

    juce::String mmss (double s)
    {
        const int t = (int) juce::jmax (0.0, s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    juce::String prettyName (const juce::File& f)
    {
        return f.getFileNameWithoutExtension().replaceCharacter ('_', ' ');
    }

    // deep copy of a JSON var so overrides never touch the loaded preset
    juce::var cloneVar (const juce::var& v)
    {
        return juce::JSON::parse (juce::JSON::toString (v));
    }
}

//==============================================================================
void EngineJob::run()
{
    juce::ChildProcess p;
    juce::var result;
    if (! p.start (args, juce::ChildProcess::wantStdOut))
        result = makeError ("Couldn't start the engine:\n" + args[0]);
    else
    {
        const auto out = p.readAllProcessOutput().trim();
        auto lines = juce::StringArray::fromLines (out);
        lines.removeEmptyStrings();
        result = lines.isEmpty() ? juce::var() : juce::JSON::parse (lines[lines.size() - 1]);
        if (! result.isObject())
            result = makeError ("The engine didn't answer properly:\n" + out.substring (0, 400));
    }
    juce::MessageManager::callAsync ([cb = callback, result] { cb (result); });
}

void RenderJob::run()
{
    bool ok = false;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (input) })
    {
        const int n = (int) r->lengthInSamples;
        const double fileRate = r->sampleRate;
        juce::AudioBuffer<float> b ((int) r->numChannels, n);
        r->read (&b, 0, n, 0, true, true);
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            double s = 0;
            for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, i);
            x[(size_t) i] = (float) (s / b.getNumChannels());
        }
        // same engine as LIVE, run over the whole file
        mwl::Engine e;
        e.prepare (fileRate, P, verbType);
        if (autoInput) e.seedAuto (mwl::activeRmsDb (x.data(), n, fileRate));
        const int lat = e.latency();
        x.resize ((size_t) (n + lat), 0.0f);
        std::vector<float> L (x.size()), R (x.size());
        for (int start = 0; start < (int) x.size() && ! threadShouldExit(); start += 4096)
        {
            const int k = juce::jmin (4096, (int) x.size() - start);
            e.process (x.data() + start, L.data() + start, R.data() + start, k, macros, inGainDb, autoInput);
        }
        if (! threadShouldExit())
        {
            juce::AudioBuffer<float> o (2, n);
            for (int i = 0; i < n; ++i)
            {
                o.setSample (0, i, L[(size_t) (i + lat)]);
                o.setSample (1, i, R[(size_t) (i + lat)]);
            }
            auto write = [&o, fileRate] (const juce::File& f, int bits)
            {
                f.deleteFile();
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (
                    new juce::FileOutputStream (f), fileRate, 2, bits, {}, 0));
                return w != nullptr && w->writeFromAudioSampleBuffer (o, 0, o.getNumSamples());
            };
            ok = write (output, 32);
            if (ok && exportFile != juce::File())
                write (exportFile, 24);
        }
    }
    if (! threadShouldExit())
        juce::MessageManager::callAsync ([cb = callback, ok] { cb (ok); });
}

//==============================================================================
Lane::Lane (juce::AudioFormatManager& fm, juce::AudioThumbnailCache& cache, juce::String t,
            juce::Colour c, bool draggable)
    : thumb (512, fm, cache), title (std::move (t)), colour (c), canDrag (draggable)
{
    thumb.addChangeListener (this);
}

Lane::~Lane() { thumb.removeChangeListener (this); }

void Lane::setFile (const juce::File& f)
{
    thumb.clear();
    if (f.existsAsFile())
        thumb.setSource (new juce::FileInputSource (f));
    repaint();
}

juce::Rectangle<int> Lane::waveArea() const
{
    auto r = getLocalBounds().reduced (0, 2);
    r.removeFromLeft (110);
    return r;
}

juce::Rectangle<int> Lane::handleArea() const
{
    return canDrag ? waveArea().removeFromRight (40) : juce::Rectangle<int>();
}

void Lane::paint (juce::Graphics& g)
{
    auto wa = waveArea();
    g.setColour (panel2);
    g.fillRoundedRectangle (wa.toFloat(), 5.0f);
    g.setColour (active ? colour : dim);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, 6, 0, 104, getHeight(), juce::Justification::centredLeft);

    auto inner = wa.reduced (6, 3);
    inner.removeFromRight (40);              // same width on both lanes so the timelines line up
    if (thumb.getTotalLength() > 0)
    {
        float lo = 0, hi = 0;
        thumb.getApproximateMinMax (0.0, thumb.getTotalLength(), 0, lo, hi);
        const float peak = juce::jmax (std::abs (lo), std::abs (hi), 0.001f);
        g.setColour (colour.withAlpha (active ? 0.9f : 0.35f));
        thumb.drawChannel (g, inner, 0.0, thumb.getTotalLength(), 0, juce::jlimit (1.0f, 8.0f, 0.95f / peak));
    }
    if (playhead)
    {
        const float x = (float) inner.getX() + playhead() * (float) inner.getWidth();
        g.setColour (gold);
        g.drawLine (x, (float) wa.getY(), x, (float) wa.getBottom(), 1.5f);
    }
    if (canDrag)
    {
        const bool ready = exportFile.existsAsFile();
        auto h = handleArea().reduced (4);
        g.setColour (ready ? (hover ? gold : gold.withAlpha (0.7f)) : dim.withAlpha (0.35f));
        g.fillRoundedRectangle (h.toFloat(), 5.0f);
        g.setColour (bg);
        for (int i = 0; i < 3; ++i)
        {
            const float y = (float) h.getCentreY() - 7.0f + (float) i * 7.0f;
            g.drawLine ((float) h.getX() + 9.0f, y, (float) h.getRight() - 9.0f, y, 2.0f);
        }
    }
}

void Lane::mouseMove (const juce::MouseEvent& e)
{
    const bool over = canDrag && handleArea().contains (e.getPosition());
    if (over != hover) { hover = over; repaint(); }
    setMouseCursor (over && exportFile.existsAsFile() ? juce::MouseCursor::DraggingHandCursor
                                                      : waveArea().contains (e.getPosition())
                                                            ? juce::MouseCursor::PointingHandCursor
                                                            : juce::MouseCursor::NormalCursor);
}

void Lane::mouseExit (const juce::MouseEvent&) { hover = false; repaint(); }

void Lane::mouseDown (const juce::MouseEvent& e)
{
    pressOnHandle = canDrag && handleArea().contains (e.getPosition());
    if (pressOnHandle) return;
    auto inner = waveArea().reduced (6, 3);
    inner.removeFromRight (40);
    if (inner.contains (e.getPosition()) && onSeek)
        onSeek ((float) (e.x - inner.getX()) / (float) juce::jmax (1, inner.getWidth()));
}

void Lane::mouseDrag (const juce::MouseEvent& e)
{
    if (! pressOnHandle || dragging || e.getDistanceFromDragStart() < 5 || ! exportFile.existsAsFile())
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray { exportFile.getFullPathName() }, false, this,
        [safe = juce::Component::SafePointer<Lane> (this)] { if (safe != nullptr) safe->dragging = false; });
}

//==============================================================================
Knob::Knob (const juce::String& name, double lo, double hi, double def, const juce::String& suffix)
{
    slider.setRange (lo, hi, (hi - lo) > 20 ? 1.0 : 0.01);
    slider.setValue (def, juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, def);
    slider.setTextValueSuffix (suffix);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setColour (juce::Slider::rotarySliderOutlineColourId, panel2.brighter (0.15f));
    slider.setColour (juce::Slider::thumbColourId, gold);
    slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    label.setText (name, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setColour (juce::Label::textColourId, dim);
    label.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (slider);
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (16));
    slider.setBounds (r);
}

//==============================================================================
MirrorEditor::MirrorEditor (MirrorProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    formats.registerBasicFormats();

    for (auto* l : { &rawName, &info, &refName, &keyLabel, &scaleLabel, &preLabel, &timeLabel, &status })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &chainLabel, &emuLabel, &tuneLabel, &toneLabel })
    {
        l->setColour (juce::Label::textColourId, dim);
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (*l);
    }
    info.setColour (juce::Label::textColourId, accent);
    info.setJustificationType (juce::Justification::centredRight);
    refName.setColour (juce::Label::textColourId, dim);
    timeLabel.setJustificationType (juce::Justification::centred);

    micLabel.setColour (juce::Label::textColourId, dim);
    micLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (micLabel);
    micBox.addItem ("Off (as recorded)", 1);
    {
        auto dir = micDir();
        auto files = dir.isDirectory() ? dir.findChildFiles (juce::File::findFiles, false, "*.json") : juce::Array<juce::File>();
        files.sort();
        for (auto& f : files)
        {
            const auto v = juce::JSON::parse (f);
            if (! v["eq"].isObject()) continue;
            micProfiles.add (v);
            micBox.addItem (v["name"].toString(), micProfiles.size() + 1);
        }
    }
    micBox.setSelectedId (juce::jlimit (0, micProfiles.size(), proc.micIndex) + 1, juce::dontSendNotification);
    micBox.setTooltip ("Make your current mic sound like the one your chains were built on. "
                       "Applies to your stock chains; Emulator presets are fitted to your mic directly.");
    micBox.onChange = [this] { proc.micIndex = micBox.getSelectedId() - 1; pushLive(); requestRender (50); };
    addAndMakeVisible (micBox);

    addAndMakeVisible (loadRaw);
    loadRaw.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    loadRaw.onClick = [this] { chooseRaw(); };

    presetBox.onChange = [this] { selectPreset (presetBox.getSelectedId() - 1, true); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (loadRef);
    loadRef.onClick = [this] { chooseRef(); };
    addAndMakeVisible (buildBtn);
    buildBtn.setColour (juce::TextButton::buttonColourId, gold.darker (0.5f));
    buildBtn.setTooltip ("Read the reference vocal and build a new chain for your raw vocal");
    buildBtn.onClick = [this] { buildFromReference(); };
    addAndMakeVisible (deletePreset);
    deletePreset.setTooltip ("Delete this Emulator preset (stock chains can't be deleted)");
    deletePreset.onClick = [this]
    {
        const int i = presetBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, presetFiles.size())) return;
        if (! presetFiles[i].isAChildOf (userDir())) return;
        presetFiles[i].deleteFile();
        scanPresets();
    };

    addAndMakeVisible (saveTweaks);
    saveTweaks.setTooltip ("Save your knob tweaks, preamp, Space and tape speed into this preset (stock chains get a '(tweaked)' copy)");
    saveTweaks.onClick = [this] { saveTweaksToPreset(); };

    keyBox.addItem ("Auto", 1);
    for (int i = 0; i < 12; ++i) keyBox.addItem (noteNames[i], i + 2);
    keyBox.setSelectedId (proc.keyIndex + 1, juce::dontSendNotification);
    keyBox.onChange = [this] { proc.keyIndex = keyBox.getSelectedId() - 1; needTune = true; requestRender (50); };
    for (int i = 0; i < 5; ++i) scaleBox.addItem (scaleNames[i], i + 1);
    scaleBox.setSelectedItemIndex (proc.scaleIndex, juce::dontSendNotification);
    scaleBox.onChange = [this] { proc.scaleIndex = scaleBox.getSelectedItemIndex(); needTune = true; requestRender (50); };
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    addAndMakeVisible (keyFromBeat);
    keyFromBeat.setTooltip ("Read the key from the instrumental");
    keyFromBeat.onClick = [this] { chooseBeat(); };

    for (auto* k : { &speed, &amount, &humanize })
    {
        addAndMakeVisible (*k);
        k->slider.onDragEnd = [this] { syncKnobsToProc(); needTune = true; requestRender (50); };
    }
    for (auto* k : { &drive, &comp, &deess, &gateK, &fx, &delay, &reverb, &out })
    {
        addAndMakeVisible (*k);
        k->slider.onValueChange = [this] { syncKnobsToProc(); requestRender (300); };
    }
    drive.slider.setValue (proc.knobDrive, juce::dontSendNotification);
    comp.slider.setValue (proc.knobComp, juce::dontSendNotification);
    deess.slider.setValue (proc.knobDeess, juce::dontSendNotification);
    gateK.slider.setValue (proc.knobGate, juce::dontSendNotification);
    fx.slider.setValue (proc.knobFx, juce::dontSendNotification);
    delay.slider.setValue (proc.knobDelay, juce::dontSendNotification);
    reverb.slider.setValue (proc.knobReverb, juce::dontSendNotification);
    out.slider.setValue (proc.knobOut, juce::dontSendNotification);

    for (auto* s : { "From chain", "Tube 737-style", "Console 73-style", "Clean" })
        preBox.addItem (s, preBox.getNumItems() + 1);
    preBox.setSelectedId (1, juce::dontSendNotification);
    preBox.onChange = [this] { pushLive(); requestRender (50); };

    // ---- 2.0: LIVE / HQ, input, space
    modeBtn.setClickingTogglesState (true);
    modeBtn.setToggleState (proc.isLive(), juce::dontSendNotification);
    modeBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffd04545));
    modeBtn.setTooltip ("LIVE: your FL audio runs through the chain in real time (any preset in the bank). "
                        "HQ: tune + render the loaded raw vocal file and preview it here.");
    modeBtn.onClick = [this] { proc.setLiveMode (modeBtn.getToggleState()); if (proc.isLive()) pushLive(); refresh(); };
    addAndMakeVisible (modeBtn);
    autoBtn.setClickingTogglesState (true);
    autoBtn.setToggleState (proc.autoInput.load(), juce::dontSendNotification);
    autoBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.4f));
    autoBtn.setTooltip ("Auto input: rides your vocal to the level the chains were built at (-20 dB). Off: use the Input knob.");
    autoBtn.onClick = [this] { proc.autoInput = autoBtn.getToggleState(); refresh(); requestRender (100); };
    addAndMakeVisible (autoBtn);
    inKnob.slider.setValue (proc.inGainDb.load(), juce::dontSendNotification);
    inKnob.slider.onValueChange = [this] { proc.inGainDb = inKnob.slider.getValue(); requestRender (300); };
    addAndMakeVisible (inKnob);
    spaceLabel.setColour (juce::Label::textColourId, dim);
    addAndMakeVisible (spaceLabel);
    spaceBox.addItem ("Room (as fitted)", 1);
    spaceBox.addItem ("Plate (Abbey Road style)", 2);
    spaceBox.setSelectedId (proc.verbType + 1, juce::dontSendNotification);
    spaceBox.onChange = [this] { proc.verbType = spaceBox.getSelectedId() - 1; pushLive(); requestRender (50); };
    addAndMakeVisible (spaceBox);
    liveInfo.setColour (juce::Label::textColourId, dim);
    liveInfo.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (liveInfo);
    lowLatBtn.setClickingTogglesState (true);
    lowLatBtn.setToggleState (proc.lowLatency.load(), juce::dontSendNotification);
    lowLatBtn.setColour (juce::TextButton::buttonOnColourId, gold.darker (0.5f));
    lowLatBtn.setTooltip ("LIVE: no limiter look-ahead (0 latency) for recording through MIRRORWALL. Off: 2 ms look-ahead, cleaner peaks.");
    lowLatBtn.onClick = [this] { proc.setLowLatency (lowLatBtn.getToggleState()); };
    addAndMakeVisible (lowLatBtn);
    speedBtn.setClickingTogglesState (true);
    speedBtn.setToggleState (proc.tape30.load(), juce::dontSendNotification);
    speedBtn.setButtonText (proc.tape30.load() ? "Tape 30 ips" : "Tape 15 ips");
    speedBtn.setTooltip ("Tape speed: 15 ips = warmer (more low bump, softer highs); 30 ips = cleaner, tighter");
    speedBtn.onClick = [this]
    {
        proc.tape30 = speedBtn.getToggleState();
        speedBtn.setButtonText (proc.tape30.load() ? "Tape 30 ips" : "Tape 15 ips");
        requestRender (100);
    };
    addAndMakeVisible (speedBtn);
    for (auto* k : { &tapeK, &airK })
    {
        addAndMakeVisible (*k);
        k->slider.onValueChange = [this] { syncKnobsToProc(); requestRender (300); };
    }
    tapeK.slider.setValue (proc.knobTape.load(), juce::dontSendNotification);
    airK.slider.setValue (proc.knobAir.load(), juce::dontSendNotification);
    tapeK.slider.setTooltip ("Tape colour: saturation, low bump, softer highs, a little wow/flutter (0 = off)");
    airK.slider.setTooltip ("Air: Aphex-style exciter, adds presence/sparkle above 4 kHz (0 = off)");
    addAndMakeVisible (preBox);

    rawLane = std::make_unique<Lane> (formats, thumbCache, "RAW", rawCol, false);
    mixLane = std::make_unique<Lane> (formats, thumbCache, "MIRRORWALL", mixCol, true);
    refLane = std::make_unique<Lane> (formats, thumbCache, "REFERENCE", refCol, false);
    if (proc.refFile.existsAsFile()) refLane->setFile (proc.refFile);
    for (auto* l : { rawLane.get(), mixLane.get(), refLane.get() })
    {
        l->onSeek = [this] (float f) { proc.seek (f); };
        l->playhead = [this] { return proc.previewProgress(); };
        addAndMakeVisible (*l);
    }

    addAndMakeVisible (play);
    play.setColour (juce::TextButton::buttonColourId, accent.darker (0.6f));
    play.onClick = [this]
    {
        if (proc.isPreviewing()) { proc.stopPreview(); return; }
        if (proc.previewProgress() >= 0.999f) proc.seek (0.0f);
        proc.startPreviewFromHere();
    };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.4f));
    loopBtn.setToggleState (proc.loop.load(), juce::dontSendNotification);
    loopBtn.onClick = [this] { proc.loop = loopBtn.getToggleState(); };
    addAndMakeVisible (loopBtn);
    abBtn.setTooltip ("A/B/C: cycle RAW -> MIRRORWALL -> REFERENCE (all level matched, same playhead)");
    abBtn.onClick = [this]
    {
        proc.hearMode = (proc.hearMode + 1) % 3;
        if (proc.hearMode == 2 && ! proc.refFile.existsAsFile()) proc.hearMode = 0;
        const bool was = proc.isPreviewing();
        rebuildPreview();
        if (was) proc.startPreviewFromHere();
        refresh();
    };
    addAndMakeVisible (abBtn);
    addAndMakeVisible (openFolder);
    openFolder.onClick = [this] { exportDir().startAsProcess(); };
    addChildComponent (progressBar);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, accent);

    setResizable (true, true);
    setResizeLimits (980, 640, 2000, 1300);
    setSize (1180, 720);            // sizing last: every child exists before the first layout

    if (proc.sessionDir == juce::File())
    {
        proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("MIRRORWALL")
                              .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
        proc.sessionDir.createDirectory();
    }
    scanPresets (proc.presetName);
    if (proc.rawFile.existsAsFile())
    {
        rawLane->setFile (proc.rawFile);
        if (mixFile().existsAsFile())
        {
            mixLane->setFile (mixFile());
            mixLane->setExport (exportFile());
            needTune = false;
            rebuildPreview();
        }
    }
    refresh();
    startTimerHz (25);

    const auto dev = juce::SystemStats::getEnvironmentVariable ("MIRRORWALL_DEV_RAW", {});
    if (dev.isNotEmpty() && ! proc.rawFile.existsAsFile() && juce::File (dev).existsAsFile())
    {
        proc.rawFile = juce::File (dev);
        rawLane->setFile (proc.rawFile);
        needTune = true;
        requestRender (100);
    }
}

MirrorEditor::~MirrorEditor()
{
    stopTimer();
    renderJob.reset();
    job.reset();
}

//==============================================================================
juce::File MirrorEditor::engineExe() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("MIRRORWALL_ENGINE", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);
   #if JUCE_WINDOWS
    const juce::String exeName ("mirrorwall-engine.exe");
   #else
    const juce::String exeName ("mirrorwall-engine");
   #endif
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/engine"),
                      me.getParentDirectory().getChildFile ("engine") })
        if (dir.getChildFile (exeName).existsAsFile())
            return dir.getChildFile (exeName);
    return {};
}

juce::File MirrorEditor::stockDir() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("MIRRORWALL_PRESETS", {});
    if (env.isNotEmpty() && juce::File (env).isDirectory())
        return juce::File (env);
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/presets"),
                      me.getParentDirectory().getChildFile ("presets") })
        if (dir.isDirectory())
            return dir;
    return {};
}

juce::File MirrorEditor::micDir() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("MIRRORWALL_MICS", {});
    if (env.isNotEmpty() && juce::File (env).isDirectory())
        return juce::File (env);
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/mics"),
                      me.getParentDirectory().getChildFile ("mics") })
        if (dir.isDirectory())
            return dir;
    return {};
}

bool MirrorEditor::presetIsStock() const
{
    return juce::isPositiveAndBelow (presetIndex, presetFiles.size()) && ! presetFiles[presetIndex].isAChildOf (userDir());
}

juce::File MirrorEditor::userDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("MIRRORWALL").getChildFile ("Presets");
    d.createDirectory();
    return d;
}

juce::File MirrorEditor::exportDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("MIRRORWALL").getChildFile ("Exports");
    d.createDirectory();
    return d;
}

juce::File MirrorEditor::exportFile() const
{
    auto clean = [] (juce::String s) { return s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-").trim(); };
    return exportDir().getChildFile (clean (proc.rawFile.getFileNameWithoutExtension()) + " - "
                                     + clean (proc.presetName) + ".wav");
}

void MirrorEditor::runEngine (juce::StringArray args, std::function<void (juce::var)> onOk)
{
    const auto exe = engineExe();
    if (! exe.existsAsFile())
    {
        proc.status = "Engine not found inside the plugin folder (Contents/Resources/engine).";
        refresh();
        return;
    }
    proc.sessionDir.getChildFile ("progress.txt").deleteFile();
    args.insert (0, exe.getFullPathName());
    args.addArray ({ "--progress", proc.sessionDir.getChildFile ("progress.txt").getFullPathName() });
    busy = true;
    refresh();
    juce::Component::SafePointer<MirrorEditor> safe (this);
    job = std::make_unique<EngineJob> (args, [safe, onOk] (juce::var r)
    {
        if (safe == nullptr) return;
        safe->busy = false;
        if (r["ok"]) onOk (r);
        else safe->proc.status = "Problem: " + r["error"].toString();
        safe->refresh();
        if (safe->pendingRender) { safe->pendingRender = false; safe->update(); }
    });
    job->startThread();
}

//==============================================================================
void MirrorEditor::scanPresets (const juce::String& select)
{
    presetFiles.clear();
    presetBox.clear (juce::dontSendNotification);
    int id = 1, want = 1;
    auto add = [&] (const juce::File& dir, const juce::String& header)
    {
        if (! dir.isDirectory()) return;
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.json");
        if (files.isEmpty()) return;
        files.sort();
        presetBox.addSectionHeading (header);
        for (auto& f : files)
        {
            presetFiles.add (f);
            presetBox.addItem (prettyName (f), id);
            if (prettyName (f) == select) want = id;
            ++id;
        }
    };
    add (stockDir(), "YOUR VOCAL CHAINS");
    add (userDir(), "EMULATOR PRESETS");
    if (presetFiles.isEmpty())
    {
        proc.status = "No presets found (stock presets should be inside the plugin folder).";
        return;
    }
    presetBox.setSelectedId (want, juce::dontSendNotification);
    selectPreset (want - 1, proc.tuneSpeed < 0);
}

void MirrorEditor::selectPreset (int index, bool resetTuning)
{
    if (! juce::isPositiveAndBelow (index, presetFiles.size())) return;
    preset = juce::JSON::parse (presetFiles[index]);
    presetIndex = index;
    proc.presetName = prettyName (presetFiles[index]);
    if (resetTuning)
    {
        const auto t = preset["tune"];
        speed.slider.setValue (mw::num (t, "speed", 20.0), juce::dontSendNotification);
        amount.slider.setValue (mw::num (t, "amount", 1.0), juce::dontSendNotification);
        humanize.slider.setValue (mw::num (t, "humanize", 0.0), juce::dontSendNotification);
        syncKnobsToProc();
    }
    else
    {
        speed.slider.setValue (proc.tuneSpeed, juce::dontSendNotification);
        amount.slider.setValue (proc.tuneAmount, juce::dontSendNotification);
        humanize.slider.setValue (proc.tuneHumanize, juce::dontSendNotification);
    }
    deletePreset.setEnabled (presetFiles[index].isAChildOf (userDir()));
    applyStoredTweaks();
    pushLive();
    requestRender (50);
    refresh();
}

void MirrorEditor::syncKnobsToProc()
{
    proc.tuneSpeed = speed.slider.getValue();
    proc.tuneAmount = amount.slider.getValue();
    proc.tuneHumanize = humanize.slider.getValue();
    proc.knobDrive = drive.slider.getValue();
    proc.knobComp = comp.slider.getValue();
    proc.knobDeess = deess.slider.getValue();
    proc.knobGate = gateK.slider.getValue();
    proc.knobFx = fx.slider.getValue();
    proc.knobDelay = delay.slider.getValue();
    proc.knobReverb = reverb.slider.getValue();
    proc.knobOut = out.slider.getValue();
    proc.knobTape = tapeK.slider.getValue();
    proc.knobAir = airK.slider.getValue();
}

juce::String MirrorEditor::tuneSignature() const
{
    return proc.rawFile.getFullPathName() + "|" + juce::String (proc.keyIndex) + "|" + juce::String (proc.scaleIndex)
           + "|" + juce::String (proc.tuneSpeed, 1) + "|" + juce::String (proc.tuneAmount, 2)
           + "|" + juce::String (proc.tuneHumanize, 2);
}

void MirrorEditor::requestRender (int delayMs)
{
    if (! proc.rawFile.existsAsFile()) return;
    needRender = true;
    renderDue = juce::Time::getMillisecondCounter() + (juce::uint32) delayMs;
}

void MirrorEditor::update()
{
    if (! proc.rawFile.existsAsFile() || ! preset.isObject()) return;
    if (busy) { pendingRender = true; return; }
    if (needTune || tunedKey != tuneSignature() || ! tunedFile().existsAsFile())
    {
        const auto sig = tuneSignature();
        juce::StringArray args { "tune", "--in", proc.rawFile.getFullPathName(),
                                 "--out", tunedFile().getFullPathName(),
                                 "--key", proc.keyIndex == 0 ? juce::String ("auto") : juce::String (noteNames[proc.keyIndex - 1]),
                                 "--scale", scaleIds[juce::jlimit (0, 4, proc.scaleIndex)],
                                 "--speed", juce::String (proc.tuneSpeed, 1),
                                 "--amount", juce::String (proc.tuneAmount, 2),
                                 "--humanize", juce::String (proc.tuneHumanize, 2) };
        proc.status = "Tuning...";
        runEngine (args, [this, sig] (juce::var r)
        {
            needTune = false;
            tunedKey = sig;
            if (r["key"].isString())
                info.setText ("Tuned to " + r["key"].toString() + " " + r["scale"].toString(), juce::dontSendNotification);
            startRender();
        });
        return;
    }
    startRender();
}

juce::var MirrorEditor::effectivePreset() const
{
    auto P = cloneVar (preset);
    const int pre = preBox.getSelectedId();
    if (pre > 1)
        if (auto* o = P["pre"].getDynamicObject())
        {
            o->setProperty ("type", pre == 2 ? "tube" : pre == 3 ? "console" : "clean");
            if (! o->hasProperty ("drive")) o->setProperty ("drive", 6.0);
            if (! o->hasProperty ("mix")) o->setProperty ("mix", 0.55);
        }
    if (proc.micIndex > 0 && proc.micIndex <= micProfiles.size() && presetIsStock())
        if (auto* o = P.getDynamicObject())
            o->setProperty ("mic_eq", cloneVar (micProfiles[proc.micIndex - 1]["eq"]));
    return P;
}

void MirrorEditor::pushLive()
{
    if (preset.isObject()) proc.setLivePreset (effectivePreset());
}

void MirrorEditor::startRender()
{
    if (! tunedFile().existsAsFile() || ! preset.isObject()) return;
    auto P = effectivePreset();
    const auto m = proc.macros();
    renderJob.reset();                 // cancels an older render
    rendering = true;
    const int gen = ++renderGeneration;
    proc.status = "Rendering " + proc.presetName + "...";
    refresh();
    juce::Component::SafePointer<MirrorEditor> safe (this);
    renderJob = std::make_unique<RenderJob> (tunedFile(), P, m, proc.verbType, proc.autoInput.load(), proc.inGainDb.load(),
                                             mixFile(), exportFile(), [safe, gen] (bool ok)
    {
        if (safe == nullptr || gen != safe->renderGeneration) return;
        safe->rendering = false;
        if (! ok) { safe->proc.status = "Render failed."; safe->refresh(); return; }
        safe->mixLane->setFile (safe->mixFile());
        safe->mixLane->setExport (safe->exportFile());
        const bool was = safe->proc.isPreviewing();
        safe->rebuildPreview();
        if (was) safe->proc.startPreviewFromHere();
        safe->proc.status = "Ready: " + safe->proc.presetName + ". Drag the gold grip into FL.";
        safe->refresh();
    });
    renderJob->startThread();
}

void MirrorEditor::rebuildPreview()
{
    rawLane->active = proc.hearMode == 0;
    mixLane->active = proc.hearMode == 1;
    refLane->active = proc.hearMode == 2;
    for (auto* l : { rawLane.get(), mixLane.get(), refLane.get() }) l->repaint();
    auto read = [this] (const juce::File& f, juce::AudioBuffer<float>& b, double& rate)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) return false;
        rate = r->sampleRate;
        b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        return true;
    };
    // loudness of the active (sung) part, so quiet gaps don't skew the match
    auto activeRms = [] (juce::AudioBuffer<float>& b)
    {
        const int hop = 2205, n = b.getNumSamples() / hop;
        std::vector<double> fr;
        for (int k = 0; k < n; ++k)
        {
            double s = 0;
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = k * hop; i < (k + 1) * hop; ++i) s += (double) b.getSample (c, i) * b.getSample (c, i);
            fr.push_back (s / (hop * juce::jmax (1, b.getNumChannels())));
        }
        if (fr.empty()) return 1e-9;
        std::sort (fr.begin(), fr.end());
        double s = 0;
        for (size_t k = fr.size() / 2; k < fr.size(); ++k) s += fr[k];
        return std::sqrt (s / (double) (fr.size() - fr.size() / 2)) + 1e-9;
    };
    auto finish = [] (juce::AudioBuffer<float>& b)
    {
        const auto pk = b.getMagnitude (0, b.getNumSamples());
        if (pk > 0.98f) b.applyGain (0.98f / pk);
        if (b.getNumChannels() == 1) { b.setSize (2, b.getNumSamples(), true); b.copyFrom (1, 0, b, 0, 0, b.getNumSamples()); }
    };
    juce::AudioBuffer<float> mix, other;
    double rm = 44100, ro = 44100;
    const bool haveMix = read (mixFile(), mix, rm);
    if (proc.hearMode == 1 && haveMix) { proc.setPreview (std::move (mix), rm); return; }
    const juce::File src = proc.hearMode == 2 ? proc.refFile : proc.rawFile;
    if (! read (src, other, ro)) { if (haveMix) proc.setPreview (std::move (mix), rm); return; }
    if (haveMix) other.applyGain ((float) (activeRms (mix) / activeRms (other)));   // level-matched A/B/C
    finish (other);
    proc.setPreview (std::move (other), ro);
}

//==============================================================================
void MirrorEditor::chooseRaw()
{
    chooser = std::make_unique<juce::FileChooser> ("Your raw (dry) vocal", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              proc.rawFile = f;
                              rememberRaw (f);
                              rawLane->setFile (f);
                              mixLane->setFile ({});
                              mixLane->setExport ({});
                              proc.seek (0.0f);
                              needTune = true;
                              requestRender (50);
                              refresh();
                          });
}

void MirrorEditor::chooseRef()
{
    chooser = std::make_unique<juce::FileChooser> ("Reference vocal (an acapella you want to sound like)",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              setReference (f);
                          });
}

void MirrorEditor::chooseBeat()
{
    chooser = std::make_unique<juce::FileChooser> ("Instrumental (to read the key)", juce::File(),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile() || busy) return;
                              proc.beatFile = f;
                              proc.status = "Reading the key...";
                              runEngine ({ "key", "--file", f.getFullPathName() }, [this] (juce::var r)
                              {
                                  const auto k = r["key"].toString();
                                  for (int i = 0; i < 12; ++i)
                                      if (k == noteNames[i]) keyBox.setSelectedId (i + 2, juce::dontSendNotification);
                                  scaleBox.setSelectedItemIndex (r["scale"].toString() == "major" ? 0 : 1, juce::dontSendNotification);
                                  proc.keyIndex = keyBox.getSelectedId() - 1;
                                  proc.scaleIndex = scaleBox.getSelectedItemIndex();
                                  proc.status = "Beat is in " + k + " " + r["scale"].toString() + ".";
                                  needTune = true;
                                  requestRender (50);
                              });
                          });
}

juce::File MirrorEditor::lastRawStore() const { return userDir().getParentDirectory().getChildFile ("last_raw_vocal.txt"); }

void MirrorEditor::rememberRaw (const juce::File& f)
{
    if (f.existsAsFile()) lastRawStore().replaceWithText (f.getFullPathName());
}

void MirrorEditor::setReference (const juce::File& f)
{
    proc.refFile = f;
    refLane->setFile (f);
    if (proc.hearMode == 2) rebuildPreview();
    refresh();
}

void MirrorEditor::saveTweaksToPreset()
{
    if (! preset.isObject() || ! juce::isPositiveAndBelow (presetIndex, presetFiles.size())) return;
    auto P = effectivePreset();
    auto* o = P.getDynamicObject();
    if (o == nullptr) return;
    auto* m = new juce::DynamicObject();
    m->setProperty ("comp", proc.knobComp.load());   m->setProperty ("reverb", proc.knobReverb.load());
    m->setProperty ("delay", proc.knobDelay.load()); m->setProperty ("fx", proc.knobFx.load());
    m->setProperty ("deess", proc.knobDeess.load()); m->setProperty ("drive", proc.knobDrive.load());
    m->setProperty ("out", proc.knobOut.load());     m->setProperty ("gate", proc.knobGate.load());
    m->setProperty ("tape", proc.knobTape.load());   m->setProperty ("air", proc.knobAir.load());
    m->setProperty ("tape30", proc.tape30.load());   m->setProperty ("space", proc.verbType);
    o->setProperty ("macros", juce::var (m));
    o->removeProperty ("mic_eq");                    // mic match is applied per session, not baked
    auto file = presetFiles[presetIndex];
    juce::String name = prettyName (file);
    if (! file.isAChildOf (userDir()))
    {
        name = name + " (tweaked)";
        file = userDir().getChildFile (name.replaceCharacter (' ', '_')
                   .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") + ".json");
    }
    o->setProperty ("name", name);
    if (file.replaceWithText (juce::JSON::toString (P)))
    {
        proc.status = "Saved your tweaks into " + name + ".";
        scanPresets (prettyName (file));
    }
    else proc.status = "Couldn't save " + file.getFullPathName();
    refresh();
}

void MirrorEditor::applyStoredTweaks()
{
    const auto m = preset["macros"];
    if (! m.isObject()) return;
    auto set = [] (Knob& k, double v) { k.slider.setValue (v, juce::dontSendNotification); };
    set (comp, mw::num (m, "comp", 1)); set (reverb, mw::num (m, "reverb", 1)); set (delay, mw::num (m, "delay", 1));
    set (fx, mw::num (m, "fx", 1)); set (deess, mw::num (m, "deess", 1)); set (drive, mw::num (m, "drive", 0));
    set (out, mw::num (m, "out", 0)); set (gateK, mw::num (m, "gate", 1)); set (tapeK, mw::num (m, "tape", 0));
    set (airK, mw::num (m, "air", 0));
    syncKnobsToProc();
    proc.tape30 = m.hasProperty ("tape30") && (bool) m["tape30"];
    speedBtn.setToggleState (proc.tape30.load(), juce::dontSendNotification);
    speedBtn.setButtonText (proc.tape30.load() ? "Tape 30 ips" : "Tape 15 ips");
    proc.verbType = juce::jlimit (0, 1, (int) mw::num (m, "space", 0));
    spaceBox.setSelectedId (proc.verbType + 1, juce::dontSendNotification);
}

void MirrorEditor::buildFromReference()
{
    if (busy) return;
    if (! proc.refFile.existsAsFile()) { chooseRef(); return; }
    juce::File raw = proc.rawFile;
    if (! raw.existsAsFile())
        raw = juce::File (lastRawStore().loadFileAsString().trim());
    if (! raw.existsAsFile())
    {
        proc.status = "Load a raw vocal of yours once (any take) - the Emulator fits the chain from your voice to the reference.";
        refresh();
        return;
    }
    const auto name = proc.refFile.getFileNameWithoutExtension().substring (0, 40).trim();
    const auto outFile = userDir().getChildFile (name.replaceCharacter (' ', '_')
                                                     .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") + ".json");
    proc.status = "Building a chain from " + proc.refFile.getFileName() + "...";
    runEngine ({ "fit", "--ref", proc.refFile.getFullPathName(), "--raw", raw.getFullPathName(),
                 "--out", outFile.getFullPathName(), "--name", name },
               [this, outFile] (juce::var r)
               {
                   proc.status = "New preset: " + r["name"].toString() + " (EQ match within "
                                 + juce::String ((double) r["eq_err"], 1) + " dB).";
                   proc.tuneSpeed = -1;
                   scanPresets (prettyName (outFile));
               });
}

//==============================================================================
bool MirrorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3")) return true;
    return false;
}

void MirrorEditor::filesDropped (const juce::StringArray& files, int x, int)
{
    // left side = raw vocal, right side = reference for the Emulator
    const juce::File f (files[0]);
    if (x > getWidth() / 2)
    {
        setReference (f);
        return;
    }
    proc.rawFile = f;
    rememberRaw (f);
    rawLane->setFile (f);
    mixLane->setFile ({});
    needTune = true;
    requestRender (50);
    refresh();
}

void MirrorEditor::refresh()
{
    rawName.setText (proc.rawFile.existsAsFile() ? proc.rawFile.getFileName() : "no raw vocal loaded",
                     juce::dontSendNotification);
    refName.setText (proc.refFile.existsAsFile() ? proc.refFile.getFileName() : "no reference loaded",
                     juce::dontSendNotification);
    status.setText (proc.status, juce::dontSendNotification);
    static const char* hearNames[] = { "Hearing: RAW", "Hearing: MIRRORWALL", "Hearing: REFERENCE" };
    abBtn.setButtonText (hearNames[juce::jlimit (0, 2, proc.hearMode)]);
    abBtn.setColour (juce::TextButton::buttonColourId, proc.hearMode == 1 ? accent.darker (0.55f)
                                                     : proc.hearMode == 2 ? refCol.darker (0.6f) : panel2);
    saveTweaks.setEnabled (preset.isObject());
    buildBtn.setEnabled (! busy);
    micBox.setEnabled (presetIsStock());
    loadRaw.setEnabled (! busy);
    keyFromBeat.setEnabled (! busy);
    play.setEnabled (proc.rawFile.existsAsFile() && ! proc.isLive());
    modeBtn.setButtonText (proc.isLive() ? "LIVE" : "HQ");
    modeBtn.setToggleState (proc.isLive(), juce::dontSendNotification);
    inKnob.setEnabled (! proc.autoInput.load());
    progressBar.setVisible (busy || rendering);
    repaint();
}

void MirrorEditor::timerCallback()
{
    if (proc.isLive()) repaint (meterArea.expanded (2));
    if (proc.isLive())
        liveInfo.setText ("LIVE  in " + juce::String (proc.liveInputDb(), 1) + " dB", juce::dontSendNotification);
    else
        liveInfo.setText ("HQ render", juce::dontSendNotification);
    play.setButtonText (proc.isPreviewing() ? "Stop" : "Play");
    const double len = proc.previewSeconds();
    timeLabel.setText (mmss (proc.previewProgress() * len) + " / " + mmss (len), juce::dontSendNotification);
    rawLane->repaint();
    mixLane->repaint();
    refLane->repaint();

    if (needRender && juce::Time::getMillisecondCounter() >= renderDue)
    {
        needRender = false;
        update();
    }
    if (busy)
    {
        const auto p = proc.sessionDir.getChildFile ("progress.txt").loadFileAsString().trim();
        if (p.contains ("|"))
        {
            progress = juce::jlimit (0.0, 1.0, p.upToFirstOccurrenceOf ("|", false, false).getDoubleValue());
            status.setText (p.fromFirstOccurrenceOf ("|", false, false) + "...", juce::dontSendNotification);
        }
        else progress = -1.0;
    }
    else if (rendering)
        progress = -1.0;
}

//==============================================================================
void MirrorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (34);
    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("MIRRORWALL", top.removeFromLeft (200), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (12.0f);
    g.drawText ("vocal chain emulator", top.removeFromLeft (160), juce::Justification::centredLeft);

    g.setColour (panel);
    auto body = getLocalBounds().reduced (14);
    body.removeFromTop (34 + 10 + 34 + 10);
    g.fillRoundedRectangle (body.removeFromTop (44).toFloat(), 6.0f);     // chain + emulator
    body.removeFromTop (8);
    g.fillRoundedRectangle (body.removeFromTop (96).toFloat(), 6.0f);     // autotune
    body.removeFromTop (8);
    g.fillRoundedRectangle (body.removeFromTop (110).toFloat(), 6.0f);    // tweak knobs

    // LIVE meters: IN (peak after input gain), GR (compression), OUT (peak)
    if (! meterArea.isEmpty())
    {
        auto m = meterArea;
        const bool live = proc.isLive();
        const float vals[3] = { live ? proc.meterIn.load() : -100.0f, live ? -proc.meterGr.load() : 0.0f,
                                live ? proc.meterOut.load() : -100.0f };
        const char* names[3] = { "IN", "GR", "OUT" };
        const int w = m.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto c = m.removeFromLeft (w).reduced (3, 0);
            auto lab = c.removeFromBottom (11);
            g.setColour (panel);
            g.fillRect (c);
            float frac;
            if (i == 1) frac = juce::jlimit (0.0f, 1.0f, -vals[1] / 24.0f);                 // 0..24 dB GR from top
            else frac = juce::jlimit (0.0f, 1.0f, (vals[i] + 48.0f) / 48.0f);               // -48..0 dBFS
            g.setColour (i == 1 ? gold : (vals[i] > -1.0f ? juce::Colours::red : accent));
            if (i == 1) g.fillRect (c.withHeight (juce::roundToInt (c.getHeight() * frac)));
            else g.fillRect (c.withTrimmedTop (juce::roundToInt (c.getHeight() * (1.0f - frac))));
            g.setColour (dim);
            g.setFont (9.0f);
            g.drawText (names[i], lab, juce::Justification::centred);
        }
    }
}

void MirrorEditor::resized()
{
    if (mixLane == nullptr) return;
    auto r = getLocalBounds().reduced (14);
    auto title = r.removeFromTop (34);
    modeBtn.setBounds (title.removeFromRight (80).reduced (0, 3));
    title.removeFromRight (6);
    autoBtn.setBounds (title.removeFromRight (56).reduced (0, 3));
    title.removeFromRight (6);
    lowLatBtn.setBounds (title.removeFromRight (96).reduced (0, 3));
    title.removeFromRight (6);
    speedBtn.setBounds (title.removeFromRight (96).reduced (0, 3));
    title.removeFromRight (8);
    meterArea = title.removeFromRight (84).reduced (0, 2);
    title.removeFromRight (6);
    liveInfo.setBounds (title.removeFromRight (130));
    title.removeFromLeft (370);
    info.setBounds (title);
    r.removeFromTop (10);

    auto src = r.removeFromTop (34);
    loadRaw.setBounds (src.removeFromLeft (140).reduced (0, 2));
    src.removeFromLeft (10);
    micBox.setBounds (src.removeFromRight (300).reduced (0, 4));
    micLabel.setBounds (src.removeFromRight (40));
    rawName.setBounds (src);
    r.removeFromTop (10);

    // chain + emulator row
    auto row = r.removeFromTop (44).reduced (12, 7);
    chainLabel.setBounds (row.removeFromLeft (60));
    presetBox.setBounds (row.removeFromLeft (230));
    row.removeFromLeft (6);
    deletePreset.setBounds (row.removeFromLeft (64));
    row.removeFromLeft (6);
    saveTweaks.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (18);
    emuLabel.setBounds (row.removeFromLeft (80));
    loadRef.setBounds (row.removeFromLeft (130));
    row.removeFromLeft (8);
    buildBtn.setBounds (row.removeFromRight (130));
    row.removeFromRight (8);
    refName.setBounds (row);
    r.removeFromTop (8);

    // autotune row
    auto tr = r.removeFromTop (96).reduced (12, 6);
    tuneLabel.setBounds (tr.removeFromLeft (80).removeFromTop (20));
    auto keys = tr.removeFromLeft (380);
    auto k1 = keys.removeFromTop (40).reduced (0, 6);
    keyLabel.setBounds (k1.removeFromLeft (36));
    keyBox.setBounds (k1.removeFromLeft (90));
    k1.removeFromLeft (12);
    scaleLabel.setBounds (k1.removeFromLeft (44));
    scaleBox.setBounds (k1.removeFromLeft (170));
    keyFromBeat.setBounds (keys.removeFromTop (40).reduced (0, 6).removeFromLeft (140));
    tr.removeFromLeft (20);
    for (auto* k : { &speed, &amount, &humanize })
        k->setBounds (tr.removeFromLeft (92));
    r.removeFromTop (8);

    // tweak row
    auto kr = r.removeFromTop (110).reduced (12, 6);
    auto lab = kr.removeFromLeft (150);
    toneLabel.setBounds (lab.removeFromTop (20));
    preLabel.setBounds (lab.removeFromTop (22));
    preBox.setBounds (lab.removeFromTop (26).withTrimmedRight (10));
    auto sp = lab.removeFromTop (26);
    spaceLabel.setBounds (sp.removeFromLeft (44));
    spaceBox.setBounds (sp.withTrimmedRight (10).reduced (0, 2));
    const int kw = kr.getWidth() / 11;
    for (auto* k : { &inKnob, &drive, &comp, &deess, &gateK, &tapeK, &airK, &fx, &delay, &reverb, &out })
        k->setBounds (kr.removeFromLeft (kw));
    r.removeFromTop (10);

    // transport
    auto bottom = r.removeFromBottom (34);
    r.removeFromBottom (10);
    play.setBounds (bottom.removeFromLeft (84).reduced (0, 2));
    bottom.removeFromLeft (6);
    loopBtn.setBounds (bottom.removeFromLeft (64).reduced (0, 2));
    bottom.removeFromLeft (6);
    abBtn.setBounds (bottom.removeFromLeft (190).reduced (0, 2));
    bottom.removeFromLeft (8);
    timeLabel.setBounds (bottom.removeFromLeft (100));
    openFolder.setBounds (bottom.removeFromRight (110).reduced (0, 2));
    bottom.removeFromRight (10);
    progressBar.setBounds (bottom.removeFromRight (160).reduced (0, 8));
    bottom.removeFromRight (8);
    status.setBounds (bottom);

    // lanes fill the rest
    const int h = (r.getHeight() - 16) / 3;
    rawLane->setBounds (r.removeFromTop (h));
    r.removeFromTop (8);
    mixLane->setBounds (r.removeFromTop (h));
    r.removeFromTop (8);
    refLane->setBounds (r.removeFromTop (h));
}
