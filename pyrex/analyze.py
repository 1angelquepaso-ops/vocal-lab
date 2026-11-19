"""PYREX MAGIK analysis: one song's Suno stems -> clock (BPM, beat map, bar 1), key, checks.

Rules (CATALOGUE/16_PYREX_MAGIK.md):
1. Drums are the clock (TITO PUENTE's engine). Fallback: Percussion, then Bass.
2. Harmony is the key: Keyboard / Synth / Guitar / Strings / Bass / Brass, vocals lower, drums never.
3. One clock + one key for every stem.
Every reading carries a confidence; anything unsure becomes a question for the user, never a guess.
"""
from __future__ import annotations

import os
import re
import tempfile

import numpy as np
import soundfile as sf

KEY_NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])

# stem role by Suno's file names ("2 Drums.wav")
ROLES = {
    'drums': 'clock', 'percussion': 'clock2',
    'bass': 'harmony', 'keyboard': 'harmony', 'synth': 'harmony', 'guitar': 'harmony',
    'strings': 'harmony', 'brass': 'harmony', 'woodwinds': 'harmony', 'piano': 'harmony',
    'lead vocals': 'vocal', 'backing vocals': 'vocal', 'vocals': 'vocal',
    'other': 'other', 'fx': 'other',
}
KEY_WEIGHT = {'keyboard': 1.0, 'piano': 1.0, 'synth': 0.9, 'guitar': 0.9, 'strings': 0.8, 'bass': 0.8,
              'brass': 0.6, 'woodwinds': 0.6, 'lead vocals': 0.6, 'backing vocals': 0.5, 'vocals': 0.6}

QUIET_DB = -50.0          # a stem this quiet (RMS) is treated as empty
LABEL_TOL = 0.005         # Suno label vs measured: 0.5 %
KEY_MARGIN = 0.10         # top-2 key scores closer than this -> ask
TUNE_MIN_CENTS = 10.0     # tuning correction only past this, and only if stems agree


def stem_name(path):
    b = os.path.splitext(os.path.basename(path))[0]
    b = re.sub(r'^\d+\s+', '', b)                 # "2 Drums" -> "Drums"
    b = re.sub(r'\s*-\s*Part_\d+$', '', b)
    return b.strip()


def role_of(name):
    n = name.lower()
    for k, r in ROLES.items():
        if n == k:
            return r
    for k, r in ROLES.items():
        if k in n:
            return r
    return 'unknown'


def label_bpm(folder):
    m = re.search(r'\(([\d.]+)\s*BPM\)', folder, re.I) or re.search(r'([\d.]+)\s*bpm', folder, re.I)
    return float(m.group(1)) if m else None


def song_title(folder):
    t = os.path.basename(os.path.normpath(folder))
    t = re.sub(r'\s*\([\d.]+\s*BPM\)', '', t, flags=re.I)
    t = re.sub(r'\s*Stems$', '', t, flags=re.I)
    return t.strip()


# ------------------------------------------------------------------ pre-checks
def precheck(paths):
    info, problems = [], []
    for p in paths:
        i = sf.info(p)
        info.append(dict(path=p, name=stem_name(p), sr=i.samplerate, frames=i.frames, ch=i.channels,
                         subtype=i.subtype))
    srs = {x['sr'] for x in info}
    lens = {x['frames'] for x in info}
    if len(srs) > 1:
        problems.append('Stems have different sample rates: ' + ', '.join(f"{x['name']} {x['sr']}" for x in info))
    if len(lens) > 1:
        ref = max(lens, key=lambda L: sum(1 for x in info if x['frames'] == L))
        odd = [x['name'] for x in info if x['frames'] != ref]
        problems.append('These stems are a different length from the rest: ' + ', '.join(odd))
    return info, problems


