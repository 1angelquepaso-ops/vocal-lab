"""TITO PUENTE regression test (runs in CI before the engine is frozen).

Renders known drum MIDI on the factory kit (produced beats + a real drummer from the
Groove MIDI Dataset), transcribes it, builds layers + fills, and checks:
  * tempo right (or its double / half), every test
  * F1 of hits per sound above the floor we measured when shipping v1
  * MIDI files written; fills land where asked; layers + fills present
Exits 1 on failure.
"""
import json
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import numpy as np  # noqa: E402
from tito import bench, cli  # noqa: E402

FLOOR = {"trap_808": 0.72, "prod_boombap": 0.70, "prod_reggaeton": 0.62, "prod_afrobeats": 0.62}


def main():
    work = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="tito_test_")
    os.makedirs(work, exist_ok=True)
    ok = True
    cases = {c[0]: c for c in bench.cases(True)}
    for name, floor in FLOOR.items():
        _, h, bpm, var, drift, rev = cases[name]
        r = bench.run_case(name, h, bpm, var, work, drift=drift, reverb=rev)
        good = r["tempo_ok"] and r["f1"] >= floor
        ok &= good
        print(f"{name:18s} bpm {r['bpm']:6.1f} (true {bpm:.0f})  F1 {r['f1']:.3f} (floor {floor})  -> {'PASS' if good else 'FAIL'}")
    # full engine round trip through the command line
    stem = os.path.join(work, "prod_boombap.wav")
    sess, out = os.path.join(work, "sess"), os.path.join(work, "out")
    os.makedirs(sess, exist_ok=True)
    cli.main(["analyze", "--session", sess, "--stem", stem, "--out", out])
    a = json.load(open(os.path.join(sess, "analysis.json")))
    res = cli.cmd_build(type("A", (), dict(session=sess, out=out, feel="quantize", qgrid="1/16", style="boombap",
                                             layers="hats,ghost,shaker,perc", grid="1/32", swing=0.2, human=0.6,
                                             fills="every8,prehook", fill_len=4.0, intensity=0.7, crash=1, room=1,
                                             seed=3))())
    n = {k: len(v) for k, v in res["notes"].items()}
    files_ok = all(os.path.exists(f) for f in res["files"].values())
    fills_ok = len(res["windows"]) >= 1 and all(abs(b - round(b / 4) * 4) < 1e-6 for _, b in res["windows"])
    good = files_ok and n["song"] > 100 and n["layers"] > 50 and n["fills"] > 5 and fills_ok and len(a["pads"]) >= 4
    ok &= good
    print(f"round trip: pads {len(a['pads'])}, notes {n}, fill windows {res['windows'][:3]}..., files ok {files_ok} -> "
          f"{'PASS' if good else 'FAIL'}")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
