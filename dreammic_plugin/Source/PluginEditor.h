#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

// A selectable card: name on top, what it's modelled on underneath.
class GearButton : public juce::Button
{
public:
    GearButton (const juce::String& name, const juce::String& style, juce::Colour accent)
        : juce::Button (name), title (name), sub (style), col (accent) { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    juce::String title, sub;
    juce::Colour col;
};

class CurveView : public juce::Component
{
public:
    explicit CurveView (DreamProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
private:
    DreamProcessor& proc;
};

class Meter : public juce::Component
{
public:
    void setLevel (float l) { level = juce::jmax (l, level * 0.85f); repaint(); }
    void paint (juce::Graphics&) override;
    juce::String label;
private:
    float level = 0;
};

class DreamEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DreamEditor (DreamProcessor&);
    ~DreamEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    DreamProcessor& proc;
    juce::OwnedArray<GearButton> micButtons, preButtons, patButtons, modeButtons;
    juce::ComboBox sourceBox;
    juce::Label sourceLabel { {}, "Your mic" }, micInfo, latencyLabel, micsTitle { {}, "MIC" },
                preTitle { {}, "PREAMP" }, patTitle { {}, "PATTERN" };
    CurveView curve;
    struct K { juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow }; juce::Label l; };
    K knobs[6];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att[6];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAtt;
    Meter inMeter, outMeter;

    void setChoice (const char* id, int index);
    int choice (const char* id) const;
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamEditor)
};
