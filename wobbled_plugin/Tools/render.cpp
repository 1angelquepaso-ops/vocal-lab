// wb_render — runs the real WOBBLE-D processor offline with a simulated FL playhead.
//   wb_render selftest                 run the click / determinism / song-lock / response checks (exit 1 on fail)
//   wb_render demo out.wav             render a short demo (Reese wobble + XY moves)
#include <juce_audio_formats/juce_audio_formats.h>
#include "../Source/PluginProcessor.h"
#include <map>
#include <tuple>
#include <cstdio>

using namespace wb;

struct TestHead : juce::AudioPlayHead
{
    double bpm = 120, ppq = 0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        return p;
    }
};

struct Ev { double beat; std::string pid; float v; };
struct Note { double start; int note; double len; };
struct Scenario
{
    double fs = 48000, bpm = 120, startBeat = 0, lengthBeats = 8;
    std::vector<Note> notes { { 0.0, 45, 1000.0 } };
    std::vector<Ev> evs;
    std::vector<std::pair<double, double>> jumps;   // at song beat -> to song beat
    std::map<std::string, float> params;
    bool playing = true;
    juce::File sample;
    juce::String shapeName;
    bool liveFirst = false;   // play + tweak with FL stopped before 'exporting'
};
struct Result { std::vector<float> L, R, lfo; std::vector<double> songBeat; double renderMs = 0; };

