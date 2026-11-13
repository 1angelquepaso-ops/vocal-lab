"""TITO factory kit: 16 drum sounds synthesised from scratch (no samples, no licences).

Used for pads a song doesn't fill (so fills always have toms + crash), for the
plugin's out-of-the-box sound, and to render test songs with known MIDI.
`variant` changes tuning/decays/filters so tests can use several different kits.
"""
from __future__ import annotations

import numpy as np
from scipy.signal import butter, sosfilt

from .common import PADS, SR


def _t(sec):
    return np.arange(int(sec * SR)) / SR


def _env(t, decay, attack=0.001):
    a = np.clip(t / max(attack, 1e-4), 0, 1)
    return a * np.exp(-t / max(decay, 1e-4))


def _bp(x, lo, hi, order=2):
    sos = butter(order, [lo / (SR / 2), min(hi, SR / 2 - 100) / (SR / 2)], btype="band", output="sos")
    return sosfilt(sos, x)


def _hp(x, f, order=2):
    return sosfilt(butter(order, f / (SR / 2), btype="high", output="sos"), x)


def _lp(x, f, order=2):
    return sosfilt(butter(order, f / (SR / 2), btype="low", output="sos"), x)


def _sweep(t, f0, f1, k):
    f = f1 + (f0 - f1) * np.exp(-t * k)
    return np.sin(2 * np.pi * np.cumsum(f) / SR)


def _metal(t, base, rng):
    ratios = [1.0, 1.342, 1.2312, 1.6532, 1.9523, 2.1523]
    x = sum(np.sign(np.sin(2 * np.pi * base * r * (1 + rng.normal(0, 0.004)) * t)) for r in ratios)
    return x / len(ratios)


def make(key, variant=0):
    rng = np.random.default_rng(1000 + variant * 37 + sum(map(ord, key)))
    v = np.random.default_rng(variant)
    j = lambda a, s=0.12: a * (1 + (v.normal(0, s) if variant else 0))   # per-variant jitter
    noise = lambda n: rng.uniform(-1, 1, n)
    if key == "kick":
        t = _t(0.6)
        x = _sweep(t, j(160), j(48), j(28)) * _env(t, j(0.28)) + 0.3 * _hp(noise(len(t)), 2500) * _env(t, 0.006)
    elif key == "kick2":                       # 808
        t = _t(1.6)
        x = np.tanh(1.6 * _sweep(t, j(70), j(44), 9) * _env(t, j(0.7), 0.002))
    elif key == "snare":
        t = _t(0.45)
        x = 0.55 * _sweep(t, j(240), j(185), 30) * _env(t, j(0.09)) + \
            0.8 * _bp(noise(len(t)), 1200, 9000) * _env(t, j(0.15))
    elif key == "clap":
        t = _t(0.5)
        e = sum(_env(np.clip(t - d, 0, None), 0.007) * (t >= d) for d in (0, 0.011, 0.022, 0.033)) \
            + 0.7 * _env(np.clip(t - 0.035, 0, None), j(0.16)) * (t >= 0.035)
        x = _bp(noise(len(t)), 900, 4000) * e
    elif key == "rim":
        t = _t(0.12)
        x = (np.sin(2 * np.pi * j(1750) * t) + 0.5 * np.sin(2 * np.pi * j(520) * t)) * _env(t, 0.018) \
            + 0.4 * _hp(noise(len(t)), 3000) * _env(t, 0.004)
    elif key in ("chh", "phh"):
        t = _t(0.25)
        d = j(0.045) if key == "chh" else j(0.028)
        x = _hp(0.6 * _metal(t, j(410), rng) + 0.6 * noise(len(t)), 7000 if key == "chh" else 5200) * _env(t, d)
        if key == "phh":
            x *= 0.7
    elif key == "ohh":
        t = _t(0.9)
        x = _hp(0.6 * _metal(t, j(410), rng) + 0.6 * noise(len(t)), 6500) * _env(t, j(0.38))
    elif key.startswith("tom"):
        f = {"tom_lo": 92, "tom_mid": 128, "tom_hi": 176}[key]
        t = _t(0.8)
        x = _sweep(t, j(f * 1.35, 0.06), j(f, 0.06), 14) * _env(t, j(0.3)) + \
            0.25 * _bp(noise(len(t)), 300, 3000) * _env(t, 0.02)
    elif key == "crash":
        t = _t(2.4)
        x = _hp(0.5 * _metal(t, j(340), rng) + noise(len(t)), 3500) * _env(t, j(1.1), 0.002)
    elif key == "ride":
        t = _t(1.8)
        ping = sum(np.sin(2 * np.pi * j(f) * t) for f in (2950, 4170, 5350)) / 3
        x = 0.6 * ping * _env(t, j(0.5)) + 0.35 * _hp(noise(len(t)), 5000) * _env(t, j(0.9))
    elif key == "perc1":                       # timbale
        t = _t(0.6)
        x = (np.sin(2 * np.pi * j(440) * t) + 0.5 * np.sin(2 * np.pi * j(440) * 2.3 * t)) * _env(t, j(0.22)) \
            + 0.5 * _bp(noise(len(t)), 2000, 8000) * _env(t, 0.01)
    elif key == "perc2":                       # shaker
        t = _t(0.18)
        x = _bp(noise(len(t)), 5000, 14000) * _env(t, j(0.045), 0.012)
    elif key == "fx":                          # cowbell
        t = _t(0.5)
        x = _bp(np.sign(np.sin(2 * np.pi * j(545) * t)) + np.sign(np.sin(2 * np.pi * j(815) * t)), 400, 3500) \
            * _env(t, j(0.16))
    else:
        raise KeyError(key)
    x = x - np.mean(x[:64]) * 0
    fade = np.minimum(1, (len(x) - np.arange(len(x))) / (0.01 * SR))
    x = x * fade
    return (x / (np.max(np.abs(x)) + 1e-9) * 0.9).astype(np.float32)


def kit(variant=0):
    return {k: make(k, variant) for k, _, _ in PADS}


def save_factory(folder):
    import os
    import soundfile as sf
    os.makedirs(folder, exist_ok=True)
    out = []
    for i, (k, name, _) in enumerate(PADS):
        p = os.path.join(folder, f"{i + 1:02d}_{k}.wav")
        sf.write(p, make(k, 0), SR, subtype="PCM_16")
        out.append(p)
    return out


def render(hits, sounds, length_s, reverb=0.0, seed=0):
    """hits = [(time_s, key, vel)] -> mono float32. Optional small room."""
    y = np.zeros(int((length_s + 2.5) * SR), np.float32)
    for t, k, vel in hits:
        s = sounds[k]
        i = int(t * SR)
        if i >= len(y):
            continue
        g = (vel / 127.0) ** 1.6
        n = min(len(s), len(y) - i)
        y[i:i + n] += g * s[:n]
    if reverb > 0:
        rng = np.random.default_rng(seed)
        ir_t = _t(0.6)
        ir = rng.normal(0, 1, len(ir_t)) * np.exp(-ir_t / 0.12)
        ir = _lp(ir, 6000) * reverb * 0.05
        ir[0] = 1.0
        from scipy.signal import fftconvolve
        y = fftconvolve(y, ir)[: len(y)].astype(np.float32)
    return y / (np.max(np.abs(y)) + 1e-9) * 0.9
