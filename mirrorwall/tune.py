"""Offline pitch correction (Praat PSOLA) with a user key/scale."""
import numpy as np

NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
SCALES = {
    "major": (0, 2, 4, 5, 7, 9, 11),
    "minor": (0, 2, 3, 5, 7, 8, 10),
    "harmonic minor": (0, 2, 3, 5, 7, 8, 11),
    "phrygian": (0, 1, 3, 5, 7, 8, 10),
    "chromatic": tuple(range(12)),
}
_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def key_from_profile(h):
    best = None
    for t in range(12):
        for nm, tp in (("major", _MAJ), ("minor", _MIN)):
            r = np.corrcoef(h, np.roll(tp, t))[0, 1]
            if best is None or r > best[0]:
                best = (r, t, nm)
    return best[1], best[2]


def key_from_audio(path):
    """Key of a beat/instrumental from its chroma (bass weighted)."""
    import librosa
    y, sr = librosa.load(path, sr=22050, mono=True, duration=180)
    ch = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=1024)
    bass = librosa.feature.chroma_cqt(y=y, sr=sr, hop_length=1024, fmin=librosa.note_to_hz("A0"), n_octaves=3)
    h = ch.sum(1) + 0.5 * bass.sum(1)
    return key_from_profile(h)


def pitch_track(y, sr):
    import parselmouth
    snd = parselmouth.Sound(np.asarray(y, dtype=np.float64), sampling_frequency=sr)
    pitch = snd.to_pitch(time_step=0.01, pitch_floor=70, pitch_ceiling=800)
    return snd, pitch.xs(), pitch.selected_array["frequency"]


def autotune(y, sr, key="auto", scale="minor", speed_ms=20.0, amount=1.0, humanize=0.0):
    """Returns (tuned, key_index, scale_name)."""
    from parselmouth.praat import call
    snd, t, f0 = pitch_track(y, sr)
    voiced = f0 > 0
    if voiced.sum() < 10 or amount <= 0:
        return np.asarray(y, dtype=np.float64), None, scale
    midi = 69 + 12 * np.log2(np.where(voiced, f0, 1.0) / 440.0)
    if key == "auto" or key is None:
        h = np.bincount(np.round(midi[voiced]).astype(int) % 12, minlength=12).astype(float)
        tonic, sc = key_from_profile(h)
        if scale in (None, "auto"):
            scale = sc
    else:
        tonic = NOTES.index(key) if isinstance(key, str) else int(key)
    allowed = np.array(sorted({(tonic + i) % 12 for i in SCALES.get(scale, SCALES["minor"])}))

    def snap(m):
        base = np.floor(m / 12) * 12
        c = np.concatenate([base - 12 + allowed, base + allowed, base + 12 + allowed])
        return c[np.argmin(np.abs(c - m))]
    corr = np.where(voiced, np.array([snap(m) for m in midi]) - midi, 0.0)
    a = np.exp(-0.01 / max(speed_ms / 1000.0, 1e-4))
    sm = np.zeros_like(corr)
    for i in range(len(corr)):
        prev = sm[i - 1] if i else 0.0
        sm[i] = a * prev + (1 - a) * corr[i] if voiced[i] else prev * a
    new = midi + amount * sm
    if humanize > 0:
        # keep part of the natural wobble on held notes
        new = new + humanize * (midi - np.convolve(midi, np.ones(15) / 15, "same")) * voiced
    manip = call(snd, "To Manipulation", 0.01, 70, 800)
    pt = call(manip, "Extract pitch tier")
    call(pt, "Remove points between", 0, snd.duration)
    for ti, v, m in zip(t, voiced, new):
        if v:
            call(pt, "Add point", ti, 440 * 2 ** ((m - 69) / 12))
    call([pt, manip], "Replace pitch tier")
    o = call(manip, "Get resynthesis (overlap-add)").values[0]
    res = np.asarray(y, dtype=np.float64).copy()
    n = min(len(o), len(res))
    res[:n] = o[:n]
    return res, tonic, scale