static void setP (WobbleProcessor& p, const std::string& pid, float v)
{
    if (auto* prm = p.state.getParameter (pid)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    else std::printf ("unknown param %s\n", pid.c_str());
}

static Result render (const Scenario& sc)
{
    Result r;
    WobbleProcessor proc;
    for (auto& kv : sc.params) setP (proc, kv.first, kv.second);
    if (sc.shapeName.isNotEmpty()) { proc.shape = Shape::preset (sc.shapeName); proc.shapeChanged(); }
    if (sc.sample.existsAsFile()) { juce::String e; proc.loadSample (sc.sample, e); for (auto& kv : sc.params) setP (proc, kv.first, kv.second); }
    const int maxBlock = 1024;
    proc.setRateAndBufferSizeDetails (sc.fs, maxBlock);
    proc.prepareToPlay (sc.fs, maxBlock);
    TestHead head; head.bpm = sc.bpm; head.playing = sc.playing;
    proc.testPlayHead = &head;
    proc.engine.lfoTrace = &r.lfo;

    const double spb = sc.fs * 60.0 / sc.bpm;           // samples per beat
    if (sc.liveFirst)
    {
        // like jamming in FL with the transport stopped: notes from the typing keyboard + knob / pad moves
        head.playing = false; head.ppq = 0;
        juce::AudioBuffer<float> lb (4, 512);
        for (int blk = 0; blk < 400; ++blk)
        {
            juce::MidiBuffer m;
            if (blk % 60 == 5)  m.addEvent (juce::MidiMessage::noteOn (1, 40 + blk % 7, (juce::uint8) 70), 17);
            if (blk % 60 == 45) m.addEvent (juce::MidiMessage::noteOff (1, 40 + (blk - 40) % 7), 3);
            if (blk == 50)  { setP (proc, id::xyX, 0.8f); setP (proc, id::chaos, 0.6f); setP (proc, id::xyY, -0.7f); }
            if (blk == 150) { setP (proc, id::xTarget, 2); setP (proc, id::cutoff, 900); }
            if (blk == 250) m.addEvent (juce::MidiMessage::pitchWheel (1, 8192), 0);
            lb.clear(); proc.processBlock (lb, m);
        }
        juce::MidiBuffer off; for (int n = 0; n < 128; ++n) off.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        lb.clear(); proc.processBlock (lb, off);
        // put every tweaked knob back where the song has it
        Scenario d; WobbleProcessor ref;
        for (auto* prm : ref.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                if (auto* t = proc.state.getParameter (rp->getParameterID())) t->setValueNotifyingHost (rp->getDefaultValue());
        for (auto& kv : sc.params) setP (proc, kv.first, kv.second);
        for (int blk = 0; blk < 200; ++blk) { juce::MidiBuffer m; lb.clear(); proc.processBlock (lb, m); }   // release tails die
        head.playing = sc.playing;
    }
    const long total = (long) std::llround (sc.lengthBeats * spb);
    const int sizes[] = { 480, 512, 128, 1024, 333, 64, 777 };
    double pos = sc.startBeat;
    long done = 0; int si = 0;
    std::vector<bool> evDone (sc.evs.size(), false);
    std::vector<bool> jumpDone (sc.jumps.size(), false);
    juce::AudioBuffer<float> buf (4, maxBlock);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (done < total)
    {
        int len = (int) juce::jmin ((long) sizes[si++ % 7], total - done);
        double jumpTo = -1;
        for (size_t j = 0; j < sc.jumps.size(); ++j)
        {
            if (jumpDone[j]) continue;
            const double at = sc.jumps[j].first;
            if (at >= pos && at < pos + len / spb)
            {
                len = juce::jmax (1, (int) std::llround ((at - pos) * spb));
                jumpTo = sc.jumps[j].second; jumpDone[j] = true; break;
            }
        }
        for (size_t e = 0; e < sc.evs.size(); ++e)
            if (! evDone[e] && sc.evs[e].beat < pos + len / spb && sc.evs[e].beat >= pos - 1e-9)
            { setP (proc, sc.evs[e].pid, sc.evs[e].v); evDone[e] = true; }
        juce::MidiBuffer midi;
        for (auto& n : sc.notes)
        {
            const double on = n.start, off = n.start + n.len;
            if (on >= pos && on < pos + len / spb) midi.addEvent (juce::MidiMessage::noteOn (1, n.note, (juce::uint8) 100), (int) ((on - pos) * spb));
            if (off >= pos && off < pos + len / spb) midi.addEvent (juce::MidiMessage::noteOff (1, n.note), (int) ((off - pos) * spb));
        }
        head.ppq = pos;
        buf.setSize (4, len, false, false, true);
        buf.clear();
        proc.processBlock (buf, midi);
        for (int i = 0; i < len; ++i)
        {
            r.L.push_back (buf.getSample (0, i)); r.R.push_back (buf.getSample (1, i));
            r.songBeat.push_back (pos + i / spb);
        }
        done += len;
        pos = jumpTo >= 0 ? jumpTo : pos + len / spb;
    }
    r.renderMs = juce::Time::getMillisecondCounterHiRes() - t0;
    return r;
}

// Click score: biggest 2nd difference relative to what a clean sine of this pitch/level can produce.
// Clean audio scores ~1-1.5. A real click scores in the hundreds.
static double clickScore (const std::vector<float>& x, double freq, double fs, long from, long to)
{
    double peak = 1e-9, worst = 0;
    for (long i = from; i < to; ++i) peak = std::max (peak, (double) std::abs (x[(size_t) i]));
    const double w = 2.0 * kPi * freq / fs;
    for (long i = juce::jmax (2L, from); i < to; ++i)
        worst = std::max (worst, (double) std::abs (x[(size_t) i] - 2.0f * x[(size_t) i - 1] + x[(size_t) i - 2]));
    return worst / (peak * w * w);
}

static Scenario clickBase()
{
    Scenario s;
    s.params = { { id::source, 1 }, { id::unison, 1 }, { id::fltType, 1 }, { id::cutoff, 20000 }, { id::reso, 0 },
                 { id::drive, 0 }, { id::hq, 0 }, { id::dCutoff, 0 }, { id::dVolume, 1 }, { id::subLock, 0 },
                 { id::clip, 0 }, { id::volume, 0 }, { id::rate, 10 }, { id::xyGlide, 0 }, { id::smooth, 0 } };
    s.lengthBeats = 10;
    return s;
}

static int fails = 0;
static void check (const char* name, bool ok, const juce::String& detail)
{
    std::printf ("%s  %-46s %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
    if (! ok) ++fails;
}

static double maxDiff (const std::vector<float>& a, size_t aFrom, const std::vector<float>& b, size_t bFrom, size_t n)
{
    double m = 0;
    for (size_t i = 0; i < n && aFrom + i < a.size() && bFrom + i < b.size(); ++i) m = std::max (m, (double) std::abs (a[aFrom + i] - b[bFrom + i]));
    return m;
}

static int selftest()
{
    const double fs = 48000, spb = 24000, f = 110.0;
    const long skip = (long) (0.05 * fs);

    // 0) detector sanity: a hard gain step must score huge
    {
        std::vector<float> x ((size_t) fs);
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (std::sin (2 * kPi * f * i / fs) * (i < x.size() / 2 ? 0.5 : 0.3));
        const double s = clickScore (x, f, fs, 100, (long) x.size());
        check ("detector catches a raw gain step", s > 10, "score " + juce::String (s, 1));
    }
    // 1) baseline
    const auto base = render (clickBase());
    const double sBase = clickScore (base.L, f, fs, skip, (long) base.L.size());
    check ("baseline wobble is clean", sBase < 3, "score " + juce::String (sBase, 2));

    // 2) touch-mode rate changes, instant pad jumps (worst case: glide 0)
    {
        auto sc = clickBase();
        sc.evs = { { 2.3, id::xyX, 0.5f }, { 3.7, id::xyX, -0.5f }, { 5.1, id::xyX, 1.0f }, { 6.6, id::xyX, -1.0f }, { 8.2, id::xyX, 0.25f } };
        const auto r = render (sc);
        const double s = clickScore (r.L, f, fs, skip, (long) r.L.size());
        check ("Touch rate jumps: no clicks", s < 3, "score " + juce::String (s, 2));
    }
    // 3) quantized rate changes (1/16) — hard re-phase on the grid + crossfade
    {
        auto sc = clickBase();
        sc.params[id::quantize] = 1;
        sc.evs = { { 2.3, id::xyX, 0.5f }, { 3.7, id::xyX, -0.5f }, { 5.1, id::xyX, 1.0f }, { 6.6, id::xyX, -1.0f } };
        const auto r = render (sc);
        const double s = clickScore (r.L, f, fs, skip, (long) r.L.size());
        check ("Quantize 1/16 rate jumps: no clicks", s < 3, "score " + juce::String (s, 2));
    }
    // 4) target switch while the dot is far off-centre
    {
        auto sc = clickBase();
        sc.evs = { { 1.0, id::xyX, 0.9f }, { 3.3, id::xTarget, 1 }, { 5.4, id::xTarget, 2 }, { 7.1, id::xTarget, 0 },
                   { 2.0, id::xyY, 0.8f }, { 4.4, id::yTarget, 1 }, { 6.2, id::yTarget, 0 } };
        const auto r = render (sc);
        const double s = clickScore (r.L, f, fs, skip, (long) r.L.size());
        check ("XY target switches: no clicks", s < 3, "score " + juce::String (s, 2));
    }
    // 5) FL playlist loop / jump
    {
        auto sc = clickBase();
        sc.jumps = { { 6.37, 2.0 }, { 7.81, 0.5 } };
        const auto r = render (sc);
        const double s = clickScore (r.L, f, fs, skip, (long) r.L.size());
        check ("Playlist loop jumps: no clicks", s < 3, "score " + juce::String (s, 2));
    }
    // 5b) hard gate shapes (Square / Stutter) with full volume wobble: tight but no pops
    for (auto nm : { "Square", "Stutter" })
    {
        auto sc = clickBase(); sc.shapeName = nm;
        const auto r = render (sc);
        const double s = clickScore (r.L, f, fs, skip, (long) r.L.size());
        check ((juce::String ("Hard-gate shape: ") + nm + " no pops").toRawUTF8(), s < 3, "score " + juce::String (s, 2));
    }
    // 6) determinism with chaos (+ and -) and pad moves
    {
        auto sc = clickBase();
        sc.params[id::source] = 4; sc.params[id::unison] = 3; sc.params[id::chaos] = 0.7f; sc.params[id::dCutoff] = 0.8f;
        sc.params[id::cutoff] = 150; sc.params[id::dVolume] = 0; sc.params[id::hq] = 1; sc.params[id::drive] = 0.5f;
        sc.evs = { { 2.0, id::xyX, 0.5f }, { 4.0, id::chaos, -0.6f }, { 5.5, id::xyY, 0.4f } };
        const auto a = render (sc), b = render (sc);
        bool same = a.L.size() == b.L.size() && std::memcmp (a.L.data(), b.L.data(), a.L.size() * sizeof (float)) == 0
                    && std::memcmp (a.R.data(), b.R.data(), a.R.size() * sizeof (float)) == 0;
        check ("Two renders are bit-identical (chaos on)", same, same ? "identical" : "DIFFERENT");
        sc.params[id::seed] = 2;
        const auto c = render (sc);
        const double d = maxDiff (a.lfo, 0, c.lfo, 0, a.lfo.size());
        check ("Reroll (new seed) changes the pattern", d > 0.05, "max lfo diff " + juce::String (d, 3));
    }
    // 6b) export after live jamming / tweaking with FL stopped == export from a fresh plugin
    {
        Scenario sc; sc.lengthBeats = 8; sc.params = { { id::chaos, 0.4f }, { id::drive, 0.6f } };
        sc.notes = { { 0, 33, 1.5 }, { 2, 36, 1.5 }, { 4, 31, 3.5 } };
        const auto fresh = render (sc);
        sc.liveFirst = true;
        const auto used = render (sc);
        const double d = maxDiff (fresh.L, 0, used.L, 0, fresh.L.size());
        check ("Export after live tweaking == fresh export", d < 1e-6, "max diff " + juce::String (d, 7));
    }
    // 7) song-lock: starting playback at a different spot gives the same wobble
    {
        auto sc = clickBase();
        sc.params[id::rate] = 8;
        sc.lengthBeats = 16;
        const auto a = render (sc);
        auto sb = sc; sb.startBeat = 6.0; sb.lengthBeats = 10;
        const auto b = render (sb);
        const size_t n = (size_t) (6 * spb);
        const double d = maxDiff (a.lfo, (size_t) (10 * spb), b.lfo, (size_t) (4 * spb), n);
        check ("Song-lock: same wobble from any start point", d < 1e-4, "max diff " + juce::String (d, 6));
    }
    // 8) touch change slides back onto the grid (compare with a render that started at the new rate)
    {
        auto sc = clickBase();
        sc.params[id::rate] = 8; sc.lengthBeats = 12; sc.params[id::resync] = 0.5f;
        sc.evs = { { 3.3, id::xyX, 0.5f } };
        const auto a = render (sc);
        auto ref = clickBase(); ref.params[id::rate] = 8; ref.lengthBeats = 12; ref.params[id::xyX] = 0.5f;
        const auto b = render (ref);
        const double d = maxDiff (a.lfo, (size_t) (8 * spb), b.lfo, (size_t) (8 * spb), (size_t) (4 * spb));
        check ("Touch change re-locks to the grid", d < 2e-3, "max diff after 4.7 beats " + juce::String (d, 5));
        // response time: first sample after the change where it differs from the unchanged render
        auto noChange = clickBase(); noChange.params[id::rate] = 8; noChange.lengthBeats = 12;
        const auto c = render (noChange);
        const size_t at = (size_t) (3.3 * spb);
        size_t k = at;
        while (k < a.lfo.size() && std::abs (a.lfo[k] - c.lfo[k]) < 0.01f) ++k;
        // event lands at the start of the block that contains it, so measure from that block
        const double ms = ((double) k - (double) at) / fs * 1000.0;
        check ("Touch response is immediate", ms < 15.0, "audible change after " + juce::String (ms, 1) + " ms");
    }
    // 9) quantize waits for the grid (1/16 at 120 bpm = 125 ms max)
    {
        auto sc = clickBase(); sc.params[id::rate] = 8; sc.params[id::quantize] = 1; sc.lengthBeats = 8;
        sc.evs = { { 3.3, id::xyX, 0.5f } };
        const auto a = render (sc);
        auto nc = clickBase(); nc.params[id::rate] = 8; nc.params[id::quantize] = 1; nc.lengthBeats = 8;
        const auto c = render (nc);
        size_t k = (size_t) (3.3 * spb);
        while (k < a.lfo.size() && std::abs (a.lfo[k] - c.lfo[k]) < 0.01f) ++k;
        const double beat = (double) k / spb;
        check ("Quantize 1/16 lands on the next 1/16", beat >= 3.49 && beat < 3.56, "changed at beat " + juce::String (beat, 3));
    }
    // 10) sample: root detection + pitch
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wb_test_A2.wav");
        {
            tmp.deleteFile();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (tmp), 44100, 1, 16, {}, 0));
            juce::AudioBuffer<float> b (1, 44100);
            for (int i = 0; i < 44100; ++i)
                b.setSample (0, i, (float) (0.5 * std::sin (2 * kPi * 110.0 * i / 44100.0) + 0.2 * std::sin (2 * kPi * 220.0 * i / 44100.0)));
            w->writeFromAudioSampleBuffer (b, 0, 44100);
        }
        WobbleProcessor p; juce::String err;
        const bool ok = p.loadSample (tmp, err);
        const int root = (int) p.state.getRawParameterValue (id::rootKey)->load();
        check ("Sample loads + root key detected (A2 = 45)", ok && root == 45, "root " + juce::String (root) + " " + err);

        Scenario sc; sc.sample = tmp; sc.lengthBeats = 4;
        sc.notes = { { 0.0, 57, 100.0 } };          // A3 -> should play 220 Hz
        sc.params = { { id::source, 0 }, { id::unison, 1 }, { id::fltType, 1 }, { id::cutoff, 20000 }, { id::reso, 0 }, { id::drive, 0 },
                      { id::dCutoff, 0 }, { id::subLock, 0 }, { id::hq, 0 }, { id::smpLoop, 1 } };
        const auto r = render (sc);
        int zc = 0; const size_t a0 = (size_t) (0.5 * fs), a1 = (size_t) (1.5 * fs);
        for (size_t i = a0 + 1; i < a1; ++i) if (r.L[i - 1] <= 0 && r.L[i] > 0) ++zc;
        check ("Sample plays in tune (A3 = 220 Hz)", std::abs (zc - 220) <= 2, juce::String (zc) + " Hz");
        const double s = clickScore (r.L, 220.0, fs, skip, (long) r.L.size());
        check ("Sample loop is click-free", s < 6, "score " + juce::String (s, 2) + " (2-partial source)");
    }
    // 10a) editor-style background load (what drag-and-drop / LOAD SAMPLE use) + closing WOBBLE-D mid-load
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wb_test_A2.wav");
        WobbleProcessor p; bool done = false, ok = false;
        p.loadSampleAsync (tmp, [&] (bool k, juce::String) { done = true; ok = k; });
        const bool returnedFast = p.loadingSample.load();          // call returned before the work finished
        const auto t0 = juce::Time::getMillisecondCounter();
        while (! done && juce::Time::getMillisecondCounter() - t0 < 10000)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        const int root = (int) p.state.getRawParameterValue (id::rootKey)->load();
        const int src = (int) p.state.getRawParameterValue (id::source)->load();
        check ("Background sample load (drop / LOAD button)", done && ok && returnedFast && root == 45 && src == 0 && p.sampleForDisplay() != nullptr,
               juce::String ("root ") + juce::String (root) + ", source " + juce::String (src));
        {
            auto q = std::make_unique<WobbleProcessor>();
            q->loadSampleAsync (tmp, [] (bool, juce::String) {});
            q.reset();                                              // close the plugin while it loads
        }
        const auto t1 = juce::Time::getMillisecondCounter();
        while (juce::Time::getMillisecondCounter() - t1 < 1500) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check ("Closing WOBBLE-D mid-load is safe", true, "no crash");
    }
    // 10b) save project + reopen: every knob, the drawn shape, loop markers, grid and the sample come back
    {
        const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("wb_test_A2.wav");
        WobbleProcessor a; juce::String err;
        a.loadSample (tmp, err);
        juce::Random r (42);
        for (auto* prm : a.getParameters()) prm->setValueNotifyingHost (r.nextFloat());
        a.shape = Shape::preset ("Yoi"); a.shape.grid = 12; a.shape.randomize (r); a.shape.loopStart = 0.25f; a.shape.loopEnd = 0.75f;
        a.shapeChanged();
        juce::MemoryBlock mb; a.getStateInformation (mb);
        WobbleProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        int bad = 0;
        for (int i = 0; i < a.getParameters().size(); ++i)
        {
            const float va = a.getParameters()[i]->getValue(), vb = b.getParameters()[i]->getValue();
            const bool isBool = dynamic_cast<juce::AudioParameterBool*> (a.getParameters()[i]) != nullptr;
            if (isBool ? ((va > 0.5f) != (vb > 0.5f)) : std::abs (va - vb) > 1e-6f)
            {
                ++bad;
                std::printf ("    %s: %.5f vs %.5f\n", a.getParameters()[i]->getName (40).toRawUTF8(), va, vb);
            }
        }
        float ta[Shape::tableSize], tb[Shape::tableSize];
        a.shape.compile (ta); b.shape.compile (tb);
        double sd = 0; for (int i = 0; i < Shape::tableSize; ++i) sd = std::max (sd, (double) std::abs (ta[i] - tb[i]));
        const bool shapeOk = sd < 1e-6 && b.shape.grid == 12 && b.shape.loopStart == 0.25f && b.shape.loopEnd == 0.75f;
        const bool smpOk = b.sampleForDisplay() != nullptr && b.sampleForDisplay()->buf.getNumSamples() == a.sampleForDisplay()->buf.getNumSamples();
        check ("Save + reopen restores knobs, shape, loop, sample", bad == 0 && shapeOk && smpOk,
               juce::String (bad) + " knobs off, shape diff " + juce::String (sd, 6) + (smpOk ? ", sample ok" : ", SAMPLE MISSING"));
    }
    // 10c) mono legato glide + poly chords
    {
        auto clean = [] { Scenario s; s.params = { { id::source, 1 }, { id::unison, 1 }, { id::fltType, 1 }, { id::cutoff, 20000 }, { id::reso, 0 },
                                                   { id::drive, 0 }, { id::hq, 0 }, { id::dCutoff, 0 }, { id::subLock, 0 }, { id::clip, 0 } }; return s; };
        auto hz = [&] (const std::vector<float>& x, double t0, double t1)
        {
            int zc = 0; const size_t a = (size_t) (t0 * fs), b = (size_t) (t1 * fs);
            for (size_t i = a + 1; i < b; ++i) if (x[i - 1] <= 0 && x[i] > 0) ++zc;
            return zc / (t1 - t0);
        };
        auto sc = clean(); sc.params[id::glide] = 200; sc.lengthBeats = 4;
        sc.notes = { { 0.0, 45, 2.0 }, { 1.0, 57, 2.0 } };          // overlapping = legato slide 110 -> 220 Hz
        const auto r = render (sc);
        const double f1 = hz (r.L, 0.1, 0.45), fMid = hz (r.L, 0.52, 0.6), f2 = hz (r.L, 1.1, 1.45);
        double minLvl = 1;   // no re-attack: level never dips during the slide
        for (size_t i = (size_t) (0.45 * fs); i < (size_t) (0.9 * fs); i += 480)
        {
            float pk = 0; for (size_t j = i; j < i + 480; ++j) pk = std::max (pk, std::abs (r.L[j]));
            minLvl = std::min (minLvl, (double) pk);
        }
        check ("Mono legato glides (no retrigger)", std::abs (f1 - 110) < 3 && fMid > 125 && fMid < 215 && std::abs (f2 - 220) < 3 && minLvl > 0.2,
               juce::String (f1, 0) + " Hz -> " + juce::String (fMid, 0) + " -> " + juce::String (f2, 0) + " Hz, min level " + juce::String (minLvl, 2));

        auto pc = clean(); pc.params[id::voiceMode] = 1; pc.lengthBeats = 4;
        pc.notes = { { 0.0, 45, 4.0 }, { 0.0, 49, 4.0 }, { 0.0, 52, 4.0 } };   // A2 C#3 E3
        const auto q = render (pc);
        const int N = 32768; const size_t off = (size_t) (0.3 * fs);
        auto mag = [&] (double f) { double re = 0, im = 0; for (int i = 0; i < N; ++i) { const double w = 0.5 - 0.5 * std::cos (2 * kPi * i / N);
                                     re += q.L[off + (size_t) i] * w * std::cos (2 * kPi * f * i / fs); im += q.L[off + (size_t) i] * w * std::sin (2 * kPi * f * i / fs); }
                                     return std::sqrt (re * re + im * im); };
        const double m1 = mag (110.0), m2 = mag (138.59), m3 = mag (164.81), mOff = mag (123.47);
        check ("Poly plays all 3 chord notes", m1 > 10 * mOff && m2 > 10 * mOff && m3 > 10 * mOff,
               "A " + juce::String (m1, 0) + " C# " + juce::String (m2, 0) + " E " + juce::String (m3, 0) + " (gap " + juce::String (mOff, 1) + ")");
    }
    // 11) CPU: heavy patch
    {
        Scenario sc; sc.lengthBeats = 16;
        sc.params = { { id::unison, 4 }, { id::hq, 1 }, { id::boostGain, 9 }, { id::dBoost, 0.6f }, { id::lowGain, 3 }, { id::midGain, -2 } };
        const auto r = render (sc);
        const double audioMs = r.L.size() / fs * 1000.0;
        check ("CPU: Reese x4 unison, HQ drive, EQ", audioMs / r.renderMs > 20, "realtime x" + juce::String (audioMs / r.renderMs, 0));
    }
    // 12) separate sub output stays clean (no wobble)
    {
        Scenario sc; sc.lengthBeats = 4; sc.params = { { id::subOut, 1 } };
        WobbleProcessor p;
        for (auto& kv : sc.params) setP (p, kv.first, kv.second);
        p.setRateAndBufferSizeDetails (fs, 512); p.prepareToPlay (fs, 512);
        TestHead h; p.testPlayHead = &h;
        juce::AudioBuffer<float> b (4, 512); double subE = 0, mainE = 0;
        for (int blk = 0; blk < 200; ++blk)
        {
            juce::MidiBuffer m; if (blk == 0) m.addEvent (juce::MidiMessage::noteOn (1, 33, (juce::uint8) 100), 0);
            h.ppq = blk * 512 / spb; b.clear(); p.processBlock (b, m);
            for (int i = 0; i < 512; ++i) { subE += b.getSample (2, i) * b.getSample (2, i); mainE += b.getSample (0, i) * b.getSample (0, i); }
        }
        check ("Sub goes to output 3-4 when Separate", subE > 1.0 && mainE > 1.0, "sub energy " + juce::String (subE, 1));
    }
    std::printf (fails == 0 ? "\nALL PASS\n" : "\n%d FAILED\n", fails);
    return fails == 0 ? 0 : 1;
}

