#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Live.h"

// MIRRORWALL 2.0
//   LIVE: FL's audio runs through the selected chain in real time (any preset in the bank).
//   HQ:   the raw vocal file is tuned + rendered offline with the same engine; the plugin plays the preview.
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
    double getTailLengthSeconds() const override { return liveMode.load() ? 4.0 : 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Preview control (message thread)
    void setPreview (juce::AudioBuffer<float>&& audio, double fileSampleRate);
    void startPreviewFromHere();
    void stopPreview();
    void seek (float fraction);
    bool isPreviewing() const { return playing.load(); }
    float previewProgress() const;
    double previewSeconds() const { return previewRate > 0 ? preview.getNumSamples() / previewRate : 0.0; }
    std::atomic<bool> loop { true };

    // LIVE engine (message thread)
    void setLivePreset (const juce::var& effectivePreset);   // rebuilds the live chain
    void setLiveMode (bool on);
    bool isLive() const { return liveMode.load(); }
    double liveInputDb() const { return liveIn.load(); }

    // Session state lives here so it survives the window being closed
    juce::File sessionDir, rawFile, beatFile, refFile;
    juce::String presetName { "LEAD BLEND" };
    juce::String status { "Load your raw vocal, pick a chain (or build one with the Emulator)." };
    int keyIndex = 0;                     // 0 = auto, 1..12 = C..B
    int scaleIndex = 1;                   // major, minor, harmonic minor, phrygian, chromatic
    double tuneSpeed = -1, tuneAmount = -1, tuneHumanize = -1;   // <0 = preset default
    std::atomic<double> knobComp { 1 }, knobReverb { 1 }, knobDelay { 1 }, knobFx { 1 }, knobDeess { 1 },
                        knobDrive { 0 }, knobOut { 0 }, knobGate { 1 }, inGainDb { 0 };
    std::atomic<bool> autoInput { true };
    int verbType = 0;                     // 0 = room (as fitted), 1 = plate
    bool hearProcessed = true;
    int micIndex = 1;                     // 0 = off, else index into the mic profiles
    mwl::Macros macros() const;

private:
    juce::SpinLock lock;
    juce::AudioBuffer<float> preview;
    double previewRate = 44100.0, hostRate = 44100.0;
    double pos = 0.0;
    std::atomic<bool> playing { false };

    // live chain
    std::atomic<bool> liveMode { false };
    std::atomic<double> liveIn { 0.0 };
    juce::SpinLock engineLock;
    std::unique_ptr<mwl::Engine> engine;
    juce::String livePresetJson;
    std::vector<float> mono, outL, outR;
    void rebuildEngine();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MirrorProcessor)
};
