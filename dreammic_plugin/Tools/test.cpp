// dm_test — numeric checks for DREAMMIC 2.0 (not shipped). Prints one JSON-ish report.
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include <thread>

using namespace dm;

// v1 design (RBJ bilinear) kept here only to show what v2 fixed
static std::complex<double> rbj (FType t, double f, double q, double gDb, double sr, double fq)
{
    const double A = std::pow (10.0, gDb / 40.0), w0 = 2.0 * PI * std::min (f, sr * 0.49) / sr;
    const double cw = std::cos (w0), sw = std::sin (w0), al = sw / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;
    switch (t)
    {
        case FType::Peak: b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A; break;
        case FType::LowShelf: { const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) - (A - 1) * cw + sa); b1 = 2 * A * ((A - 1) - (A + 1) * cw); b2 = A * ((A + 1) - (A - 1) * cw - sa);
            a0 = (A + 1) + (A - 1) * cw + sa; a1 = -2 * ((A - 1) + (A + 1) * cw); a2 = (A + 1) + (A - 1) * cw - sa; break; }
        case FType::HighShelf: { const double sa = 2 * std::sqrt (A) * al;
            b0 = A * ((A + 1) + (A - 1) * cw + sa); b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - sa);
            a0 = (A + 1) - (A - 1) * cw + sa; a1 = 2 * ((A - 1) - (A + 1) * cw); a2 = (A + 1) - (A - 1) * cw - sa; break; }
        case FType::HP: b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
        default: b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + al; a1 = -2 * cw; a2 = 1 - al; break;
    }
    const auto z = std::polar (1.0, -2.0 * PI * fq / sr);
    return (b0 + b1 * z + b2 * z * z) / (a0 + a1 * z + a2 * z * z);
}
static double v1MicDb (int mic, double sr, double f)
{
    std::complex<double> h (1, 0);
    const auto& m = MICS[mic];
    for (int i = 0; i < m.numFilters; ++i) h *= rbj (m.filters[i].t, m.filters[i].f, m.filters[i].q, m.filters[i].g, sr, f);
    return 20 * std::log10 (std::abs (h));
}
static double analogMicDb (int mic, double f)
{
    std::vector<mx::ASec> v; const auto& m = MICS[mic];
    for (int i = 0; i < m.numFilters; ++i) addSections (m.filters[i], 1.0, v);
    return 20 * std::log10 (std::abs (mx::analogResponse (v, f)));
}
static double liveMicDb (int mic, double sr, double f)
{
    std::vector<mx::ASec> v; const auto& m = MICS[mic];
    for (int i = 0; i < m.numFilters; ++i) addSections (m.filters[i], 1.0, v);
    std::vector<mx::BQ> d; mx::toDigital (v, sr, d);
    return 20 * std::log10 (std::abs (mx::digitalResponse (d, f, sr)));
}

static juce::AudioBuffer<float> sine (double f, double dbfs, double sr, int n)
{
    juce::AudioBuffer<float> b (2, n);
    const double a = std::pow (10.0, dbfs / 20.0);
    for (int i = 0; i < n; ++i) { const float v = (float) (a * std::sin (2 * PI * f * i / sr)); b.setSample (0, i, v); b.setSample (1, i, v); }
    return b;
}

static void run (DreamProcessor& proc, juce::AudioBuffer<float>& io, int bs = 512)
{
    juce::MidiBuffer midi;
    for (int s = 0; s < io.getNumSamples(); s += bs)
    {
        const int len = juce::jmin (bs, io.getNumSamples() - s);
        juce::AudioBuffer<float> b (2, len);
        for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, io, c, s, len);
        proc.processBlock (b, midi);
        for (int c = 0; c < 2; ++c) io.copyFrom (c, s, b, c, 0, len);
    }
}

static void set (DreamProcessor& p, const char* id, float v)
{
    if (auto* prm = p.state.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

// harmonic levels (dB re fundamental) by single-bin DFT over the 2nd half
static void harmonics (const juce::AudioBuffer<float>& b, double f0, double sr, double& h2, double& h3, double& thd)
{
    const int n0 = b.getNumSamples() / 2, n = b.getNumSamples() - n0;
    auto mag = [&] (double f)
    {
        std::complex<double> s (0, 0);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * PI * i / (n - 1));
            s += std::polar ((double) b.getSample (0, n0 + i) * w, -2 * PI * f * (n0 + i) / sr);
        }
        return std::abs (s);
    };
    const double a1 = mag (f0), a2 = mag (2 * f0), a3 = mag (3 * f0);
    double e = 0; for (int k = 2; k <= 9; ++k) { const double a = mag (k * f0); e += a * a; }
    h2 = 20 * std::log10 (a2 / a1 + 1e-12); h3 = 20 * std::log10 (a3 / a1 + 1e-12); thd = 100 * std::sqrt (e) / a1;
}


