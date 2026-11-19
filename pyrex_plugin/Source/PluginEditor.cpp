#include "PluginEditor.h"

namespace
{
    // PYREX = glass + heat: deep violet glass, magenta-orange flame accent
    const juce::Colour bg (0xff0f0c16), panel (0xff1a1524), panel2 (0xff261f33), accent (0xffff7a3d),
                       glass (0xffb07cff), text (0xffece8f2), dim (0xff8c8597),
                       green (0xff5fd38d), amber (0xffffb547), red (0xffff5a5a);

    const char* noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

    juce::Colour levelColour (const juce::String& l)
    {
        return l == "green" ? green : l == "amber" ? amber : l == "red" ? red : dim;
    }

    juce::var makeError (const juce::String& m)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", m);
        return juce::var (o);
    }

    juce::String mmss (double s)
    {
        const bool neg = s < 0;
        s = std::abs (s);
        const int m = (int) (s / 60);
        return (neg ? "-" : "") + juce::String (m) + ":" + juce::String (s - m * 60, 2).paddedLeft ('0', 5);
    }

    juce::String fmtBpm (double b)
    {
        return std::abs (b - std::round (b)) < 0.005 ? juce::String ((int) std::round (b)) : juce::String (b, 2);
    }

    int rootOf (const juce::String& key)
    {
        const auto r = key.endsWith ("m") ? key.dropLastCharacters (1) : key;
        for (int i = 0; i < 12; ++i)
            if (r == noteNames[i]) return i;
        return -1;
    }

    void setupLabel (juce::Label& l, bool head = false, float size = 14.0f)
    {
        l.setColour (juce::Label::textColourId, head ? dim : text);
        l.setFont (juce::FontOptions (head ? 11.5f : size, head ? juce::Font::bold : juce::Font::plain));
    }

    void styleButton (juce::TextButton& b, bool toggle = false)
    {
        b.setColour (juce::TextButton::buttonColourId, panel2);
        b.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.25f));
        b.setColour (juce::TextButton::textColourOffId, text);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        if (toggle) b.setClickingTogglesState (true);
    }

    void styleBox (juce::TextEditor& e)
    {
        e.setColour (juce::TextEditor::backgroundColourId, panel2);
        e.setColour (juce::TextEditor::textColourId, text);
        e.setColour (juce::TextEditor::outlineColourId, panel2.brighter (0.2f));
        e.setJustification (juce::Justification::centred);
        e.setInputRestrictions (7, "0123456789.");
    }

    void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, panel2);
        c.setColour (juce::ComboBox::textColourId, text);
        c.setColour (juce::ComboBox::outlineColourId, panel2.brighter (0.2f));
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
void DragTile::paint (juce::Graphics& g)
{
    const bool ready = file.existsAsFile();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (ready ? colour.withAlpha (hover ? 0.30f : 0.16f) : panel2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (ready ? colour : dim.withAlpha (0.4f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (ready ? text : dim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (10, 0).withTrimmedRight (26), juce::Justification::centredLeft, true);
    g.setColour (ready ? colour : dim.withAlpha (0.4f));
    auto gr = getLocalBounds().removeFromRight (26).reduced (6, 0);
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

void DragTile::mouseExit (const juce::MouseEvent&) { hover = false; repaint(); }

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! file.existsAsFile()) return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray { file.getFullPathName() }, false, this,
        [safe = juce::Component::SafePointer<DragTile> (this)] { if (safe != nullptr) safe->dragging = false; });
}

//==============================================================================
void Badge::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto c = levelColour (lvl);
    g.setColour (c.withAlpha (lvl.isEmpty() ? 0.05f : 0.14f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (c.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.2f);
    auto dot = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.3f);
    g.setColour (c);
    g.fillEllipse (dot);
    auto t = getLocalBounds().withTrimmedLeft (getHeight()).reduced (4, 4);
    g.setColour (text);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (h, t.removeFromTop (t.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.5f));
    g.drawText (s, t, juce::Justification::topLeft, true);
}

//==============================================================================
PyrexEditor::PyrexEditor (PyrexProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    for (auto* b : { &loadBtn, &half, &dbl, &setBpm, &nudgeL, &nudgeR, &openBtn })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    styleButton (lockBtn, true);
    styleButton (keyBtn, true);
    addAndMakeVisible (lockBtn);
    addAndMakeVisible (keyBtn);
    styleButton (renderBtn);
    renderBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.2f));
    addAndMakeVisible (renderBtn);

    for (auto* e : { &songBpmBox, &targetBox })
    {
        styleBox (*e);
        addAndMakeVisible (*e);
    }
    songBpmBox.setTextToShowWhenEmpty ("song BPM", dim);
    targetBox.setTextToShowWhenEmpty ("= FL", dim);

    styleCombo (keyBox);
    styleCombo (toKeyBox);
    for (int m = 0; m < 2; ++m)
        for (int i = 0; i < 12; ++i)
            keyBox.addItem (juce::String (noteNames[i]) + (m ? "m" : ""), 1 + m * 12 + i);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (toKeyBox);

    setupLabel (title, false, 22.0f);
    title.setText ("PYREX MAGIK", juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, accent);
    title.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    for (auto* l : { &hSong, &hLock, &hOut })
        setupLabel (*l, true);
    hSong.setText ("SONG", juce::dontSendNotification);
    hLock.setText ("LOCK", juce::dontSendNotification);
    hOut.setText ("LOCKED STEMS  -  drag each one onto the FL playlist at bar 1", juce::dontSendNotification);
    for (auto* l : { &stemsLabel, &bpmLabel, &keyLabel, &bar1Label, &hostLabel, &shiftLabel, &statusLabel, &notesLabel, &flagsLabel })
        setupLabel (*l);
    stemsLabel.setColour (juce::Label::textColourId, dim);
    notesLabel.setColour (juce::Label::textColourId, amber);
    notesLabel.setJustificationType (juce::Justification::topLeft);
    flagsLabel.setColour (juce::Label::textColourId, dim);
    flagsLabel.setJustificationType (juce::Justification::topLeft);
    statusLabel.setColour (juce::Label::textColourId, dim);
    for (auto* l : { &title, &hSong, &hLock, &hOut, &stemsLabel, &bpmLabel, &keyLabel, &bar1Label, &hostLabel, &shiftLabel,
                     &statusLabel, &notesLabel, &flagsLabel })
        addAndMakeVisible (*l);
    addAndMakeVisible (clockBadge);
    addAndMakeVisible (renderBadge);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, accent);
    progressBar.setColour (juce::ProgressBar::backgroundColourId, panel2);
    addAndMakeVisible (progressBar);

    lockBtn.setToggleState (proc.bpmLock, juce::dontSendNotification);
    keyBtn.setToggleState (proc.keyTranspose, juce::dontSendNotification);
    if (proc.targetBpm > 0) targetBox.setText (fmtBpm (proc.targetBpm), false);

    loadBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("One song's stems (select all of them)", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  juce::StringArray f;
                                  for (auto& r : fc.getResults()) f.add (r.getFullPathName());
                                  if (! f.isEmpty()) setStems (f);
                              });
    };
    auto rebpm = [this] (double factor)
    {
        if (! proc.analysis.isObject()) return;
        const double cur = proc.bpmOverride > 0 ? proc.bpmOverride : (double) proc.analysis["clock"]["bpm"];
        proc.bpmOverride = cur * factor;
        analyze();
    };
    half.onClick = [rebpm] { rebpm (0.5); };
    dbl.onClick = [rebpm] { rebpm (2.0); };
    setBpm.onClick = [this]
    {
        const double v = songBpmBox.getText().getDoubleValue();
        if (v >= 40 && v <= 300) { proc.bpmOverride = v; analyze(); }
    };
    nudgeL.onClick = [this] { proc.nudge -= 1; settingsChanged(); };
    nudgeR.onClick = [this] { proc.nudge += 1; settingsChanged(); };
    lockBtn.onClick = [this] { proc.bpmLock = lockBtn.getToggleState(); settingsChanged(); };
    keyBtn.onClick = [this] { proc.keyTranspose = keyBtn.getToggleState(); settingsChanged(); };
    targetBox.onReturnKey = [this] { proc.targetBpm = targetBox.getText().getDoubleValue(); settingsChanged(); };
    targetBox.onFocusLost = [this] { proc.targetBpm = targetBox.getText().getDoubleValue(); settingsChanged(); };
    keyBox.onChange = [this]
    {
        proc.keyName = keyBox.getText();
        fillToKeys();
        settingsChanged();
    };
    toKeyBox.onChange = [this]
    {
        proc.transpose = shiftFor (originalKey(), toKeyBox.getText());
        settingsChanged();
    };
    renderBtn.onClick = [this] { renderNow(); };
    openBtn.onClick = [this]
    {
        auto d = outputDir();
        if (d.isDirectory()) d.startAsProcess();
    };

    setResizable (false, false);
    setSize (860, 640);                // last (playbook: children must exist first)
    refresh();
    startTimerHz (8);
    // dev loop only (Linux screenshots / CI): PYREX_TEST_STEMS = folder -> load it on open
    const auto dev = juce::SystemStats::getEnvironmentVariable ("PYREX_TEST_STEMS", {});
    if (dev.isNotEmpty() && juce::File (dev).isDirectory())
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PyrexEditor> (this), dev]
                                         { if (safe != nullptr) safe->filesDropped ({ dev }, 0, 0); });
}

