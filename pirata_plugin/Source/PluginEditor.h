#pragma once
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Runs one engine command on a background thread; result comes back on the
// message thread. Nothing touches the audio thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("pirata engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (120000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// Piano roll: chord names across the top, chords / bass / melody notes,
// bar lines and the preview playhead. Click to move the playhead.
class PianoRoll : public juce::Component
{
public:
    void setData (const juce::var& build) { data = build; repaint(); }
    std::function<double()> playheadBeats;   // <0 = none
    std::function<void (double)> onSeekBeats;
    bool showChords = true, showMelody = true;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::var data;
    juce::Rectangle<int> noteArea() const;
    double lengthBeats() const;
};

// A labelled grip that drags one already-saved MIDI file out into FL.
class DragTile : public juce::Component
{
public:
    DragTile (juce::String name, juce::Colour c) : title (std::move (name)), colour (c) {}
    void setFile (const juce::File& f) { file = f; repaint(); }
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

class PirataEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit PirataEditor (PirataProcessor&);
    ~PirataEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    PirataProcessor& proc;
    juce::AudioFormatManager formats;

    // top bar
    juce::TextButton loadBtn { "Load sample" }, reanalyze { "Re-analyze" };
    juce::Label sampleName, bpmLabel { {}, "BPM" }, info;
    juce::TextEditor bpmBox;
    juce::TextButton halfBtn { juce::CharPointer_UTF8 ("\xc3\xb7" "2") }, dblBtn { "x2" }, ratioBtn { "3:2" };
    juce::Label tempoWarn;
    void scaleBpm (double factor);

    // chords
    juce::Label chordsLabel { {}, "CHORDS" };
    juce::TextButton simpleBtn { "Simple" }, playedBtn { "As played" }, richBtn { "Rich" };
    juce::Label timingLabel { {}, "Timing" };
    juce::ComboBox timingBox;

    // melody layer
    juce::Label melodyLabel { {}, "MELODY LAYER" }, arpLabel { {}, "ARP" }, arpHint, densityLabel { {}, "Density" },
                registerLabel { {}, "Register" }, patternLabel { {}, "Pattern" },
                rateLabel { {}, "Rate" }, octLabel { {}, "Octaves" };
    juce::ComboBox modeBox, registerBox, patternBox, rateBox, octBox;
    juce::Slider density { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton randomize { "Randomize" };

    // roll + exports
    PianoRoll roll;
    DragTile chordsTile { "CHORDS", juce::Colour (0xffe0a84a) },
             bassTile   { "BASS",   juce::Colour (0xff6fa8dc) },
             melodyTile { "MELODY", juce::Colour (0xffe07a9a) },
             fullTile   { "FULL",   juce::Colour (0xffc9c4b8) };
    juce::Label dragHint;

    // transport
    juce::TextButton play { "Play" }, loopBtn { "Loop" }, openFolder { "Open folder" };
    juce::ToggleButton hearOrig { "Original" }, hearChords { "Chords" }, hearMelody { "Melody" };
    juce::Label timeLabel, status;
    double progress = -1.0;
    juce::ProgressBar progressBar { progress };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EngineJob> job;
    bool busy = false, pendingBuild = false;

    juce::File engineExe() const;
    juce::File exportDir() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void chooseFile();
    void analyze();
    void buildLayers();
    void rebuildPreview();
    void applyBuild();
    void refresh();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PirataEditor)
};
