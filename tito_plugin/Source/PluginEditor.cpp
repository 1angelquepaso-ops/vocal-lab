#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff101014), panel (0xff1a1a21), panel2 (0xff23232c), accent (0xffe0a84a),
                       text (0xffe9e6df), dim (0xff86848c), layCol (0xff6fa8dc), fillCol (0xffe07a9a),
                       chopCol (0xffe0a84a), userCol (0xff7fd1a8), factoryCol (0xff6b6a73);

    // engine style ids (tito/styles.py ORDER) - index 0 auto, 1 from this song
    const char* styleIds[] = { "auto", "song", "trap", "drill", "boombap", "hiphop", "rnb", "reggaeton", "corridos",
                               "banda", "cumbia", "salsa", "bachata", "merengue", "samba", "afrocuban", "dancehall",
                               "reggae", "afrobeats", "afrobeat", "amapiano", "highlife", "rock", "punk", "metal", "funk",
                               "jazz", "gospel", "neworleans", "country", "pop", "house", "techno", "dnb", "jersey",
                               "garage", "middleeastern", "bhangra" };
    const char* styleNames[] = { "Auto (match the song)", "From this song", "Trap", "Drill (UK / NY)", "Boom bap",
                                 "Hip-hop (live)", "R&B / neo-soul", "Reggaeton / dembow", "Corridos tumbados",
                                 "Banda / tambora", "Cumbia", "Salsa (Tito's)", "Bachata", "Merengue", "Samba / bossa",
                                 "Afro-Cuban (bembe / rumba)", "Dancehall", "Reggae (one drop)", "Afrobeats / afro-pop",
                                 "Afrobeat (Fela)", "Amapiano", "Highlife", "Rock", "Punk", "Metal", "Funk",
                                 "Jazz / swing", "Gospel", "New Orleans second line", "Country", "Pop", "House / dance",
                                 "Techno", "Drum & bass / breaks", "Jersey club", "UK garage / 2-step",
                                 "Middle Eastern (maqsum / baladi)", "Bhangra (dhol)" };
    constexpr int numStyles = (int) (sizeof (styleIds) / sizeof (styleIds[0]));
    const char* gridIds[] = { "1/16", "1/32", "1/16T", "1/32T", "1/8T" };
    const char* qgridIds[] = { "1/16", "1/32", "1/16T", "auto" };
    const double fillLens[] = { 1.0, 2.0, 4.0, 8.0 };
    const char* layerIds[] = { "hats", "ghost", "shaker", "perc", "openhat" };
    const char* whereIds[] = { "every8", "every16", "prehook" };

    juce::var makeError (const juce::String& m)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("ok", false);
        o->setProperty ("error", m);
        return juce::var (o);
    }

    juce::String mmss (double s)
    {
        const int t = (int) juce::jmax (0.0, s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }

    void setupLabel (juce::Label& l, bool head = false)
    {
        l.setColour (juce::Label::textColourId, head ? dim : text);
        if (head) l.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    }

    void styleToggle (juce::TextButton& b)
    {
        b.setClickingTogglesState (true);
        b.setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
        b.setColour (juce::TextButton::buttonColourId, panel2);
    }

    juce::Colour sourceColour (const juce::String& s)
    {
        return s == "chop" ? chopCol : s == "factory" ? factoryCol : userCol;
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
    g.setColour (ready ? colour.withAlpha (hover ? 0.32f : 0.2f) : panel2);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (ready ? colour : dim.withAlpha (0.5f));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (title, getLocalBounds().reduced (10, 0), juce::Justification::centredLeft);
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
double SongStrip::lengthBeats() const
{
    const double l = (double) proc.build["length_beats"];
    return l > 0 ? l : 64.0;
}

void SongStrip::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    const auto& b = proc.build;
    if (! b.isObject())
    {
        g.setColour (dim);
        g.setFont (15.0f);
        g.drawText ("Drop a drum stem here (or click Load drum track). TITO PUENTE finds every hit, puts each sound on a pad "
                    "and writes the full song MIDI.", getLocalBounds().reduced (20), juce::Justification::centred, true);
        return;
    }
    auto area = getLocalBounds().reduced (6);
    auto top = area.removeFromTop (18);
    const double len = lengthBeats();
    auto xOf = [&] (double beat) { return (float) area.getX() + (float) (beat / len) * (float) area.getWidth(); };

    // sections
    if (auto* secs = b["sections"].getArray())
        for (auto& s : *secs)
        {
            const float x0 = xOf ((int) s["start"] * 4.0), x1 = xOf ((int) s["end"] * 4.0);
            const auto label = s["label"].toString();
            const bool hook = label == "Hook";
            g.setColour (hook ? accent.withAlpha (0.35f) : panel2);
            g.fillRoundedRectangle (x0 + 1, (float) top.getY(), juce::jmax (2.0f, x1 - x0 - 2), (float) top.getHeight(), 3.0f);
            g.setColour (hook ? text : dim);
            g.setFont (11.0f);
            g.drawText (label, juce::Rectangle<float> (x0 + 4, (float) top.getY(), x1 - x0 - 6, (float) top.getHeight()).toNearestInt(),
                        juce::Justification::centredLeft, true);
        }
    area.removeFromTop (3);
    // fill windows
    if (auto* w = b["windows"].getArray())
        for (auto& win : *w)
        {
            g.setColour (fillCol.withAlpha (0.12f));
            g.fillRect (juce::Rectangle<float> (xOf ((double) win[0]), (float) area.getY(),
                                                xOf ((double) win[1]) - xOf ((double) win[0]), (float) area.getHeight()));
        }
    // bar lines every 4 bars
    for (int bar = 0; bar * 4.0 < len; bar += 4)
    {
        g.setColour (panel2.brighter (0.15f));
        g.drawVerticalLine ((int) xOf (bar * 4.0), (float) area.getY(), (float) area.getBottom());
        if (bar % 8 == 0)
        {
            g.setColour (dim);
            g.setFont (10.0f);
            g.drawText (juce::String (bar + 1), (int) xOf (bar * 4.0) + 2, area.getBottom() - 12, 30, 12, juce::Justification::left);
        }
    }
    // hits: 16 rows, pad 1 at the bottom
    const float rowH = (float) area.getHeight() / 16.0f;
    int layer = 0;
    for (auto* k : { "song", "layers", "fills" })
    {
        const auto col = layer == 0 ? accent : layer == 1 ? layCol : fillCol;
        if (auto* arr = b["notes"][k].getArray())
            for (auto& n : *arr)
            {
                const int pad = (int) n[1];
                const float y = (float) area.getBottom() - (float) (pad + 1) * rowH;
                const float x = xOf ((double) n[0]);
                g.setColour (col.withAlpha (0.35f + 0.65f * (float) (int) n[2] / 127.0f));
                g.fillRect (x, y + 0.5f, 1.6f, juce::jmax (1.0f, rowH - 1.0f));
            }
        ++layer;
    }
    // playhead
    const double ph = proc.playheadBeats();
    if (proc.isPlaying() || ph > 0)
    {
        g.setColour (text);
        g.drawVerticalLine ((int) xOf (ph), (float) getY() - (float) getY() + 2.0f, (float) getHeight() - 2.0f);
    }
}

void SongStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! proc.build.isObject() || onSeek == nullptr) return;
    const auto area = getLocalBounds().reduced (6);
    onSeek (juce::jlimit (0.0, lengthBeats(), (double) (e.x - area.getX()) / (double) area.getWidth() * lengthBeats()));
}

