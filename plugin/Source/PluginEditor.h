#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>

// Runs one engine command on a background thread and hands the parsed JSON
// back on the message thread. Nothing touches the audio thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("vocal-lab engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (60000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// A small handle you drag from straight into FL's playlist.
class DragHandle : public juce::Component
{
public:
    std::function<juce::StringArray()> getFiles;
    juce::String text { "drag" };
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    bool dragging = false;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
};

struct VoiceRow
{
    juce::Label name;
    juce::ToggleButton mute { "mute" };
    juce::TextButton reroll { "reroll" }, remove { "x" };
    DragHandle drag;
};

class HarmonizerEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit HarmonizerEditor (HarmonizerProcessor&);
    ~HarmonizerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    HarmonizerProcessor& proc;

    juce::TextButton loadVocal { "Load vocal chunk" }, loadBeat { "Load beat (analysis only)" };
    juce::Label vocalName, beatName, status;
    juce::Label voicesLabel { {}, "Voices" }, colourLabel { {}, "Colour" };
    juce::ComboBox voicesBox;
    juce::Slider colour;
    juce::TextButton generate { "Generate 8" };
    juce::TextButton prev { "<" }, next { ">" };
    juce::Label optionLabel;
    VoiceRow rows[4];
    juce::TextButton addVoice { "+ add voice" }, play { "Play preview" }, openFolder { "Open folder" };
    juce::ToggleButton leadToggle { "hear lead in preview" };
    DragHandle dragAll;

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EngineJob> job;
    bool busy = false;

    juce::File engineExe() const;
    juce::File exportDir() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void analyze();
    void refresh();
    void rebuildPreview();
    juce::var currentVoices() const;
    juce::String muteKey (const juce::String& voice) const;
    juce::StringArray exportVoices (bool onlyUnmuted, const juce::String& single = {});
    void chooseFile (bool beat);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
