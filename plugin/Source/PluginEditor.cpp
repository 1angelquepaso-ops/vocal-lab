#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff15151b), panel (0xff1f1f27), panel2 (0xff272731),
                       accent (0xffe0a84a), text (0xffe8e6e1), dim (0xff8a8890),
                       wave (0xff7fb3d5), leadWave (0xffc9c4b8);

    const char* styleIds[]    = { "mix", "classic", "colour", "floating", "busy", "low",
                                  "dark_phrygian", "dark_harmonic" };
    const char* styleNames[]  = { "Mix (surprise me)", "Classic", "Colour 9ths", "Floating",
                                  "Busy", "Low counter-melody", "Dark Phrygian",
                                  "Dark harmonic minor" };

    juce::var makeError (const juce::String& msg)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", msg);
        return juce::var (o);
    }

    juce::String mmss (double s)
    {
        const int t = (int) s;
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
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
Lane::Lane (juce::AudioFormatManager& fm, juce::AudioThumbnailCache& cache, bool isLead)
    : lead (isLead), thumb (512, fm, cache)
{
    thumb.addChangeListener (this);
    if (! lead)
    {
        for (auto* b : { &mute, &solo, &reroll, &remove })
            addAndMakeVisible (*b);
        mute.setClickingTogglesState (true);
        solo.setClickingTogglesState (true);
        mute.setTooltip ("Mute this voice");
        solo.setTooltip ("Hear only this voice (with the lead if 'hear lead' is on)");
        reroll.setTooltip ("Write a new part for this voice; the others stay");
        remove.setTooltip ("Remove this voice");
        mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb04a4a));
        solo.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.2f));
    }
}

Lane::~Lane() { thumb.removeChangeListener (this); }

void Lane::setFile (const juce::File& stem)
{
    thumb.clear();
    if (stem.existsAsFile())
        thumb.setSource (new juce::FileInputSource (stem));
    repaint();
}

juce::Rectangle<int> Lane::waveArea() const
{
    auto r = getLocalBounds().reduced (0, 3);
    r.removeFromLeft (70);
    r.removeFromRight (196);
    return r;
}

juce::Rectangle<int> Lane::handleArea() const
{
    return waveArea().removeFromRight (34);
}

void Lane::resized()
{
    if (lead) return;
    auto r = getLocalBounds().reduced (0, 6);
    r.removeFromRight (4);
    remove.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    reroll.setBounds (r.removeFromRight (64));
    r.removeFromRight (4);
    solo.setBounds (r.removeFromRight (32));
    r.removeFromRight (4);
    mute.setBounds (r.removeFromRight (32));
}

void Lane::paint (juce::Graphics& g)
{
    auto wa = waveArea();
    g.setColour (panel2);
    g.fillRoundedRectangle (wa.toFloat(), 4.0f);

    g.setColour (lead ? leadWave : (dimmed ? dim : text));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (lead ? "LEAD" : voice.toUpperCase(), 4, 0, 64, getHeight(),
                juce::Justification::centredLeft);

    auto inner = wa.reduced (4, 2);
    inner.removeFromRight (34);
    if (thumb.getTotalLength() > 0)
    {
        // scale quiet takes up so the shape is readable
        float lo = 0, hi = 0;
        thumb.getApproximateMinMax (0.0, thumb.getTotalLength(), 0, lo, hi);
        const float peak = juce::jmax (std::abs (lo), std::abs (hi), 0.001f);
        g.setColour ((lead ? leadWave : wave).withAlpha (dimmed ? 0.3f : 0.9f));
        thumb.drawChannels (g, inner, 0.0, thumb.getTotalLength(), juce::jlimit (1.0f, 12.0f, 0.95f / peak));
    }
    else
    {
        g.setColour (dim);
        g.setFont (12.0f);
        g.drawText (lead ? "load a vocal" : "", inner, juce::Justification::centred);
    }

    if (playhead)
    {
        const float x = (float) inner.getX() + playhead() * (float) inner.getWidth();
        g.setColour (accent);
        g.drawLine (x, (float) wa.getY(), x, (float) wa.getBottom(), 1.5f);
    }

    if (! lead && ! voice.isEmpty())
    {
        // grab handle at the end of the waveform
        auto h = handleArea().reduced (3);
        g.setColour (exportReady ? (hoverHandle ? accent : accent.withAlpha (0.65f))
                                 : dim.withAlpha (0.4f));
        g.fillRoundedRectangle (h.toFloat(), 4.0f);
        g.setColour (bg);
        for (int i = 0; i < 3; ++i)
        {
            const float y = (float) h.getCentreY() - 6.0f + (float) i * 6.0f;
            g.drawLine ((float) h.getX() + 8.0f, y, (float) h.getRight() - 8.0f, y, 2.0f);
        }
    }
}

