import json, numpy as np
from build_shelf import *
from sb_model import sb

d = json.load(open('shelf_tables.json'))


def lookup(kind, k, g):
    D = d[kind]; ks = np.array(D['ks']); gs = np.array(D['gs']); T = np.array(D['table'])
    k = float(np.clip(k, ks[0], ks[-1]))
    side = gs[gs < 0] if g < 0 else gs[gs > 0]
    off = 0 if g < 0 else int(np.sum(gs < 0))
    g = float(np.clip(g, side.min(), side.max()))
    i = min(np.searchsorted(ks, k, side='right') - 1, len(ks) - 2); a = (k - ks[i]) / (ks[i + 1] - ks[i])
    j = min(np.searchsorted(side, g, side='right') - 1, len(side) - 2); b = (g - side[j]) / (side[j + 1] - side[j]); j += off
    return (T[i, j] * (1 - a) + T[i + 1, j] * a) * (1 - b) + (T[i, j + 1] * (1 - a) + T[i + 1, j + 1] * a) * b


def check(kind):
    lo, hi, dg, dc = data(kind); kd = d[kind]['kd']
    rows = [('lo', i, c) for i, c in enumerate(lo)] + [('hi', i, c) for i, c in enumerate(hi)] + \
           [('dg', kd, c) for c in dg] + [('dc', kd, c) for c in dc]
    errs = []
    for name, k, c in rows:
        g, fh = plateau_corner(c, kind)
        e = float(np.max(np.abs(sb(kind, lookup(kind, k, g), G, 2) - c)))
        errs.append((name, round(float(k), 2), round(float(g), 2), round(e, 3)))
    w = max(x[3] for x in errs)
    print(kind, 'vs measured curves: worst %.3f dB, median %.3f' % (w, np.median([x[3] for x in errs])))
    print('   >0.25 dB:', [x for x in errs if x[3] > 0.25])


if __name__ == '__main__':
    check('L'); check('H')
