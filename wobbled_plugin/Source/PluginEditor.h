#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

namespace wbui
{
namespace col
{
    const juce::Colour bg { 0xff0e0f14 }, panel { 0xff16181f }, panel2 { 0xff1d2029 }, line { 0xff2a2e3a };
    const juce::Colour text { 0xffd8dce6 }, dim { 0xff7c8396 }, lime { 0xffb8f23a }, mag { 0xffe04fd0 }, sub { 0xff6fb7ff };
}

class Look : public juce::LookAndFeel_V4
{
public:
    Look();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (12.0f, juce::Font::bold); }
};

// Right-click on any control opens FL's own parameter menu (Create automation clip, Link to controller...)
bool showHostMenu (juce::Component& from, juce::AudioProcessorParameter* param, juce::Point<int> screenPos);

struct HostSlider : juce::Slider
{
    juce::AudioProcessorParameter* param = nullptr;
    bool rightDown = false;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { rightDown = true; showHostMenu (*this, param, e.getScreenPosition()); return; }
        rightDown = false; juce::Slider::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! rightDown) juce::Slider::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (rightDown) { rightDown = false; return; } juce::Slider::mouseUp (e); }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::Slider::mouseDoubleClick (e); }
};

struct HostButton : juce::TextButton
{
    juce::AudioProcessorParameter* param = nullptr;
    bool rightDown = false;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { rightDown = true; showHostMenu (*this, param, e.getScreenPosition()); return; }
        rightDown = false; juce::TextButton::mouseDown (e);
    }
    void mouseDrag (const juce::MouseEvent& e) override { if (! rightDown) juce::TextButton::mouseDrag (e); }
    void mouseUp (const juce::MouseEvent& e) override { if (rightDown) { rightDown = false; return; } juce::TextButton::mouseUp (e); }
};

struct HostCombo : juce::ComboBox
{
    juce::AudioProcessorParameter* param = nullptr;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { showHostMenu (*this, param, e.getScreenPosition()); return; }
        juce::ComboBox::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! e.mods.isPopupMenu()) juce::ComboBox::mouseUp (e); }
};

// knob with caption underneath
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& caption, const juce::String& help);
    void resized() override;
    void paint (juce::Graphics&) override;
    HostSlider slider;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
};

class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& caption, const juce::String& help);
    void resized() override;
    void paint (juce::Graphics&) override;
    HostCombo box;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
};

class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& st, const char* paramId, const juce::String& text, const juce::String& caption, const juce::String& help);
    void resized() override;
    void paint (juce::Graphics&) override;
    HostButton button;
private:
    juce::String caption;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
};

class SampleView : public juce::Component
{
public:
    explicit SampleView (WobbleProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void refresh();
    std::function<void()> onClickLoad;
private:
    WobbleProcessor& proc;
    const wb::SampleData* shown = nullptr;
    std::vector<std::pair<float, float>> peaks;
    float lastStart = -1, lastLen = -1; bool lastLoop = false, lastRev = false, lastLoading = false; int lastSrc = -1;
};

class LfoView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit LfoView (WobbleProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void tick();
private:
    WobbleProcessor& proc;
    enum class Drag { none, point, curve, loopStart, loopEnd } drag = Drag::none;
    int dragIndex = -1;
    float lastPos = -1, lastVal = -1;
    juce::Rectangle<float> graph() const;
    juce::Rectangle<float> strip() const;
    juce::Point<float> toScreen (float x, float y) const;
    float snapX (float x, bool free) const;
    int hitPoint (juce::Point<float> p) const;
    int hitCurve (juce::Point<float> p) const;
    void changed();
};

class XYPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit XYPad (WobbleProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void tick();              // spring + repaint (called at 30 Hz)
    void centre();
private:
    WobbleProcessor& proc;
    juce::RangedAudioParameter *px, *py;
    bool dragging = false, springing = false, rightClick = false;
    float lastX = 9, lastY = 9; int lastXT = -1, lastYT = -1;
    std::deque<juce::Point<float>> trail;
    juce::Rectangle<float> pad() const { return getLocalBounds().toFloat().reduced (2.0f); }
    void setFromMouse (juce::Point<float>);
};
} // namespace wbui

class WobbleEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit WobbleEditor (WobbleProcessor&);
    ~WobbleEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    static constexpr int baseW = 1280, baseH = 800;
    WobbleProcessor& proc;
    wbui::Look look;
    juce::TooltipWindow tips { this, 500 };

    struct Content : juce::Component
    {
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> sections;
        void paint (juce::Graphics&) override;
    } content;

    std::vector<std::unique_ptr<juce::Component>> owned;
    template <typename T, typename... A> T* make (A&&... a)
    {
        auto c = std::make_unique<T> (std::forward<A> (a)...);
        auto* raw = c.get();
        content.addAndMakeVisible (raw);
        owned.push_back (std::move (c));
        return raw;
    }

    wbui::SampleView* sampleView = nullptr;
    wbui::LfoView* lfoView = nullptr;
    wbui::XYPad* xyPad = nullptr;
    juce::TextButton loadBtn { "LOAD SAMPLE" }, initBtn { "INIT" }, trimBtn { "TRIM" }, normBtn { "NORM" };
    juce::TextButton rerollBtn { "REROLL" }, randBtn { "RANDOMIZE" }, loopAllBtn { "LOOP ALL" }, centreBtn { "CENTRE" };
    juce::ComboBox gridBox, presetBox;
    wbui::HostButton xBtns[4], yBtns[4];
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;

    std::vector<juce::Component*> rowSample, rowWobble, rowTargets, rowVoice, rowFilter, rowEq, rowOut;
    wbui::Choice* sourceChoice = nullptr;
    wbui::Toggle *springT = nullptr, *snapT = nullptr;
    wbui::Knob* glideK = nullptr;

    void timerCallback() override;
    void parentHierarchyChanged() override;
    void keepFocusInHost();
    void updateGrid (bool force);
    int lastGridRate = -1, lastGridSteps = -1;
    void layoutContent();
    void openChooser();
    void loadFile (const juce::File&);
    void syncTargetButtons();
};
