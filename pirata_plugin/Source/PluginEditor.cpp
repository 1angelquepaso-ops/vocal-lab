#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff111116), panel (0xff1b1b22), panel2 (0xff24242d),
                       accent (0xffe0a84a), text (0xffe8e6e1), dim (0xff86848c),
                       chordCol (0xffe0a84a), bassCol (0xff6fa8dc), melCol (0xffe07a9a);

    const char* modeIds[]   = { "off", "complement", "counter", "amplify", "random", "arp" };
    const char* modeNames[] = { "Off", "Complement  (works with)", "Counter  (plays against)",
                                "Amplify  (doubles the top line)", "Random  (in key)", "Arp" };
    const char* patIds[]    = { "up", "down", "updown", "converge", "random" };
    const char* patNames[]  = { "Up", "Down", "Up-down", "Converge", "Random" };
    const double rates[]    = { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0 };
    const char* rateNames[] = { "1/8", "1/8 triplet", "1/16", "1/16 triplet" };
    const char* regIds[]    = { "low", "mid", "high" };
    const char* detailIds[] = { "simple", "played", "rich" };

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

    void styleLabel (juce::Label& l, bool heading = false)
    {
        l.setColour (juce::Label::textColourId, heading ? dim : text);
        if (heading)
            l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
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

//==============================================================================
double PianoRoll::lengthBeats() const
{
    const double l = (double) data["length_beats"];
    return l > 0 ? l : 16.0;
}

juce::Rectangle<int> PianoRoll::noteArea() const
{
    auto r = getLocalBounds().reduced (2);
    r.removeFromTop (24);       // chord names
    r.removeFromLeft (34);      // octave labels
    return r;
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto area = noteArea();
    const double len = lengthBeats();
    const auto notes = data["notes"];

    if (! data.isObject())
    {
        g.setColour (dim);
        g.setFont (15.0f);
        g.drawText ("Drop a sample here, or click Load sample", getLocalBounds(),
                    juce::Justification::centred);
        return;
    }

    // pitch range from everything that's shown
    int lo = 127, hi = 0;
    for (auto* k : { "chords", "bass", "melody" })
        if (auto* a = notes[k].getArray())
            for (auto& n : *a)
            {
                lo = juce::jmin (lo, (int) n["pitch"]);
                hi = juce::jmax (hi, (int) n["pitch"]);
            }
    if (lo > hi) { lo = 48; hi = 84; }
    lo -= 2; hi += 2;
    const float rowH = (float) area.getHeight() / (float) (hi - lo + 1);
    auto yOf = [&] (int p) { return (float) area.getBottom() - (float) (p - lo + 1) * rowH; };
    auto xOf = [&] (double b) { return (float) area.getX() + (float) (b / len) * (float) area.getWidth(); };

    // black-key shading + octave labels
    for (int p = lo; p <= hi; ++p)
    {
        const int pc = p % 12;
        if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)
        {
            g.setColour (panel2.withAlpha (0.55f));
            g.fillRect ((float) area.getX(), yOf (p), (float) area.getWidth(), rowH);
        }
        if (pc == 0)
        {
            g.setColour (dim);
            g.setFont (10.0f);
            g.drawText ("C" + juce::String (p / 12 - 1), 2, (int) yOf (p) - 2, 30, (int) rowH + 4,
                        juce::Justification::centredRight);
            g.setColour (panel2.brighter (0.2f));
            g.drawHorizontalLine ((int) (yOf (p) + rowH), (float) area.getX(), (float) area.getRight());
        }
    }
    // bars and beats
    for (int b = 0; b <= (int) len; ++b)
    {
        g.setColour (b % 4 == 0 ? dim.withAlpha (0.6f) : panel2.brighter (0.1f));
        g.drawVerticalLine ((int) xOf (b), (float) area.getY() - 22.0f * (b % 4 == 0), (float) area.getBottom());
        if (b % 4 == 0 && b < (int) len)
        {
            g.setColour (dim);
            g.setFont (10.0f);
            g.drawText (juce::String (b / 4 + 1), (int) xOf (b) + 3, area.getBottom() - 14, 30, 12,
                        juce::Justification::left);
        }
    }
    // chord names
    if (auto* cs = data["chords"].getArray())
        for (auto& c : *cs)
        {
            const float x0 = xOf ((double) c["start"]), x1 = xOf ((double) c["end"]);
            juce::Rectangle<float> r (x0 + 1, 2.0f, x1 - x0 - 2, 20.0f);
            g.setColour (panel2);
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (accent);
            g.setFont (juce::FontOptions (r.getWidth() < 46 ? 11.0f : 13.0f, juce::Font::bold));
            g.drawFittedText (c["name"].toString(), r.toNearestInt().reduced (3, 0),
                              juce::Justification::centredLeft, 1, 0.7f);
        }

    auto drawSet = [&] (const char* k, juce::Colour col, bool on)
    {
        if (auto* a = notes[k].getArray())
            for (auto& n : *a)
            {
                const float x0 = xOf ((double) n["start"]), x1 = xOf ((double) n["end"]);
                juce::Rectangle<float> r (x0, yOf ((int) n["pitch"]) + 0.5f,
                                          juce::jmax (2.0f, x1 - x0 - 1.0f), juce::jmax (2.0f, rowH - 1.0f));
                const float v = 0.45f + 0.55f * (float) (int) n["vel"] / 127.0f;
                g.setColour (col.withAlpha (on ? v : 0.15f));
                g.fillRoundedRectangle (r, 2.0f);
            }
    };
    drawSet ("bass", bassCol, showChords);
    drawSet ("chords", chordCol, showChords);
    drawSet ("melody", melCol, showMelody);

    if (playheadBeats)
    {
        const double b = playheadBeats();
        if (b >= 0)
        {
            g.setColour (text);
            const float x = xOf (b);
            g.drawLine (x, (float) area.getY() - 22, x, (float) area.getBottom(), 1.5f);
        }
    }
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    const auto area = noteArea();
    if (onSeekBeats && e.x >= area.getX())
        onSeekBeats (juce::jlimit (0.0, 1.0, (double) (e.x - area.getX()) / juce::jmax (1, area.getWidth()))
                     * lengthBeats());
}

