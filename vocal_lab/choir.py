"""Choir stack: 2-4 harmony voices voiced together as chords.

For every lead note, all voices are chosen at once from the key, so they form
a chord with the lead and with each other. Voicings are scored on:
  - no clashes between ANY two sung parts (lead included),
  - chord tones of the beat's chord (colour tones allowed as an option),
  - small, smooth moves for every voice (good voice leading),
  - no parallel 5ths / octaves, no voice crossing, no doubled notes.
Among the best-scoring voicings one is drawn at random (seeded), so every
run is a different arrangement.
"""
import itertools
from dataclasses import dataclass

import numpy as np

from . import harmony as H

# voice name -> semitone window relative to the lead
LAYOUTS = {
    'above2': {'high': (5, 14), 'mid': (2, 9)},
    'around2': {'high': (2, 9), 'low': (-9, -2)},
    'below2': {'mid': (-9, -2), 'low': (-17, -5)},
    'full4': {'high': (5, 14), 'mid': (2, 9), 'low': (-9, -2), 'bass': (-17, -5)},
    'wide3': {'high': (5, 14), 'mid': (2, 9), 'low': (-9, -2)},
}

PAIR_CLASH = {1, 6, 11}           # minor 2nd, tritone, major 7th between parts
SOFT_CLASH = {2, 10}              # major 2nd / minor 7th: colour, but costs


@dataclass
class ChoirFeel:
    layout: str = 'around2'
    colour: float = 0.3      # 0 = chord tones only, 1 = 9ths/sus freely
    smooth: float = 1.0      # weight on small voice moves
    variety: float = 0.6     # 0 = always best voicing, 1 = loose random pick
    top_k: int = 6
    parallel_penalty: float = 12.0   # cost of a parallel 5th/octave between parts
    _key: object = None              # set by arrange()


def _cost(v, prev, lead, prev_lead, chord, feel, onset):
    names = list(v)
    parts = [lead] + [v[n] for n in names]
    c = 0.0
    # clashes between every pair of sung parts
    for a, b in itertools.combinations(parts, 2):
        ic = abs(a - b) % 12
        if a == b:
            return np.inf
        if ic in PAIR_CLASH:
            return np.inf
        if ic in SOFT_CLASH:
            c += 2.5 * (1 - 0.7 * feel.colour)
    # harmony vs lead: strict on syllable onsets
    if onset:
        for n in names:
            if (abs(v[n] - lead) % 12) in H.CLASH:
                return np.inf
    # chord fit
    if chord:
        colour = H.colour_tones(chord, feel._key) if feel._key else set()
        for n in names:
            pc = v[n] % 12
            if pc not in chord:
                c += 1.6 * (1 - feel.colour) if pc in colour else 1.6
        # the chord should be complete-ish across lead + voices
        covered = {p % 12 for p in parts} & set(chord)
        c += 0.6 * (len(chord) - len(covered))
    # doubled pitch classes (thin sound)
    pcs = [p % 12 for p in parts]
    c += 0.9 * (len(pcs) - len(set(pcs)))
    if prev:
        for n in names:
            mv = abs(v[n] - prev[n])
            c += feel.smooth * (0.0 if mv == 0 else 0.35 * mv if mv <= 2
                                else 0.5 * mv + 0.5)
        # parallel 5ths / octaves between any two parts
        old = [prev_lead] + [prev[n] for n in names]
        for (a0, a1), (b0, b1) in itertools.combinations(zip(old, parts), 2):
            if a1 != a0 and b1 != b0 and np.sign(a1 - a0) == np.sign(b1 - b0):
                if abs(a0 - b0) % 12 in (0, 7) and abs(a1 - b1) % 12 == abs(a0 - b0) % 12:
                    c += feel.parallel_penalty
    return c


def arrange(notes, key, chords=None, feel=ChoirFeel(), seed=0):
    """Return {voice: [(start, end, interval, target)]} plus per-note voicings."""
    rng = np.random.default_rng(seed)
    feel._key = key
    layout = LAYOUTS[feel.layout]
    names = sorted(layout, key=lambda n: -layout[n][1])   # high to low
    plans = {n: [] for n in names}
    prev, prev_lead = None, None
    for i, note in enumerate(notes):
        lead = key.snap(note['pitch'])
        chord = H.chord_at(chords or [], note['start'])
        cands = {n: [int(m) for m in key.notes
                     if lead + layout[n][0] <= m <= lead + layout[n][1]]
                 for n in names}
        scored = []
        for combo in itertools.product(*(cands[n] for n in names)):
            # keep the stack ordered: no voice crossing
            if any(combo[j] <= combo[j + 1] for j in range(len(combo) - 1)):
                continue
            v = dict(zip(names, combo))
            c = _cost(v, prev, lead, prev_lead, chord, feel, onset=True)
            if np.isfinite(c):
                scored.append((c, v))
        if not scored:
            # nothing legal: hold previous voicing if it still works, else rest
            if prev and np.isfinite(_cost(prev, None, lead, None, chord, feel, True)):
                v = prev
            else:
                prev_lead = lead
                continue
        else:
            scored.sort(key=lambda x: x[0])
            top = scored[:feel.top_k]
            cs = np.array([s[0] for s in top])
            temp = 0.15 + 2.0 * feel.variety
            w = np.exp(-(cs - cs.min()) / temp)
            v = top[int(rng.choice(len(top), p=w / w.sum()))][1]
        for n in names:
            plans[n].append((note['start'], note['end'], int(v[n] - lead), int(v[n])))
        prev, prev_lead = v, lead
    return plans


def check(plans, notes, key, chords=None):
    """Measured checks on an arrangement: clashes, chord fit, motion."""
    names = list(plans)
    by_start = {n: {p[0]: p for p in plans[n]} for n in names}
    lead_by = {n['start']: key.snap(n['pitch']) for n in notes}
    clashes = soft = par = 0
    ct = []
    moves = {n: [] for n in names}
    prev = None
    for s, lead in lead_by.items():
        v = {n: by_start[n][s][3] for n in names if s in by_start[n]}
        if len(v) < len(names):
            prev = None
            continue
        parts = [lead] + list(v.values())
        for a, b in itertools.combinations(parts, 2):
            ic = abs(a - b) % 12
            clashes += ic in PAIR_CLASH or a == b
            soft += ic in SOFT_CLASH
        ch = H.chord_at(chords or [], s)
        if ch:
            ct += [p % 12 in ch for p in v.values()]
        if prev:
            for n in names:
                moves[n].append(abs(v[n] - prev[1][n]))
            old = [prev[0]] + [prev[1][n] for n in names]
            for (a0, a1), (b0, b1) in itertools.combinations(zip(old, parts), 2):
                if a1 != a0 and b1 != b0 and np.sign(a1 - a0) == np.sign(b1 - b0) \
                        and abs(a0 - b0) % 12 in (0, 7) and abs(a1 - b1) % 12 == abs(a0 - b0) % 12:
                    par += 1
        prev = (lead, v)
    return {'hard_clashes': clashes, 'colour_seconds': soft,
            'parallel_5ths_8ves': par,
            'chord_tones': f"{100 * np.mean(ct):.0f}%" if ct else 'n/a',
            'avg_move_semitones': {n: round(float(np.mean(m)), 2) for n, m in moves.items() if m}}