void Lane::mouseMove (const juce::MouseEvent& e)
{
    const bool over = ! lead && handleArea().contains (e.getPosition());
    if (over != hoverHandle) { hoverHandle = over; repaint(); }
    setMouseCursor (over && exportReady ? juce::MouseCursor::DraggingHandCursor
                                        : waveArea().contains (e.getPosition())
                                              ? juce::MouseCursor::PointingHandCursor
                                              : juce::MouseCursor::NormalCursor);
}

void Lane::mouseExit (const juce::MouseEvent&)
{
    hoverHandle = false;
    repaint();
}

void Lane::mouseDown (const juce::MouseEvent& e)
{
    pressOnHandle = ! lead && handleArea().contains (e.getPosition());
    if (pressOnHandle) return;
    auto inner = waveArea().reduced (4, 2);
    inner.removeFromRight (34);
    if (inner.contains (e.getPosition()) && onSeek)
        onSeek ((float) (e.x - inner.getX()) / (float) juce::jmax (1, inner.getWidth()));
}

void Lane::mouseDrag (const juce::MouseEvent& e)
{
    // only the grab handle starts a drag, and only one file, already on disk
    if (! pressOnHandle || dragging || ! exportReady || e.getDistanceFromDragStart() < 5)
        return;
    if (! exportFile.existsAsFile())
        return;
    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        juce::StringArray { exportFile.getFullPathName() }, false, this,
        [safe = juce::Component::SafePointer<Lane> (this)]
        { if (safe != nullptr) safe->dragging = false; });
}

void Lane::mouseUp (const juce::MouseEvent&)
{
    pressOnHandle = false;
    dragging = false;
}