//==============================================================================
void PadButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const auto& m = proc.meta[(size_t) index];
    const bool has = proc.pads.samples[(size_t) index] != nullptr;
    g.setColour (panel2.interpolatedWith (accent, flash * 0.55f));
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (selected ? accent : panel2.brighter (0.25f));
    g.drawRoundedRectangle (r, 7.0f, selected ? 2.0f : 1.0f);
    auto t = r.reduced (8.0f, 6.0f).toNearestInt();
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String (index + 1), t.removeFromTop (14), juce::Justification::left);
    g.setColour (text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (tito::padInfo[index].name, t.removeFromTop (18), juce::Justification::left, true);
    g.setFont (11.0f);
    g.setColour (dim);
    if (m.hitsInSong > 0)
        g.drawText (juce::String (m.hitsInSong) + " hits", t.removeFromTop (14), juce::Justification::left);
    // source dot + lock
    if (has)
    {
        g.setColour (sourceColour (m.source));
        g.fillEllipse (r.getRight() - 16.0f, r.getY() + 9.0f, 7.0f, 7.0f);
    }
    if (m.locked)
    {
        g.setColour (text);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("LOCK", (int) r.getRight() - 42, (int) r.getBottom() - 18, 36, 12, juce::Justification::right);
    }
}

void PadButton::mouseDown (const juce::MouseEvent& e)
{
    if (onPress) onPress (index, juce::jlimit (0.3f, 1.0f, 1.0f - 0.6f * (float) e.y / (float) juce::jmax (1, getHeight())));
}

void PadButton::filesDropped (const juce::StringArray& files, int, int)
{
    if (onDropFile && files.size() > 0) onDropFile (index, juce::File (files[0]));
}

