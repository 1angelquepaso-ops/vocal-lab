#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Engine.h"
#include "Learn.h"

class MexProcessor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
{
public:
    MexProcessor();
    ~MexProcessor() override { cancelPendingUpdate(); stopTimer(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MEX-EQ"; }
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

    // --- helpers shared with the editor
    static juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }
    juce::RangedAudioParameter* bandParam (int band, const char* name) { return state.getParameter (pid (band, name)); }
    void setParam (const juce::String& id, float realValue);
    void readBand (int i, mx::BandParams& out) const;
    void readGlobals (mx::GlobalParams& g) const;
    int findFreeBand() const;
    void resetBand (int i);
    float bandUiOffset (int i) const { return coreBase.rt[i].uiOffset.load() + coreOS.rt[i].uiOffset.load(); }

    // LEARN
    void startLearn();
    void finishLearn();                       // analyse + create bands (message thread / tools)
    void learnPump() { learner.pump(); }      // tools: consume audio like the 10 Hz timer does
    bool isLearning() const { return learner.learning.load(); }
    double learnSeconds() const { return learnSec.load(); }
    juce::String lastLearnSummary;

    // analyzer feed (post = output, pre = input), mono
    static constexpr int AN = 1 << 13;
    std::vector<float> anPre, anPost;
    std::atomic<int> anPos { 0 };
    double getSr() const { return sampleRateHz; }
    int modeLatency() const { return reportedLatency; }
    juce::String modeName() const;

    void rebuildNow();                        // latency + linear-phase FIR (message thread / tools)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void handleAsyncUpdate() override { rebuildNow(); }
    void timerCallback() override;

    mx::Core coreBase, coreOS;
    mx::Learner learner;
    std::atomic<double> learnSec { 0 };
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    juce::dsp::Convolution convLR { juce::dsp::Convolution::NonUniform { 512 } };
    juce::dsp::Convolution convMS { juce::dsp::Convolution::NonUniform { 512 } };
    juce::AudioBuffer<float> scUp, delayBuf;
    int delayPos = 0;
    double sampleRateHz = 44100;
    int maxBlock = 512;
    int reportedLatency = -1, osLatency = 0;
    bool prepared = false;
    std::atomic<juce::int64> staticHash { 0 };
    juce::int64 builtHash = -1;
    int builtFirN = 0;
    mx::BandParams bp[mx::NBANDS];

    // cached raw parameter pointers
    struct BandPtrs { std::atomic<float>* v[18]; };
    BandPtrs ptrs[mx::NBANDS];
    std::atomic<float> *pQuality, *pPhase, *pDrive, *pOut, *pStrength;

    int currentMode() const;   // 0 LIVE, 1 Natural, 2 Linear, 3 AUTO-live (delay-matched)
    int firSizeFor (int phase) const;
    juce::int64 computeStaticHash() const;
    void designFir (int N);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MexProcessor)
};
