"""MIRRORWALL vocal chain — Python reference implementation.

Every stage is a causal, sample-by-sample process so the C++ twin in
mirrorwall_plugin/Source/Chain.h produces the same result and can later run
live. Keep the two files in step: same formulas, same constants, same order.

Order: input trim -> mic EQ -> preamp -> opto comp -> FET comp -> tone EQ
       -> de-esser -> gate -> [doubler, slap delay, FDN reverb] -> limiter -> output
"""
from __future__ import annotations

import math
import sys
import numpy as np
import numba

# numba's disk cache needs real source files; a frozen engine has none
CACHE = not getattr(sys, "frozen", False)

SR = 44100.0


# ------------------------------------------------------------------ biquads (RBJ cookbook)
def biquad_coeffs(kind, f, q, gain_db, sr=SR):
    A = 10 ** (gain_db / 40)
    w0 = 2 * math.pi * min(f, sr * 0.49) / sr
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * q)
    if kind == "peak":
        b0, b1, b2 = 1 + alpha * A, -2 * cw, 1 - alpha * A
        a0, a1, a2 = 1 + alpha / A, -2 * cw, 1 - alpha / A
    elif kind == "lowshelf":
        sa = 2 * math.sqrt(A) * alpha
        b0 = A * ((A + 1) - (A - 1) * cw + sa)
        b1 = 2 * A * ((A - 1) - (A + 1) * cw)
        b2 = A * ((A + 1) - (A - 1) * cw - sa)
        a0 = (A + 1) + (A - 1) * cw + sa
        a1 = -2 * ((A - 1) + (A + 1) * cw)
        a2 = (A + 1) + (A - 1) * cw - sa
    elif kind == "highshelf":
        sa = 2 * math.sqrt(A) * alpha
        b0 = A * ((A + 1) + (A - 1) * cw + sa)
        b1 = -2 * A * ((A - 1) + (A + 1) * cw)
        b2 = A * ((A + 1) + (A - 1) * cw - sa)
        a0 = (A + 1) - (A - 1) * cw + sa
        a1 = 2 * ((A - 1) - (A + 1) * cw)
        a2 = (A + 1) - (A - 1) * cw - sa
    elif kind == "hp":
        b0, b1, b2 = (1 + cw) / 2, -(1 + cw), (1 + cw) / 2
        a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
    elif kind == "lp":
        b0, b1, b2 = (1 - cw) / 2, 1 - cw, (1 - cw) / 2
        a0, a1, a2 = 1 + alpha, -2 * cw, 1 - alpha
    else:
        raise ValueError(kind)
    return np.array([b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0])


def biquad_db(c, freqs, sr=SR):
    w = 2 * np.pi * freqs / sr
    z = np.exp(-1j * w)
    num = c[0] + c[1] * z + c[2] * z * z
    den = 1 + c[3] * z + c[4] * z * z
    return 20 * np.log10(np.abs(num / den) + 1e-12)


@numba.njit(cache=CACHE)
def _biquad_run(x, c):
    y = np.empty_like(x)
    z1 = 0.0
    z2 = 0.0
    b0, b1, b2, a1, a2 = c[0], c[1], c[2], c[3], c[4]
    for i in range(len(x)):
        xi = x[i]
        yi = b0 * xi + z1
        z1 = b1 * xi - a1 * yi + z2
        z2 = b2 * xi - a2 * yi
        y[i] = yi
    return y


# Fixed band layout for the tone EQ (frequencies/Qs fixed, gains fitted).
EQ_BANDS = [
    ("lowshelf", 100.0, 0.707),
    ("peak", 160.0, 1.4),
    ("peak", 250.0, 1.4),
    ("peak", 400.0, 1.4),
    ("peak", 630.0, 1.4),
    ("peak", 1000.0, 1.4),
    ("peak", 1600.0, 1.4),
    ("peak", 2500.0, 1.4),
    ("peak", 4000.0, 1.4),
    ("peak", 6300.0, 1.4),
    ("peak", 9000.0, 1.4),
    ("highshelf", 12500.0, 0.707),
]


