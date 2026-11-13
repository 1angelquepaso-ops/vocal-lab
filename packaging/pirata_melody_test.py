"""PIRATA regression test: Simple must be the sample's real melody, Rich must keep
that melody on top with clean chords under it.

Renders three known samples on the bundled grand (held chords, 8th-note stab
comping, a melody weaving through the chord tones), runs analyze + build and
checks the results. Exits 1 on failure.

    python packaging/pirata_melody_test.py [workdir]
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from pirata import analysis, arrange, piano  # noqa: E402


def render(path, bpm, bars, comp, chord_vel, mel_vel):
    b = 60 / bpm
    notes, truth = [], []
    for i, (chord, mel) in enumerate(bars):
        t = i * 4
        for o, d in comp:
            for k, p in enumerate(chord):
                if k == 0 and o > 0:
                    continue
                notes.append(((t + o) * b, (t + o + d) * b, p, chord_vel if k else chord_vel + 10))
        for o, d, p in mel:
            notes.append(((t + o) * b, (t + o + d * 0.95) * b, p, mel_vel))
            truth.append(dict(start=t + o, end=t + o + d, pitch=p))
    piano.save(path, piano.render(notes))
    return truth


CASES = {
    "held_90bpm": (90, [[45, 57, 60, 64], [41, 57, 60, 65], [36, 55, 60, 64], [43, 55, 59, 62]],
                   [[(0, 1, 72), (1, .5, 71), (1.5, .5, 72), (2, 1.5, 76), (3.5, .5, 74)],
                    [(0, 1.5, 72), (1.5, .5, 69), (2, 1, 77), (3, 1, 76)],
                    [(0, .5, 72), (.5, .5, 74), (1, 1, 76), (2, 2, 79)],
                    [(0, 1, 74), (1, 1, 71), (2, 1, 74), (3, 1, 79)]],
                   [(0, 3.8)], 70, 92),
    "stabs_100bpm": (100, [[40, 55, 59, 64], [36, 55, 60, 64], [43, 55, 59, 62], [38, 54, 57, 62]],
                     [[(0, .75, 71), (.75, .75, 74), (1.5, 1, 76), (3, 1, 74)],
                      [(0, 1, 72), (1, .5, 71), (1.5, .5, 69), (2, 2, 67)],
                      [(.5, .5, 67), (1, .75, 71), (1.75, 1.25, 74), (3, 1, 79)],
                      [(0, 1.5, 78), (1.5, .5, 76), (2, 1, 74), (3, .5, 69), (3.5, .5, 71)]],
                     [(0, .4), (.5, .3), (1.5, .4), (2.5, .3), (3, .4)], 66, 95),
    "weave_85bpm": (85, [[38, 57, 62, 65], [34, 58, 62, 65], [41, 57, 60, 65], [45, 57, 61, 64]],
                    [[(0, 1, 69), (1, 1, 67), (2, 1, 65), (3, 1, 64)],
                     [(0, 2, 62), (2, 1, 65), (3, 1, 70)],
                     [(0, 1, 69), (1, .5, 67), (1.5, .5, 65), (2, 2, 72)],
                     [(0, 1, 73), (1, 1, 69), (2, 1, 67), (3, 1, 64)]],
                    [(0, 3.8)], 62, 98),
}


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="pirata_mel_")
    os.makedirs(work, exist_ok=True)
    ok = True
    for name, (bpm, chords, mel, comp, cv, mv) in CASES.items():
        wav = os.path.join(work, name + ".wav")
        truth = render(wav, bpm, list(zip(chords * 2, mel * 2)), comp, cv, mv)
        an = json.loads(json.dumps(analysis.analyze(wav)))
        # Simple: one voice, real melody notes, in time (grid must not be shifted)
        line, _ = arrange.chord_versions(an, "simple")
        match = lambda n, t: abs(n["start"] - t["start"]) < 0.13 and n["pitch"] == t["pitch"]
        hit = sum(1 for t in truth if any(match(n, t) for n in line))
        wrong = sum(1 for n in line if not any(match(n, t) for t in truth))
        voices = max(sum(1 for m in line if m["start"] <= n["start"] < m["end"]) for n in line)
        # Rich: melody on top, nothing crossing above it, no half-step rubs under it
        up, bass = arrange.chord_versions(an, "rich")
        lead = arrange.lead_line(an["notes"])
        lk = {(n["start"], n["pitch"]) for n in lead}
        harm = [v for v in up if (v["start"], v["pitch"]) not in lk]
        cross = rub = 0
        for v in harm:
            for n in lead:
                if v["start"] < n["end"] and v["end"] > n["start"]:
                    cross += v["pitch"] >= n["pitch"]
                    rub += v["pitch"] < n["pitch"] and (n["pitch"] - v["pitch"]) % 12 == 1
        good = hit >= 0.9 * len(truth) and wrong <= 0.15 * len(truth) and voices == 1 \
            and cross == 0 and rub == 0 and len(harm) >= 2 * len(truth)
        ok &= good
        print(f"{name}: simple {hit}/{len(truth)} melody notes, {wrong} wrong, {voices} voice | "
              f"rich {len(harm)} chord notes, {cross} crossings, {rub} rubs -> {'PASS' if good else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
