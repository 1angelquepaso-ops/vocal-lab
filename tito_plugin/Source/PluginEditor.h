#pragma once
#include "PluginProcessor.h"
#include "SoundLibrary.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Runs one engine command on a background thread; the result comes back on the message thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("tito engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (180000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// One labelled grip that drags one already-saved MIDI file into FL.
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

// Whole song at a glance: sections, every hit per pad, fill spots, playhead.
class SongStrip : public juce::Component
{
public:
    explicit SongStrip (TitoProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void (double)> onSeek;

private:
    TitoProcessor& proc;
    double lengthBeats() const;
};

class PadButton : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    PadButton (TitoProcessor& p, int i) : proc (p), index (i) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;
    std::function<void (int, float)> onPress;
    std::function<void (int, juce::File)> onDropFile;
    float flash = 0.0f;
    bool selected = false;

private:
    TitoProcessor& proc;
    int index;
};

// Waveform of the selected pad with draggable start / end markers.
class PadWave : public juce::Component
{
public:
    explicit PadWave (TitoProcessor& p) : proc (p) {}
    void setPad (int i) { pad = i; cacheFor = nullptr; repaint(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void()> onChange;

private:
    TitoProcessor& proc;
    int pad = 0, dragging = 0;
    const void* cacheFor = nullptr;
    juce::Path cache;
};

class TitoEditor : public juce::AudioProcessorEditor,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit TitoEditor (TitoProcessor&);
    ~TitoEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    TitoProcessor& proc;
    SoundLibrary library;
    juce::Random rng;

    // top bar
    juce::TextButton loadBtn { "Load drum track" }, reanalyzeBtn { "Re-analyze" }, x2Btn { "x2" }, halfBtn { juce::CharPointer_UTF8 ("\xc3\xb7" "2") };
    juce::Label stemName, info, bpmLabel { {}, "BPM" }, sensLabel { {}, "Sensitivity" };
    juce::TextEditor bpmBox;
    juce::ComboBox tempoFixBox;          // 3:2 / 4:3 tempo traps (x2 / ÷2 are the buttons)
    juce::Slider sens { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::ComboBox feelBox, qgridBox;

    SongStrip strip;

    // pads
    juce::OwnedArray<PadButton> padButtons;
    juce::TextButton shuffleKitBtn { "Shuffle kit" }, lockBtn { "Lock pad" }, folderBtn { "Sound folder..." },
                     saveKitBtn { "Save kit" }, loadKitBtn { "Load kit" };
    juce::Label folderLabel;

    // pad editor
    juce::Label padTitle, padSource;
    PadWave wave;
    juce::Slider sStart, sLength, sPitch, sDecay, sVolume, sPan;
    juce::Label lStart { {}, "Start" }, lLength { {}, "Length" }, lPitch { {}, "Pitch" }, lDecay { {}, "Decay" },
                lVolume { {}, "Volume" }, lPan { {}, "Pan" }, lChoke { {}, "Choke" }, lOut { {}, "Output" };
    juce::TextButton loopBtn { "Loop" }, reverseBtn { "Reverse" }, loadSoundBtn { "Load sound..." },
                     shufflePadBtn { "Shuffle pad" }, revertBtn { "Song chop" }, factoryBtn { "Factory" };
    juce::ComboBox chokeBox, outBox;
    juce::TextButton linkBtn { "Link pads" };

    // groove layers
    juce::Label grooveHead { {}, "GROOVE LAYERS" }, gridLabel { {}, "Grid" }, swingLabel { {}, "Swing" },
                humanLabel { {}, "Drummer feel" }, styleLabel { {}, "Style" };
    juce::ComboBox gridBox, styleBox;
    juce::TextButton layBtn[5] { juce::TextButton ("Hat rolls"), juce::TextButton ("Ghost snares"), juce::TextButton ("Shaker"),
                                 juce::TextButton ("Perc"), juce::TextButton ("Open hats") };
    juce::Slider swingS { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight },
                 humanS { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    // fills
    juce::Label fillHead { {}, "DRUMMER FILLS" }, lenLabel { {}, "Length" }, intLabel { {}, "Intensity" };
    juce::TextButton whereBtn[3] { juce::TextButton ("Every 8 bars"), juce::TextButton ("Every 16 bars"), juce::TextButton ("Before hook") };
    juce::ComboBox lenBox;
    juce::Slider intS { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton crashBtn { "Crash after" }, roomBtn { "Make room" }, newFillsBtn { "New fills + layers" };

    // transport + exports
    juce::TextButton playBtn { "Play" }, openFolderBtn { "Open folder" };
    juce::ToggleButton hearBtn[4] { juce::ToggleButton ("Song"), juce::ToggleButton ("Layers"), juce::ToggleButton ("Fills"),
                                    juce::ToggleButton ("Original") };
    juce::Label timeLabel, status, dragHint, tempoWarn;
    double progress = -1.0;
    juce::ProgressBar progressBar { progress };
    DragTile songTile { "FULL SONG", juce::Colour (0xffe0a84a) }, layersTile { "GROOVE LAYERS", juce::Colour (0xff6fa8dc) },
             fillsTile { "FILLS", juce::Colour (0xffe07a9a) }, allTile { "ALL", juce::Colour (0xffc9c4b8) };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EngineJob> job;
    bool busy = false, pendingBuild = false;
    juce::int64 buildDue = 0;
    std::array<int, tito::numPads> lastHits {};
    bool loadingUi = false;

    juce::File engineExe() const;
    juce::File exportDir() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void chooseStem();
    void analyze();
    void requestBuild (int delayMs = 250);
    void buildNow();
    void applyBuild();
    void applyAnalysis (const juce::var& r);
    void selectPad (int i);
    void pushPadParams();
    void refreshPadEditor();
    void refresh();
    void shufflePad (int i);
    void loadToPad (int i, const juce::File& f, const juce::String& source);   // + its linked pads
    void updateTempoWarning();
    void saveKit();
    void loadKit();
    void chooseSoundFolder();
    void loadOriginal();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitoEditor)
};