//==============================================================================
void PadWave::paint (juce::Graphics& g)
{
    g.setColour (panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    auto s = proc.pads.samples[(size_t) pad];
    if (s == nullptr) return;
    const auto r = getLocalBounds().toFloat().reduced (6.0f);
    if (cacheFor != s.get())
    {
        cache.clear();
        const auto& au = s->audio;
        const int n = au.getNumSamples();
        const int w = juce::jmax (8, (int) r.getWidth());
        const float mid = r.getCentreY(), h = r.getHeight() * 0.48f;
        for (int x = 0; x < w; ++x)
        {
            const int a = (int) ((juce::int64) x * n / w), b = juce::jmax (a + 1, (int) ((juce::int64) (x + 1) * n / w));
            float mx = 0;
            for (int c = 0; c < au.getNumChannels(); ++c)
                mx = juce::jmax (mx, au.getMagnitude (c, a, b - a));
            cache.addRectangle (r.getX() + (float) x, mid - mx * h, 1.0f, juce::jmax (1.0f, 2 * mx * h));
        }
        cacheFor = s.get();
    }
    const auto p = proc.pads.getParams (pad);
    const float xa = r.getX() + p.start * r.getWidth();
    const float xb = xa + p.length * (r.getRight() - xa);
    g.setColour (dim.withAlpha (0.45f));
    g.fillPath (cache);
    g.saveState();
    g.reduceClipRegion (juce::Rectangle<float> (xa, r.getY(), xb - xa, r.getHeight()).toNearestInt());
    g.setColour (sourceColour (proc.meta[(size_t) pad].source));
    g.fillPath (cache);
    g.restoreState();
    for (float x : { xa, xb })
    {
        g.setColour (text);
        g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
    }
    g.setColour (dim);
    g.setFont (10.0f);
    g.drawText (p.reverse ? "REVERSE" : "", getLocalBounds().reduced (8, 4), juce::Justification::topRight);
    g.drawText (juce::String (s->audio.getNumSamples() / s->rate, 2) + " s  " + s->file.getFileName(),
                getLocalBounds().reduced (8, 4), juce::Justification::bottomLeft, true);
}

void PadWave::mouseDown (const juce::MouseEvent& e)
{
    const auto r = getLocalBounds().toFloat().reduced (6.0f);
    const auto p = proc.pads.getParams (pad);
    const float xa = r.getX() + p.start * r.getWidth();
    const float xb = xa + p.length * (r.getRight() - xa);
    dragging = std::abs ((float) e.x - xa) <= std::abs ((float) e.x - xb) ? 1 : 2;
    mouseDrag (e);
}

void PadWave::mouseDrag (const juce::MouseEvent& e)
{
    const auto r = getLocalBounds().toFloat().reduced (6.0f);
    auto p = proc.pads.getParams (pad);
    const float f = juce::jlimit (0.0f, 1.0f, ((float) e.x - r.getX()) / r.getWidth());
    const float endAbs = p.start + p.length * (1.0f - p.start);
    if (dragging == 1)
    {
        p.start = juce::jmin (f, endAbs - 0.005f);
        p.length = juce::jlimit (0.005f, 1.0f, (endAbs - p.start) / juce::jmax (0.005f, 1.0f - p.start));
    }
    else
        p.length = juce::jlimit (0.005f, 1.0f, (f - p.start) / juce::jmax (0.005f, 1.0f - p.start));
    proc.pads.setParams (pad, p);
    repaint();
    if (onChange) onChange();
}

//==============================================================================
TitoEditor::TitoEditor (TitoProcessor& p)
    : AudioProcessorEditor (&p), proc (p), strip (p), wave (p)
{
    loadingUi = true;
    // ---- top bar
    for (auto* l : { &stemName, &info, &bpmLabel, &sensLabel, &folderLabel, &padTitle, &padSource, &timeLabel, &status, &dragHint,
                     &lStart, &lLength, &lPitch, &lDecay, &lVolume, &lPan, &lChoke, &lOut, &gridLabel, &swingLabel, &humanLabel,
                     &styleLabel, &lenLabel, &intLabel })
    {
        setupLabel (*l);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &grooveHead, &fillHead })
    {
        setupLabel (*l, true);
        addAndMakeVisible (*l);
    }
    info.setColour (juce::Label::textColourId, accent);
    info.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    info.setJustificationType (juce::Justification::centredRight);
    padTitle.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    padSource.setColour (juce::Label::textColourId, dim);
    folderLabel.setColour (juce::Label::textColourId, dim);
    dragHint.setText ("Drag into FL", juce::dontSendNotification);
    dragHint.setColour (juce::Label::textColourId, dim);
    dragHint.setJustificationType (juce::Justification::centred);

    addAndMakeVisible (loadBtn);
    loadBtn.setColour (juce::TextButton::buttonColourId, accent.darker (0.35f));
    loadBtn.onClick = [this] { chooseStem(); };
    addAndMakeVisible (reanalyzeBtn);
    reanalyzeBtn.onClick = [this] { analyze(); };
    for (auto* b : { &x2Btn, &halfBtn })
    {
        addAndMakeVisible (*b);
        b->onClick = [this, b]
        {
            const double cur = proc.analysis.isObject() ? (double) proc.analysis["bpm"] : bpmBox.getText().getDoubleValue();
            if (cur <= 0) return;
            const double nb = b == &x2Btn ? cur * 2.0 : cur / 2.0;
            bpmBox.setText (juce::String (nb, 2), juce::dontSendNotification);
            analyze();
        };
    }
    x2Btn.setTooltip ("Double the tempo (the song was read at half time)");
    halfBtn.setTooltip ("Halve the tempo (the song was read at double time)");
    addAndMakeVisible (bpmBox);
    bpmBox.setJustification (juce::Justification::centred);
    bpmBox.setInputRestrictions (6, "0123456789.");
    bpmBox.setText (proc.bpmOverride > 0 ? juce::String (proc.bpmOverride, 2) : juce::String(), juce::dontSendNotification);
    bpmBox.onReturnKey = [this] { analyze(); };
    addAndMakeVisible (sens);
    sens.setRange (0.0, 1.0, 0.01);
    sens.setValue (proc.sensitivity, juce::dontSendNotification);
    sens.onDragEnd = [this] { proc.sensitivity = sens.getValue(); if (proc.stemFile.existsAsFile()) analyze(); };
    sens.setTooltip ("Higher finds quieter hits (ghost notes); lower ignores bleed and noise");
    addAndMakeVisible (feelBox);
    feelBox.addItem ("Keep timing (real feel)", 1);
    feelBox.addItem ("Quantize", 2);
    feelBox.setSelectedItemIndex (proc.feel, juce::dontSendNotification);
    feelBox.onChange = [this] { proc.feel = feelBox.getSelectedItemIndex(); qgridBox.setEnabled (proc.feel == 1); requestBuild (0); };
    addAndMakeVisible (qgridBox);
    for (int i = 0; i < 4; ++i) qgridBox.addItem (i == 3 ? "Auto 1/16 + triplets" : juce::String ("to ") + qgridIds[i], i + 1);
    qgridBox.setSelectedItemIndex (proc.qgrid, juce::dontSendNotification);
    qgridBox.setEnabled (proc.feel == 1);
    qgridBox.onChange = [this] { proc.qgrid = qgridBox.getSelectedItemIndex(); requestBuild (0); };

    addAndMakeVisible (strip);
    strip.onSeek = [this] (double b) { proc.seekBeats (b); };

    // ---- pads (MPC layout: pad 1 bottom-left)
    for (int i = 0; i < tito::numPads; ++i)
    {
        auto* pb = padButtons.add (new PadButton (proc, i));
        addAndMakeVisible (pb);
        pb->onPress = [this] (int idx, float vel) { selectPad (idx); proc.audition (idx, vel); };
        pb->onDropFile = [this] (int idx, juce::File f)
        {
            if (proc.loadPadFile (idx, f, "user")) { selectPad (idx); proc.status = "Pad " + juce::String (idx + 1) + ": " + f.getFileName(); }
            refresh();
        };
    }
    for (auto* b : { &shuffleKitBtn, &lockBtn, &folderBtn, &saveKitBtn, &loadKitBtn })
        addAndMakeVisible (*b);
    shuffleKitBtn.onClick = [this]
    {
        for (int i = 0; i < tito::numPads; ++i)
            if (! proc.meta[(size_t) i].locked) shufflePad (i);
        refresh();
    };
    shuffleKitBtn.setTooltip ("Random sounds of the right type for every unlocked pad (from your sound folder)");
    lockBtn.onClick = [this]
    {
        auto& m = proc.meta[(size_t) proc.selectedPad];
        m.locked = ! m.locked;
        refresh();
    };
    lockBtn.setTooltip ("A locked pad keeps its sound through Shuffle kit and new songs");
    folderBtn.onClick = [this] { chooseSoundFolder(); };
    saveKitBtn.onClick = [this] { saveKit(); };
    loadKitBtn.onClick = [this] { loadKit(); };

    // ---- pad editor
    addAndMakeVisible (wave);
    wave.onChange = [this] { refreshPadEditor(); };
    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.onValueChange = [this] { if (! loadingUi) pushPadParams(); };
        addAndMakeVisible (s);
    };
    setupSlider (sStart, 0, 100, 0.1, " %");
    setupSlider (sLength, 0.5, 100, 0.1, " %");
    setupSlider (sPitch, -24, 24, 0.1, " st");
    setupSlider (sDecay, 0, 100, 1, " %");
    setupSlider (sVolume, -36, 12, 0.1, " dB");
    setupSlider (sPan, -100, 100, 1, "");
    sDecay.setTooltip ("100% = the sample's own decay; lower cuts it shorter");
    for (auto* b : { &loopBtn, &reverseBtn })
    {
        styleToggle (*b);
        addAndMakeVisible (*b);
        b->onClick = [this] { pushPadParams(); };
    }
    addAndMakeVisible (chokeBox);
    chokeBox.addItem ("Off", 1);
    for (int g = 1; g <= 4; ++g) chokeBox.addItem ("Group " + juce::String (g), g + 1);
    chokeBox.onChange = [this] { if (! loadingUi) pushPadParams(); };
    chokeBox.setTooltip ("Pads in the same group cut each other off (open hat stopped by closed hat)");
    addAndMakeVisible (outBox);
    outBox.addItem ("Main out", 1);
    for (int o = 1; o <= tito::numPads; ++o) outBox.addItem ("Out " + juce::String (o + 1) + "  (pad " + juce::String (o) + ")", o + 1);
    outBox.onChange = [this] { if (! loadingUi) pushPadParams(); };
    outBox.setTooltip ("Send this pad to its own FL mixer output (enable the plugin's extra outputs in FL)");
    for (auto* b : { &loadSoundBtn, &shufflePadBtn, &revertBtn, &factoryBtn })
        addAndMakeVisible (*b);
    loadSoundBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Sound for pad " + juce::String (proc.selectedPad + 1),
                                                       proc.soundFolder, "*.wav;*.aif;*.aiff;*.flac;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  if (fc.getResult().existsAsFile()) proc.loadPadFile (proc.selectedPad, fc.getResult(), "user");
                                  refresh();
                              });
    };
    shufflePadBtn.onClick = [this] { shufflePad (proc.selectedPad); refresh(); };
    revertBtn.onClick = [this]
    {
        const juce::File f (proc.meta[(size_t) proc.selectedPad].chopFile);
        if (f.existsAsFile()) proc.loadPadFile (proc.selectedPad, f, "chop");
        refresh();
    };
    revertBtn.setTooltip ("Back to the hit chopped from the song");
    factoryBtn.onClick = [this]
    {
        const auto f = proc.factoryDir().getChildFile (juce::String (proc.selectedPad + 1).paddedLeft ('0', 2) + "_"
                                                      + tito::padInfo[proc.selectedPad].key + ".wav");
        if (f.existsAsFile()) proc.loadPadFile (proc.selectedPad, f, "factory");
        refresh();
    };

    // ---- groove layers
    addAndMakeVisible (gridBox);
    for (int i = 0; i < 5; ++i) gridBox.addItem (gridIds[i], i + 1);
    gridBox.setSelectedItemIndex (proc.grid, juce::dontSendNotification);
    gridBox.onChange = [this] { proc.grid = gridBox.getSelectedItemIndex(); requestBuild(); };
    gridBox.setTooltip ("Rate of the hat rolls and shaker, locked to the song's BPM");
    for (int i = 0; i < 5; ++i)
    {
        styleToggle (layBtn[i]);
        addAndMakeVisible (layBtn[i]);
        layBtn[i].setToggleState (proc.lay[i], juce::dontSendNotification);
        layBtn[i].onClick = [this, i] { proc.lay[i] = layBtn[i].getToggleState(); requestBuild (0); };
    }
    for (auto* s : { &swingS, &humanS, &intS })
    {
        s->setRange (0, 100, 1);
        s->setTextValueSuffix (" %");
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20);
        addAndMakeVisible (*s);
    }
    swingS.setValue (proc.swing * 100, juce::dontSendNotification);
    humanS.setValue (proc.human * 100, juce::dontSendNotification);
    intS.setValue (proc.intensity * 100, juce::dontSendNotification);
    swingS.onValueChange = [this] { proc.swing = swingS.getValue() / 100.0; requestBuild(); };
    humanS.onValueChange = [this] { proc.human = humanS.getValue() / 100.0; requestBuild(); };
    intS.onValueChange = [this] { proc.intensity = intS.getValue() / 100.0; requestBuild(); };
    humanS.setTooltip ("Real drummer timing + dynamics (learned from 13 hours of real drummers) on the added layers and fills");
    swingS.setTooltip ("Pushes the off 16ths late on the added layers and fills (100% = triplet feel)");
    addAndMakeVisible (styleBox);
    for (int i = 0; i < numStyles; ++i) styleBox.addItem (styleNames[i], i + 1);
    styleBox.setSelectedItemIndex (proc.style, juce::dontSendNotification);
    styleBox.onChange = [this] { proc.style = styleBox.getSelectedItemIndex(); requestBuild (0); };

    // ---- fills
    for (int i = 0; i < 3; ++i)
    {
        styleToggle (whereBtn[i]);
        addAndMakeVisible (whereBtn[i]);
        whereBtn[i].setToggleState (proc.fillWhere[i], juce::dontSendNotification);
        whereBtn[i].onClick = [this, i] { proc.fillWhere[i] = whereBtn[i].getToggleState(); requestBuild (0); };
    }
    addAndMakeVisible (lenBox);
    for (auto* n : { "1 beat", "2 beats", "1 bar", "2 bars" }) lenBox.addItem (n, lenBox.getNumItems() + 1);
    lenBox.setSelectedItemIndex (proc.fillLen, juce::dontSendNotification);
    lenBox.onChange = [this] { proc.fillLen = lenBox.getSelectedItemIndex(); requestBuild (0); };
    for (auto* b : { &crashBtn, &roomBtn })
    {
        styleToggle (*b);
        addAndMakeVisible (*b);
    }
    crashBtn.setToggleState (proc.crash, juce::dontSendNotification);
    roomBtn.setToggleState (proc.room, juce::dontSendNotification);
    crashBtn.onClick = [this] { proc.crash = crashBtn.getToggleState(); requestBuild (0); };
    roomBtn.onClick = [this] { proc.room = roomBtn.getToggleState(); requestBuild (0); };
    roomBtn.setTooltip ("Clears the song's groove under each fill, like a drummer stopping the beat to play it");
    addAndMakeVisible (newFillsBtn);
    newFillsBtn.onClick = [this] { proc.seed = rng.nextInt (1 << 30); requestBuild (0); };

    // ---- transport + exports
    addAndMakeVisible (playBtn);
    playBtn.onClick = [this]
    {
        if (! proc.build.isObject()) return;
        proc.play (! proc.isPlaying());
        refresh();
    };
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible (hearBtn[i]);
        hearBtn[i].setToggleState (proc.hear[i].load(), juce::dontSendNotification);
        hearBtn[i].onClick = [this, i]
        {
            proc.hear[i] = hearBtn[i].getToggleState();
            if (i == 3 && proc.hear[3] && ! proc.hasOriginal()) loadOriginal();
        };
    }
    hearBtn[3].setTooltip ("A/B against the original drum stem");
    for (auto* t : { &songTile, &layersTile, &fillsTile, &allTile })
        addAndMakeVisible (*t);
    addAndMakeVisible (dragHint);
    addAndMakeVisible (openFolderBtn);
    openFolderBtn.onClick = [this] { exportDir().startAsProcess(); };
    addAndMakeVisible (progressBar);
    progressBar.setColour (juce::ProgressBar::foregroundColourId, accent);
    progressBar.setVisible (false);

    // sound folder: remembered for every instance
    if (! proc.soundFolder.isDirectory())
    {
        const auto cfg = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TITO PUENTE/sound_folder.txt");
        if (cfg.existsAsFile()) proc.soundFolder = juce::File (cfg.loadFileAsString().trim());
    }
    if (proc.soundFolder.isDirectory())
        library.scan (proc.soundFolder, [safe = juce::Component::SafePointer<TitoEditor> (this)] { if (safe) safe->refresh(); });

    setResizable (true, true);
    setResizeLimits (1050, 760, 2200, 1500);
    loadingUi = false;
    setSize (1220, 840);       // last: every child exists before the first layout
    selectPad (proc.selectedPad);
    applyBuild();
    refresh();
    startTimerHz (30);

    const auto dev = juce::SystemStats::getEnvironmentVariable ("TITO_DEV_STEM", {});
    if (dev.isNotEmpty() && ! proc.stemFile.existsAsFile() && juce::File (dev).existsAsFile())
    {
        proc.stemFile = juce::File (dev);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TitoEditor> (this)] { if (safe) safe->analyze(); });
    }
}

