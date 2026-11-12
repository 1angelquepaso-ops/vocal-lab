#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

class DreamProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
{
public:
    DreamProcessor();
    ~DreamProcessor() override { cancelPendingUpdate(); stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DREAMMIC"; }
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

    juce::AudioProcessorValueTreeState state;
    dm::Engine engine;
    dm::Params currentParams() const;
    void rebuildNow();                 // FIR + latency + level match (message thread / offline tools)

    // ---- presets (v2) ----
    struct Factory { const char* name; std::vector<std::pair<const char*, float>> values; };
    static const std::vector<Factory>& factoryPresets();
    static juce::File userPresetDir();         // Documents\DREAMMIC\Presets
    juce::StringArray userPresetNames() const;
    void loadFactory (int index);
    bool loadUser (const juce::String& name);
    bool saveUser (const juce::String& name);
    juce::String presetName;                   // shown in the preset box (saved with the project)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleAsyncUpdate() override { startTimer (250); }     // debounce knob drags
    void timerCallback() override { stopTimer(); rebuildNow(); }
    void setParam (const juce::String& id, float value);
    juce::ValueTree soundTree() const;
    void applySoundTree (const juce::ValueTree& t);
    static bool sameSound (const dm::Params& a, const dm::Params& b);
    int reportedLatency = -1;
    bool prepared = false;
    dm::Params matchedFor;
    bool matchValid = false;
    double rate = 44100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DreamProcessor)
};
