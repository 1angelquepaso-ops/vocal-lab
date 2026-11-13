"""TITO PUENTE engine command line (called by the plugin; prints ONE JSON line).

  analyze --session DIR --stem FILE --out DIR [--bpm N] [--sensitivity 0..1]
  build   --session DIR --out DIR [--feel keep|quantize] [--qgrid 1/16|1/32|1/16T|auto]
          [--style auto|trap|...|song] [--layers hats,ghost,shaker,perc,openhat] [--grid 1/16|1/32|1/16T|1/32T|1/8T]
          [--swing 0..1] [--human 0..1] [--fills every8,every16,prehook,sections] [--fill-len 1|2|4|8]
          [--intensity 0..1] [--crash 0|1] [--room 0|1] [--seed N] [--tempo-map auto|on|off]
  factory --out DIR          write the 16 factory pad sounds
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import time
import traceback

from .common import NAMES, PADS, progress_writer, save_json, load_json, tempo_map, write_midi


def cmd_analyze(a):
    from . import transcribe
    os.makedirs(a.session, exist_ok=True)
    os.makedirs(a.out, exist_ok=True)
    say = progress_writer(a.session)
    an = transcribe.analyze(a.stem, a.out, bpm=a.bpm, sensitivity=a.sensitivity, progress=say)
    save_json(os.path.join(a.session, "analysis.json"), an)
    return dict(ok=True, bpm=an["bpm"], steady=an["steady"], bars=an["bars"], duration=round(an["duration"], 2),
                t0=round(an["t0"], 4), hits=len(an["hits"]), sounds=an["n_sounds"],
                pads=[dict(pad=p["pad"], key=p["key"], name=NAMES[p["pad"]], file=p["file"], hits=p["hits"])
                      for p in an["pads"]],
                sections=an["sections"], song_fills=an["song_fills"])


def _compact(notes):
    return [[round(n["beat"], 4), n["pad"], n["vel"], round(n.get("len", 0.12), 3)] for n in notes]


def cmd_build(a):
    from . import generate
    an = load_json(os.path.join(a.session, "analysis.json"))
    say = progress_writer(a.session)
    say(0.2, "Building MIDI")
    lay = [x for x in (a.layers or "").split(",") if x]
    where = [x for x in (a.fills or "").split(",") if x]
    r = generate.build(an, feel=a.feel, qgrid=a.qgrid, style=a.style, layer_set=lay, grid=a.grid, swing=a.swing,
                       human=a.human, fill_where=where, fill_len=a.fill_len, intensity=a.intensity,
                       crash=bool(a.crash), room=bool(a.room), seed=a.seed)
    os.makedirs(a.out, exist_ok=True)
    stem = os.path.splitext(os.path.basename(an["file"]))[0][:40]
    tag = "keep" if a.feel == "keep" else "quant"
    files = {
        "song": os.path.join(a.out, f"{stem}_TITO_song_{tag}.mid"),
        "layers": os.path.join(a.out, f"{stem}_TITO_layers_{a.seed}.mid"),
        "fills": os.path.join(a.out, f"{stem}_TITO_fills_{a.seed}.mid"),
        "all": os.path.join(a.out, f"{stem}_TITO_all_{a.seed}.mid"),
    }
    bpm = an["bpm"]
    # drifting (live) song + keep timing: the MIDI carries the song's own tempo changes
    use_map = a.tempo_map == "on" or (a.tempo_map == "auto" and not an.get("steady", True) and a.feel == "keep")
    tm = tempo_map(an.get("beats", [])) if use_map else []
    tm = tm if len(tm) > 1 else None
    write_midi(files["song"], [("Song", r["song"])], bpm, tempos=tm)
    if r["layers"]:
        write_midi(files["layers"], [("Groove layers", r["layers"])], bpm, tempos=tm)
    else:
        files.pop("layers")
    if r["fills"]:
        write_midi(files["fills"], [("Fills", r["fills"])], bpm, tempos=tm)
    else:
        files.pop("fills")
    write_midi(files["all"], [("Song", r["song"]), ("Groove layers", r["layers"]), ("Fills", r["fills"])], bpm,
               tempos=tm)
    end = max([n["beat"] for n in r["song"] + r["layers"] + r["fills"]], default=4.0)
    length = 4.0 * (int(end // 4) + 1)
    res = dict(ok=True, bpm=bpm, style=r["style"], files=files, length_beats=length, t0=an["t0"],
               steady=bool(an.get("steady", True)), tempo_map=bool(tm),
               sections=an["sections"], windows=r["windows"],
               notes=dict(song=_compact(r["song"]), layers=_compact(r["layers"]), fills=_compact(r["fills"])))
    save_json(os.path.join(a.session, "build.json"), res)
    say(1.0, "Done")
    return res


def cmd_factory(a):
    from . import kit
    files = kit.save_factory(a.out)
    return dict(ok=True, files=files, names=NAMES)


def main(argv=None):
    p = argparse.ArgumentParser(prog="tito-engine")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("analyze")
    s.add_argument("--session", required=True)
    s.add_argument("--stem", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--bpm", type=float, default=None)
    s.add_argument("--sensitivity", type=float, default=0.5)
    s = sub.add_parser("build")
    s.add_argument("--session", required=True)
    s.add_argument("--out", required=True)
    s.add_argument("--feel", default="keep", choices=["keep", "quantize"])
    s.add_argument("--qgrid", default="1/16")
    s.add_argument("--style", default="auto")
    s.add_argument("--layers", default="")
    s.add_argument("--grid", default="1/16")
    s.add_argument("--swing", type=float, default=0.0)
    s.add_argument("--human", type=float, default=0.6)
    s.add_argument("--fills", default="")
    s.add_argument("--fill-len", type=float, default=4.0)
    s.add_argument("--intensity", type=float, default=0.6)
    s.add_argument("--crash", type=int, default=1)
    s.add_argument("--room", type=int, default=1)
    s.add_argument("--seed", type=int, default=1)
    s.add_argument("--tempo-map", default="auto", choices=["auto", "on", "off"])
    s = sub.add_parser("factory")
    s.add_argument("--out", required=True)
    a = p.parse_args(argv)
    t = time.time()
    try:
        fn = {"analyze": cmd_analyze, "build": cmd_build, "factory": cmd_factory}[a.cmd]
        res = fn(a)
        res["seconds"] = round(time.time() - t, 2)
    except Exception as e:
        res = dict(ok=False, error=f"{type(e).__name__}: {e}", trace=traceback.format_exc()[-1500:])
    sys.stdout.write(json.dumps(res) + "\n")
    sys.stdout.flush()
    return 0 if res.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