//==============================================================================
void DragTile::paint (juce::Graphics& g)
{
    const bool ready = file.existsAsFile();
    auto r = getLocalBounds().toFloat().reduced (1);
    g.setColour (hover && ready ? colour.withAlpha (0.25f) : panel2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (ready ? colour : dim.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (10, 0), juce::Justification::centredLeft);
    // grip dots
    auto gr = getLocalBounds().removeFromRight (30).reduced (8, 0);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse ((float) gr.getX() + (float) j * 7.0f, (float) getHeight() / 2.0f - 8.0f + (float) i * 7.0f, 3.5f, 3.5f);
}

void DragTile::mouseEnter (const juce::MouseEvent&)
{
    hover = true;
    setMouseCursor (file.existsAsFile() ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

void DragTile::mouseExit (const juce::MouseEvent&)
{
    hover = false;
    repaint();
}

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! file.existsAsFile())
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray { file.getFullPathName() }, false, this,
        [safe = juce::Component::SafePointer<DragTile> (this)]
        { if (safe != nullptr) safe->dragging = false; });
}

//==============================================================================
PirataEditor::PirataEditor (PirataProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    formats.registerBasicFormats();

    for (auto* l : { &sampleName, &bpmLabel, &info, &timeLabel, &status, &densityLabel,
                     &registerLabel, &patternLabel, &rateLabel, &octLabel, &dragHint, &arpHint })
    {
        styleLabel (*l);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &chordsLabel, &melodyLabel, &arpLabel })
    {
        styleLabel (*l, true);
        addAndMakeVisible (*l);
    }
    info.setColour (juce::Label::textColourId, accent);
    info.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    info.setJustificationType (juce::Justification::centredRight);
    dragHint.setText ("Drag into FL", juce::dontSendNotification);
    arpHint.setText ("Choose Arp in the melody layer to set pattern, rate and octaves.", juce::dontSendNotification);
    arpHint.setColour (juce::Label::textColourId, dim);
    dragHint.setColour (juce::Label::textColourId, dim);
    dragHint.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (loadBtn);
    loadBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.35f));
    loadBtn.onClick = [this] { chooseFile(); };
    addAndMakeVisible (reanalyze);
    reanalyze.onClick = [this] { analyze(); };
    reanalyze.setTooltip ("Analyze again (e.g. after typing the BPM)");
    bpmBox.setTextToShowWhenEmpty ("auto", dim);
    bpmBox.setInputRestrictions (6, "0123456789.");
    bpmBox.setJustification (juce::Justification::centred);
    if (proc.bpmOverride > 0) bpmBox.setText (juce::String (proc.bpmOverride, 1));
    bpmBox.onReturnKey = [this] { analyze(); };
    addAndMakeVisible (bpmBox);

    // chord detail: radio buttons
    int i = 0;
    for (auto* b : { &simpleBtn, &playedBtn, &richBtn })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1001);
        b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.25f));
        b->setToggleState (proc.detail == i, juce::dontSendNotification);
        b->onClick = [this, b, i]
        {
            if (! b->getToggleState() || proc.detail == i) return;
            proc.detail = i;
            buildLayers();
        };
        addAndMakeVisible (*b);
        ++i;
    }
    simpleBtn.setTooltip ("One-note line: the sample's own melody, in its real rhythm (root bass on the BASS tile)");
    playedBtn.setTooltip ("Exactly the notes found in the sample, cleaned and on the grid");
    richBtn.setTooltip ("The sample's melody on top with lush in-key chords (9ths, 11ths, 13ths) under it + root/10th left hand");

    for (int k = 0; k < 6; ++k) modeBox.addItem (modeNames[k], k + 1);
    modeBox.setSelectedItemIndex (proc.mode, juce::dontSendNotification);
    modeBox.onChange = [this] { proc.mode = modeBox.getSelectedItemIndex(); resized(); buildLayers(); };
    addAndMakeVisible (modeBox);

    for (auto* s : { "Low (inside the chords)", "Mid (just above)", "High (sparkle)" })
        registerBox.addItem (s, registerBox.getNumItems() + 1);
    registerBox.setSelectedItemIndex (proc.registerIndex, juce::dontSendNotification);
    registerBox.onChange = [this] { proc.registerIndex = registerBox.getSelectedItemIndex(); buildLayers(); };
    addAndMakeVisible (registerBox);

    density.setRange (0.0, 1.0, 0.01);
    density.setValue (proc.density, juce::dontSendNotification);
    density.setColour (juce::Slider::trackColourId, melCol.withAlpha (0.7f));
    density.setColour (juce::Slider::thumbColourId, melCol);
    density.onDragEnd = [this] { proc.density = density.getValue(); buildLayers(); };
    addAndMakeVisible (density);

    for (int k = 0; k < 5; ++k) patternBox.addItem (patNames[k], k + 1);
    patternBox.setSelectedItemIndex (proc.arpPattern, juce::dontSendNotification);
    patternBox.onChange = [this] { proc.arpPattern = patternBox.getSelectedItemIndex(); buildLayers(); };
    for (int k = 0; k < 4; ++k) rateBox.addItem (rateNames[k], k + 1);
    rateBox.setSelectedItemIndex (proc.arpRate, juce::dontSendNotification);
    rateBox.onChange = [this] { proc.arpRate = rateBox.getSelectedItemIndex(); buildLayers(); };
    for (int k = 1; k <= 3; ++k) octBox.addItem (juce::String (k), k);
    octBox.setSelectedId (proc.arpOctaves, juce::dontSendNotification);
    octBox.onChange = [this] { proc.arpOctaves = octBox.getSelectedId(); buildLayers(); };
    for (auto* c : { &patternBox, &rateBox, &octBox })
        addAndMakeVisible (*c);

    randomize.setColour (juce::TextButton::buttonColourId, melCol.darker (0.5f));
    randomize.setTooltip ("Write a new melody with the same settings");
    randomize.onClick = [this]
    {
        proc.seed = juce::Random::getSystemRandom().nextInt (1 << 30);
        if (proc.mode == 0) { proc.mode = 1; modeBox.setSelectedItemIndex (1, juce::dontSendNotification); resized(); }
        buildLayers();
    };
    addAndMakeVisible (randomize);

    roll.playheadBeats = [this]
    {
        const double bpm = (double) proc.build["bpm"];
        if (bpm <= 0 || proc.previewSeconds() <= 0) return -1.0;
        return proc.previewProgress() * proc.previewSeconds() * bpm / 60.0;
    };
    roll.onSeekBeats = [this] (double b)
    {
        const double bpm = (double) proc.build["bpm"];
        if (bpm <= 0 || proc.previewSeconds() <= 0) return;
        proc.seek ((float) (b * 60.0 / bpm / proc.previewSeconds()));
    };
    addAndMakeVisible (roll);
    for (auto* t : { &chordsTile, &bassTile, &melodyTile, &fullTile })
        addAndMakeVisible (*t);

    addAndMakeVisible (play);
    play.setColour (juce::TextButton::buttonColourId, accent.darker (0.35f));
    play.onClick = [this]
    {
        if (proc.isPreviewing()) { proc.stopPreview(); return; }
        if (proc.previewProgress() >= 0.999f) proc.seek (0.0f);
        proc.startPreviewFromHere();
    };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
    loopBtn.setToggleState (proc.loop.load(), juce::dontSendNotification);
    loopBtn.onClick = [this] { proc.loop = loopBtn.getToggleState(); };
    addAndMakeVisible (loopBtn);

    hearOrig.setToggleState (proc.hearOriginal, juce::dontSendNotification);
    hearChords.setToggleState (proc.hearChords, juce::dontSendNotification);
    hearMelody.setToggleState (proc.hearMelody, juce::dontSendNotification);
    hearOrig.setTooltip ("Hear the original sample (to A/B against the extraction)");
    hearOrig.onClick   = [this] { proc.hearOriginal = hearOrig.getToggleState(); rebuildPreview(); };
    hearChords.onClick = [this] { proc.hearChords = hearChords.getToggleState(); rebuildPreview(); };
    hearMelody.onClick = [this] { proc.hearMelody = hearMelody.getToggleState(); rebuildPreview(); };
    for (auto* t : { &hearOrig, &hearChords, &hearMelody })
    {
        t->setColour (juce::ToggleButton::textColourId, text);
        t->setColour (juce::ToggleButton::tickColourId, accent);
        addAndMakeVisible (*t);
    }
    addAndMakeVisible (openFolder);
    openFolder.onClick = [this] { exportDir().startAsProcess(); };
    timeLabel.setJustificationType (juce::Justification::centred);
    addChildComponent (progressBar);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, accent);

    setResizable (true, true);
    setResizeLimits (900, 600, 2000, 1300);
    setSize (1120, 740);            // sizing last: every child exists before the first layout

    applyBuild();
    refresh();
    startTimerHz (25);

    // developer hook for automated UI checks; unset on normal machines
    const auto dev = juce::SystemStats::getEnvironmentVariable ("PIRATA_DEV_SAMPLE", {});
    if (dev.isNotEmpty() && ! proc.sampleFile.existsAsFile() && juce::File (dev).existsAsFile())
    {
        proc.sampleFile = juce::File (dev);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PirataEditor> (this)]
                                         { if (safe != nullptr) safe->analyze(); });
    }
}