TitoEditor::~TitoEditor()
{
    stopTimer();
    job.reset();
}

//==============================================================================
juce::File TitoEditor::engineExe() const
{
    auto env = juce::SystemStats::getEnvironmentVariable ("TITO_ENGINE", {});
    if (env.isNotEmpty() && juce::File (env).existsAsFile()) return juce::File (env);
   #if JUCE_WINDOWS
    const juce::String exeName ("tito-engine.exe");
   #else
    const juce::String exeName ("tito-engine");
   #endif
    const auto me = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (auto dir : { me.getParentDirectory().getParentDirectory().getChildFile ("Resources/engine"),
                      me.getParentDirectory().getChildFile ("engine") })
        if (dir.getChildFile (exeName).existsAsFile()) return dir.getChildFile (exeName);
    return {};
}

juce::File TitoEditor::exportDir() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TITO PUENTE")
                 .getChildFile (proc.stemFile.existsAsFile() ? proc.stemFile.getFileNameWithoutExtension() : "untitled");
    d.createDirectory();
    return d;
}

void TitoEditor::runEngine (juce::StringArray args, std::function<void (juce::var)> onOk)
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
    juce::Component::SafePointer<TitoEditor> safe (this);
    job = std::make_unique<EngineJob> (args, [safe, onOk] (juce::var r)
    {
        if (safe == nullptr) return;
        safe->busy = false;
        if (r["ok"]) onOk (r);
        else safe->proc.status = "Problem: " + r["error"].toString();
        safe->refresh();
        if (safe->pendingBuild) { safe->pendingBuild = false; safe->buildNow(); }
    });
    job->startThread();
}

