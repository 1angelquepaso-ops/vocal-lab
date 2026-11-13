"""Chord versions (simple / as played / rich) and the second-melody layer.

Everything here works in beats on a 1/16 grid. Notes are dicts
{start, end, pitch, vel}.
"""
from __future__ import annotations

import numpy as np

from .analysis import QUALITIES, key_scale

STEP = 0.25            # one 16th in beats


# ------------------------------------------------------------------ helpers
def chord_at(chords, t):
    for c in chords:
        if c["start"] <= t < c["end"]:
            return c
    return chords[-1] if chords else None


def split_bass(notes, max_bass=52):
    """Bass = the lowest note of an onset group when it sits clearly under the rest."""
    bass, upper = [], []
    by_on = {}
    for n in notes:
        by_on.setdefault(n["start"], []).append(n)
    for t, group in by_on.items():
        group = sorted(group, key=lambda n: n["pitch"])
        low = group[0]
        gap = group[1]["pitch"] - low["pitch"] if len(group) > 1 else 99
        held_below = any(m["start"] < t < m["end"] - 0.25 and m["pitch"] < low["pitch"] for m in notes)
        if not held_below and low["pitch"] <= max_bass and (gap >= 5 or low["pitch"] < 45):
            bass.append(low)
            upper.extend(group[1:])
        else:
            upper.extend(group)
    return sorted(bass, key=lambda n: n["start"]), sorted(upper, key=lambda n: (n["start"], n["pitch"]))


def attack_points(notes, chords):
    """Where the original re-strikes harmony: onsets with 2+ notes, bass hits, chord changes."""
    cnt = {}
    for n in notes:
        cnt[n["start"]] = cnt.get(n["start"], 0) + (2 if n["pitch"] < 52 else 1)
    pts = {t for t, c in cnt.items() if c >= 2}
    pts |= {c["start"] for c in chords if c["root"] is not None}
    end = max([c["end"] for c in chords], default=0)
    return sorted(p for p in pts if 0 <= p < end)


def top_line(notes):
    """Highest note per onset, made monophonic."""
    by = {}
    for n in notes:
        if n["start"] not in by or n["pitch"] > by[n["start"]]["pitch"]:
            by[n["start"]] = n
    line = sorted(by.values(), key=lambda n: n["start"])
    out = []
    for i, n in enumerate(line):
        e = n["end"]
        if i + 1 < len(line):
            e = min(e, line[i + 1]["start"])
        if e > n["start"]:
            out.append(dict(n, end=e))
    return out


# ------------------------------------------------------------------ voicing
def rich_quality(q, root, scale, tonic, mode):
    """Pick a lusher extension that still belongs to the key."""
    def fits(qq):
        return all(((root + i) % 12) in scale or i in (0, 4, 7, 10, 11, 3)
                   for i in QUALITIES[qq])
    dominant = root == (tonic + 7) % 12
    opts = {
        "": ["maj9", "add9", "6/9", "maj7"] if not dominant else ["9", "13", "7"],
        "m": ["m9", "m11", "madd9", "m7"],
        "5": ["add9", "madd9", "sus2"],
        "sus2": ["add9", "7sus4", "sus2"],
        "sus4": ["7sus4", "11", "sus4"],
        "7": ["13", "9", "7"] if not (mode == "minor" and dominant) else ["7b9", "7", "9"],
        "maj7": ["maj9", "maj13", "maj7"],
        "m7": ["m9", "m11", "m7"],
        "6": ["6/9", "maj9", "6"],
        "m6": ["m6", "m9"],
        "dim": ["m7b5", "dim7"],
        "m7b5": ["m7b5"],
        "add9": ["maj9", "6/9", "add9"],
        "madd9": ["m9", "m11", "madd9"],
    }.get(q, [q])
    for qq in opts:
        if fits(qq):
            return qq
    return q


