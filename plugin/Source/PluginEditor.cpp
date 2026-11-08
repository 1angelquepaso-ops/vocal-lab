#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff16161c), panel (0xff22222b), accent (0xffe0a84a), text (0xffe8e6e1);

    juce::var makeError (const juce::String& msg)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", msg);
        return juce::var (o);
    }
}

//==============================================================================
void EngineJob::run()
{
    juce::ChildProcess p;
    juce::var result;
    if (! p.start (args, juce::ChildProcess::wantStdOut))
    {
        result = makeError ("Couldn't start the engine:\n" + args[0]);
    }
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
void DragHandle::paint (juce::Graphics& g)
{
    g.setColour (isEnabled() ? accent : accent.withAlpha (0.3f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 4.0f, 1.5f);
    g.setFont (13.0f);
    g.drawFittedText (text, getLocalBounds(), juce::Justification::centred, 1);
}

void DragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || ! isEnabled() || e.getDistanceFromDragStart() < 4 || ! getFiles)
        return;
    const auto files = getFiles();
    if (files.isEmpty())
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (files, false, this,
                                                                [this] { dragging = false; });
}

//==============================================================================
HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setSize (660, 470);

    for (auto* l : { &vocalName, &beatName, &status, &optionLabel, &voicesLabel, &colourLabel })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (*l);
    }
    status.setJustificationType (juce::Justification::topLeft);
    optionLabel.setJustificationType (juce::Justification::centred);
    optionLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));

    addAndMakeVisible (loadVocal);
    addAndMakeVisible (loadBeat);
    loadVocal.onClick = [this] { chooseFile (false); };
    loadBeat.onClick  = [this] { chooseFile (true); };

    for (int i = 1; i <= 4; ++i)
        voicesBox.addItem (juce::String (i), i);
    voicesBox.setSelectedId (proc.voices, juce::dontSendNotification);
    voicesBox.onChange = [this] { proc.voices = voicesBox.getSelectedId(); };
    addAndMakeVisible (voicesBox);

    colour.setSliderStyle (juce::Slider::LinearHorizontal);
    colour.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
    colour.setRange (0.0, 1.0, 0.05);
    colour.setValue (proc.colour, juce::dontSendNotification);
    colour.onValueChange = [this] { proc.colour = (float) colour.getValue(); };
    addAndMakeVisible (colour);

    generate.setColour (juce::TextButton::buttonColourId, accent.darker (0.3f));
    addAndMakeVisible (generate);
    generate.onClick = [this]
    {
        proc.stopPreview();
        runEngine ({ "generate", "--session", proc.sessionDir.getFullPathName(),
                     "--count", "8", "--voices", juce::String (proc.voices),
                     "--colour", juce::String (proc.colour, 2),
                     "--seed", juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) },
                   [this] (juce::var r)
                   {
                       proc.options = r["options"];
                       proc.currentOption = 0;
                       proc.muted.clear();
                       proc.status = "8 options ready. Flip with < >, mute or reroll voices, drag out what you like.";
                   });
    };

    addAndMakeVisible (prev);
    addAndMakeVisible (next);
    addAndMakeVisible (optionLabel);
    prev.onClick = [this] { proc.currentOption = juce::jmax (0, proc.currentOption - 1); proc.stopPreview(); refresh(); };
    next.onClick = [this]
    {
        if (auto* a = proc.options.getArray())
            proc.currentOption = juce::jmin (a->size() - 1, proc.currentOption + 1);
        proc.stopPreview();
        refresh();
    };

    for (auto& row : rows)
    {
        row.name.setColour (juce::Label::textColourId, text);
        addAndMakeVisible (row.name);
        addAndMakeVisible (row.mute);
        addAndMakeVisible (row.reroll);
        addAndMakeVisible (row.remove);
        addAndMakeVisible (row.drag);
        auto* r = &row;
        row.mute.onClick = [this, r]
        {
            proc.muted[muteKey (r->name.getText())] = r->mute.getToggleState();
            rebuildPreview();
        };
        row.reroll.onClick = [this, r]
        {
            proc.stopPreview();
            runEngine ({ "reroll", "--session", proc.sessionDir.getFullPathName(),
                         "--option", juce::String (proc.currentOption), "--voice", r->name.getText(),
                         "--colour", juce::String (proc.colour, 2),
                         "--seed", juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) },
                       [this] (juce::var res)
                       {
                           if (auto* o = proc.options[proc.currentOption].getDynamicObject())
                               o->setProperty ("voices", res["voices"]);
                           proc.status = "Voice rerolled. The other voices were kept.";
                       });
        };
        row.remove.onClick = [this, r]
        {
            proc.stopPreview();
            runEngine ({ "remove", "--session", proc.sessionDir.getFullPathName(),
                         "--option", juce::String (proc.currentOption), "--voice", r->name.getText() },
                       [this] (juce::var res)
                       {
                           if (auto* o = proc.options[proc.currentOption].getDynamicObject())
                               o->setProperty ("voices", res["voices"]);
                           proc.status = "Voice removed.";
                       });
        };
        row.drag.text = "drag WAV";
        row.drag.getFiles = [this, r] { return exportVoices (false, r->name.getText()); };
    }

    addAndMakeVisible (addVoice);
    addVoice.onClick = [this]
    {
        proc.stopPreview();
        runEngine ({ "add", "--session", proc.sessionDir.getFullPathName(),
                     "--option", juce::String (proc.currentOption),
                     "--colour", juce::String (proc.colour, 2),
                     "--seed", juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) },
                   [this] (juce::var res)
                   {
                       if (auto* o = proc.options[proc.currentOption].getDynamicObject())
                           o->setProperty ("voices", res["voices"]);
                       proc.status = "Voice added around the others.";
                   });
    };

    addAndMakeVisible (play);
    play.onClick = [this]
    {
        if (proc.isPreviewing()) { proc.stopPreview(); return; }
        rebuildPreview();
        proc.startPreview();
    };

    leadToggle.setToggleState (proc.leadInPreview, juce::dontSendNotification);
    leadToggle.onClick = [this] { proc.leadInPreview = leadToggle.getToggleState(); rebuildPreview(); };
    addAndMakeVisible (leadToggle);

    dragAll.text = "drag all unmuted";
    dragAll.getFiles = [this] { return exportVoices (true); };
    addAndMakeVisible (dragAll);

    addAndMakeVisible (openFolder);
    openFolder.onClick = [this] { exportVoices (true); exportDir().startAsProcess(); };

    refresh();
    startTimerHz (10);
}

