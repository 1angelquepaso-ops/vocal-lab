"""Drum stem -> hits per sound, pads, beat grid, sections.

How it works
1. Onsets: band-balanced spectral flux (so quiet hats count as much as loud
   kicks), refined to ~1 ms on the waveform.
2. Each onset gets an "attack spectrum" = what the hit ADDED (post minus pre).
3. Hits are grouped by attack shape (same drum sample -> same shape). Groups
   that are just two other groups sounding together (kick + hat) are spotted
   with NNLS and dropped as templates.
4. Every hit is decomposed over the remaining templates (NNLS), so a kick and
   a hat struck together come out as two notes with their own velocities.
5. Each template is labelled (kick, 808, snare, clap, rim, hats, toms, crash,
   ride, perc) from its sound: low-end share, brightness, decay, pitch,
   clap flams. Labels pick the pad; the cleanest single hit becomes the chop.
6. Beat grid: tracked beats, straightened to one BPM when the song is steady,
   kept as a beat map when it drifts. Bar 1 = where kick/snare/crash say so.
7. Sections from how the bar patterns change; fills already in the song are
   marked.
"""
from __future__ import annotations

import os

import numpy as np
from scipy.optimize import nnls

from .common import PADS, PAD_INDEX, SR

# tuned on tito.bench (see CATALOGUE/15_TITO_PUENTE.md)
KNOBS = dict(prune=0.10, group_thr=0.09, dup_sim=0.90, explain=0.4, typ_min=0.1, onset_a=0.3, onset_c=0.03,
             onset_floor=0.6)

ASR = 22050          # analysis rate
HOP = 128            # 5.8 ms
NFFT = 1024
NMEL = 64


# ------------------------------------------------------------------ audio
def load(path):
    import soundfile as sf
    y, sr = sf.read(path, always_2d=True, dtype="float32")
    if sr != SR:
        import soxr
        y = soxr.resample(y, sr, SR).astype(np.float32)
    return y            # (n, ch) at 44.1 kHz