def _closest_voicing(pcs, prev, lo, hi, size):
    """Choose pitches for the pcs in [lo,hi] minimising motion from prev."""
    best = None
    pcs = list(pcs)
    for base in range(lo, lo + 12):
        # build ascending stack starting near base
        for rot in range(len(pcs)):
            order = pcs[rot:] + pcs[:rot]
            v, cur = [], base - 1
            for pc in order:
                p = cur + 1 + ((pc - (cur + 1)) % 12)
                v.append(p)
                cur = p
            if v[-1] > hi or v[0] < lo:
                continue
            v = v[:size]
            if prev:
                cost = sum(min(abs(a - b) for b in prev) for a in v) + abs(np.mean(v) - np.mean(prev)) * 0.5
            else:
                cost = abs(np.mean(v) - (lo + hi) / 2)
            if best is None or cost < best[0]:
                best = (cost, v)
    if best is None:
        return sorted(lo + ((pc - lo) % 12) for pc in pcs)[:size]
    return best[1]


def _is_ghost(n, notes):
    """A note an octave / octave+5th / two octaves above a louder note struck at
    the same moment may be a transcription overtone (soft penalty only: real
    melodies double chord tones at the octave all the time)."""
    for m in notes:
        if m is n:
            continue
        d = n["pitch"] - m["pitch"]
        if d in (12, 19, 24) and abs(m["start"] - n["start"]) <= 0.13 and m["vel"] > n["vel"]:
            return True
    return False


LEAD = dict(pitch=0.9, vel=0.3, ghost=0.6, low=0.35, leap=0.08, big=0.8, skip=0.9, under=0.6)


def lead_line(notes, prm=None):
    """The sample's melody as one monophonic line.

    Every onset is a slot; at each slot we either take one of the notes that
    start there or skip the slot (accompaniment only). A Viterbi path over the
    last lead pitch scores height, loudness, smooth motion and overtone ghosts,
    so the line follows the voice that actually sings instead of whatever note
    is highest.
    """
    if not notes:
        return []
    P = dict(LEAD, **(prm or {}))
    by = {}
    for n in notes:
        by.setdefault(n["start"], []).append(n)
    slots = sorted(by)
    tops = np.array([max(n["pitch"] for n in by[t]) for t in slots])
    ref = float(np.percentile(tops, 60))
    vmean = float(np.mean([n["vel"] for n in notes]))
    ghosts = {id(n) for n in notes if _is_ghost(n, notes)}

    def emit(n):
        c = -P["pitch"] * (n["pitch"] - ref) / 12 - P["vel"] * (n["vel"] - vmean) / 25
        if id(n) in ghosts:
            c += P["ghost"]
        low = ref - 10 - n["pitch"]
        if low > 0:
            c += P["low"] * low
        return c

    def trans(p0, p):
        if p0 is None:
            return 0.0
        leap = abs(p - p0)
        return P["leap"] * max(0, leap - 4) + (P["big"] if leap > 12 else 0.0)

    SKIP = P["skip"]
    dp = {None: (0.0, -1e9)}          # last lead pitch -> (cost, that note's end)
    back = []                         # per slot: new_state -> (prev_state, note or None)
    for t in slots:
        nd, bk = {}, {}
        for s, (c, e0) in dp.items():  # skip this slot
            v = c + SKIP
            if s not in nd or v < nd[s][0]:
                nd[s], bk[s] = (v, e0), (s, None)
        for n in by[t]:
            e = emit(n)
            p = n["pitch"]
            best = None
            for s, (c, e0) in dp.items():
                v = c + trans(s, p)
                # struck under a melody note that is still ringing = accompaniment
                if s is not None and e0 > t + 0.1 and p <= s:
                    v += P["under"]
                if best is None or v < best[0]:
                    best = (v, s)
            v = best[0] + e
            if p not in nd or v < nd[p][0]:
                nd[p], bk[p] = (v, n["end"]), (best[1], n)
        # keep the search small
        dp = dict(sorted(nd.items(), key=lambda kv: kv[1][0])[:24])
        back.append({k: bk[k] for k in dp})
    state = min(dp, key=lambda k: dp[k][0])
    picked = []
    for bk in reversed(back):
        prev, n = bk[state]
        if n is not None:
            picked.append(n)
        state = prev
    line = sorted(picked, key=lambda n: n["start"])
    out = []
    for i, n in enumerate(line):
        e = n["end"]
        if i + 1 < len(line):
            nxt = line[i + 1]["start"]
            # legato: transcription ends notes as the piano decays, so hold each
            # line note into the next one unless there is a real rest (> 1.5 beats)
            e = nxt if 0 < nxt - e <= 1.5 else min(e, nxt)
        if e > n["start"]:
            out.append(dict(start=n["start"], end=e, pitch=int(n["pitch"]), vel=int(n["vel"])))
    return out


