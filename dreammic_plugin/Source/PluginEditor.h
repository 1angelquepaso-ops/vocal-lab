#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

// A selectable card: name on top, what it's modelled on underneath (or a single centred label).
class GearButton : public juce::Button
{
public:
    GearButton (const juce::String& name, const juce::String& style, juce::Colour accent)
        : juce::Button (name), title (name), sub (style), col (accent) { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void setTitle (const juce::String& t) { if (t != title) { title = t; repaint(); } }
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
    void setGr (float db) { gr = juce::jmax (db, gr * 0.9f); repaint(); }
    void paint (juce::Graphics&) override;
    juce::String label;
    bool grMode = false;
private:
    float level = 0, gr = 0;
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
    juce::OwnedArray<GearButton> micButtons, preButtons, patButtons, modeButtons, compButtons;
    std::unique_ptr<GearButton> lowCutBtn, presenceBtn, autoBtn, matchBtn, saveBtn;
    juce::ComboBox sourceBox, presetBox;
    juce::Label sourceLabel { {}, "Your mic" }, micInfo, latencyLabel, patTitle { {}, "PATTERN" },
                switchTitle { {}, "SWITCHES" }, compTitle { {}, "TRACKING COMP" }, presetLabel { {}, "Preset" };
    juce::OwnedArray<juce::Label> micGroupLabels, preGroupLabels;
    CurveView curve;
    struct K { juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow }; juce::Label l; };
    static constexpr int NK = 8;
    K knobs[NK];
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att[NK];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> srcAtt;
    Meter inMeter, grMeter, outMeter;
    juce::String shownPreset { "-" };

    void setChoice (const char* id, int index);
    int choice (const char* id) const;
    bool flag (const char* id) const { return proc.state.getRawParameterValue (id)->load() > 0.5f; }
    void toggle (const char* id) { setChoice (id, flag (id) ? 0 : 1); }
    void refreshPresets();
    void askSave();
    void timerCallback() override;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamEditor)
};
