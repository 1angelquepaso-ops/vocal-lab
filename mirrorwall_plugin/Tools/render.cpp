// mw_render in.wav preset.json out.wav  — renders the C++ chain (parity checks)
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/Chain.h"

int main (int argc, char** argv)
{
    if (argc < 4) { std::puts ("usage: mw_render in.wav preset.json out.wav"); return 1; }
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (juce::File (argv[1])));
    if (! r) return 2;
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    mw::Buf x ((size_t) b.getNumSamples());
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        double s = 0;
        for (int c = 0; c < b.getNumChannels(); ++c) s += b.getSample (c, i);
        x[(size_t) i] = s / b.getNumChannels();
    }
    const auto P = juce::JSON::parse (juce::File (argv[2]));
    mw::Buf L, R;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    mw::process (x, P, mw::Macros(), L, R);
    std::printf ("render %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
    juce::AudioBuffer<float> o (2, (int) L.size());
    for (size_t i = 0; i < L.size(); ++i) { o.setSample (0, (int) i, (float) L[i]); o.setSample (1, (int) i, (float) R[i]); }
    juce::File out (argv[3]);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), 44100.0, 2, 32, {}, 0));
    w->writeFromAudioSampleBuffer (o, 0, o.getNumSamples());
    return 0;
}
