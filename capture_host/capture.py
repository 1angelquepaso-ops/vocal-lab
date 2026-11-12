"""MIRRORWALL capture controller (drives mw_capture.exe).

  python capture.py scan            scan the WaveShell ONCE and save the plugin list (click Cancel on any
                                    Waves pop-up yourself during this step)
  python capture.py smoke           load every plugin + one short render: report, nothing long
  python capture.py capture <what>  full captures; <what> = a group (eq, color, dyn, fx), plugin folder
                                    names, or "all". Resumable: run again to continue.

Rules learned the hard way: one run at a time (lock file), smoke test before long captures,
stop with Ctrl+C (never Task Manager), every plugin in its own process with a timeout.
"""
import json, os, re, subprocess, sys, threading, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import waves_capture as W                      # test signals, analysis and plan builder (shared)

HOST = os.environ.get("MW_HOST", os.path.join(HERE, "mw_capture.exe"))
SHELL = os.environ.get("MW_SHELL", r"C:\Program Files\Common Files\VST3\WaveShell-VST3 9.91_x64.vst3")
LIST = os.path.join(HERE, "waves_list.xml")
SHELL2 = os.environ.get("MW_SHELL2", r"C:\Program Files\Common Files\VST3\WaveShell2-VST3 9.91_x64.vst3")
DATA = os.path.join(HERE, "data")
LOCK = os.path.join(HERE, "capture.lock")
SR = 44100
BATCH = 40

# (folder, Waves name, kind, params to leave out)
PLUGINS = [
    # EQ
    ("scheps73", "Scheps 73 Mono", "eq", None),
    ("puigtec_eqp1a", "PuigTec EQP1A Mono", "eq", None),
    ("puigtec_meq5", "PuigTec MEQ5 Mono", "eq", None),
    ("api550a", "API-550A Mono", "eq", None),
    ("api560", "API-560 Mono", "eq", None),
    # colour
    ("redd37_51", "REDD37-51 Mono", "color", None),
    ("nls_channel", "NLS Channel Mono", "color", None),
    ("j37", "J37 Mono", "color", None),
    ("kramer_tape", "Kramer Tape Mono", "color", None),
    ("aphex_exciter", "Aphex Vintage Exciter Mono", "color", None),
    # dynamics
    ("cla76", "CLA-76 Mono", "dyn", None),
    ("cla2a", "CLA-2A Mono", "dyn", None),
    ("cla3a", "CLA-3A Mono", "dyn", None),
    ("dbx160", "dbx-160 Mono", "dyn", None),
    ("rvox", "RVox Mono", "dyn", None),
    ("c1comp", "C1 comp Mono", "dyn", None),
    ("ssl_g_channel_dyn", "SSLGChannel Mono", "dyn",       # EQ section already measured (= G-EQ)
     r"^(LF|LMF|HMF|HF|HP|LP)\b|\bEQ\b|div3|mul3|bell"),
    ("deesser", "DeEsser Mono", "dyn", None),
    ("vocal_rider", "Vocal Rider Mono", "dyn", None),
    # reverb / delay
    ("abbey_road_plates", "Abbey Road Plates Mono/Stereo", "fx", None),
    ("h_delay", "H-Delay Mono/Stereo", "fx", None),
    ("h_reverb", "H-Reverb Mono/Stereo", "fx", None),
    ("reel_adt", "Reel ADT Mono/Stereo", "fx", None),
    ("doubler2", "Doubler2 Mono/Stereo", "fx", None),
]
GROUPS = {"eq", "color", "dyn", "fx"}
if os.environ.get("MW_TEST_PLUGINS"):              # local testing only
    PLUGINS = json.loads(os.environ["MW_TEST_PLUGINS"])


def log(*a):
    W.log(*a)                                    # same run_log.txt format as the old batch


def run_host(args, timeout, on_line=None):
    """Run mw_capture with a timeout; Waves pop-ups from it are auto-cancelled. Returns (rc, lines)."""
    p = subprocess.Popen([HOST] + args, cwd=HERE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, encoding="utf-8", errors="replace", bufsize=1)
    W.WATCH_PIDS.add(p.pid)
    state = {"killed": False}
    def kill():
        state["killed"] = True
        p.kill()
    killer = threading.Timer(timeout, kill)
    killer.start()
    lines = []
    for line in p.stdout:
        line = line.rstrip()
        if line:
            lines.append(line)
            if on_line:
                on_line(line)
    p.wait()
    killer.cancel()
    W.WATCH_PIDS.discard(p.pid)
    timed_out = state["killed"]
    return (-999 if timed_out else p.returncode), lines


def lock():
    if os.path.exists(LOCK):
        try:
            pid = int(open(LOCK).read().strip())
            import ctypes
            h = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)
            if h:
                ctypes.windll.kernel32.CloseHandle(h)
                print("Another capture run is already going (pid %d). Stop that one first." % pid)
                sys.exit(1)
        except Exception:
            pass
    open(LOCK, "w").write(str(os.getpid()))