//==============================================================================
HarmonizerEditor::HarmonizerEditor (HarmonizerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    formats.registerBasicFormats();

    for (auto* l : { &vocalName, &beatName, &keyInfo, &styleLabel, &voicesLabel,
                     &optionsTitle, &timeLabel, &status })
    {
        l->setColour (juce::Label::textColourId, text);
        addAndMakeVisible (*l);
    }
    keyInfo.setColour (juce::Label::textColourId, accent);
    keyInfo.setJustificationType (juce::Justification::centredRight);
    optionsTitle.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    optionsTitle.setColour (juce::Label::textColourId, dim);
    status.setJustificationType (juce::Justification::centredLeft);
    timeLabel.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (loadVocal);
    addAndMakeVisible (loadBeat);
    loadVocal.onClick = [this] { chooseFile (false); };
    loadBeat.onClick  = [this] { chooseFile (true); };

    for (int i = 0; i < 8; ++i)
        styleBox.addItem (styleNames[i], i + 1);
    styleBox.setSelectedItemIndex (proc.styleIndex, juce::dontSendNotification);
    styleBox.onChange = [this] { proc.styleIndex = styleBox.getSelectedItemIndex(); };
    addAndMakeVisible (styleBox);

    for (int i = 1; i <= 4; ++i)
        voicesBox.addItem (juce::String (i) + (i == 1 ? " voice" : " voices"), i);
    voicesBox.setSelectedId (proc.voices, juce::dontSendNotification);
    voicesBox.onChange = [this] { proc.voices = voicesBox.getSelectedId(); };
    addAndMakeVisible (voicesBox);

    gen1.setColour (juce::TextButton::buttonColourId, accent.darker (0.35f));
    addAndMakeVisible (gen1);
    addAndMakeVisible (gen4);
    gen1.onClick = [this] { generate (1); };
    gen4.onClick = [this] { generate (4); };

    optionList.setRowHeight (30);
    optionList.setColour (juce::ListBox::backgroundColourId, panel);
    optionList.setOutlineThickness (0);
    addAndMakeVisible (optionList);
    addAndMakeVisible (keepBtn);
    addAndMakeVisible (deleteBtn);
    keepBtn.onClick = [this]
    {
        const int id = currentId();
        if (id < 0) return;
        if (proc.kept.count (id)) proc.kept.erase (id); else proc.kept.insert (id);
        optionList.repaint();
        refresh();
    };
    deleteBtn.onClick = [this]
    {
        if (proc.currentOption < 0) return;
        proc.stopPreview();
        proc.options.remove (proc.currentOption);
        optionList.updateContent();
        showOption (juce::jmin (proc.currentOption, proc.options.size() - 1));
    };

    leadLane = std::make_unique<Lane> (formats, thumbCache, true);
    leadLane->onSeek = [this] (float f) { proc.seek (f); };
    leadLane->playhead = [this] { return proc.previewProgress(); };
    addAndMakeVisible (*leadLane);
    for (auto& ln : voiceLanes)
    {
        ln = std::make_unique<Lane> (formats, thumbCache, false);
        auto* lane = ln.get();
        lane->onSeek = [this] (float f) { proc.seek (f); };
        lane->playhead = [this] { return proc.previewProgress(); };
        lane->mute.onClick = [this, lane]
        {
            proc.muted[key (lane->voice)] = lane->mute.getToggleState();
            rebuildPreview(); refresh();
        };
        lane->solo.onClick = [this, lane]
        {
            proc.soloed[key (lane->voice)] = lane->solo.getToggleState();
            rebuildPreview(); refresh();
        };
        lane->reroll.onClick = [this, lane] { voiceCommand ("reroll", lane->voice); };
        lane->remove.onClick = [this, lane] { voiceCommand ("remove", lane->voice); };
        addChildComponent (*lane);
    }
    addAndMakeVisible (addVoice);
    addVoice.onClick = [this] { voiceCommand ("add", {}); };

    addAndMakeVisible (play);
    play.onClick = [this]
    {
        if (proc.isPreviewing()) { proc.stopPreview(); return; }
        rebuildPreview();
        if (proc.previewProgress() >= 0.999f) proc.seek (0.0f);
        proc.startPreviewFromHere();
    };
    loopBtn.setClickingTogglesState (true);
    loopBtn.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
    loopBtn.setToggleState (proc.loop.load(), juce::dontSendNotification);
    loopBtn.onClick = [this] { proc.loop = loopBtn.getToggleState(); };
    addAndMakeVisible (loopBtn);
    leadToggle.setToggleState (proc.leadInPreview, juce::dontSendNotification);
    leadToggle.onClick = [this] { proc.leadInPreview = leadToggle.getToggleState(); rebuildPreview(); };
    addAndMakeVisible (leadToggle);
    addAndMakeVisible (openFolder);
    openFolder.onClick = [this] { exportDir().startAsProcess(); };
    addChildComponent (progressBar);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, accent);

    if (proc.sessionDir.getChildFile ("vocal.wav").existsAsFile())
        leadLane->setFile (proc.sessionDir.getChildFile ("vocal.wav"));
    setResizable (true, true);
    setResizeLimits (820, 560, 1800, 1200);
    setSize (980, 640);              // sizing last: every child exists before the first layout

    optionList.updateContent();
    showOption (proc.currentOption);
    startTimerHz (20);

    // developer hook for automated UI checks; unset on normal machines
    const auto dev = juce::SystemStats::getEnvironmentVariable ("VOCAL_LAB_DEV_VOCAL", {});
    if (dev.isNotEmpty() && ! proc.vocalFile.existsAsFile() && juce::File (dev).existsAsFile())
    {
        proc.vocalFile = juce::File (dev);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<HarmonizerEditor> (this)]
                                         { if (safe != nullptr) safe->analyze(); });
    }
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
        proc.status = "Engine not found inside the plugin folder (Contents/Resources/engine).";
        refresh();
        return;
    }
    proc.sessionDir.getChildFile ("progress.txt").deleteFile();
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
                              if (! f.existsAsFile()) return;
                              (beat ? proc.beatFile : proc.vocalFile) = f;
                              if (proc.vocalFile.existsAsFile()) analyze(); else refresh();
                          });
}

