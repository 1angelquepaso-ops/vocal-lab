#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Runs one engine command on a background thread; the result comes back on the message thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("pyrex engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (600000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// One labelled grip that drags ONE already-saved WAV into FL (playbook: never drag many at once).
class DragTile : public juce::Component
{
public:
    DragTile (juce::String name, juce::Colour c) : title (std::move (name)), colour (c) {}
    void setFile (const juce::File& f) { file = f; repaint(); }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    juce::String title;
    juce::Colour colour;
    juce::File file;
    bool hover = false, dragging = false;
};

// Green / amber / red light with a word and a line of numbers.
class Badge : public juce::Component
{
public:
    void set (const juce::String& level, const juce::String& head, const juce::String& sub)
    { lvl = level; h = head; s = sub; repaint(); }
    void paint (juce::Graphics&) override;

private:
    juce::String lvl, h, s;
};

class PyrexEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit PyrexEditor (PyrexProcessor&);
    ~PyrexEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }

private:
    PyrexProcessor& proc;

    juce::TextButton loadBtn { "Load stems..." }, half { juce::CharPointer_UTF8 ("\xc3\xb7" "2") }, dbl { juce::CharPointer_UTF8 ("\xc3\x97" "2") },
                     setBpm { "Set" }, nudgeL { "-1 beat" }, nudgeR { "+1 beat" },
                     lockBtn { "BPM LOCK" }, keyBtn { "KEY TRANSPOSE" }, renderBtn { "RENDER" }, openBtn { "Open folder" };
    juce::TextEditor songBpmBox, targetBox;
    juce::ComboBox keyBox, toKeyBox;
    juce::Label title, stemsLabel, bpmLabel, keyLabel, bar1Label, hostLabel, shiftLabel, statusLabel, notesLabel, flagsLabel,
                hSong, hLock, hOut;
    Badge clockBadge, renderBadge;
    double progress = 0.0;
    juce::ProgressBar progressBar { progress };
    juce::OwnedArray<DragTile> tiles;
    std::unique_ptr<EngineJob> job;
    std::unique_ptr<juce::FileChooser> chooser;
    bool busy = false, dragOver = false;
    juce::String progressText;

    juce::File engineExe() const;
    juce::File outputDir() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void setStems (const juce::StringArray& files);
    void analyze();
    void renderNow();
    void settingsChanged();
    double targetBpmNow() const;
    juce::String originalKey() const;
    int shiftFor (const juce::String& from, const juce::String& to) const;
    void fillToKeys();
    void refresh();
    void timerCallback() override;
};