# Detail EQ: third-octave peaking bands (graphic-EQ style) for the fine match.
DETAIL_CENTERS = [50.0 * 2 ** (i / 3) for i in range(26)]      # 50 Hz .. ~15 kHz
DETAIL_Q = 4.32


def eq_sections(eq):
    """eq = {hpf, hpf_order(1|2), lpf, gains[9]} -> list of biquad coefficient arrays."""
    secs = []
    for _ in range(int(eq.get("hpf_order", 2))):
        if eq.get("hpf", 0) > 20:
            secs.append(biquad_coeffs("hp", eq["hpf"], 0.707, 0))
    for (k, f, q), g in zip(EQ_BANDS, eq["gains"]):
        secs.append(biquad_coeffs(k, f, q, g))
    if eq.get("lpf", 0) and eq["lpf"] < 20000:
        secs.append(biquad_coeffs("lp", eq["lpf"], 0.707, 0))
    for f, g in zip(DETAIL_CENTERS, eq.get("detail", [])):
        if g != 0.0:
            secs.append(biquad_coeffs("peak", f, DETAIL_Q, g))
    return secs


def eq_curve_db(eq, freqs):
    tot = np.zeros_like(freqs, dtype=float)
    for c in eq_sections(eq):
        tot += biquad_db(c, freqs)
    return tot


def run_eq(x, eq):
    for c in eq_sections(eq):
        x = _biquad_run(x, c)
    return x


# ------------------------------------------------------------------ preamp flavours
@numba.njit(cache=CACHE)
def _tube(x, drive_db, mix, bias):
    d = 10.0 ** (drive_db / 20.0)
    tb = math.tanh(bias)
    k = 1.0 / (1.0 - tb * tb)          # unity small-signal gain
    y = np.empty_like(x)
    for i in range(len(x)):
        z = (math.tanh(d * x[i] + bias) - tb) / d * k
        y[i] = (1 - mix) * x[i] + mix * z
    return y


@numba.njit(cache=CACHE)
def _console(x, drive_db, mix):
    d = 10.0 ** (drive_db / 20.0)
    y = np.empty_like(x)
    for i in range(len(x)):
        v = d * x[i]
        z = math.tanh(v + 0.15 * v * v * v / (1.0 + v * v)) / d
        y[i] = (1 - mix) * x[i] + mix * z
    return y


def preamp(x, p):
    t = p.get("type", "clean")
    if t == "tube":
        return _tube(x, p["drive"], p["mix"], 0.12)
    if t == "console":
        y = _console(x, p["drive"], p["mix"])
        # transformer colour: a touch of low weight, softened top
        y = _biquad_run(y, biquad_coeffs("lowshelf", 90.0, 0.707, 1.5))
        return _biquad_run(y, biquad_coeffs("highshelf", 15000.0, 0.707, -1.0))
    return x


# ------------------------------------------------------------------ dynamics
@numba.njit(cache=CACHE)
def _gr(over, ratio, knee):
    """Gain reduction (dB) with a soft knee - identical to Live.h grDb()."""
    s = 1.0 - 1.0 / max(1.0, ratio)
    if knee <= 0:
        return over * s if over > 0 else 0.0
    if 2 * over < -knee:
        return 0.0
    if 2 * abs(over) <= knee:
        t = over + knee / 2
        return s * t * t / (2 * knee)
    return over * s