void TitoEditor::chooseStem()
{
    chooser = std::make_unique<juce::FileChooser> ("Drum stem / drum track", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResult().existsAsFile()) return;
                              proc.stemFile = fc.getResult();
                              bpmBox.clear();
                              analyze();
                          });
}

bool TitoEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
    return false;
}

void TitoEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    // dropped on a pad -> that pad's sound; anywhere else -> new drum track
    for (auto* pb : padButtons)
        if (pb->getBounds().contains (x, y)) { pb->filesDropped (files, x, y); return; }
    proc.stemFile = juce::File (files[0]);
    bpmBox.clear();
    analyze();
}

void TitoEditor::analyze()
{
    if (busy || ! proc.stemFile.existsAsFile()) return;
    proc.play (false);
    proc.sessionDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("TITO PUENTE")
                          .getChildFile ("s" + juce::String (juce::Time::currentTimeMillis()));
    proc.sessionDir.createDirectory();
    proc.bpmOverride = bpmBox.getText().getDoubleValue();
    juce::StringArray args { "analyze", "--session", proc.sessionDir.getFullPathName(), "--stem", proc.stemFile.getFullPathName(),
                             "--out", exportDir().getFullPathName(), "--sensitivity", juce::String (proc.sensitivity, 2) };
    if (proc.bpmOverride >= 40 && proc.bpmOverride <= 300)
        args.addArray ({ "--bpm", juce::String (proc.bpmOverride, 2) });
    proc.status = "Finding every hit...";
    proc.setOriginal ({}, 44100.0);
    runEngine (args, [this] (juce::var r)
    {
        applyAnalysis (r);
        proc.status = "Found " + juce::String ((int) r["hits"]) + " hits, " + juce::String ((int) r["sounds"]) + " sounds in "
                      + juce::String ((double) r["seconds"], 1) + " s.";
        pendingBuild = true;
    });
}

void TitoEditor::applyAnalysis (const juce::var& r)
{
    proc.analysis = r;
    if (proc.bpmOverride <= 0)
        bpmBox.setText (juce::String ((double) r["bpm"], 2), juce::dontSendNotification);
    std::array<bool, tito::numPads> got {};
    for (auto& m : proc.meta) m.hitsInSong = 0;
    if (auto* pads = r["pads"].getArray())
        for (auto& p : *pads)
        {
            const int i = (int) p["pad"];
            if (i < 0 || i >= tito::numPads) continue;
            got[(size_t) i] = true;
            auto& m = proc.meta[(size_t) i];
            m.hitsInSong = (int) p["hits"];
            m.chopFile = p["file"].toString();
            if (! m.locked)
            {
                proc.loadPadFile (i, juce::File (m.chopFile), "chop");
                proc.pads.setParams (i, tito::PadParams {});
            }
        }
    // pads the song doesn't use go back to the factory sound (unless locked or your own)
    for (int i = 0; i < tito::numPads; ++i)
    {
        auto& m = proc.meta[(size_t) i];
        if (! got[(size_t) i]) m.chopFile = {};
        if (! got[(size_t) i] && ! m.locked && m.source == "chop")
        {
            const auto f = proc.factoryDir().getChildFile (juce::String (i + 1).paddedLeft ('0', 2) + "_" + tito::padInfo[i].key + ".wav");
            if (f.existsAsFile()) proc.loadPadFile (i, f, "factory");
        }
    }
    // open hat choked by closed / pedal hat by default
    for (int i : { 5, 6, 7 })
    {
        auto pp = proc.pads.getParams (i);
        if (pp.choke == 0) { pp.choke = 1; proc.pads.setParams (i, pp); }
    }
    selectPad (proc.selectedPad);
}

void TitoEditor::requestBuild (int delayMs)
{
    if (loadingUi || ! proc.analysis.isObject()) return;
    buildDue = juce::Time::currentTimeMillis() + delayMs;
}

void TitoEditor::buildNow()
{
    if (! proc.analysis.isObject()) return;
    if (busy) { pendingBuild = true; return; }
    juce::StringArray lays, where;
    for (int i = 0; i < 5; ++i) if (proc.lay[i]) lays.add (layerIds[i]);
    for (int i = 0; i < 3; ++i) if (proc.fillWhere[i]) where.add (whereIds[i]);
    juce::StringArray args { "build", "--session", proc.sessionDir.getFullPathName(), "--out", exportDir().getFullPathName(),
                             "--feel", proc.feel == 1 ? "quantize" : "keep", "--qgrid", qgridIds[juce::jlimit (0, 3, proc.qgrid)],
                             "--style", styleIds[juce::jlimit (0, numStyles - 1, proc.style)],
                             "--layers=" + lays.joinIntoString (","), "--grid", gridIds[juce::jlimit (0, 4, proc.grid)],
                             "--swing", juce::String (proc.swing, 2), "--human", juce::String (proc.human, 2),
                             "--fills=" + where.joinIntoString (","), "--fill-len", juce::String (fillLens[juce::jlimit (0, 3, proc.fillLen)]),
                             "--intensity", juce::String (proc.intensity, 2), "--crash", proc.crash ? "1" : "0",
                             "--room", proc.room ? "1" : "0", "--seed", juce::String (proc.seed) };
    proc.status = "Writing MIDI...";
    runEngine (args, [this] (juce::var r)
    {
        proc.build = r;
        applyBuild();
        proc.status = "Ready. Drag FULL SONG / GROOVE LAYERS / FILLS / ALL into FL (channel = this plugin).";
    });
}

