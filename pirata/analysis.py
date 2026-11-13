"""Sample analysis: notes -> cleaned notes, tempo grid, key and detailed chords."""
from __future__ import annotations

import os
import re

import numpy as np

from . import transcribe as T

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
SHARP_KEYS = {"G", "D", "A", "E", "B", "F#", "C#"}

# quality -> intervals; ordered roughly simple -> complex
QUALITIES = {
    "":      (0, 4, 7),
    "m":     (0, 3, 7),
    "5":     (0, 7),
    "dim":   (0, 3, 6),
    "aug":   (0, 4, 8),
    "sus2":  (0, 2, 7),
    "sus4":  (0, 5, 7),
    "6":     (0, 4, 7, 9),
    "m6":    (0, 3, 7, 9),
    "7":     (0, 4, 7, 10),
    "maj7":  (0, 4, 7, 11),
    "m7":    (0, 3, 7, 10),
    "mMaj7": (0, 3, 7, 11),
    "m7b5":  (0, 3, 6, 10),
    "dim7":  (0, 3, 6, 9),
    "7sus4": (0, 5, 7, 10),
    "add9":  (0, 2, 4, 7),
    "madd9": (0, 2, 3, 7),
    "6/9":   (0, 2, 4, 7, 9),
    "9":     (0, 2, 4, 7, 10),
    "maj9":  (0, 2, 4, 7, 11),
    "m9":    (0, 2, 3, 7, 10),
    "7b9":   (0, 1, 4, 7, 10),
    "7#9":   (0, 3, 4, 7, 10),
    "11":    (0, 2, 5, 7, 10),
    "m11":   (0, 2, 3, 5, 7, 10),
    "maj7#11": (0, 4, 6, 7, 11),
    "13":    (0, 2, 4, 7, 9, 10),
    "m13":   (0, 2, 3, 7, 9, 10),
    "maj13": (0, 2, 4, 7, 9, 11),
}
# small preference for common qualities when evidence ties
PRIOR = {"": 0.10, "m": 0.10, "7": 0.05, "maj7": 0.05, "m7": 0.05, "sus2": 0.02, "sus4": 0.02,
         "5": -0.15, "aug": -0.08, "dim": -0.04, "dim7": -0.06, "7b9": -0.06, "7#9": -0.06,
         "mMaj7": -0.06, "maj7#11": -0.06}


def pc_name(pc: int, sharp: bool) -> str:
    n = NOTE_NAMES[pc % 12]
    if sharp:
        n = {"Eb": "D#", "Ab": "G#", "Bb": "A#"}.get(n, n)
    else:
        n = {"C#": "Db"}.get(n, n)
    return n


# ---------------------------------------------------------------- tempo grid
def bpm_from_name(path: str):
    m = re.search(r"(\d{2,3}(?:\.\d)?)\s*-?\s*bpm", os.path.basename(path), re.I)
    if m:
        v = float(m.group(1))
        if 50 <= v <= 200:
            return v
    return None


def _coh(on, w, period):
    """Phase coherence of onsets on a grid with this period (0..1) and its angle."""
    if len(on) == 0:
        return 0.0, 0.0
    z = np.sum(w * np.exp(2j * np.pi * on / period))
    return float(abs(z) / (w.sum() + 1e-9)), float(np.angle(z))


def _fine(on, w, bpm):
    """How well onsets sit on a straight (8th + 16th) or triplet (8th-T) grid."""
    beat = 60 / bpm
    s8, _ = _coh(on, w, beat / 2)
    s16, _ = _coh(on, w, beat / 4)
    st = (s8 + 0.5 * s16) / 1.5
    t3, _ = _coh(on, w, beat / 3)
    t6, _ = _coh(on, w, beat / 6)
    tr = (t3 + 0.5 * t6) / 1.5
    return st, tr