@numba.njit(cache=CACHE)
def _opto(x, sr, thr, ratio, attack_ms, rel_fast_ms, rel_slow_ms):
    y = np.empty_like(x)
    ea = math.exp(-1.0 / (sr * 0.001))            # 1 ms detector attack
    er = math.exp(-1.0 / (sr * 0.050))            # 50 ms detector release
    ga = math.exp(-1.0 / (sr * attack_ms / 1000.0))
    rf = math.exp(-1.0 / (sr * rel_fast_ms / 1000.0))
    rs = math.exp(-1.0 / (sr * rel_slow_ms / 1000.0))
    ls = math.exp(-1.0 / (sr * 2.0))
    env = 0.0
    grs = 0.0
    hist = 0.0
    for i in range(len(x)):
        lv = abs(x[i])
        if lv > env:
            env = ea * env + (1 - ea) * lv
        else:
            env = er * env + (1 - er) * lv
        db = 20.0 * math.log10(env + 1e-9)
        gr = _gr(db - thr, ratio, 8.0)
        hist = ls * hist + (1 - ls) * gr
        if gr > grs:
            grs = ga * grs + (1 - ga) * gr
        else:
            r = rf + (rs - rf) * min(1.0, hist / 6.0)
            grs = r * grs + (1 - r) * gr
        y[i] = x[i] * 10.0 ** (-grs / 20.0)
    return y


@numba.njit(cache=CACHE)
def _fet(x, sr, thr, ratio, attack_ms, release_ms):
    y = np.empty_like(x)
    a = math.exp(-1.0 / (sr * attack_ms / 1000.0))
    r = math.exp(-1.0 / (sr * release_ms / 1000.0))
    env = 0.0
    for i in range(len(x)):
        lv = abs(x[i])
        if lv > env:
            env = a * env + (1 - a) * lv
        else:
            env = r * env + (1 - r) * lv
        db = 20.0 * math.log10(env + 1e-9)
        gr = _gr(db - thr, ratio, 4.0)
        y[i] = x[i] * 10.0 ** (-gr / 20.0)
    return y


@numba.njit(cache=CACHE)
def _vca(x, sr, thr_panel, ratio, fast, release_ms, knee):
    """SSL G-channel style (fitted to the Nov 2026 capture): RMS detector, curve ~15 dB below the panel
    threshold, very wide knee, auto 30 ms / fast 3 ms attack."""
    y = np.empty_like(x)
    ed = math.exp(-1.0 / (sr * 0.005))
    ga = math.exp(-1.0 / (sr * (0.003 if fast else 0.030)))
    gr_ = math.exp(-1.0 / (sr * release_ms / 1000.0))
    thr = thr_panel - 15.0
    p = 0.0
    grs = 0.0
    for i in range(len(x)):
        p = ed * p + (1 - ed) * x[i] * x[i]
        gr = _gr(10.0 * math.log10(p + 1e-18) - thr, ratio, knee)
        c = ga if gr > grs else gr_
        grs = c * grs + (1 - c) * gr
        y[i] = x[i] * 10.0 ** (-grs / 20.0)
    return y


@numba.njit(cache=CACHE)
def _lev(x, sr, comp_panel, ratio, knee, gate_db):
    """RVox style leveller (fitted to the Nov 2026 capture): curve ~12 dB below the panel value,
    ~6:1, 24 dB knee, automatic make-up, optional 1:2 downward expander."""
    y = np.empty_like(x)
    ed = math.exp(-1.0 / (sr * 0.005))
    ga = math.exp(-1.0 / (sr * 0.005))
    gr_ = math.exp(-1.0 / (sr * 0.250))
    eg = math.exp(-1.0 / (sr * 0.020))
    thr = comp_panel - 12.0
    makeup = 10.0 ** ((-comp_panel * (1.0 - 1.0 / ratio) * 0.85) / 20.0)
    p = 0.0
    grs = 0.0
    gg = 1.0
    for i in range(len(x)):
        p = ed * p + (1 - ed) * x[i] * x[i]
        lvl = 10.0 * math.log10(p + 1e-18)
        gr = _gr(lvl - thr, ratio, knee)
        c = ga if gr > grs else gr_
        grs = c * grs + (1 - c) * gr
        under = gate_db - lvl
        tg = 10.0 ** (-min(40.0, under) / 20.0) if under > 0 else 1.0
        gg = eg * gg + (1 - eg) * tg
        y[i] = x[i] * 10.0 ** (-grs / 20.0) * makeup * gg
    return y


