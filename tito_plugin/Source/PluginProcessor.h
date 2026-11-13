#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Pads.h"
#include <algorithm>
#include <vector>

// TITO PUENTE: an instrument. FL's piano roll (notes 36-51) plays the 16 pads.
// The window can also preview the extracted song / groove layers / fills through
// the same pads, and A/B against the original drum stem.
class TitoProcessor : public juce::AudioProcessor
{
public:
    TitoProcessor();
    ~TitoProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "TITO PUENTE"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---------------------------------------------------------------- pads (message thread)
    tito::PadEngine pads;
    struct PadMeta { juce::String source, chopFile; bool locked = false; int hitsInSong = 0; };
    std::array<PadMeta, tito::numPads> meta;
    bool loadPadFile (int pad, const juce::File& f, const juce::String& source);
    juce::File padFile (int pad) const;
    void audition (int pad, float vel) { auditionQueue.push (pad, vel); }

    // ---------------------------------------------------------------- preview sequencer
    struct Event { double beat; int pad; float vel; int layer; };   // layer 0 song, 1 grooves, 2 fills
    void setSequence (std::vector<Event> ev, double bpm, double lengthBeats, double t0);
    void setOriginal (juce::AudioBuffer<float>&& audio, double rate);
    bool hasOriginal() const { return originalLoaded.load(); }
    void play (bool on);
    bool isPlaying() const { return playing.load(); }
    void seekBeats (double b) { seekTo.store (b); }
    double playheadBeats() const { return playhead.load(); }
    std::atomic<bool> hear[4] { true, true, true, false };          // song, grooves, fills, original
    std::atomic<bool> loopPreview { true };

    // ---------------------------------------------------------------- session state (message thread)
    juce::File sessionDir, stemFile, soundFolder;
    juce::var analysis, build;
    juce::String status { "Load a drum stem (2-6 min) to rebuild its MIDI on the pads." };
    int selectedPad = 0;
    // settings
    double bpmOverride = 0.0, sensitivity = 0.5;
    int feel = 0, qgrid = 0, style = 0, grid = 0, fillLen = 2, seed = 1;
    bool lay[5] { false, false, false, false, false };       // hats, ghost, shaker, perc, openhat
    bool fillWhere[3] { false, false, false };               // every8, every16, prehook
    double swing = 0.0, human = 0.6, intensity = 0.6;
    bool crash = true, room = true;

    // FL's tempo (read every audio block) - the window warns when it doesn't match the song
    std::atomic<double> hostBpm { 0.0 };
    // pads that hold the same drum (engine-detected); swapping one swaps them all while linkPads
    std::vector<std::vector<int>> padLinks;
    bool linkPads = true;
    std::vector<int> linkedWith (int pad) const
    {
        for (const auto& g : padLinks)
            if (std::find (g.begin(), g.end(), pad) != g.end())
            {
                std::vector<int> out;
                for (int q : g) if (q != pad) out.push_back (q);
                return out;
            }
        return {};
    }
    void readLinks (const juce::var& analysisResult)
    {
        padLinks.clear();
        if (auto* arr = analysisResult["links"].getArray())
            for (auto& g : *arr)
                if (auto* ga = g.getArray())
                {
                    std::vector<int> v;
                    for (auto& x : *ga)
                        if ((int) x >= 0 && (int) x < tito::numPads) v.push_back ((int) x);
                    if (v.size() > 1) padLinks.push_back (std::move (v));
                }
    }

    juce::AudioFormatManager formats;
    juce::File factoryDir() const;
    void loadFactoryKit (bool onlyEmpty);

private:
    static BusesProperties makeBuses();
    // lock-free-ish handoff of pad auditions from the UI to the audio thread
    struct AuditionQueue
    {
        std::array<std::pair<int, float>, 32> items {};
        std::atomic<int> w { 0 }, r { 0 };
        void push (int p, float v) { const int i = w.load(); items[(size_t) (i % 32)] = { p, v }; w.store (i + 1); }
        bool pop (std::pair<int, float>& out)
        {
            const int i = r.load();
            if (i >= w.load()) return false;
            out = items[(size_t) (i % 32)];
            r.store (i + 1);
            return true;
        }
    } auditionQueue;

    juce::SpinLock seqLock;
    std::vector<Event> seq;
    double seqBpm = 120.0, seqLength = 16.0, seqT0 = 0.0;
    std::atomic<bool> playing { false }, originalLoaded { false };
    std::atomic<double> playhead { 0.0 }, seekTo { -1.0 };
    double hostRate = 44100.0;
    size_t nextEvent = 0;

    juce::SpinLock origLock;
    juce::AudioBuffer<float> original;
    double originalRate = 44100.0;

    juce::AudioBuffer<float> scratch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TitoProcessor)
};
