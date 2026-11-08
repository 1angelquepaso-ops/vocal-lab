"""Build the stock presets from the user's reference chains (run once, offline)."""
import json, os, sys, time
import numpy as np, soundfile as sf
from . import dsp, fit, tune

D = "/mnt/user-data/uploads/IDEAS/"
RAW = D + "rich shit MEX RAW VOX.wav"
LEADS = {
    "BAYWATCH": "BAYWATCH 2023 ACAPELLA 2.wav",
    "BLUE FAYGO": "blue faygo prepro acapella.wav",
    "DAYSINN": "daysinn prepro acapella.wav",
    "DRAKE DEMO": "drake demo_5 acapella.wav",
    "SAIN JHN": "SAIN JHN PEPRO acapella APRIL.wav",
    "SNOW BUTTON": "SNOW BUTTON prepro_4 ACAPELLA.wav",
}
HARM = "baywatch prepro_2 RAW HARMZ OG.wav"


def main(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    x, sr = sf.read(RAW)
    y = x.mean(1)[: sr * 40]
    meas = {}
    for nm, f in list(LEADS.items()) + [("HARMONY", HARM)]:
        meas[nm] = fit.measure_ref(D + f)
        print(nm, "measured", flush=True)
    jobs = [(nm, [meas[nm]]) for nm in LEADS] + [("LEAD BLEND", [meas[n] for n in LEADS]),
                                                 ("HARMONY", [meas["HARMONY"]])]
    tuned = {}
    for nm, refs in jobs:
        t0 = time.time()
        T = fit.target_from(refs)
        td = fit.tune_defaults(T["within10"])
        k = (td["speed"], td["amount"])
        if k not in tuned:
            tuned[k] = tune.autotune(y, sr, "auto", "auto", td["speed"], td["amount"], td["humanize"])[0] \
                if td["amount"] > 0 else y
        print(nm, flush=True)
        P = fit.fit_preset(tuned[k], T, name=nm)
        P["tune"] = td
        P["role"] = "harmony" if nm == "HARMONY" else "lead"
        with open(os.path.join(out_dir, nm.replace(" ", "_") + ".json"), "w") as fh:
            json.dump(P, fh, indent=1)
        print(f"  done in {time.time() - t0:.0f}s", flush=True)


if __name__ == "__main__":
    main(sys.argv[1])
