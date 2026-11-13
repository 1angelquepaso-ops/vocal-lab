"""Build / load the style library (styles.json) from the Groove MIDI Dataset.

From every GMD style we keep:
  * real fills (one per fill recording, mapped to our pads, landing crash removed)
  * the drummer FEEL: per 16th slot, how early/late real drummers play (mean + spread,
    in fractions of a 16th) and how hard (velocity per slot for kick / snare / hats).

    python -m tito.library          # rebuild tito/data/styles.json
"""
from __future__ import annotations

import json
import os
import sys

import numpy as np

from . import gmd
from .common import PAD_INDEX
from .styles import STYLES

FAMILY = {"kick": "kick", "kick2": "kick", "snare": "snare", "rim": "snare", "clap": "snare",
          "chh": "hat", "phh": "hat", "ohh": "hat", "ride": "hat", "crash": "hat",
          "tom_lo": "tom", "tom_mid": "tom", "tom_hi": "tom", "perc1": "perc", "perc2": "perc", "fx": "perc"}


def data_path():
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.path.join(here, "data", "styles.json")]
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands.append(os.path.join(base, "tito", "data", "styles.json"))
    for c in cands:
        if os.path.exists(c):
            return c
    return cands[0]


def _fill_from(hits):
    """Fill recording -> (length_beats, notes relative to the fill START, landing crash dropped)."""
    if not hits:
        return None
    last = max(b for b, _, _ in hits)
    length = float(np.ceil(last + 0.05))
    # a crash in the last ~third of a beat before the end lands on the next downbeat
    body = [(b, k, v) for b, k, v in hits if not (k == "crash" and b > length - 0.35)]
    if len(body) < 3:
        return None
    # if the last notes run past the bar line, the fill lands there
    notes = [[round(b, 4), PAD_INDEX[k], int(v)] for b, k, v in body]
    return dict(len=length, notes=notes)


def _feel(files):
    off = [[] for _ in range(16)]
    vel = {f: [[] for _ in range(16)] for f in ("kick", "snare", "hat")}
    for h, _ in files:
        for b, k, v in h:
            q = b * 4
            slot = int(round(q)) % 16
            off[slot].append(q - round(q))
            fam = FAMILY.get(k)
            if fam in vel:
                vel[fam][slot].append(v)
    allo = np.concatenate([np.array(o) for o in off if o]) if any(off) else np.zeros(1)
    bias = float(np.median(allo))                     # recording alignment, not feel
    mean = [round(float(np.clip(np.mean(o) - bias, -0.45, 0.45)), 3) if len(o) > 4 else 0.0 for o in off]
    sd = [round(float(np.clip(np.std(o), 0.0, 0.3)), 3) if len(o) > 4 else 0.08 for o in off]
    vel_out = {f: [round(float(np.mean(x)), 1) if len(x) > 2 else None for x in v] for f, v in vel.items()}
    return dict(offset=mean, spread=sd, vel=vel_out, n=int(sum(len(o) for o in off)))


def build(out=None):
    rows = gmd.index()
    if not rows:
        raise SystemExit("Groove MIDI Dataset zip not found (tito/data/groove-v1.0.0-midionly.zip)")
    by_style = {}
    for r in rows:
        if r["time_signature"] != "4-4":
            continue
        by_style.setdefault(r["style"], []).append(r)
    gmd_styles = {}
    for st, rs in sorted(by_style.items()):
        prim = st.split("/")[0]
        for key in {st, prim}:
            g = gmd_styles.setdefault(key, dict(fills=[], beats=[]))
            for r in rs:
                h, bpm = gmd.read(r)
                if r["beat_type"] == "fill":
                    f = _fill_from(h)
                    if f:
                        f["bpm"] = round(bpm, 1)
                        f["src"] = r["id"]
                        g["fills"].append(f)
                else:
                    g["beats"].append((h[:4000], bpm))
    lib = dict(source="Groove MIDI Dataset v1.0.0 (Google Magenta, CC BY 4.0) + hand-written styles",
               gmd={}, styles={})
    for key, g in gmd_styles.items():
        feel = _feel(g["beats"]) if g["beats"] else None
        # de-duplicate fills that appear under both the sub-style and the main style
        seen, fills = set(), []
        for f in g["fills"]:
            if f["src"] in seen:
                continue
            seen.add(f["src"])
            fills.append({k: v for k, v in f.items() if k != "src"})
        lib["gmd"][key] = dict(fills=fills, feel=feel)
    for sid, st in STYLES.items():
        lib["styles"][sid] = st
    out = out or data_path()
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        json.dump(lib, f, separators=(",", ":"))
    return lib


_LIB = None


def load():
    global _LIB
    if _LIB is None:
        p = data_path()
        if not os.path.exists(p):
            build(p)
        with open(p) as f:
            _LIB = json.load(f)
    return _LIB


def style_feel(sid):
    """Drummer feel for a style: learned from its GMD styles, else straight."""
    lib = load()
    st = lib["styles"].get(sid, {})
    for g in st.get("gmd", []):
        f = lib["gmd"].get(g, {}).get("feel")
        if f:
            return f
    return dict(offset=[0.0] * 16, spread=[0.05] * 16, vel=None, n=0)


def style_fills(sid):
    lib = load()
    out = []
    for g in lib["styles"].get(sid, {}).get("gmd", []):
        out += lib["gmd"].get(g, {}).get("fills", [])
    return out


if __name__ == "__main__":
    lib = build()
    n = sum(len(v["fills"]) for k, v in lib["gmd"].items() if "/" not in k)
    print("styles:", len(lib["styles"]), "| gmd styles:", len(lib["gmd"]), "| real fills:", n,
          "| size kB:", os.path.getsize(data_path()) // 1024)
