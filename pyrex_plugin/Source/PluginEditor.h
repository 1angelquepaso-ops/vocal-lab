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
    ~EngineJob() override { cancel(); stopThread (5000); }      // closing the window must never wait for a render
    void run() override;
    void cancel();                       // STOP: kills the engine (and its Rubber Band helpers) at once

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
    std::atomic<bool> cancelled { false };
    juce::CriticalSection lock;
    juce::ChildProcess* running = nullptr;
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

// The loaded stems as chips: each shows the file name and an x to take it out of the batch.
class StemList : public juce::Component
{
public:
    std::function<void (int)> onRemove;
    void set (const juce::StringArray& paths, bool allowRemove)
    {
        names.clear();
        for (auto& p : paths) names.add (juce::File (p).getFileNameWithoutExtension());
        removable = allowRemove;
        repaint();
    }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::StringArray names;
    bool removable = true;
    static constexpr int chipW = 158, chipH = 24, gap = 6;
    juce::Rectangle<int> chipRect (int i) const;
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
                     setBpm { "Set" }, clearBtn { "Clear" }, stopBtn { "STOP" }, flBtn { "= FL" }, nudgeL { "-1 beat" }, nudgeR { "+1 beat" },
                     lockBtn { "BPM LOCK" }, keyBtn { "KEY TRANSPOSE" }, renderBtn { "RENDER" }, openBtn { "Open folder" };
    juce::TextEditor songBpmBox, targetBox;
    StemList stemList;
    bool rendering = false;
    bool renderAfterAnalyze = false;     // RENDER pressed on a fresh list: read the clock, then go straight on
    juce::int64 busySince = 0;
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
    void setStems (const juce::StringArray& files);       // ADDS to the list (12 max), never replaces it
    void removeStem (int index);
    void clearStems();
    void analyze (bool thenRender = false);
    void startNow();
    void stopNow();
    void renderNow();
    void settingsChanged();
    double targetBpmNow() const;
    double nudgeStep() const;
    juce::String originalKey() const;
    int shiftFor (const juce::String& from, const juce::String& to) const;
    void fillToKeys();
    void refresh();
    void timerCallback() override;
};