bool HarmonizerEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3"))
            return true;
    return false;
}

void HarmonizerEditor::filesDropped (const juce::StringArray& files, int x, int)
{
    // drop on the left half = vocal, right half = beat (or two files at once)
    if (files.size() >= 2) { proc.vocalFile = juce::File (files[0]); proc.beatFile = juce::File (files[1]); }
    else if (x > getWidth() / 2 && proc.vocalFile.existsAsFile()) proc.beatFile = juce::File (files[0]);
    else proc.vocalFile = juce::File (files[0]);
    analyze();
}

void HarmonizerEditor::analyze()
{
    if (busy) return;
    proc.stopPreview();
    proc.options.clear();
    proc.currentOption = -1;
    proc.muted.clear(); proc.soloed.clear(); proc.kept.clear();
    optionList.updateContent();
    proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("vocal-lab")
                          .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
    proc.sessionDir.createDirectory();
    juce::StringArray args { "analyze", "--session", proc.sessionDir.getFullPathName(),
                             "--vocal", proc.vocalFile.getFullPathName() };
    if (proc.beatFile.existsAsFile())
        args.addArray ({ "--beat", proc.beatFile.getFullPathName() });
    leadLane->setFile ({});
    runEngine (args, [this] (juce::var r)
    {
        juce::String s;
        s << r["key"].toString() << " " << r["scale"].toString() << "  ·  "
          << mmss ((double) r["seconds"]) << "  ·  chords from " << r["chords_from"].toString();
        if (! r["bpm"].isVoid())
            s << "  ·  " << juce::String ((double) r["bpm"], 1) << " BPM";
        proc.keyInfo = s;
        proc.status = "Ready. Pick a style and voices, then + Generate 1.";
        leadLane->setFile (proc.sessionDir.getChildFile ("vocal.wav"));
        rebuildPreview();
    });
}

void HarmonizerEditor::generate (int count)
{
    if (! proc.sessionDir.getChildFile ("meta.json").existsAsFile()) return;
    proc.stopPreview();
    runEngine ({ "generate", "--session", proc.sessionDir.getFullPathName(),
                 "--count", juce::String (count), "--voices", juce::String (proc.voices),
                 "--style", styleIds[juce::jlimit (0, 7, proc.styleIndex)],
                 "--seed", juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) },
               [this, count] (juce::var r)
               {
                   int first = proc.options.size();
                   if (auto* a = r["options"].getArray())
                       for (auto& o : *a) proc.options.add (o);
                   optionList.updateContent();
                   proc.status = count == 1 ? "New option added." : juce::String (count) + " options added.";
                   showOption (first);
               });
}

void HarmonizerEditor::voiceCommand (const juce::String& cmd, const juce::String& voice)
{
    if (currentId() < 0) return;
    juce::StringArray args { cmd, "--session", proc.sessionDir.getFullPathName(),
                             "--option", juce::String (currentId()),
                             "--seed", juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)) };
    if (voice.isNotEmpty()) args.addArray ({ "--voice", voice });
    runEngine (args, [this, cmd] (juce::var r)
    {
        replaceCurrent (r);
        proc.status = cmd == "reroll" ? "Voice rerolled; the other voices were kept."
                    : cmd == "add"    ? "Voice added around the others."
                                      : "Voice removed.";
    });
}