static int demo (const juce::File& out)
{
    Scenario sc; sc.bpm = 140; sc.lengthBeats = 32;
    sc.notes = { { 0, 33, 7.5 }, { 8, 36, 3.5 }, { 12, 31, 3.5 }, { 16, 33, 15.5 } };
    sc.params = { { id::rate, 8 }, { id::boostGain, 8 }, { id::dBoost, 0.5f } };
    sc.evs = { { 4, id::xyX, 0.5f }, { 6, id::xyX, 1.0f }, { 8, id::xyX, 0 }, { 12, id::xyY, 0.8f },
               { 16, id::xTarget, 1 }, { 16, id::xyX, 0.7f }, { 20, id::yTarget, 1 }, { 20, id::xyY, 0.6f },
               { 24, id::xTarget, 0 }, { 24, id::xyX, -0.5f }, { 28, id::xyX, 0.75f } };
    const auto r = render (sc);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), sc.fs, 2, 24, {}, 0));
    juce::AudioBuffer<float> b (2, (int) r.L.size());
    for (size_t i = 0; i < r.L.size(); ++i) { b.setSample (0, (int) i, r.L[i]); b.setSample (1, (int) i, r.R[i]); }
    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    float pk = b.getMagnitude (0, b.getNumSamples());
    std::printf ("{\"seconds\": %.1f, \"peak\": %.3f, \"render_ms\": %.0f}\n", r.L.size() / sc.fs, pk, r.renderMs);
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? argv[1] : "selftest";
    if (mode == "demo" && argc > 2) return demo (juce::File (argv[2]));
    return selftest();
}