PyrexEditor::~PyrexEditor()
{
    stopTimer();
    job.reset();
}

//==============================================================================
juce::File PyrexEditor::engineExe() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("PYREX_ENGINE", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile()) return juce::File (env);
   #if JUCE_WINDOWS
    const juce::String exeName ("pyrex-engine.exe");
   #else
    const juce::String exeName ("pyrex-engine");
   #endif
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/engine"),
                      me.getParentDirectory().getChildFile ("engine") })
        if (dir.getChildFile (exeName).existsAsFile()) return dir.getChildFile (exeName);
    return {};
}

double PyrexEditor::targetBpmNow() const
{
    if (proc.targetBpm >= 40 && proc.targetBpm <= 300) return proc.targetBpm;
    const double h = proc.hostBpm.load();
    return h > 0 ? h : 120.0;
}

juce::String PyrexEditor::originalKey() const
{
    if (proc.keyName.isNotEmpty()) return proc.keyName;
    return proc.analysis.isObject() ? proc.analysis["key"]["key"].toString() : juce::String();
}

int PyrexEditor::shiftFor (const juce::String& from, const juce::String& to) const
{
    const int a = rootOf (from), b = rootOf (to);
    if (a < 0 || b < 0) return 0;
    int d = ((b - a) % 12 + 12) % 12;
    return d > 6 ? d - 12 : d;                   // shortest way: -5 ... +6
}

