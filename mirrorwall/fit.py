"""Read a reference vocal and fit a MIRRORWALL preset onto a raw vocal."""
from __future__ import annotations

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import welch, butter, sosfiltfilt

from . import dsp, measure, tune

SR = int(dsp.SR)
CENTERS = 1000 * 2 ** (np.arange(-17, 15) / 3)
FIT_BAND = (CENTERS >= 45) & (CENTERS <= 17000)


def curve(x):
    m = x if x.ndim == 1 else x.mean(1)
    f, P = welch(m, SR, nperseg=8192)
    out = []
    for c in CENTERS:
        k = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(P[k].mean() + 1e-20) if k.any() else -200)
    out = np.array(out)
    ref = (CENTERS >= 250) & (CENTERS <= 4000)
    return out - out[ref].mean()


def eq_error(target, got, floor=-24.0):
    """Mean dB error over 45 Hz-17 kHz, treating anything below -24 dB as 'cut'."""
    d = np.maximum(target, floor) - np.maximum(got, floor)
    return float(np.mean(np.abs(d[FIT_BAND])))


def quick(st):
    mid = st.mean(1)
    side = (st[:, 0] - st[:, 1]) / 2
    width = 10 * np.log10((side ** 2).sum() / ((mid ** 2).sum() + 1e-20) + 1e-12)
    hop = int(0.05 * SR)
    n = len(mid) // hop
    rms = 20 * np.log10(np.sqrt((mid[: n * hop].reshape(n, hop) ** 2).mean(1)) + 1e-12)
    top = np.percentile(rms, 95)
    act = rms > top - 30
    tails = []
    for i in range(5, n - 40):
        if act[i - 5:i].all() and rms[i] > top - 12 and rms[i + 1] < rms[i] - 6 \
                and np.all(rms[i + 1:i + 40] < rms[i] - 3):
            tails.append(rms[i + 1:i + 30] - rms[i])
    tail = float(np.median(np.median(np.array(tails), 0)[6:20])) if tails else np.nan
    s = rms[act]
    spread = float(np.percentile(s, 95) - np.percentile(s, 10))
    return width, tail, spread


def crest(st):
    """Transient shape: median (peak - RMS) dB over active 50 ms frames. Fast compressors (FET) shave
    peaks and lower it; slow/opto ones let the front of each word through and keep it higher."""
    m = st if st.ndim == 1 else st.mean(1)
    hop = int(0.05 * SR)
    n = len(m) // hop
    if n < 4:
        return np.nan
    fr = m[: n * hop].reshape(n, hop)
    rms = 20 * np.log10(np.sqrt((fr ** 2).mean(1)) + 1e-12)
    pk = 20 * np.log10(np.abs(fr).max(1) + 1e-12)
    act = rms > np.percentile(rms, 95) - 20
    return float(np.median((pk - rms)[act])) if act.any() else np.nan


def sib_index(mono):
    sos = butter(4, [5000, 10000], "bandpass", fs=SR, output="sos")
    seg = mono[: SR * 90]
    sib = sosfiltfilt(sos, seg)
    h = int(0.01 * SR)
    k = len(sib) // h
    a = 20 * np.log10(np.sqrt((sib[: k * h].reshape(k, h) ** 2).mean(1)) + 1e-12)
    b = 20 * np.log10(np.sqrt((seg[: k * h].reshape(k, h) ** 2).mean(1)) + 1e-12)
    act = b > np.percentile(b, 95) - 30
    rel = (a - b)[act]
    return float(np.percentile(rel, 99) - np.percentile(rel, 50))


def target_from(ref_metrics_list):
    """Merge one or more reference measurements into a single target."""
    R = ref_metrics_list
    fin = lambda k: [r[k] for r in R if np.isfinite(r[k])]
    lags = [r["side_lag"] for r in R if r["side_corr"] > 0.25] or \
           [r["delay_lag"] for r in R if r["delay_corr"] > 0.2]
    return dict(
        curve=np.nanmedian([r["curve"] for r in R], 0),
        spread=float(np.median([r["spread"] for r in R])),
        sib=float(np.median([r["sib_peak"] for r in R])),
        width=float(np.median([r["width"] for r in R])),
        tail=float(np.median(fin("tail_level"))) if fin("tail_level") else -22.0,
        rt60=float(np.clip(np.median([v for v in fin("rt60")] or [1.6]), 0.6, 3.5)),
        within10=float(np.median([r["tune_within10"] for r in R])),
        floor=float(np.median([r["floor"] for r in R])),
        lag=float(np.median(lags)) if lags else 0.0,
        rms=float(np.median([r["rms"] for r in R])),
        crest=float(np.median([r["crest"] for r in R if np.isfinite(r.get("crest", np.nan))] or [np.nan])),
    )