void HarmonizerEditor::replaceCurrent (const juce::var& res)
{
    if (proc.currentOption < 0) return;
    auto* o = new juce::DynamicObject();
    o->setProperty ("option", res["option"]);
    o->setProperty ("style", res["style"]);
    o->setProperty ("label", res["label"]);
    o->setProperty ("voices", res["voices"]);
    proc.options.set (proc.currentOption, juce::var (o));
    optionList.repaint();
    showOption (proc.currentOption);
}

//==============================================================================
juce::var HarmonizerEditor::current() const
{
    return juce::isPositiveAndBelow (proc.currentOption, proc.options.size())
               ? proc.options[proc.currentOption] : juce::var();
}

int HarmonizerEditor::currentId() const
{
    const auto c = current();
    return c.isObject() ? (int) c["option"] : -1;
}

juce::String HarmonizerEditor::key (const juce::String& voice) const
{
    return juce::String (currentId()) + "/" + voice;
}

void HarmonizerEditor::showOption (int index)
{
    proc.currentOption = proc.options.isEmpty() ? -1 : juce::jlimit (0, proc.options.size() - 1, index);
    if (proc.currentOption >= 0)
        optionList.selectRow (proc.currentOption, true, true);
    else
        optionList.deselectAllRows();

    const auto* vs = current()["voices"].getArray();
    for (int i = 0; i < 4; ++i)
    {
        auto& lane = *voiceLanes[i];
        const bool on = vs != nullptr && i < vs->size();
        lane.setVisible (on);
        if (! on) continue;
        lane.voice = (*vs)[i]["name"].toString();
        lane.setFile (juce::File ((*vs)[i]["wav"].toString()));
        lane.setExport ({}, false);
        lane.mute.setToggleState (proc.muted[key (lane.voice)], juce::dontSendNotification);
        lane.solo.setToggleState (proc.soloed[key (lane.voice)], juce::dontSendNotification);
    }
    prepareExports();
    rebuildPreview();
    resized();
    refresh();
}

void HarmonizerEditor::prepareExports()
{
    // Copy this option's stems to Documents NOW, off the message thread, so a
    // drag only ever hands FL one file that already exists.
    const auto* vs = current()["voices"].getArray();
    if (vs == nullptr) return;
    const auto dir = exportDir();
    const auto base = proc.vocalFile.getFileNameWithoutExtension();
    const auto label = current()["label"].toString().removeCharacters (" ");
    const int id = currentId();
    for (int i = 0; i < juce::jmin (4, vs->size()); ++i)
    {
        const juce::File src ((*vs)[i]["wav"].toString());
        const auto dst = dir.getChildFile (base + "_opt" + juce::String (id + 1) + "_" + label
                                           + "_" + (*vs)[i]["name"].toString() + ".wav");
        juce::Component::SafePointer<Lane> lane (voiceLanes[i].get());
        juce::Thread::launch ([src, dst, lane, id, this]
        {
            const bool ok = src.existsAsFile() && (dst.deleteFile(), src.copyFileTo (dst));
            juce::MessageManager::callAsync ([lane, dst, ok, id, safe = juce::Component::SafePointer<HarmonizerEditor> (this)]
            {
                if (lane != nullptr && safe != nullptr && safe->currentId() == id)
                    lane->setExport (dst, ok);
            });
        });
    }
}

