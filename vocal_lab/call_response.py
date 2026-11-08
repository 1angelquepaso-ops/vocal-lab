"""Call & response: answer the lead in its own gaps.

After each phrase, a fragment of that phrase (its tail, or its opening hook) is
re-sung in the gap that follows, landing on the beat grid. The fragment is
transformed in key: echoed, moved up or down by scale steps, flipped by an
octave, mirrored (melodic inversion), or "answered" (transposed so its notes
land on the beat's chord tones at the moment they're heard).
"""
from dataclasses import dataclass, field

import numpy as np
import scipy.signal as ss

from . import harmony as H


@dataclass
class CRFeel:
    min_gap: float = 0.35      # smallest gap (s) worth answering
    density: float = 0.8       # chance to answer any given gap
    wait: float = 0.05         # earliest start after the phrase ends
    margin: float = 0.08       # must finish this long before the next phrase
    max_notes: int = 5         # longest fragment
    repeat: float = 0.35       # chance of a second, ping-ponged repeat in long gaps
    strict_grid: bool = False  # True: skip gaps too tight to start on a 16th
    weights: dict = field(default_factory=lambda: {
        'echo': 0.6, 'step_up': 1.2, 'step_down': 1.0, 'octave': 0.6,
        'mirror': 0.9, 'answer': 1.6})


def phrases(notes, min_gap):
    """Split the lead into phrases at gaps >= min_gap. Returns
    [(phrase_notes, gap_start, gap_end)] for every phrase followed by a gap."""
    out, cur = [], []
    for a, b in zip(notes, notes[1:] + [None]):
        cur.append(a)
        gap_end = b['start'] if b else a['end'] + 4.0
        if gap_end - a['end'] >= min_gap:
            out.append((cur, a['end'], gap_end))
            cur = []
    return out


def snap_up(t, grid, sub=2):
    """First grid subdivision (sub per beat) at or after t."""
    if grid is None or len(grid) < 2:
        return t
    beat = grid[1] - grid[0]
    step = beat / sub
    k = np.ceil((t - grid[0]) / step - 1e-6)
    return float(grid[0] + k * step)


def _transform(kind, srcs, key, rng):
    idx = [int(np.where(key.notes == s)[0][0]) for s in srcs]
    if kind == 'echo':
        return list(srcs)
    if kind == 'step_up':
        k = int(rng.choice([2, 2, 4]))
        return [int(key.notes[i + k]) for i in idx]
    if kind == 'step_down':
        k = int(rng.choice([-2, -5, -5]))
        return [int(key.notes[i + k]) for i in idx]
    if kind == 'octave':
        d = 12 if rng.random() < 0.5 else -12
        return [s + d for s in srcs]
    if kind == 'mirror':
        shift = int(rng.choice([0, 2, -2, 4]))
        return [int(key.notes[idx[0] - (i - idx[0]) + shift]) for i in idx]
    raise ValueError(kind)


def _chord_fit(targets, times, chords):
    if not chords:
        return 0.5
    hits = [t % 12 in (H.chord_at(chords, tm) or set()) for t, tm in zip(targets, times)]
    # the last note matters most: it's where the answer comes to rest
    return 0.6 * np.mean(hits) + 0.4 * float(hits[-1])


