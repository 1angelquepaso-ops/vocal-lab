// me_render in.wav out.wav [param=value ...] [learn=1] [rt=0|1]
// Runs the real MEX-EQ processor offline. Band params: b1_type=6 b1_freq=1000 ... (real values).
// learn=1 : play the whole file once with LEARN on, create the bands, then render.
// rt=1    : behave like real-time playback (AUTO -> LIVE path); default = offline export.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include <thread>

int main (int argc, char** argv)
{
    if (argc < 3) { std::puts ("usage: me_render in.wav out.wav [b1_used=1 b1_type=6 ...] [learn=1] [rt=1]"); return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[1])));
    if (! r) { std::puts ("cannot read input"); return 2; }
    const int n = (int) r->lengthInSamples;
    juce::AudioBuffer<float> in (2, n);
    r->read (&in, 0, n, 0, true, true);
    MexProcessor proc;
    bool learn = false, realtime = false;
    for (int i = 3; i < argc; ++i)
    {
        const juce::String kv (argv[i]);
        const auto k = kv.upToFirstOccurrenceOf ("=", false, false);
        const float v = kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (k == "learn") { learn = v > 0.5f; continue; }
        if (k == "rt") { realtime = v > 0.5f; continue; }
        if (auto* p = proc.state.getParameter (k)) p->setValueNotifyingHost (p->convertTo0to1 (v));
        else { std::printf ("unknown parameter %s\n", k.toRawUTF8()); return 3; }
    }
    const int bs = 512;
    proc.setNonRealtime (! realtime);
    proc.setPlayConfigDetails (2, 2, r->sampleRate, bs);
    proc.prepareToPlay (r->sampleRate, bs);
    juce::MidiBuffer midi;
    auto runOnce = [&] (juce::AudioBuffer<float>* out)
    {
        for (int s = 0; s < n; s += bs)
        {
            const int len = juce::jmin (bs, n - s);
            juce::AudioBuffer<float> b (2, len);
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, in, c, s, len);
            proc.processBlock (b, midi);
            if (out) for (int c = 0; c < 2; ++c) out->copyFrom (c, s, b, c, 0, len);
            if (proc.isLearning() && (s / bs) % 8 == 0) proc.learnPump();
        }
    };
    if (learn)
    {
        proc.startLearn();
        runOnce (nullptr);
        proc.finishLearn();
        std::printf ("{\"learn\": \"%s\"}\n", proc.lastLearnSummary.toRawUTF8());
        for (int i = 0; i < mx::NBANDS; ++i)
        {
            mx::BandParams b; proc.readBand (i, b);
            if (b.used) std::printf ("  band %d: %.0f Hz Q %.2f range %.1f dB\n", i + 1, b.spec.freq, b.spec.q, b.range);
        }
        proc.prepareToPlay (r->sampleRate, bs);
    }
    proc.rebuildNow();
    std::this_thread::sleep_for (std::chrono::milliseconds (900));    // let linear-phase FIRs load
    juce::AudioBuffer<float> out (2, n);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    runOnce (&out);
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::printf ("{\"mode\": \"%s\", \"latency\": %d, \"render_ms\": %.0f, \"realtime_x\": %.1f}\n", proc.modeName().toRawUTF8(),
                 proc.getLatencySamples(), ms, (n / r->sampleRate * 1000.0) / ms);
    juce::File o (argv[2]); o.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (o), r->sampleRate, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer (out, 0, n);
    return 0;
}
