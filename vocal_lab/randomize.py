"""Randomize: generate, score, lock & reroll, mutate.

The "shopping" engine. Note choices are instant; audio comes from a pitch-shift
bank built once per song, so a reroll is just a new plan + a splice.
"""
from dataclasses import asdict, replace

import numpy as np

from . import harmony as H
from . import melody as M

# style families the randomizer draws Feel settings from
STYLES = {
    'floating': dict(side='up', movement=(0.2, 0.5), contrary=(0.4, 0.8), split=(0.3, 0.6), hold=(0.1, 0.25)),
    'busy':     dict(side='up', movement=(0.6, 0.9), contrary=(0.3, 0.7), split=(0.7, 0.95), spice=(0.3, 0.6)),
    'low':      dict(side='down', movement=(0.3, 0.6), contrary=(0.6, 0.95), hold=(0.2, 0.4), lo=3, hi=12),
    'colour':   dict(side='up', colour=(0.6, 1.0), movement=(0.3, 0.6)),
    'dark':     dict(side='up', flavour=('phrygian', 'harmonic'), flavour_rate=(0.5, 0.9), split=(0.6, 0.9)),
}


def _draw_feel(style, rng):
    kw = {}
    for k, v in STYLES[style].items():
        if isinstance(v, tuple) and len(v) == 2 and all(isinstance(x, float) for x in v):
            kw[k] = float(rng.uniform(*v))
        elif isinstance(v, tuple):
            kw[k] = str(rng.choice(v))
        else:
            kw[k] = v
    return M.Feel(**kw)


def score(ev, notes, key, chords=None):
    """Theory score (higher is better) plus the parts it was built from."""
    sung = [e for e in ev if e['target'] is not None]
    if len(sung) < 3:
        return -1.0, {}
    ons = [e for e in sung if e['onset']]
    chord_fit = np.mean([e['target'] % 12 in (H.chord_at(chords or [], e['start']) or set())
                         for e in ons]) if chords else 0.5
    leaps = np.array([abs(b['target'] - a['target']) for a, b in zip(sung, sung[1:])])
    smooth = float(np.mean(leaps <= 4))
    big = float(np.mean(leaps > 7))
    contr = n = 0
    for a, b in zip(sung, sung[1:]):
        ld = np.sign(b['src'] - a['src'])
        if ld:
            n += 1
            contr += np.sign(b['target'] - a['target']) == -ld
    contrary = contr / max(n, 1)
    # independence: how often it's NOT just a parallel third
    indep = np.mean([e['target'] != key.step(e['src'], 2 if e['target'] > e['src'] else -2)
                     for e in ons])
    variety = min(len({e['target'] for e in sung}) / 8, 1.0)
    parts = dict(chord_fit=chord_fit, smooth=smooth, contrary=contrary,
                 independence=indep, variety=variety, big_leaps=big)
    s = (1.6 * chord_fit + 1.0 * smooth + 0.8 * contrary + 0.8 * indep
         + 0.5 * variety - 1.5 * big)
    return float(s), {k: round(float(v), 2) for k, v in parts.items()}


def generate(notes, key, chords=None, n=8, styles=None, seed=0):
    """n candidates across the chosen styles, best first."""
    rng = np.random.default_rng(seed)
    styles = styles or list(STYLES)
    out = []
    for i in range(n * 3):                         # over-generate, keep the best n
        st = styles[i % len(styles)]
        feel = _draw_feel(st, rng)
        sd = int(rng.integers(1e9))
        ev = M.line(notes, key, feel, chords, sd)
        sc, parts = score(ev, notes, key, chords)
        out.append(dict(style=st, feel=feel, seed=sd, events=ev, score=sc, parts=parts))
    out.sort(key=lambda c: -c['score'])
    # keep variety: at most ceil(n/len(styles))+1 per style
    cap = int(np.ceil(n / len(styles))) + 1
    picked, per = [], {}
    for c in out:
        if per.get(c['style'], 0) < cap:
            picked.append(c)
            per[c['style']] = per.get(c['style'], 0) + 1
        if len(picked) == n:
            break
    return picked


def mutate(cand, notes, key, chords=None, amount=0.3, seed=0):
    """Similar-but-different: nudge the Feel numbers, new note draw."""
    rng = np.random.default_rng(seed)
    f = asdict(cand['feel'])
    for k, v in f.items():
        if isinstance(v, float) and k not in ('focus', 'split_min'):
            f[k] = float(np.clip(v + rng.normal(0, amount * 0.3), 0, 1))
    feel = M.Feel(**f)
    sd = int(rng.integers(1e9))
    ev = M.line(notes, key, feel, chords, sd)
    sc, parts = score(ev, notes, key, chords)
    return dict(style=cand['style'] + '~', feel=feel, seed=sd, events=ev, score=sc, parts=parts)


def lock_reroll(cand, locked, notes, key, chords=None, tries=12, seed=0):
    """Keep the events inside locked (start, end) time ranges; reroll the rest,
    keeping the best-scoring reroll whose seams don't leap more than a 4th."""
    rng = np.random.default_rng(seed)

    def is_locked(e):
        return any(a <= e['start'] < b for a, b in locked)

    best = None
    for _ in range(tries):
        new = M.line(notes, key, cand['feel'], chords, int(rng.integers(1e9)))
        # events align 1:1 only when splits match; rebuild by lead-note group
        kept = [e for e in cand['events'] if is_locked(e)]
        kept_groups = {e['group'] for e in kept}
        merged = sorted(kept + [e for e in new if e['group'] not in kept_groups],
                        key=lambda e: e['start'])
        sung = [e for e in merged if e['target'] is not None]
        seams = [abs(b['target'] - a['target']) for a, b in zip(sung, sung[1:])
                 if is_locked(a) != is_locked(b)]
        if seams and max(seams) > 5:
            continue
        sc, parts = score(merged, notes, key, chords)
        if best is None or sc > best['score']:
            best = dict(style=cand['style'] + '+lock', feel=cand['feel'], seed=None,
                        events=merged, score=sc, parts=parts)
    return best or cand