@numba.njit(cache=CACHE)
def _deess(x, band, sr, thr, depth):
    y = np.empty_like(x)
    a_s = math.exp(-1.0 / (sr * 0.004))
    a_b = math.exp(-1.0 / (sr * 0.030))
    a_g = math.exp(-1.0 / (sr * 0.001))
    ps = 0.0
    pb = 0.0
    g = 1.0
    for i in range(len(x)):
        s = band[i]
        ps = a_s * ps + (1 - a_s) * s * s
        pb = a_b * pb + (1 - a_b) * x[i] * x[i]
        rel = 10.0 * math.log10((ps + 1e-18) / (pb + 1e-18))
        gr = rel + thr
        if gr < 0:
            gr = 0.0
        if gr > 24:
            gr = 24.0
        tg = 10.0 ** (-gr * depth / 20.0)
        g = a_g * g + (1 - a_g) * tg
        y[i] = (x[i] - s) + s * g
    return y


def deess(x, thr, depth=1.0):
    b = _biquad_run(x, biquad_coeffs("hp", 4500.0, 0.707, 0))
    b = _biquad_run(b, biquad_coeffs("hp", 4500.0, 0.707, 0))
    b = _biquad_run(b, biquad_coeffs("lp", 11000.0, 0.707, 0))
    return _deess(x, b, SR, thr, depth)


@numba.njit(cache=CACHE)
def _gate(x, sr, thr, range_db, hold_ms, attack_ms, release_ms):
    y = np.empty_like(x)
    ae = math.exp(-1.0 / (sr * 0.010))
    aa = math.exp(-1.0 / (sr * attack_ms / 1000.0))
    ar = math.exp(-1.0 / (sr * release_ms / 1000.0))
    floor = 10.0 ** (-range_db / 20.0)
    hold = int(hold_ms / 1000.0 * sr)
    p = 0.0
    cnt = 0
    g = 0.0
    for i in range(len(x)):
        p = ae * p + (1 - ae) * x[i] * x[i]
        db = 10.0 * math.log10(p + 1e-18)
        if db > thr:
            cnt = hold
        elif cnt > 0:
            cnt -= 1
        tg = 1.0 if cnt > 0 else floor
        c = aa if tg > g else ar
        g = c * g + (1 - c) * tg
        y[i] = x[i] * g
    return y


# ------------------------------------------------------------------ space
@numba.njit(cache=CACHE)
def _doubler(x, sr, wet, side, base_l, base_r, depth_ms, rate):
    n = len(x)
    out = np.zeros((n, 2))
    maxd = int((max(base_l, base_r) + depth_ms + 2) / 1000.0 * sr) + 2
    buf = np.zeros(maxd)
    w = 0
    ph = 0.0
    inc = 2 * math.pi * rate / sr
    for i in range(n):
        buf[w] = x[i]
        taps = np.empty(2)
        for ch in range(2):
            base = base_l if ch == 0 else base_r
            off = math.pi if ch == 1 else 0.0
            d = (base + depth_ms * math.sin(ph + off)) / 1000.0 * sr
            rp = w - d
            while rp < 0:
                rp += maxd
            i0 = int(rp)
            fr = rp - i0
            i1 = i0 + 1
            if i1 >= maxd:
                i1 -= maxd
            taps[ch] = buf[i0] * (1 - fr) + buf[i1] * fr
        mid = 0.5 * (taps[0] + taps[1])
        out[i, 0] = wet * (taps[0] - side * mid)
        out[i, 1] = wet * (taps[1] - side * mid)
        ph += inc
        if ph > 2 * math.pi:
            ph -= 2 * math.pi
        w += 1
        if w >= maxd:
            w = 0
    return out


@numba.njit(cache=CACHE)
def _slap(send, sr, ms, wet, fb):
    n = len(send)
    out = np.zeros(n)
    d = max(1, int(ms / 1000.0 * sr))
    buf = np.zeros(d)
    w = 0
    for i in range(n):
        v = buf[w]
        out[i] = v * wet
        buf[w] = send[i] + v * fb
        w += 1
        if w >= d:
            w = 0
    return out


