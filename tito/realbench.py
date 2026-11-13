"""Real-stem bench: run the engine on real (stem-separated) drum tracks that have no answer key
and measure what CAN be measured without one.

    python -m tito.realbench FOLDER [--out DIR] [--json result.json]

File names may carry the true BPM as  "<name>__<bpm>.wav"  (0 = unknown).

Per song:
  recall   share of clearly audible onsets (independent librosa detector, within 24 dB of the
           song's loud hits) that TITO wrote a note for (30 ms)
  prec     share of TITO's hit times confirmed by a sensitive detector (full band or highs alone)
  ms       median timing error of the matches
  tempo    detected BPM vs the BPM in the file name (x2 / ÷2 counted right, 3:2 counted WRONG)
  flags    naming red flags measured on the chops TITO cut (not on its own features):
             crash_busy   Crash plays more than once per bar on average (a pulse, not a crash)
             tom_bright   a Tom pad whose chop has no low end and is bright (hat / perc on a tom pad)
             hat_missing  no hat pad, but a bright short sound plays >= 2x per bar (a hat on another pad)
             kick_thin    Kick pad chop with almost no low end
             snare_boom   Snare / Clap pad chop that is mostly low end (a kick / 808 on the snare pad)
  stack    share of hit moments that fire 2+ pads (layering is real, but a high share = ghost notes)
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys
import tempfile
import time

import numpy as np

from .common import PADS, PAD_INDEX


def _ref_onsets(path):
    import librosa
    y, sr = librosa.load(path, sr=44100, mono=True)
    on = librosa.onset.onset_detect(y=y, sr=sr, hop_length=128, units="time", delta=0.05)
    rms = librosa.feature.rms(y=y, hop_length=128, frame_length=1024)[0]
    tt = librosa.times_like(rms, sr=sr, hop_length=128)
    lv = np.array([rms[(tt >= t) & (tt < t + 0.04)].max(initial=1e-9) for t in on])
    lvdb = 20 * np.log10(lv + 1e-9)
    loud = np.percentile(lvdb, 90) if len(lvdb) else 0
    # sensitive set (quiet hits, highs on their own) to judge whether a written note is real
    from scipy.signal import butter, sosfilt
    hp = sosfilt(butter(4, 2000, "high", fs=sr, output="sos"), y)
    s1 = librosa.onset.onset_detect(y=y, sr=sr, hop_length=128, units="time", delta=0.02)
    s2 = librosa.onset.onset_detect(y=hp, sr=sr, hop_length=128, units="time", delta=0.02)
    return on, lvdb - loud, np.sort(np.concatenate([s1, s2]))


def _near(x, ref):
    if not len(ref) or not len(x):
        return np.full(len(x), 9.0)
    i = np.clip(np.searchsorted(ref, x), 1, len(ref) - 1) if len(ref) > 1 else np.zeros(len(x), int)
    return np.minimum(np.abs(ref[i] - x), np.abs(ref[np.maximum(i - 1, 0)] - x))


def chop_profile(path):
    import soundfile as sf
    y, sr = sf.read(path, always_2d=True)
    y = y.mean(axis=1)
    n = min(len(y), int(0.06 * sr))                # the attack + body, not the tail
    seg = y[:n] * np.hanning(n)
    S = np.abs(np.fft.rfft(seg, 4096)) ** 2
    f = np.fft.rfftfreq(4096, 1 / sr)
    cen = float((S * f).sum() / (S.sum() + 1e-12))
    low = float(S[f < 150].sum() / (S.sum() + 1e-12))
    return dict(centroid=cen, low=low, length=len(y) / sr)


def flags_for(an):
    bars = max(1, an["bars"])
    per = {}
    for h in an["hits"]:
        per.setdefault(h["pad"], []).append(h["beat"])
    prof = {p["pad"]: chop_profile(p["file"]) for p in an["pads"]}
    fl = []
    crash = PAD_INDEX["crash"]
    if len(per.get(crash, [])) > 1.0 * bars:
        fl.append("crash_busy")
    for k in ("tom_lo", "tom_mid", "tom_hi"):
        p = PAD_INDEX[k]
        if p in prof and prof[p]["low"] < 0.08 and prof[p]["centroid"] > 2500:
            fl.append("tom_bright")
            break
    hats = [PAD_INDEX[k] for k in ("chh", "ohh", "phh")]
    if not any(per.get(p) for p in hats):
        for p, b in per.items():
            if p in prof and prof[p]["centroid"] > 3000 and prof[p]["low"] < 0.08 and len(b) >= 2 * bars * 0.5 \
                    and p not in (PAD_INDEX["snare"], PAD_INDEX["clap"]):
                fl.append("hat_missing")
                break
    kp = PAD_INDEX["kick"]
    if kp in prof and prof[kp]["low"] < 0.15:
        fl.append("kick_thin")
    for k in ("snare", "clap"):
        p = PAD_INDEX[k]
        if p in prof and prof[p]["low"] > 0.6:
            fl.append("snare_boom")
            break
    return fl, prof


def tempo_ok(bpm, true):
    if not true:
        return None
    r = bpm / true
    return any(abs(r - x) < 0.02 * x for x in (0.5, 1.0, 2.0))


def run_one(path, out):
    from . import transcribe
    m = re.search(r"__(\d+)\.\w+$", path)
    true = float(m.group(1)) if m else 0.0
    t0 = time.time()
    an = transcribe.analyze(path, out)
    secs = time.time() - t0
    on, rel, sens = _ref_onsets(path)
    aud = on[rel > -24]
    ht = np.array(sorted({h["t"] for h in an["hits"]}))
    d = _near(aud, ht)
    d2 = _near(ht, sens)
    from collections import Counter
    c = Counter(round(h["t"], 3) for h in an["hits"])
    stack = float(np.mean([v > 1 for v in c.values()])) if c else 0.0
    fl, prof = flags_for(an)
    return dict(name=os.path.basename(path), bpm=an["bpm"], true=true, tempo_ok=tempo_ok(an["bpm"], true),
                steady=an["steady"], bars=an["bars"], hits=len(an["hits"]), sounds=an["n_sounds"],
                recall=round(float(np.mean(d < 0.03)) if len(d) else 0, 3),
                prec=round(float(np.mean(d2 < 0.03)) if len(d2) else 0, 3),
                ms=round(1000 * float(np.median(d[d < 0.03])) if np.any(d < 0.03) else 0, 1),
                stack=round(stack, 3), flags=fl,
                pads={PADS[p["pad"]][0]: p["hits"] for p in an["pads"]},
                sections=[f'{s["label"]}:{s["start"]}-{s["end"]}' for s in an["sections"]],
                secs=round(secs, 1))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("--out", default=None)
    ap.add_argument("--json", default=None)
    a = ap.parse_args(argv)
    out = a.out or tempfile.mkdtemp(prefix="tito_real_")
    files = sorted(f for f in glob.glob(os.path.join(a.folder, "*")) if re.search(r"\.(wav|mp3|flac)$", f, re.I))
    res = []
    for f in files:
        try:
            r = run_one(f, os.path.join(out, os.path.splitext(os.path.basename(f))[0]))
        except Exception as e:                                    # noqa: BLE001
            r = dict(name=os.path.basename(f), error=repr(e))
            print(f"{r['name'][:30]:30s} ERROR {e}")
            res.append(r)
            continue
        res.append(r)
        t = "" if r["tempo_ok"] is None else ("ok " if r["tempo_ok"] else "BAD")
        print(f"{r['name'][:30]:30s} bpm {r['bpm']:6.2f} {t:3s} {'steady' if r['steady'] else 'drift '} "
              f"rec {r['recall']:.2f} prec {r['prec']:.2f} {r['ms']:4.1f}ms stack {r['stack']:.2f} "
              f"pads {len(r['pads']):2d} flags {','.join(r['flags']) or '-'}  | {' '.join(r['sections'])}")
        sys.stdout.flush()
    ok = [r for r in res if "error" not in r]
    if ok:
        print(f"MEAN recall {np.mean([r['recall'] for r in ok]):.3f}  prec {np.mean([r['prec'] for r in ok]):.3f}  "
              f"flags {sum(len(r['flags']) for r in ok)}  tempo right "
              f"{sum(1 for r in ok if r['tempo_ok'])}/{sum(1 for r in ok if r['tempo_ok'] is not None)}  "
              f"errors {len(res) - len(ok)}")
    if a.json:
        json.dump(res, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