juce::File PyrexEditor::outputDir() const
{
    if (! proc.analysis.isObject()) return {};
    const auto key = originalKey();
    const auto keyTo = proc.keyTranspose && proc.transpose != 0 ? toKeyBox.getText() : juce::String();
    const double bpm = proc.bpmLock ? targetBpmNow() : (double) proc.analysis["clock"]["bpm"];
    auto name = proc.analysis["title"].toString() + " - " + fmtBpm (bpm) + "bpm - " + key + (keyTo.isNotEmpty() ? " to " + keyTo : "");
    name = juce::File::createLegalFileName (name);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("PYREX MAGIK").getChildFile (name);
}

void PyrexEditor::runEngine (juce::StringArray args, std::function<void (juce::var)> onOk)
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
    progress = 0.0;
    refresh();
    juce::Component::SafePointer<PyrexEditor> safe (this);
    job = std::make_unique<EngineJob> (args, [safe, onOk] (juce::var r)
    {
        if (safe == nullptr) return;
        safe->busy = false;
        safe->progress = 0.0;
        if (r["ok"]) onOk (r);
        else safe->proc.status = "Problem: " + r["error"].toString();
        safe->refresh();
    });
    job->startThread();
}

//==============================================================================
bool PyrexEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav") || juce::File (f).isDirectory()) return true;
    return false;
}

void PyrexEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    juce::StringArray wavs;
    for (auto& f : files)
    {
        juce::File x (f);
        if (x.isDirectory())                    // a whole unzipped Suno folder
            for (auto& c : x.findChildFiles (juce::File::findFiles, false, "*.wav"))
                wavs.add (c.getFullPathName());
        else if (x.hasFileExtension ("wav"))
            wavs.add (f);
    }
    if (! wavs.isEmpty()) setStems (wavs);
}

void PyrexEditor::setStems (const juce::StringArray& files)
{
    if (busy) return;
    juce::StringArray f;
    for (auto& x : files)
        if (! juce::File (x).getFileName().containsIgnoreCase ("part_")) f.add (x);
    f.sort (true);
    if (f.size() > 12)
    {
        proc.status = "That's " + juce::String (f.size()) + " files: drop ONE song's stems (12 max).";
        refresh();
        return;
    }
    proc.stems = f;
    proc.bpmOverride = 0;
    proc.nudge = 0;
    proc.keyName = {};
    proc.transpose = 0;
    proc.render = juce::var();
    analyze();
}

void PyrexEditor::analyze()
{
    if (busy || proc.stems.isEmpty()) return;
    proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("PYREX MAGIK")
                          .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
    proc.sessionDir.createDirectory();
    proc.render = juce::var();
    juce::StringArray args { "analyze", "--out", proc.sessionDir.getChildFile ("analysis.json").getFullPathName(), "--stems" };
    args.addArray (proc.stems);
    if (proc.bpmOverride >= 40 && proc.bpmOverride <= 300)
        args.addArray ({ "--bpm", juce::String (proc.bpmOverride, 3) });
    proc.status = "Reading the clock and the key...";
    runEngine (args, [this] (juce::var)
    {
        proc.analysis = juce::JSON::parse (proc.sessionDir.getChildFile ("analysis.json"));
        if (! proc.analysis.isObject() || ! proc.analysis["ok"])
        {
            proc.status = "Problem: " + (proc.analysis.isObject() ? proc.analysis["error"].toString() : juce::String ("no analysis"));
            proc.analysis = juce::var();
            return;
        }
        proc.keyName = proc.analysis["key"]["key"].toString();
        auto* q = proc.analysis["questions"].getArray();
        proc.status = (q != nullptr && ! q->isEmpty()) ? "Check the questions below, then RENDER." : "Ready: press RENDER.";
    });
}

void PyrexEditor::settingsChanged()
{
    if (proc.render.isObject())
    {
        proc.render = juce::var();
        proc.status = "Settings changed: RENDER again.";
    }
    refresh();
}

void PyrexEditor::renderNow()
{
    if (busy || ! proc.analysis.isObject()) return;
    const auto dir = outputDir();
    dir.createDirectory();
    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))   // one canonical set per folder
        f.deleteFile();
    const double rate = proc.hostRate.load() > 0 ? proc.hostRate.load() : 44100.0;
    juce::StringArray args { "render", "--analysis", proc.sessionDir.getChildFile ("analysis.json").getFullPathName(),
                             "--bpm", juce::String (targetBpmNow(), 4), "--sr", juce::String ((int) std::round (rate)),
                             "--out-dir", dir.getFullPathName(),
                             "--out", proc.sessionDir.getChildFile ("render.json").getFullPathName(),
                             "--key-name", originalKey() };
    if (! proc.bpmLock) args.add ("--no-lock");
    if (proc.keyTranspose && proc.transpose != 0) args.addArray ({ "--transpose", juce::String (proc.transpose) });
    if (proc.nudge != 0) args.addArray ({ "--nudge", juce::String (proc.nudge) });
    proc.status = "Rendering " + juce::String (proc.stems.size()) + " stems...";
    runEngine (args, [this] (juce::var)
    {
        proc.render = juce::JSON::parse (proc.sessionDir.getChildFile ("render.json"));
        if (! proc.render.isObject() || ! proc.render["ok"])
        {
            proc.status = "Problem: " + (proc.render.isObject() ? proc.render["error"].toString() : juce::String ("no render"));
            proc.render = juce::var();
            return;
        }
        const auto lvl = proc.render["level"].toString();
        proc.status = lvl == "red" ? "Render failed a check: drag is off. See below."
                    : lvl == "amber" ? "Done, with flagged sections: check them in FL."
                                     : "Locked. Drag the stems onto the playlist at bar 1.";
    });
}