// Synthetic sung line: gliding pitch + vibrato, 30 harmonics through 3 moving formants, syllable gaps,
// an "s" burst every ~1.3 s. Normalised to -18 dBFS RMS. Good enough to exercise comps, de-esser, level match.
static juce::AudioBuffer<float> synthVocal (double sr, int n, int seed)
{
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (seed);
    std::vector<dm::Biquad> f1, f2, f3, sib = dm::toBiquads ({ mx::secHP2 (2 * PI * 5500.0, 0.707), mx::secLP2 (2 * PI * 9000.0, 0.707) }, sr);
    double ph = 0, e = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        if (i % 256 == 0)
        {   // vowel moves between "ah", "ee", "oo"
            const double v = 0.5 + 0.5 * std::sin (2 * PI * 0.7 * t + seed);
            auto bp = [&] (double f, double q) { mx::ASec a; a.b1 = 2 * PI * f / q; a.a2 = 1; a.a1 = 2 * PI * f / q; a.a0 = std::pow (2 * PI * f, 2); a.b0 = 0; a.b2 = 0; return a; };
            auto keep = [] (std::vector<dm::Biquad>& o, std::vector<dm::Biquad> nw) { if (o.size() == nw.size()) for (size_t k = 0; k < o.size(); ++k) { nw[k].z1[0] = o[k].z1[0]; nw[k].z2[0] = o[k].z2[0]; } o = nw; };
            keep (f1, dm::toBiquads ({ bp (300 + 450 * v, 6) }, sr));
            keep (f2, dm::toBiquads ({ bp (2300 - 1200 * v, 8) }, sr));
            keep (f3, dm::toBiquads ({ bp (2900, 10) }, sr));
        }
        const double f0 = 165 + 45 * std::sin (2 * PI * 0.31 * t + seed) + 3 * std::sin (2 * PI * 5.5 * t);
        ph += f0 / sr;
        double src = 0;
        for (int k = 1; k <= 30 && k * f0 < 0.45 * sr; ++k) src += std::sin (2 * PI * k * ph) / k;
        double v = 0, a = src; for (auto& q : f1) a = q.tick (a, 0); v += a;
        a = src; for (auto& q : f2) a = q.tick (a, 0); v += 0.5 * a;
        a = src; for (auto& q : f3) a = q.tick (a, 0); v += 0.25 * a;
        v += 0.02 * src;
        const double sp = std::fmod (2.6 * t, 1.0);
        const double env = sp < 0.82 ? std::pow (std::sin (PI * sp / 0.82), 0.6) : 0.0;
        double s = 0;
        if (std::fmod (t, 1.3) < 0.11) { s = rng.nextFloat() * 2 - 1; for (auto& q : sib) s = q.tick (s, 0); s *= 3.0; }
        const float out = (float) (v * env + s);
        b.setSample (0, i, out); b.setSample (1, i, out); e += (double) out * out;
    }
    b.applyGain ((float) (std::pow (10.0, -18 / 20.0) / std::sqrt (e / n)));
    return b;
}
static double kLoudness (const juce::AudioBuffer<float>& b, double sr, int from)
{
    auto k = dm::Engine::kWeighting (sr); double s = 0; int c = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) { double v = b.getSample (0, i); for (auto& f : k) v = f.tick (v, 0); if (i >= from) { s += v * v; ++c; } }
    return 10 * std::log10 (s / std::max (1, c) + 1e-20);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    {
        auto v = synthVocal (44100, 44100 * 12, 5);
        juce::File o (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("dm_synth_vocal.wav")); o.deleteFile();
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (o), 44100, 2, 24, {}, 0));
        w->writeFromAudioSampleBuffer (v, 0, v.getNumSamples());
    }
    std::printf ("== 1. Mic curves: LIVE filter vs analog target, 20 Hz-20 kHz (max error dB) ==\n");
    std::printf ("%-14s %10s %10s %10s %10s %10s %10s\n", "mic", "v1 @44.1k", "v2 @44.1k", "v2 @48k", "v2 @96k", "v1 <12k", "v2 <12k");
    double worstV1 = 0, worstV2 = 0;
    for (int m = 0; m < NUM_MICS; ++m)
    {
        double e1 = 0, e2[3] = {}, l1 = 0, l2 = 0;
        const double rates[3] = { 44100, 48000, 96000 };
        for (int k = 0; k <= 300; ++k)
        {
            const double f = 20.0 * std::pow (1000.0, k / 300.0);
            const double a = analogMicDb (m, f);
            e1 = std::max (e1, std::abs (v1MicDb (m, 44100, f) - a));
            for (int r = 0; r < 3; ++r) e2[r] = std::max (e2[r], std::abs (liveMicDb (m, rates[r], f) - a));
            if (f <= 12000) { l1 = std::max (l1, std::abs (v1MicDb (m, 44100, f) - a)); l2 = std::max (l2, std::abs (liveMicDb (m, 44100, f) - a)); }
        }
        worstV1 = std::max (worstV1, e1); worstV2 = std::max (worstV2, e2[0]);
        std::printf ("%-14s %10.2f %10.2f %10.2f %10.2f %10.2f %10.2f\n", MICS[m].name, e1, e2[0], e2[1], e2[2], l1, l2);
    }
    std::printf ("worst: v1 %.2f dB -> v2 %.2f dB (44.1k)\n\n", worstV1, worstV2);

    const double sr = 44100;
    // 2. LIVE vs HQ static tone (pink-ish: compare full engine render of a sweep of sines at -30 dBFS, warmth 0)
    std::printf ("== 2. LIVE vs HQ tone match (warmth 0, -30 dBFS sines, max |diff| dB 50 Hz-16 kHz) ==\n");
    double worstLH = 0;
    for (int m : { 0, 5, 10, 13, 18 })
    {
        double worst = 0;
        for (double f : { 50.0, 100.0, 300.0, 1000.0, 3000.0, 6000.0, 9000.0, 12000.0, 16000.0 })
        {
            double lvl[2];
            for (int mode = 0; mode < 2; ++mode)
            {
                DreamProcessor p;
                set (p, "mic", (float) m); set (p, "warmth", 0); set (p, "mode", (float) mode); set (p, "pre", 10);
                p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512); p.rebuildNow();
                std::this_thread::sleep_for (std::chrono::milliseconds (150));
                auto b = sine (f, -30, sr, 32768);
                run (p, b);
                lvl[mode] = juce::Decibels::gainToDecibels (b.getRMSLevel (0, 16384, 16384));
            }
            worst = std::max (worst, std::abs (lvl[0] - lvl[1]));
        }
        worstLH = std::max (worstLH, worst);
        std::printf ("%-14s %.2f dB\n", MICS[m].name, worst);
    }
    std::printf ("worst LIVE vs HQ: %.2f dB\n\n", worstLH);

    // 3. preamp harmonics: 220 Hz, -12 dBFS, Warmth 0/5/10, LIVE
    std::printf ("== 3. Preamps: 220 Hz @ -12 dBFS, LIVE. THD %% at Warmth 0/5/10, H2/H3 dB at 10, level change dB at 10 ==\n");
    for (int pr = 0; pr < NUM_PRES; ++pr)
    {
        double thd[3], h2 = 0, h3 = 0, lv = 0;
        const float wv[3] = { 0, 5, 10 };
        for (int k = 0; k < 3; ++k)
        {
            DreamProcessor p;
            set (p, "source", 0); set (p, "mic", 9); set (p, "pre", (float) pr); set (p, "warmth", wv[k]);
            p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512); p.rebuildNow();
            auto b = sine (220, -12, sr, 44100);
            run (p, b);
            double a2, a3, t; harmonics (b, 220, sr, a2, a3, t);
            thd[k] = t; if (k == 2) { h2 = a2; h3 = a3; lv = juce::Decibels::gainToDecibels (b.getRMSLevel (0, 22050, 22050)) - (-12 - 3.01); }
        }
        std::printf ("%-12s THD %5.2f %5.2f %5.2f   H2 %6.1f  H3 %6.1f   level %+5.1f\n", PRES[pr].name, thd[0], thd[1], thd[2], h2, h3, lv);
    }

    // aliasing check: 7 kHz sine hot through the hardest tube, energy at non-harmonic bins (v2 has ADAA on top of 2x OS)
    std::printf ("\n== 4. Tracking comp: GR on a synthetic sung line at -18 dBFS (avg dB over 2nd half), Comp 2/5/8 ==\n");
    for (int ct = 1; ct < NUM_COMPS; ++ct)
    {
        std::printf ("%-14s", COMP_NAMES[ct]);
        for (float amt : { 2.0f, 5.0f, 8.0f })
        {
            DreamProcessor p;
            set (p, "source", 0); set (p, "pre", 10); set (p, "warmth", 0); set (p, "mic", 3);
            set (p, "comptype", (float) ct); set (p, "comp", amt);
            p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512); p.rebuildNow();
            const int n = (int) (6 * sr);
            auto b = synthVocal (sr, n, 3);
            double grSum = 0; int cnt = 0;
            juce::MidiBuffer midi;
            for (int s = 0; s < n; s += 512)
            {
                const int len = juce::jmin (512, n - s);
                juce::AudioBuffer<float> blk (2, len);
                for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, b, c, s, len);
                p.processBlock (blk, midi);
                if (s > n / 2) { grSum += p.engine.compGrDb.load(); ++cnt; }
            }
            std::printf ("  %5.1f", grSum / cnt);
        }
        std::printf ("\n");
    }

    std::printf ("\n== 5. Level match: output vs input loudness (K-weighted dB), synthetic sung line at -18 dBFS ==\n");
    for (auto cfg : std::vector<std::vector<std::pair<const char*, float>>> {
             { { "mic", 0 }, { "pre", 0 }, { "warmth", 3 } },
             { { "mic", 10 }, { "pre", 1 }, { "warmth", 8 }, { "presence", 1 }, { "comptype", 2 }, { "comp", 6 } },
             { { "mic", 5 }, { "pre", 7 }, { "warmth", 10 }, { "air", 6 }, { "input", 12 } },
             { { "mic", 18 }, { "pre", 2 }, { "distance", 4 }, { "comptype", 4 }, { "comp", 7 }, { "deess", 6 } } })
    {
        double res[2];
        for (int mt = 0; mt < 2; ++mt)
        {
            DreamProcessor p;
            for (auto& [k, v] : cfg) set (p, k, v);
            set (p, "match", (float) mt);
            p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512); p.rebuildNow();
            const int n = (int) (6 * sr);
            auto b = synthVocal (sr, n, 11);
            const double inDb = kLoudness (b, sr, n / 2);
            run (p, b);
            res[mt] = kLoudness (b, sr, n / 2) - inDb;
        }
        std::printf ("%-28s  off %+6.1f dB   on %+6.1f dB\n", (juce::String (MICS[(int) cfg[0].second].name) + " config").toRawUTF8(), res[0], res[1]);
    }

    std::printf ("\n== 6. Old v1 project state loads (only v1 params in the XML) ==\n");
    {
        DreamProcessor p;
        juce::String v1 = "<DREAMMIC><PARAM id=\"source\" value=\"1\"/><PARAM id=\"mic\" value=\"5\"/><PARAM id=\"pattern\" value=\"0\"/>"
                          "<PARAM id=\"pre\" value=\"2\"/><PARAM id=\"mode\" value=\"1\"/><PARAM id=\"distance\" value=\"9\"/>"
                          "<PARAM id=\"warmth\" value=\"6.5\"/><PARAM id=\"hpf\" value=\"90\"/><PARAM id=\"air\" value=\"2\"/>"
                          "<PARAM id=\"input\" value=\"3\"/><PARAM id=\"output\" value=\"-2\"/></DREAMMIC>";
        auto xml = juce::parseXML (v1);
        juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary (*xml, mb);
        p.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto q = p.currentParams();
        std::printf ("mic %d pre %d mode %d warmth %.1f hpf %.0f | v2 defaults: comp %d deess %.1f lowcut %d match %d auto %d -> %s\n",
                     q.mic, q.pre, q.mode, q.warmth, q.hpfHz, q.comp, q.deess, (int) q.lowCut, (int) q.match, (int) q.autoIn,
                     (q.mic == 5 && q.pre == 2 && q.mode == 1 && q.comp == 0 && ! q.match) ? "OK" : "FAIL");
    }

    std::printf ("\n== 7. Presets ==\n");
    {
        DreamProcessor p;
        for (int i = 0; i < (int) DreamProcessor::factoryPresets().size(); ++i)
        {
            p.loadFactory (i);
            const auto q = p.currentParams();
            std::printf ("%-20s %s + %s, comp %s\n", DreamProcessor::factoryPresets()[(size_t) i].name, MICS[q.mic].name, PRES[q.pre].name, COMP_NAMES[q.comp]);
        }
        p.loadFactory (2);
        const bool saved = p.saveUser ("zz test preset");
        p.loadFactory (8);
        const bool loaded = p.loadUser ("zz test preset");
        const auto q = p.currentParams();
        std::printf ("save/load user preset: %s (mic %s, presence %d)\n", (saved && loaded && q.mic == 10 && q.presence) ? "OK" : "FAIL", MICS[q.mic].name, (int) q.presence);
        DreamProcessor::userPresetDir().getChildFile ("zz test preset.dmpreset").deleteFile();
    }

    std::printf ("\n== 8. Latency + speed ==\n");
    for (int mode = 0; mode < 2; ++mode)
    {
        DreamProcessor p;
        set (p, "mode", (float) mode); set (p, "comptype", 3); set (p, "deess", 5); set (p, "mic", 12);
        p.setPlayConfigDetails (2, 2, sr, 512); p.prepareToPlay (sr, 512); p.rebuildNow();
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        auto b = sine (440, -18, sr, (int) (20 * sr));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        run (p, b);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("%s: latency %d samples (%.2f ms), %.0fx real time\n", mode ? "HQ  " : "LIVE", p.getLatencySamples(),
                     1000.0 * p.getLatencySamples() / sr, 20000.0 / ms);
    }
    return 0;
}