HarmonizerEditor::~HarmonizerEditor()
{
    stopTimer();
    job.reset();
}

//==============================================================================
juce::File HarmonizerEditor::engineExe() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("VOCAL_LAB_ENGINE", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile())
        return juce::File (env);

   #if JUCE_WINDOWS
    const juce::String exeName ("vocal-lab-engine.exe");
   #else
    const juce::String exeName ("vocal-lab-engine");
   #endif
    // inside the VST3 bundle: <bundle>/Contents/x86_64-win/<plugin>.vst3 -> <bundle>/Contents/Resources/engine
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/engine"),
                      me.getParentDirectory().getChildFile ("engine"),
                      juce::File ("C:\\vocal-lab\\engine") })
        if (dir.getChildFile (exeName).existsAsFile())
            return dir.getChildFile (exeName);
    return {};
}

juce::File HarmonizerEditor::exportDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                 .getChildFile ("vocal-lab").getChildFile ("harmonies")
                 .getChildFile (proc.vocalFile.getFileNameWithoutExtension());
    d.createDirectory();
    return d;
}

void HarmonizerEditor::runEngine (juce::StringArray args, std::function<void (juce::var)> onOk)
{
    if (busy)
        return;
    const auto exe = engineExe();
    if (! exe.existsAsFile())
    {
        proc.status = "Engine not found. It ships inside the plugin folder (Contents/Resources/engine).";
        refresh();
        return;
    }
    args.insert (0, exe.getFullPathName());
    busy = true;
    proc.status = "Working...";
    refresh();
    juce::Component::SafePointer<HarmonizerEditor> safe (this);
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
        safe->rebuildPreview();
    });
    job->startThread();
}

void HarmonizerEditor::chooseFile (bool beat)
{
    chooser = std::make_unique<juce::FileChooser> (beat ? "Beat for this section (analysis only)" : "Dry vocal chunk",
                                                   juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, beat] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile())
                                  return;
                              (beat ? proc.beatFile : proc.vocalFile) = f;
                              if (proc.vocalFile.existsAsFile())
                                  analyze();
                              else
                                  refresh();
                          });
}

bool HarmonizerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3"))
            return true;
    return false;
}

void HarmonizerEditor::filesDropped (const juce::StringArray& files, int, int y)
{
    // drop on the top half = vocal, bottom half = beat (or two files at once)
    if (files.size() >= 2) { proc.vocalFile = juce::File (files[0]); proc.beatFile = juce::File (files[1]); }
    else if (y > getHeight() / 2 && proc.vocalFile.existsAsFile()) proc.beatFile = juce::File (files[0]);
    else proc.vocalFile = juce::File (files[0]);
    analyze();
}