def _bass_onsets(notes):
    """Low notes that start something (bass / left hand) with their weights."""
    lo = [n for n in notes if n["pitch"] < 55]
    if len(lo) < 4:
        lo = sorted(notes, key=lambda n: n["pitch"])[: max(4, len(notes) // 4)]
    return (np.array([n["start"] for n in lo]),
            np.array([n["amp"] * (1.0 + max(0.0, n["end"] - n["start"])) for n in lo]))


def metric_check(notes, bpm):
    """TITO lesson: a triplet / swung sample read at 3:2 or 4:3 the true tempo looks
    just as tight on the fine grid. At the true tempo the bass and chord changes land
    on whole 4-beat bars; at the 3:2-off tempo they fall every 1.5 / 0.75 bars."""
    on = np.array([n["start"] for n in notes])
    w = np.array([n["amp"] for n in notes])
    bo, bw = _bass_onsets(notes)
    if len(bo) < 4:
        return bpm, "none"

    def score(b):
        st, tr = _fine(on, w, b)
        bar, _ = _coh(bo, bw, 4 * 60 / b)
        half, _ = _coh(bo, bw, 2 * 60 / b)
        return max(st, tr), 0.6 * bar + 0.4 * half

    f0, m0 = score(bpm)
    best, why = bpm, "none"
    for k in (2 / 3, 3 / 4, 4 / 3, 3 / 2):
        b = bpm * k
        if not 55 <= b <= 180:
            continue
        f, m = score(b)
        if m > m0 + 0.25 and f > f0 - 0.12:
            best, why, m0, f0 = b, f"{k:.3g}", m, f
    return best, why


def harmonic_rhythm_bpm(on, w, bpm, lo=70.0, hi=150.0):
    """Sparse samples (a pad or held chords changing once a bar) have no rhythm for the
    grid fit to lock onto - any tempo fits a few whole notes, and a pad's notes fade in
    one after another. When attacks are sparse, the tempo whose 4/4 bars best line up
    with the chord attacks wins (if clearly better than the current reading)."""
    if len(on) < 4:
        return None
    t = np.sort(on)
    groups = 1 + int(np.sum(np.diff(t) > 0.08))
    span = max(1e-3, t[-1] - t[0])
    if groups / span > 1.6 or span < 4.0:
        return None

    def bar_coh(b):
        c, _ = _coh(on, w, 240.0 / b)
        return c

    cands = np.arange(lo, hi + 0.01, 0.1)
    cs = np.array([bar_coh(b) for b in cands])
    # an octave apart both fit (a change every 2 bars at half the tempo): prefer ~95 BPM
    score = cs - 0.04 * np.abs(np.log2(cands / 95.0))
    best = float(cands[int(np.argmax(score))])
    if cs.max() < 0.6 or bar_coh(bpm) > cs.max() - 0.2:
        return None
    return best


def refine_period(on, w, bpm, start, div=4):
    """Exact BPM from the notes: residuals against the 16th grid, fitted as a line over
    time grown from the start of the sample outwards (a slope = the period is off).
    Returns bpm, phase anchor and the leftover spread (ms) after the fit."""
    if len(on) < 8:
        return bpm, start, 0.0
    order = np.argsort(on)
    on, w = on[order], w[order]
    per = 60 / bpm / div
    t0 = start
    span = on[-1] - on[0]
    for frac in (0.3, 0.6, 1.0, 1.0):
        sel = on <= on[0] + frac * span + 1e-9
        x, ww = on[sel], w[sel]
        r = x - t0 - np.round((x - t0) / per) * per
        good = np.abs(r) < per * 0.3
        if good.sum() < 6:
            break
        X = np.stack([np.ones(good.sum()), x[good] - t0], 1)
        W = ww[good]
        A = X.T @ (X * W[:, None])
        a, b = np.linalg.solve(A + 1e-9 * np.eye(2), X.T @ (W * r[good]))
        if abs(b) > 0.01:
            break
        t0 = t0 + a
        per = per * (1 + b)
    r = on - t0 - np.round((on - t0) / per) * per
    good = np.abs(r) < per * 0.3
    spread = 1000 * float(np.median(np.abs(r[good] - np.median(r[good])))) if good.any() else 0.0
    return 60 / per / div, t0, spread


def fit_grid(notes, duration, y22, bpm_hint=None):
    """Find tempo + phase that best puts note onsets on an 8th/16th (or triplet) grid."""
    import librosa
    on = np.array([n["start"] for n in notes]) if notes else np.zeros(0)
    w = np.array([n["amp"] * (1.5 if n["pitch"] < 52 else 1.0) for n in notes]) if notes else np.zeros(0)
    info = dict(metric="none", spread_ms=0.0, triplet=False)

    if bpm_hint:
        cands = [bpm_hint]
    else:
        oe = librosa.onset.onset_strength(y=y22, sr=22050)
        tg = librosa.feature.tempo(onset_envelope=oe, sr=22050, aggregate=None, start_bpm=95)
        t0 = float(np.median(tg))
        cands = sorted({round(t0 * k, 1) for k in (0.5, 2 / 3, 0.75, 1, 4 / 3, 1.5, 2)
                        if 58 <= t0 * k <= 180})

    def score(bpm):
        if len(on) == 0:
            return 0.0, 0.0
        per = 60 / bpm / 2  # 8th note
        ph = 2 * np.pi * on / per
        z = np.sum(w * np.exp(1j * ph))
        # 16th consistency too
        z16 = np.sum(w * np.exp(2j * ph))
        return float(abs(z) + 0.5 * abs(z16)) / (w.sum() + 1e-9), float(np.angle(z))

    best = None
    for c in cands:
        span = [c] if bpm_hint else np.arange(c * 0.97, c * 1.03, 0.05)
        for b in span:
            s, _ = score(b)
            # mild prior toward the 70-150 range samples usually live in
            pri = 1.0 if 70 <= b <= 150 else 0.9
            if best is None or s * pri > best[0]:
                best = (s * pri, b)
    bpm = best[1]
    if not bpm_hint and len(on):
        bpm, info["metric"] = metric_check(notes, bpm)
        hr = harmonic_rhythm_bpm(on, w, bpm)
        if hr:
            bpm, info["metric"] = hr, "harmonic rhythm"
    # straight or triplet subdivision decides which fine grid locks the phase
    st, tr = _fine(on, w, bpm)
    sub = 3 if tr > st + 0.1 else 2
    info["triplet"] = sub == 3
    beat = 60 / bpm
    perf = beat / sub
    _, ang = _coh(on, w, perf)
    first = ((ang / (2 * np.pi)) * perf) % perf
    if not bpm_hint and len(on):
        # exact BPM: fit the drift of the notes against the grid (CSIRAC lesson)
        bpm, first, info["spread_ms"] = refine_period(on, w, bpm, first, 4 if sub == 2 else 3)
        if abs(bpm - round(bpm)) < 0.35:
            bpm = float(round(bpm))
        beat = 60 / bpm
        perf = beat / sub
        _, ang = _coh(on, w, perf)
        first = ((ang / (2 * np.pi)) * perf) % perf
    # choose beat phase (which fine line is "on the beat") by onset weight
    sc = []
    # bass notes sit on beats far more often than upper comping does
    wb = np.array([n["amp"] * (4.0 if n["pitch"] < 52 else 1.0) for n in notes]) if notes else np.zeros(0)
    on0 = float(on.min()) if len(on) else 0.0
    for k in range(sub):
        t = first + k * perf
        # signed: onsets ON the beat score +1, onsets off it score less
        # (abs() here made the two phases tie and could lock the grid half a beat off)
        v = float(np.sum(wb * np.cos(2 * np.pi * (on - t) / beat))) if len(on) else 0.0
        # samples are nearly always cut to start on a beat
        d = (on0 - t) % beat
        if min(d, beat - d) < perf / 2:
            v += 0.25 * float(wb.sum())
        sc.append(v)
    start = (first + int(np.argmax(sc)) * perf) % beat
    # TITO lesson: the fine-grid phase is dragged by swung / syncopated notes. Re-anchor
    # it on the notes that sit near the beat and 8th lines (beat lines for triplets).
    if len(on) >= 4:
        lines = 2 if sub == 2 else 1
        for _ in range(2):
            x = (on - start) / beat
            r = x - np.round(x * lines) / lines
            sel = np.abs(r) < 0.1
            if sel.sum() < 4:
                break
            start += float(np.median(r[sel])) * beat
    if start > beat * 0.9:
        start -= beat
    n = int((duration - start) / beat) + 2
    grid = start + beat * np.arange(n)
    return float(bpm), float(start), grid, info


# ---------------------------------------------------------------- onset refinement
def refine_onsets(notes, y, sr):
    """TITO lesson: the transcriber's note starts are frame-based (11.6 ms frames;
    on piano they scatter +-5-13 ms around the real attack). Each note is moved to
    where its attack really starts: the energy above 2 kHz (hammer / pluck / stab
    transient, free of bass and pad tails) is followed back from its steepest rise
    to 25 % of the way up from the floor before it. Slow attacks (pads, swells)
    show no clear transient and keep their time. Measured: +-0.7 ms median."""
    from scipy.signal import butter, sosfiltfilt
    if not notes:
        return notes
    hp = sosfiltfilt(butter(4, 2000 / (sr / 2), btype="high", output="sos"), y)
    L = max(3, int(0.002 * sr))
    env = np.sqrt(np.convolve(hp ** 2, np.ones(L) / L, mode="same"))
    pre, post, rise = int(0.034 * sr), int(0.020 * sr), int(0.010 * sr)
    out = []
    for n in notes:
        m = dict(n)
        c = int(n["start"] * sr)
        a, b = max(0, c - pre), min(len(y), c + post)
        seg = env[a:b]
        if len(seg) > 4 * L:
            k = int(np.argmax(np.diff(seg)))
            pk = k + int(np.argmax(seg[k:k + rise]))
            base = float(np.min(seg[:k])) if k > 0 else float(seg[0])
            if seg[pk] > 1.5 * base + 1e-7:
                thr = base + 0.25 * (seg[pk] - base)
                j = k
                while j > 0 and seg[j] > thr:
                    j -= 1
                m["start"] = (a + j) / sr
                m["end"] = max(n["end"], m["start"] + 0.03)
        out.append(m)
    out.sort(key=lambda n: (n["start"], n["pitch"]))
    return out


# ---------------------------------------------------------------- cleanup
def clean_notes(notes, act, min_amp=0.3):
    """Remove ghost octaves/12ths below or above stronger notes, stray blips."""
    out = []
    for n in notes:
        if n["amp"] < min_amp * 0.85 and n["end"] - n["start"] < 0.15:
            continue
        out.append(dict(n))
    keep = []
    for n in out:
        ghost = False
        for m in out:
            if m is n:
                continue
            ov = min(n["end"], m["end"]) - max(n["start"], m["start"])
            if ov <= 0.6 * (n["end"] - n["start"]):
                continue
            d = n["pitch"] - m["pitch"]
            # sub-harmonic ghosts (rumble an octave / 12th / 2 octaves under a louder note)
            if d in (-12, -19, -24) and n["pitch"] < 50 and n["amp"] < m["amp"] * 0.8:
                ghost = True
            # short, weak octave-above blips riding on a louder note
            if d == 12 and n["amp"] < m["amp"] * 0.5 and n["end"] - n["start"] < 0.25 \
                    and n["start"] >= m["start"] - 0.03:
                ghost = True
            if ghost:
                break
        if not ghost:
            keep.append(n)
    keep.sort(key=lambda n: (n["start"], n["pitch"]))
    return keep


def merge_restrikes(notes, gap=0.06):
    """Join same-pitch fragments separated by a tiny gap (decoder splits)."""
    by = {}
    for n in sorted(notes, key=lambda n: n["start"]):
        L = by.setdefault(n["pitch"], [])
        if L and n["start"] - L[-1]["end"] < gap and n["amp"] < L[-1]["amp"] * 1.05:
            L[-1]["end"] = max(L[-1]["end"], n["end"])
        else:
            L.append(dict(n))
    out = [n for L in by.values() for n in L]
    out.sort(key=lambda n: (n["start"], n["pitch"]))
    return out


def quantize(notes, grid, div=4, triplets=True):
    """Snap to the beat grid. Each beat is straight 1/16 unless its notes clearly sit
    on 8th-note triplets (or 16th triplets), then that beat uses the triplet grid.
    Returns notes in beats; every note also keeps its real (unsnapped) position
    `ts` / `te` in beats so exports can keep the sample's timing."""
    beat = grid[1] - grid[0]
    t0 = grid[0]
    xs = [((n["start"] - t0) / beat, (n["end"] - t0) / beat) for n in notes]
    subs = {}
    if triplets:
        per_beat = {}
        for (s, _), n in zip(xs, notes):
            per_beat.setdefault(int(np.floor(s + 1 / 12)), []).append((s, n["amp"]))
        for b, L in per_beat.items():
            f = np.array([s for s, _ in L])
            w = np.array([a for _, a in L])
            if len(f) < 2:
                continue

            def err(d):
                return float(np.sum(w * np.abs(f - np.round(f * d) / d)) / w.sum())
            e4, e3, e6 = err(4), err(3), err(6)
            # a triplet beat needs a note that is clearly off the 1/16 grid
            off = np.max(np.abs(f - np.round(f * 4) / 4))
            if off > 0.06 and min(e3, e6 * 1.4) < 0.45 * e4:
                subs[b] = 3 if e3 <= e6 * 1.4 else 6
    out = []
    for (xs_, xe), n in zip(xs, notes):
        d = subs.get(int(np.floor(xs_ + 1 / 12)), div)
        s = round(xs_ * d) / d
        de = subs.get(int(np.floor(xe + 1 / 12)), div)
        e = round(xe * de) / de
        if e <= s:
            e = s + 1 / d
        out.append(dict(start=round(s, 6), end=round(e, 6), pitch=n["pitch"],
                        vel=int(np.clip(30 + n["amp"] * 110, 35, 120)),
                        ts=round(xs_, 5), te=round(xe, 5)))
    # remove duplicates and overlaps of identical pitch
    out.sort(key=lambda n: (n["pitch"], n["start"]))
    res = []
    for n in out:
        if res and res[-1]["pitch"] == n["pitch"] and n["start"] < res[-1]["end"]:
            if n["start"] == res[-1]["start"]:
                res[-1]["end"] = max(res[-1]["end"], n["end"])
                continue
            res[-1]["end"] = n["start"]
        res.append(n)
    res = [n for n in res if n["end"] > n["start"]]
    res.sort(key=lambda n: (n["start"], n["pitch"]))
    return res


# ---------------------------------------------------------------- key
_MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def detect_key(qnotes):
    prof = np.zeros(12)
    bass = np.zeros(12)
    for n in qnotes:
        d = n["end"] - n["start"]
        prof[n["pitch"] % 12] += d
        if n["pitch"] < 52:
            bass[n["pitch"] % 12] += d
    prof = prof + 0.5 * bass
    best = None
    for t in range(12):
        for mode, tpl in (("major", _MAJ), ("minor", _MIN)):
            r = np.corrcoef(prof, np.roll(tpl, t))[0, 1]
            if best is None or r > best[0]:
                best = (r, t, mode)
    return best[1], best[2], float(best[0])


def key_scale(tonic, mode):
    iv = (0, 2, 4, 5, 7, 9, 11) if mode == "major" else (0, 2, 3, 5, 7, 8, 10)
    return {(tonic + i) % 12 for i in iv}


# ---------------------------------------------------------------- chords
def beat_salience(act, grid_start, beat, n_beats):
    """Mean network pitch salience (88 piano keys) per beat."""
    N = act["note"]
    t = T.frames_to_time(N.shape[0])
    k = np.floor((t - grid_start) / beat).astype(int)
    S = np.zeros((n_beats, 88))
    cnt = np.zeros(n_beats)
    ok = (k >= 0) & (k < n_beats)
    np.add.at(S, k[ok], N[ok])
    np.add.at(cnt, k[ok], 1)
    S /= np.maximum(cnt, 1)[:, None]
    return np.clip(S - 0.12, 0, None)


def salience_profiles(S):
    P = np.zeros((S.shape[0], 12))
    B = np.zeros((S.shape[0], 12))
    for p in range(88):
        P[:, (p + 21) % 12] += S[:, p]
    for k in range(S.shape[0]):
        low = np.nonzero(S[k, :34] > 0.12)[0]      # below G3
        if len(low):
            b = low[0]
            B[k, (b + 21) % 12] += S[k, b]
    return P, B


def beat_profiles(qnotes, n_beats, sub=1):
    """Per (sub)beat pitch-class weights, bass weights and lowest pitch."""
    nb = n_beats * sub
    P = np.zeros((nb, 12))
    B = np.zeros((nb, 12))
    for n in qnotes:
        s, e = n["start"] * sub, n["end"] * sub
        a, b = int(np.floor(s)), int(np.ceil(e))
        for k in range(max(0, a), min(nb, b)):
            ov = min(e, k + 1) - max(s, k)
            if ov <= 0:
                continue
            # attack inside this cell counts more than a held-over note
            w = ov * (1.0 if s >= k - 1e-6 else 0.6) * (n["vel"] / 100)
            P[k, n["pitch"] % 12] += w
    # bass: lowest sounding pitch per cell
    for k in range(nb):
        low = [n for n in qnotes if n["start"] * sub < k + 1 and n["end"] * sub > k]
        if low:
            m = min(n["pitch"] for n in low)
            for n in low:
                if n["pitch"] <= m + 2 and n["pitch"] < 60:
                    B[k, n["pitch"] % 12] += 1
            if not B[k].any():
                B[k, m % 12] += 0.5
    return P, B


def chord_score(prof, bass, root, q, scale):
    iv = QUALITIES[q]
    pcs = [(root + i) % 12 for i in iv]
    tot = prof.sum() + 1e-9
    p = prof / tot
    inside = sum(p[c] for c in pcs)
    outside = 1 - inside
    # every chord tone should actually be present (penalise missing tones)
    present = np.array([p[c] for c in pcs])
    missing = sum(max(0.0, 0.06 - x) for x in present) * 4
    # root must be meaningfully present unless it is in the bass
    btot = bass.sum()
    bass_root = bass[root] / btot if btot else 0
    s = inside - 1.6 * outside - missing + 0.25 * bass_root + PRIOR.get(q, 0)
    s -= 0.025 * max(0, len(iv) - 3)
    # out-of-key chord tones are allowed but need evidence
    s -= 0.04 * sum(1 for c in pcs if c not in scale)
    return s


def label(root, q, bass_pc, sharp):
    name = pc_name(root, sharp) + q
    if bass_pc is not None and bass_pc != root:
        name += "/" + pc_name(bass_pc, sharp)
    return name


def best_chords(prof, bass, scale, top=3):
    res = []
    for r in range(12):
        for q in QUALITIES:
            res.append((chord_score(prof, bass, r, q, scale), r, q))
    res.sort(reverse=True)
    return res[:top]


def chord_track(qnotes, n_beats, scale, sal=None, beats_per_bar=4, change_cost=0.22, phase=0):
    """Viterbi over beats with candidate chords; changes favour bar/half-bar lines."""
    P, B = beat_profiles(qnotes, n_beats)
    if sal is not None:
        SP, SB = salience_profiles(sal[:n_beats])
        # notes say what was struck, salience says what is ringing: blend both
        pn = P / np.maximum(P.sum(1, keepdims=True), 1e-9)
        ps = SP / np.maximum(SP.sum(1, keepdims=True), 1e-9)
        P = np.where(P.sum(1, keepdims=True) > 0, 0.55 * pn + 0.45 * ps, ps)
        bn = B / np.maximum(B.sum(1, keepdims=True), 1e-9)
        bs = SB / np.maximum(SB.sum(1, keepdims=True), 1e-9)
        B = 0.5 * bn + 0.5 * bs
    cands = []
    for r in range(12):
        for q in QUALITIES:
            cands.append((r, q))
    C = len(cands)
    S = np.full((n_beats, C), -2.0)
    for k in range(n_beats):
        if P[k].sum() < 1e-6:
            S[k] = 0
            continue
        for j, (r, q) in enumerate(cands):
            S[k, j] = chord_score(P[k], B[k], r, q, scale)
    # Viterbi
    D = S[0].copy()
    back = np.zeros((n_beats, C), int)
    for k in range(1, n_beats):
        pos = (k - phase) % beats_per_bar
        cost = change_cost * (0.35 if pos == 0 else 0.8 if pos == beats_per_bar // 2 else 1.8)
        best_prev = D.argmax()
        stay = D
        move = D[best_prev] - cost
        back[k] = np.where(stay >= move, np.arange(C), best_prev)
        D = np.maximum(stay, move) + S[k]
    path = np.zeros(n_beats, int)
    path[-1] = D.argmax()
    for k in range(n_beats - 1, 0, -1):
        path[k - 1] = back[k, path[k]]
    # segments
    segs = []
    for k in range(n_beats):
        if P[k].sum() < 1e-6 and (not segs or segs[-1]["silent"]):
            if segs and segs[-1]["silent"]:
                segs[-1]["end"] = k + 1
            else:
                segs.append(dict(start=k, end=k + 1, idx=-1, silent=True))
            continue
        if segs and not segs[-1]["silent"] and segs[-1]["idx"] == path[k]:
            segs[-1]["end"] = k + 1
        else:
            segs.append(dict(start=k, end=k + 1, idx=int(path[k]), silent=P[k].sum() < 1e-6))
    return segs, cands, P, B


def tidy_segments(segs, P, B, phase=0, bpb=4, onsets=None):
    """Fold one-beat flickers that aren't on a bar line into the chord before."""
    out = []
    for sg in segs:
        short = sg["end"] - sg["start"] <= 1 and (sg["start"] - phase) % bpb != 0
        if out and short and not out[-1]["silent"] and not sg["silent"]:
            out[-1]["end"] = sg["end"]
            continue
        out.append(dict(sg))
    # trailing ring-out after the last real chord: nothing new is struck there
    while len(out) > 1 and out[-1]["end"] - out[-1]["start"] <= 2 and onsets is not None \
            and not any(out[-1]["start"] <= o < out[-1]["end"] for o in onsets):
        out[-2]["end"] = out[-1]["end"]
        out.pop()
    return out


def refine_segments(segs, cands, P, B, scale, sharp):
    """Re-label each segment from all of its evidence; add slash bass + alternatives."""
    out = []
    for s in segs:
        if s["silent"]:
            out.append(dict(start=s["start"], end=s["end"], name="N.C.", root=None, quality=None,
                            bass=None, pcs=[], alts=[], conf=0.0))
            continue
        prof = P[s["start"]:s["end"]].sum(0)
        bass = B[s["start"]:s["end"]].sum(0)
        top = best_chords(prof, bass, scale, top=4)
        sc, r, q = top[0]
        bass_pc = int(bass.argmax()) if bass.sum() else None
        if bass_pc is not None and bass[bass_pc] / bass.sum() < 0.45:
            bass_pc = r
        if bass_pc is not None and bass_pc not in [(r + i) % 12 for i in QUALITIES[q]] \
                and bass[bass_pc] / bass.sum() < 0.6:
            bass_pc = r
        margin = sc - top[1][0]
        alts = [label(rr, qq, None, sharp) for _, rr, qq in top[1:3]]
        out.append(dict(start=s["start"], end=s["end"], name=label(r, q, bass_pc, sharp),
                        root=r, quality=q, bass=bass_pc,
                        pcs=sorted({(r + i) % 12 for i in QUALITIES[q]}),
                        alts=alts, conf=float(np.clip(0.5 + margin * 3, 0, 1))))
    # merge neighbours that ended up with the same label
    merged = []
    for c in out:
        if merged and merged[-1]["name"] == c["name"]:
            merged[-1]["end"] = c["end"]
        else:
            merged.append(c)
    return merged


def find_downbeat(segs, qnotes, bpb=4):
    """Bar phase where chord changes and bass attacks line up best."""
    sc = np.zeros(bpb)
    for sg in segs[1:]:
        sc[sg["start"] % bpb] += 2
    for n in qnotes:
        if n["pitch"] < 55 and abs(n["start"] - round(n["start"])) < 1e-6:
            sc[int(round(n["start"])) % bpb] += 0.5 * (n["end"] - n["start"])
    return int(np.argmax(sc))


def chord_key(chords, tonic, mode, kconf, qnotes):
    """Re-weigh the key using harmony: tonic chord first/last, V7 -> i pulls."""
    prof = np.zeros(12)
    for n in qnotes:
        prof[n["pitch"] % 12] += n["end"] - n["start"]
    real = [c for c in chords if c["root"] is not None]
    if not real:
        return tonic, mode
    best = None
    for t in range(12):
        for md, tpl in (("major", _MAJ), ("minor", _MIN)):
            r = np.corrcoef(prof, np.roll(tpl, t))[0, 1]
            third = 4 if md == "major" else 3
            tonic_q = lambda c: c["root"] == t and (third + t) % 12 in c["pcs"]
            dur = sum(c["end"] - c["start"] for c in real)
            r += 0.20 * tonic_q(real[0]) + 0.10 * tonic_q(real[-1])
            r += 0.15 * sum(c["end"] - c["start"] for c in real if tonic_q(c)) / dur
            # dominant (major-3rd chord on the 5th) resolving to the tonic
            for a, b in zip(real, real[1:] + real[:1]):
                if a["root"] == (t + 7) % 12 and (t + 11) % 12 in a["pcs"] and b["root"] == t:
                    r += 0.12
                    break
            if best is None or r > best[0]:
                best = (r, t, md)
    return best[1], best[2]


def is_sharp(tonic, mode):
    major_tonic = tonic if mode == "major" else (tonic + 3) % 12
    return major_tonic in (7, 2, 9, 4, 11, 6, 1)


def name_chords(chords, sharp):
    for c in chords:
        if c["root"] is not None:
            c["name"] = label(c["root"], c["quality"], c["bass"], sharp)
            c["alts"] = [a for a in c["alts"]]
    return chords


# ---------------------------------------------------------------- top level
def analyze(path, bpm=None, sensitivity=0.5, progress=None):
    import librosa
    say = progress or (lambda *a: None)
    say(0.05, "Reading audio")
    y, _ = librosa.load(path, sr=T.SR, mono=True)
    dur = len(y) / T.SR
    say(0.15, "Transcribing notes")
    onset_t = 0.6 - 0.25 * sensitivity          # 0.6 strict .. 0.35 sensitive
    frame_t = 0.35 - 0.12 * sensitivity
    raw, act = T.transcribe(y, onset_thresh=onset_t, frame_thresh=frame_t)
    say(0.6, "Finding tempo and chords")
    notes = merge_restrikes(clean_notes(raw, act))
    notes = refine_onsets(notes, y, T.SR)
    hint = bpm or bpm_from_name(path)
    bpm, start, grid, tinfo = fit_grid(notes, dur, y, hint)
    beat = 60 / bpm
    q = quantize(notes, grid)
    tonic, mode, kconf = detect_key(q)
    scale = key_scale(tonic, mode)
    n_beats = int(np.ceil(max([n["end"] for n in q], default=1)))
    sal = beat_salience(act, start, beat, n_beats)

    # pass 1 -> bar phase, pass 2 -> chords with bar-aware change costs
    segs, cands, P, B = chord_track(q, n_beats, scale, sal)
    phase = find_downbeat(segs, q)
    segs, cands, P, B = chord_track(q, n_beats, scale, sal, phase=phase)
    segs = tidy_segments(segs, P, B, phase, onsets=[n["start"] for n in q])
    chords = refine_segments(segs, cands, P, B, scale, False)
    tonic, mode = chord_key(chords, tonic, mode, kconf, q)
    if key_scale(tonic, mode) != scale:
        scale = key_scale(tonic, mode)
        chords = refine_segments(segs, cands, P, B, scale, False)
    sharp = is_sharp(tonic, mode)
    chords = refine_segments(segs, cands, P, B, scale, sharp)

    # move the origin so beat 0 is a bar line at or before the first note
    if phase:
        shift = phase - 4
        start += shift * beat
        for n in q:
            n["start"] -= shift
            n["end"] -= shift
            n["ts"] = round(n["ts"] - shift, 5)
            n["te"] = round(n["te"] - shift, 5)
        for c in chords:
            c["start"] -= shift
            c["end"] -= shift
    say(0.95, "Done")
    return dict(
        file=os.path.abspath(path), duration=dur, bpm=bpm, grid_start=start,
        key=pc_name(tonic, sharp) + (" minor" if mode == "minor" else " major"),
        tonic=tonic, mode=mode, sharp=bool(sharp),
        notes=q, chords=chords, raw_count=len(raw),
        timing=timing_info(q, tinfo, bpm),
    )


def timing_info(q, tinfo, bpm):
    """How the sample sits on the grid: steady, looseness, 16th swing, triplet beats.
    (Push / lay-back can't be measured on a lone sample: there is no click to be
    late against, the grid is anchored on the sample's own beats.)"""
    if not q:
        return dict(steady=True)
    ms = 60000 / bpm
    dev = np.array([n["ts"] - n["start"] for n in q]) * ms           # ms off the grid
    pos = np.array([round((n["start"] % 1) * 12) for n in q])         # 12ths of a beat
    on8 = np.isin(pos, (0, 6))
    feel = float(np.median(dev[on8])) if on8.any() else float(np.median(dev))
    off16 = np.isin(pos, (3, 9))
    swing = 50.0
    if off16.sum() >= max(4, 0.15 * len(q)):
        late = float(np.median(dev[off16])) - feel                   # ms later than straight
        swing = 50.0 + 100.0 * late / (ms / 2)                       # 50 = straight, 66.7 = triplet
    trip = float(np.mean(pos % 3 != 0))
    return dict(
        steady=bool(tinfo.get("spread_ms", 0) < 25),
        spread_ms=round(float(tinfo.get("spread_ms", 0)), 1),
        loose_ms=round(float(np.median(np.abs(dev[on8] - feel))) if on8.any() else 0.0, 1),
        swing_16=round(float(np.clip(swing, 40, 75)), 1),
        triplet_share=round(trip, 2),
        metric_fix=tinfo.get("metric", "none"),
    )
