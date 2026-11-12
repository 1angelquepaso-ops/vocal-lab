# Builds the G-mode shelf tables (LF + HF) for MEX-EQ from the Edison captures.
# Model per table node: H(s) = k * (s^2 + s w1/q1 + w1^2)/(s^2 + s w2/q2 + w2^2) * (s + w3)/(s + w4)
from fitlib import *
from scipy.optimize import least_squares
import json
from sb_model import sb

U = '/mnt/user-data/uploads/Documents/MEX-EQ/captures/'
S = {f: segments(U + f) for f in ['E2_geq_bands_takes.wav', 'E3_geq_hf_hp_div3.wav', 'E6_gch_lp_shelves.wav']}
seg = lambda f, k: S[f][k][2]
LX = np.log(G)
GX = np.geomspace(5, 80000, 400); LGX = np.log(GX)
MASK = (GX > 20) & (GX < 20000)
XQ = np.linspace(-9, 9, 721)


def gen7(p, w):
    lk, lw1, lq1, lw2, lq2, lw3, lw4 = p; s = 1j * w
    w1, w2, q1, q2, w3, w4 = np.exp([lw1, lw2, lq1, lq2, lw3, lw4])
    H = np.exp(lk) * (s * s + s * w1 / q1 + w1 * w1) / (s * s + s * w2 / q2 + w2 * w2) * (s + w3) / (s + w4)
    return 20 * np.log10(np.abs(H))


def plateau_corner(c, kind):
    g = c[-1] if kind == 'H' else c[0]; h = g / 2
    if kind == 'H':
        i = np.where(np.abs(c) >= abs(h))[0][0]; j = i - 1
    else:
        i = np.where(np.abs(c) >= abs(h))[0][-1]; j = i + 1
    a, b = (j, i) if c[i] > c[j] else (i, j)
    x = np.interp(h, [c[a], c[b]], [LX[a], LX[b]])
    return g, np.exp(x)


def norm_shape(c, kind):
    g, fh = plateau_corner(c, kind)
    Sx = np.interp(XQ, LX - np.log(fh), c / g, left=(c / g)[0], right=(c / g)[-1])
    return g, np.log(fh), Sx


def data(kind):
    if kind == 'H':
        lo = [seg('E6_gch_lp_shelves.wav', k) for k in range(14, 24)]
        hi = [seg('E3_geq_hf_hp_div3.wav', k) for k in range(2, 11)]
        dg = [seg('E2_geq_bands_takes.wav', k) for k in range(53, 58)]
        dc = [seg('E3_geq_hf_hp_div3.wav', k) for k in range(12, 17)]
    else:
        lo = [seg('E6_gch_lp_shelves.wav', k) for k in range(3, 13)]
        hi = [seg('E2_geq_bands_takes.wav', k) for k in range(43, 52)]
        dg = [seg('E2_geq_bands_takes.wav', k) for k in range(36, 41)]
        dc = [seg('E3_geq_hf_hp_div3.wav', k) for k in range(18, 23)]
    return lo, hi, dg, dc


class Model:
    def __init__(self, kind):
        self.kind = kind
        lo, hi, dg, dc = data(kind)
        LO = [norm_shape(c, kind) for c in lo]; HI = [norm_shape(c, kind) for c in hi]
        self.lo_g = np.array([x[0] for x in LO]); self.lo_lf = np.array([x[1] for x in LO]); self.lo_S = np.array([x[2] for x in LO])
        self.hi_g = np.array([x[0] for x in HI]); self.hi_lf = np.array([x[1] for x in HI]); self.hi_S = np.array([x[2] for x in HI])
        while len(self.hi_lf) < len(self.lo_lf):      # extend hi sweep by the lo spacing
            i = len(self.hi_lf)
            self.hi_lf = np.append(self.hi_lf, self.hi_lf[-1] + self.lo_lf[i] - self.lo_lf[i - 1])
            self.hi_S = np.vstack([self.hi_S, self.hi_S[-1]]); self.hi_g = np.append(self.hi_g, self.hi_g[-1])
        self.DG = sorted([norm_shape(c, kind) for c in dg], key=lambda x: abs(x[0]))
        self.DC = sorted([norm_shape(c, kind) for c in dc], key=lambda x: abs(x[0]))
        self.fn = np.exp(self.lo_lf)
        if kind == 'H':
            self.kd = 5.0
        else:   # LF default position: where the low-gain sweep corner equals the default-position +4 dB corner
            self.kd = float(np.interp(self.DG[1][1], self.lo_lf, np.arange(len(self.lo_lf))))
        self.n = len(self.lo_lf)
        P = self.pos
        self.gA, self.gB = P(self.lo_g, self.kd), P(self.hi_g, self.kd)
        self.lfA_d, self.SA_d = P(self.lo_lf, self.kd), P(self.lo_S, self.kd)
        self.lfB_d, self.SB_d = P(self.hi_lf, self.kd), P(self.hi_S, self.kd)
        self.dS = self.SB_d - self.SA_d

    def pos(self, arr, k):
        k = float(np.clip(k, 0, len(arr) - 1)); i = min(int(np.floor(k)), len(arr) - 2); t = k - i
        return arr[i] * (1 - t) + arr[i + 1] * t

    def fam(self, F, a):
        gs = np.array([abs(x[0]) for x in F])
        if a <= gs[0]: return F[0][1], F[0][2]
        if a >= gs[-1]:
            t = (a - gs[-1]) / (gs[-1] - gs[-2]); return F[-1][1] + t * (F[-1][1] - F[-2][1]), F[-1][2]
        i = np.searchsorted(gs, a) - 1; t = (a - gs[i]) / (gs[i + 1] - gs[i])
        return F[i][1] * (1 - t) + F[i + 1][1] * t, F[i][2] * (1 - t) + F[i + 1][2] * t

    def boost(self, k, a):
        P = self.pos
        lfA, SA, gAk = P(self.lo_lf, k), P(self.lo_S, k), P(self.lo_g, k)
        lfB, SB, gBk = P(self.hi_lf, k), P(self.hi_S, k), P(self.hi_g, k)
        u = (a - gAk) / (gBk - gAk)
        if u <= 0:
            lf_d, S_d = self.fam(self.DG, max(self.gA * a / gAk, 0.3))
            return lfA + (lf_d - self.lfA_d), SA + (S_d - self.SA_d)
        ad = self.gA + u * (self.gB - self.gA)
        lf_d, S_d = self.fam(self.DG, ad)
        if u >= 1:
            return lfB + (lf_d - self.lfB_d), SB + (S_d - self.SB_d)
        wS = float(np.dot(S_d - self.SA_d, self.dS) / np.dot(self.dS, self.dS))
        wl = (lf_d - self.lfA_d) / (self.lfB_d - self.lfA_d)
        return lfA + wl * (lfB - lfA), SA + wS * (SB - SA)

    def target(self, k, g):
        if g > 0:
            lf, Sx = self.boost(k, g)
        else:
            lfc, Sc = self.fam(self.DC, -g); lfb, Sb = self.boost(k, -g); lfd, Sd = self.boost(self.kd, -g)
            lf, Sx = lfc + (lfb - lfd), Sc + (Sb - Sd)
        return g * np.interp(LGX - lf, XQ, Sx)