void HarmonizerEditor::analyze()
{
    proc.stopPreview();
    proc.options = juce::var();
    proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("vocal-lab")
                          .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
    proc.sessionDir.createDirectory();
    juce::StringArray args { "analyze", "--session", proc.sessionDir.getFullPathName(),
                             "--vocal", proc.vocalFile.getFullPathName() };
    if (proc.beatFile.existsAsFile())
        args.addArray ({ "--beat", proc.beatFile.getFullPathName() });
    runEngine (args, [this] (juce::var r)
    {
        juce::String s;
        s << "Key " << r["key"].toString() << " " << r["scale"].toString()
          << "  |  " << r["notes"].toString() << " notes, " << r["seconds"].toString() << " s"
          << "  |  chords from the " << r["chords_from"].toString();
        if (! r["bpm"].isVoid())
            s << " (" << juce::String ((double) r["bpm"], 1) << " BPM)";
        proc.status = s + "\nPick how many voices, then Generate.";
    });
}

//==============================================================================
juce::var HarmonizerEditor::currentVoices() const
{
    if (auto* a = proc.options.getArray())
        if (proc.currentOption < a->size())
            return (*a)[proc.currentOption]["voices"];
    return {};
}

juce::String HarmonizerEditor::muteKey (const juce::String& voice) const
{
    return juce::String (proc.currentOption) + "/" + voice;
}

juce::StringArray HarmonizerEditor::exportVoices (bool onlyUnmuted, const juce::String& single)
{
    juce::StringArray out;
    const auto dir = exportDir();
    const auto base = proc.vocalFile.getFileNameWithoutExtension();
    if (auto* vs = currentVoices().getArray())
        for (auto& v : *vs)
        {
            const auto name = v["name"].toString();
            if (single.isNotEmpty() && name != single) continue;
            if (onlyUnmuted && proc.muted[muteKey (name)]) continue;
            const juce::File src (v["wav"].toString());
            const auto dst = dir.getChildFile (base + "_opt" + juce::String (proc.currentOption + 1)
                                               + "_" + name + ".wav");
            if (src.existsAsFile() && src.copyFileTo (dst))
                out.add (dst.getFullPathName());
        }
    return out;
}

void HarmonizerEditor::rebuildPreview()
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::AudioBuffer<float> mix;
    double rate = 44100.0;

    auto addFile = [&] (const juce::File& f, float gain)
    {
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) return;
        rate = r->sampleRate;
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> tmp ((int) r->numChannels, n);
        r->read (&tmp, 0, n, 0, true, true);
        if (mix.getNumSamples() < n)
            mix.setSize (1, n, true, true);
        for (int c = 0; c < tmp.getNumChannels(); ++c)
            mix.addFrom (0, 0, tmp, c, 0, n, gain / (float) tmp.getNumChannels());
    };

    if (proc.leadInPreview)
        addFile (proc.sessionDir.getChildFile ("vocal.wav"), 0.8f);
    if (auto* vs = currentVoices().getArray())
        for (auto& v : *vs)
            if (! proc.muted[muteKey (v["name"].toString())])
                addFile (juce::File (v["wav"].toString()), 0.7f);

    const auto peak = mix.getMagnitude (0, mix.getNumSamples());
    if (peak > 0.9f)
        mix.applyGain (0.9f / peak);
    const bool wasPlaying = proc.isPreviewing();
    proc.setPreview (std::move (mix), rate);
    if (wasPlaying)
        proc.startPreview();
}

