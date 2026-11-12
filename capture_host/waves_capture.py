"""MIRRORWALL Waves capture batch.

Loads each Waves plugin once (via Pedalboard), builds a settings plan from its own knobs, renders a
fixed set of test signals at every setting and stores the measurements (not raw audio) per plugin.
Resumable: run again and it continues where it stopped. Progress: run_log.txt and progress.txt.

Per setting it stores:
  ms     transfer function (dB + phase) at 360 log frequencies, multisine at -30/-18/-6 dBFS RMS
  lev    1 kHz level steps -40..0 dBFS: output gain dB and H2/H3/H5 (dB re fundamental)
  env    gain trace (2 ms frames) for a -30 -> -6 -> -30 dBFS tone step (attack / release)
  ess    exponential sweep: harmonic responses H1..H5 (dB) at 200 log frequencies
  ir     (reverbs / delays only) 4 s impulse response, L/R, from the sweep
"""
import json, os, re, subprocess, sys, threading, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "data")
SR = 44100
SHELL = r"C:\Program Files\Common Files\VST3\WaveShell-VST3 9.91_x64.vst3"

PLUGINS = [  # (folder, Waves name, kind)  kind: eq / dyn / color / fx
    # Tier 1
    ("cla76", "CLA-76 Mono", "dyn"),
    ("cla2a", "CLA-2A Mono", "dyn"),
    ("dbx160", "dbx-160 Mono", "dyn"),
    ("rvox", "RVox Mono", "dyn"),
    ("c1comp", "C1 comp Mono", "dyn"),
    ("ssl_e_channel", "SSLChannel Mono", "dyn"),
    ("ssl_g_channel", "SSLGChannel Mono", "dyn"),
    ("scheps73", "Scheps 73 Mono", "eq"),
    ("puigtec_eqp1a", "PuigTec EQP1A Mono", "eq"),
    ("deesser", "DeEsser Mono", "dyn"),
    ("abbey_road_plates", "Abbey Road Plates Mono/Stereo", "fx"),
    ("h_delay", "H-Delay Mono/Stereo", "fx"),
    # Tier 2
    ("ssl_g_eq", "SSLEQ Mono", "eq"),
    ("api550a", "API-550A Mono", "eq"),
    ("api560", "API-560 Mono", "eq"),
    ("puigtec_meq5", "PuigTec MEQ5 Mono", "eq"),
    ("redd37_51", "REDD37-51 Mono", "color"),
    ("nls_channel", "NLS Channel Mono", "color"),
    ("j37", "J37 Mono", "color"),
    ("kramer_tape", "Kramer Tape Mono", "color"),
    ("aphex_exciter", "Aphex Vintage Exciter Mono", "color"),
    ("cla3a", "CLA-3A Mono", "dyn"),
    ("vocal_rider", "Vocal Rider Mono", "dyn"),
    ("h_reverb", "H-Reverb Mono/Stereo", "fx"),
    ("reel_adt", "Reel ADT Mono/Stereo", "fx"),
    ("doubler2", "Doubler2 Mono/Stereo", "fx"),
]

SKIP = re.compile(r"bypass|preset|phase|meter|^gr\b|undo|redo|a/b|load|save|setup|copy|link|midi|dither", re.I)
CHUNK = 40


def log(*a):
    s = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a)
    print(s, flush=True)
    with open(os.path.join(HERE, "run_log.txt"), "a", encoding="utf-8") as f:
        f.write(s + "\n")


