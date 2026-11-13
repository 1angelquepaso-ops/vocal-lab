"""Song MIDI, groove layers, fills, drummer feel.

All positions are in beats from bar 1 of the song. Notes = {beat, pad, vel, len}.
"""
from __future__ import annotations

import numpy as np

from . import library
from .common import PAD_INDEX
from .styles import STYLES

P = PAD_INDEX
GRIDS = {"1/8": 0.5, "1/16": 0.25, "1/32": 0.125, "1/8T": 1 / 3, "1/16T": 1 / 6, "1/32T": 1 / 12}


def N(beat, pad, vel, ln=0.12):
    return dict(beat=float(beat), pad=int(pad if isinstance(pad, int) else P[pad]),
                vel=int(np.clip(round(vel), 1, 127)), len=ln)


# ------------------------------------------------------------------ feel
def humanize(notes, sid, human, swing, rng):
    """Real-drummer feel learned from the style's recordings: how early/late each
    16th is played (and how much it wanders), plus swing on the off-16ths."""
    if not notes or (human <= 0 and swing <= 0):
        return notes
    feel = library.style_feel(sid)
    off, spr = feel["offset"], feel["spread"]
    out = []
    for n in notes:
        q = n["beat"] * 4
        on_grid = abs(q - round(q)) < 0.08
        slot = int(round(q)) % 16
        shift = 0.0
        if on_grid:
            shift += human * (off[slot] + 0.6 * spr[slot] * rng.normal()) * 0.25
            if slot % 2 == 1:
                shift += swing * 0.0833                 # 100% swing = triplet feel
        else:
            shift += human * 0.02 * rng.normal() * 0.25
        vel = n["vel"] * (1 + human * 0.1 * rng.normal())
        out.append(dict(n, beat=max(0.0, n["beat"] + shift), vel=int(np.clip(round(vel), 1, 127))))
    return out


def quantize(notes, grid, strength=1.0):
    if grid == "auto":
        out = []
        for n in notes:
            b = n["beat"]
            c16 = round(b * 4) / 4
            c12 = round(b * 6) / 6
            tgt = c16 if abs(b - c16) <= abs(b - c12) + 0.01 else c12
            out.append(dict(n, beat=b + strength * (tgt - b)))
        return out
    st = GRIDS.get(grid, 0.25)
    return [dict(n, beat=n["beat"] + strength * (round(n["beat"] / st) * st - n["beat"])) for n in notes]


# ------------------------------------------------------------------ song
def song_notes(an, feel="keep", qgrid="1/16"):
    notes = [N(h["beat"], h["pad"], h["vel"]) for h in an["hits"] if h["beat"] >= -0.01]
    if feel == "quantize":
        notes = quantize(notes, qgrid)
    return sorted(notes, key=lambda n: (n["beat"], n["pad"]))