void PyrexEditor::fillToKeys()
{
    const auto from = originalKey();
    const bool minor = from.endsWith ("m");
    const auto keep = toKeyBox.getText();
    toKeyBox.clear (juce::dontSendNotification);
    for (int i = 0; i < 12; ++i)
        toKeyBox.addItem (juce::String (noteNames[i]) + (minor ? "m" : ""), i + 1);
    int idx = rootOf (from);
    if (keep.isNotEmpty() && keep.endsWith ("m") == minor && rootOf (keep) >= 0 && proc.transpose != 0)
        idx = rootOf (keep);
    else if (idx >= 0)
        idx = (idx + proc.transpose + 12) % 12;
    if (idx >= 0) toKeyBox.setSelectedId (idx + 1, juce::dontSendNotification);
}

//==============================================================================
void PyrexEditor::refresh()
{
    const auto& a = proc.analysis;
    const bool has = a.isObject();
    statusLabel.setText (proc.status, juce::dontSendNotification);
    stemsLabel.setText (proc.stems.isEmpty() ? juce::String ("Drop a Suno stems folder or its WAVs anywhere on this window.")
                                             : juce::String (proc.stems.size()) + " stems: " + [&]
                                               {
                                                   juce::StringArray n;
                                                   for (auto& s : proc.stems) n.add (juce::File (s).getFileNameWithoutExtension());
                                                   return n.joinIntoString (", ");
                                               }(),
                        juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &half, &dbl, &setBpm, &songBpmBox, &nudgeL, &nudgeR, &keyBox,
                                                             &renderBtn })
        c->setEnabled (has && ! busy);
    loadBtn.setEnabled (! busy);
    toKeyBox.setEnabled (has && ! busy && proc.keyTranspose);
    hostLabel.setText ("FL tempo: " + (proc.hostBpm.load() > 0 ? fmtBpm (proc.hostBpm.load()) : juce::String ("-"))
                           + "    rate: " + (proc.hostRate.load() > 0 ? juce::String ((int) proc.hostRate.load()) : juce::String ("-")),
                       juce::dontSendNotification);

    juce::StringArray notes;
    if (has)
    {
        const auto& c = a["clock"];
        const double bpm = c["bpm"], meas = c["measured_bpm"];
        juce::String line = "Song BPM  " + fmtBpm (bpm) + "   (Suno label " + (c["label_bpm"].isVoid() ? juce::String ("none")
                            : fmtBpm ((double) c["label_bpm"])) + ")";
        if (std::abs (meas - bpm) > 0.01) line << "   drums read " << fmtBpm (meas);
        bpmLabel.setText (line, juce::dontSendNotification);
        if (! songBpmBox.hasKeyboardFocus (true)) songBpmBox.setText (fmtBpm (bpm), false);

        const auto key = originalKey();
        for (int i = 1; i <= 24; ++i)
            if (keyBox.getItemText (i - 1) == key) keyBox.setSelectedId (i, juce::dontSendNotification);
        const auto& k = a["key"];
        keyLabel.setText ("Key   (also possible: " + k["alt"].toString() + ")", juce::dontSendNotification);

        const double beat = 60.0 / bpm;
        bar1Label.setText ("Bar 1 of the music at " + mmss ((double) c["music_bar1_s"] + proc.nudge * beat) + "   ("
                               + c["downbeat_style"].toString() + ")" + (proc.nudge != 0 ? "   nudged " + juce::String (proc.nudge) + " beat(s)" : ""),
                           juce::dontSendNotification);
        const auto& cf = c["conf"];
        const auto lvl = c["level"].toString();
        clockBadge.set (lvl, lvl == "green" ? "Clock: locked tight" : lvl == "amber" ? "Clock: some sections drift" : "Clock: not reliable",
                        "source " + c["source"].toString() + "  |  drift " + juce::String ((double) cf["drift_ms"], 1) + " ms  |  feel "
                            + juce::String ((double) cf["feel_ms"], 1) + " ms (kept)  |  " + c["mode"].toString() + " lock");
        if (auto* q = a["questions"].getArray()) for (auto& x : *q) notes.add ("? " + x.toString());
        if (auto* q = a["notes"].getArray())     for (auto& x : *q) notes.add ("- " + x.toString());
        if (toKeyBox.getNumItems() == 0) fillToKeys();
    }
    else
    {
        bpmLabel.setText ("Song BPM  -", juce::dontSendNotification);
        keyLabel.setText ("Key", juce::dontSendNotification);
        bar1Label.setText ("Bar 1  -", juce::dontSendNotification);
        clockBadge.set ({}, "No song loaded", "Drums are the clock, harmony is the key.");
    }
    if (proc.keyTranspose && std::abs (proc.transpose) > 3)
        notes.add ("- Transposing " + juce::String (std::abs (proc.transpose)) + " semitones: big shifts can sound processed.");
    if (has && proc.bpmLock)
    {
        const double ratio = (double) a["clock"]["bpm"] / targetBpmNow();
        if (std::abs (ratio - 1.0) > 0.15)
            notes.add ("- Stretch is " + juce::String (std::abs (ratio - 1.0) * 100.0, 0) + "%: past 15% any stretcher starts to sound processed.");
    }
    notesLabel.setText (notes.joinIntoString ("\n"), juce::dontSendNotification);
    shiftLabel.setText (proc.keyTranspose ? (proc.transpose == 0 ? juce::String ("no shift") : (proc.transpose > 0 ? "+" : "")
                                                 + juce::String (proc.transpose) + " semitones")
                                          : juce::String ("pitch untouched"),
                        juce::dontSendNotification);

    // result
    const auto& r = proc.render;
    const bool done = r.isObject() && (bool) r["ok"];
    const auto lvl = done ? r["level"].toString() : juce::String();
    const bool drag = done && (bool) r["drag_allowed"];
    tiles.clear();
    juce::StringArray flags;
    if (done)
    {
        const auto& g = r["check"]["checks"]["grid"];
        renderBadge.set (lvl, lvl == "green" ? "Locked to " + fmtBpm ((double) r["plan"]["target_bpm"]) + " BPM"
                              : lvl == "amber" ? "Locked, with flagged sections" : "Not locked: drag off",
                         "offset " + juce::String ((double) g["offset_ms"], 2) + " ms | median " + juce::String ((double) g["median_ms"], 1)
                             + " ms | stems " + juce::String ((double) r["check"]["checks"]["align"]["spread_ms"], 1) + " ms");
        if (auto* fo = r["files"].getDynamicObject())
            for (auto& pr : fo->getProperties())
            {
                auto* t = tiles.add (new DragTile (pr.name.toString(), levelColour (lvl)));
                if (drag) t->setFile (juce::File (pr.value.toString()));
                addAndMakeVisible (t);
            }
        const double bar = 4 * 60.0 / (double) r["plan"]["target_bpm"];
        if (auto* fl = g["flagged"].getArray())
            for (auto& f : *fl)
                flags.add ("Check bars " + juce::String ((int) ((double) f["start_s"] / bar) + 1) + "-"
                           + juce::String ((int) ((double) f["end_s"] / bar) + 1) + "  (" + mmss ((double) f["start_s"]) + " - "
                           + mmss ((double) f["end_s"]) + ", drift " + juce::String ((double) f["drift_ms"], 1) + " ms)");
        if (auto* pr = r["problems"].getArray())
            for (auto& p : *pr) flags.add ("Failed check: " + p.toString());
    }
    else
        renderBadge.set ({}, "Not rendered yet", "Every stem gets the same clock, trim and length.");
    flagsLabel.setText (flags.joinIntoString ("\n"), juce::dontSendNotification);
    openBtn.setEnabled (done);
    resized();
    repaint();
}

