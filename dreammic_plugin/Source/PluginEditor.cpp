#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff0d0e12), panel (0xff17191f), panel2 (0xff20232b), ink (0xffecebe8),
                       dim (0xff858892), micCol (0xffd9b36c), preCol (0xff8fc9de);
    const char* knobIds[] = { "distance", "warmth", "hpf", "air", "input", "output" };
    const char* knobNames[] = { "Distance", "Warmth", "High-pass", "Air", "Input", "Output" };
}

void GearButton::paintButton (juce::Graphics& g, bool over, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2);
    const bool on = getToggleState();
    g.setColour (on ? col.withAlpha (0.18f) : (over ? panel2.brighter (0.06f) : panel2));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? col : panel2.brighter (0.2f));
    g.drawRoundedRectangle (r, 6.0f, on ? 1.6f : 1.0f);
    auto t = getLocalBounds().reduced (10, 4);
    if (sub.isEmpty())
    {
        g.setColour (on ? col : ink);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (title, t, juce::Justification::centred);
        return;
    }
    g.setColour (on ? col : ink);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (title, t.removeFromTop (t.getHeight() / 2 + 1), juce::Justification::bottomLeft);
    g.setColour (dim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (sub, t, juce::Justification::topLeft);
}

void CurveView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (panel);
    g.fillRoundedRectangle (r, 6.0f);
    auto a = r.reduced (34, 14);
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
    // curve of the static chain for the current settings
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
    g.drawText ("Tone change applied to your recording (your mic undone + this mic, distance, high-pass, preamp, air)",
                r.reduced (10, 4).removeFromTop (14).toNearestInt(), juce::Justification::centredRight);
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto bar = r.withTrimmedBottom (16).reduced (4, 0);
    g.setColour (panel2);
    g.fillRoundedRectangle (bar, 3.0f);
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
    for (int i = 0; i < 5; ++i)
    {
        auto* b = patButtons.add (new GearButton (dm::PATTERN_NAMES[i], {}, micCol));
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
    for (auto* l : { &sourceLabel, &micInfo, &latencyLabel, &micsTitle, &preTitle, &patTitle })
    {
        l->setColour (juce::Label::textColourId, dim);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &micsTitle, &preTitle, &patTitle })
        l->setFont (juce::FontOptions (12.0f, juce::Font::bold));
    micInfo.setColour (juce::Label::textColourId, ink);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    for (int i = 0; i < dm::NUM_SOURCES; ++i) sourceBox.addItem (dm::SOURCE_NAMES[i], i + 1);
    srcAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.state, "source", sourceBox);
    sourceBox.setTooltip ("The mic you actually recorded with; DREAMMIC undoes its colour first");
    addAndMakeVisible (sourceBox);
    addAndMakeVisible (curve);
    for (int i = 0; i < 6; ++i)
    {
        auto& k = knobs[i];
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        k.s.setColour (juce::Slider::rotarySliderFillColourId, i < 4 ? micCol : preCol);
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
    knobs[2].s.textFromValueFunction = [] (double v) { return v <= 21 ? juce::String ("Off") : juce::String ((int) v) + " Hz"; };
    knobs[0].s.textFromValueFunction = [] (double v) { return juce::String (v, 0) + " cm"; };
    knobs[2].s.updateText();
    knobs[0].s.updateText();
    inMeter.label = "IN";
    outMeter.label = "OUT";
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    setResizable (true, true);
    setResizeLimits (980, 620, 1800, 1100);
    setSize (1080, 680);
    startTimerHz (30);
    timerCallback();
}

DreamEditor::~DreamEditor() { stopTimer(); }

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
    const int m = choice ("mic"), pr = choice ("pre"), pt = choice ("pattern"), md = choice ("mode");
    for (int i = 0; i < micButtons.size(); ++i) micButtons[i]->setToggleState (i == m, juce::dontSendNotification);
    for (int i = 0; i < preButtons.size(); ++i) preButtons[i]->setToggleState (i == pr, juce::dontSendNotification);
    for (int i = 0; i < patButtons.size(); ++i) patButtons[i]->setToggleState (i == pt, juce::dontSendNotification);
    for (int i = 0; i < modeButtons.size(); ++i) modeButtons[i]->setToggleState (i == md, juce::dontSendNotification);
    micInfo.setText (juce::String (dm::MICS[m].name) + "  ·  " + dm::MICS[m].blurb + "      "
                         + dm::PRES[pr].name + "  ·  " + dm::PRES[pr].blurb, juce::dontSendNotification);
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    latencyLabel.setText ("Latency: " + juce::String (proc.getLatencySamples()) + " samples ("
                              + juce::String (1000.0 * proc.getLatencySamples() / sr, 1) + " ms)",
                          juce::dontSendNotification);
    inMeter.setLevel (proc.engine.inPeak.exchange (0.0f));
    outMeter.setLevel (proc.engine.outPeak.exchange (0.0f));
    curve.repaint();
}

void DreamEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds().reduced (14).removeFromTop (34);
    g.setColour (micCol);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText ("DREAMMIC", r.removeFromLeft (180), juce::Justification::centredLeft);
    g.setColour (dim);
    g.setFont (12.0f);
    g.drawText ("mic + preamp emulator", r.removeFromLeft (170), juce::Justification::centredLeft);
}

void DreamEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    auto top = r.removeFromTop (34);
    top.removeFromLeft (360);
    auto modes = top.removeFromRight (190);
    modeButtons[0]->setBounds (modes.removeFromLeft (95).reduced (2));
    modeButtons[1]->setBounds (modes.reduced (2));
    latencyLabel.setBounds (top.removeFromRight (230));
    r.removeFromTop (8);

    auto row = r.removeFromTop (30);
    sourceLabel.setBounds (row.removeFromLeft (70));
    sourceBox.setBounds (row.removeFromLeft (260).reduced (0, 2));
    row.removeFromLeft (16);
    micInfo.setBounds (row);
    r.removeFromTop (8);

    // meters on the right
    auto meters = r.removeFromRight (70);
    inMeter.setBounds (meters.removeFromLeft (35));
    outMeter.setBounds (meters);
    r.removeFromRight (10);

    // mic column on the left
    auto left = r.removeFromLeft (250);
    micsTitle.setBounds (left.removeFromTop (18));
    const int mh = left.getHeight() / dm::NUM_MICS;
    for (auto* b : micButtons) b->setBounds (left.removeFromTop (mh));
    r.removeFromLeft (12);

    // preamps at the bottom
    auto preArea = r.removeFromBottom (74);
    preTitle.setBounds (preArea.removeFromTop (18));
    const int pw = preArea.getWidth() / dm::NUM_PRES;
    for (auto* b : preButtons) b->setBounds (preArea.removeFromLeft (pw));
    r.removeFromBottom (8);

    // knobs
    auto kr = r.removeFromBottom (110);
    const int kw = kr.getWidth() / 6;
    for (auto& k : knobs)
    {
        auto c = kr.removeFromLeft (kw);
        k.l.setBounds (c.removeFromTop (16));
        k.s.setBounds (c);
    }
    r.removeFromBottom (8);

    // pattern buttons + curve
    auto pr = r.removeFromBottom (34);
    patTitle.setBounds (pr.removeFromLeft (70));
    const int bw = pr.getWidth() / 5;
    for (auto* b : patButtons) b->setBounds (pr.removeFromLeft (bw));
    r.removeFromBottom (8);
    curve.setBounds (r);
}