PirataEditor::~PirataEditor()
{
    stopTimer();
    job.reset();
}

//==============================================================================
juce::File PirataEditor::engineExe() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("PIRATA_ENGINE", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);
   #if JUCE_WINDOWS
    const juce::String exeName ("pirata-engine.exe");
   #else
    const juce::String exeName ("pirata-engine");
   #endif
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/engine"),
                      me.getParentDirectory().getChildFile ("engine") })
        if (dir.getChildFile (exeName).existsAsFile())
            return dir.getChildFile (exeName);
    return {};
}

juce::File PirataEditor::exportDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("PIRATA")
                 .getChildFile (proc.sampleFile.getFileNameWithoutExtension());
    d.createDirectory();
    return d;
}

void PirataEditor::runEngine (juce::StringArray args, std::function<void (juce::var)> onOk)
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
    busy = true;
    refresh();
    juce::Component::SafePointer<PirataEditor> safe (this);
    job = std::make_unique<EngineJob> (args, [safe, onOk] (juce::var r)
    {
        if (safe == nullptr)
            return;
        safe->busy = false;
        if (r["ok"])
            onOk (r);
        else
            safe->proc.status = "Problem: " + r["error"].toString();
        safe->refresh();
        if (safe->pendingBuild)
        {
            safe->pendingBuild = false;
            safe->buildLayers();
        }
    });
    job->startThread();
}

void PirataEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Sample to extract (piano, pads, chords)",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              proc.sampleFile = f;
                              bpmBox.clear();
                              analyze();
                          });
}

bool PirataEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg"))
            return true;
    return false;
}

void PirataEditor::filesDropped (const juce::StringArray& files, int, int)
{
    proc.sampleFile = juce::File (files[0]);
    bpmBox.clear();
    analyze();
}

void PirataEditor::analyze()
{
    if (busy || ! proc.sampleFile.existsAsFile()) return;
    proc.stopPreview();
    proc.build = juce::var();
    proc.analysis = juce::var();
    applyBuild();
    proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("PIRATA")
                          .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
    proc.sessionDir.createDirectory();
    proc.bpmOverride = bpmBox.getText().getDoubleValue();
    juce::StringArray args { "analyze", "--session", proc.sessionDir.getFullPathName(),
                             "--sample", proc.sampleFile.getFullPathName() };
    if (proc.bpmOverride >= 40 && proc.bpmOverride <= 220)
        args.addArray ({ "--bpm", juce::String (proc.bpmOverride, 2) });
    proc.status = "Analyzing...";
    runEngine (args, [this] (juce::var r)
    {
        proc.analysis = r;
        if (proc.bpmOverride <= 0)
            bpmBox.setText (juce::String ((double) r["bpm"], 1), juce::dontSendNotification);
        proc.status = "Analyzed in " + juce::String ((double) r["seconds"], 1) + " s.";
        pendingBuild = true;          // build straight away with the current settings
    });
}