def _bass_pitch(pc):
    bp = 36 + ((pc - 36) % 12)
    return bp - 12 if bp > 45 else bp


def simple_line(an):
    """Simple: the melody as a single-note line + a plain root bass."""
    lead = lead_line(an["notes"])
    bass = []
    for c in an["chords"]:
        if c["root"] is None:
            continue
        bpc = c["bass"] if c["bass"] is not None else c["root"]
        bass.append(dict(start=c["start"], end=c["end"] - 0.05, pitch=_bass_pitch(bpc), vel=86))
    return lead, bass


# voice priority under a melody note: 3rd, 7th, 9th, 13th/11th, 5th
_COLOUR = {3: 0, 4: 0, 10: 1, 11: 1, 2: 2, 9: 3, 5: 3, 6: 3, 1: 4, 8: 4, 7: 5}


def _rich_quality_for(c, mpc, scale, tonic, mode):
    """Lush quality for chord c that agrees with the melody pitch class."""
    r = c["root"]
    base = rich_quality(c["quality"], r, scale, tonic, mode)
    if mpc is None:
        return base
    want = (mpc - r) % 12
    if want in QUALITIES[base] or want == 0:
        return base
    # melody sits on a tension: pick the richest in-key quality that contains it
    third = 3 if 3 in QUALITIES.get(c["quality"] or "", ()) or (c["quality"] or "").startswith("m") else 4
    for qq in sorted(QUALITIES, key=lambda q: -len(QUALITIES[q])):
        iv = QUALITIES[qq]
        if want in iv and third in iv and all(((r + i) % 12) in scale for i in iv):
            return qq
    return base