def slap(x, ms, wet, fb=0.0):
    s = _biquad_run(x, biquad_coeffs("hp", 300.0, 0.707, 0))
    s = _biquad_run(s, biquad_coeffs("lp", 6000.0, 0.707, 0))
    r = _slap(s, SR, ms, wet, fb)
    out = np.zeros((len(x), 2))
    out[:, 1] = r
    out[:, 0] = r * 0.25              # a little bleed so it isn't hard-panned
    return out


FDN_DELAYS = np.array([1557, 1617, 1491, 1422, 1277, 1356, 1188, 1116], dtype=np.float64)


@numba.njit(cache=CACHE)
def _fdn(x, sr, rt60, predelay_ms, damp_hz, size, wet, width):
    n = len(x)
    N = 8
    lens = np.empty(N, dtype=np.int64)
    for k in range(N):
        lens[k] = int(FDN_DELAYS_C[k] * size * sr / 44100.0)
    maxl = 0
    for k in range(N):
        if lens[k] > maxl:
            maxl = lens[k]
    bufs = np.zeros((N, maxl + 1))
    idx = np.zeros(N, dtype=np.int64)
    gains = np.empty(N)
    for k in range(N):
        gains[k] = 10.0 ** (-3.0 * (lens[k] / sr) / rt60)
    lp = np.zeros(N)
    dc = math.exp(-2 * math.pi * damp_hz / sr)
    pd = max(1, int(predelay_ms / 1000.0 * sr))
    pbuf = np.zeros(pd)
    pw = 0
    out = np.zeros((n, 2))
    outs = np.empty(N)
    for i in range(n):
        xin = pbuf[pw]
        pbuf[pw] = x[i]
        pw += 1
        if pw >= pd:
            pw = 0
        s = 0.0
        for k in range(N):
            v = bufs[k, idx[k]]
            lp[k] = (1 - dc) * v + dc * lp[k]
            outs[k] = lp[k] * gains[k]
            s += outs[k]
        s *= 2.0 / N                                  # Householder reflection
        l = 0.0
        r = 0.0
        for k in range(N):
            fbk = outs[k] - s
            bufs[k, idx[k]] = xin * 0.35 + fbk
            idx[k] += 1
            if idx[k] >= lens[k]:
                idx[k] = 0
            if k % 2 == 0:
                l += outs[k]
            else:
                r += outs[k]
        m = 0.5 * (l + r)
        out[i, 0] = wet * (m + width * (l - m))
        out[i, 1] = wet * (m + width * (r - m))
    return out


FDN_DELAYS_C = FDN_DELAYS


def reverb(x, v):
    s = _biquad_run(x, biquad_coeffs("hp", 250.0, 0.707, 0))
    return _fdn(s, SR, v["rt60"], v.get("predelay", 25.0), v.get("damp", 7000.0),
                v.get("size", 1.0), v["wet"], v.get("width", 1.0))


# ------------------------------------------------------------------ limiter
@numba.njit(cache=CACHE)
def _limiter(st, sr, ceiling_db, release_ms):
    n = st.shape[0]
    la = int(0.002 * sr)
    c = 10.0 ** (ceiling_db / 20.0)
    r = math.exp(-1.0 / (sr * release_ms / 1000.0))
    out = np.zeros_like(st)
    need = np.ones(n + la)
    for i in range(n):
        pk = max(abs(st[i, 0]), abs(st[i, 1]))
        need[i] = c / pk if pk > c else 1.0
    g = 1.0
    for i in range(n):
        m = 1.0
        for j in range(i, i + la + 1):
            if j < n and need[j] < m:
                m = need[j]
        if m < g:
            g = m
        else:
            g = r * g + (1 - r) * m
        out[i, 0] = st[i, 0] * g
        out[i, 1] = st[i, 1] * g
    return out


