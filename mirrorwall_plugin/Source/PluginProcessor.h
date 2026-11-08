#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

// MIRRORWALL passes FL's audio straight through. Its only sound is the preview
// player: when Play is on in the plugin window, the raw or processed vocal is mixed into this channel. Nothing else runs.
class MirrorProcessor : public juce::AudioProcessor
{
public:
    MirrorProcessor();
    ~MirrorProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MIRRORWALL"; }
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
    void setPreview (juce::AudioBuffer<float>&& audio, double fileSampleRate);
    void startPreviewFromHere();
    void stopPreview();
    void seek (float fraction);
    bool isPreviewing() const { return playing.load(); }
    float previewProgress() const;
    double previewSeconds() const { return previewRate > 0 ? preview.getNumSamples() / previewRate : 0.0; }
    std::atomic<bool> loop { true };

    // Session state lives here so it survives the window being closed
    juce::File sessionDir, rawFile, beatFile, refFile;
    juce::String presetName { "LEAD BLEND" };
    juce::String status { "Load your raw vocal, pick a chain (or build one with the Emulator)." };
    int keyIndex = 0;                     // 0 = auto, 1..12 = C..B
    int scaleIndex = 1;                   // major, minor, harmonic minor, phrygian, chromatic
    double tuneSpeed = -1, tuneAmount = -1, tuneHumanize = -1;   // <0 = preset default
    double knobComp = 1, knobReverb = 1, knobDelay = 1, knobFx = 1, knobDeess = 1,
           knobDrive = 0, knobOut = 0, knobGate = 1;
    bool hearProcessed = true;
    int micIndex = 1;                     // 0 = off, else index into the mic profiles

private:
    juce::SpinLock lock;
    juce::AudioBuffer<float> preview;
    double previewRate = 44100.0, hostRate = 44100.0;
    double pos = 0.0;
    std::atomic<bool> playing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirrorProcessor)
};
