"""Accuracy bench: known drum MIDI -> audio (factory kit variants) -> transcribe -> score.

    python -m tito.bench [--quick] [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import tempfile

import numpy as np

from . import gmd, kit, transcribe
from .common import PADS, PAD_INDEX, SR


def beats_to_hits(beats_hits, bpm, drift=0.0, t0=0.25, seed=0):
    """Beat positions -> seconds, optional slow tempo drift (live band)."""
    rng = np.random.default_rng(seed)
    per = 60 / bpm
    out = []
    for b, k, v in beats_hits:
        warp = drift * np.sin(2 * np.pi * b / 64) * 8 * per if drift else 0.0
        out.append((t0 + b * per + warp, k, v))
    return out


def trap_song(bars=16, seed=0):
    """Authored trap test: 808, clap on 3, rolling hats, open hat, perc."""
    rng = np.random.default_rng(seed)
    h = []
    for bar in range(bars):
        b0 = bar * 4
        for s in (0, 1.5, 2.75):
            h.append((b0 + s, "kick2", 110))
        h.append((b0 + 2, "clap", 118))
        st = 0.0
        while st < 4:
            if bar % 4 == 3 and 3 <= st < 4:
                for r in range(6):
                    h.append((b0 + st + r / 6, "chh", 60 + 8 * r))
                st += 1
                continue
            h.append((b0 + st, "chh", 92 if st % 1 == 0 else 70))
            st += 0.5
        if bar % 2 == 1:
            h.append((b0 + 3.5, "ohh", 90))
        if bar % 4 == 0:
            h.append((b0, "crash", 105))
            h.append((b0, "kick", 120))
        h.append((b0 + 1.75, "perc1", 80))
    return sorted(h)


FAMILY = {"chh": "hats", "phh": "hats", "ohh": "hats", "tom_lo": "toms", "tom_mid": "toms", "tom_hi": "toms",
          "kick2": "kick"}


def family(truth):
    return [(t, FAMILY.get(k, k), v) for t, k, v in truth]


# produced-beat test patterns: 16 steps per bar, "x" = hit (velocity 110), "o" = soft (70)
PRODUCED = {
    "boombap": (90, {"kick": "x......x..x.....", "snare": "....x.......x...", "chh": "x.x.x.x.x.x.x.x.",
                     "ohh": "..............o.", "rim": "...........o...."}),
    "reggaeton": (95, {"kick": "x...x...x...x...", "snare": "...x..x....x..x.", "chh": ".oxo.oxo.oxo.oxx",
                       "perc1": "..x...x...x...x."}),
    "afrobeats": (108, {"kick": "x.....x...x.....", "rim": "...x..x....x..x.", "clap": "....x.......x...",
                        "perc2": ".oxo.oxo.oxo.oxo", "chh": "..x...x...x...x."}),
    "house": (124, {"kick": "x...x...x...x...", "clap": "....x.......x...", "ohh": "..x...x...x...x.",
                    "chh": ".o.o.o.o.o.o.o.o", "ride": "x...x...x...x..."}),
    "drill": (142, {"kick2": "x......x..x.....", "snare": "........x.......", "chh": "x..x..x.x..x..x.",
                    "perc1": "......x.......x.", "rim": "...........x...."}),
}


def produced_song(style, bars=16, seed=0):
    rng = np.random.default_rng(seed)
    bpm, pat = PRODUCED[style]
    h = []
    for bar in range(bars):
        for k, row in pat.items():
            for i, ch in enumerate(row):
                if ch != ".":
                    v = 110 if ch == "x" else 70
                    h.append((bar * 4 + i / 4, k, int(np.clip(v + rng.normal(0, 6), 30, 127))))
        if bar % 8 == 7:                         # a tom fill into a crash every 8 bars
            for j, k in enumerate(["tom_hi", "tom_hi", "tom_mid", "tom_mid", "tom_lo", "tom_lo"]):
                h.append((bar * 4 + 2.5 + j * 0.25, k, 100))
        if bar % 8 == 0:
            h.append((bar * 4, "crash", 110))
    h = [x for x in h if not (x[1] not in ("crash",) and bar_fill(x, bars))]
    return sorted(h), bpm


def bar_fill(x, bars):
    return False


def score(truth, pred, tol=0.03):
    """Per truth sound: F1 after mapping each predicted pad to the truth sound it mostly hits."""
    keys = sorted({k for _, k, _ in truth})
    pads = sorted({p["pad"] for p in pred})
    tt = {k: np.array([t for t, kk, _ in truth if kk == k]) for k in keys}
    conf = np.zeros((len(pads), len(keys)))
    for pi, pad in enumerate(pads):
        pt = np.array([p["t"] for p in pred if p["pad"] == pad])
        for ki, k in enumerate(keys):
            if len(pt) and len(tt[k]):
                d = np.abs(pt[:, None] - tt[k][None, :]).min(axis=1)
                conf[pi, ki] = np.sum(d < tol)
    # a pad belongs to the truth sound it covers the biggest SHARE of (claps sit on hats:
    # the clap pad covers 100% of claps but only 11% of hats)
    npad = np.array([sum(1 for p in pred if p["pad"] == pad) for pad in pads])[:, None]
    nkey = np.array([len(tt[k]) for k in keys])[None, :]
    f1pk = 2 * conf / (npad + nkey)
    mapping = {pads[pi]: keys[int(np.argmax(f1pk[pi]))] for pi in range(len(pads)) if conf[pi].max() > 0}
    res = {}
    for k in keys:
        mine = [p["t"] for p in pred if mapping.get(p["pad"]) == k]
        tk = tt[k]
        if not len(tk):
            continue
        mine = np.array(mine)
        tp = 0
        used = np.zeros(len(mine), bool)
        timing = []
        for t in tk:
            if len(mine):
                d = np.abs(mine - t)
                d[used] = 9
                j = int(np.argmin(d))
                if d[j] < tol:
                    used[j] = True
                    tp += 1
                    timing.append(mine[j] - t)
        prec = tp / max(1, len(mine))
        rec = tp / len(tk)
        f1 = 2 * prec * rec / max(1e-9, prec + rec)
        named = sum(1 for p, kk in mapping.items() if kk == k and PADS[p][0] == k) > 0
        res[k] = dict(n=int(len(tk)), f1=round(f1, 3), rec=round(rec, 3), prec=round(prec, 3),
                      ms=round(1000 * float(np.median(np.abs(timing))) if timing else 0, 1), named=named)
    allf = np.average([r["f1"] for r in res.values()], weights=[r["n"] for r in res.values()])
    return res, float(allf), mapping


def run_case(name, beats_hits, bpm, variant, out, drift=0.0, reverb=0.0, seed=0):
    import soundfile as sf
    hits = beats_to_hits(beats_hits, bpm, drift=drift, seed=seed)
    snd = kit.kit(variant)
    length = max(t for t, _, _ in hits) + 1
    y = kit.render(hits, snd, length, reverb=reverb, seed=seed)
    wav = os.path.join(out, f"{name}.wav")
    sf.write(wav, y, SR)
    an = transcribe.analyze(wav, os.path.join(out, name), bpm=None)
    res, f1, mapping = score(hits, an["hits"])
    fam = score(family(hits), an["hits"])[1]
    # tempo counts as right at the true BPM or its double/half (x2 / ÷2 button in the plugin)
    ratio = an["bpm"] / bpm
    tempo_ok = any(abs(ratio - r) < 0.02 * r for r in (0.5, 1.0, 2.0))
    return dict(name=name, bpm_true=round(bpm, 2), bpm=an["bpm"], f1=round(f1, 3), fam=round(fam, 3),
                tempo_ok=tempo_ok, per=res,
                sounds=an["n_sounds"], pads=[PADS[p["pad"]][0] for p in an["pads"]], an=an)


def cases(quick=False):
    rows = [r for r in gmd.index() if r["beat_type"] == "beat" and r["time_signature"] == "4-4"
            and r["split"] == "test"]
    rng = np.random.default_rng(7)
    styles = {}
    for r in rows:
        styles.setdefault(r["style"].split("/")[0], []).append(r)
    picks = []
    for st in sorted(styles):
        picks.append(styles[st][int(rng.integers(len(styles[st])))])
    if quick:
        picks = picks[:4]
    out = []
    for i, r in enumerate(picks):
        h, bpm = gmd.read(r)
        h = [x for x in h if x[0] < 64]                 # first 16 bars
        out.append((f"gmd_{r['style'].replace('/', '-')}_{i}", h, bpm, 1 + i % 4, 0.0, 0.3 * (i % 2)))
    out.append(("trap_808", trap_song(), 140.0, 0, 0.0, 0.0))
    for i, st in enumerate(PRODUCED):
        h, bpm = produced_song(st, seed=i)
        out.append((f"prod_{st}", h, float(bpm), (i + 1) % 6, 0.0, 0.2 * (i % 2)))
    out.append(("trap_808_v3", trap_song(seed=3), 143.0, 3, 0.0, 0.4))
    if not quick and picks:
        h, bpm = gmd.read(picks[0])
        out.append(("live_drift", [x for x in h if x[0] < 64], bpm, 2, 0.012, 0.3))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--out", default=None)
    ap.add_argument("--min-f1", type=float, default=0.0)
    a = ap.parse_args(argv)
    out = a.out or tempfile.mkdtemp(prefix="tito_bench_")
    os.makedirs(out, exist_ok=True)
    allres = []
    for name, h, bpm, var, drift, rev in cases(a.quick):
        r = run_case(name, h, bpm, var, out, drift=drift, reverb=rev)
        allres.append(r)
        worst = sorted(r["per"].items(), key=lambda kv: kv[1]["f1"])[:3]
        print(f"{name:34s} bpm {r['bpm_true']:6.1f}->{r['bpm']:6.1f} {'ok ' if r['tempo_ok'] else 'BAD'} "
              f"F1 {r['f1']:.3f} fam {r['fam']:.3f} sounds {r['sounds']:2d}  "
              f"weakest: " + ", ".join(f"{k} {v['f1']:.2f}(n{v['n']})" for k, v in worst))
    mean = float(np.mean([r["f1"] for r in allres]))
    fam = float(np.mean([r["fam"] for r in allres]))
    tempo = sum(r["tempo_ok"] for r in allres)
    print(f"MEAN F1 {mean:.3f}  family F1 {fam:.3f}  tempo right {tempo}/{len(allres)}")
    json.dump([{k: v for k, v in r.items() if k != "an"} for r in allres], open(os.path.join(out, "bench.json"), "w"),
              indent=1)
    return 0 if mean >= a.min_f1 else 1


if __name__ == "__main__":
    sys.exit(main())