def unlock():
    try: os.remove(LOCK)
    except Exception: pass


# ------------------------------------------------------------------ scan
def merge_lists(dst, src):
    """Add the PLUGIN entries of one saved list to another."""
    import xml.etree.ElementTree as ET
    a, b = ET.parse(dst), ET.parse(src)
    have = {e.get("name") for e in a.getroot()}
    for e in b.getroot():
        if e.get("name") not in have:
            a.getroot().append(e)
    a.write(dst, encoding="utf-8", xml_declaration=True)


def scan():
    log("=== scan: reading the Waves shells once (2-5 min). Click Cancel on any Waves pop-up. ===")
    if not os.path.exists(LIST):
        rc, lines = run_host(["scan", SHELL, LIST], 30 * 60, lambda l: log("   ", l) if l.startswith(("scanned", "ERROR")) else None)
        if not os.path.exists(LIST):
            log("scan failed", rc); return False
    if os.path.exists(SHELL2):                  # the second shell holds NLS, SSL E/G Channel, RS56 ...
        tmp = os.path.join(HERE, "waves_list2.xml")
        rc, lines = run_host(["scan", SHELL2, tmp], 15 * 60, lambda l: log("   ", l) if l.startswith(("scanned", "ERROR")) else None)
        if os.path.exists(tmp):
            merge_lists(LIST, tmp)
    txt = open(LIST, encoding="utf-8", errors="replace").read()
    missing = [p[1] for p in PLUGINS if f'name="{p[1]}"' not in txt]
    log(f"plugin list saved ({txt.count('<PLUGIN ')} plugins). missing from list: {missing if missing else 'none'}")
    return True


# ------------------------------------------------------------------ helpers
def params_for(folder, pname):
    d = os.path.join(DATA, folder); os.makedirs(d, exist_ok=True)
    f = os.path.join(d, "params.json")
    if os.path.exists(f):
        return json.load(open(f)), None
    t0 = time.time()
    rc, lines = run_host(["params", LIST, pname, f], 10 * 60)
    if rc != 0 or not os.path.exists(f):
        return None, f"params failed (code {rc}): " + " / ".join(lines[-3:])
    return json.load(open(f)), round(time.time() - t0, 1)


def render(pname, wav, settings, outdir, timeout, block=512):
    sf_ = os.path.join(outdir, "settings.json")
    json.dump(settings, open(sf_, "w"))
    times = []
    rc, lines = run_host(["render", LIST, pname, wav, sf_, outdir, str(block)], timeout,
                         lambda l: times.append(float(l.split()[2])) if l.startswith("R ") else None)
    return rc, lines, times