void PyrexEditor::timerCallback()
{
    static double lastHost = -1;
    if (busy && proc.sessionDir.isDirectory())
    {
        const auto t = proc.sessionDir.getChildFile ("progress.txt").loadFileAsString();
        if (t.contains ("|"))
        {
            progress = t.upToFirstOccurrenceOf ("|", false, false).getDoubleValue();
            progressText = t.fromFirstOccurrenceOf ("|", false, false);
            statusLabel.setText (progressText, juce::dontSendNotification);
        }
    }
    const double h = proc.hostBpm.load();
    if (h != lastHost)
    {
        lastHost = h;
        if (! busy) refresh();
    }
}

//==============================================================================
void PyrexEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto b = getLocalBounds().reduced (12);
    b.removeFromTop (44);
    auto top = b.removeFromTop (300);
    g.setColour (panel);
    g.fillRoundedRectangle (top.removeFromLeft (520).toFloat(), 8.0f);
    top.removeFromLeft (10);
    g.fillRoundedRectangle (top.toFloat(), 8.0f);
    b.removeFromTop (10);
    g.fillRoundedRectangle (b.toFloat(), 8.0f);
    if (dragOver)
    {
        g.setColour (glass.withAlpha (0.18f));
        g.fillAll();
        g.setColour (glass);
        g.drawRect (getLocalBounds().reduced (4), 2);
    }
    g.setColour (glass.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("Suno stems  ->  one clock, one key  ->  locked to FL", getLocalBounds().reduced (14, 12).removeFromTop (30),
                juce::Justification::centredRight);
}