void HarmonizerEditor::refresh()
{
    vocalName.setText (proc.vocalFile.existsAsFile() ? proc.vocalFile.getFileName() : "no vocal loaded",
                       juce::dontSendNotification);
    beatName.setText (proc.beatFile.existsAsFile() ? proc.beatFile.getFileName() : "no beat (optional)",
                      juce::dontSendNotification);
    status.setText (proc.status, juce::dontSendNotification);

    const auto* a = proc.options.getArray();
    const int nOpt = a ? a->size() : 0;
    optionLabel.setText (nOpt ? "Option " + juce::String (proc.currentOption + 1) + " / " + juce::String (nOpt)
                              : "No options yet",
                         juce::dontSendNotification);

    const bool haveVocal = proc.sessionDir.getChildFile ("meta.json").existsAsFile();
    loadVocal.setEnabled (! busy);
    loadBeat.setEnabled (! busy);
    generate.setEnabled (! busy && haveVocal);
    prev.setEnabled (! busy && proc.currentOption > 0);
    next.setEnabled (! busy && proc.currentOption < nOpt - 1);

    const auto* vs = currentVoices().getArray();
    const int nv = vs ? vs->size() : 0;
    for (int i = 0; i < 4; ++i)
    {
        auto& row = rows[i];
        const bool on = i < nv;
        for (juce::Component* c : { (juce::Component*) &row.name, (juce::Component*) &row.mute,
                                    (juce::Component*) &row.reroll, (juce::Component*) &row.remove,
                                    (juce::Component*) &row.drag })
            c->setVisible (on);
        if (! on) continue;
        const auto name = (*vs)[i]["name"].toString();
        row.name.setText (name, juce::dontSendNotification);
        row.mute.setToggleState (proc.muted[muteKey (name)], juce::dontSendNotification);
        row.reroll.setEnabled (! busy);
        row.remove.setEnabled (! busy && nv > 1);
        row.drag.setEnabled (! busy);
    }
    addVoice.setEnabled (! busy && nOpt > 0 && nv < 4);
    play.setEnabled (haveVocal);
    dragAll.setEnabled (! busy && nv > 0);
    openFolder.setEnabled (nv > 0);
    repaint();
}

void HarmonizerEditor::timerCallback()
{
    play.setButtonText (proc.isPreviewing() ? "Stop" : "Play preview");
    repaint (getLocalBounds().removeFromBottom (12));
}

//==============================================================================
void HarmonizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (panel);
    g.fillRoundedRectangle (juce::Rectangle<float> (12, 150, (float) getWidth() - 24, 230), 8.0f);
    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("vocal-lab Harmonizer", 16, 8, 300, 26, juce::Justification::left);
    g.setColour (text.withAlpha (0.5f));
    g.setFont (12.0f);
    g.drawText ("dry harmonies, in key, drag into FL", getWidth() - 260, 12, 244, 20, juce::Justification::right);
    // preview progress bar
    g.setColour (accent.withAlpha (0.8f));
    g.fillRect (0.0f, (float) getHeight() - 4, getWidth() * proc.previewProgress(), 4.0f);
}

void HarmonizerEditor::resized()
{
    auto r = getLocalBounds().reduced (16);
    r.removeFromTop (28);

    auto files = r.removeFromTop (30);
    loadVocal.setBounds (files.removeFromLeft (150));
    vocalName.setBounds (files.removeFromLeft (170).withTrimmedLeft (6));
    loadBeat.setBounds (files.removeFromLeft (180));
    beatName.setBounds (files.withTrimmedLeft (6));
    r.removeFromTop (6);

    auto ctl = r.removeFromTop (28);
    voicesLabel.setBounds (ctl.removeFromLeft (50));
    voicesBox.setBounds (ctl.removeFromLeft (60));
    ctl.removeFromLeft (14);
    colourLabel.setBounds (ctl.removeFromLeft (52));
    colour.setBounds (ctl.removeFromLeft (200));
    ctl.removeFromLeft (14);
    generate.setBounds (ctl);
    r.removeFromTop (12);

    auto inner = r.removeFromTop (230).reduced (10, 8);
    auto nav = inner.removeFromTop (30);
    prev.setBounds (nav.removeFromLeft (40));
    next.setBounds (nav.removeFromRight (40));
    optionLabel.setBounds (nav);
    inner.removeFromTop (8);
    for (auto& row : rows)
    {
        auto line = inner.removeFromTop (34).reduced (0, 3);
        row.name.setBounds (line.removeFromLeft (110));
        row.mute.setBounds (line.removeFromLeft (80));
        row.reroll.setBounds (line.removeFromLeft (90));
        line.removeFromLeft (6);
        row.remove.setBounds (line.removeFromLeft (34));
        line.removeFromLeft (14);
        row.drag.setBounds (line.removeFromLeft (120));
    }
    addVoice.setBounds (inner.removeFromTop (28).removeFromLeft (120));
    r.removeFromTop (10);

    auto bottom = r.removeFromTop (30);
    play.setBounds (bottom.removeFromLeft (120));
    bottom.removeFromLeft (8);
    leadToggle.setBounds (bottom.removeFromLeft (170));
    openFolder.setBounds (bottom.removeFromRight (110));
    bottom.removeFromRight (8);
    dragAll.setBounds (bottom.removeFromRight (150));
    r.removeFromTop (6);
    status.setBounds (r);
}
