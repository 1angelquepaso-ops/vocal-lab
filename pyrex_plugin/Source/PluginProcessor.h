#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

// PYREX MAGIK: an FX slot that never touches the audio (pass-through). The window is the tool:
// drop one Suno song's stems -> one clock + one key -> render locked to FL's BPM -> drag out.
// It only listens to FL for the project BPM and sample rate.
class PyrexProcessor : public juce::AudioProcessor
{
public:
    PyrexProcessor();
    ~PyrexProcessor() override = default;

    void prepareToPlay (double sampleRate, int) override { hostRate.store (sampleRate); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PYREX MAGIK"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    std::atomic<double> hostBpm { 0.0 }, hostRate { 0.0 };

    // ---- session (message thread)
    juce::StringArray stems;           // dropped WAV paths
    juce::File sessionDir;             // temp: analysis.json, render.json, progress.txt
    juce::var analysis, render;        // engine results
    double bpmOverride = 0.0;          // ÷2 / ×2 / typed song BPM (0 = measured)
    double targetBpm = 0.0;            // 0 = follow FL
    bool bpmLock = true, keyTranspose = false;
    juce::String keyName;              // the user's confirmed original key ("" = detected)
    int transpose = 0;                 // semitones (Key Transpose)
    double nudge = 0.0;                // bar 1 moved in beats (whole; halves for a drum-less song)
    juce::String status { "Drop one song's Suno stems here (up to 12 WAVs)." };

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PyrexProcessor)
};