def measure_ref(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True)
    m = measure.analyze(x, sr)
    if sr != SR:
        import librosa
        x = np.stack([librosa.resample(x[:, c], orig_sr=sr, target_sr=SR) for c in range(x.shape[1])], 1)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, 1)
    m["curve"] = curve(x)
    m["rms"] = dsp.active_rms_db(x)
    m["crest"] = crest(x)
    return m


def tune_defaults(within10):
    if within10 > 0.72:
        return dict(speed=8.0, amount=1.0, humanize=0.0)
    if within10 > 0.55:
        return dict(speed=30.0, amount=1.0, humanize=0.1)
    if within10 > 0.4:
        return dict(speed=60.0, amount=0.6, humanize=0.3)
    return dict(speed=80.0, amount=0.0, humanize=0.5)


def fit_eq(prev_eq, want_delta, hpf_grid=(0, 70, 100, 140, 190, 260, 350), lpf_grid=(20000, 16000, 12500)):
    """Find hpf, lpf and band gains whose curve best equals prev curve + delta."""
    want = dsp.eq_curve_db(prev_eq, CENTERS) + want_delta
    # below -24 dB it's simply "cut" — don't chase the exact depth of the hole
    floor = -24.0
    w = np.where(FIT_BAND, 1.0, 0.0)
    best = None
    for hp in hpf_grid:
        for lp in lpf_grid:
            def resid(g):
                eq = dict(hpf=hp, hpf_order=2, lpf=lp, gains=list(g))
                got = dsp.eq_curve_db(eq, CENTERS)
                return (np.maximum(got, floor) - np.maximum(want, floor)) * w
            g0 = np.array(prev_eq.get("gains", [0] * len(dsp.EQ_BANDS)), float)
            if len(g0) != len(dsp.EQ_BANDS):
                g0 = np.zeros(len(dsp.EQ_BANDS))
            r = least_squares(resid, g0, bounds=(-18, 18), max_nfev=200)
            cost = float(np.mean(np.abs(r.fun[FIT_BAND])))
            if best is None or cost < best[0]:
                best = (cost, dict(hpf=float(hp), hpf_order=2, lpf=float(lp),
                                   gains=[round(float(v), 2) for v in r.x]))
    return best[1], best[0]