def harmonize(an):
    """Rich: the sample's melody on top, lush in-key voicings underneath, left
    hand root + 10th. Chords re-strike where the original re-strikes, at chord
    changes, and under long melody notes on the beat."""
    chords = [c for c in an["chords"] if c["root"] is not None]
    if not chords:
        return [], []
    tonic, mode = an["tonic"], an["mode"]
    scale = key_scale(tonic, mode)
    lead = lead_line(an["notes"])
    pts = set(attack_points(an["notes"], chords))
    for n in lead:
        if n["start"] % 1.0 == 0 and n["end"] - n["start"] >= 0.99:
            pts.add(n["start"])
    upper, bass = [], []
    prev = None
    for c in chords:
        r = c["root"]
        hits = sorted(p for p in pts if c["start"] <= p < c["end"])
        if not hits or hits[0] > c["start"]:
            hits = [c["start"]] + hits
        for i, h in enumerate(hits):
            e = hits[i + 1] if i + 1 < len(hits) else c["end"]
            m = next((n for n in lead if n["start"] <= h < n["end"]), None) \
                or next((n for n in lead if h < n["start"] < min(e, h + 0.5)), None)
            mp = m["pitch"] if m else None
            mpc = mp % 12 if m else None
            q = _rich_quality_for(c, mpc, scale, tonic, mode)
            iv = [i2 for i2 in QUALITIES[q] if i2 != 0]
            pcs = []
            for i2 in sorted(iv, key=lambda i2: _COLOUR.get(i2, 6)):
                pc = (r + i2) % 12
                if mpc is not None and (pc == mpc or (mpc - pc) % 12 == 1):
                    continue          # no doubling, no half-step rub under the tune
                pcs.append(pc)
            pcs = pcs[:4]
            # keep it lush: at least 3 voices under the tune (root, then in-key 9th/11th/5th)
            for extra in (0, 7, 2, 5, 9):
                if len(pcs) >= 3:
                    break
                pc = (r + extra) % 12
                if pc in pcs or pc not in scale and extra:
                    continue
                if mpc is not None and (pc == mpc or (mpc - pc) % 12 == 1):
                    continue
                pcs.append(pc)
            pcs = pcs or [(r + 7) % 12]
            hi = (mp - 1) if mp is not None else 74
            lo = max(50, hi - 17)
            v = _closest_voicing(pcs, prev, lo, hi, len(pcs))
            if mp is not None:
                v = [p for p in v if p < mp and (mp - p) % 12 != 1]
            # no half-step clusters inside the voicing: drop the lower note an octave
            spread = []
            for p in sorted(v):
                if spread and p - spread[-1] == 1:
                    low = spread.pop() - 12
                    if low >= 48 and all(abs(low - q) > 1 for q in spread):
                        spread.append(low)
                        spread.sort()
                spread.append(p)
            v = spread
            if not v:
                continue
            prev = v
            for p in v:
                upper.append(dict(start=h, end=e - 0.05, pitch=int(p), vel=76 if h == c["start"] else 66))
        # left hand: root (or slash bass) + a warm 10th / 7th
        bpc = c["bass"] if c["bass"] is not None else r
        bp = _bass_pitch(bpc)
        bass.append(dict(start=c["start"], end=c["end"] - 0.05, pitch=int(bp), vel=86))
        qc = rich_quality(c["quality"], r, scale, tonic, mode)
        span = [n["pitch"] for n in upper if c["start"] <= n["start"] < c["end"]]
        ceiling = min(62, (min(span) if span else 64) - 3)
        third = 3 if 3 in QUALITIES[qc] else 4
        seventh = 10 if 10 in QUALITIES[qc] else 11 if 11 in QUALITIES[qc] else None
        for iv in [third] + ([seventh] if seventh else []) + [7]:
            lp = bp + 7 + (((r + iv) - (bp + 7)) % 12)     # 10th / 7th / 5th above the bass
            if lp <= ceiling:
                bass.append(dict(start=c["start"], end=c["end"] - 0.05, pitch=int(lp), vel=68))
                break
    # when the tune moves over a held chord, release any voice it would rub
    # against (half-step / b9 below it) or cross above
    clean = []
    for v in upper:
        cut = v["end"]
        for n in lead:
            if n["start"] < v["start"] or n["start"] >= cut or n["start"] >= v["end"]:
                continue
            d = n["pitch"] - v["pitch"]
            if d <= 0 or d % 12 == 1:
                cut = n["start"]
        if cut - v["start"] >= 0.2:
            clean.append(dict(v, end=min(v["end"], cut - (0.02 if cut < v["end"] else 0))))
    upper = clean
    # the melody itself rides on top, a little louder
    upper +=[dict(n, vel=int(np.clip(n["vel"] + 6, 70, 118))) for n in lead]
    upper.sort(key=lambda n: (n["start"], n["pitch"]))
    return upper, bass


def chord_versions(an, detail):
    notes = an["notes"]
    if detail == "played":
        bass, upper = split_bass(notes)
        return upper, bass
    if detail == "simple":
        return simple_line(an)
    return harmonize(an)


# ------------------------------------------------------------------ melody layer
REGISTERS = {"low": (55, 72), "mid": (64, 81), "high": (72, 91)}


def melody_range(notes, chord_notes, register):
    """Registers sit relative to the top of the chords so the line stays audible."""
    src = chord_notes or notes
    tops = {}
    for n in src:
        tops[n["start"]] = max(tops.get(n["start"], 0), n["pitch"])
    top = int(np.median(list(tops.values()))) if tops else 72
    lo, hi = {"low": (top - 9, top + 5), "mid": (top + 1, top + 15),
              "high": (top + 7, top + 22)}.get(register, (top + 1, top + 15))
    shift = max(0, hi - 96) - max(0, 52 - lo)
    return lo - shift, hi - shift


def _rhythm(rng, density, bar_len=4.0, avoid=None):
    """One bar of onsets on the 16th grid; strong positions favoured."""
    w = np.array([1.0, .25, .5, .25, .8, .25, .5, .3, .9, .25, .5, .25, .8, .3, .5, .35]) * 1.0
    if avoid is not None:
        w = w * np.where(avoid, 0.25, 1.0)
    n = int(np.clip(round(1.5 + density * 7 + rng.normal(0, 0.7)), 1, 14))
    pos = np.sort(rng.choice(16, size=min(n, 16), replace=False, p=w / w.sum()))
    return [p * STEP for p in pos]