void TitoEditor::applyBuild()
{
    const auto& b = proc.build;
    const auto files = b["files"];
    auto fileOf = [&] (const char* k) { return b.isObject() && files[k].isString() ? juce::File (files[k].toString()) : juce::File(); };
    songTile.setFile (fileOf ("song"));
    layersTile.setFile (fileOf ("layers"));
    fillsTile.setFile (fileOf ("fills"));
    allTile.setFile (fileOf ("all"));
    if (b.isObject())
    {
        std::vector<TitoProcessor::Event> ev;
        int layer = 0;
        for (auto* k : { "song", "layers", "fills" })
        {
            if (auto* arr = b["notes"][k].getArray())
                for (auto& n : *arr)
                    ev.push_back ({ (double) n[0], (int) n[1], (float) (int) n[2] / 127.0f, layer });
            ++layer;
        }
        proc.setSequence (std::move (ev), (double) b["bpm"], (double) b["length_beats"], (double) b["t0"]);
        if (proc.style == 0 && b["style"].isString())
            for (int i = 2; i < numStyles; ++i)
                if (b["style"].toString() == styleIds[i])
                    styleBox.setText ("Auto: " + juce::String (styleNames[i]), juce::dontSendNotification);
    }
    strip.repaint();
}

void TitoEditor::loadOriginal()
{
    const auto f = proc.stemFile;
    if (! f.existsAsFile()) return;
    juce::Component::SafePointer<TitoEditor> safe (this);
    juce::Thread::launch ([safe, f, this]
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) return;
        auto buf = std::make_shared<juce::AudioBuffer<float>> (2, (int) rd->lengthInSamples);
        rd->read (buf.get(), 0, (int) rd->lengthInSamples, 0, true, true);
        const double rate = rd->sampleRate;
        juce::MessageManager::callAsync ([safe, buf, rate]
        {
            if (safe == nullptr) return;
            safe->proc.setOriginal (std::move (*buf), rate);
        });
    });
}

//==============================================================================
void TitoEditor::selectPad (int i)
{
    proc.selectedPad = juce::jlimit (0, tito::numPads - 1, i);
    for (int k = 0; k < tito::numPads; ++k)
    {
        padButtons[k]->selected = k == proc.selectedPad;
        padButtons[k]->repaint();
    }
    wave.setPad (proc.selectedPad);
    refreshPadEditor();
}

void TitoEditor::refreshPadEditor()
{
    loadingUi = true;
    const int i = proc.selectedPad;
    const auto p = proc.pads.getParams (i);
    const auto& m = proc.meta[(size_t) i];
    padTitle.setText ("Pad " + juce::String (i + 1) + "  " + tito::padInfo[i].name, juce::dontSendNotification);
    const auto f = proc.padFile (i);
    padSource.setText ((m.source == "chop" ? "Chopped from the song" : m.source == "factory" ? "Factory sound" : "Your sound")
                       + juce::String (m.locked ? "  (locked)" : "") + (f.existsAsFile() ? "  -  " + f.getFileName() : juce::String()),
                       juce::dontSendNotification);
    sStart.setValue (p.start * 100.0, juce::dontSendNotification);
    sLength.setValue (p.length * 100.0, juce::dontSendNotification);
    sPitch.setValue (p.pitch, juce::dontSendNotification);
    sDecay.setValue (p.decay * 100.0, juce::dontSendNotification);
    sVolume.setValue (p.volume, juce::dontSendNotification);
    sPan.setValue (p.pan * 100.0, juce::dontSendNotification);
    loopBtn.setToggleState (p.loop, juce::dontSendNotification);
    reverseBtn.setToggleState (p.reverse, juce::dontSendNotification);
    chokeBox.setSelectedItemIndex (p.choke, juce::dontSendNotification);
    outBox.setSelectedItemIndex (p.out, juce::dontSendNotification);
    lockBtn.setButtonText (m.locked ? "Unlock pad" : "Lock pad");
    revertBtn.setEnabled (juce::File (m.chopFile).existsAsFile());
    wave.repaint();
    loadingUi = false;
}

void TitoEditor::pushPadParams()
{
    tito::PadParams p;
    p.start = (float) sStart.getValue() / 100.0f;
    p.length = (float) sLength.getValue() / 100.0f;
    p.pitch = (float) sPitch.getValue();
    p.decay = (float) sDecay.getValue() / 100.0f;
    p.volume = (float) sVolume.getValue();
    p.pan = (float) sPan.getValue() / 100.0f;
    p.loop = loopBtn.getToggleState();
    p.reverse = reverseBtn.getToggleState();
    p.choke = juce::jmax (0, chokeBox.getSelectedItemIndex());
    p.out = juce::jmax (0, outBox.getSelectedItemIndex());
    proc.pads.setParams (proc.selectedPad, p);
    wave.repaint();
}

void TitoEditor::shufflePad (int i)
{
    if (library.count() == 0)
    {
        proc.status = library.isScanning() ? "Still reading your sound folder..." : "Pick your sound folder first (Sound folder...).";
        return;
    }
    const auto f = library.pick (tito::padInfo[i].type, rng);
    if (f.existsAsFile() && proc.loadPadFile (i, f, "user"))
        proc.pads.setParams (i, tito::PadParams {});
    if (i == proc.selectedPad) refreshPadEditor();
}

void TitoEditor::chooseSoundFolder()
{
    chooser = std::make_unique<juce::FileChooser> ("Your drum sounds folder", proc.soundFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto d = fc.getResult();
                              if (! d.isDirectory()) return;
                              proc.soundFolder = d;
                              const auto cfg = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("TITO PUENTE/sound_folder.txt");
                              cfg.getParentDirectory().createDirectory();
                              cfg.replaceWithText (d.getFullPathName());
                              proc.status = "Reading your sound folder (first time only)...";
                              library.scan (d, [safe = juce::Component::SafePointer<TitoEditor> (this)]
                              {
                                  if (safe == nullptr) return;
                                  safe->proc.status = "Sound folder ready: " + juce::String (safe->library.count()) + " drum sounds sorted by type.";
                                  safe->refresh();
                              });
                              refresh();
                          });
}

void TitoEditor::saveKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Save kit", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                   .getChildFile ("TITO PUENTE/kits"), "*.titokit");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("titokit");
                              f.getParentDirectory().createDirectory();
                              juce::Array<juce::var> arr;
                              for (int i = 0; i < tito::numPads; ++i)
                              {
                                  auto* o = new juce::DynamicObject();
                                  const auto p = proc.pads.getParams (i);
                                  o->setProperty ("file", proc.padFile (i).getFullPathName());
                                  o->setProperty ("start", p.start); o->setProperty ("length", p.length);
                                  o->setProperty ("pitch", p.pitch); o->setProperty ("decay", p.decay);
                                  o->setProperty ("volume", p.volume); o->setProperty ("pan", p.pan);
                                  o->setProperty ("loop", p.loop); o->setProperty ("reverse", p.reverse);
                                  o->setProperty ("choke", p.choke); o->setProperty ("out", p.out);
                                  arr.add (juce::var (o));
                              }
                              f.replaceWithText (juce::JSON::toString (juce::var (arr)));
                              proc.status = "Kit saved: " + f.getFileName();
                              refresh();
                          });
}

