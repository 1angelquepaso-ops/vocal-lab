"""Melody mode: independent, randomized-in-key counter-melodies.

Instead of a fixed interval (3rd / 5th / octave), every harmony note is chosen
fresh from the key, based on where the harmony line has been and where the
lead is going. The line can move while the lead holds, hold while the lead
moves, rest, and slip in short passing notes mid-syllable.
"""
from dataclasses import dataclass

import numpy as np
import pretty_midi

from . import core
from . import harmony as H


@dataclass
class Feel:
    """Knobs for the character of the line. 0..1 unless noted."""
    side: str = 'up'          # 'up' = above the lead, 'down' = below
    movement: float = 0.5     # 0 = mostly steps, 1 = more leaps
    contrary: float = 0.6     # pull toward moving opposite the lead
    split: float = 0.5        # chance to walk notes over a held lead note
    hold: float = 0.15        # chance to sustain while the lead moves
    rest: float = 0.08        # chance to drop out for a lead note
    spice: float = 0.25       # weight of passing tones mid-note
    focus: float = 1.6        # >1 sharpens the draw toward the best-scoring notes
    lo: int = 2               # nearest allowed distance from the lead (semis)
    hi: int = 10              # farthest allowed distance from the lead
    split_min: float = 0.45   # lead notes shorter than this never split


def events(notes, key, feel, rng):
    """Expand lead notes into harmony slots (sub-divided where long)."""
    evs = []
    for gi, n in enumerate(notes):
        src = key.snap(n['pitch'])
        dur = n['end'] - n['start']
        rest = rng.random() < feel.rest and gi > 0
        k = 1
        if dur >= feel.split_min and rng.random() < feel.split:
            k = 2 if dur < 0.9 or rng.random() < 0.6 else 3
        # uneven divisions sound sung, even ones sound sequenced
        w = rng.dirichlet(np.full(k, 4.0)) if k > 1 else np.array([1.0])
        edges = n['start'] + np.concatenate([[0], np.cumsum(w)]) * dur
        for j in range(k):
            evs.append(dict(start=float(edges[j]), end=float(edges[j + 1]),
                            src=src, group=gi, onset=j == 0, rest=rest))
    return evs


def line(notes, key, feel=Feel(), chords=None, seed=0):
    """Choose a target note for every slot. Returns list of event dicts."""
    rng = np.random.default_rng(seed)
    evs = events(notes, key, feel, rng)
    sign = 1 if feel.side == 'up' else -1
    prev_t = prev_src = prev_ic = None
    for e in evs:
        if e['rest']:
            e['target'] = None
            continue
        src = e['src']
        chord = H.chord_at(chords or [], e['start'])
        lo, hi = (src + feel.lo, src + feel.hi) if sign > 0 else \
            (src - feel.hi, src - feel.lo)
        cands = [m for m in key.notes if lo <= m <= hi]

        # sustain: keep the previous pitch if it's still consonant here
        if prev_t is not None and e['onset'] and rng.random() < feel.hold \
                and lo <= prev_t <= hi and \
                (abs(prev_t - src) % 12) not in H.CLASH:
            e['target'] = prev_t
            prev_src, prev_ic = src, abs(prev_t - src) % 12
            continue

        ws = []
        for c in cands:
            ic = abs(c - src) % 12
            w = 1.0
            # harmonic rule: consonant on the syllable, passing tones only mid-note
            if ic in H.CLASH:
                if e['onset'] or ic == 6:
                    ws.append(0.0)
                    continue
                w *= 3.0 * feel.spice
            if ic == 0:
                w *= 0.05                     # don't double the lead
            if chord and e['onset']:
                w *= 2.5 if c % 12 in chord else 0.7
            if prev_t is not None:
                leap = abs(c - prev_t)
                if leap == 0:
                    w *= 0.6 if e['onset'] else 0.0   # sub-notes must move
                elif leap <= 2:
                    w *= 3.0 * (1 - 0.6 * feel.movement)
                elif leap <= 4:
                    w *= 1.4
                elif leap <= 7:
                    w *= 0.3 + 0.9 * feel.movement
                else:
                    w *= 0.05 + 0.3 * feel.movement
                # contrary / oblique / parallel motion against the lead
                ld = np.sign(src - prev_src) if prev_src is not None else 0
                hd = np.sign(c - prev_t)
                if ld != 0 and hd == -ld:
                    w *= 1 + 5.0 * feel.contrary
                elif ld != 0 and hd == ld:
                    w *= 1 - 0.75 * feel.contrary
                # no parallel 5ths / octaves into a new syllable
                if e['onset'] and prev_ic in (0, 7) and ic == prev_ic and hd != 0:
                    w *= 0.15
            ws.append(w)
        ws = np.array(ws) ** feel.focus
        if ws.sum() == 0:
            t = H._safe_step(src, key, 2 * sign)
        else:
            t = int(rng.choice(cands, p=ws / ws.sum()))
        e['target'] = t
        prev_t, prev_src, prev_ic = t, src, abs(t - src) % 12
    return evs