void PirataEditor::buildLayers()
{
    if (! proc.analysis.isObject()) return;
    if (busy) { pendingBuild = true; return; }
    juce::StringArray args { "build", "--session", proc.sessionDir.getFullPathName(),
                             "--detail", detailIds[juce::jlimit (0, 2, proc.detail)],
                             "--mode", modeIds[juce::jlimit (0, 5, proc.mode)],
                             "--density", juce::String (proc.density, 2),
                             "--register", regIds[juce::jlimit (0, 2, proc.registerIndex)],
                             "--seed", juce::String (proc.seed),
                             "--arp-pattern", patIds[juce::jlimit (0, 4, proc.arpPattern)],
                             "--arp-rate", juce::String (rates[juce::jlimit (0, 3, proc.arpRate)], 4),
                             "--arp-octaves", juce::String (proc.arpOctaves),
                             "--out", exportDir().getFullPathName() };
    proc.status = "Building...";
    runEngine (args, [this] (juce::var r)
    {
        proc.build = r;
        applyBuild();
        proc.status = "Ready. Drag CHORDS / BASS / MELODY / FULL into FL.";
    });
}

void PirataEditor::applyBuild()
{
    const auto& b = proc.build;
    roll.setData (b);
    const auto files = b["files"];
    chordsTile.setFile (b.isObject() ? juce::File (files["chords"].toString()) : juce::File());
    bassTile.setFile (b.isObject() ? juce::File (files["bass"].toString()) : juce::File());
    melodyTile.setFile (b.isObject() && files["melody"].isString() ? juce::File (files["melody"].toString()) : juce::File());
    fullTile.setFile (b.isObject() ? juce::File (files["all"].toString()) : juce::File());
    const bool was = proc.isPreviewing();
    rebuildPreview();
    if (was) proc.startPreviewFromHere();
}