void TitoEditor::loadKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Load kit", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                   .getChildFile ("TITO PUENTE/kits"), "*.titokit");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto v = juce::JSON::parse (fc.getResult());
                              auto* arr = v.getArray();
                              if (arr == nullptr) return;
                              for (int i = 0; i < juce::jmin (tito::numPads, arr->size()); ++i)
                              {
                                  const auto& o = arr->getReference (i);
                                  if (proc.meta[(size_t) i].locked) continue;
                                  const juce::File f (o["file"].toString());
                                  if (f.existsAsFile()) proc.loadPadFile (i, f, "user");
                                  tito::PadParams p;
                                  p.start = o["start"]; p.length = o["length"]; p.pitch = o["pitch"]; p.decay = o["decay"];
                                  p.volume = o["volume"]; p.pan = o["pan"]; p.loop = o["loop"]; p.reverse = o["reverse"];
                                  p.choke = o["choke"]; p.out = o["out"];
                                  proc.pads.setParams (i, p);
                              }
                              proc.status = "Kit loaded: " + fc.getResult().getFileName();
                              selectPad (proc.selectedPad);
                              refresh();
                          });
}

//==============================================================================
void TitoEditor::refresh()
{
    const auto& a = proc.analysis;
    stemName.setText (proc.stemFile.existsAsFile() ? proc.stemFile.getFileName() : "No drum track loaded", juce::dontSendNotification);
    if (a.isObject())
        info.setText (juce::String ((double) a["bpm"], 1) + " BPM   " + juce::String ((int) a["bars"]) + " bars   "
                      + juce::String ((int) a["hits"]) + " hits" + ((bool) a["steady"] ? juce::String() : juce::String ("   live tempo")),
                      juce::dontSendNotification);
    else
        info.setText ({}, juce::dontSendNotification);
    status.setText (proc.status, juce::dontSendNotification);
    playBtn.setButtonText (proc.isPlaying() ? "Stop" : "Play");
    playBtn.setEnabled (proc.build.isObject());
    for (auto* b : { &loadBtn, &reanalyzeBtn, &x2Btn, &halfBtn }) b->setEnabled (! busy);
    reanalyzeBtn.setEnabled (! busy && proc.stemFile.existsAsFile());
    progressBar.setVisible (busy);
    folderLabel.setText (proc.soundFolder.isDirectory()
                             ? (library.isScanning() ? "Reading " : "") + proc.soundFolder.getFileName()
                                   + (library.count() > 0 ? "  (" + juce::String (library.count()) + " sounds)" : juce::String())
                             : "No sound folder yet", juce::dontSendNotification);
    refreshPadEditor();
    for (auto* pb : padButtons) pb->repaint();
    strip.repaint();
}

void TitoEditor::timerCallback()
{
    if (busy)
    {
        const auto t = proc.sessionDir.getChildFile ("progress.txt").loadFileAsString();
        if (t.contains ("|"))
        {
            progress = t.upToFirstOccurrenceOf ("|", false, false).getDoubleValue();
            status.setText (t.fromFirstOccurrenceOf ("|", false, false), juce::dontSendNotification);
        }
        else progress = -1.0;
    }
    if (buildDue > 0 && juce::Time::currentTimeMillis() >= buildDue)
    {
        buildDue = 0;
        buildNow();
    }
    for (int i = 0; i < tito::numPads; ++i)
    {
        const int h = proc.pads.hits[(size_t) i].load();
        auto* pb = padButtons[i];
        if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; pb->flash = 1.0f; pb->repaint(); }
        else if (pb->flash > 0.0f) { pb->flash = juce::jmax (0.0f, pb->flash - 0.12f); pb->repaint(); }
    }
    if (proc.isPlaying() || playBtn.getButtonText() == "Stop")
    {
        strip.repaint();
        const double bpm = proc.build.isObject() ? (double) proc.build["bpm"] : 120.0;
        timeLabel.setText (mmss (proc.playheadBeats() * 60.0 / bpm) + "   bar " + juce::String ((int) (proc.playheadBeats() / 4) + 1),
                           juce::dontSendNotification);
        if (! proc.isPlaying()) playBtn.setButtonText ("Play");
    }
}

//==============================================================================
void TitoEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    g.setColour (accent);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("TITO PUENTE", 16, 10, 190, 30, juce::Justification::centredLeft);
    // panels
    g.setColour (panel);
    for (auto r : { juce::Rectangle<int> (12, 200, 452, 410), juce::Rectangle<int> (474, 200, getWidth() - 486, 410) })
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
    const int by = getHeight() - 226;
    g.fillRoundedRectangle (juce::Rectangle<int> (12, by, getWidth() / 2 - 18, 128).toFloat(), 8.0f);
    g.fillRoundedRectangle (juce::Rectangle<int> (getWidth() / 2 + 6, by, getWidth() / 2 - 18, 128).toFloat(), 8.0f);
    // legend for pad colours
    g.setFont (11.0f);
    int x = 22;
    for (auto [c, t] : { std::pair<juce::Colour, const char*> { chopCol, "from the song" }, { userCol, "your sound" }, { factoryCol, "factory" } })
    {
        g.setColour (c);
        g.fillEllipse ((float) x, 590.0f, 7.0f, 7.0f);
        g.setColour (dim);
        g.drawText (t, x + 11, 584, 90, 18, juce::Justification::centredLeft);
        x += 104;
    }
}

