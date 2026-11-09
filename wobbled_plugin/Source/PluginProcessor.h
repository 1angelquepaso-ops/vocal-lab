#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include "Engine.h"

class WobbleProcessor : public juce::AudioProcessor
{
public:
    WobbleProcessor();
    ~WobbleProcessor() override { alive->store (false); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "WOBBLE-D"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState state;
    wb::Engine engine;

    // ---- shape (message thread owns `shape`; call shapeChanged() after editing it)
    wb::Shape shape;
    void shapeChanged();

    // ---- sample (message thread)
    bool loadSample (const juce::File& f, juce::String& error, bool touchParams = true);   // synchronous (tests, project load)
    void loadSampleAsync (const juce::File& f, std::function<void (bool, juce::String)> done);   // editor
    std::atomic<bool> loadingSample { false };
    void clearSample();
    const wb::SampleData* sampleForDisplay() const { return latestSample; }
    bool autoTrim = true, autoNormalize = true;

    void initPatch();
    void rerollChaos();

    // render-tool hooks
    wb::Params snapshot() const;
    juce::AudioPlayHead* testPlayHead = nullptr;

private:
    static juce::File sampleFolder();
    static std::unique_ptr<wb::SampleData> prepareSample (const juce::File&, bool trim, bool norm, juce::String& error);
    void finishLoad (std::unique_ptr<wb::SampleData>, bool touchParams);
    void setParamWithGesture (const char* pid, float plainValue);
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    void publishSample (std::unique_ptr<wb::SampleData> s);

    juce::SpinLock tableLock;
    float pendingTable[wb::Shape::tableSize] {};
    float pendingLoopStart = 0, pendingLoopEnd = 1;
    std::atomic<int> tableVersion { 1 };
    int engineTableVersion = 0;

    juce::SpinLock sampleLock;
    std::vector<std::unique_ptr<wb::SampleData>> sampleStore;
    const wb::SampleData* pendingSample = nullptr;
    const wb::SampleData* ackSample = nullptr;
    const wb::SampleData* latestSample = nullptr;
    bool sampleDirty = false;

    std::map<juce::String, std::atomic<float>*> raw;
    float rv (const char* id) const { return raw.at (id)->load(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WobbleProcessor)
};