def fit_preset(raw, T, name="Custom", pre_type="tube", log=print):
    """raw: mono float (tuned already if wanted). T: target dict. Returns preset."""
    P = dict(name=name, version=1,
             tune=tune_defaults(T["within10"]),
             pre=dict(type=pre_type, drive=8.0, mix=0.55),
             opto=dict(thr=-26.0, ratio=3.0, attack=10.0, rel_fast=60.0, rel_slow=1500.0),
             fet=dict(thr=-18.0, ratio=6.0, attack=0.4, release=80.0),
             eq=dict(hpf=0.0, hpf_order=2, lpf=20000.0, gains=[0.0] * len(dsp.EQ_BANDS)),
             deess=dict(thr=10.0, depth=1.0),
             gate=dict(thr=-60.0, range=0.0, hold=90.0, attack=2.0, release=70.0, on=False),
             dbl=dict(wet=0.0, side=0.0),
             slap=dict(ms=max(60.0, T["lag"] * 1000) if T["lag"] > 0 else 120.0,
                       wet=0.0, fb=0.0),
             verb=dict(rt60=T["rt60"], predelay=25.0, damp=7000.0, size=1.0, wet=0.1, width=1.0),
             out_rms_db=T["rms"], ceiling=-1.0)
    dry = lambda p: dict(p, dbl=dict(wet=0), slap=dict(p["slap"], wet=0), verb=dict(p["verb"], wet=0))

    # 1) compression: try all four measured styles (+ the classic opto->FET chain) and keep the one that
    #    lands the reference's loudness spread AND transient shape (crest) best
    base = dict(P, opto=dict(P["opto"], on=False), fet=dict(P["fet"], on=False),
                vca=dict(thr=-20.0, ratio=4.0, fast=False, release=300.0, on=False),
                lev=dict(comp=-20.0, ratio=6.0, knee=24.0, gate=-90.0, on=False))
    cands = []
    for ot in (-14, -18, -22, -26, -30, -60):                       # CLA-2A -> CLA-76 chain (1.x behaviour)
        for ft in (-8, -12, -16, -60):
            cands.append(("opto+fet", dict(base, opto=dict(P["opto"], thr=float(ot), on=ot > -60),
                                           fet=dict(P["fet"], thr=float(ft), on=ft > -60))))
    for ot in (-18, -22, -26, -30, -34):                            # CLA-2A alone
        cands.append(("opto (CLA-2A)", dict(base, opto=dict(P["opto"], thr=float(ot), on=True))))
    for ft in (-10, -14, -18, -22):                                 # CLA-76 alone
        cands.append(("fet (CLA-76)", dict(base, fet=dict(P["fet"], thr=float(ft), on=True))))
    for vt in (-10, -15, -20, -25):                                 # SSL G channel
        for fast in (False, True):
            cands.append(("vca (SSL)", dict(base, vca=dict(base["vca"], thr=float(vt), fast=fast, on=True))))
    for lc in (-8, -14, -20, -26):                                  # RVox
        cands.append(("lev (RVox)", dict(base, lev=dict(base["lev"], comp=float(lc), on=True))))
    have_crest = np.isfinite(T.get("crest", np.nan))
    best = None
    for style, p in cands:
        st = dsp.process(raw, dry(p))
        _, _, sp = quick(st)
        cr = crest(st) if have_crest else np.nan
        e = abs(sp - T["spread"]) + (0.75 * abs(cr - T["crest"]) if have_crest and np.isfinite(cr) else 0.0)
        if best is None or e < best[0]:
            best = (e, p, sp, cr, style)
    P = best[1]
    log(f"  compression: {best[4]}, spread {best[2]:.1f} (target {T['spread']:.1f})"
        + (f", crest {best[3]:.1f} (target {T['crest']:.1f})" if have_crest else ""))

    # 2) tone EQ, three refinement passes
    err = None
    for it in range(3):
        st = dsp.process(raw, dry(P))
        delta = np.where(FIT_BAND, T["curve"] - curve(st), 0.0)
        eq, err = fit_eq(P["eq"], 0.85 * delta)
        P = dict(P, eq=eq)
    # detail EQ: third-octave trims on what's left (two passes)
    for it in range(2):
        st = dsp.process(raw, dry(P))
        resid = np.maximum(T["curve"], -24) - np.maximum(curve(st), -24)
        want = np.interp(np.log(dsp.DETAIL_CENTERS), np.log(CENTERS), resid)
        prev = np.array(P["eq"].get("detail", [0.0] * len(dsp.DETAIL_CENTERS)))
        base = dict(P["eq"], detail=[0.0] * len(dsp.DETAIL_CENTERS))
        fc = np.array(dsp.DETAIL_CENTERS)
        def r_(g):
            e = dict(base, detail=list(g))
            got = dsp.eq_curve_db(e, fc) - dsp.eq_curve_db(base, fc)
            return got - (prev_curve + 0.9 * want)
        prev_curve = dsp.eq_curve_db(dict(base, detail=list(prev)), fc) - dsp.eq_curve_db(base, fc)
        r = least_squares(r_, np.clip(prev + 0.9 * want, -11.9, 11.9), bounds=(-12, 12), max_nfev=300)
        P = dict(P, eq=dict(P["eq"], detail=[round(float(v), 2) for v in r.x]))
    st = dsp.process(raw, dry(P))
    err = eq_error(T["curve"], curve(st))
    log(f"  EQ: mean error {err:.2f} dB")

    # 3) de-esser
    best = None
    for thr in (2, 5, 8, 11, 14, 17, 20, 24):
        p = dict(P, deess=dict(thr=float(thr), depth=1.0))
        sb = sib_index(dsp.process(raw, dry(p)).mean(1))
        e = abs(sb - T["sib"])
        if best is None or e < best[0]:
            best = (e, p, sb)
    P = best[1]
    log(f"  de-ess: sibilance {best[2]:.1f} (target {T['sib']:.1f})")

    # 4) gate: references that sit at digital silence between phrases were gated
    if T["floor"] < -90:
        P["gate"] = dict(P["gate"], thr=-48.0, range=80.0, on=True)
    elif T["floor"] < -60:
        P["gate"] = dict(P["gate"], thr=-50.0, range=35.0, on=True)

    # 5) space: doubler / slap / reverb to land width and tail
    best = None
    wide = T["width"] > -6
    for sw in ((0.0,) if T["lag"] <= 0 else (0.1, 0.2, 0.35, 0.5)):
        for vw in (0.02, 0.04, 0.07, 0.11, 0.16, 0.24):
            for dw in ((0.0, 0.25) if not wide else (0.6, 0.9)):
                for ds in ((0.0,) if not wide else (0.5, 1.0)):
                    p = dict(P, slap=dict(P["slap"], wet=sw), verb=dict(P["verb"], wet=vw),
                             dbl=dict(wet=dw, side=ds))
                    w, tl, _ = quick(dsp.process(raw, p))
                    e = abs(w - T["width"]) / 3 + (abs(tl - T["tail"]) / 10 if np.isfinite(tl) else 0.5) + vw * 2
                    if best is None or e < best[0]:
                        best = (e, p, w, tl)
    P = best[1]
    log(f"  space: width {best[2]:.1f} (target {T['width']:.1f}), tail {best[3]:.1f} (target {T['tail']:.1f})")
    P["fit"] = dict(eq_err=round(err, 2), width=round(best[2], 1), target_width=round(T["width"], 1))
    return P