def load_mono(path, sr_out=22050):
    import soxr
    y, sr = sf.read(path, always_2d=True, dtype='float32')
    m = y.mean(axis=1)
    rms = 20 * np.log10(np.sqrt(np.mean(m ** 2)) + 1e-12)
    return soxr.resample(m, sr, sr_out).astype(np.float32), float(rms)


# ------------------------------------------------------------------ clock
CLOCK_PADS = {'kick', 'kick2', 'snare', 'clap'}


def _lines(beats):
    """Grid lines: straight + triplet subdivisions, fine enough that lines sit >= ~60 ms apart
    (16ths at 116, 32nds at 79), so a hit is judged against the line it was meant for."""
    beats = np.asarray(beats, float)
    per = float(np.median(np.diff(beats)))
    n = 4 if per < 0.6 else 8
    k = np.arange(len(beats))
    return np.unique(np.concatenate([np.interp(np.arange(0, len(beats) - 1 + 1e-9, 1 / d), k, beats)
                                     for d in (n, n * 3 // 2)]))


def grid_confidence(hits, beats, win_s=12.0):
    """How well the backbone (kick / snare / clap) sits on the clock.
    drift = the slow part of the error (median over ~12 s): that is CLOCK error and the stretch
            must remove it.
    feel  = the hit-to-hit scatter around it: Suno's human feel, part of the performance, kept.
    Windows whose drift is > 10 ms are returned as flagged spans."""
    beats = np.asarray(beats, float)
    hs = [h for h in hits if h['key'] in CLOCK_PADS]
    if len(hs) < 16:
        hs = list(hits)
    empty = dict(on_grid_10ms=0.0, median_ms=999.0, drift_ms=999.0, feel_ms=999.0, offset_ms=0.0, n=len(hs), bad=[])
    if len(hs) < 16 or len(beats) < 8:
        return empty
    t = np.array([h['t'] for h in hs])
    v = np.array([h['vel'] for h in hs], float)
    m = (t >= beats[0] - 0.05) & (t <= beats[-1] + 0.05) & (v >= np.percentile(v, 30))
    t = t[m]
    if len(t) < 12:
        return empty
    L = _lines(beats)
    j = np.clip(np.searchsorted(L, t), 1, len(L) - 1)
    lo, hi = L[j - 1], L[j]
    near = np.where(t - lo < hi - t, lo, hi)
    r = (t - near) * 1000                                     # signed, ms
    slow = np.array([np.median(r[np.abs(t - x) <= win_s / 2]) for x in t])
    feel = np.abs(r - slow)
    bad, cur = [], None
    dense = np.array([np.sum(np.abs(t - x) <= win_s / 2) for x in t]) >= 8
    for x, d, ok in zip(t, slow, dense):
        if ok and abs(d) > 10:
            if cur and x - cur[1] < win_s:
                cur[1], cur[2] = x, max(cur[2], abs(d))
            else:
                cur = [x, x, abs(d)]
                bad.append(cur)
    bad = [dict(start_s=round(a - win_s / 2, 2), end_s=round(b + win_s / 2, 2), drift_ms=round(d, 1))
           for a, b, d in bad]
    return dict(on_grid_10ms=round(float(np.mean(np.abs(r) < 10)) * 100, 1),
                median_ms=round(float(np.median(np.abs(r))), 2),
                drift_ms=round(float(np.max(np.abs(slow[dense]))) if dense.any() else 999.0, 1),
                feel_ms=round(float(np.median(feel)), 1), offset_ms=round(float(np.median(r)), 2),
                n=int(len(t)), bad=bad)


def local_tempo(beats, win=16):
    b = np.asarray(beats)
    out = []
    for s in range(0, len(b) - win, win // 2):
        out.append((float(b[s]), 60.0 / float(np.polyfit(np.arange(win + 1), b[s:s + win + 1], 1)[0])))
    return out


def clock_from(path, bpm_hint=None):
    from tito.common import PADS
    from tito.transcribe import analyze as tito_analyze
    r = tito_analyze(path, tempfile.mkdtemp(prefix='pyrex_'), bpm=bpm_hint)
    hits = [dict(t=h['t'], vel=h['vel'], key=PADS[h['pad']][0]) for h in r['hits']]
    return dict(bpm=float(r['bpm']), steady=bool(r['steady']), beats=list(r['beats']), t0=float(r['t0']),
                period=float(r['period']), hits=hits, n_hits=len(hits), conf=grid_confidence(hits, r['beats']))


def downbeat(beats, hits):
    """Which beat is "1". Suno trap / Latin is often half-time (snare once a bar, on 3); pop /
    corridos backbeat has snare on 2 and 4. Kick on 1 decides between the backbeat options.
    Returns (index of the first downbeat in beats, style, confidence 0-1)."""
    beats = np.asarray(beats, float)
    def pos_weights(keys):
        w = np.zeros(4)
        for h in hits:
            if h['key'] not in keys:
                continue
            i = int(np.searchsorted(beats, h['t'] + 0.04)) - 1
            if 0 <= i < len(beats) - 1 and abs(h['t'] - beats[i]) < 0.06 * (beats[i + 1] - beats[i]) + 0.01:
                w[i % 4] += h['vel']
        return w
    sn, kk = pos_weights({'snare', 'clap'}), pos_weights({'kick', 'kick2'})
    if sn.sum() < 0.1 * kk.sum():          # no real snare: a rim may carry the backbeat
        sn = pos_weights({'rim'})
    if sn.sum() == 0 and kk.sum() == 0:
        return 0, 'unknown', 0.0
    share = sn / (sn.sum() + 1e-12)
    top = int(np.argmax(share))
    pair = share[top] + share[(top + 2) % 4]
    if share[top] >= 0.7:                                   # half-time: snare on 3
        ph, style, conf = (top - 2) % 4, 'half-time', float(share[top])
    elif pair >= 0.7:                                       # backbeat: snare on 2 + 4
        a, b = (top - 1) % 4, (top + 1) % 4
        ph = a if kk[a] >= kk[b] else b
        style, conf = 'backbeat', float(pair * abs(kk[a] - kk[b]) / (kk[a] + kk[b] + 1e-12))
    else:                                                   # no clear snare: strongest kick
        ph, style = int(np.argmax(kk)), 'kick'
        conf = min(0.35, float(kk[ph] / (kk.sum() + 1e-12)))
    return ph, style, round(conf, 2)


def extend_grid(beats, dur):
    """Grid lines from before 0 s to past the end, at the edge tempo (intro before the drums,
    tail after them stay on the grid)."""
    b = list(np.asarray(beats, float))
    p0 = float(np.median(np.diff(b[:9])))
    p1 = float(np.median(np.diff(b[-9:])))
    head = 0
    while b[0] > 0:
        b.insert(0, b[0] - p0)
        head += 1
    while b[-1] < dur:
        b.append(b[-1] + p1)
    return np.array(b), head


def octave_resolve(measured, label):
    """Same grid, other count (x2 / ÷2) -> Suno's label decides. 3:2 family -> ask."""
    if not label:
        return measured, 'no_label', None
    r = measured / label
    for f, tag in ((1.0, 'match'), (2.0, 'double'), (0.5, 'half')):
        if abs(r / f - 1) <= LABEL_TOL:
            return measured / f, tag, None
    for f in (1.5, 2 / 3, 4 / 3, 0.75, 3.0, 1 / 3):
        if abs(r / f - 1) <= 0.01:
            return measured, 'ratio_3_2', (f"Measured {measured:.2f} BPM is a 3:2-type ratio of Suno's "
                                           f"{label:g}. Which is right?")
    for f in (1.0, 2.0, 0.5):
        if abs(r / f - 1) <= 0.03:
            return measured / f, 'label_off', (f"Suno says {label:g} BPM; the drums measure "
                                               f"{measured / f:.2f}. Using the measurement.")
    return measured, 'label_far', f"Suno says {label:g} BPM but the drums measure {measured:.2f}. Using the measurement."


def confidence_level(c, steady=True, dur=None):
    """green: clock error (drift) <= 10 ms everywhere. amber: some spans drift (flagged with
    markers) or few backbone hits. red: drift everywhere / no usable clock."""
    if c['n'] < 16 or c['drift_ms'] >= 999:
        return 'red'
    if c['drift_ms'] <= 10 and c['n'] >= 32:
        return 'green'
    if dur:
        flagged = sum(b['end_s'] - b['start_s'] for b in c['bad'])
        if flagged > 0.5 * dur:
            return 'red'
    return 'amber'


# ------------------------------------------------------------------ key
def key_scores(v):
    v = v / (v.sum() + 1e-12)
    out = []
    for i in range(12):
        out.append((float(np.corrcoef(v, np.roll(MAJ, i))[0, 1]), KEY_NAMES[i]))
        out.append((float(np.corrcoef(v, np.roll(MIN, i))[0, 1]), KEY_NAMES[i] + 'm'))
    out.sort(reverse=True)
    return out


def _relative(k):
    r = KEY_NAMES.index(k.rstrip('m'))
    return KEY_NAMES[(r + 3) % 12] if k.endswith('m') else KEY_NAMES[(r + 9) % 12] + 'm'


def key_analysis(stems, bar1_s=None, bar_s=None):
    """stems: list of (name, mono22, rms_db). Returns key, margin, tuning, per-window keys."""
    import librosa
    SR = 22050
    H = 2048
    chroma, tunes, per_stem = {}, {}, {}
    for name, y, rms in stems:
        n = name.lower()
        w = next((v for k, v in KEY_WEIGHT.items() if n == k or k in n), 0.0)
        if w == 0 or rms < QUIET_DB:
            continue
        tun = float(librosa.estimate_tuning(y=y, sr=SR))
        C = librosa.feature.chroma_cqt(y=y, sr=SR, hop_length=H, tuning=tun)
        chroma[name] = (w, C)
        if 'bass' not in n:                  # bass tuning reads are unreliable (bench: +41 c)
            tunes[name] = tun * 100
        ks = key_scores(C.sum(axis=1))
        per_stem[name] = [k for _, k in ks[:2]]
    if not chroma:
        return dict(key=None, question='No harmonic stems loud enough to read the key.')
    tot = sum(w * C.sum(axis=1) / (C.sum() + 1e-12) for w, C in chroma.values())
    ks = key_scores(tot)
    (s1, k1), (s2, k2) = ks[0], ks[1]
    margin = s1 - s2
    nfr = min(C.shape[1] for _, C in chroma.values())
    win = int(30 * SR / H)
    wins = []
    for s in range(0, nfr - win // 2, win):
        v = sum(w * C[:, s:s + win].sum(axis=1) / (C[:, s:s + win].sum() + 1e-12) for w, C in chroma.values())
        wins.append(dict(t=round(s * H / SR, 1), key=key_scores(v)[0][1]))
    other = [w for w in wins if w['key'] != k1]
    tv = np.array(list(tunes.values())) if tunes else np.array([0.0])
    tune = float(np.median(tv))
    tune_ok = (np.ptp(tv) <= 10) if len(tv) > 1 else True
    q = None
    rel = _relative(k1) == k2
    if rel and bar1_s is not None and bar_s:
        # relative major / minor share every note: the chord on the "1" names the tonic
        # the FIRST chord of the song: where the harmonic stems start sounding (intros often
        # start before the drums), one bar long
        e = sum(w * C.sum(axis=0) / (C.sum() + 1e-12) for w, C in chroma.values())
        on = np.nonzero(e > 0.25 * np.median(e[e > 0]))[0] if (e > 0).any() else np.array([0])
        a = int(on[0]) if len(on) else 0
        b = max(a + 1, a + int(bar_s * SR / H))
        v = sum(w * C[:, a:b].sum(axis=1) for w, C in chroma.values())
        def triad(k):
            r = KEY_NAMES.index(k.rstrip('m'))
            third = 3 if k.endswith('m') else 4
            return v[r] + v[(r + third) % 12] + v[(r + 7) % 12]
        t1, t2 = triad(k1), triad(k2)
        if t2 > 1.15 * t1:
            k1, k2, s1, s2 = k2, k1, s2, s1
        if max(t1, t2) > 1.15 * min(t1, t2):
            margin = max(margin, KEY_MARGIN)          # decided by the bar-1 chord
    if margin < KEY_MARGIN:
        q = f'Key is {k1} or {k2} (too close to call). Which one?'
    return dict(key=k1, alt=k2, score=round(s1, 3), margin=round(margin, 3), question=q,
                per_stem=per_stem, windows=wins,
                key_change=len(other) >= 2 and len(other) >= 0.25 * len(wins),
                tuning_cents=round(tune, 1), tuning_correct=bool(abs(tune) > TUNE_MIN_CENTS and tune_ok))


# ------------------------------------------------------------------ song
def analyze_song(paths, label=None, bpm_override=None, progress=None):
    say = progress or (lambda f, m: None)
    say(0.02, 'Checking stems')
    info, problems = precheck(paths)
    if problems:
        return dict(ok=False, error=' / '.join(problems), stems=info)
    for x in info:
        x['role'] = role_of(x['name'])
    sr = info[0]['sr']
    dur = info[0]['frames'] / sr
    say(0.08, 'Loading stems')
    mono = {}
    for x in info:
        y, rms = load_mono(x['path'])
        x['rms_db'] = round(rms, 1)
        x['empty'] = rms < QUIET_DB
        mono[x['name']] = (y, rms)
    # clock: drums first, then percussion, then bass
    order = [x for r in ('clock', 'clock2') for x in info if x['role'] == r and not x['empty']]
    order += [x for x in info if x['name'].lower() == 'bass' and not x['empty']]
    if not order:
        order = [x for x in info if x['role'] == 'clock']
    tried, clock, src = [], None, None
    for i, x in enumerate(order[:3]):
        say(0.15 + 0.2 * i, f'Reading the clock from {x["name"]}')
        try:
            c = clock_from(x['path'], bpm_override)
            c['dur'] = dur
        except Exception as e:                                  # noqa: BLE001
            tried.append(dict(stem=x['name'], error=str(e)))
            continue
        c['level'] = confidence_level(c['conf'], c['steady'], c.get('dur'))
        tried.append(dict(stem=x['name'], bpm=c['bpm'], level=c['level'], conf=c['conf']))
        # priority order (drums > percussion > bass): the first stem that isn't red is the clock
        if clock is None or (clock['level'] == 'red' and c['level'] != 'red'):
            clock, src = c, x['name']
        if c['level'] != 'red':
            break
    questions, notes = [], []
    if clock is None:
        return dict(ok=False, error='No stem gave a usable clock.', tried=tried, stems=info)
    measured = clock['bpm']
    if bpm_override:
        bpm, tag = float(bpm_override), 'user'
    else:
        bpm, tag, q = octave_resolve(measured, label)
        if q:
            (questions if tag == 'ratio_3_2' else notes).append(q)
    beats = np.asarray(clock['beats'], float)
    f = measured / bpm
    if abs(f - 1) > 0.01 and not (abs(f - 2) < 0.02 or abs(f - 0.5) < 0.01):
        # 3:2-type user choice: a different lattice -> re-read the clock at that tempo
        say(0.6, f'Re-reading the grid at {bpm:.2f} BPM')
        src_path = next(x['path'] for x in info if x['name'] == src)
        c2 = clock_from(src_path, bpm)
        c2['level'] = confidence_level(c2['conf'], c2['steady'], dur)
        clock, beats = c2, np.asarray(c2['beats'], float)
    cands = [beats]
    if abs(f - 2) < 0.02:          # same lattice, half the count: try both parities
        cands = [beats[0::2], beats[1::2]]
    elif abs(f - 0.5) < 0.01:
        cands = [np.sort(np.concatenate([beats, (beats[:-1] + beats[1:]) / 2]))]
    best = None
    for bb in cands:
        ph, style, dconf = downbeat(bb, clock['hits'])
        conf = grid_confidence(clock['hits'], bb)
        score = dconf - 0.001 * conf['drift_ms']
        if best is None or score > best[0]:
            best = (score, bb, ph, style, dconf, conf)
    _, beats, ph, style, dconf, conf = best
    clock['conf'] = conf
    clock['level'] = confidence_level(conf, clock['steady'], dur)
    # first downbeat at or before the first backbone hit = "music bar 1"
    first_hit = min((h['t'] for h in clock['hits'] if h['key'] in CLOCK_PADS), default=beats[0])
    dbs = [i for i in range(len(beats)) if i % 4 == ph]
    i1 = max([i for i in dbs if beats[i] <= first_hit + 0.05], default=dbs[0])
    music_bar1 = float(beats[i1])
    # bar lines = where the backbone hits actually land (removes a constant onset/phase bias)
    shift = conf.get('offset_ms', 0.0) / 1000 if abs(conf.get('offset_ms', 0.0)) < 15 else 0.0
    beats = beats + shift
    music_bar1 = float(beats[i1])
    grid, head = extend_grid(beats[i1 % 4:], dur)          # keep the bar phase: start on a downbeat
    # file start = the bar line at or before 0 s (silence pads it when it is negative)
    pre = int(np.ceil(head / 4) * 4)
    while len(grid) and pre > head:
        grid = np.insert(grid, 0, grid[0] - (grid[1] - grid[0]))
        head += 1
    start_s = float(grid[0])
    bar_of_music1 = int(round((np.argmin(np.abs(grid - music_bar1))) / 4)) + 1
    period = float(np.median(np.diff(beats)))
    lt = local_tempo(beats)
    tempos = [b for _, b in lt] or [bpm]
    if not clock['steady']:
        notes.append(f'Tempo drifts {min(tempos):.1f}-{max(tempos):.1f} BPM: Grid snap recommended.')
    if dconf < 0.4:
        questions.append(f'Bar 1 is a best guess ({style}). Check it lines up and nudge ±1 beat if not.')
    say(0.75, 'Reading the key')
    key = key_analysis([(n, y, r) for n, (y, r) in mono.items()], music_bar1, 4 * period)
    if key.get('question'):
        questions.append(key['question'])
    if key.get('key_change'):
        notes.append('The key seems to change in parts of the song: check before transposing.')
    mode = 'global' if clock['steady'] else 'grid'
    level = clock['level']
    say(0.98, 'Done')
    return dict(ok=True, title=None, duration=round(dur, 3), sr=sr,
                stems=[{k: v for k, v in x.items() if k != 'path'} | dict(path=x['path']) for x in info],
                clock=dict(source=src, measured_bpm=round(measured, 3), bpm=round(bpm, 3),
                           display_bpm=int(round(bpm)), label_bpm=label, label_check=tag,
                           steady=clock['steady'], mode=mode, level=level, conf=clock['conf'],
                           music_bar1_s=round(music_bar1, 4), music_bar1_is_bar=bar_of_music1,
                file_start_s=round(start_s, 4), pad_silence_s=round(max(0.0, -start_s), 4),
                downbeat_style=style, downbeat_conf=dconf, phase_shift_ms=round(shift * 1000, 2), period=period,
                           local_bpm_min=round(min(tempos), 2), local_bpm_max=round(max(tempos), 2),
                           beats=[round(float(b), 5) for b in grid], tried=tried),
                key=key, questions=questions, notes=notes)