void PirataEditor::rebuildPreview()
{
    roll.showChords = proc.hearChords;
    roll.showMelody = proc.hearMelody;
    roll.repaint();
    const auto& b = proc.build;
    if (! b.isObject()) { proc.setPreview ({}, 44100.0); return; }

    const double rate = 44100.0;
    juce::AudioBuffer<float> mix;
    auto addFile = [&] (const juce::File& f, float gain, double offsetSec)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) return;
        const double ratio = r->sampleRate / rate;
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> tmp ((int) r->numChannels, n);
        r->read (&tmp, 0, n, 0, true, true);
        // offset > 0: file audio at t corresponds to preview time t - offset
        const int outLen = (int) ((double) n / ratio - offsetSec * rate);
        if (outLen <= 0) return;
        if (mix.getNumSamples() < outLen) mix.setSize (2, outLen, true, true);
        for (int c = 0; c < 2; ++c)
        {
            const float* src = tmp.getReadPointer (juce::jmin (c, tmp.getNumChannels() - 1));
            float* dst = mix.getWritePointer (c);
            for (int i = 0; i < outLen; ++i)
            {
                const double sp = ((double) i / rate + offsetSec) * r->sampleRate;
                const int i0 = (int) sp;
                if (i0 < 0) continue;
                if (i0 + 1 >= n) break;
                const float fr = (float) (sp - i0);
                dst[i] += gain * (src[i0] + fr * (src[i0 + 1] - src[i0]));
            }
        }
    };

    const auto pv = b["preview"];
    if (proc.hearChords)
        addFile (juce::File (pv["chords"].toString()), 0.9f, 0.0);
    if (proc.hearMelody && pv["melody"].isString())
        addFile (juce::File (pv["melody"].toString()), 0.85f, 0.0);
    if (proc.hearOriginal)
        addFile (proc.sampleFile, 0.8f, (double) b["orig_offset"]);

    // keep the loop exactly the piano length so bars line up
    const double bars = (double) b["length_beats"] * 60.0 / juce::jmax (1.0, (double) b["bpm"]);
    const int len = (int) (bars * rate);
    if (len > 0) mix.setSize (2, len, true, true, true);
    const auto peak = mix.getMagnitude (0, mix.getNumSamples());
    if (peak > 0.95f) mix.applyGain (0.95f / peak);
    proc.setPreview (std::move (mix), rate);
}

void PirataEditor::refresh()
{
    sampleName.setText (proc.sampleFile.existsAsFile() ? proc.sampleFile.getFileName() : "no sample loaded",
                        juce::dontSendNotification);
    const auto& a = proc.analysis;
    if (a.isObject())
        info.setText (a["key"].toString() + "   " + juce::String ((double) a["bpm"], 1) + " BPM   "
                          + juce::String ((double) a["bars"], 0) + " bars",
                      juce::dontSendNotification);
    else
        info.setText ({}, juce::dontSendNotification);
    status.setText (proc.status, juce::dontSendNotification);

    const bool ready = a.isObject();
    for (auto* c : { (juce::Component*) &simpleBtn, (juce::Component*) &playedBtn, (juce::Component*) &richBtn,
                     (juce::Component*) &modeBox, (juce::Component*) &randomize, (juce::Component*) &density,
                     (juce::Component*) &registerBox, (juce::Component*) &patternBox,
                     (juce::Component*) &rateBox, (juce::Component*) &octBox })
        c->setEnabled (ready);
    loadBtn.setEnabled (! busy);
    reanalyze.setEnabled (! busy && proc.sampleFile.existsAsFile());
    play.setEnabled (proc.build.isObject());
    openFolder.setEnabled (proc.sampleFile.existsAsFile());
    progressBar.setVisible (busy);
    repaint();
}

void PirataEditor::timerCallback()
{
    play.setButtonText (proc.isPreviewing() ? "Stop" : "Play");
    const double len = proc.previewSeconds();
    timeLabel.setText (mmss (proc.previewProgress() * len) + " / " + mmss (len), juce::dontSendNotification);
    roll.repaint();
    if (busy)
    {
        const auto p = proc.sessionDir.getChildFile ("progress.txt").loadFileAsString().trim();
        if (p.contains ("|"))
        {
            progress = juce::jlimit (0.0, 1.0, p.upToFirstOccurrenceOf ("|", false, false).getDoubleValue());
            status.setText (p.fromFirstOccurrenceOf ("|", false, false) + "...", juce::dontSendNotification);
        }
        else
            progress = -1.0;
    }
}

//==============================================================================
void PirataEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (34);
    g.setColour (accent);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("PIRATA", top.removeFromLeft (130), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (12.0f);
    g.drawText ("chord + MIDI extractor  /  melody builder", top.removeFromLeft (280),
                juce::Justification::centredLeft);

    // control panels
    g.setColour (panel);
    auto body = getLocalBounds().reduced (14);
    body.removeFromTop (34 + 10 + 34 + 10);
    g.fillRoundedRectangle (body.removeFromTop (132).toFloat(), 6.0f);
}

