#pragma once
#include "PluginProcessor.h"
#include "Chain.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

// One engine command on a background thread; result back on the message thread.
class EngineJob : public juce::Thread
{
public:
    EngineJob (juce::StringArray a, std::function<void (juce::var)> done)
        : juce::Thread ("mirrorwall engine"), args (std::move (a)), callback (std::move (done)) {}
    ~EngineJob() override { stopThread (120000); }
    void run() override;

private:
    juce::StringArray args;
    std::function<void (juce::var)> callback;
};

// Runs the C++ chain on the tuned vocal, writes the processed WAV.
class RenderJob : public juce::Thread
{
public:
    RenderJob (juce::File in, juce::var preset, mw::Macros m, juce::File out, juce::File exportCopy,
               std::function<void (bool)> done)
        : juce::Thread ("mirrorwall render"), input (std::move (in)), P (std::move (preset)), macros (m),
          output (std::move (out)), exportFile (std::move (exportCopy)), callback (std::move (done)) {}
    ~RenderJob() override { stopThread (60000); }
    void run() override;

private:
    juce::File input;
    juce::var P;
    mw::Macros macros;
    juce::File output, exportFile;
    std::function<void (bool)> callback;
};

// Waveform lane with playhead; the processed lane has a grab handle to drag into FL.
class Lane : public juce::Component, private juce::ChangeListener
{
public:
    Lane (juce::AudioFormatManager&, juce::AudioThumbnailCache&, juce::String title, juce::Colour, bool draggable);
    ~Lane() override;
    void setFile (const juce::File& f);
    void setExport (const juce::File& f) { exportFile = f; repaint(); }
    std::function<void (float)> onSeek;
    std::function<float()> playhead;
    bool active = true;

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; pressOnHandle = false; }

private:
    juce::AudioThumbnail thumb;
    juce::String title;
    juce::Colour colour;
    const bool canDrag;
    juce::File exportFile;
    bool hover = false, dragging = false, pressOnHandle = false;
    juce::Rectangle<int> waveArea() const;
    juce::Rectangle<int> handleArea() const;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
};

class Knob : public juce::Component
{
public:
    Knob (const juce::String& name, double lo, double hi, double def, const juce::String& suffix = {});
    juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
    juce::Label label;
    void resized() override;
};

class MirrorEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit MirrorEditor (MirrorProcessor&);
    ~MirrorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    MirrorProcessor& proc;
    juce::AudioFormatManager formats;
    juce::AudioThumbnailCache thumbCache { 8 };

    // source
    juce::TextButton loadRaw { "Load raw vocal" };
    juce::Label rawName, info, micLabel { {}, "Mic" };
    juce::ComboBox micBox;
    juce::Array<juce::var> micProfiles;
    // chain
    juce::Label chainLabel { {}, "CHAIN" }, emuLabel { {}, "EMULATOR" };
    juce::ComboBox presetBox;
    juce::TextButton loadRef { "Load reference" }, buildBtn { "Build preset" }, deletePreset { "Delete" };
    juce::Label refName;
    // tuning
    juce::Label tuneLabel { {}, "AUTOTUNE" }, keyLabel { {}, "Key" }, scaleLabel { {}, "Scale" };
    juce::ComboBox keyBox, scaleBox;
    juce::TextButton keyFromBeat { "Key from beat" };
    Knob speed { "Retune", 1, 200, 20, " ms" }, amount { "Amount", 0, 1, 1 }, humanize { "Humanize", 0, 1, 0 };
    // tone knobs
    juce::Label toneLabel { {}, "TWEAK" }, preLabel { {}, "Preamp" };
    juce::ComboBox preBox;
    Knob drive { "Drive", -6, 12, 0, " dB" }, comp { "Compression", 0, 2, 1 }, deess { "De-ess", 0, 2, 1 },
         gateK { "Gate", 0, 1, 1 }, fx { "FX / Width", 0, 2, 1 }, delay { "Delay", 0, 2, 1 },
         reverb { "Reverb", 0, 2, 1 }, out { "Output", -12, 6, 0, " dB" };
    // lanes + transport
    std::unique_ptr<Lane> rawLane, mixLane;
    juce::TextButton play { "Play" }, loopBtn { "Loop" }, abBtn { "Hearing: MIRRORWALL" }, openFolder { "Open folder" };
    juce::Label timeLabel, status;
    double progress = -1.0;
    juce::ProgressBar progressBar { progress };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<EngineJob> job;
    std::unique_ptr<RenderJob> renderJob;
    bool busy = false, rendering = false, needTune = true, needRender = false, pendingRender = false;
    juce::String tunedKey;               // settings the current tuned file was made with
    juce::Array<juce::File> presetFiles;
    juce::var preset;                    // current preset (parsed)
    int renderGeneration = 0;
    juce::uint32 renderDue = 0;

    juce::File engineExe() const;
    juce::File stockDir() const;
    juce::File userDir() const;
    juce::File micDir() const;
    bool presetIsStock() const;
    int presetIndex = -1;
    juce::File exportDir() const;
    juce::File tunedFile() const { return proc.sessionDir.getChildFile ("tuned.wav"); }
    juce::File mixFile() const { return proc.sessionDir.getChildFile ("mirrorwall.wav"); }
    juce::File exportFile() const;
    void runEngine (juce::StringArray args, std::function<void (juce::var)> onOk);
    void scanPresets (const juce::String& select = {});
    void selectPreset (int index, bool resetTuning);
    juce::String tuneSignature() const;
    void requestRender (int delayMs = 250);
    void update();                       // tune if needed, else render
    void startRender();
    void rebuildPreview();
    void chooseRaw();
    void chooseRef();
    void chooseBeat();
    void buildFromReference();
    void syncKnobsToProc();
    void refresh();
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirrorEditor)
};
