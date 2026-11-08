"""Write a short sung-like test melody (B minor) for the build's smoke test."""
import numpy as np, soundfile as sf
sr = 44100
notes = [59, 61, 62, 61, 59, 57, 59, 62, 64, 62, 61, 59]
y = []
for n in notes:
    f = 440 * 2 ** ((n - 69) / 12); t = np.arange(int(0.35 * sr)) / sr
    vib = 1 + 0.004 * np.sin(2 * np.pi * 5.5 * t)
    ph = 2 * np.pi * f * np.cumsum(vib) / sr
    tone = sum(np.sin(k * ph) / k for k in range(1, 6)) * np.minimum(1, t / 0.03) * np.minimum(1, (0.35 - t) / 0.05)
    y.append(0.3 * tone); y.append(np.zeros(int(0.05 * sr)))
sf.write('test_vocal.wav', np.concatenate(y).astype('float32'), sr)
