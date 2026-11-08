"""Smoke-test material: a sung-like dry line and a 'processed' reference of it."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import numpy as np, soundfile as sf
from mirrorwall import dsp
sr = 44100
notes = [59, 61, 62, 61, 59, 57, 59, 62, 64, 62, 61, 59] * 3
y = []
for n in notes:
    f = 440 * 2 ** ((n - 69) / 12); t = np.arange(int(0.35 * sr)) / sr
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t)
    ph = 2 * np.pi * f * np.cumsum(vib) / sr
    tone = sum(np.sin(k * ph) / k for k in range(1, 8)) * np.minimum(1, t / 0.03) * np.minimum(1, (0.35 - t) / 0.05)
    y.append(0.3 * tone); y.append(np.zeros(int(0.12 * sr)))
y = np.concatenate(y)
sf.write("mw_raw.wav", y.astype("float32"), sr)
P = json.load(open(os.path.join(os.path.dirname(__file__), "..", "mirrorwall", "presets", "SAIN_JHN.json")))
sf.write("mw_ref.wav", dsp.process(y, P).astype("float32"), sr)
