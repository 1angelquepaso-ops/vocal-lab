#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <set>

// The processor passes FL's audio straight through, untouched. Its only job is
// the preview player: when the user hits Play in the plugin window, the chunk
// plus the selected (unmuted) harmony voices are mixed into the output so they
// can be heard on this mixer channel. Nothing runs unless Play is on.
class HarmonizerProcessor : public juce::AudioProcessor
{
public:
    HarmonizerProcessor();
    ~HarmonizerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "vocal-lab Harmonizer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    // Preview control (message thread)
    void setPreview (juce::AudioBuffer<float>&& mono, double fileSampleRate);
    void startPreview();
    void stopPreview();
    void startPreviewFromHere();         // play from wherever the playhead is
    void seek (float fraction);          // 0..1, keeps playing if it was
    bool isPreviewing() const { return playing.load(); }
    float previewProgress() const;
    double previewSeconds() const { return previewRate > 0 ? preview.getNumSamples() / previewRate : 0.0; }
    std::atomic<bool> loop { false };

    // Session state lives here so it survives the window being closed
    juce::File sessionDir;
    juce::File vocalFile, beatFile;
    juce::Array<juce::var> options;  // one engine result object per option
    int currentOption = -1;
    std::map<juce::String, bool> muted, soloed;   // key: "<option id>/<voice>"
    std::set<int> kept;                           // option ids marked with a star
    juce::String status { "Load a dry vocal chunk (a hook or phrase) to start." };
    juce::String keyInfo;
    int voices = 1;
    int styleIndex = 0;
    bool leadInPreview = true;

private:
    juce::SpinLock lock;
    juce::AudioBuffer<float> preview;
    double previewRate = 44100.0, hostRate = 44100.0;
    double pos = 0.0;
    std::atomic<bool> playing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonizerProcessor)
};