void TitoEditor::resized()
{
    if (padButtons.size() < tito::numPads) return;
    const int W = getWidth(), H = getHeight();
    // top bar
    auto top = juce::Rectangle<int> (210, 10, W - 222, 30);
    loadBtn.setBounds (top.removeFromLeft (140));
    top.removeFromLeft (8);
    auto right = top;
    qgridBox.setBounds (right.removeFromRight (150).reduced (0, 2));
    right.removeFromRight (6);
    feelBox.setBounds (right.removeFromRight (180).reduced (0, 2));
    right.removeFromRight (10);
    reanalyzeBtn.setBounds (right.removeFromRight (90));
    right.removeFromRight (6);
    halfBtn.setBounds (right.removeFromRight (34));
    x2Btn.setBounds (right.removeFromRight (34));
    right.removeFromRight (4);
    bpmBox.setBounds (right.removeFromRight (64).reduced (0, 2));
    bpmLabel.setBounds (right.removeFromRight (36));
    stemName.setBounds (right);
    auto row2 = juce::Rectangle<int> (210, 42, W - 222, 22);
    sensLabel.setBounds (row2.removeFromLeft (80));
    sens.setBounds (row2.removeFromLeft (160));
    info.setBounds (row2.removeFromRight (300));
    status.setBounds (row2.reduced (10, 0));

    strip.setBounds (12, 70, W - 24, 122);

    // pads 4x4, pad 1 bottom-left
    const int gx = 22, gy = 210, cell = 98;
    for (int i = 0; i < tito::numPads; ++i)
    {
        const int col = i % 4, row = 3 - i / 4;
        padButtons[i]->setBounds (gx + col * (cell + 8), gy + row * (cell - 18), cell + 6, cell - 22);
    }
    auto pr = juce::Rectangle<int> (22, gy + 4 * (cell - 18) + 6, 434, 26);
    shuffleKitBtn.setBounds (pr.removeFromLeft (100)); pr.removeFromLeft (6);
    lockBtn.setBounds (pr.removeFromLeft (84)); pr.removeFromLeft (6);
    saveKitBtn.setBounds (pr.removeFromLeft (72)); pr.removeFromLeft (6);
    loadKitBtn.setBounds (pr.removeFromLeft (72)); pr.removeFromLeft (6);
    folderBtn.setBounds (pr);
    folderLabel.setBounds (22, gy + 4 * (cell - 18) + 34, 434, 18);

    // pad editor
    auto ed = juce::Rectangle<int> (486, 208, W - 510, 394);
    auto head = ed.removeFromTop (24);
    padTitle.setBounds (head.removeFromLeft (260));
    padSource.setBounds (head);
    wave.setBounds (ed.removeFromTop (110).reduced (0, 4));
    ed.removeFromTop (6);
    auto colL = ed.removeFromLeft (ed.getWidth() / 2 - 8);
    ed.removeFromLeft (16);
    auto colR = ed;
    auto row = [] (juce::Rectangle<int>& col, juce::Label& l, juce::Component& c)
    {
        auto r = col.removeFromTop (30);
        l.setBounds (r.removeFromLeft (64));
        c.setBounds (r.reduced (0, 3));
    };
    row (colL, lStart, sStart); row (colL, lLength, sLength); row (colL, lPitch, sPitch);
    row (colR, lDecay, sDecay); row (colR, lVolume, sVolume); row (colR, lPan, sPan);
    row (colL, lChoke, chokeBox); row (colR, lOut, outBox);
    auto bl = colL.removeFromTop (34).reduced (0, 4);
    loopBtn.setBounds (bl.removeFromLeft (bl.getWidth() / 2 - 3)); bl.removeFromLeft (6); reverseBtn.setBounds (bl);
    auto br = colR.removeFromTop (34).reduced (0, 4);
    loadSoundBtn.setBounds (br.removeFromLeft (br.getWidth() / 2 - 3)); br.removeFromLeft (6); shufflePadBtn.setBounds (br);
    auto bl2 = colL.removeFromTop (34).reduced (0, 4);
    revertBtn.setBounds (bl2.removeFromLeft (bl2.getWidth() / 2 - 3)); bl2.removeFromLeft (6); factoryBtn.setBounds (bl2);

    // groove layers (bottom left) + fills (bottom right)
    const int by = H - 226;
    auto gl = juce::Rectangle<int> (22, by + 6, W / 2 - 38, 116);
    auto gh = gl.removeFromTop (22);
    grooveHead.setBounds (gh.removeFromLeft (130));
    styleBox.setBounds (gh.removeFromRight (210));
    styleLabel.setBounds (gh.removeFromRight (44));
    gl.removeFromTop (4);
    auto lb = gl.removeFromTop (28);
    const int bw = (lb.getWidth() - 4 * 6) / 5;
    for (int i = 0; i < 5; ++i) { layBtn[i].setBounds (lb.removeFromLeft (bw)); lb.removeFromLeft (6); }
    gl.removeFromTop (6);
    auto g2 = gl.removeFromTop (26);
    gridLabel.setBounds (g2.removeFromLeft (36));
    gridBox.setBounds (g2.removeFromLeft (80).reduced (0, 1));
    g2.removeFromLeft (10);
    swingLabel.setBounds (g2.removeFromLeft (46));
    swingS.setBounds (g2.reduced (0, 2));
    auto g3 = gl.removeFromTop (26);
    humanLabel.setBounds (g3.removeFromLeft (92));
    humanS.setBounds (g3.reduced (0, 2));

    auto fr = juce::Rectangle<int> (W / 2 + 16, by + 6, W / 2 - 38, 116);
    auto fh = fr.removeFromTop (22);
    fillHead.setBounds (fh.removeFromLeft (130));
    newFillsBtn.setBounds (fh.removeFromRight (150));
    fr.removeFromTop (4);
    auto wb = fr.removeFromTop (28);
    const int ww = (wb.getWidth() - 2 * 6) / 3;
    for (int i = 0; i < 3; ++i) { whereBtn[i].setBounds (wb.removeFromLeft (ww)); wb.removeFromLeft (6); }
    fr.removeFromTop (6);
    auto f2 = fr.removeFromTop (26);
    lenLabel.setBounds (f2.removeFromLeft (50));
    lenBox.setBounds (f2.removeFromLeft (90).reduced (0, 1));
    f2.removeFromLeft (10);
    crashBtn.setBounds (f2.removeFromLeft (100).reduced (0, 1));
    f2.removeFromLeft (6);
    roomBtn.setBounds (f2.removeFromLeft (100).reduced (0, 1));
    auto f3 = fr.removeFromTop (26);
    intLabel.setBounds (f3.removeFromLeft (64));
    intS.setBounds (f3.reduced (0, 2));

    // transport
    auto tr = juce::Rectangle<int> (12, H - 90, W - 24, 32);
    playBtn.setBounds (tr.removeFromLeft (90));
    tr.removeFromLeft (10);
    for (int i = 0; i < 4; ++i) hearBtn[i].setBounds (tr.removeFromLeft (i == 3 ? 86 : 72));
    tr.removeFromLeft (10);
    timeLabel.setBounds (tr.removeFromLeft (140));
    progressBar.setBounds (tr.removeFromRight (260).reduced (0, 6));

    // exports
    auto ex = juce::Rectangle<int> (12, H - 52, W - 24, 40);
    openFolderBtn.setBounds (ex.removeFromRight (110).reduced (0, 4));
    ex.removeFromRight (8);
    dragHint.setBounds (ex.removeFromLeft (90));
    const int tw = (ex.getWidth() - 3 * 8) / 4;
    for (auto* t : { &songTile, &layersTile, &fillsTile, &allTile }) { t->setBounds (ex.removeFromLeft (tw)); ex.removeFromLeft (8); }
}
