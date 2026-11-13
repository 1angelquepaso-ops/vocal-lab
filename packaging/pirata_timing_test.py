"""PIRATA timing regression test (lessons from TITO PUENTE).

Renders samples with known timing on the bundled grand and checks:
  - exact BPM (no filename hint): 140 / 128 / 92 must come back exact
  - Keep timing: every note within a few ms of where it was played
    (laid-back / pushed humanised playing, swung 16ths)
  - Auto grid: 8th-note-triplet arps land on 1/3-beat positions, straight
    material stays on the 1/16 grid; swing % measured
  - a slow pad with one chord per bar gets its tempo from the harmonic rhythm
Exits 1 on failure.

    python packaging/pirata_timing_test.py [workdir]
"""
import json
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pirata import analysis, cli, piano  # noqa: E402

PIANO_LATENCY = 0.0133      # measured: first 5 % of the attack, median over the test pitches
CHORDS = [[45, 57, 60, 64], [41, 57, 60, 65], [36, 55, 60, 64], [43, 55, 59, 62]]


def make(path, bpm, bars, pattern, jitter_ms=0.0, lag_ms=0.0, seed=0):
    """pattern(bar, chord) -> list of (beat, dur_beats, pitch, vel). Returns truth in seconds."""
    rng = np.random.default_rng(seed)
    b = 60 / bpm
    notes, truth = [], []
    for i in range(bars):
        offs = {}                       # a player's timing moves per hit, not per finger
        for beat, dur, p, v in pattern(i, CHORDS[i % 4]):
            if beat not in offs:
                offs[beat] = (lag_ms + rng.normal(0, jitter_ms)) / 1000 if jitter_ms or lag_ms else 0.0
            off = offs[beat]
            t = max(0.0, (i * 4 + beat) * b + off)
            notes.append((t, t + dur * b * 0.95, p, v))
            truth.append(dict(t=t, beat=i * 4 + beat, pitch=p))
    piano.save(path, piano.render(notes))
    return truth


def held_and_eighths(i, ch):
    out = [(0, 3.8, ch[0], 85)] + [(0, 1.9, p, 70) for p in ch[1:]]
    for k in range(8):
        out.append((k * 0.5, 0.45, ch[1 + k % 3] + 12, 80))
    return out


def triplet_arp(i, ch):
    out = [(0, 3.8, ch[0], 85)]
    up = [ch[1] + 12, ch[2] + 12, ch[3] + 12]
    for k in range(12):                       # 8th-note triplets
        out.append((k / 3, 0.3, up[k % 3], 78))
    return out


def swung_16ths(i, ch):
    out = [(0, 3.8, ch[0], 85)]
    for k in range(16):
        pos = k * 0.25
        if k % 2:                              # 62 % swing on the off-16ths
            pos = (k - 1) * 0.25 + 0.5 * 0.62
        out.append((pos, 0.2, ch[1 + k % 3] + 12, 74 if k % 2 else 84))
    return out


def soft_pad(path, bpm=92):
    """Slow-attack saw pad, one chord per bar (no rhythm to lock onto)."""
    import soundfile as sf
    sr = 44100
    b = 60 / bpm
    prog = [[45, 57, 60, 64, 67], [41, 57, 60, 64], [40, 55, 60, 64], [40, 56, 59, 64],
            [38, 57, 60, 65], [43, 55, 59, 65], [36, 55, 59, 64], [40, 56, 59, 62]]
    y = np.zeros(int(8 * 4 * b * sr) + sr)
    for i, ch in enumerate(prog):
        t0, n = int(i * 4 * b * sr), int(4 * b * sr)
        t = np.arange(n) / sr
        env = np.minimum(1, t / 0.25) * np.minimum(1, (n / sr - t) / 0.3)
        for p in ch:
            f = 440 * 2 ** ((p - 69) / 12)
            for k in range(1, 8):
                y[t0:t0 + n] += env * np.sin(2 * np.pi * f * k * t + k) / k * 0.08
    sf.write(path, y / np.abs(y).max() * 0.8, sr)


def run(work):
    ok = True
    cases = [
        ("steady140", 140, held_and_eighths, dict()),
        ("steady128", 128, held_and_eighths, dict()),
        ("laidback92", 92, held_and_eighths, dict(jitter_ms=9, lag_ms=18, seed=4)),
        ("triplets75", 75, triplet_arp, dict()),
        ("swing16_84", 84, swung_16ths, dict(jitter_ms=4, seed=2)),
    ]
    for name, bpm, pat, kw in cases:
        wav = os.path.join(work, name + ".wav")            # no BPM in the name on purpose
        truth = make(wav, bpm, 8, pat, **kw)
        an = json.loads(json.dumps(analysis.analyze(wav)))
        bpm_ok = abs(an["bpm"] - bpm) < 0.05
        # Keep timing: match every played note to an exported note (same pitch, nearest)
        spb = 60 / an["bpm"]
        kept = cli.retime(an["notes"], an, "keep")
        ks = [(an["grid_start"] + n["start"] * spb, n["pitch"]) for n in kept]
        sig = []
        for t in truth:
            c = [s - t["t"] for s, p in ks if p == t["pitch"] and abs(s - t["t"]) < 0.08]
            if c:
                sig.append(min(c, key=abs))
        found = len(sig) / len(truth)
        sig = np.array(sig) if sig else np.array([1.0])
        # the Salamander samples speak ~11-15 ms after note-on: that is where the
        # attack really is, so the target is note-on + PIANO_LATENCY
        lat = 1000 * float(np.median(sig))
        errs = np.abs(sig - PIANO_LATENCY)
        med = 1000 * float(np.median(errs))
        p90 = 1000 * float(np.percentile(errs, 90))
        # Auto grid: fractional positions
        fr = [round((n["start"] % 1) * 12) for n in an["notes"]]
        trip = sum(1 for f in fr if f in (4, 8)) / max(1, len(fr))
        good = bpm_ok and found >= 0.9 and med <= 3.0 and p90 <= 8.0
        if name.startswith("triplets"):
            good &= trip >= 0.45          # 2 of every 3 arp notes sit on 1/3 or 2/3
        elif name.startswith("steady"):
            good &= trip <= 0.02          # straight stays straight
        elif name.startswith("swing"):
            good &= abs(an["timing"]["swing_16"] - 62) < 3
        ok &= good
        print(f"{name}: bpm {an['bpm']:.2f} (true {bpm}) | keep timing: {found:.0%} notes, "
              f"median {med:.1f} ms, p90 {p90:.1f} ms (attack at +{lat:.1f} ms) | triplet slots {trip:.0%} | "
              f"swing {an['timing']['swing_16']:.0f}% steady={an['timing']['steady']} -> {'PASS' if good else 'FAIL'}")
    # sparse pad: tempo from the harmonic rhythm (old engine read 149.5)
    wav = os.path.join(work, "softpad.wav")
    soft_pad(wav)
    an = analysis.analyze(wav)
    good = abs(an["bpm"] - 92) < 0.05 and len(an["chords"]) >= 7
    ok &= good
    print(f"softpad: bpm {an['bpm']:.2f} (true 92), {len(an['chords'])} chords -> {'PASS' if good else 'FAIL'}")
    return ok


if __name__ == "__main__":
    w = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="pirata_time_")
    os.makedirs(w, exist_ok=True)
    sys.exit(0 if run(w) else 1)
