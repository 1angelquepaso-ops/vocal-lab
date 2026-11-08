#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Runs one engine command on a background thread and hands the parsed JSON
// back on the message thread. Nothing touches the audio thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("vocal-lab engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (120000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// One horizontal lane: name, waveform with playhead, buttons, and a grab
// handle at the right end of the waveform that drags the voice into FL.
class Lane : public juce::Component, private juce::ChangeListener
{
public:
    Lane (juce::AudioFormatManager&, juce::AudioThumbnailCache&, bool isLead);
    ~Lane() override;

    void setFile (const juce::File& stem);
    void setExport (const juce::File& f, bool ready) { exportFile = f; exportReady = ready; repaint(); }

    juce::String voice;
    juce::TextButton mute { "M" }, solo { "S" }, reroll { "reroll" }, remove { "x" };
    std::function<void (float)> onSeek;
    std::function<float()> playhead;
    bool dimmed = false;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    const bool lead;
    juce::AudioThumbnail thumb;
    juce::File exportFile;
    bool exportReady = false, dragging = false, hoverHandle = false, pressOnHandle = false;

    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> handleArea() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

class HarmonizerEditor : public juce::AudioProcessorEditor,
                         public juce::FileDragAndDropTarget,
                         private juce::ListBoxModel,
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
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache { 32 };

    // source
    juce::TextButton loadVocal { "Load vocal" }, loadBeat { "Load beat (analysis only)" };
    juce::Label vocalName, beatName, keyInfo;
    // generate
    juce::Label styleLabel { {}, "Style" }, voicesLabel { {}, "Voices" };
    juce::ComboBox styleBox, voicesBox;
    juce::TextButton gen1 { "+ Generate 1" }, gen4 { "Generate 4" };
    // options list
    juce::Label optionsTitle { {}, "OPTIONS" };
    juce::ListBox optionList { "options", this };
    juce::TextButton keepBtn { "Keep" }, deleteBtn { "Delete" };
    // lanes
    std::unique_ptr<Lane> leadLane;
    std::unique_ptr<Lane> voiceLanes[4];
    juce::TextButton addVoice { "+ add voice" };
    // transport
    juce::TextButton play { "Play" }, loopBtn { "Loop" }, openFolder { "Open folder" };
    juce::ToggleButton leadToggle { "hear lead" };
    juce::Label timeLabel, status;
    double progress = -1.0;               // <0 = idle, else 0..1 (or indeterminate)
    juce::ProgressBar progressBar { progress };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EngineJob> job;
    bool busy = false;

    // ListBoxModel
    int getNumRows() override { return proc.options.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;

    juce::File engineExe() const;
    juce::File exportDir() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void analyze();
    void generate (int count);
    void voiceCommand (const juce::String& cmd, const juce::String& voice);
    void replaceCurrent (const juce::var& res);
    void showOption (int index);
    void refresh();
    void rebuildPreview();
    void prepareExports();
    juce::var current() const;
    int currentId() const;
    juce::String key (const juce::String& voice) const;
    void chooseFile (bool beat);
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerEditor)
};
