#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0d0e12), panel (0xff17191f), panel2 (0xff20232b), ink (0xffecebe8),
                       dim (0xff858892), micCol (0xffd9b36c), preCol (0xff8fc9de), dynCol (0xffc49ad8);
    const char* knobIds[] = { "distance", "hpf", "air", "warmth", "comp", "deess", "input", "output" };
    const char* knobNames[] = { "Distance", "High-pass", "Air", "Warmth", "Comp", "De-ess", "Input", "Output" };

    // display order (tables are append-only, the editor groups them)
    std::vector<int> micsInGroup (int g) { std::vector<int> v; for (int i = 0; i < dm::NUM_MICS; ++i) if (dm::MICS[i].group == g) v.push_back (i); return v; }
    std::vector<int> presInGroup (int g) { std::vector<int> v; for (int i = 0; i < dm::NUM_PRES; ++i) if (dm::PRES[i].group == g) v.push_back (i); return v; }
}

void GearButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.35f;
    g.setColour ((on ? col.withAlpha (0.18f) : (over ? panel2.brighter (0.06f) : panel2)).withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour ((on ? col : panel2.brighter (0.2f)).withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (r, 6.0f, on ? 1.6f : 1.0f);
    auto t = getLocalBounds().reduced (8, 3);
    if (sub.isEmpty())
    {
        g.setColour ((on ? col : ink).withMultipliedAlpha (alpha));
        g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        g.drawFittedText (title, t, juce::Justification::centred, 1);
        return;
    }
    g.setColour (on ? col : ink);
    g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    g.drawText (title, t.removeFromTop (t.getHeight() / 2 + 1), juce::Justification::bottomLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (10.5f));
    g.drawText (sub, t, juce::Justification::topLeft);
}

void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    auto a = r.reduced (34, 16);
    const double lo = std::log10 (20.0), hi = std::log10 (20000.0);
    auto xOf = [&] (double f) { return a.getX() + (float) ((std::log10 (f) - lo) / (hi - lo)) * a.getWidth(); };
    auto yOf = [&] (double db) { return a.getCentreY() - (float) (db / 18.0) * a.getHeight() * 0.5f; };
    g.setFont (10.0f);
    for (double db : { -12.0, -6.0, 0.0, 6.0, 12.0 })
    {
        g.setColour (db == 0 ? dim.withAlpha (0.6f) : panel2.brighter (0.15f));
        g.drawHorizontalLine ((int) yOf (db), a.getX(), a.getRight());
        g.setColour (dim);
        g.drawText (juce::String ((int) db), (int) r.getX() + 2, (int) yOf (db) - 6, 28, 12, juce::Justification::centredRight);
    }
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (panel2.brighter (0.15f));
        g.drawVerticalLine ((int) xOf (f), a.getY(), a.getBottom());
        g.setColour (dim);
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f),
                    (int) xOf (f) - 15, (int) a.getBottom(), 30, 12, juce::Justification::centred);
    }
    dm::Engine e;
    e.p = proc.currentParams();
    juce::Path path;
    for (int i = 0; i <= 200; ++i)
    {
        const double f = std::pow (10.0, lo + (hi - lo) * i / 200.0);
        const float y = juce::jlimit (a.getY(), a.getBottom(), yOf (e.curveDb (f)));
        if (i == 0) path.startNewSubPath (xOf (f), y); else path.lineTo (xOf (f), y);
    }
    g.setColour (micCol);
    g.strokePath (path, juce::PathStrokeType (2.0f));
    g.setColour (dim);
    g.setFont (11.0f);
    g.drawText ("Tone change on your recording: your mic undone + this mic, switches, distance, high-pass, preamp, air",
                r.reduced (10, 3).removeFromTop (14).toNearestInt(), juce::Justification::centredRight);
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = r.withTrimmedBottom (16).reduced (4, 0);
    g.setColour (panel2);
    g.fillRoundedRectangle (bar, 3.0f);
    if (grMode)
    {
        const float frac = juce::jlimit (0.0f, 1.0f, gr / 20.0f);
        g.setColour (dynCol);
        g.fillRoundedRectangle (bar.withHeight (bar.getHeight() * frac), 3.0f);
        g.setColour (dim);
        g.setFont (10.0f);
        g.drawText (label, r.removeFromBottom (14), juce::Justification::centred);
        if (gr > 0.1f)
        {
            g.setColour (ink);
            g.drawText (juce::String (gr, 1), bar.withHeight (14).translated (0, 2), juce::Justification::centred);
        }
        return;
    }
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto fill = bar.withTop (bar.getBottom() - bar.getHeight() * frac);
    // green target zone for vocals: about -18 to -6 dBFS
    g.setColour (juce::Colour (0xff3d6b4a).withAlpha (0.35f));
    g.fillRect (bar.withTop (bar.getBottom() - bar.getHeight() * (54.0f / 60.0f))
                    .withBottom (bar.getBottom() - bar.getHeight() * (42.0f / 60.0f)));
    g.setColour (db > -1.0f ? juce::Colour (0xffd9534f) : preCol);
    g.fillRoundedRectangle (fill, 3.0f);
    g.setColour (dim);
    g.setFont (10.0f);
    g.drawText (label, r.removeFromBottom (14), juce::Justification::centred);
}

