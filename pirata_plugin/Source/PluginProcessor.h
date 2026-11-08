#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

// PIRATA passes FL's audio straight through. Its only sound is the preview
// player: when Play is on in the plugin window, the original sample and/or
// the grand-piano renders are mixed into this channel. Nothing else runs.
class PirataProcessor : public juce::AudioProcessor
{
public:
    PirataProcessor();
    ~PirataProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PIRATA"; }
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
    juce::File sessionDir, sampleFile;
    juce::var analysis, build;          // last engine results
    juce::String status { "Load a sample (piano loop, pads, chords) to start." };
    int detail = 1;                     // 0 simple, 1 as played, 2 rich
    int mode = 0;                       // 0 off .. 5 arp
    int registerIndex = 1;
    double density = 0.5;
    int arpPattern = 0, arpRate = 1, arpOctaves = 2;
    int seed = 1;
    double bpmOverride = 0.0;
    bool hearOriginal = false, hearChords = true, hearMelody = true;

private:
    juce::SpinLock lock;
    juce::AudioBuffer<float> preview;
    double previewRate = 44100.0, hostRate = 44100.0;
    double pos = 0.0;
    std::atomic<bool> playing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PirataProcessor)
};