void HarmonizerEditor::rebuildPreview()
{
    juce::AudioBuffer<float> mix;
    double rate = 44100.0;
    auto addFile = [&] (const juce::File& f, float gain)
    {
        std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
        if (r == nullptr) return;
        rate = r->sampleRate;
        const int n = (int) r->lengthInSamples;
        juce::AudioBuffer<float> tmp ((int) r->numChannels, n);
        r->read (&tmp, 0, n, 0, true, true);
        if (mix.getNumSamples() < n) mix.setSize (1, n, true, true);
        for (int c = 0; c < tmp.getNumChannels(); ++c)
            mix.addFrom (0, 0, tmp, c, 0, n, gain / (float) tmp.getNumChannels());
    };

    bool anySolo = false;
    const auto* vs = current()["voices"].getArray();
    if (vs != nullptr)
        for (auto& v : *vs) anySolo |= proc.soloed[key (v["name"].toString())];

    if (proc.leadInPreview)
        addFile (proc.sessionDir.getChildFile ("vocal.wav"), 0.8f);
    if (vs != nullptr)
        for (auto& v : *vs)
        {
            const auto k = key (v["name"].toString());
            if (proc.muted[k] || (anySolo && ! proc.soloed[k])) continue;
            addFile (juce::File (v["wav"].toString()), 0.7f);
        }
    for (int i = 0; i < 4; ++i)
        if (voiceLanes[i]->isVisible())
        {
            const auto k = key (voiceLanes[i]->voice);
            voiceLanes[i]->dimmed = proc.muted[k] || (anySolo && ! proc.soloed[k]);
            voiceLanes[i]->repaint();
        }
    const auto peak = mix.getMagnitude (0, mix.getNumSamples());
    if (peak > 0.9f) mix.applyGain (0.9f / peak);
    proc.setPreview (std::move (mix), rate);
}

//==============================================================================
void HarmonizerEditor::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, proc.options.size())) return;
    const auto o = proc.options[row];
    const int id = (int) o["option"];
    if (selected) { g.setColour (panel2.brighter (0.15f)); g.fillRect (0, 0, w, h); }
    g.setColour (selected ? accent : text);
    g.setFont (juce::FontOptions (14.0f));
    const int nv = o["voices"].getArray() ? o["voices"].getArray()->size() : 0;
    juce::String s;
    s << (proc.kept.count (id) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x98\x85 ")) : "   ")
      << (row + 1) << "  " << o["label"].toString() << "  (" << nv << "v)";
    g.drawText (s, 6, 0, w - 8, h, juce::Justification::centredLeft);
}

void HarmonizerEditor::selectedRowsChanged (int row)
{
    if (row >= 0 && row != proc.currentOption)
    {
        const bool was = proc.isPreviewing();
        showOption (row);
        if (was) proc.startPreviewFromHere();
    }
}

void HarmonizerEditor::refresh()
{
    vocalName.setText (proc.vocalFile.existsAsFile() ? proc.vocalFile.getFileName() : "no vocal loaded",
                       juce::dontSendNotification);
    beatName.setText (proc.beatFile.existsAsFile() ? proc.beatFile.getFileName() : "no beat (optional)",
                      juce::dontSendNotification);
    keyInfo.setText (proc.keyInfo, juce::dontSendNotification);
    status.setText (proc.status, juce::dontSendNotification);

    const bool ready = proc.sessionDir.getChildFile ("meta.json").existsAsFile();
    const bool haveOpt = currentId() >= 0;
    const auto* vs = current()["voices"].getArray();
    const int nv = vs ? vs->size() : 0;
    for (auto* b : { &loadVocal, &loadBeat })
        b->setEnabled (! busy);
    gen1.setEnabled (! busy && ready);
    gen4.setEnabled (! busy && ready);
    keepBtn.setEnabled (haveOpt);
    keepBtn.setButtonText (haveOpt && proc.kept.count (currentId()) ? "Unkeep" : "Keep");
    deleteBtn.setEnabled (! busy && haveOpt);
    addVoice.setEnabled (! busy && haveOpt && nv < 4);
    for (auto& ln : voiceLanes)
    {
        ln->reroll.setEnabled (! busy);
        ln->remove.setEnabled (! busy && nv > 1);
    }
    play.setEnabled (ready);
    openFolder.setEnabled (haveOpt);
    progressBar.setVisible (busy);
    repaint();
}