def plan(notes, key, chords=None, grid=None, feel=CRFeel(), seed=0,
         lead_range=None):
    """Decide every response. Returns a list of dicts:
    {start, delta, kind, pan, notes:[{src_start, src_end, src, target, onset}]}
    """
    rng = np.random.default_rng(seed)
    if lead_range is None:
        ps = [key.snap(n['pitch']) for n in notes]
        lead_range = (min(ps) - 5, max(ps) + 7)
    out = []
    side = 1
    for ph, g0, g1 in phrases(notes, feel.min_gap):
        if rng.random() > feel.density:
            continue
        # land on an 8th if it fits, else a 16th, else just fit the gap
        for sub in ((2, 4) if feel.strict_grid else (2, 4, None)):
            start = snap_up(g0 + feel.wait, grid, sub) if sub else g0 + feel.wait
            room = g1 - feel.margin - start
            if room >= 0.22:
                break
        else:
            continue
        # candidate fragments: tails and heads of the phrase
        frags = []
        for k in range(1, min(feel.max_notes, len(ph)) + 1):
            for frag, label in ((ph[-k:], 'tail'), (ph[:k], 'head')):
                dur = frag[-1]['end'] - frag[0]['start']
                if dur <= room and dur > 0.12:
                    fill = dur / room
                    w = (0.4 + fill) * (1.4 if label == 'tail' else 1.0)
                    frags.append((frag, w))
        if not frags:
            # nothing fits whole: take the phrase's last note, trimmed
            last = dict(ph[-1])
            last['end'] = min(last['end'], last['start'] + room)
            if last['end'] - last['start'] < 0.15:
                continue
            frags = [([last], 1.0)]
        fw = np.array([w for _, w in frags])
        frag = frags[int(rng.choice(len(frags), p=fw / fw.sum()))][0]
        delta = start - frag[0]['start']
        srcs = [key.snap(n['pitch']) for n in frag]
        times = [n['start'] + delta for n in frag]

        # score each transformation against the beat's chords
        opts = []
        for kind, w in feel.weights.items():
            if kind == 'answer':
                best = None
                for st in (-5, -4, -2, 2, 3, 4, 5):
                    tg = [int(key.notes[int(np.where(key.notes == s)[0][0]) + st])
                          for s in srcs]
                    f = _chord_fit(tg, times, chords)
                    if best is None or f > best[0]:
                        best = (f, tg)
                fit, tg = best
            else:
                tg = _transform(kind, srcs, key, rng)
                fit = _chord_fit(tg, times, chords)
            if not all(lead_range[0] <= t <= lead_range[1] for t in tg):
                continue
            opts.append((kind, tg, w * (0.3 + fit) ** 2))
        if not opts:
            continue
        ow = np.array([o[2] for o in opts])
        kind, tg, _ = opts[int(rng.choice(len(opts), p=ow / ow.sum()))]
        side = -side
        resp = {'start': start, 'delta': delta, 'kind': kind, 'pan': 0.6 * side,
                'gain_db': -4.0,
                'notes': [{'src_start': n['start'], 'src_end': n['end'],
                           'src': s, 'target': t, 'onset': True}
                          for n, s, t in zip(frag, srcs, tg)]}
        out.append(resp)
        # ping-pong repeat if there's room after the first answer
        dur = frag[-1]['end'] - frag[0]['start']
        rep_start = snap_up(start + dur + 0.05, grid)
        if rng.random() < feel.repeat and rep_start + dur <= g1 - feel.margin:
            out.append({**resp, 'start': rep_start,
                        'delta': rep_start - frag[0]['start'],
                        'kind': kind + '+repeat', 'pan': -resp['pan'],
                        'gain_db': -8.0})
    return out


def lead_level(y, sr, notes):
    """Median RMS of the lead while it sings: the reference responses sit under."""
    r = [np.sqrt(np.mean(y[int(n['start'] * sr):int(n['end'] * sr)] ** 2))
         for n in notes if n['end'] - n['start'] > 0.1]
    return float(np.median(r)) if r else 0.05


def render(y, sr, bank, responses, ref_rms=None, preroll=0.03, release=0.08,
           xfade=0.03, tail_fade=0.12, highpass=180.0):
    """Render all responses to a stereo stem (same length as y).

    With ref_rms, each response is level-matched to the lead's singing level
    before its gain_db is applied, so a soft phrase tail doesn't come back
    inaudibly quiet."""
    out = np.zeros((len(y), 2), dtype='float32')
    fl = int(xfade * sr)
    for r in responses:
        voice = np.zeros(len(y), dtype='float32')
        ns = r['notes']
        for j, n in enumerate(ns):
            iv = int(n['target'] - n['src'])
            s = n['src_start'] - (preroll if j == 0 else 0.0)
            e = n['src_end'] + (release if j == len(ns) - 1 else xfade / 2)
            if j + 1 < len(ns):
                e = min(e, ns[j + 1]['src_start'] + xfade / 2)
            a, b = int(max(s, 0) * sr), int(min(e * sr, len(y)))
            seg = bank[iv][a:b].copy()
            f = min(fl, len(seg) // 2)
            if f:
                ramp = np.sin(np.linspace(0, np.pi / 2, f)) ** 2
                seg[:f] *= ramp
                seg[-f:] *= ramp[::-1]
            d = int(r['delta'] * sr)
            a2, b2 = a + d, min(b + d, len(y))
            if a2 < 0 or a2 >= len(y):
                continue
            voice[a2:b2] += seg[:b2 - a2]
        # soften the end so the answer trails off instead of cutting
        nz = np.nonzero(voice)[0]
        if len(nz):
            end = nz[-1]
            tf = min(int(tail_fade * sr), end - nz[0])
            voice[end - tf:end] *= np.linspace(1, 0, tf) ** 1.5
        if highpass:
            bb, aa = ss.butter(2, highpass / (sr / 2), 'high')
            voice = ss.lfilter(bb, aa, voice).astype('float32')
        if ref_rms:
            nz = np.abs(voice) > 1e-5
            cur = np.sqrt(np.mean(voice[nz] ** 2)) if nz.any() else 0
            if cur > 0:
                voice *= min(ref_rms / cur, 4.0)      # cap the boost at +12 dB
        voice *= 10 ** (r['gain_db'] / 20)
        th = (r['pan'] + 1) * np.pi / 4
        out[:, 0] += voice * np.cos(th)
        out[:, 1] += voice * np.sin(th)
    return out
