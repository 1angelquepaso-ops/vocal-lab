#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include <algorithm>
#include <cmath>

// The drawable wobble shape: points on a 0..1 grid that wrap around (last point flows into the first).
// Each point owns the segment that starts at it: Curve (bendable), S (smooth, sine-like) or Hold (step).
namespace wb
{
enum class Seg { curve = 0, smooth = 1, hold = 2 };

struct ShapePoint
{
    float x = 0, y = 0;     // x in [0,1), y in [0,1]
    float curve = 0;        // -1..1, only for Seg::curve
    Seg seg = Seg::curve;
};

struct Shape
{
    static constexpr int tableSize = 2048;
    std::vector<ShapePoint> pts;
    float loopStart = 0.0f, loopEnd = 1.0f;
    int grid = 4;           // grid steps per cycle (derived from gridNote + the Rate / pattern length)
    int gridNote = 3;       // index into gridNoteNames: the musical length of one grid step

    static juce::StringArray gridNoteNames() { return { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/32T" }; }
    static double gridNoteBeats (int i)
    {
        static const double b[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
        return b[juce::jlimit (0, 6, i)];
    }
    // how many steps of this note value fit in one cycle of `cycleBeats` (0 = doesn't fit)
    static int stepsFor (double cycleBeats, int note)
    {
        const double n = cycleBeats / gridNoteBeats (note);
        if (n < 0.999) return 0;
        return juce::jlimit (1, 64, (int) std::lround (n));
    }

    void sort() { std::sort (pts.begin(), pts.end(), [] (auto& a, auto& b) { return a.x < b.x; }); }

    static float segValue (const ShapePoint& a, float bx, float by, float x)
    {
        const float span = bx - a.x;
        const float u = span > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (x - a.x) / span) : 0.0f;
        float f = u;
        switch (a.seg)
        {
            case Seg::hold:   f = 0.0f; break;
            case Seg::smooth: f = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * u); break;
            case Seg::curve:
                if (a.curve >= 0.0f) f = std::pow (u, 1.0f + 4.0f * a.curve);
                else                 f = 1.0f - std::pow (1.0f - u, 1.0f - 4.0f * a.curve);
                break;
        }
        return a.y + (by - a.y) * f;
    }

    float valueAt (float x) const
    {
        if (pts.empty()) return 0.5f;
        if (pts.size() == 1) return pts[0].y;
        const int n = (int) pts.size();
        // find segment containing x (with wrap)
        int i = n - 1;
        for (int k = 0; k < n; ++k)
            if (pts[(size_t) k].x <= x) i = k; else break;
        if (x < pts[0].x)       // before first point: in wrap segment from last point
        {
            const auto& a = pts[(size_t) (n - 1)];
            return segValue (a, pts[0].x + 1.0f, pts[0].y, x + 1.0f);
        }
        const auto& a = pts[(size_t) i];
        if (i == n - 1) return segValue (a, pts[0].x + 1.0f, pts[0].y, x);
        const auto& b = pts[(size_t) (i + 1)];
        return segValue (a, b.x, b.y, x);
    }

    void compile (float* table) const
    {
        for (int i = 0; i < tableSize; ++i)
            table[i] = juce::jlimit (0.0f, 1.0f, valueAt ((float) i / (float) tableSize));
    }

    // ---------- presets ----------
    static Shape preset (const juce::String& name)
    {
        Shape s;
        auto add = [&] (float x, float y, Seg g = Seg::curve, float c = 0) { s.pts.push_back ({ x, y, c, g }); };
        if (name == "Triangle")      { add (0, 0); add (0.5f, 1); }
        else if (name == "Saw Down") { add (0, 1); add (0.96f, 0); }
        else if (name == "Saw Up")   { add (0, 0); add (0.96f, 1); }
        else if (name == "Square")   { add (0, 1, Seg::hold); add (0.5f, 0, Seg::hold); }
        else if (name == "Stutter")
        {
            for (int k = 0; k < 8; ++k) add (k / 8.0f, (k % 2 == 0) ? 1.0f : 0.0f, Seg::hold);
        }
        else if (name == "Yoi")
        {
            add (0.0f, 0.0f, Seg::curve, -0.5f); add (0.18f, 1.0f, Seg::smooth);
            add (0.45f, 0.45f, Seg::smooth); add (0.62f, 0.85f, Seg::curve, 0.5f); add (0.92f, 0.05f);
        }
        else if (name == "Ramp Hit") { add (0, 1, Seg::curve, 0.7f); add (0.9f, 0.0f); }
        else                         { add (0, 0, Seg::smooth); add (0.5f, 1, Seg::smooth); }   // Sine
        return s;
    }
    static juce::StringArray presetNames() { return { "Sine", "Triangle", "Saw Down", "Saw Up", "Square", "Stutter", "Yoi", "Ramp Hit" }; }

    // ---------- randomize (only inside the loop region, locked to the grid) ----------
    void randomize (juce::Random& rnd)
    {
        const float step = 1.0f / (float) grid;
        const float a = loopStart, b = loopEnd;
        pts.erase (std::remove_if (pts.begin(), pts.end(), [&] (auto& p) { return p.x >= a - 1e-5f && p.x < b - 1e-5f; }), pts.end());
        for (float x = a; x < b - 1e-5f; x += step)
        {
            ShapePoint p;
            p.x = std::round (x / step) * step;
            p.y = (float) rnd.nextInt (9) / 8.0f;
            const int t = rnd.nextInt (10);
            p.seg = t < 5 ? Seg::hold : (t < 8 ? Seg::curve : Seg::smooth);
            p.curve = (rnd.nextFloat() * 2.0f - 1.0f) * 0.8f;
            pts.push_back (p);
        }
        sort();
    }

    // ---------- state ----------
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("LFO");
        t.setProperty ("loopStart", loopStart, nullptr);
        t.setProperty ("loopEnd", loopEnd, nullptr);
        t.setProperty ("grid", grid, nullptr);
        t.setProperty ("gridNote", gridNote, nullptr);
        for (auto& p : pts)
        {
            juce::ValueTree c ("P");
            c.setProperty ("x", p.x, nullptr);
            c.setProperty ("y", p.y, nullptr);
            c.setProperty ("c", p.curve, nullptr);
            c.setProperty ("s", (int) p.seg, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }
    static Shape fromTree (const juce::ValueTree& t)
    {
        Shape s;
        if (! t.isValid() || t.getNumChildren() == 0) return preset ("Sine");
        s.loopStart = (float) t.getProperty ("loopStart", 0.0f);
        s.loopEnd = (float) t.getProperty ("loopEnd", 1.0f);
        s.grid = (int) t.getProperty ("grid", 4);
        s.gridNote = (int) t.getProperty ("gridNote", 3);
        for (auto c : t)
            s.pts.push_back ({ (float) c.getProperty ("x"), (float) c.getProperty ("y"),
                               (float) c.getProperty ("c", 0.0f), (Seg) (int) c.getProperty ("s", 0) });
        s.sort();
        if (s.loopEnd - s.loopStart < 0.01f) { s.loopStart = 0; s.loopEnd = 1; }
        return s;
    }
};
} // namespace wb
