"""Real-sample bench (TITO PUENTE lesson): run PIRATA on real loops that have no answer
key and measure what CAN be measured without one.

    python -m pirata.realbench FOLDER [--json result.json]

The true BPM is read from the file name ("<name>__<bpm>.wav", "... 90bpm ...", 0 = unknown)
but the engine is NOT given it: each file is analysed from a copy with a neutral name,
so the tempo reading is tested for real.

Per sample:
  tempo    detected BPM vs the file name (x2 / ÷2 counted right, 3:2 / 4:3 counted WRONG)
  recall   share of clearly audible attacks (independent librosa detector, within 24 dB of
           the sample's loud attacks) that have a PIRATA note starting within 30 ms
  ms       median distance of those matches (Keep timing positions)
  steady   tempo steady (spread of the notes against the fitted grid < 25 ms)
  loose    spread (ms) of the on-beat / on-8th notes around the grid (human looseness)
  swing    16th swing % (50 straight, 66.7 triplet); trip = share of notes on triplet slots
  chords   chord changes per bar (a very high number = flicker on dense comping)
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import shutil
import sys
import tempfile
import time

import numpy as np

from . import analysis

EXTS = ("wav", "aif", "aiff", "flac", "mp3", "ogg")


def true_bpm(path):
    b = os.path.splitext(os.path.basename(path))[0]
    m = re.search(r"__(\d{2,3}(?:\.\d+)?)$", b)
    if m:
        return float(m.group(1))
    return analysis.bpm_from_name(path) or 0.0


def _ref_onsets(path):
    import librosa
    y, sr = librosa.load(path, sr=22050, mono=True)
    on = librosa.onset.onset_detect(y=y, sr=sr, hop_length=128, units="time", delta=0.05, backtrack=True)
    rms = librosa.feature.rms(y=y, hop_length=128, frame_length=1024)[0]
    tt = librosa.times_like(rms, sr=sr, hop_length=128)
    lv = np.array([rms[(tt >= t) & (tt < t + 0.04)].max(initial=1e-9) for t in on])
    lvdb = 20 * np.log10(lv + 1e-9)
    loud = np.percentile(lvdb, 90) if len(lvdb) else 0
    return on[lvdb - loud > -24]


def tempo_verdict(got, true):
    if not true:
        return "?"
    for k, tag in ((1, "ok"), (2, "ok x2"), (0.5, "ok /2")):
        if abs(got - true * k) <= 0.06 * k + 0.02:
            return tag
    for k in (1.5, 2 / 3, 4 / 3, 0.75):
        if abs(got - true * k) < 0.5:
            return "WRONG 3:2" if k in (1.5, 2 / 3) else "WRONG 4:3"
    return "WRONG"


def bench_one(path, work):
    tmp = os.path.join(work, "sample" + os.path.splitext(path)[1].lower())
    shutil.copy(path, tmp)                       # neutral name: no BPM hint
    t = time.time()
    an = analysis.analyze(tmp)
    secs = time.time() - t
    spb = 60 / an["bpm"]
    real = np.array(sorted({round(an["grid_start"] + n["ts"] * spb, 4) for n in an["notes"]}))
    ref = _ref_onsets(tmp)
    if len(ref) and len(real):
        i = np.clip(np.searchsorted(real, ref), 1, max(1, len(real) - 1))
        d = np.minimum(np.abs(real[np.minimum(i, len(real) - 1)] - ref), np.abs(real[i - 1] - ref))
        hit = d < 0.030
        recall, ms = float(hit.mean()), 1000 * float(np.median(d[hit])) if hit.any() else 99.0
    else:
        recall, ms = 0.0, 99.0
    bars = max(1.0, max([c["end"] for c in an["chords"]], default=4) / 4)
    tb = true_bpm(path)
    tm = an.get("timing", {})
    return dict(file=os.path.basename(path), true_bpm=tb, bpm=an["bpm"], tempo=tempo_verdict(an["bpm"], tb),
                key=an["key"], recall=round(recall, 3), ms=round(ms, 1),
                steady=tm.get("steady"), loose=tm.get("loose_ms"), swing=tm.get("swing_16"),
                trip=tm.get("triplet_share"), metric_fix=tm.get("metric_fix"),
                chords_per_bar=round(len(an["chords"]) / bars, 2), notes=len(an["notes"]),
                seconds=round(secs, 1))


def main(argv=None):
    p = argparse.ArgumentParser(prog="pirata.realbench")
    p.add_argument("folder")
    p.add_argument("--json", default=None)
    a = p.parse_args(argv)
    files = sorted(f for e in EXTS for f in glob.glob(os.path.join(a.folder, "**", "*." + e), recursive=True))
    if not files:
        print("no audio files in", a.folder)
        return 1
    rows = []
    with tempfile.TemporaryDirectory() as work:
        for f in files:
            try:
                r = bench_one(f, work)
            except Exception as e:  # keep going on a bad file
                r = dict(file=os.path.basename(f), error=f"{type(e).__name__}: {e}")
            rows.append(r)
            if "error" in r:
                print(f"{r['file'][:38]:38s}  ERROR {r['error']}")
                continue
            print(f"{r['file'][:38]:38s} {r['bpm']:7.2f} ({r['true_bpm'] or '?':>5}) {r['tempo']:10s} "
                  f"rec {r['recall']:.2f} {r['ms']:4.1f}ms "
                  f"{'steady' if r['steady'] else 'DRIFT '} loose {r['loose']:4.1f} "
                  f"swing {r['swing']:4.1f} trip {r['trip']:.2f} "
                  f"chords/bar {r['chords_per_bar']:.2f}  {r['key']}")
    ok = [r for r in rows if "error" not in r]
    known = [r for r in ok if r["true_bpm"]]
    if ok:
        print(f"\n{len(ok)} samples | tempo right {sum(r['tempo'].startswith('ok') for r in known)}/{len(known)} known"
              f" | mean recall {np.mean([r['recall'] for r in ok]):.3f}"
              f" | median timing {np.median([r['ms'] for r in ok]):.1f} ms"
              f" | steady {sum(bool(r['steady']) for r in ok)}/{len(ok)}")
    if a.json:
        with open(a.json, "w") as fh:
            json.dump(rows, fh, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