void PirataEditor::resized()
{
    if (bpmBox.getParentComponent() == nullptr)
        return;
    auto r = getLocalBounds().reduced (14);
    auto title = r.removeFromTop (34);
    info.setBounds (title.removeFromRight (360));
    r.removeFromTop (10);

    // sample row
    auto src = r.removeFromTop (34);
    loadBtn.setBounds (src.removeFromLeft (120).reduced (0, 2));
    src.removeFromLeft (10);
    reanalyze.setBounds (src.removeFromRight (100).reduced (0, 2));
    src.removeFromRight (8);
    bpmBox.setBounds (src.removeFromRight (70).reduced (0, 4));
    bpmLabel.setBounds (src.removeFromRight (40));
    sampleName.setBounds (src);
    r.removeFromTop (10);

    // controls panel: chords row + melody row
    auto ctl = r.removeFromTop (132).reduced (12, 8);
    auto row1 = ctl.removeFromTop (34);
    chordsLabel.setBounds (row1.removeFromLeft (110));
    for (auto* b : { &simpleBtn, &playedBtn, &richBtn })
    {
        b->setBounds (row1.removeFromLeft (110).reduced (2, 3));
    }
    ctl.removeFromTop (6);
    auto row2 = ctl.removeFromTop (34);
    melodyLabel.setBounds (row2.removeFromLeft (110));
    modeBox.setBounds (row2.removeFromLeft (240).reduced (2, 4));
    row2.removeFromLeft (10);
    randomize.setBounds (row2.removeFromRight (120).reduced (2, 3));
    row2.removeFromRight (10);
    const bool arp = proc.mode == 5;
    for (auto* c : { (juce::Component*) &patternLabel, (juce::Component*) &patternBox,
                     (juce::Component*) &rateLabel, (juce::Component*) &rateBox,
                     (juce::Component*) &octLabel, (juce::Component*) &octBox })
        c->setVisible (arp);
    for (auto* c : { (juce::Component*) &registerLabel, (juce::Component*) &registerBox })
        c->setVisible (true);
    densityLabel.setBounds (row2.removeFromLeft (56));
    density.setBounds (row2.removeFromLeft (140));
    row2.removeFromLeft (14);
    registerLabel.setBounds (row2.removeFromLeft (62));
    registerBox.setBounds (row2.removeFromLeft (190).reduced (2, 4));

    ctl.removeFromTop (6);
    auto row3 = ctl.removeFromTop (34);
    arpLabel.setBounds (row3.removeFromLeft (110));
    arpHint.setVisible (! arp);
    arpHint.setBounds (row3);
    patternLabel.setBounds (row3.removeFromLeft (56));
    patternBox.setBounds (row3.removeFromLeft (130).reduced (2, 4));
    row3.removeFromLeft (14);
    rateLabel.setBounds (row3.removeFromLeft (40));
    rateBox.setBounds (row3.removeFromLeft (140).reduced (2, 4));
    row3.removeFromLeft (14);
    octLabel.setBounds (row3.removeFromLeft (60));
    octBox.setBounds (row3.removeFromLeft (70).reduced (2, 4));
    r.removeFromTop (10);

    // transport at the bottom
    auto bottom = r.removeFromBottom (34);
    r.removeFromBottom (10);
    play.setBounds (bottom.removeFromLeft (84).reduced (0, 2));
    bottom.removeFromLeft (6);
    loopBtn.setBounds (bottom.removeFromLeft (64).reduced (0, 2));
    bottom.removeFromLeft (8);
    timeLabel.setBounds (bottom.removeFromLeft (100));
    bottom.removeFromLeft (8);
    hearOrig.setBounds (bottom.removeFromLeft (92));
    hearChords.setBounds (bottom.removeFromLeft (86));
    hearMelody.setBounds (bottom.removeFromLeft (90));
    openFolder.setBounds (bottom.removeFromRight (110).reduced (0, 2));
    bottom.removeFromRight (10);
    progressBar.setBounds (bottom.removeFromRight (170).reduced (0, 8));
    bottom.removeFromRight (8);
    status.setBounds (bottom);

    // roll + drag tiles column
    auto tiles = r.removeFromRight (150);
    r.removeFromRight (10);
    roll.setBounds (r);
    dragHint.setBounds (tiles.removeFromTop (24));
    for (auto* t : { &chordsTile, &bassTile, &melodyTile, &fullTile })
    {
        t->setBounds (tiles.removeFromTop (48));
        tiles.removeFromTop (8);
    }
}
