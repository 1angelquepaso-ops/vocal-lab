"""Grand piano preview renderer (Salamander Grand Piano V3, CC-BY 3.0).

Real Yamaha C5 recordings sampled every minor third at four velocity layers.
Each note picks the nearest sample, resamples by at most +-1.5 semitones,
and gets a natural damper release at its note-off.
"""
from __future__ import annotations

import glob
import os
import sys
from functools import lru_cache

import numpy as np
import soundfile as sf

SR = 44100
LAYERS = [5, 9, 12, 15]                 # Salamander velocity layer numbers
LAYER_TOP = [48, 80, 104, 127]          # MIDI velocity upper bound per layer
NAMES = {"C": 0, "Ds": 3, "Fs": 6, "A": 9}


def _dir() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.path.join(here, "piano")]
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands.append(os.path.join(base, "pirata", "piano"))
    for c in cands:
        if os.path.isdir(c):
            return c
    raise FileNotFoundError("piano samples not found")


@lru_cache(maxsize=1)
def _index():
    idx = {}
    for f in glob.glob(os.path.join(_dir(), "*.flac")):
        b = os.path.basename(f)[:-5]
        name, v = b.split("v")
        letter = name.rstrip("0123456789")
        octv = int(name[len(letter):])
        midi = 12 * (octv + 1) + NAMES[letter]
        idx[(midi, int(v))] = f
    roots = sorted({m for m, _ in idx})
    return idx, roots


@lru_cache(maxsize=256)
def _sample(midi: int, layer: int):
    idx, _ = _index()
    y, sr = sf.read(idx[(midi, layer)], dtype="float32")
    if sr != SR:
        n = int(len(y) * SR / sr)
        t = np.linspace(0, len(y) - 1, n)
        y = np.stack([np.interp(t, np.arange(len(y)), y[:, c]) for c in range(y.shape[1])], 1)
    return y.astype(np.float32)


def _note_audio(pitch: int, vel: int, dur_s: float) -> np.ndarray:
    _, roots = _index()
    root = min(roots, key=lambda r: abs(r - pitch))
    li = next(i for i, top in enumerate(LAYER_TOP) if vel <= top)
    s = _sample(root, LAYERS[li])
    ratio = 2 ** ((pitch - root) / 12)
    rel = 0.35 if pitch < 60 else 0.25
    n_out = int((dur_s + rel) * SR)
    n_avail = int((len(s) - 1) / ratio)
    n_out = min(n_out, n_avail)
    t = np.arange(n_out) * ratio
    i0 = t.astype(np.int64)
    fr = (t - i0)[:, None]
    y = s[i0] * (1 - fr) + s[np.minimum(i0 + 1, len(s) - 1)] * fr
    # blend gain within the layer so velocities in between sound continuous
    lo = LAYER_TOP[li - 1] if li else 1
    g = 0.55 + 0.45 * (vel - lo) / max(1, LAYER_TOP[li] - lo)
    y = y * g
    k = int(dur_s * SR)
    if k < len(y):
        r = len(y) - k
        y[k:] *= np.exp(-np.linspace(0, 7, r))[:, None]
    return y


def render(notes, length_s: float | None = None, gain_db: float = -3.0) -> np.ndarray:
    """notes: iterable of (start_s, end_s, pitch, velocity). Returns stereo float32."""
    notes = [n for n in notes if 21 <= n[2] <= 108]
    end = max([n[1] for n in notes], default=0) + 1.5
    total = int((length_s if length_s else end) * SR)
    out = np.zeros((max(total, 1), 2), np.float32)
    for s, e, p, v in notes:
        a = _note_audio(int(p), int(np.clip(v, 1, 127)), max(0.05, e - s))
        i = int(round(s * SR))
        if i >= total:
            continue
        if i < 0:                        # starts before the render: play its tail
            a, i = a[-i:], 0
            if len(a) == 0:
                continue
        m = min(len(a), total - i)
        out[i:i + m] += a[:m]
    peak = np.abs(out).max()
    target = 10 ** (gain_db / 20)
    if peak > target:
        out *= target / peak
    return out


def save(path: str, audio: np.ndarray):
    sf.write(path, audio, SR, subtype="PCM_16")