# ------------------------------------------------------------------ level helpers
def active_rms_db(x):
    """RMS of the louder half of 50 ms frames (ignores gaps) — same in C++."""
    hop = int(0.05 * SR)
    m = x if x.ndim == 1 else x.mean(1)
    n = len(m) // hop
    if n == 0:
        return -120.0
    fr = (m[: n * hop].reshape(n, hop) ** 2).mean(1)
    fr = np.sort(fr)[n // 2:]
    return float(10 * np.log10(fr.mean() + 1e-20))


IN_REF_DB = -20.0


# ------------------------------------------------------------------ the chain
def process(x, P, macros=None, return_parts=False):
    """x: mono float64 (already tuned if wanted). P: preset dict. Returns stereo."""
    m = dict(comp=1.0, reverb=1.0, delay=1.0, fx=1.0, deess=1.0, drive=0.0, out=0.0, gate=1.0)
    if macros:
        m.update(macros)
    x = np.asarray(x, dtype=np.float64)
    x = x * 10 ** ((IN_REF_DB - active_rms_db(x)) / 20)
    if P.get("mic_eq"):
        x = run_eq(x, P["mic_eq"])
    pre = dict(P.get("pre", {"type": "clean"}))
    if pre.get("type", "clean") != "clean":
        pre["drive"] = pre.get("drive", 6.0) + m["drive"]
        x = preamp(x, pre)
    shift = (1.0 - m["comp"]) * 10.0          # macro: +/- 10 dB threshold
    o = P.get("opto")
    if o and o.get("on", True):
        x = _opto(x, SR, o["thr"] + shift, o.get("ratio", 3.0), o.get("attack", 10.0),
                  o.get("rel_fast", 60.0), o.get("rel_slow", 1500.0))
    f = P.get("fet")
    if f and f.get("on", True):
        x = _fet(x, SR, f["thr"] + shift, f.get("ratio", 6.0), f.get("attack", 0.4), f.get("release", 80.0))
    c = P.get("vca")
    if c and c.get("on", True):
        x = _vca(x, SR, c["thr"] + shift, c.get("ratio", 4.0), bool(c.get("fast", False)),
                 c.get("release", 300.0), c.get("knee", 45.0))
    lv = P.get("lev")
    if lv and lv.get("on", True):
        x = _lev(x, SR, lv["comp"] + shift, lv.get("ratio", 6.0), lv.get("knee", 24.0), lv.get("gate", -90.0))
    if P.get("eq"):
        x = run_eq(x, P["eq"])
    d = P.get("deess")
    if d and d.get("on", True):
        x = deess(x, d["thr"] + (m["deess"] - 1.0) * 6.0, d.get("depth", 1.0))
    g = P.get("gate")
    if g and g.get("on", True) and m["gate"] > 0:
        x = _gate(x, SR, g["thr"], g.get("range", 60.0) * m["gate"], g.get("hold", 90.0),
                  g.get("attack", 2.0), g.get("release", 70.0))
    st = np.stack([x, x], 1)
    db = P.get("dbl")
    if db and db.get("wet", 0) * m["fx"] > 0:
        w = db["wet"] * m["fx"]
        st = st * (1 - 0.5 * min(w, 1.0)) + _doubler(x, SR, w, db.get("side", 0.0), 14.0, 22.0, 2.5, 0.35)
    s = P.get("slap")
    if s and s.get("wet", 0) * m["delay"] > 0:
        st = st + slap(x, s["ms"], s["wet"] * m["delay"], s.get("fb", 0.0))
    v = P.get("verb")
    if v and v.get("wet", 0) * m["reverb"] > 0:
        st = st + reverb(x, dict(v, wet=v["wet"] * m["reverb"]))
    gain = 10 ** ((P.get("out_rms_db", -18.0) - active_rms_db(st) + m["out"]) / 20)
    st = st * gain
    st = _limiter(st, SR, P.get("ceiling", -1.0), 60.0)
    return st