DreamEditor::DreamEditor (DreamProcessor& p)
    : AudioProcessorEditor (&p), proc (p), curve (p)
{
    for (int i = 0; i < dm::NUM_MICS; ++i)
    {
        auto* b = micButtons.add (new GearButton (dm::MICS[i].name, dm::MICS[i].style, micCol));
        b->setTooltip (dm::MICS[i].blurb);
        b->onClick = [this, i] { setChoice ("mic", i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < dm::NUM_PRES; ++i)
    {
        auto* b = preButtons.add (new GearButton (dm::PRES[i].name, dm::PRES[i].style, preCol));
        b->setTooltip (dm::PRES[i].blurb);
        b->onClick = [this, i] { setChoice ("pre", i); };
        addAndMakeVisible (b);
    }
    for (auto* gname : dm::MIC_GROUPS) addAndMakeVisible (micGroupLabels.add (new juce::Label ({}, gname)));
    for (auto* gname : dm::PRE_GROUPS) addAndMakeVisible (preGroupLabels.add (new juce::Label ({}, gname)));
    for (int i = 0; i < 5; ++i)
    {
        static const char* shortPat[] = { "Native", "Cardioid", "Supercardioid", "Omni", "Figure-8" };
        auto* b = patButtons.add (new GearButton (shortPat[i], {}, micCol));
        b->onClick = [this, i] { setChoice ("pattern", i); };
        addAndMakeVisible (b);
    }
    patButtons[0]->setTooltip ("Keep the pattern you recorded with");
    for (int i = 0; i < 2; ++i)
    {
        auto* b = modeButtons.add (new GearButton (i == 0 ? "LIVE" : "HQ", {}, i == 0 ? preCol : micCol));
        b->setTooltip (i == 0 ? "Near-zero latency: for recording through it"
                              : "Linear-phase mic curve + 8x oversampled preamp: for mixing and bouncing");
        b->onClick = [this, i] { setChoice ("mode", i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < dm::NUM_COMPS; ++i)
    {
        static const char* shortComp[] = { "Off", "Opto (2A)", "FET (76)", "VCA (SSL G)", "Leveller (RVox)" };
        auto* b = compButtons.add (new GearButton (shortComp[i], {}, dynCol));
        b->setTooltip (dm::COMP_TIPS[i]);
        b->onClick = [this, i] { setChoice ("comptype", i); };
        addAndMakeVisible (b);
    }
    lowCutBtn = std::make_unique<GearButton> ("Low cut", juce::String(), micCol);
    presenceBtn = std::make_unique<GearButton> ("Presence", juce::String(), micCol);
    autoBtn = std::make_unique<GearButton> ("Auto input", juce::String(), preCol);
    matchBtn = std::make_unique<GearButton> ("Level match", juce::String(), preCol);
    saveBtn = std::make_unique<GearButton> ("Save", juce::String(), micCol);
    lowCutBtn->onClick = [this] { toggle ("lowcut"); };
    presenceBtn->onClick = [this] { toggle ("presence"); };
    autoBtn->onClick = [this] { toggle ("autoin"); };
    matchBtn->onClick = [this] { toggle ("match"); };
    saveBtn->onClick = [this] { askSave(); };
    lowCutBtn->setTooltip ("The mic's own low cut / bass roll switch");
    presenceBtn->setTooltip ("The mic's own presence boost switch (SM7 types)");
    autoBtn->setTooltip ("Rides your input to a steady -18 dBFS so every preamp is driven the same (max 3 dB per second)");
    matchBtn->setTooltip ("Output as loud as the input, so you judge the tone, not the volume");
    saveBtn->setTooltip ("Save these settings as a preset (Documents\\DREAMMIC\\Presets)");
    for (auto* b : { lowCutBtn.get(), presenceBtn.get(), autoBtn.get(), matchBtn.get(), saveBtn.get() }) addAndMakeVisible (*b);

    for (auto* l : { &sourceLabel, &micInfo, &latencyLabel, &patTitle, &switchTitle, &compTitle, &presetLabel })
    {
        l->setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &patTitle, &switchTitle, &compTitle }) l->setFont (juce::FontOptions (11.5f, juce::Font::bold));
    for (auto* l : micGroupLabels) { l->setFont (juce::FontOptions (11.0f, juce::Font::bold)); l->setColour (juce::Label::textColourId, micCol.withAlpha (0.75f)); }
    for (auto* l : preGroupLabels) { l->setFont (juce::FontOptions (11.0f, juce::Font::bold)); l->setColour (juce::Label::textColourId, preCol.withAlpha (0.75f)); }
    micInfo.setColour (juce::Label::textColourId, ink);
    latencyLabel.setJustificationType (juce::Justification::centredRight);

    sourceBox.addSectionHeading ("YOUR OWN MICS");
    sourceBox.addItem ("Flat / unknown mic", 1);
    sourceBox.addItem ("Samson C01U Pro (measured)", 2);
    sourceBox.addItem ("Mojave MA-50", 3);
    sourceBox.addSectionHeading ("RECORDED ON ONE OF THESE (undo its colour)");
    for (int s = 3; s < dm::NUM_SOURCES; ++s)
        sourceBox.addItem (juce::String (dm::MICS[dm::sourceMicIndex (s)].style).replace (" style", ""), s + 1);
    srcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, "source", sourceBox);
    sourceBox.setTooltip ("The mic you actually recorded with; DREAMMIC undoes its colour first");
    addAndMakeVisible (sourceBox);

    presetBox.setTextWhenNothingSelected ("Choose a preset");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        if (id <= 0) return;
        if (id < 1000) proc.loadFactory (id - 1);
        else proc.loadUser (presetBox.getItemText (presetBox.indexOfItemId (id)));
        shownPreset = "-";
    };
    refreshPresets();
    addAndMakeVisible (presetBox);
    addAndMakeVisible (curve);

    for (int i = 0; i < NK; ++i)
    {
        auto& k = knobs[i];
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        k.s.setColour (juce::Slider::rotarySliderFillColourId, i < 4 ? micCol : i < 6 ? dynCol : preCol);
        k.s.setColour (juce::Slider::rotarySliderOutlineColourId, panel2.brighter (0.15f));
        k.s.setColour (juce::Slider::thumbColourId, ink);
        k.s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        att[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.state, knobIds[i], k.s);
        k.l.setText (knobNames[i], juce::dontSendNotification);
        k.l.setJustificationType (juce::Justification::centred);
        k.l.setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (k.s);
        addAndMakeVisible (k.l);
    }
    knobs[1].s.textFromValueFunction = [] (double v) { return v <= 21 ? juce::String ("Off") : juce::String ((int) v) + " Hz"; };
    knobs[0].s.textFromValueFunction = [] (double v) { return juce::String (v, 0) + " cm"; };
    knobs[5].s.textFromValueFunction = [] (double v) { return v <= 0.01 ? juce::String ("Off") : juce::String (v, 1); };
    for (auto& k : knobs) k.s.updateText();
    inMeter.label = "IN";
    grMeter.label = "GR";
    grMeter.grMode = true;
    outMeter.label = "OUT";
    addAndMakeVisible (inMeter);
    addAndMakeVisible (grMeter);
    addAndMakeVisible (outMeter);
    setResizable (true, true);
    setResizeLimits (1120, 740, 1900, 1200);
    setSize (1240, 800);
    startTimerHz (30);
    timerCallback();
}

DreamEditor::~DreamEditor() { stopTimer(); }

void DreamEditor::refreshPresets()
{
    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("FACTORY");
    const auto& f = DreamProcessor::factoryPresets();
    for (int i = 0; i < (int) f.size(); ++i) presetBox.addItem (f[(size_t) i].name, i + 1);
    const auto users = proc.userPresetNames();
    if (! users.isEmpty())
    {
        presetBox.addSectionHeading ("YOUR PRESETS");
        for (int i = 0; i < users.size(); ++i) presetBox.addItem (users[i], 1000 + i);
    }
    shownPreset = "-";
}

void DreamEditor::askSave()
{
    auto* w = new juce::AlertWindow ("Save preset", "Name this sound:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", proc.presetName.isEmpty() ? juce::String ("My preset") : proc.presetName);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DreamEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r != 1 || safe == nullptr) return;
        if (safe->proc.saveUser (w->getTextEditorContents ("name"))) safe->refreshPresets();
    }), true);
    if (auto* te = w->getTextEditor ("name")) { te->grabKeyboardFocus(); te->selectAll(); }
}