void PyrexEditor::resized()
{
    if (title.getParentComponent() == nullptr) return;       // playbook: null-guard before children exist
    auto b = getLocalBounds().reduced (12);
    auto head = b.removeFromTop (44);
    title.setBounds (head.removeFromLeft (260).withTrimmedBottom (10));

    auto top = b.removeFromTop (300);
    auto L = top.removeFromLeft (520).reduced (12, 10);
    top.removeFromLeft (10);
    auto R = top.reduced (12, 10);

    hSong.setBounds (L.removeFromTop (16));
    auto row = L.removeFromTop (28);
    loadBtn.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    stemsLabel.setBounds (row);
    L.removeFromTop (6);
    row = L.removeFromTop (26);
    bpmLabel.setBounds (row.removeFromLeft (290));
    half.setBounds (row.removeFromLeft (36));
    row.removeFromLeft (4);
    dbl.setBounds (row.removeFromLeft (36));
    row.removeFromLeft (6);
    songBpmBox.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    setBpm.setBounds (row.removeFromLeft (44));
    L.removeFromTop (6);
    row = L.removeFromTop (26);
    keyBox.setBounds (row.removeFromLeft (80));
    row.removeFromLeft (8);
    keyLabel.setBounds (row);
    L.removeFromTop (6);
    row = L.removeFromTop (26);
    bar1Label.setBounds (row.removeFromLeft (330));
    nudgeL.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    nudgeR.setBounds (row.removeFromLeft (70));
    L.removeFromTop (8);
    clockBadge.setBounds (L.removeFromTop (48));
    L.removeFromTop (6);
    notesLabel.setBounds (L);

    hLock.setBounds (R.removeFromTop (16));
    row = R.removeFromTop (30);
    lockBtn.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    targetBox.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (6);
    hostLabel.setBounds (R.removeFromTop (22));
    R.removeFromTop (6);
    row = R.removeFromTop (30);
    keyBtn.setBounds (row.removeFromLeft (120));
    row.removeFromLeft (8);
    toKeyBox.setBounds (row.removeFromLeft (80));
    shiftLabel.setBounds (R.removeFromTop (22));
    R.removeFromTop (10);
    renderBtn.setBounds (R.removeFromTop (40));
    R.removeFromTop (8);
    progressBar.setBounds (R.removeFromTop (18));
    R.removeFromTop (8);
    renderBadge.setBounds (R.removeFromTop (48));
    statusLabel.setBounds (R.removeFromTop (34));

    b.removeFromTop (10);
    auto out = b.reduced (12, 10);
    row = out.removeFromTop (20);
    openBtn.setBounds (row.removeFromRight (110));
    hOut.setBounds (row);
    out.removeFromTop (8);
    auto grid = out.removeFromTop (34 * 4 + 6 * 3);
    const int cols = 3;
    const int w = (grid.getWidth() - (cols - 1) * 8) / cols;
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (grid.getX() + (i % cols) * (w + 8), grid.getY() + (i / cols) * 40, w, 34);
    out.removeFromTop (6);
    flagsLabel.setBounds (out);
}