def to_segments(evs, preroll=0.04, release=0.06):
    """Render segments: (start, end, interval, group). Pre-roll only on
    syllable onsets so mid-note pitch moves don't smear the consonant."""
    segs = []
    for e in evs:
        if e['target'] is None:
            continue
        iv = int(e['target'] - e['src'])
        s = e['start'] - (preroll if e['onset'] else 0.0)
        en = e['end'] + release
        if segs and segs[-1][2] == iv and s - segs[-1][1] < 0.12:
            segs[-1][1] = en
            continue
        if segs and s < segs[-1][1]:
            mid = (s + segs[-1][1]) / 2
            segs[-1][1], s = mid, mid
        segs.append([max(s, 0.0), en, iv, e['group']])
    return [tuple(x) for x in segs]


def splice_groups(bank, n_samples, sr, segs, rng, base_ms=22, spread_ms=14,
                  xfade=0.05, level_jitter_db=1.2):
    """Humanized splice where timing drifts per lead syllable, not per
    segment, so pitch moves inside one held note stay phase-continuous."""
    out = np.zeros(n_samples, dtype='float32')
    fl = int(xfade * sr)
    off, gain, last_g = base_ms, 1.0, None
    for s, e, iv, g in segs:
        if g != last_g:
            off = float(np.clip(off + rng.normal(0, spread_ms / 2),
                                base_ms - spread_ms, base_ms + spread_ms))
            gain = 10 ** (rng.normal(0, level_jitter_db) / 20)
            last_g = g
        d = int(off / 1000 * sr)
        a = max(int(s * sr) - fl // 2, 0)
        b = min(int(e * sr) + fl // 2, n_samples)
        if b <= a:
            continue
        seg = bank[iv][a:b] * gain
        f = min(fl, len(seg) // 2)
        if f > 0:
            ramp = np.sin(np.linspace(0, np.pi / 2, f)) ** 2
            seg[:f] *= ramp
            seg[-f:] *= ramp[::-1]
        a2, b2 = a + d, min(b + d, n_samples)
        out[a2:b2] += seg[:b2 - a2]
    return out


def write_midi(path, lines, lead_evs=None, offset=0.0, bpm=80.0):
    """One MIDI track per harmony line (+ the lead's notes for reference).
    offset = where this audio starts in the song, so the MIDI lines up when
    placed at bar 1 of the project."""
    pm = pretty_midi.PrettyMIDI(initial_tempo=bpm)
    if lead_evs:
        inst = pretty_midi.Instrument(program=0, name='Lead (reference)')
        seen = set()
        for e in lead_evs:
            if e['group'] in seen:
                continue
            seen.add(e['group'])
            grp = [x for x in lead_evs if x['group'] == e['group']]
            inst.notes.append(pretty_midi.Note(70, e['src'], e['start'] + offset,
                                               grp[-1]['end'] + offset))
        pm.instruments.append(inst)
    for name, evs in lines.items():
        inst = pretty_midi.Instrument(program=52, name=name)
        for e in evs:
            if e['target'] is None:
                continue
            vel = 96 if e['onset'] else 80
            inst.notes.append(pretty_midi.Note(vel, int(e['target']),
                                               e['start'] + offset,
                                               e['end'] + offset))
        pm.instruments.append(inst)
    pm.write(path)
