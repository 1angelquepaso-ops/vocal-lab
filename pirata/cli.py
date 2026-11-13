"""PIRATA engine command line (called by the plugin, prints one JSON line).

  analyze --session DIR --sample FILE [--bpm N] [--sensitivity 0..1]
  build   --session DIR --detail simple|played|rich
          --mode off|complement|counter|amplify|random|arp
          [--density 0..1] [--register low|mid|high] [--seed N]
          [--arp-pattern up|down|updown|converge|random] [--arp-rate 0.5|0.25|0.1667|0.125]
          [--arp-octaves 1..3] [--timing grid|16|32|16T|8T|keep] [--out DIR]

Timing (export + preview): grid = PIRATA's own grid (1/16, triplet beats kept as
triplets), 16 / 32 / 16T / 8T = snap the real timing to that grid, keep = the
sample's own timing (every note where it was played, ~1-3 ms).
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import traceback

import mido
import numpy as np

from . import analysis, arrange, piano

GRIDS = {"16": 4, "32": 8, "16T": 6, "8T": 3}


def _progress(session):
    def f(frac, msg):
        try:
            with open(os.path.join(session, "progress.txt"), "w") as fh:
                fh.write(f"{frac:.2f}|{msg}")
        except OSError:
            pass
    return f


def write_midi(path, tracks, bpm, ppq=480):
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
            s = max(0, int(round(n["start"] * ppq)))
            e = max(s + 1, int(round(n["end"] * ppq)))
            ev.append((s, 1, n["pitch"], n["vel"]))
            ev.append((e, 0, n["pitch"], 0))
        ev.sort(key=lambda x: (x[0], x[1]))
        last = 0
        for t, on, p, v in ev:
            tr.append(mido.Message("note_on" if on else "note_off", note=int(p),
                                   velocity=int(v) if on else 0, time=t - last))
            last = t
        mid.tracks.append(tr)
    mid.save(path)


def _warp(an):
    """Grid position (beats) -> where the sample really plays it: per grid slot the
    median offset of the sample's own notes, interpolated in between. Generated
    notes (Rich voicings, bass, melody layer) take on the sample's feel this way."""
    slots = {}
    for n in an["notes"]:
        if "ts" in n:
            slots.setdefault(n["start"], []).append(n["ts"] - n["start"])
    if not slots:
        return lambda x: x
    q = np.array(sorted(slots))
    o = np.array([float(np.median(slots[k])) for k in q])
    return lambda x: x + float(np.interp(x, q, o))


def retime(notes, an, mode):
    """Apply the Timing choice to built notes (all in beats)."""
    if mode in (None, "grid") or not notes:
        return notes
    warp = _warp(an)
    real = {(n["start"], n["pitch"]): n["ts"] for n in an["notes"] if "ts" in n}
    d = GRIDS.get(mode)
    out = []
    for n in notes:
        s = real.get((n["start"], n["pitch"]))
        if s is None:
            s = warp(n["start"])
        e = warp(n["end"])
        if d:
            s, e = round(s * d) / d, round(e * d) / d
            if e <= s:
                e = s + 1 / d
        else:
            e = max(e, s + 0.05)
        out.append(dict(n, start=round(s, 5), end=round(e, 5)))
    # same pitch: no duplicates, no overlaps
    out.sort(key=lambda n: (n["pitch"], n["start"]))
    res = []
    for n in out:
        if res and res[-1]["pitch"] == n["pitch"] and n["start"] < res[-1]["end"]:
            if n["start"] - res[-1]["start"] < 1e-4:
                res[-1]["end"] = max(res[-1]["end"], n["end"])
                continue
            res[-1]["end"] = n["start"]
        res.append(n)
    res = [n for n in res if n["end"] > n["start"]]
    res.sort(key=lambda n: (n["start"], n["pitch"]))
    return res


def _shift(notes, by):
    # a note played just ahead of bar 1 (Keep timing) starts the bar instead of before it
    return [dict(n, start=max(0.0, n["start"] - by), end=max(n["end"] - by, n["start"] - by + 0.02, 0.02))
            for n in notes]


def cmd_analyze(a):
    os.makedirs(a.session, exist_ok=True)
    say = _progress(a.session)
    an = analysis.analyze(a.sample, bpm=a.bpm, sensitivity=a.sensitivity, progress=say)
    with open(os.path.join(a.session, "analysis.json"), "w") as f:
        json.dump(an, f)
    bars = max([c["end"] for c in an["chords"]], default=0) / 4
    return dict(ok=True, key=an["key"], bpm=an["bpm"], bars=int(round(bars)),
                notes=len(an["notes"]), timing=an.get("timing", {}),
                chords=[dict(start=c["start"], end=c["end"], name=c["name"], alts=c["alts"],
                             conf=round(c["conf"], 2)) for c in an["chords"]])