# ------------------------------------------------------------------ test signals + analysis
def make_signals():
    import numpy as np
    rng = np.random.default_rng(7)
    P = 8192
    spec = np.zeros(P // 2 + 1, complex)
    k = np.arange(1, P // 2)
    spec[k] = np.exp(2j * np.pi * rng.random(len(k)))
    base = np.fft.irfft(spec, P)
    base /= np.sqrt(np.mean(base ** 2))
    S = {"P": P}
    for db in (-30, -18, -6):
        S[f"ms{db}"] = (np.tile(base, 6) * 10 ** (db / 20)).astype("float32")
    t = np.arange(int(0.3 * SR)) / SR
    tone = lambda db, n: np.sin(2 * np.pi * 1000 * np.arange(n) / SR) * 10 ** (db / 20)
    S["lev_db"] = list(range(-40, 1, 4))
    S["lev"] = np.concatenate([np.sin(2 * np.pi * 1000 * t) * 10 ** (d / 20) for d in S["lev_db"]]).astype("float32")
    n1, n2, n3 = int(0.5 * SR), int(1.0 * SR), int(1.5 * SR)
    tt = np.arange(n1 + n2 + n3)
    amp = np.concatenate([np.full(n1, 10 ** (-30 / 20)), np.full(n2, 10 ** (-6 / 20)), np.full(n3, 10 ** (-30 / 20))])
    S["env"] = (np.sin(2 * np.pi * 1000 * tt / SR) * amp).astype("float32")
    S["env_in"] = amp
    T, f1, f2 = 2.0, 20.0, 20000.0
    L = T / np.log(f2 / f1)
    ts = np.arange(int(T * SR)) / SR
    sweep = np.sin(2 * np.pi * f1 * L * (np.exp(ts / L) - 1)) * 10 ** (-12 / 20)
    fade = np.ones_like(sweep); m = int(0.01 * SR); fade[:m] = np.linspace(0, 1, m); fade[-m:] = np.linspace(1, 0, m)
    sweep *= fade
    inv = sweep[::-1] * np.exp(-ts / L)
    w = np.fft.rfftfreq(1 << 18, 1 / SR)
    # normalise inverse filter so sweep * inv = unit impulse
    H = np.fft.rfft(sweep, 1 << 18) * np.fft.rfft(inv, 1 << 18)
    band = (w > 200) & (w < 10000)
    inv /= np.mean(np.abs(H[band]))
    S["ess"] = np.concatenate([sweep, np.zeros(int(5.0 * SR))]).astype("float32")
    S["ess_inv"], S["ess_T"], S["ess_L"] = inv, T, L
    gap = np.zeros(int(1.5 * SR), "float32")
    order = ["ms-30", "ms-18", "ms-6", "lev", "env", "ess"]
    parts, pos, offs = [], 0, {}
    for k in order:
        offs[k] = (pos, len(S[k])); parts += [S[k], gap]; pos += len(S[k]) + len(gap)
    S["all"], S["offs"] = np.concatenate(parts).astype("float32"), offs
    S["f_ms"] = np.geomspace(20, 20000, 360)
    S["f_ess"] = np.geomspace(20, 20000, 200)
    return S


def analyse(S, out, kind):
    import numpy as np
    R = {}
    P = S["P"]
    msH = []
    for db in (-30, -18, -6):
        x = S[f"ms{db}"][2 * P:].reshape(4, P)
        y = out[f"ms{db}"][0][2 * P:6 * P].reshape(4, P)
        H = np.fft.rfft(y, axis=1).mean(0) / (np.fft.rfft(x, axis=1).mean(0) + 1e-30)
        kk = np.clip(np.round(S["f_ms"] * P / SR).astype(int), 1, P // 2 - 1)
        msH.append(np.stack([20 * np.log10(np.abs(H[kk]) + 1e-12), np.angle(H[kk])]))
    R["ms"] = np.array(msH, "float32")                       # (3 levels, 2, 360)
    seg = int(0.3 * SR)
    lev = []
    y = out["lev"][0]
    for i, d in enumerate(S["lev_db"]):
        a = y[(i + 1) * seg - 4096:(i + 1) * seg] * np.hanning(4096)
        F = np.abs(np.fft.rfft(a))
        pk = lambda f: F[max(1, int(round(f * 4096 / SR)) - 2):int(round(f * 4096 / SR)) + 3].max() + 1e-12
        h1 = pk(1000)
        gain = 20 * np.log10(np.sqrt(np.mean(y[(i + 1) * seg - 4096:(i + 1) * seg] ** 2)) + 1e-12) - (d - 3.0103)
        lev.append([gain, 20 * np.log10(pk(2000) / h1), 20 * np.log10(pk(3000) / h1), 20 * np.log10(pk(5000) / h1)])
    R["lev"] = np.array(lev, "float32")                       # (11, 4)
    fr = int(0.002 * SR)
    e = out["env"][0]
    n = len(e) // fr
    rms = np.sqrt((e[:n * fr].reshape(n, fr) ** 2).mean(1)) + 1e-12
    ref = S["env_in"][:n * fr].reshape(n, fr).mean(1) / np.sqrt(2)
    R["env"] = (20 * np.log10(rms / ref)).astype("float16")   # (1500,)
    y = out["ess"]
    N = 1 << 19
    irs = [np.fft.irfft(np.fft.rfft(ch, N) * np.fft.rfft(S["ess_inv"], N), N) for ch in y]
    c = len(S["ess_inv"]) - 1                                  # linear IR peak position
    hs = []
    for h in range(1, 6):
        off = int(round(S["ess_L"] * np.log(h) * SR))
        st = c - off
        win = irs[0][max(0, st - 256):st + (2048 if h > 1 else 8192)]
        F = np.fft.rfft(win, 1 << 15)
        kk = np.clip(np.round(S["f_ess"] * (1 << 15) / SR).astype(int), 1, (1 << 14) - 1)
        hs.append(20 * np.log10(np.abs(F[kk]) + 1e-12))
    R["ess"] = np.array(hs, "float32")                         # (5, 200)
    if kind == "fx":
        R["ir"] = np.array([ir[c - 64:c - 64 + 4 * SR] for ir in irs], "float32")  # (ch, 4 s)
    return R


# ------------------------------------------------------------------ settings plan
def build_plan(params, kind):
    """params: list of dict(name, default, steps, boolean, discrete, text_at)"""
    use = [p for p in params if not SKIP.search(p["name"])]
    plan = [{}]                                                # 0 = defaults
    def positions(p):
        if p["boolean"]:
            return [0.0, 1.0]
        if p["discrete"] and 1 < p["steps"] <= 13:
            return [i / (p["steps"] - 1) for i in range(p["steps"])]
        return [i / 8 for i in range(9)]
    for p in use:                                              # one knob at a time
        for r in positions(p):
            if abs(r - p["default"]) > 1e-6:
                plan.append({p["name"]: r})
    low = {p["name"]: p["name"].lower() for p in use}
    def find(words):
        return [p for p in use if any(w in low[p["name"]] for w in words) and not p["boolean"]]
    pairs = []
    if kind in ("eq", "dyn", "color"):                         # EQ band gain x its freq / Q
        for g in find(["gain", "boost", "atten", "cut"]):
            stem = re.split(r"\s*(gain|boost|atten|cut)", low[g["name"]])[0].strip()
            if not stem:
                continue
            for p in use:
                lp = low[p["name"]]
                if p is not g and lp.startswith(stem) and any(w in lp for w in ("frq", "freq", " q", "bw", "band")):
                    pairs.append((g, p))
    if kind in ("dyn", "color"):                               # drive x ratio / attack / release
        drives = find(["input", "thresh", "peak", "compress", "comp thr", "drive", "sens", "amount", "rec"])
        others = find(["ratio", "attack", "release", "knee", "output", "makeup", "gain"])
        for d in drives[:3]:
            for o in others[:5]:
                if o is not d:
                    pairs.append((d, o))
    seen = set()
    for a, b in pairs[:14]:
        key = (a["name"], b["name"])
        if key in seen:
            continue
        seen.add(key)
        for ra in [i / 6 for i in range(7)]:
            for rb in [i / 4 for i in range(5)]:
                plan.append({a["name"]: ra, b["name"]: rb})
    return plan


# ------------------------------------------------------------------ child: one plugin
def run_plugin(folder, pname, kind):
    import numpy as np, soundfile as sf
    from pedalboard import load_plugin
    d = os.path.join(OUT, folder)
    os.makedirs(d, exist_ok=True)
    t0 = time.time()
    plugin = load_plugin(SHELL, plugin_name=pname, initialization_timeout=60.0)
    print(f"loaded in {time.time() - t0:.0f}s", flush=True)
    cp = {p.name: p for p in plugin._parameters}
    params = []
    for p in plugin._parameters:
        e = dict(name=p.name, default=float(p.default_raw_value), steps=int(p.num_steps),
                 boolean=bool(p.is_boolean), discrete=bool(p.is_discrete), text=p.string_value)
        try:
            e["text_at"] = {f"{r:.3f}": p.get_text_for_raw_value(r) for r in [i / 8 for i in range(9)]}
        except Exception:
            pass
        params.append(e)
    defaults = {p.name: float(p.raw_value) for p in plugin._parameters}
    planf = os.path.join(d, "plan.json")
    if os.path.exists(planf):
        plan = json.load(open(planf))["plan"]
    else:
        plan = build_plan(params, kind)
        json.dump(dict(plugin=pname, kind=kind, sr=SR, params=params, defaults=defaults, plan=plan),
                  open(planf, "w"), indent=1)
    done = set()
    for f in os.listdir(d):
        if f.startswith("chunk_") and f.endswith(".json"):
            done.update(json.load(open(os.path.join(d, f)))["ids"])
    todo = [i for i in range(len(plan)) if i not in done]
    print(f"plan {len(plan)} settings, {len(todo)} to do", flush=True)
    S = make_signals()
    sigs = ["ms-30", "ms-18", "ms-6", "lev", "env", "ess"]
    # Some Waves plugins rebuild themselves on every reset: test with and without reset, keep the faster
    x = np.concatenate([S["ms-18"], np.zeros(SR, "float32")])[None, :]
    BS, RESET = 8192, True
    timing = {}
    for rs in (True, False):
        t2 = time.perf_counter()
        try:
            plugin.process(x.copy(), SR, buffer_size=BS, reset=rs)
            timing[rs] = time.perf_counter() - t2
        except Exception:
            timing[rs] = None
        print(f"probe reset={rs}: {timing[rs] if timing[rs] is None else round(timing[rs], 3)} s", flush=True)
    ok = {k: v for k, v in timing.items() if v is not None}
    RESET = min(ok, key=ok.get) if ok else True
    print(f"using reset={RESET}", flush=True)
    est = (ok.get(RESET, 1.0) if ok else 1.0) * len(S["all"]) / len(x[0]) + 0.05
    cap = max(20, int(2 * 3600 / est))
    if len(todo) > cap:                     # too slow for the full plan in ~2 h: spread a subset now
        keep = sorted(set([todo[0]] + [todo[int(k * len(todo) / cap)] for k in range(cap)]))
        print(f"plan thinned to {len(keep)} of {len(todo)} settings (about {est:.1f} s per setting); "
              "run again later to fill in the rest", flush=True)
        todo = keep
    nchunk = len([f for f in os.listdir(d) if f.startswith("chunk_") and f.endswith(".json")])
    buf, ids, texts = {}, [], []
    t1 = time.time()

    def flush():
        nonlocal buf, ids, texts, nchunk
        if not ids:
            return
        np.savez_compressed(os.path.join(d, f"chunk_{nchunk:03d}.npz"), **{k: np.stack(v) for k, v in buf.items()})
        json.dump(dict(ids=ids, settings=[plan[i] for i in ids], texts=texts),
                  open(os.path.join(d, f"chunk_{nchunk:03d}.json"), "w"))
        nchunk += 1
        buf, ids, texts = {}, [], []

    for j, i in enumerate(todo):
        for name, raw in defaults.items():                     # back to defaults, then this setting
            if cp[name].raw_value != raw:
                try: cp[name].raw_value = raw
                except Exception: pass
        for name, raw in plan[i].items():
            try: cp[name].raw_value = raw
            except Exception: pass
        texts.append({name: cp[name].string_value for name in plan[i]})
        out = {}
        for k in sigs:                                  # separate renders, 1 s of silence after each
            xx = np.concatenate([S[k], np.zeros(SR, "float32")])[None, :]
            yy = np.atleast_2d(plugin.process(xx, SR, buffer_size=BS, reset=RESET))
            out[k] = yy[:, :len(S[k])]
        if i == 0:
            sf.write(os.path.join(d, "default_ms-18.wav"), out["ms-18"].T, SR, subtype="FLOAT")
            sf.write(os.path.join(d, "default_ess.wav"), out["ess"].T, SR, subtype="FLOAT")
        R = analyse(S, out, kind)
        for k, v in R.items():
            buf.setdefault(k, []).append(v)
        ids.append(i)
        if len(ids) >= CHUNK:
            flush()
        if (j + 1) % 25 == 0 or j + 1 == len(todo) or j < 2:
            rate = (j + 1) / (time.time() - t1)
            print(f"{j + 1}/{len(todo)} settings ({rate:.1f}/s)", flush=True)
            with open(os.path.join(HERE, "progress.txt"), "w") as f:
                f.write(f"{folder}: {j + 1}/{len(todo)}\n")
    flush()
    open(os.path.join(d, "DONE"), "w").write(time.strftime("%Y-%m-%d %H:%M:%S"))
    print("CHILD_OK", flush=True)



# ------------------------------------------------------------------ auto-cancel Waves pop-ups
# Waves starts every plugin in the shell while loading; Grand Rhapsody (no samples installed) then
# opens "Locate ... samples folder" and waits. This presses Cancel on dialogs that belong to OUR
# capture processes only (matched by process id), never on other windows.
WATCH_PIDS = set()
DISMISSED = [0]


def dialog_watcher():
    if os.name != "nt":
        return
    import ctypes
    from ctypes import wintypes as wt
    u = ctypes.windll.user32
    k = ctypes.windll.kernel32
    PROC = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    for f, at in ((u.EnumWindows, [PROC, wt.LPARAM]), (u.EnumChildWindows, [wt.HWND, PROC, wt.LPARAM]),
                  (u.GetClassNameW, [wt.HWND, wt.LPWSTR, ctypes.c_int]), (u.GetWindowTextW, [wt.HWND, wt.LPWSTR, ctypes.c_int]),
                  (u.GetWindowTextLengthW, [wt.HWND]), (u.IsWindowVisible, [wt.HWND]),
                  (u.GetWindowThreadProcessId, [wt.HWND, ctypes.POINTER(wt.DWORD)]),
                  (u.PostMessageW, [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM])):
        f.argtypes = at
    k.OpenProcess.restype = wt.HANDLE
    k.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    k.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, wt.LPWSTR, ctypes.POINTER(wt.DWORD)]
    k.CloseHandle.argtypes = [wt.HANDLE]
    WM_COMMAND, IDCANCEL, IDOK = 0x0111, 2, 1
    seen = {}

    def cls(h):
        b = ctypes.create_unicode_buffer(256); u.GetClassNameW(h, b, 256); return b.value

    def text(h):
        n = u.GetWindowTextLengthW(h); b = ctypes.create_unicode_buffer(n + 2); u.GetWindowTextW(h, b, n + 1); return b.value

    def exe(pid):
        hp = k.OpenProcess(0x1000, False, pid)
        if not hp:
            return ""
        b = ctypes.create_unicode_buffer(520); n = wt.DWORD(520)
        k.QueryFullProcessImageNameW(hp, 0, b, ctypes.byref(n)); k.CloseHandle(hp)
        return os.path.basename(b.value).lower()

    def check(h, _):
        try:
            if not u.IsWindowVisible(h) or cls(h) != "#32770":
                return True
            pid = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(pid))
            ours = pid.value in WATCH_PIDS or exe(pid.value).startswith("python")
            if not ours:
                return True
            texts = [text(h)]
            def kid(c, _):
                texts.append(text(c)); return True
            u.EnumChildWindows(h, PROC(kid), 0)
            blob = " ".join(texts).lower()
            if pid.value not in WATCH_PIDS and not any(w in blob for w in ("locate", "sample", "waves", "folder")):
                return True
            n = seen.get(h, 0)
            u.PostMessageW(h, WM_COMMAND, IDCANCEL if n < 2 else IDOK, 0)
            if n == 0:
                DISMISSED[0] += 1
                log("    auto-closed pop-up:", " | ".join(t for t in texts if t)[:120])
            seen[h] = n + 1
        except Exception as e:
            log("    watcher error:", e)
        return True

    cb = PROC(check)
    log("pop-up watcher running")
    while True:
        try:
            u.EnumWindows(cb, 0)
        except Exception as e:
            log("    watcher error:", e)
        time.sleep(0.5)


# ------------------------------------------------------------------ main
def main():
    os.makedirs(OUT, exist_ok=True)
    rank = {"eq": 0, "color": 1, "fx": 2, "dyn": 3}
    PLUGINS.sort(key=lambda p: rank[p[2]])
    only = sys.argv[1:]
    log("=== Waves capture batch:", len(PLUGINS), "plugins ===")
    threading.Thread(target=dialog_watcher, daemon=True).start()
    for n, (folder, pname, kind) in enumerate(PLUGINS, 1):
        if only and folder not in only:
            continue
        if os.path.exists(os.path.join(OUT, folder, "DONE")):
            log(f"[{n}/{len(PLUGINS)}] {pname}: already done")
            continue
        log(f"[{n}/{len(PLUGINS)}] {pname}: loading (about 3-4 min)...")
        t0 = time.time()
        p = subprocess.Popen([sys.executable, __file__, "--child", folder, pname, kind],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                             encoding="utf-8", errors="replace")
        WATCH_PIDS.add(p.pid)
        d0 = DISMISSED[0]
        timer = threading.Timer(3 * 3600, p.kill)
        timer.start()
        state = {"loaded": False}
        def load_guard():                    # a Waves load that hangs (hidden dialog / no licence) gets skipped
            if not state["loaded"] and p.poll() is None:
                log("    no load after 8 min (hidden Waves window or no licence?) - skipping")
                p.kill()
        guard = threading.Timer(12 * 60, load_guard)
        guard.start()
        ok = False
        tail = []
        for line in p.stdout:
            line = line.rstrip()
            if not line:
                continue
            if line.startswith("loaded"):
                state["loaded"] = True
            if line == "CHILD_OK":
                ok = True
                continue
            tail = (tail + [line])[-15:]
            if re.match(r"^(loaded|plan|probe|using|\d+/\d+ settings)", line):
                log("    ", line)
        p.wait()
        timer.cancel()
        guard.cancel()
        WATCH_PIDS.discard(p.pid)
        if DISMISSED[0] > d0:
            log(f"    closed {DISMISSED[0] - d0} Waves pop-up(s) automatically")
        if ok:
            log(f"    done in {(time.time() - t0) / 60:.1f} min")
        else:
            log(f"    FAILED (code {p.returncode}) - skipping. Last lines:")
            for l in tail:
                log("      ", l)
    log("=== batch finished ===")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--child":
        try:
            run_plugin(sys.argv[2], sys.argv[3], sys.argv[4])
        except Exception:
            traceback.print_exc()
            sys.exit(1)
    else:
        try:
            main()
        except Exception:
            log(traceback.format_exc())