def _durations(onsets, bar_len, legato=0.92):
    out = []
    for i, o in enumerate(onsets):
        nxt = onsets[i + 1] if i + 1 < len(onsets) else bar_len
        out.append(max(STEP, (nxt - o) * legato))
    return out


def _scale_pitches(scale, lo, hi):
    return [p for p in range(lo, hi + 1) if p % 12 in scale]


def melody(an, mode="complement", density=0.5, register="mid", seed=0, chord_notes=None,
           arp_pattern="up", arp_rate=0.25, arp_octaves=2):
    """Generate the secondary line. chord_notes = current chord version (for arp tones)."""
    rng = np.random.default_rng(seed)
    chords = [c for c in an["chords"] if c["root"] is not None]
    if not chords:
        return []
    tonic, md = an["tonic"], an["mode"]
    scale = key_scale(tonic, md)
    lo, hi = melody_range(an["notes"], chord_notes, register)
    end = max(c["end"] for c in chords)
    start = min(c["start"] for c in chords)
    ref = top_line(an["notes"])

    if mode == "arp":
        return _arp(an, chords, chord_notes, arp_pattern, arp_rate, arp_octaves, lo, hi, rng, density)
    if mode == "amplify":
        return _amplify(ref, chords, scale, lo, hi, rng, density)

    pool = _scale_pitches(scale, lo, hi)
    # motif: 2 rhythm cells reused in an A A' B A'' pattern over 4 bars
    bar0 = int(np.floor(start / 4))
    n_bars = int(np.ceil(end / 4)) - bar0
    cells = {}
    out = []
    prev = None
    ref_on = np.zeros(int(np.ceil(end / STEP)) + 16, bool)
    for n in ref:
        k = int(round(n["start"] / STEP))
        if 0 <= k < len(ref_on):
            ref_on[k] = True
    phrase_dir = 1
    for b in range(n_bars):
        bar = bar0 + b
        t0 = bar * 4.0
        slot = b % 4
        key = {0: "A", 1: "A", 2: "B", 3: "A"}[slot] if mode != "random" else f"R{b}"
        avoid = None
        if mode in ("complement",):
            k0 = int(round(t0 / STEP))
            avoid = ref_on[k0:k0 + 16] if k0 + 16 <= len(ref_on) else None
            if avoid is not None and len(avoid) < 16:
                avoid = None
        if key not in cells or (mode == "complement" and avoid is not None and rng.random() < 0.35):
            cells[key] = _rhythm(rng, density if mode != "random" else rng.uniform(0.2, 0.9), avoid=avoid)
        on = list(cells[key])
        if slot == 3 and len(on) > 2 and mode != "random":
            on = on[:-1]  # breathe at the phrase end
        durs = _durations(on, 4.0, legato=0.9 if mode != "random" else rng.uniform(0.5, 0.95))
        if slot == 0:
            phrase_dir = rng.choice([1, -1])
        for i, (o, d) in enumerate(zip(on, durs)):
            t = t0 + o
            if t < start or t >= end:
                continue
            c = chord_at(chords, t)
            ct = set(c["pcs"])
            strong = (o % 1.0) == 0
            ref_note = next((n for n in ref if n["start"] <= t < n["end"]), None)
            sounding = [n["pitch"] for n in (chord_notes or []) if n["start"] <= t < n["end"]]
            ref_prev = next((n for n in reversed(ref) if n["start"] < t), None)
            best, cand = None, []
            for p in pool:
                s = 0.0
                if p % 12 in ct:
                    s += 2.0 if strong else 0.8
                elif strong:
                    s -= 1.5
                if prev is not None:
                    leap = abs(p - prev)
                    if mode == "random":
                        s -= 0.08 * leap
                    else:
                        s -= 0.35 * max(0, leap - 2) + (0.6 if leap == 0 else 0)
                        s += 0.4 * (1 if np.sign(p - prev) == phrase_dir else 0)
                else:
                    s -= abs(p - (lo + hi) / 2) * 0.1
                # a half-step (or b9) above a ringing chord tone is the harsh one
                rub = sum(1 for q in sounding if (p - q) % 12 == 1 and 0 < p - q <= 25)
                s -= (2.5 if strong else 1.2) * rub
                if ref_note is not None:
                    iv = abs(p - ref_note["pitch"]) % 12
                    if iv in (1, 2, 6, 11):
                        s -= 2.0 if strong else 1.0
                    if mode == "counter":
                        if iv in (3, 4, 8, 9):
                            s += 1.0
                        if ref_prev is not None and prev is not None:
                            rd = np.sign(ref_note["pitch"] - ref_prev["pitch"])
                            if rd and np.sign(p - prev) == -rd:
                                s += 1.2
                        if abs(p - ref_note["pitch"]) < 3:
                            s -= 1.0
                if mode == "random":
                    s += rng.normal(0, 1.2)
                else:
                    s += rng.normal(0, 0.35)
                cand.append((s, p))
            cand.sort(reverse=True)
            p = cand[0][1]
            vel = int(np.clip(84 + (8 if strong else -4) + rng.normal(0, 5), 50, 110))
            out.append(dict(start=t, end=t + d, pitch=int(p), vel=vel))
            prev = p
        if rng.random() < 0.3:
            phrase_dir *= -1
    return out