def build(kind):
    M = Model(kind)
    ks = np.arange(0, M.n - 1 + 1e-9, 0.125)
    gs = np.concatenate([-np.arange(21.5, 0.24, -0.5), np.arange(0.25, 21.6, 0.5)])
    table = np.zeros((len(ks), len(gs), 9)); errs = np.zeros((len(ks), len(gs)))
    kd_i = int(np.argmin(abs(ks - M.kd)))
    korder = [kd_i] + [i for d in range(1, len(ks)) for i in (kd_i - d, kd_i + d) if 0 <= i < len(ks)]

    def fit(k, g, x0, w):
        T = M.target(k, g); ref = np.array(x0)
        lb = [np.log(5), -30, np.log(0.15), np.log(8), -6, np.log(0.2), np.log(8), -6, np.log(0.2)]
        ub = [np.log(60000), 30, np.log(3), np.log(60000), 6, np.log(3), np.log(60000), 6, np.log(3)]
        x0 = np.clip(x0, np.array(lb) + 1e-6, np.array(ub) - 1e-6)
        reg = lambda p: np.concatenate([w * (p - ref), 0.03 * np.array([p[4], p[7]])])
        r = least_squares(lambda p: np.concatenate([sb(kind, p, GX, 2) - T, reg(p)]), x0, bounds=(lb, ub))
        return r.x, float(np.max(np.abs(sb(kind, r.x, GX[MASK], 2) - T[MASK])))

    for side in (-1, 1):
        cols = sorted([j for j in range(len(gs)) if np.sign(gs[j]) == side], key=lambda j: -abs(gs[j]))
        prev_col = None
        for j in cols:
            g = gs[j]
            for n, ki in enumerate(korder):
                k = ks[ki]
                if prev_col is not None:
                    x0 = prev_col[ki]
                elif n == 0:
                    best = None
                    _, fh = plateau_corner(np.interp(LX, LGX, M.target(k, g)), kind)
                    for qm in [0.5, 0.8]:
                        for fbm in ([0.3, 3] if kind == 'H' else [3, 0.3]):
                            xs = [np.log(fh), g, np.log(qm), np.log(fh * fbm), -0.5, np.log(.7), np.log(fh / fbm), 0.3, np.log(.7)]
                            x, e = fit(k, g, xs, 0.0)
                            if best is None or e < best[1]: best = (x, e)
                    x0 = best[0]
                else:
                    nb = ki - 1 if ki > kd_i else ki + 1
                    x0 = table[nb, j]
                x, e = fit(k, g, x0, 0.1)
                table[ki, j] = x; errs[ki, j] = e
            prev_col = table[:, j].copy()
    # mid-cell interpolation check
    mid = []
    for ki in range(len(ks) - 1):
        for j in range(len(gs) - 1):
            if np.sign(gs[j]) != np.sign(gs[j + 1]): continue
            p = (table[ki, j] + table[ki + 1, j] + table[ki, j + 1] + table[ki + 1, j + 1]) / 4
            T = M.target((ks[ki] + ks[ki + 1]) / 2, (gs[j] + gs[j + 1]) / 2)
            mid.append(np.max(np.abs(sb(kind, p, GX[MASK], 2) - T[MASK])))
    mid = np.array(mid)
    print(kind, 'nodes: worst %.3f dB 95pct %.3f | mid-cell: worst %.3f 95pct %.3f' % (errs.max(), np.percentile(errs, 95), mid.max(), np.percentile(mid, 95)))
    return M, dict(kind=kind, ks=ks.tolist(), gs=gs.tolist(), fn=M.fn.tolist(), kd=M.kd, table=table.tolist())


if __name__ == '__main__':
    out = {}
    for kind in ['L', 'H']:
        M, d = build(kind); out[kind] = d
        print(kind, 'nominal corners', np.round(d['fn']).tolist(), 'default k %.2f' % d['kd'])
    json.dump(out, open('shelf_tables.json', 'w'))