def cmd_build(a):
    with open(os.path.join(a.session, "analysis.json")) as f:
        an = json.load(f)
    say = _progress(a.session)
    say(0.1, "Building chords")
    upper, bass = arrange.chord_versions(an, a.detail)
    mel = []
    if a.mode != "off":
        say(0.3, "Writing melody")
        # Simple's "chords" track is a single melody line, so arps/registers use the chord symbols
        mel = arrange.melody(an, a.mode, a.density, a.register, a.seed,
                             chord_notes=upper if a.detail != "simple" else None,
                             arp_pattern=a.arp_pattern, arp_rate=a.arp_rate, arp_octaves=a.arp_octaves)
    upper, bass, mel = (retime(x, an, a.timing) for x in (upper, bass, mel))
    # everything starts on the first bar that has music
    first = min([n["start"] for n in upper + bass + mel], default=0)
    origin = 4 * int((first + 0.1) // 4)          # a note pushed just ahead of bar 1 stays in bar 1
    upper, bass, mel = _shift(upper, origin), _shift(bass, origin), _shift(mel, origin)
    chords = [dict(c, start=c["start"] - origin, end=c["end"] - origin) for c in an["chords"]]
    bpm = an["bpm"]
    spb = 60.0 / bpm

    out = a.out or a.session
    os.makedirs(out, exist_ok=True)
    stem = os.path.splitext(os.path.basename(an["file"]))[0][:40]
    tag = {"simple": "simple", "played": "asplayed", "rich": "rich"}[a.detail]
    mtag = a.mode if a.mode != "arp" else f"arp-{a.arp_pattern}"
    files = {
        "chords": os.path.join(out, f"{stem}_PIRATA_chords_{tag}.mid"),
        "bass": os.path.join(out, f"{stem}_PIRATA_bass_{tag}.mid"),
        "all": os.path.join(out, f"{stem}_PIRATA_full_{tag}.mid"),
    }
    write_midi(files["chords"], [("Chords", upper)], bpm)
    write_midi(files["bass"], [("Bass", bass)], bpm)
    tracks = [("Chords", upper), ("Bass", bass)]
    if mel:
        files["melody"] = os.path.join(out, f"{stem}_PIRATA_melody_{mtag}_{a.seed}.mid")
        write_midi(files["melody"], [("Melody", mel)], bpm)
        tracks.append(("Melody", mel))
    write_midi(files["all"], tracks, bpm)

    say(0.6, "Rendering grand piano")
    to_s = lambda ns: [(n["start"] * spb, n["end"] * spb, n["pitch"], n["vel"]) for n in ns]
    end_b = max([n["end"] for n in upper + bass + mel], default=4)
    length = (4 * -(-end_b // 4)) * spb + 2.0
    wav_ch = os.path.join(a.session, "preview_chords.wav")
    # Simple previews as the bare one-note line; the root bass stays on the BASS tile
    heard = upper if a.detail == "simple" else upper + bass
    piano.save(wav_ch, piano.render(to_s(heard), length_s=length, gain_db=-4))
    res = dict(ok=True, bpm=bpm, key=an["key"], files=files, preview={"chords": wav_ch},
               length_beats=4 * -(-end_b // 4),
               orig_offset=an["grid_start"] + origin * spb,
               chords=[dict(start=c["start"], end=c["end"], name=c["name"]) for c in chords
                       if c["end"] > 0],
               notes={"chords": upper, "bass": bass, "melody": mel})
    if mel:
        wav_m = os.path.join(a.session, "preview_melody.wav")
        piano.save(wav_m, piano.render(to_s(mel), length_s=length, gain_db=-6))
        res["preview"]["melody"] = wav_m
    with open(os.path.join(a.session, "build.json"), "w") as f:
        json.dump(res, f)
    say(1.0, "Done")
    return res


def main(argv=None):
    p = argparse.ArgumentParser(prog="pirata-engine")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("analyze")
    s.add_argument("--session", required=True)
    s.add_argument("--sample", required=True)
    s.add_argument("--bpm", type=float, default=None)
    s.add_argument("--sensitivity", type=float, default=0.5)
    s = sub.add_parser("build")
    s.add_argument("--session", required=True)
    s.add_argument("--detail", default="played", choices=["simple", "played", "rich"])
    s.add_argument("--mode", default="off",
                   choices=["off", "complement", "counter", "amplify", "random", "arp"])
    s.add_argument("--density", type=float, default=0.5)
    s.add_argument("--register", default="mid", choices=["low", "mid", "high"])
    s.add_argument("--seed", type=int, default=0)
    s.add_argument("--arp-pattern", default="up", choices=["up", "down", "updown", "converge", "random"])
    s.add_argument("--arp-rate", type=float, default=0.25)
    s.add_argument("--arp-octaves", type=int, default=2)
    s.add_argument("--timing", default="grid", choices=["grid", "keep"] + list(GRIDS))
    s.add_argument("--out", default=None)
    a = p.parse_args(argv)
    t = time.time()
    try:
        res = cmd_analyze(a) if a.cmd == "analyze" else cmd_build(a)
        res["seconds"] = round(time.time() - t, 2)
    except Exception as e:  # report to the plugin instead of crashing
        res = dict(ok=False, error=f"{type(e).__name__}: {e}", trace=traceback.format_exc()[-1500:])
    sys.stdout.write(json.dumps(res) + "\n")
    sys.stdout.flush()
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