def _mel_power(mono22):
    """Mel power spectrogram in chunks (keeps memory low on long songs)."""
    import librosa
    fb = librosa.filters.mel(sr=ASR, n_fft=NFFT, n_mels=NMEL, fmin=30, fmax=11000)
    win = np.hanning(NFFT).astype(np.float32)
    pad = np.concatenate([np.zeros(NFFT // 2, np.float32), mono22, np.zeros(NFFT, np.float32)])
    n_frames = 1 + len(mono22) // HOP
    out = np.empty((NMEL, n_frames), np.float32)
    step = 4096
    for f0 in range(0, n_frames, step):
        f1 = min(n_frames, f0 + step)
        idx = (np.arange(f0, f1) * HOP)[:, None] + np.arange(NFFT)[None, :]
        frames = pad[idx] * win
        spec = np.abs(np.fft.rfft(frames, axis=1)) ** 2
        out[:, f0:f1] = (fb @ spec.T).astype(np.float32)
    return out


BANDS = [(0, 10), (10, 30), (30, 64)]     # mel-band index ranges: lows, mids, highs


def onset_curve(M):
    """Band-balanced superflux-style onset strength."""
    L = np.log1p(1e3 * M)
    from scipy.ndimage import maximum_filter1d
    ref = maximum_filter1d(L, size=3, axis=0)
    lag = 2
    d = np.zeros_like(L)
    d[:, lag:] = np.maximum(0.0, L[:, lag:] - ref[:, :-lag])
    curve = np.zeros(L.shape[1], np.float32)
    for a, b in BANDS:
        bf = d[a:b].sum(axis=0)
        s = np.percentile(bf[bf > 0], 99) if np.any(bf > 0) else 1.0
        curve += bf / (s + 1e-9)
    return curve


def pick_peaks(curve, min_gap_s=0.028, sens=0.5):
    from scipy.ndimage import maximum_filter1d, uniform_filter1d
    fps = ASR / HOP
    w = int(0.03 * fps) | 1
    is_max = curve == maximum_filter1d(curve, size=w)
    local = uniform_filter1d(curve, size=int(0.25 * fps) | 1)
    floor = np.percentile(curve, 50)
    # generous on purpose: junk onsets get tiny activations later and are dropped there
    thr = local * (1.0 + KNOBS["onset_a"] * (1 - sens)) + floor * (KNOBS["onset_floor"] * (1 - sens)) \
        + KNOBS["onset_c"]
    cand = np.where(is_max & (curve > thr))[0]
    out, last = [], -10 ** 9
    gap = int(min_gap_s * fps)
    for c in cand:
        if c - last >= gap:
            out.append(c)
            last = c
        elif curve[c] > curve[out[-1]]:
            out[-1] = c
            last = c
    return np.array(out, int)


def refine_times(frames, mono44):
    """Frame -> sample-accurate attack time: the start of the steepest rise, read on
    the full signal or with the lows filtered out (808 tails / room under hats),
    whichever shows the hit more clearly. Never walks back through the background."""
    from scipy.signal import butter, sosfiltfilt
    hp = sosfiltfilt(butter(2, 200 / (SR / 2), btype="high", output="sos"), mono44)
    sm = np.ones(44) / 44
    times = []
    for f in frames:
        c = int(f * HOP * SR / ASR)
        a, b = max(0, c - int(0.014 * SR)), min(len(mono44), c + int(0.010 * SR))
        best = None
        for sig in (mono44, hp):
            seg = np.abs(sig[a:b])
            if len(seg) < 100:
                continue
            env = np.convolve(seg, sm, mode="same")
            d = np.diff(env)
            k = int(np.argmax(d))
            base = np.median(env[: max(5, k)]) + 1e-9
            ratio = env[min(len(env) - 1, k + 44)] / base         # how clearly the hit rises
            j = k
            while j > 0 and d[j - 1] > 0.3 * d[k]:
                j -= 1
            if best is None or ratio > best[0]:
                best = (ratio, a + j)
        times.append((best[1] if best else c) / SR)
    return np.array(times)


def sustain(M, frames, A):
    """How much of each hit's OWN sound is still ringing 45-85 ms later
    (background already sounding is removed; NaN if the next hit is too close)."""
    fps = ASR / HOP
    w1, w2 = int(0.045 * fps), int(0.085 * fps)
    out = np.full(len(frames), np.nan)
    nxt = np.append(frames[1:], 10 ** 9)
    for i, f in enumerate(frames):
        if nxt[i] <= f + w2 or f + w2 >= M.shape[1]:
            continue
        shape = A[i] / (A[i].sum() + 1e-12)
        pre = M[:, max(0, f - 3):max(1, f - 1)].mean(axis=1)
        later = np.maximum(0.0, M[:, f + w1:f + w2].mean(axis=1) - pre)
        out[i] = float(later @ shape) / (float(A[i] @ shape) + 1e-12)
    return out


def split_by_ring(groups, sus, min_size=3):
    """Same attack, different ring (closed vs open hat, short vs long 808) -> two sounds."""
    out = []
    for g in groups:
        v = sus[g]
        ok = ~np.isnan(v)
        if ok.sum() < 2 * min_size:
            out.append(g)
            continue
        lv = np.log10(np.clip(v[ok], 0.01, None))
        order = np.sort(lv)
        best, cut, gap_best = 0, None, 0
        for k in range(min_size, len(order) - min_size + 1):
            a, b = order[:k], order[k:]
            gap = np.median(b) - np.median(a)
            mad = (np.median(np.abs(a - np.median(a))) * len(a) + np.median(np.abs(b - np.median(b))) * len(b)) \
                / len(order) + 0.03
            jump = order[k] - order[k - 1]
            if gap / mad > best and jump > 0.15:
                best, cut, gap_best = gap / mad, (order[k - 1] + order[k]) / 2, gap
        if cut is None or best < 3.0 or gap_best < 0.35:
            out.append(g)
            continue
        lo = [i for i, x in zip(g, sus[g]) if not np.isnan(x) and np.log10(max(x, 0.01)) <= cut]
        hi = [i for i, x in zip(g, sus[g]) if not np.isnan(x) and np.log10(max(x, 0.01)) > cut]
        unk = [i for i, x in zip(g, sus[g]) if np.isnan(x)]
        (lo if len(lo) >= len(hi) else hi).extend(unk)
        out.extend([np.array(sorted(lo)), np.array(sorted(hi))])
    return sorted(out, key=len, reverse=True)


def band_loudness(A):
    """Loudness with every band weighted equally (hats count as much as 808s)."""
    out = np.zeros(len(A))
    for a, b in BANDS:
        bs = A[:, a:b].sum(axis=1)
        out += bs / (np.percentile(bs, 99) + 1e-12)
    return out


def merge_bursts(frames, times, A):
    """A look-alike within 40 ms = the same hit still building (clap bursts, flammed
    samples): keep the first time, add up the energy."""
    S = _shape(A)
    keep = np.ones(len(times), bool)
    A = A.copy()
    last = 0
    for i in range(1, len(times)):
        if times[i] - times[last] < 0.042 and S[i] @ S[last] > 0.4:
            A[last] = A[last] + A[i]
            keep[i] = False
            continue
        last = i
    return frames[keep], times[keep], A[keep]


def attack_spectra(M, frames):
    """Energy each hit ADDED, per band. Only genuinely new energy counts: a band that
    grows a little (an 808 gliding down, a cymbal wash) is mostly ignored, a band that
    jumps (a real hit) counts fully:  added = (post - pre)^2 / post."""
    n = M.shape[1]
    A = np.zeros((len(frames), NMEL), np.float32)
    for i, f in enumerate(frames):
        pre = M[:, max(0, f - 3):max(1, f - 1)].max(axis=1)
        post = M[:, min(n - 1, f):min(n, f + 4)].mean(axis=1)
        d = np.maximum(0.0, post - pre)
        A[i] = d * d / (post + 1e-12)
    return A


# ------------------------------------------------------------------ grouping
def _shape(A):
    s = np.sqrt(A / (A.sum(axis=1, keepdims=True) + 1e-12))     # Hellinger
    return s / (np.linalg.norm(s, axis=1, keepdims=True) + 1e-12)


def group_hits(A, thr=None):
    thr = thr or KNOBS["group_thr"]
    """Leader clustering to micro groups, then average-linkage merge."""
    S = _shape(A)
    order = np.argsort(-A.sum(axis=1))          # loud hits lead
    leaders, members = [], []
    for i in order:
        if leaders:
            L = np.array([S[l] for l in leaders])
            d = 1 - L @ S[i]
            k = int(np.argmin(d))
            if d[k] < thr * 0.5:
                members[k].append(i)
                continue
        leaders.append(i)
        members.append([i])
    C = np.array([S[m].mean(axis=0) for m in members])
    C /= np.linalg.norm(C, axis=1, keepdims=True) + 1e-12
    w = np.array([len(m) for m in members], float)
    if len(C) > 1:
        from scipy.cluster.hierarchy import fcluster, linkage
        from scipy.spatial.distance import pdist
        Z = linkage(pdist(C, "cosine"), "average")
        lab = fcluster(Z, t=thr, criterion="distance")
    else:
        lab = np.array([1])
    groups = {}
    for li, l in enumerate(lab):
        groups.setdefault(l, []).extend(members[li])
    groups = sorted(groups.values(), key=len, reverse=True)
    return [np.array(sorted(g)) for g in groups]


def templates_from_groups(A, groups, min_hits=3):
    """Median attack spectrum per group (unit sum), drop combos of other groups."""
    T, keep = [], []
    for g in groups:
        if len(g) < min_hits:
            continue
        t = np.median(A[g] / (A[g].sum(axis=1, keepdims=True) + 1e-12), axis=0)
        T.append(t / (t.sum() + 1e-12))
        keep.append(g)
    if not T:
        return np.zeros((0, NMEL)), []
    T = np.array(T)
    # near-duplicates (same sample, slightly different backgrounds) -> one sound
    merged = True
    while merged and len(T) > 1:
        merged = False
        Sh = _shape(T)
        sim = Sh @ Sh.T
        np.fill_diagonal(sim, 0)
        a, b = np.unravel_index(np.argmax(sim), sim.shape)
        if sim[a, b] > KNOBS["dup_sim"]:
            a, b = min(a, b), max(a, b)
            keep[a] = np.array(sorted(np.concatenate([keep[a], keep[b]])))
            t = np.median(A[keep[a]] / (A[keep[a]].sum(axis=1, keepdims=True) + 1e-12), axis=0)
            T[a] = t / (t.sum() + 1e-12)
            T = np.delete(T, b, axis=0)
            del keep[b]
            merged = True
    single = np.ones(len(T), bool)
    for k in range(len(T)):
        others = [j for j in range(len(T)) if j != k and single[j]]
        if len(others) < 2:
            continue
        W = T[others].T
        h, res = nnls(W, T[k])
        err = res / (np.linalg.norm(T[k]) + 1e-12)
        share = h / (h.sum() + 1e-12)
        if err < 0.12 and np.sum(share > 0.15) >= 2:
            single[k] = False            # this group = two others played together
    return T[single], [g for g, s in zip(keep, single) if s]


def decompose(A, T):
    """Activations of every template in every hit (NNLS)."""
    H = np.zeros((len(A), len(T)))
    W = T.T
    for i, a in enumerate(A):
        H[i], _ = nnls(W, a)
    return H


def prune_duplicates(A, T, groups, H, keep_ratio=None):
    """Drop sounds that are redundant: if the hits a sound plays on are explained almost
    as well without it, it was a near-copy of another sound (same sample at another
    velocity, or with a different background). Its hits go to the others."""
    keep_ratio = keep_ratio or KNOBS["prune"]
    T = np.array(T)
    alive = list(range(len(T)))
    while len(alive) > 1:
        Ta = T[alive]
        Ha = H[:, alive] if H.shape[1] == len(T) else decompose(A, Ta)
        base = np.sum((A - Ha @ Ta) ** 2, axis=1)
        worst, wj = None, None
        for j, k in enumerate(alive):
            act = np.zeros(len(A), bool)
            act[groups[k]] = True                     # judged on its OWN hits
            if act.sum() == 0:
                worst, wj = 0.0, j
                break
            rest = [x for x in range(len(alive)) if x != j]
            Hr = decompose(A[act], Ta[rest])
            err = np.sum((A[act] - Hr @ Ta[rest]) ** 2, axis=1)
            gain = (err.sum() - base[act].sum()) / (np.sum(A[act] ** 2) + 1e-12)
            if worst is None or gain < worst:
                worst, wj = gain, j
        if worst is None or worst >= keep_ratio:
            break
        dead = alive.pop(wj)
        H = decompose(A, T[alive])
        # give the dead sound's hits to whichever sound now explains them most
        for i in groups[dead]:
            best = int(np.argmax(H[i]))
            groups[alive[best]] = np.append(groups[alive[best]], i)
        H = np.zeros((len(A), len(T)))
        H[:, alive] = decompose(A, T[alive])
    T2 = T[alive]
    g2 = [np.array(sorted(set(groups[k].tolist()))) for k in alive]
    return T2, g2, decompose(A, T2)


def discover_layers(A, T, groups, H, max_new=4):
    if len(T) >= 16:
        return T, groups, H
    R = np.maximum(0.0, A - H @ T)
    rel = np.linalg.norm(R, axis=1) / (np.linalg.norm(A, axis=1) + 1e-12)
    # judge leftovers band by band so a crash over an 808 isn't drowned by the boom
    lvA = band_loudness(A)
    lvR = np.zeros(len(R))
    for a, b in BANDS:
        rs = R[:, a:b].sum(axis=1)
        lvR += rs / (np.percentile(A[:, a:b].sum(axis=1), 99) + 1e-12)
    cand = np.where((rel > 0.3) | (lvR > 0.35 * np.maximum(lvA, 1e-9)))[0]
    cand = cand[lvR[cand] > 0.02 * lvA.max()]
    if len(cand) < 2:
        return T, groups, H
    sub = group_hits(R[cand])
    added = 0
    T = list(T)
    for g in sub:
        if len(g) < 2 or added >= max_new or len(T) >= 16:
            continue
        idx = cand[g]
        t = np.median(R[idx] / (R[idx].sum(axis=1, keepdims=True) + 1e-12), axis=0)
        t = t / (t.sum() + 1e-12)
        sims = _shape(np.array(T)) @ _shape(t[None, :])[0]
        if sims.max() > KNOBS["dup_sim"]:
            continue                       # already a known sound
        T.append(t)
        groups.append(np.array(sorted(idx)))
        added += 1
    T = np.array(T)
    if added:
        H = decompose(A, T)
    return T, groups, H


# ------------------------------------------------------------------ labelling
def _mel_freqs():
    import librosa
    return librosa.mel_frequencies(n_mels=NMEL + 2, fmin=30, fmax=11000)[1:-1]


def sound_features(M, mono44, frames, times, members, T, H, k):
    """Describe sound k from its OWN attack spectrum (template) and ring-out,
    with whatever else was sounding removed (808 tails, cymbal wash)."""
    fq = _mel_freqs()
    t = T[k] / (T[k].sum() + 1e-12)
    centroid = float((fq * t).sum())
    low = float(t[fq < 150].sum())
    high = float(t[fq > 5000].sum())
    band = (fq > 40) & (fq < 1200)
    tb = t[band]
    tonal = float(tb.max() / (np.median(tb) + 1e-12)) if tb.size else 0.0
    pitch = float(fq[band][np.argmax(tb)]) if tb.size else 0.0
    # ring-out: background-subtracted envelope projected on this sound's spectrum
    fps = ASR / HOP
    own = H[:, k]
    share = own / (H.sum(axis=1) + 1e-12)
    nxt = np.append(frames[1:], M.shape[1])
    cand = sorted([i for i in members if share[i] > 0.7], key=lambda i: -own[i])[:16] or list(members)[:16]
    full, cut = [], []
    for i in cand:
        f = frames[i]
        pre = M[:, max(0, f - 3):max(1, f - 1)].mean(axis=1)
        end = min(nxt[i] - 1, f + int(2.5 * fps), M.shape[1])
        if end <= f + 2:
            continue
        env = np.maximum(0.0, M[:, f:end] - pre[:, None]).T @ t
        pk = int(np.argmax(env[:4]))
        e0 = env[pk] + 1e-12
        below = np.where(env[pk:] < e0 * 0.01)[0]                # -20 dB in amplitude
        if len(below):
            full.append(below[0] / fps)
        else:
            cut.append((end - f) / fps)
    if full:
        decay = float(np.median(full))
        if cut and np.median(cut) > decay * 1.5:
            decay = float(max(decay, np.median(cut)))
    elif cut:
        decay = float(np.max(cut))
    else:
        decay = 0.1
    # clap: several bursts in the first 45 ms (mid band so 808 tails don't count)
    from scipy.signal import butter, find_peaks, sosfilt
    sos = butter(2, [900 / (SR / 2), 5000 / (SR / 2)], btype="band", output="sos")
    flams = []
    for i in cand[:8]:
        a = int(times[i] * SR)
        seg = mono44[a:a + int(0.05 * SR)]
        if len(seg) < 400:
            continue
        x = np.abs(sosfilt(sos, seg))
        e = np.convolve(x, np.ones(96) / 96, mode="same")
        pks, _ = find_peaks(e, prominence=e.max() * 0.2, distance=int(0.006 * SR))
        flams.append(len(pks))
    flam = float(np.median(flams)) if flams else 1.0
    return dict(decay=round(decay, 4), centroid=round(centroid, 1), low=round(low, 4), high=round(high, 4),
                tonal=round(tonal, 2), pitch=round(pitch, 1), flam=flam)


# Reference profile per pad: log2 centroid (Hz), low share (<150 Hz), high share (>5 kHz),
# log2 decay (s), log10 tonality. Measured on the factory kit (6 variants) - see tito/bench.py.
PROTO = {
    "kick": (7.17, 0.73, 0.0, -1.94, 3.45), "kick2": (6.51, 0.97, 0.0, 0.63, 4.84),
    "snare": (9.32, 0.02, 0.03, -2.61, 2.62), "clap": (11.11, 0.0, 0.03, -3.12, 1.19),
    "rim": (10.43, 0.0, 0.0, -4.26, 2.35), "chh": (12.87, 0.0, 0.89, -3.24, 1.02),
    "ohh": (12.84, 0.0, 0.87, -0.41, 1.12), "phh": (12.75, 0.0, 0.8, -3.89, 0.9),
    "tom_lo": (6.97, 0.91, 0.0, -0.62, 3.51), "tom_mid": (7.44, 0.34, 0.0, -0.66, 3.22),
    "tom_hi": (7.89, 0.02, 0.0, -1.01, 3.14), "crash": (12.52, 0.0, 0.62, -0.56, 0.91),
    "ride": (12.0, 0.0, 0.27, 0.13, 1.36), "perc1": (9.11, 0.0, 0.0, -0.88, 2.22),
    "perc2": (12.79, 0.0, 0.86, -3.21, 1.36), "fx": (9.57, 0.0, 0.0, -1.31, 1.96),
}
W = np.array([1.0, 3.0, 3.0, 0.6, 0.5])
CATCH_ALL = {"perc1": 1.6, "perc2": 1.6, "fx": 2.0}       # leftovers can always live here


def feat_vec(ft):
    return np.array([np.log2(max(ft["centroid"], 20)), ft["low"], ft["high"],
                     np.log2(max(ft["decay"], 0.02)), np.log10(max(ft["tonal"], 1))])


def label_costs(ft):
    x = feat_vec(ft)
    out = {}
    for key, proto in PROTO.items():
        d = float(np.sqrt(np.sum((W * (x - np.array(proto))) ** 2)))
        out[key] = min(d, CATCH_ALL[key]) if key in CATCH_ALL else d
    return out


def assign_pads(feats, counts):
    """Best overall sound -> pad assignment (Hungarian). Frequent sounds weigh more,
    then toms are ordered low->high by pitch and hats by how long they ring."""
    from scipy.optimize import linear_sum_assignment
    keys = [k for k, _, _ in PADS]
    n = len(feats)
    C = np.full((n, len(keys)), 50.0)
    for i, ft in enumerate(feats):
        if not ft:
            continue
        lc = label_costs(ft)
        wgt = 1.0 + 0.3 * np.log1p(counts[i])
        for j, k in enumerate(keys):
            C[i, j] = lc[k] * wgt
    rows, cols = linear_sum_assignment(C)
    out = {int(r): keys[c] for r, c in zip(rows, cols)}
    # toms: low -> high by measured pitch / brightness
    tom_sounds = [k for k, v in out.items() if v.startswith("tom")]
    if len(tom_sounds) > 1:
        slots = sorted([out[k] for k in tom_sounds], key=["tom_lo", "tom_mid", "tom_hi"].index)
        bright = lambda k: feats[k]["centroid"] if feats[k] else 0
        for k, slot in zip(sorted(tom_sounds, key=bright), slots):
            out[k] = slot
    # hats: shortest ring = pedal/closed, longest = open; most frequent short one = closed
    hats = [k for k, v in out.items() if v in ("chh", "ohh", "phh")]
    if len(hats) > 1:
        slots = sorted([out[k] for k in hats], key=["phh", "chh", "ohh"].index)
        by_dec = sorted(hats, key=lambda k: feats[k]["decay"] if feats[k] else 0)
        for k, slot in zip(by_dec, slots):
            out[k] = slot
        shorts = [k for k in hats if out[k] in ("chh", "phh")]
        if len(shorts) == 2:
            a, b = sorted(shorts, key=lambda k: -counts[k])
            out[a], out[b] = "chh", "phh"
    return out


# ------------------------------------------------------------------ grid
def coherent_tempo(times, H):
    """Tempo whose 16th / 8th / beat grid lines up best with every hit (weighted).
    Double/half ambiguity: prefer the octave inside 80-160 BPM (x2 / ÷2 in the plugin)."""
    from scipy.signal import find_peaks
    w = np.sqrt(H.sum(axis=1) + 1e-12)
    w = w / w.max()
    bs = np.arange(55, 200, 0.05)
    sc = np.empty(len(bs))
    for i, b in enumerate(bs):
        per = 60 / b
        ph = 2j * np.pi * times / per
        sc[i] = (np.abs(np.sum(w * np.exp(4 * ph))) + 0.5 * np.abs(np.sum(w * np.exp(2 * ph)))
                 + 0.5 * np.abs(np.sum(w * np.exp(ph)))) / w.sum()
    pk, _ = find_peaks(sc, distance=10)
    if not len(pk):
        return 120.0
    best = bs[pk[np.argmax(sc[pk])]]
    def score_near(b):
        j = np.abs(bs - b) < 0.02 * b
        return sc[j].max() if j.any() else 0, bs[j][np.argmax(sc[j])] if j.any() else b
    fam = [score_near(best * r) for r in (0.5, 1.0, 2.0) if 55 <= best * r <= 200]
    inside = [(s_, b_) for s_, b_ in fam if 80 <= b_ <= 160 and s_ > 0.5 * sc.max()]
    s_, b_ = max(inside) if inside else max(fam)
    # refine to 0.01 BPM
    fine = np.arange(b_ - 0.3, b_ + 0.3, 0.01)
    f = [np.abs(np.sum(w * np.exp(2j * np.pi * times / (60 / b / 4)))) for b in fine]
    return float(fine[int(np.argmax(f))])


def beat_grid(mono22, M, times, H, pad_of, bpm_hint=None):
    import librosa
    oe = onset_curve(M)
    fps = ASR / HOP
    if bpm_hint:
        bpm0 = float(bpm_hint)
    else:
        bpm0 = coherent_tempo(times, H)
    _, beats = librosa.beat.beat_track(onset_envelope=oe, sr=ASR, hop_length=HOP, bpm=bpm0,
                                       tightness=200, units="frames")
    bt = beats / fps
    if len(bt) < 8:
        period = 60 / bpm0
        bt = np.arange(times[0] if len(times) else 0, (times[-1] if len(times) else 4) + period, period)
    # straight line fit = steady tempo?
    k = np.arange(len(bt))
    p = np.polyfit(k, bt, 1)
    resid = bt - np.polyval(p, k)
    period = float(p[0])
    steady = float(np.sqrt(np.mean(resid ** 2))) < 0.018
    bpm = 60 / period
    if bpm_hint:
        bpm = float(bpm_hint)
        period = 60 / bpm
    elif abs(bpm - round(bpm)) < 0.2:
        bpm = float(round(bpm))
        period = 60 / bpm
    if steady:
        # phase: best fit of all hits (weighted) to the straight grid
        w = H.sum(axis=1) if len(H) else np.ones(len(times))
        ph = np.angle(np.sum(w * np.exp(2j * np.pi * times / period)))
        off = (ph / (2 * np.pi)) * period
        first = off % period
        n = int((times[-1] - first) / period) + 3 if len(times) else 8
        bt = first + period * np.arange(-1, n)
    # downbeat phase from kick on 1, snare on 2/4, crash on 1
    def act(keys):
        idx = [k for k, v in pad_of.items() if v in keys]
        return H[:, idx].sum(axis=1) if idx else np.zeros(len(times))
    kick, snare, crash = act({"kick", "kick2"}), act({"snare", "clap", "rim"}), act({"crash"})
    bi = np.searchsorted(bt, times + 0.04) - 1
    on = np.abs(times - bt[np.clip(bi, 0, len(bt) - 1)]) < 0.05
    sc = []
    for ph in range(4):
        pos = (bi - ph) % 4
        sc.append(np.sum(kick[on & (pos == 0)]) * 1.0 + np.sum(kick[on & (pos == 2)]) * 0.4
                  + np.sum(snare[on & ((pos == 1) | (pos == 3))]) * 1.0
                  - np.sum(snare[on & (pos == 0)]) * 0.5 + np.sum(crash[on & (pos == 0)]) * 1.5)
    phase = int(np.argmax(sc))
    # bar 1 starts at the downbeat at or before the first hit
    first_hit = times[0] if len(times) else 0
    db = [i for i in range(len(bt)) if (i - phase) % 4 == 0]
    start_i = max([i for i in db if bt[i] <= first_hit + 0.05], default=db[0] if db else 0)
    if steady and bt[start_i] > first_hit + 0.05:
        start_i -= 4
    beats_out = list(bt[max(0, start_i):]) if start_i >= 0 else \
        list(bt[0] - period * np.arange(-start_i, 0, -1)) + list(bt)
    return dict(bpm=round(float(bpm), 2), steady=bool(steady), beats=[float(b) for b in beats_out],
                period=period, t0=float(beats_out[0]))


def to_beats(times, grid):
    """Seconds -> beat position on the (possibly drifting) beat map."""
    bt = np.array(grid["beats"])
    per = grid["period"]
    ext = np.concatenate([bt, bt[-1] + per * np.arange(1, 64)])
    i = np.clip(np.searchsorted(ext, times, side="right") - 1, 0, len(ext) - 2)
    frac = (times - ext[i]) / (ext[i + 1] - ext[i])
    pos = i + frac
    pos[times < ext[0]] = (times[times < ext[0]] - ext[0]) / per
    return pos


def relabel_by_rhythm(hits, pads_feat, n_bars):
    """Rhythm is a strong clue for names: the sound on 2 and 4 (or 3 in half-time)
    most of the time is the snare/clap; the low sound landing on beat 1 is the kick.
    Swaps pad indexes when the sound-only naming put them elsewhere."""
    if n_bars < 2 or not hits:
        return hits, {}
    per = {}
    for h in hits:
        per.setdefault(h["pad"], []).append(h["beat"])
    def share(pad, positions):
        b = np.array(per.get(pad, []))
        if len(b) == 0:
            return 0.0
        ph = b % 4
        on = np.zeros(len(b), bool)
        for p in positions:
            on |= np.abs(ph - p) < 0.1
        return float(on.mean())
    swaps = {}
    def swap(a, b):
        for h in hits:
            if h["pad"] == a: h["pad"] = b
            elif h["pad"] == b: h["pad"] = a
        swaps[a], swaps[b] = b, a
    snare_pads = {P_SN, P_CL, P_RIM}
    cand = [p for p in per if len(per[p]) >= 0.8 * n_bars and pads_feat.get(p, {}).get("centroid", 0) > 300
            and pads_feat.get(p, {}).get("low", 1) < 0.5]
    if cand:
        best = max(cand, key=lambda p: max(share(p, (1, 3)), share(p, (2,))))
        bb = max(share(best, (1, 3)), share(best, (2,)))
        if best not in snare_pads and bb > 0.6:
            tgt = max(snare_pads, key=lambda q: -1 if q not in per else -abs(len(per[q])))
            tgt = P_SN if (P_SN not in per or max(share(P_SN, (1, 3)), share(P_SN, (2,))) < bb) else P_CL
            swap(best, tgt)
    lows = [p for p in per if pads_feat.get(p, {}).get("low", 0) > 0.4 and len(per[p]) >= 0.5 * n_bars]
    if lows:
        best = max(lows, key=lambda p: share(p, (0,)) * np.log1p(len(per[p])))
        if best not in (P_K, P_K2) and share(best, (0,)) > 0.3:
            k_bb = share(P_K, (0,)) if P_K in per else -1
            if share(best, (0,)) > k_bb:
                swap(best, P_K)
    return hits, swaps


P_K, P_K2, P_SN, P_CL, P_RIM = (PAD_INDEX[k] for k in ("kick", "kick2", "snare", "clap", "rim"))


# ------------------------------------------------------------------ sections
def sections(hits, n_bars):
    """Split the song where the bar patterns change; label Intro/Verse/Hook/..."""
    if n_bars < 4:
        return [dict(start=0, end=n_bars, label="Loop")], []
    P = np.zeros((n_bars, 16, 16))
    for h in hits:
        b = int(h["beat"] // 4)
        if 0 <= b < n_bars:
            P[b, h["pad"], int((h["beat"] % 4) * 4) % 16] += h["vel"] / 127
    V = P.reshape(n_bars, -1)
    energy = V.sum(axis=1)
    Vn = V / (np.linalg.norm(V, axis=1, keepdims=True) + 1e-9)
    crash_on_1 = P[:, PAD_INDEX["crash"], 0] > 0
    # novelty: compare 4-bar blocks before / after each bar line
    nov = np.zeros(n_bars)
    for b in range(2, n_bars - 1):
        a = Vn[max(0, b - 4):b].mean(axis=0)
        c = Vn[b:min(n_bars, b + 4)].mean(axis=0)
        e1, e2 = energy[max(0, b - 4):b].mean(), energy[b:min(n_bars, b + 4)].mean()
        nov[b] = (1 - a @ c / (np.linalg.norm(a) * np.linalg.norm(c) + 1e-9)) + \
            0.5 * abs(e1 - e2) / (max(e1, e2) + 1e-9) + 0.25 * crash_on_1[b]
    bounds = [0]
    thr = max(0.12, np.percentile(nov, 85))
    for b in range(4, n_bars - 3):
        if b - bounds[-1] < 4:
            continue
        if nov[b] >= thr and (b % 4 == 0 or nov[b] > 1.5 * thr):
            bounds.append(b)
    bounds.append(n_bars)
    segs = [(bounds[i], bounds[i + 1]) for i in range(len(bounds) - 1)]
    feats = [Vn[a:b].mean(axis=0) for a, b in segs]
    en = [energy[a:b].mean() for a, b in segs]
    letters, reps = [], []
    for f in feats:
        for li, r in enumerate(reps):
            if f @ r / (np.linalg.norm(f) * np.linalg.norm(r) + 1e-9) > 0.9:
                letters.append(li)
                break
        else:
            reps.append(f)
            letters.append(len(reps) - 1)
    counts = np.bincount(letters)
    out = []
    med = np.median(en)
    hook_letter = None
    rep_letters = [l for l in range(len(reps)) if counts[l] >= 2]
    if rep_letters:
        hook_letter = max(rep_letters, key=lambda l: np.mean([e for e, x in zip(en, letters) if x == l]))
    for i, ((a, b), l, e) in enumerate(zip(segs, letters, en)):
        if i == 0 and e < med * 0.85 and len(segs) > 2:
            lab = "Intro"
        elif i == len(segs) - 1 and e < med * 0.85 and len(segs) > 2:
            lab = "Outro"
        elif l == hook_letter and len(segs) > 1:
            lab = "Hook"
        elif counts[l] == 1 and 0 < i < len(segs) - 1 and len(segs) > 3:
            lab = "Bridge"
        else:
            lab = "Verse"
        out.append(dict(start=int(a), end=int(b), label=lab))
    # fills already in the song: last bar of a section that breaks from its own pattern
    fills = []
    toms = [PAD_INDEX[k] for k in ("tom_lo", "tom_mid", "tom_hi", "snare")]
    for s in out:
        b = s["end"] - 1
        if b <= s["start"]:
            continue
        typ = Vn[s["start"]:b].mean(axis=0)
        dev = 1 - Vn[b] @ typ / (np.linalg.norm(typ) + 1e-9)
        busy = P[b, toms, 8:].sum() > 1.5 * (P[s["start"]:b, :, :][:, toms, 8:].sum() / max(1, b - s["start"]) + 0.3)
        if dev > 0.25 or busy:
            fills.append(dict(bar=int(b), dev=round(float(dev), 2)))
    return out, fills


# ------------------------------------------------------------------ chops
def choose_chop(times, H, k, members):
    own = H[:, k]
    share = own / (H.sum(axis=1) + 1e-12)
    prev_gap = np.diff(np.concatenate([[-10.0], times]))
    next_gap = np.diff(np.concatenate([times, [times[-1] + 10]]))
    best, bi = -1e9, None
    p95 = np.percentile(own[members], 95) if len(members) else 1
    for i in members:
        if own[i] <= 0:
            continue
        loud = min(own[i] / (p95 + 1e-12), 1.0)
        sc = 3 * share[i] + 1.5 * min(next_gap[i], 1.0) + 0.6 * min(prev_gap[i], 0.3) + loud
        if sc > best:
            best, bi = sc, i
    return bi


def write_chop(y44, t, length, path):
    import soundfile as sf
    a = max(0, int((t - 0.0015) * SR))
    b = min(len(y44), int((t + length) * SR))
    seg = y44[a:b].copy()
    if len(seg) < 64:
        return False
    nf = min(len(seg) // 2, int(0.008 * SR))
    seg[-nf:] *= np.linspace(1, 0, nf)[:, None]
    ni = min(16, len(seg))
    seg[:ni] *= np.linspace(0, 1, ni)[:, None]
    peak = np.max(np.abs(seg)) + 1e-9
    sf.write(path, (seg / peak * 0.95).astype(np.float32), SR, subtype="PCM_24")
    return True


# ------------------------------------------------------------------ main
def analyze(path, out_dir, bpm=None, sensitivity=0.5, progress=None):
    say = progress or (lambda f, m: None)
    say(0.02, "Loading drums")
    y44 = load(path)
    mono44 = y44.mean(axis=1)
    import soxr
    mono22 = soxr.resample(mono44, SR, ASR).astype(np.float32)
    dur = len(mono44) / SR
    say(0.08, "Finding every hit")
    M = _mel_power(mono22)
    curve = onset_curve(M)
    frames = pick_peaks(curve, sens=sensitivity)
    if len(frames) == 0:
        raise ValueError("No drum hits found in this file.")
    times = refine_times(frames, mono44)
    A_raw = attack_spectra(M, frames)
    # every band measured against its own typical hit level: a quiet hat is a full-size
    # event in the highs, an 808 tail wobble is a tiny one in the lows
    bin_scale = np.percentile(A_raw, 95, axis=0) + 1e-9
    A = A_raw / bin_scale
    loud = band_loudness(A)
    keep = loud > np.percentile(loud, 99) * (0.0005 + 0.006 * (1 - sensitivity))
    frames, times, A = frames[keep], times[keep], A[keep]
    frames, times, A = merge_bursts(frames, times, A)
    say(0.3, f"Grouping {len(times)} hits into sounds")
    groups = split_by_ring(group_hits(A), sustain(M, frames, A))
    T, groups = templates_from_groups(A, groups, min_hits=2 if len(times) < 60 else 3)
    if len(T) == 0:
        raise ValueError("Couldn't find repeating drum sounds in this file.")
    # junk: a "sound" that is one narrow band spike, or whose hits are all whisper-quiet
    lv = band_loudness(A)
    def degenerate(t):
        hi = t[8:]                       # above the kick/808 region a lone spike = junk
        return hi.max() > 0.9 * t.sum()
    ok = [i for i in range(len(T)) if not degenerate(T[i]) and np.median(lv[groups[i]]) > 0.004 * lv.max()]
    if ok:
        T, groups = T[ok], [groups[i] for i in ok]
    while len(T) > 16:                        # rarest sound joins its closest look-alike
        j = len(T) - 1
        sims = _shape(T[:j]) @ _shape(T[j:j + 1])[0]
        k = int(np.argmax(sims))
        groups[k] = np.array(sorted(np.concatenate([groups[k], groups[j]])))
        T, groups = T[:j], groups[:j]
    say(0.45, "Separating hits that overlap")
    H = decompose(A, T)
    # layered sounds that never play alone (crash + kick on the 1 under an 808):
    # whatever the known sounds leave unexplained, if it repeats, is a sound too
    T, groups, H = discover_layers(A, T, groups, H)
    say(0.5, "Merging duplicate sounds")
    T, groups, H = prune_duplicates(A, T, groups, H)
    # each template's typical activation (from its own group)
    typ = np.array([np.median(H[g, k]) if len(g) else 1.0 for k, g in enumerate(groups)])
    say(0.55, "Naming the sounds")
    T_phys = T * bin_scale
    T_phys = T_phys / (T_phys.sum(axis=1, keepdims=True) + 1e-12)
    feats = [sound_features(M, mono44, frames, times, g, T_phys, H, k) for k, g in enumerate(groups)]
    pad_of = assign_pads(feats, [len(g) for g in groups])
    say(0.65, "Beat grid + bar 1")
    grid = beat_grid(mono22, M, times, H, pad_of, bpm)
    beats = to_beats(times, grid)
    say(0.75, "Writing hits")
    hits = []
    p95 = np.array([np.percentile(H[g, k], 95) if len(g) else 1.0 for k, g in enumerate(groups)])
    Wn = T / (np.linalg.norm(T, axis=1, keepdims=True) + 1e-12)      # each sound's own bands
    for i in range(len(times)):
        for k in range(len(T)):
            h = H[i, k]
            if h <= 0 or k not in pad_of:
                continue
            # judged against this sound's own typical level and against how much of
            # the energy IN ITS OWN BANDS it explains (a hat under a kick still counts)
            explains = (h * T[k]) @ Wn[k] / (A[i] @ Wn[k] + 1e-12)
            if h < KNOBS["typ_min"] * typ[k] or explains < KNOBS["explain"]:
                continue
            vel = int(np.clip(round(127 * np.sqrt(min(h / (p95[k] + 1e-12), 1.4) / 1.0)), 12, 127))
            hits.append(dict(t=round(float(times[i]), 5), beat=round(float(beats[i]), 4),
                             pad=PAD_INDEX[pad_of[k]], vel=vel))
    hits.sort(key=lambda h: (h["beat"], h["pad"]))
    n_bars = int(np.ceil((max(h["beat"] for h in hits) + 0.01) / 4)) if hits else 1
    feat_by_pad = {PAD_INDEX[pad_of[k]]: (feats[k] or {}) for k in pad_of}
    hits, swaps = relabel_by_rhythm(hits, feat_by_pad, n_bars)
    if swaps:
        inv = {PAD_INDEX[v]: k for k, v in pad_of.items()}
        new_pad_of = {}
        for pad, k in inv.items():
            new_pad_of[k] = [kk for kk, _, _ in PADS][swaps.get(pad, pad)]
        pad_of = new_pad_of
        hits.sort(key=lambda h: (h["beat"], h["pad"]))
    say(0.85, "Finding sections and fills")
    secs, song_fills = sections(hits, n_bars)
    say(0.9, "Chopping one clean hit per pad")
    pads_dir = os.path.join(out_dir, "pads")
    os.makedirs(pads_dir, exist_ok=True)
    stem = os.path.splitext(os.path.basename(path))[0][:40]
    pads = []
    nxt = np.append(times[1:], dur)
    for k, g in enumerate(groups):
        if k not in pad_of:
            continue
        key = pad_of[k]
        ft = feats[k] or {}
        i = choose_chop(times, H, k, g)
        if i is None:
            continue
        want = float(np.clip((ft.get("decay", 0.3) or 0.3) * 1.6, 0.08, 2.5))
        length = min(want, nxt[i] - times[i] - 0.002) if nxt[i] - times[i] > 0.05 else want
        f = os.path.join(pads_dir, f"{stem}_{PAD_INDEX[key] + 1:02d}_{key}.wav")
        if write_chop(y44, times[i], max(0.05, length), f):
            pads.append(dict(pad=PAD_INDEX[key], key=key, file=os.path.abspath(f),
                             hits=int(sum(1 for h in hits if h["pad"] == PAD_INDEX[key])),
                             from_t=round(float(times[i]), 3),
                             features={a: round(b, 3) if isinstance(b, float) else b for a, b in ft.items()}))
    pads.sort(key=lambda p: p["pad"])
    say(0.98, "Done")
    return dict(file=os.path.abspath(path), duration=dur, bpm=grid["bpm"], steady=grid["steady"],
                t0=grid["t0"], beats=grid["beats"], period=grid["period"], bars=n_bars,
                hits=hits, pads=pads, sections=secs, song_fills=song_fills,
                n_onsets=int(len(times)), n_sounds=int(len(T)))
