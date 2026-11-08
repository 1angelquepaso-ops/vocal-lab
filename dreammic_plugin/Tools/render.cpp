// dm_render in.wav out.wav [key=value ...]  — runs the real plugin processor offline
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 3) { std::puts ("usage: dm_render in.wav out.wav [mic=0 pre=0 mode=0 ...]"); return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[1])));
    if (! r) return 2;
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> in (2, n);
    r->read (&in, 0, n, 0, true, true);
    DreamProcessor proc;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String kv (argv[i]);
        if (auto* p = proc.state.getParameter (kv.upToFirstOccurrenceOf ("=", false, false)))
            p->setValueNotifyingHost (p->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()));
    }
    const int bs = 512;
    proc.setPlayConfigDetails (2, 2, r->sampleRate, bs);
    proc.prepareToPlay (r->sampleRate, bs);
    proc.rebuildNow();
    std::this_thread::sleep_for (std::chrono::milliseconds (600));   // let the convolution load
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> out (2, n);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int s = 0; s < n; s += bs)
    {
        const int len = juce::jmin (bs, n - s);
        juce::AudioBuffer<float> b (2, len);
        for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in, c, s, len);
        proc.processBlock (b, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, s, b, c, 0, len);
    }
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::printf ("{\"latency\": %d, \"render_ms\": %.0f, \"realtime_x\": %.1f}\n", proc.getLatencySamples(), ms,
                 (n / r->sampleRate * 1000.0) / ms);
    juce::File o (argv[2]);
    o.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (o), r->sampleRate, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, n);
    return 0;
}
