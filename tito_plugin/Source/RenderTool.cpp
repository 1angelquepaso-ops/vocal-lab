// tito_render: offline check of the real plugin code (no audio device needed).
//   tito_render preview <build.json> <out.wav> [seconds]   - the window's Play button path
//   tito_render midi    <file.mid>   <out.wav> [seconds]   - FL's piano roll path (notes 36-51)
#include "PluginProcessor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 4) { std::puts ("usage: tito_render preview|midi <in> <out.wav> [seconds]"); return 1; }
    const juce::String mode (argv[1]);
    const juce::File in (argv[2]), out (argv[3]);
    const double seconds = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 20.0;
    const double sr = 44100.0;
    const int block = 512;

    TitoProcessor p;
    p.setRateAndBufferSizeDetails (sr, block);
    p.prepareToPlay (sr, block);
    p.loadFactoryKit (false);

    juce::MidiMessageSequence midiSeq;
    if (mode == "preview")
    {
        const auto b = juce::JSON::parse (in);
        std::vector<TitoProcessor::Event> ev;
        int layer = 0;
        for (auto* k : { "song", "layers", "fills" })
        {
            if (auto* arr = b["notes"][k].getArray())
                for (auto& n : *arr)
                    ev.push_back ({ (double) n[0], (int) n[1], (float) (int) n[2] / 127.0f, layer });
            ++layer;
        }
        p.loopPreview = false;
        p.setSequence (std::move (ev), (double) b["bpm"], (double) b["length_beats"], (double) b["t0"]);
        p.play (true);
    }
    else
    {
        juce::FileInputStream fis (in);
        juce::MidiFile mf;
        if (! mf.readFrom (fis)) { std::puts ("bad midi"); return 1; }
        mf.convertTimestampTicksToSeconds();
        for (int t = 0; t < mf.getNumTracks(); ++t)
            midiSeq.addSequence (*mf.getTrack (t), 0.0);
        midiSeq.updateMatchedPairs();
    }

    const int total = (int) (seconds * sr);
    juce::AudioBuffer<float> all (2, total);
    all.clear();
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    int ev = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), buf.getNumChannels(), n);
        juce::MidiBuffer mb;
        while (ev < midiSeq.getNumEvents())
        {
            const auto& m = midiSeq.getEventPointer (ev)->message;
            const int at = (int) (m.getTimeStamp() * sr) - pos;
            if (at >= n) break;
            if (m.isNoteOnOrOff()) mb.addEvent (m, juce::jmax (0, at));
            ++ev;
        }
        p.processBlock (view, mb);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, pos, view, juce::jmin (c, view.getNumChannels() - 1), 0, n);
    }
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), sr, 2, 24, {}, 0));
    if (w == nullptr) return 1;
    w->writeFromAudioSampleBuffer (all, 0, total);
    std::printf ("rendered %.1f s, peak %.3f, rms %.4f\n", seconds, all.getMagnitude (0, total), all.getRMSLevel (0, 0, total));
    return 0;
}
