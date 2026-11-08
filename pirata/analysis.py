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


def fit_grid(notes, duration, y22, bpm_hint=None):
    """Find tempo + phase that best puts note onsets on an 8th/16th grid."""
    import librosa
    on = np.array([n["start"] for n in notes]) if notes else np.zeros(0)
    w = np.array([n["amp"] * (1.5 if n["pitch"] < 52 else 1.0) for n in notes]) if notes else np.zeros(0)

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
    if not bpm_hint and abs(bpm - round(bpm)) < 0.35:
        bpm = float(round(bpm))
    _, ang = score(bpm)
    per8 = 60 / bpm / 2
    offset = (ang / (2 * np.pi)) * per8          # time of an 8th-grid line
    beat = 60 / bpm
    # choose beat phase (which 8th line is "on the beat") by onset weight
    first = offset % per8
    sc = []
    for k in (0, 1):
        t = first + k * per8
        ph = np.angle(np.sum(w * np.exp(2j * np.pi * (on - t) / beat))) if len(on) else 0
        sc.append(abs(np.sum(w * np.cos(2 * np.pi * (on - t) / beat))))
    start = (first + int(np.argmax(sc)) * per8) % beat
    if start > beat * 0.9:
        start -= beat
    n = int((duration - start) / beat) + 2
    grid = start + beat * np.arange(n)
    return float(bpm), float(start), grid


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


def quantize(notes, grid, div=4):
    """Snap to a 1/16 grid laid over the beat grid. Returns notes in beats."""
    beat = grid[1] - grid[0]
    t0 = grid[0]
    out = []
    for n in notes:
        s = round((n["start"] - t0) / beat * div) / div
        e = round((n["end"] - t0) / beat * div) / div
        if e <= s:
            e = s + 1 / div
        out.append(dict(start=s, end=e, pitch=n["pitch"], vel=int(np.clip(30 + n["amp"] * 110, 35, 120))))
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
    hint = bpm or bpm_from_name(path)
    bpm, start, grid = fit_grid(notes, dur, y, hint)
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
        for c in chords:
            c["start"] -= shift
            c["end"] -= shift
    say(0.95, "Done")
    return dict(
        file=os.path.abspath(path), duration=dur, bpm=bpm, grid_start=start,
        key=pc_name(tonic, sharp) + (" minor" if mode == "minor" else " major"),
        tonic=tonic, mode=mode, sharp=bool(sharp),
        notes=q, chords=chords, raw_count=len(raw),
    )