def _amplify(ref, chords, scale, lo, hi, rng, density):
    """Shadow the top line a diatonic 3rd/6th above (or below), with octave lifts."""
    out = []
    sp = sorted(p for p in range(24, 108) if p % 12 in scale)
    above = rng.random() < 0.7
    for n in ref:
        c = chord_at(chords, n["start"])
        ct = set(c["pcs"]) if c else set()
        base = n["pitch"]
        cands = []
        for steps in ((2, 5) if above else (-2, -5)):
            i = min(range(len(sp)), key=lambda k: abs(sp[k] - base))
            j = i + steps
            if 0 <= j < len(sp):
                cands.append(sp[j])
        cands.sort(key=lambda p: (0 if p % 12 in ct else 1, abs(p - base)))
        p = cands[0] if cands else base + 12
        while p < lo:
            p += 12
        while p > hi + 5:
            p -= 12
        out.append(dict(start=n["start"], end=n["end"], pitch=int(p), vel=int(n["vel"] * 0.9)))
        # sparkle: occasional grace pickup into strong notes when busy
        if density > 0.6 and n["start"] % 1.0 == 0 and rng.random() < density - 0.5 and n["start"] >= STEP:
            g = sp[max(0, sp.index(p) - 1)] if p in sp else p - 1
            out.append(dict(start=n["start"] - STEP, end=n["start"], pitch=int(g), vel=60))
    return sorted(out, key=lambda n: n["start"])


def _arp(an, chords, chord_notes, pattern, rate, octaves, lo, hi, rng, density):
    out = []
    t_end = max(c["end"] for c in chords)
    idx = 0
    for c in chords:
        if chord_notes:
            src = sorted({n["pitch"] for n in chord_notes if n["start"] < c["end"] and n["end"] > c["start"]
                          and n["pitch"] >= 48})
            pcs = sorted({p % 12 for p in src}) or c["pcs"]
        else:
            pcs = c["pcs"]
        base = lo - 5
        tones = sorted({base + ((pc - base) % 12) + 12 * o for pc in pcs for o in range(octaves)})
        tones = [p for p in tones if p <= hi + 7]
        if not tones:
            continue
        if pattern == "down":
            seq = tones[::-1]
        elif pattern == "updown":
            seq = tones + tones[-2:0:-1]
        elif pattern == "converge":
            seq, a, b = [], 0, len(tones) - 1
            while a <= b:
                seq.append(tones[a])
                if a != b:
                    seq.append(tones[b])
                a, b = a + 1, b - 1
        else:
            seq = list(tones)
        t = c["start"]
        k = 0
        while t < c["end"] - 1e-6 and t < t_end:
            if pattern == "random":
                p = int(rng.choice(tones))
            else:
                p = seq[k % len(seq)]
            beat_pos = (t % 1.0)
            acc = 10 if abs(beat_pos) < 1e-6 else 0
            gate = rate * (0.55 + 0.4 * density)
            out.append(dict(start=round(t, 4), end=round(t + gate, 4), pitch=int(p),
                            vel=int(np.clip(72 + acc + rng.normal(0, 4), 40, 115))))
            t += rate
            k += 1
            idx += 1
    return out
