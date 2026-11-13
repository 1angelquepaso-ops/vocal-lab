"""Shared constants: the 16-pad map, MIDI writing, small helpers."""
from __future__ import annotations

import json
import os

import numpy as np

SR = 44100
BASE_NOTE = 36                       # pad 1 = MIDI note 36 ... pad 16 = 51

# index -> (key, display name, shuffle type)
PADS = [
    ("kick",   "Kick",       "kick"),
    ("kick2",  "Kick 2",     "kick"),
    ("snare",  "Snare",      "snare"),
    ("clap",   "Clap",       "clap"),
    ("rim",    "Rim",        "rim"),
    ("chh",    "Closed hat", "hat"),
    ("ohh",    "Open hat",   "openhat"),
    ("phh",    "Pedal hat",  "hat"),
    ("tom_lo", "Tom low",    "tom"),
    ("tom_mid", "Tom mid",   "tom"),
    ("tom_hi", "Tom high",   "tom"),
    ("crash",  "Crash",      "crash"),
    ("ride",   "Ride",       "ride"),
    ("perc1",  "Perc 1",     "perc"),
    ("perc2",  "Perc 2",     "perc"),
    ("fx",     "FX",         "fx"),
]
PAD_INDEX = {k: i for i, (k, _, _) in enumerate(PADS)}
NAMES = [n for _, n, _ in PADS]

# default choke groups (open hat is cut by closed + pedal hat)
CHOKE = {"chh": 1, "ohh": 1, "phh": 1}


def note_of(key: str) -> int:
    return BASE_NOTE + PAD_INDEX[key]


def write_midi(path, tracks, bpm, ppq=480):
    """tracks = [(name, [ {beat, pad(index), vel, len(beats)} ])]; drum notes on channel 10."""
    import mido
    mid = mido.MidiFile(ticks_per_beat=ppq, type=1)
    meta = mido.MidiTrack()
    meta.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(bpm), time=0))
    meta.append(mido.MetaMessage("time_signature", numerator=4, denominator=4, time=0))
    mid.tracks.append(meta)
    for name, notes in tracks:
        tr = mido.MidiTrack()
        tr.append(mido.MetaMessage("track_name", name=name, time=0))
        ev = []
        for n in notes:
            s = max(0, int(round(n["beat"] * ppq)))
            e = s + max(1, int(round(n.get("len", 0.125) * ppq)))
            p = BASE_NOTE + int(n["pad"])
            ev.append((s, 1, p, int(np.clip(n["vel"], 1, 127))))
            ev.append((e, 0, p, 0))
        ev.sort(key=lambda x: (x[0], x[1]))
        last = 0
        for t, on, p, v in ev:
            tr.append(mido.Message("note_on" if on else "note_off", channel=9, note=p,
                                   velocity=v if on else 0, time=t - last))
            last = t
        mid.tracks.append(tr)
    mid.save(path)


def progress_writer(session):
    def f(frac, msg):
        try:
            with open(os.path.join(session, "progress.txt"), "w") as fh:
                fh.write(f"{frac:.2f}|{msg}")
        except OSError:
            pass
    return f


def save_json(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f)


def load_json(path):
    with open(path) as f:
        return json.load(f)