def active_bars(song, n_bars):
    cnt = np.zeros(n_bars + 1, int)
    for n in song:
        b = int(n["beat"] // 4)
        if 0 <= b <= n_bars:
            cnt[b] += 1
    return [b for b in range(n_bars) if cnt[b] >= 2]


def _occupied(song, pads, tol=0.06):
    s = {}
    for n in song:
        if n["pad"] in pads:
            s.setdefault(int(round(n["beat"] / 0.0625)), True)
    return lambda b: any(int(round(b / 0.0625)) + d in s for d in (-1, 0, 1))


def _pattern(row):
    steps = len(row) or 16
    return [(i * 4.0 / steps, ch) for i, ch in enumerate(row) if ch in "xo"]


# ------------------------------------------------------------------ groove layers
def layers(song, an, sid, which, grid, rng, sections=None):
    st = STYLES[sid]
    n_bars = an["bars"]
    bars = active_bars(song, n_bars)
    hook = {b for s in (sections or []) if s["label"] == "Hook" for b in range(s["start"], s["end"])}
    step = GRIDS.get(grid, 0.25)
    out = []
    snare_at = _occupied(song, {P["snare"], P["clap"], P["rim"]})
    hats_at = _occupied(song, {P["chh"], P["ohh"], P["phh"]})
    med = {}
    for n in song:
        med.setdefault(n["pad"], []).append(n["vel"])
    lvl = lambda pad, d=90: float(np.median(med[P[pad]])) if P[pad] in med else d
    for b in bars:
        b0 = b * 4.0
        lift = 1.0 if (not hook or b in hook) else 0.85       # a bit more in the hook
        if "hats" in which and (b % 2 == 1 or b in hook):
            # trap-style roll on the last beat (or 3rd) of the bar at the grid rate
            start = b0 + (3.0 if rng.random() < 0.7 else 2.0)
            length = 1.0 if step <= 0.125 else 0.5 + 0.5 * (rng.random() < 0.5)
            k = 0
            up = rng.random() < 0.6
            t = start + (1.0 - length)
            nsteps = max(2, int(round(length / step)))
            for k in range(nsteps):
                ramp = k / max(1, nsteps - 1)
                v = lvl("chh") * (0.55 + 0.5 * (ramp if up else 1 - ramp)) * lift
                out.append(N(t + k * step, "chh", v, ln=step * 0.8))
        if "ghost" in which and st.get("ghost"):
            for pos, ch in _pattern(st["ghost"]):
                t = b0 + pos
                if not snare_at(t):
                    out.append(N(t, "snare", lvl("snare") * (0.28 + 0.08 * rng.random()), ln=0.1))
        if "shaker" in which:
            acc = {round(p, 3) for p, ch in _pattern(st.get("shaker", "")) if ch == "x"}
            t = 0.0
            while t < 4.0 - 1e-6:
                strong = any(abs(t - a) < 1e-3 for a in acc)
                v = (78 if strong else 52) * lift + rng.normal(0, 3)
                out.append(N(b0 + t, "perc2", v, ln=step * 0.7))
                t += step
        if "perc" in which and st.get("perc"):
            for pos, ch in _pattern(st["perc"]):
                out.append(N(b0 + pos, "perc1", (96 if ch == "x" else 68) * lift + rng.normal(0, 4), ln=0.15))
        if "openhat" in which:
            for pos in (1.5, 3.5):
                t = b0 + pos
                if not hats_at(t) or rng.random() < 0.5:
                    out.append(N(t, "ohh", lvl("ohh", 85) * 0.9, ln=0.4))
    return out


# ------------------------------------------------------------------ fills
def _rate(intensity):
    return 0.5 if intensity < 0.3 else 0.25 if intensity < 0.75 else 0.125


def fam_toms_down(L, it, rng):
    r = _rate(it) if it < 0.9 else 0.25
    seq, out, t, k = ["tom_hi", "tom_hi", "tom_mid", "tom_mid", "tom_lo", "tom_lo"], [], 0.0, 0
    n = int(round(L / r))
    for i in range(n):
        pad = seq[min(len(seq) - 1, int(i * len(seq) / n))] if n > 2 else "snare"
        if i < n * 0.25 and L >= 2:
            pad = "snare"
        out.append(N(i * r, pad, 92 + 25 * (i % int(max(1, 1 / r)) == 0) + 10 * i / n))
        if it > 0.5 and abs((i * r) % 1.0) < 1e-6:
            out.append(N(i * r, "kick", 100))
    return out


def fam_snare_roll(L, it, rng, pad="snare"):
    out, r = [], _rate(it)
    n = int(round(L / r))
    for i in range(n):
        t = i * r
        if it > 0.8 and t >= L - 1:                     # last beat doubles up
            out.append(N(t, pad, 60 + 60 * i / n))
            out.append(N(t + r / 2, pad, 55 + 60 * i / n))
        else:
            out.append(N(t, pad, 55 + 65 * i / n))
    return out


def fam_trap_roll(L, it, rng):
    out = []
    half = L / 2
    r1 = 1 / 3 if it < 0.6 else 1 / 6
    t = 0.0
    while t < half - 1e-6:
        out.append(N(t, "clap" if rng.random() < 0.5 else "snare", 70 + 40 * t / L))
        t += r1
    r2 = 1 / 6 if it < 0.5 else 1 / 12
    t = half
    while t < L - 1e-6:
        out.append(N(t, "chh", 50 + 70 * (t - half) / max(1e-6, L - half), ln=r2 * 0.8))
        t += r2
    out.append(N(L - 0.5, "kick2", 110, ln=0.5))
    return out


def fam_drop(L, it, rng):
    out = [N(L - 0.25, "snare", 100)]
    if it > 0.4:
        out.append(N(L - 0.5, "kick2" if rng.random() < 0.6 else "kick", 105, ln=0.5))
    if it > 0.7:
        out.append(N(L - 0.125, "snare", 85))
    return out


def fam_chop(L, it, rng):
    out, r = [], 0.25
    for i in range(int(L / r)):
        if rng.random() < 0.35 + 0.5 * it:
            pad = "snare" if rng.random() < 0.6 else "kick"
            out.append(N(i * r, pad, 70 + 40 * rng.random()))
    return out


def fam_timbal(L, it, rng):
    out, r = [], 0.25 if it < 0.8 else 1 / 6
    n = int(round(L / r))
    for i in range(n):
        acc = (i * r) % 1.0 < 1e-6
        out.append(N(i * r, "perc1", (105 if acc else 72) + 15 * i / n))
        if acc and it > 0.5:
            out.append(N(i * r, "fx", 80))               # cowbell on the beats
    return out


def fam_abanico(L, it, rng):
    out = fam_timbal(max(0.5, L - 0.5), it * 0.6, rng)
    t = L - 0.5
    while t < L - 1e-6:                                    # the roll into the rimshot
        out.append(N(t, "perc1", 60 + 50 * (t - (L - 0.5)) / 0.5))
        t += 1 / 16
    return out


def fam_tarola(L, it, rng):
    out = fam_snare_roll(L, max(it, 0.85), rng)           # banda snare rolls are 32nds
    for b in range(int(L)):
        out.append(N(b, "kick", 100))                       # tambora on the beats
    return out


def fam_talking(L, it, rng):
    out, r = [], 0.25
    pat = [1, 0, 1, 1, 0, 1, 0, 1]
    for i in range(int(L / r)):
        if pat[i % len(pat)] or it > 0.8:
            out.append(N(i * r, "perc1" if i % 3 else "perc2", 75 + 30 * rng.random()))
    return out


def fam_log_roll(L, it, rng):
    out, r = [], 0.25
    for i in range(int(L / r)):
        if rng.random() < 0.4 + 0.4 * it:
            out.append(N(i * r, "tom_lo" if rng.random() < 0.6 else "tom_mid", 85 + 20 * rng.random(), ln=0.3))
    return out


def fam_blast(L, it, rng):
    out = []
    r = 0.125 if it > 0.5 else 0.25
    t = 0.0
    while t < L - 1e-6:
        out.append(N(t, "kick", 105))
        if abs((t / 0.5) - round(t / 0.5)) < 1e-6:
            out.append(N(t, "snare", 110))
        t += r
    tail = fam_toms_down(min(1.0, L), 0.7, rng)
    return [n for n in out if n["beat"] < L - 1] + [dict(n, beat=n["beat"] + L - min(1.0, L)) for n in tail]


def fam_build(L, it, rng):
    out, t = [], 0.0
    while t < L - 1e-6:
        frac = t / L
        r = 0.5 if frac < 0.4 else 0.25 if frac < 0.75 else 0.125
        out.append(N(t, "snare", 60 + 60 * frac))
        if it > 0.6 and frac >= 0.5:
            out.append(N(t, "clap", 50 + 60 * frac))
        t += r
    return out


def fam_kick_run(L, it, rng):
    out, r = [], 1 / 3
    t = 0.0
    while t < L - 1e-6:
        out.append(N(t, "kick", 100 + 15 * rng.random()))
        t += r
    out.append(N(L - 0.25, "clap", 110))
    return out


def fam_darbuka(L, it, rng):
    out, r = [], 0.25
    for i in range(int(L / r)):
        doum = i % 4 == 0
        out.append(N(i * r, "tom_lo" if doum else "rim", 105 if doum else 70 + 20 * rng.random()))
    if it > 0.6:
        t = L - 0.5
        while t < L:
            out.append(N(t, "rim", 70 + 40 * (t - L + 0.5) / 0.5))
            t += 1 / 12
    return out


FAMILIES = {"toms_down": fam_toms_down, "snare_roll": fam_snare_roll, "trap_roll": fam_trap_roll,
            "drop": fam_drop, "chop": fam_chop, "timbal": fam_timbal, "abanico": fam_abanico,
            "tarola": fam_tarola, "talking": fam_talking, "log_roll": fam_log_roll, "blast": fam_blast,
            "build": fam_build, "kick_run": fam_kick_run, "darbuka": fam_darbuka}


def _real_fill(sid, L, it, rng):
    fills = library.style_fills(sid)
    if not fills:
        return None
    cand = [f for f in fills if abs(f["len"] - L) <= max(1.0, 0.5 * L)] or fills
    dens = np.array([len(f["notes"]) / max(1.0, f["len"]) for f in cand])
    order = np.argsort(dens)
    pick = cand[int(order[int(np.clip(it * (len(order) - 1) + rng.normal(0, 1.5), 0, len(order) - 1))])]
    start = pick["len"] - L                                  # keep the END of the fill (where it lands)
    out = [N(b - start, pad, v) for b, pad, v in pick["notes"] if b - start >= -0.05]
    return out


def _song_fill(an, song, L, rng):
    bars = [f["bar"] for f in an.get("song_fills", [])]
    if not bars:
        return None
    b = bars[int(rng.integers(len(bars)))]
    end = (b + 1) * 4.0
    return [dict(n, beat=n["beat"] - (end - L)) for n in song if end - L <= n["beat"] < end]


def fill_spots(an, where):
    n_bars = an["bars"]
    spots = set()
    if "every8" in where:
        spots |= {b for b in range(8, n_bars + 1, 8)}
    if "every16" in where:
        spots |= {b for b in range(16, n_bars + 1, 16)}
    if "prehook" in where:
        spots |= {s["start"] for s in an.get("sections", []) if s["label"] == "Hook" and s["start"] > 0}
    if "sections" in where:
        spots |= {s["start"] for s in an.get("sections", []) if s["start"] > 0}
    return sorted(b for b in spots if 1 <= b <= n_bars)


def fills(an, song, sid, where, L, it, crash, rng):
    st = STYLES.get(sid, {})
    fams = st.get("fills", ["snare_roll"])
    out, spots = [], fill_spots(an, where)
    windows = []
    for b in spots:
        end = b * 4.0
        notes = None
        if sid == "song":
            notes = _song_fill(an, song, L, rng)
        if notes is None:
            fam = fams[int(rng.integers(len(fams)))]
            if fam == "gmd":
                notes = _real_fill(sid, L, it, rng)
            if notes is None and "gmd" in fams and rng.random() < 0.5:
                notes = _real_fill(sid, L, it, rng)
            if notes is None:
                fam = fam if fam in FAMILIES else next((f for f in fams if f in FAMILIES), "snare_roll")
                notes = FAMILIES[fam](L, it, rng)
        start = end - L
        notes = [dict(n, beat=n["beat"] + start) for n in notes if -0.05 <= n["beat"] < L]
        out += notes
        if crash and b < an["bars"] + 1:
            out.append(N(end, "crash", 112, ln=1.0))
            out.append(N(end, "kick", 110))
        windows.append((start, end))
    return out, windows


def make_room(song, windows):
    keep_pads = {P["crash"]}
    return [n for n in song if n["pad"] in keep_pads or not any(a - 0.02 <= n["beat"] < b - 0.02 for a, b in windows)]


# ------------------------------------------------------------------ build everything
def build(an, feel="keep", qgrid="1/16", style="auto", layer_set=(), grid="1/16", swing=0.0, human=0.6,
          fill_where=(), fill_len=4.0, intensity=0.6, crash=True, room=True, seed=1):
    from .styles import auto_style
    from .common import PADS
    rng = np.random.default_rng(seed)
    present = {PADS[p["pad"]][0] for p in an.get("pads", [])}
    sid = auto_style(an["bpm"], present) if style == "auto" else style
    song = song_notes(an, feel, qgrid)
    lay = layers(song, an, sid if sid in STYLES else "trap", set(layer_set), grid, rng, an.get("sections"))
    fl, windows = fills(an, song, sid, set(fill_where), float(fill_len), intensity, crash, rng) \
        if fill_where else ([], [])
    if room and windows:
        song = make_room(song, windows)
        lay = make_room(lay, windows)
    feel_sid = sid if sid in STYLES else "trap"
    lay = humanize(lay, feel_sid, human, swing, rng)
    fl = humanize(fl, feel_sid, human, swing, rng)
    key = lambda n: (n["beat"], n["pad"])
    return dict(style=sid, song=sorted(song, key=key), layers=sorted(lay, key=key), fills=sorted(fl, key=key),
                windows=windows)
