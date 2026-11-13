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

FLOOR = {"trap_808": 0.72, "prod_boombap": 0.70, "prod_reggaeton": 0.62, "prod_afrobeats": 0.62,
         # v1.1: dark / processed electronic kits at non-round tempos (Nov 2026 field test)
         "electro_pulse_dark": 0.50, "boombap_dark": 0.70, "house_dark": 0.33}


def field_test_checks(r, bars_breakdown=(12, 20)):
    """The Nov 2026 field-test failures, as tests: exact non-round BPM on a steady grid, no
    'Crash' playing every beat, a quiet middle part found as a Breakdown."""
    an = r["an"]
    from tito.common import PAD_INDEX
    crash = sum(1 for h in an["hits"] if h["pad"] == PAD_INDEX["crash"])
    secs = an["sections"]
    brk = any(s["label"] == "Breakdown" and s["start"] < bars_breakdown[1] and s["end"] > bars_breakdown[0]
              for s in secs)
    ok = an["steady"] and abs(an["bpm"] - r["bpm_true"]) < 0.06 and crash <= an["bars"] and brk
    print(f"field test: steady {an['steady']} bpm {an['bpm']} (true {r['bpm_true']}), crash hits {crash} / "
          f"{an['bars']} bars, breakdown found {brk} -> {'PASS' if ok else 'FAIL'}")
    return ok


def tempo_map_check(work):
    """Live drummer with drifting tempo: the exported MIDI (tempo map) must play back on the
    original performance's timing."""
    import mido
    case = [c for c in bench.cases(False) if c[0] == "live_drift"]
    if not case:
        print("tempo map: no live case available -> SKIP")
        return True
    name, h, bpm, var, drift, rev = case[0]
    r = bench.run_case(name, h, bpm, var, work, drift=drift, reverb=rev)
    sess, out = os.path.join(work, "sess_live"), os.path.join(work, "out_live")
    os.makedirs(sess, exist_ok=True)
    json.dump(r["an"], open(os.path.join(sess, "analysis.json"), "w"))
    res = cli.cmd_build(type("A", (), dict(session=sess, out=out, feel="keep", qgrid="1/16", style="auto", layers="",
                                             grid="1/16", swing=0.0, human=0.6, fills="", fill_len=4.0, intensity=0.6,
                                             crash=1, room=1, seed=1, tempo_map="auto"))())
    mf = mido.MidiFile(res["files"]["song"])
    t, played = 0.0, []
    for m in mf:
        t += m.time
        if m.type == "note_on" and m.velocity > 0:
            played.append(t)
    played = np.array(sorted(set(np.round(played, 4))))
    hits = np.array(sorted({x["t"] - r["an"]["t0"] for x in r["an"]["hits"]}))
    i = np.clip(np.searchsorted(played, hits), 1, len(played) - 1)
    d = np.minimum(np.abs(played[i] - hits), np.abs(played[i - 1] - hits))
    ok = (not r["an"]["steady"]) and res.get("tempo_map") and float(np.median(d)) < 0.005
    print(f"tempo map: drifting {not r['an']['steady']}, map written {res.get('tempo_map')}, "
          f"MIDI vs performance median {1000 * np.median(d):.1f} ms -> {'PASS' if ok else 'FAIL'}")
    return bool(ok)


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
        print(f"{name:22s} bpm {r['bpm']:6.2f} (true {bpm:.2f})  F1 {r['f1']:.3f} (floor {floor})  names {r['names']:.2f}  "
              f"-> {'PASS' if good else 'FAIL'}")
        if name == "electro_pulse_dark":
            ok &= field_test_checks(r)
    ok &= tempo_map_check(work)
    # full engine round trip through the command line
    stem = os.path.join(work, "prod_boombap.wav")
    sess, out = os.path.join(work, "sess"), os.path.join(work, "out")
    os.makedirs(sess, exist_ok=True)
    cli.main(["analyze", "--session", sess, "--stem", stem, "--out", out])
    a = json.load(open(os.path.join(sess, "analysis.json")))
    res = cli.cmd_build(type("A", (), dict(session=sess, out=out, feel="quantize", qgrid="1/16", style="boombap",
                                             layers="hats,ghost,shaker,perc", grid="1/32", swing=0.2, human=0.6,
                                             fills="every8,prehook", fill_len=4.0, intensity=0.7, crash=1, room=1,
                                             seed=3, tempo_map="auto"))())
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
