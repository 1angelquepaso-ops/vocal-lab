"""MIRRORWALL engine (called by the plugin; prints one JSON line).

  tune --in FILE --out tuned.wav [--key auto|C..B] [--scale minor|major|...]
       [--speed MS] [--amount 0..1] [--humanize 0..1]
  key  --file BEAT
  fit  --ref REF --raw RAW --out preset.json [--name NAME] [--pre tube|console|clean]
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import traceback

import numpy as np
import soundfile as sf

SR = 44100


def _progress(path):
    def f(frac, msg):
        if not path:
            return
        try:
            with open(path, "w") as fh:
                fh.write(f"{frac:.2f}|{msg}")
        except OSError:
            pass
    return f


def load_mono(path):
    import librosa
    y, sr = sf.read(path, always_2d=True)
    y = y.mean(1)
    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    return y.astype(np.float64)


def cmd_tune(a):
    from . import tune
    say = _progress(a.progress)
    say(0.1, "Reading vocal")
    y = load_mono(a.inp)
    key = a.key
    scale = a.scale
    if a.amount > 0:
        say(0.3, "Tuning")
        y2, tonic, scale = tune.autotune(y, SR, key, scale, a.speed, a.amount, a.humanize)
    else:
        y2, tonic = y, None
    sf.write(a.out, y2.astype(np.float32), SR, subtype="FLOAT")
    say(1.0, "Done")
    return dict(ok=True, out=a.out, key=tune.NOTES[tonic] if tonic is not None else None, scale=scale,
                seconds_audio=round(len(y) / SR, 1))


def cmd_key(a):
    from . import tune
    t, s = tune.key_from_audio(a.file)
    return dict(ok=True, key=tune.NOTES[t], scale=s)


def _finite(o):
    """JSON-safe: NaN/inf -> None (JUCE's JSON reader rejects NaN, which used to lose the preset)."""
    if isinstance(o, dict):
        return {k: _finite(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [_finite(v) for v in o]
    if isinstance(o, (float, np.floating)):
        return float(o) if np.isfinite(o) else None
    if isinstance(o, np.integer):
        return int(o)
    return o


def cmd_fit(a):
    from . import fit, tune
    say = _progress(a.progress)
    say(0.05, "Reading the reference")
    m = fit.measure_ref(a.ref)
    T = fit.target_from([m])
    say(0.25, "Preparing your vocal")
    y = load_mono(a.raw)
    # fit on up to 45 s of the busiest part of the raw take
    hop = SR
    n = len(y) // hop
    if n > 45:
        e = np.array([np.mean(y[i * hop:(i + 1) * hop] ** 2) for i in range(n)])
        best = int(np.argmax(np.convolve(e, np.ones(45), "valid")))
        y = y[best * hop:(best + 45) * hop]
    td = fit.tune_defaults(T["within10"])
    if td["amount"] > 0:
        say(0.35, "Tuning for the fit")
        y = tune.autotune(y, SR, "auto", "auto", td["speed"], td["amount"], td["humanize"])[0]
    steps = iter([(0.45, "Matching compression"), (0.6, "Matching EQ"), (0.75, "Matching de-esser"),
                  (0.85, "Matching space")])

    def log(msg):
        try:
            f, s = next(steps)
            say(f, s)
        except StopIteration:
            pass
    P = fit.fit_preset(y, T, name=a.name or os.path.splitext(os.path.basename(a.ref))[0][:40],
                       pre_type=a.pre, log=log)
    P["tune"] = td
    P["source"] = os.path.basename(a.ref)
    P["role"] = "lead"
    with open(a.out, "w") as fh:
        json.dump(_finite(P), fh, indent=1)
    say(1.0, "Done")
    return dict(ok=True, preset=a.out, name=P["name"], eq_err=P["fit"]["eq_err"])


def main(argv=None):
    p = argparse.ArgumentParser(prog="mirrorwall-engine")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("tune")
    s.add_argument("--in", dest="inp", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--key", default="auto")
    s.add_argument("--scale", default="minor")
    s.add_argument("--speed", type=float, default=20.0)
    s.add_argument("--amount", type=float, default=1.0)
    s.add_argument("--humanize", type=float, default=0.0)
    s.add_argument("--progress", default=None)
    s = sub.add_parser("key")
    s.add_argument("--file", required=True)
    s = sub.add_parser("fit")
    s.add_argument("--ref", required=True)
    s.add_argument("--raw", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--name", default=None)
    s.add_argument("--pre", default="tube", choices=["tube", "console", "clean"])
    s.add_argument("--progress", default=None)
    a = p.parse_args(argv)
    t = time.time()
    try:
        res = {"tune": cmd_tune, "key": cmd_key, "fit": cmd_fit}[a.cmd](a)
        res["seconds"] = round(time.time() - t, 2)
    except Exception as e:
        res = dict(ok=False, error=f"{type(e).__name__}: {e}", trace=traceback.format_exc()[-1500:])
    sys.stdout.write(json.dumps(_finite(res)) + "\n")
    sys.stdout.flush()
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
