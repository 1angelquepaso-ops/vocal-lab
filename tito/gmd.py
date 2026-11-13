"""Groove MIDI Dataset access (Roland TD-11 note map -> our pads)."""
from __future__ import annotations

import csv
import io
import os
import zipfile

import numpy as np

GMD_MAP = {
    36: "kick", 38: "snare", 40: "snare", 37: "rim",
    48: "tom_hi", 50: "tom_hi", 45: "tom_mid", 47: "tom_mid", 43: "tom_lo", 58: "tom_lo",
    46: "ohh", 26: "ohh", 42: "chh", 22: "chh", 44: "phh",
    49: "crash", 55: "crash", 57: "crash", 52: "crash",
    51: "ride", 59: "ride", 53: "ride",
}


def zip_path():
    here = os.path.dirname(os.path.abspath(__file__))
    import sys
    cands = [os.path.join(here, "data", "groove-v1.0.0-midionly.zip")]
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands.append(os.path.join(base, "tito", "data", "groove-v1.0.0-midionly.zip"))
    for c in cands:
        if os.path.exists(c):
            return c
    return None


def index():
    zp = zip_path()
    if not zp:
        return []
    with zipfile.ZipFile(zp) as z:
        rows = list(csv.DictReader(io.TextIOWrapper(z.open("groove/info.csv"), "utf-8")))
    return rows


def read(row_or_name):
    """-> (hits [(beat, key, vel)], bpm). Beats from the file's own tempo."""
    import mido
    name = row_or_name["midi_filename"] if isinstance(row_or_name, dict) else row_or_name
    with zipfile.ZipFile(zip_path()) as z:
        data = z.read("groove/" + name)
    mid = mido.MidiFile(file=io.BytesIO(data))
    tpb = mid.ticks_per_beat
    tempo = 500000
    hits = []
    for tr in mid.tracks:
        t = 0
        for m in tr:
            t += m.time
            if m.type == "set_tempo":
                tempo = m.tempo
            if m.type == "note_on" and m.velocity > 0 and m.note in GMD_MAP:
                hits.append((t / tpb, GMD_MAP[m.note], m.velocity))
    hits.sort()
    return hits, 60e6 / tempo
