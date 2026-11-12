#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"

namespace mxui
{
const juce::Colour BG { 0xff111216 }, PANEL { 0xff181a20 }, GRAPH { 0xff0c0d10 }, GRID { 0xff20232b }, TEXT { 0xffc9ced8 },
    DIM { 0xff6b7180 }, GOLD { 0xffe0b25a }, CURVE { 0xfff2f4f8 };
juce::Colour bandColour (int i);

class LnF : public juce::LookAndFeel_V4
{
public:
    LnF();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (13.0f, juce::Font::bold); }
};

class Graph : public juce::Component
{
public:
    explicit Graph (MexProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void updateAnalyzer();
    int selected = -1;
    int analyzerMode = 1;     // 0 off, 1 post, 2 pre+post
    std::function<void (int)> onSelect;

private:
    MexProcessor& proc;
    int dragging = -1, hover = -1;
    juce::Point<float> dragStart; float startF = 0, startG = 0, startQ = 0;
    std::vector<float> specPost, specPre;
    juce::dsp::FFT fft { 13 };
    std::vector<float> fftBuf, win;

    float xForF (double f) const;
    double fForX (float x) const;
    float yForDb (double db) const;
    double dbForY (float y) const;
    float yForSpec (double db) const;
    int hitNode (juce::Point<float> p) const;
    juce::Point<float> nodePos (int i, const mx::BandParams& b) const;
    void showBandMenu (int band);
    void addBandAt (juce::Point<float> p);
    void setP (int band, const char* f, float v);
    float getP (int band, const char* f) const;
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (34.0f, 8.0f).withTrimmedBottom (14.0f); }
};

class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, const juce::String& suffix = {});
    void resized() override;
    juce::Slider slider;
    juce::Label label;
};
} // namespace mxui

class MexEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MexEditor (MexProcessor&);
    ~MexEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectBand (int i);
    void refreshPanel();

    MexProcessor& proc;
    mxui::LnF lnf;
    mxui::Graph graph;

    // top bar
    juce::TextButton liveBtn { "LIVE" }, hqBtn { "HQ" }, autoBtn { "AUTO" }, learnBtn { "LEARN" };
    juce::ComboBox phaseBox, analyzerBox;
    mxui::Knob drive { "DRIVE" }, output { "OUTPUT" }, strength { "STRENGTH" };
    juce::Label modeLabel, statusLabel;

    // band panel
    juce::Label bandTitle;
    juce::ComboBox typeBox, orderBox, charBox, shapeBox, stereoBox;
    mxui::Knob freqK { "FREQ" }, gainK { "GAIN" }, qK { "Q" }, rangeK { "RANGE" }, thrK { "THRESH" },
        attK { "ATTACK" }, relK { "RELEASE" };
    juce::ToggleButton dynBtn { "Dynamic" }, autoThrBtn { "Auto thr" }, scBtn { "Ext SC" };
    juce::TextButton delBtn { "Delete" };

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> aDrive, aOut, aStrength, aFreq, aGain, aQ, aRange, aThr, aAtt, aRel;
    std::unique_ptr<CA> aPhase, aType, aOrder, aChar, aShape, aStereo;
    std::unique_ptr<BA> aDyn, aAutoThr, aSc;
    int panelBand = -2;
    int thrBoundAuto = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MexEditor)
};