# ------------------------------------------------------------------ smoke test
def smoke():
    import numpy as np, soundfile as sf
    S = W.make_signals()
    os.makedirs(DATA, exist_ok=True)
    tmp = os.path.join(DATA, "_smoke"); os.makedirs(tmp, exist_ok=True)
    wav = os.path.join(tmp, "in.wav")
    x = np.concatenate([S["ms-18"], np.zeros(SR // 2, "float32")])
    sf.write(wav, x, SR, subtype="FLOAT")
    report = {}
    log(f"=== smoke test: {len(PLUGINS)} plugins (load + one 1.6 s render each) ===")
    for folder, pname, kind, _ in PLUGINS:
        r = dict(plugin=pname)
        P, info = params_for(folder, pname)
        if P is None:
            r.update(ok=False, problem=info)
        else:
            r["params"] = len(P["params"]); r["latency"] = P["latency"]; r["out_ch"] = P["out"]
            defaults = {p["name"]: p["default"] for p in P["params"]}
            out = os.path.join(tmp, folder); os.makedirs(out, exist_ok=True)
            t0 = time.time()
            rc, lines, times = render(pname, wav, dict(defaults=defaults, settings=[dict(id=0, set={})]), out, 5 * 60)
            r["total_s"] = round(time.time() - t0, 1)
            f = os.path.join(out, "out_0.wav")
            if rc == 0 and os.path.exists(f):
                y, _ = sf.read(f, always_2d=True)
                rms = float(np.sqrt(np.mean(y ** 2)))
                r.update(ok=bool(np.isfinite(y).all() and rms > 1e-5), render_s=times[0] if times else None,
                         out_rms_db=round(20 * np.log10(rms + 1e-12), 1))
                if not r["ok"]:
                    r["problem"] = "silent or invalid output"
            else:
                r.update(ok=False, problem=f"render failed (code {rc}): " + " / ".join(lines[-3:]))
        report[folder] = r
        log(f"  {'OK  ' if r.get('ok') else 'FAIL'} {pname:32s} "
            + (f"params {r.get('params')}, render {r.get('render_s')} s, out {r.get('out_rms_db')} dB"
               if r.get("ok") else r.get("problem", "")))
        json.dump(report, open(os.path.join(DATA, "smoke_report.json"), "w"), indent=1)
    ok = sum(1 for r in report.values() if r.get("ok"))
    log(f"=== smoke test done: {ok}/{len(report)} OK ===")


# ------------------------------------------------------------------ full capture
def capture_one(folder, pname, kind, exclude):
    import numpy as np, soundfile as sf
    d = os.path.join(DATA, folder)
    if os.path.exists(os.path.join(d, "DONE")):
        log(f"  {pname}: already done"); return True
    P, info = params_for(folder, pname)
    if P is None:
        log(f"  {pname}: {info}"); return False
    params = [dict(name=p["name"], default=p["default"], steps=p["steps"], boolean=p["boolean"],
                   discrete=p["discrete"]) for p in P["params"] if not p.get("meta")]
    if exclude:
        params = [p for p in params if not re.search(exclude, p["name"], re.I)]
    planf = os.path.join(d, "plan.json")
    if os.path.exists(planf):
        plan = json.load(open(planf))["plan"]
    else:
        plan = W.build_plan(params, kind)
        json.dump(dict(plugin=pname, kind=kind, sr=SR, params=P["params"], plan=plan), open(planf, "w"), indent=1)
    done = set()
    for f in os.listdir(d):
        if f.startswith("chunk_") and f.endswith(".json"):
            done.update(json.load(open(os.path.join(d, f)))["ids"])
    todo = [i for i in range(len(plan)) if i not in done]
    log(f"  {pname}: {len(plan)} settings, {len(todo)} to do")
    S = W.make_signals()
    wav = os.path.join(d, "signals.wav")
    if not os.path.exists(wav):
        sf.write(wav, S["all"], SR, subtype="FLOAT")
    defaults = {p["name"]: p["default"] for p in P["params"]}
    nchunk = len([f for f in os.listdir(d) if f.startswith("chunk_") and f.endswith(".json")])
    tmp = os.path.join(d, "_render"); os.makedirs(tmp, exist_ok=True)
    t_start = time.time()
    for b in range(0, len(todo), BATCH):
        ids = todo[b:b + BATCH]
        settings = dict(defaults=defaults, settings=[dict(id=i, set=plan[i]) for i in ids])
        rc, lines, times = render(pname, wav, settings, tmp, 60 * 60)
        buf, got = {}, []
        for i in ids:
            f = os.path.join(tmp, f"out_{i}.wav")
            if not os.path.exists(f):
                continue
            y, _ = sf.read(f, always_2d=True)
            y = y.T
            out = {k: y[:, o:o + n] for k, (o, n) in S["offs"].items()}
            if i == 0:
                sf.write(os.path.join(d, "default_out.wav"), y.T, SR, subtype="FLOAT")
            R = W.analyse(S, out, kind)
            for k, v in R.items():
                buf.setdefault(k, []).append(v)
            got.append(i)
            os.remove(f)
        if got:
            np.savez_compressed(os.path.join(d, f"chunk_{nchunk:03d}.npz"), **{k: np.stack(v) for k, v in buf.items()})
            json.dump(dict(ids=got, settings=[plan[i] for i in got]), open(os.path.join(d, f"chunk_{nchunk:03d}.json"), "w"))
            nchunk += 1
        n_done = b + len(got)
        rate = (sum(times) / len(times)) if times else float("nan")
        log(f"     {min(b + BATCH, len(todo))}/{len(todo)} settings  ({rate:.2f} s each)")
        open(os.path.join(HERE, "progress.txt"), "w").write(f"{folder}: {min(b + BATCH, len(todo))}/{len(todo)}\n")
        if rc != 0:
            log(f"     host stopped early (code {rc}): " + " / ".join(lines[-3:]))
            return False
    open(os.path.join(d, "DONE"), "w").write(time.strftime("%Y-%m-%d %H:%M:%S"))
    log(f"  {pname}: done in {(time.time() - t_start) / 60:.1f} min")
    return True


def capture(what):
    sel = [p for p in PLUGINS if (not what or "all" in what or p[2] in what or p[0] in what)]
    log(f"=== capture: {', '.join(p[0] for p in sel)} ===")
    for folder, pname, kind, ex in sel:
        try:
            capture_one(folder, pname, kind, ex)
        except Exception:
            log(traceback.format_exc())
    log("=== capture finished ===")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "smoke"
    lock()
    if cmd != "scan" and os.path.exists(LIST):
        threading.Thread(target=W.dialog_watcher, daemon=True).start()
    try:
        if not os.path.exists(LIST) and cmd != "scan":
            if not scan():
                sys.exit(1)
            threading.Thread(target=W.dialog_watcher, daemon=True).start()
        if cmd == "scan":
            scan()
        elif cmd == "smoke":
            smoke()
        elif cmd == "capture":
            capture(sys.argv[2:])
        else:
            print(__doc__)
    except KeyboardInterrupt:
        log("stopped with Ctrl+C")
    finally:
        unlock()