void DreamEditor::setChoice (const char* id, int index)
{
    if (auto* prm = proc.state.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

int DreamEditor::choice (const char* id) const
{
    return (int) proc.state.getRawParameterValue (id)->load();
}

void DreamEditor::timerCallback()
{
    const int m = choice ("mic"), pr = choice ("pre"), pt = choice ("pattern"), md = choice ("mode"), ct = choice ("comptype");
    for (int i = 0; i < micButtons.size(); ++i) micButtons[i]->setToggleState (i == m, juce::dontSendNotification);
    for (int i = 0; i < preButtons.size(); ++i) preButtons[i]->setToggleState (i == pr, juce::dontSendNotification);
    for (int i = 0; i < patButtons.size(); ++i) patButtons[i]->setToggleState (i == pt, juce::dontSendNotification);
    for (int i = 0; i < modeButtons.size(); ++i) modeButtons[i]->setToggleState (i == md, juce::dontSendNotification);
    for (int i = 0; i < compButtons.size(); ++i) compButtons[i]->setToggleState (i == ct, juce::dontSendNotification);
    const auto& mic = dm::MICS[m];
    lowCutBtn->setEnabled (mic.lowCut.n > 0);
    presenceBtn->setEnabled (mic.presence.n > 0);
    lowCutBtn->setTitle (mic.lowCut.n > 0 ? juce::String (mic.lowCut.label) : juce::String ("Low cut"));
    lowCutBtn->setToggleState (flag ("lowcut") && mic.lowCut.n > 0, juce::dontSendNotification);
    presenceBtn->setToggleState (flag ("presence") && mic.presence.n > 0, juce::dontSendNotification);
    const bool au = flag ("autoin");
    autoBtn->setToggleState (au, juce::dontSendNotification);
    matchBtn->setToggleState (flag ("match"), juce::dontSendNotification);
    knobs[6].s.setEnabled (! au);
    knobs[6].l.setText (au ? "Input (Auto " + juce::String (proc.engine.autoGainDb.load(), 1) + " dB)" : juce::String ("Input"),
                        juce::dontSendNotification);
    knobs[4].s.setEnabled (ct > 0);
    micInfo.setText (juce::String (mic.name) + "  ·  " + mic.blurb + "      "
                         + dm::PRES[pr].name + "  ·  " + dm::PRES[pr].blurb, juce::dontSendNotification);
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    latencyLabel.setText ("Latency: " + juce::String (proc.getLatencySamples()) + " samples ("
                              + juce::String (1000.0 * proc.getLatencySamples() / sr, 1) + " ms)",
                          juce::dontSendNotification);
    if (proc.presetName != shownPreset)
    {
        shownPreset = proc.presetName;
        presetBox.setText (shownPreset, juce::dontSendNotification);
    }
    inMeter.setLevel (proc.engine.inPeak.exchange (0.0f));
    outMeter.setLevel (proc.engine.outPeak.exchange (0.0f));
    grMeter.setGr (proc.engine.compGrDb.load());
    curve.repaint();
}

void DreamEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (14).removeFromTop (34);
    g.setColour (micCol);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("DREAMMIC", r.removeFromLeft (170), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (12.0f);
    g.drawText ("mic + preamp emulator  2.0", r.removeFromLeft (190), juce::Justification::centredLeft);
}

void DreamEditor::resized()
{
    if (micButtons.isEmpty()) return;
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (34);
    top.removeFromLeft (370);
    presetLabel.setBounds (top.removeFromLeft (50));
    presetBox.setBounds (top.removeFromLeft (220).reduced (0, 4));
    top.removeFromLeft (6);
    saveBtn->setBounds (top.removeFromLeft (66).reduced (0, 2));
    auto modes = top.removeFromRight (190);
    modeButtons[0]->setBounds (modes.removeFromLeft (95).reduced (2));
    modeButtons[1]->setBounds (modes.reduced (2));
    latencyLabel.setBounds (top.removeFromRight (230));
    r.removeFromTop (8);

    auto row = r.removeFromTop (30);
    sourceLabel.setBounds (row.removeFromLeft (64));
    sourceBox.setBounds (row.removeFromLeft (280).reduced (0, 2));
    row.removeFromLeft (16);
    micInfo.setBounds (row);
    r.removeFromTop (8);

    // preamps at the bottom: one row per group
    auto preArea = r.removeFromBottom (2 * 50 + 4);
    for (int g = 0; g < 2; ++g)
    {
        auto pr = preArea.removeFromTop (50);
        if (g == 0) preArea.removeFromTop (4);
        preGroupLabels[g]->setBounds (pr.removeFromLeft (150));
        const auto ids = presInGroup (g);
        const int w = pr.getWidth() / juce::jmax (5, (int) ids.size());
        for (int i : ids) preButtons[i]->setBounds (pr.removeFromLeft (w));
    }
    r.removeFromBottom (8);

    // meters on the right
    auto meters = r.removeFromRight (105);
    inMeter.setBounds (meters.removeFromLeft (35));
    grMeter.setBounds (meters.removeFromLeft (35));
    outMeter.setBounds (meters);
    r.removeFromRight (10);

    // mics on the left: 2 columns per group
    auto left = r.removeFromLeft (470);
    int rows = 0;
    for (int g = 0; g < 3; ++g) rows += ((int) micsInGroup (g).size() + 1) / 2;
    const int rh = (left.getHeight() - 3 * 18) / rows;
    for (int g = 0; g < 3; ++g)
    {
        micGroupLabels[g]->setBounds (left.removeFromTop (18));
        const auto ids = micsInGroup (g);
        for (size_t k = 0; k < ids.size(); k += 2)
        {
            auto line = left.removeFromTop (rh);
            micButtons[ids[k]]->setBounds (line.removeFromLeft (line.getWidth() / 2));
            if (k + 1 < ids.size()) micButtons[ids[k + 1]]->setBounds (line);
        }
    }
    r.removeFromLeft (12);

    // knobs
    auto kr = r.removeFromBottom (108);
    const int kw = kr.getWidth() / NK;
    for (auto& k : knobs)
    {
        auto c = kr.removeFromLeft (kw);
        k.l.setBounds (c.removeFromTop (16));
        k.s.setBounds (c);
    }
    r.removeFromBottom (6);

    // tracking comp
    auto cr = r.removeFromBottom (34);
    compTitle.setBounds (cr.removeFromLeft (110));
    const int cw = cr.getWidth() / dm::NUM_COMPS;
    for (auto* b : compButtons) b->setBounds (cr.removeFromLeft (cw));
    r.removeFromBottom (6);

    // mic switches + level tools
    auto sr = r.removeFromBottom (34);
    switchTitle.setBounds (sr.removeFromLeft (110));
    const int sw = sr.getWidth() / 4;
    lowCutBtn->setBounds (sr.removeFromLeft (sw));
    presenceBtn->setBounds (sr.removeFromLeft (sw));
    autoBtn->setBounds (sr.removeFromLeft (sw));
    matchBtn->setBounds (sr);
    r.removeFromBottom (6);

    // pattern
    auto pr = r.removeFromBottom (34);
    patTitle.setBounds (pr.removeFromLeft (110));
    const int bw = pr.getWidth() / 5;
    for (auto* b : patButtons) b->setBounds (pr.removeFromLeft (bw));
    r.removeFromBottom (8);
    curve.setBounds (r);
}