void HarmonizerEditor::timerCallback()
{
    play.setButtonText (proc.isPreviewing() ? "Stop" : "Play");
    const double len = proc.previewSeconds();
    timeLabel.setText (mmss (proc.previewProgress() * len) + " / " + mmss (len), juce::dontSendNotification);
    leadLane->repaint();
    for (auto& ln : voiceLanes)
        if (ln->isVisible()) ln->repaint();

    if (busy)
    {
        const auto p = proc.sessionDir.getChildFile ("progress.txt").loadFileAsString().trim();
        if (p.isNotEmpty())
        {
            status.setText (p, juce::dontSendNotification);
            // "... 3/7" -> 3/7 of the way
            const auto frac = p.fromLastOccurrenceOf (" ", false, false);
            const int a = frac.upToFirstOccurrenceOf ("/", false, false).getIntValue();
            const int b = frac.fromFirstOccurrenceOf ("/", false, false).getIntValue();
            progress = (a > 0 && b > 0) ? juce::jlimit (0.0, 1.0, (a - 0.5) / b) : -1.0;
        }
        else
            progress = -1.0;
    }
}

//==============================================================================
void HarmonizerEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (12);
    g.setColour (accent);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("vocal-lab Harmonizer", r.removeFromTop (28), juce::Justification::centredLeft);

    g.setColour (panel);
    auto body = getLocalBounds().reduced (12);
    body.removeFromTop (28 + 8 + 34 + 8);
    body.removeFromBottom (44 + 8);
    g.fillRoundedRectangle (body.removeFromLeft (220).toFloat(), 6.0f);
    body.removeFromLeft (10);
    g.fillRoundedRectangle (body.toFloat(), 6.0f);
}

void HarmonizerEditor::resized()
{
    if (leadLane == nullptr)
        return;
    auto r = getLocalBounds().reduced (12);
    r.removeFromTop (28 + 8);

    auto src = r.removeFromTop (34);
    loadVocal.setBounds (src.removeFromLeft (110).reduced (0, 3));
    vocalName.setBounds (src.removeFromLeft (200).withTrimmedLeft (6));
    loadBeat.setBounds (src.removeFromLeft (190).reduced (0, 3));
    beatName.setBounds (src.removeFromLeft (170).withTrimmedLeft (6));
    keyInfo.setBounds (src);
    r.removeFromTop (8);

    auto bottom = r.removeFromBottom (44);
    r.removeFromBottom (8);

    // options column
    auto left = r.removeFromLeft (220).reduced (8);
    optionsTitle.setBounds (left.removeFromTop (22));
    auto btns = left.removeFromBottom (30);
    keepBtn.setBounds (btns.removeFromLeft (btns.getWidth() / 2).reduced (2, 0));
    deleteBtn.setBounds (btns.reduced (2, 0));
    left.removeFromBottom (6);
    optionList.setBounds (left);
    r.removeFromLeft (10);

    // main area
    auto main = r.reduced (10, 8);
    auto gen = main.removeFromTop (30);
    styleLabel.setBounds (gen.removeFromLeft (44));
    styleBox.setBounds (gen.removeFromLeft (190));
    gen.removeFromLeft (14);
    voicesLabel.setBounds (gen.removeFromLeft (52));
    voicesBox.setBounds (gen.removeFromLeft (100));
    gen.removeFromLeft (14);
    gen4.setBounds (gen.removeFromRight (110));
    gen.removeFromRight (8);
    gen1.setBounds (gen);
    main.removeFromTop (12);

    const int laneH = juce::jlimit (54, 110, (main.getHeight() - 40) / 5);
    leadLane->setBounds (main.removeFromTop (laneH));
    main.removeFromTop (6);
    for (auto& ln : voiceLanes)
        if (ln->isVisible())
        {
            ln->setBounds (main.removeFromTop (laneH));
            main.removeFromTop (4);
        }
    addVoice.setBounds (main.removeFromTop (30).removeFromLeft (120));

    // transport + status
    auto tb = bottom.removeFromTop (30);
    play.setBounds (tb.removeFromLeft (80));
    tb.removeFromLeft (6);
    loopBtn.setBounds (tb.removeFromLeft (64));
    tb.removeFromLeft (10);
    timeLabel.setBounds (tb.removeFromLeft (110));
    leadToggle.setBounds (tb.removeFromLeft (110));
    openFolder.setBounds (tb.removeFromRight (110));
    tb.removeFromRight (10);
    progressBar.setBounds (tb.removeFromRight (180).reduced (0, 6));
    status.setBounds (tb);
    bottom.removeFromTop (2);
}
