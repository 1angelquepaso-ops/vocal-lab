"""Engine entry point for the plugin (v2).

Every call prints one JSON object on stdout. While working it also writes a
one-line status to <session>/progress.txt ("Building voices 3/7") that the
plugin shows in its progress bar.

Session folder:
  meta.json              key, notes, chords, chunk info
  vocal.wav              the chunk (mono)
  bank/iv_<n>.npz        pitch-shifted pieces for interval n (only the sung parts)
  opt_<k>.json           {"style": ..., "plans": {voice: [[start, end, interval, target], ...]}}
  opt_<k>_<voice>.wav    that voice as a DRY stem (same length as the chunk)

Commands
  analyze  --session D --vocal V [--beat B] [--key auto|B] [--scale minor]
  generate --session D [--count 1] [--voices 1-4] [--style mix|classic|colour|...]
  reroll   --session D --option K --voice NAME
  add      --session D --option K
  remove   --session D --option K --voice NAME
"""
import argparse
import glob
import json
import os
import shutil
import sys
import time

import numpy as np
import soundfile as sf

from . import core, harmony as H, choir as C, melody as M, randomize as R

LAYOUT_FOR = {2: 'around2', 3: 'wide3', 4: 'full4'}
ADD_ORDER = ['mid', 'high', 'low', 'bass']
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])

# style -> (single-line Feel overrides, choir colour, label)
STYLES = {
    'classic':       (dict(side='up', movement=0.3, colour=0.0, contrary=0.5), 0.0, 'Classic'),
    'colour':        (dict(side='up', movement=0.4, colour=0.9), 0.9, 'Colour 9ths'),
    'floating':      (dict(side='up', movement=0.3, contrary=0.7, split=0.45, hold=0.2), 0.3, 'Floating'),
    'busy':          (dict(side='up', movement=0.8, split=0.9, spice=0.45, hold=0.05), 0.4, 'Busy'),
    'low':           (dict(side='down', movement=0.45, contrary=0.85, hold=0.3, lo=3, hi=12), 0.3, 'Low counter'),
    'dark_phrygian': (dict(side='up', flavour='phrygian', flavour_rate=0.8, split=0.85), 0.3, 'Dark Phrygian'),
    'dark_harmonic': (dict(side='up', flavour='harmonic', flavour_rate=0.8, split=0.85), 0.3, 'Dark harmonic'),
}
SIMILAR = 0.75          # options sharing more than this many notes count as repeats


def _out(obj):
    sys.stdout.write(json.dumps(obj) + '\n')
    sys.stdout.flush()


def _progress(d, text):
    try:
        with open(os.path.join(d, 'progress.txt'), 'w') as f:
            f.write(text)
    except OSError:
        pass


def _load(d):
    meta = json.load(open(os.path.join(d, 'meta.json')))
    y, sr = sf.read(os.path.join(d, 'vocal.wav'), dtype='float32')
    key = H.Key(meta['key'], meta['scale'])
    chords = [(s, e, H.make_chord(pcs, root)) for s, e, pcs, root in meta['chords']]
    return meta, y, sr, key, chords


# ---------------------------------------------------------------- the bank
def _needed(plans, pad=0.15):
    """interval -> [(start, end)] seconds where some voice sings it."""
    need = {}
    for plan in plans.values():
        for s, e, iv, _ in plan:
            need.setdefault(iv, []).append((s - pad, e + pad))
    return need


def _bank(d, y, sr, plans, label=''):
    """Full-length arrays for every interval the plans use, shifting only the
    sung regions that aren't cached yet."""
    os.makedirs(os.path.join(d, 'bank'), exist_ok=True)
    need = _needed(plans)
    bank, todo = {}, []
    for iv, regs in need.items():
        path = os.path.join(d, 'bank', f'iv_{iv}.npz')
        cached = []
        if os.path.exists(path):
            z = np.load(path)
            cached = [(int(a), z[f'c{i}']) for i, a in enumerate(z['starts'])]
        covered = [(a / sr, (a + len(c)) / sr) for a, c in cached]
        missing = [(s, e) for s, e in regs
                   if not any(cs <= s and e <= ce for cs, ce in covered)]
        todo.append((iv, path, cached, missing))
    jobs = [t for t in todo if t[3] and t[0] != 0]
    for j, (iv, path, cached, missing) in enumerate(todo):
        if missing and iv != 0:
            _progress(d, f'{label}Building voices {jobs.index(todo[j]) + 1}/{len(jobs)}')
            cached = cached + core.shift_regions(y, sr, iv, missing)
            np.savez(path, starts=np.array([a for a, _ in cached]),
                     **{f'c{i}': c for i, (_, c) in enumerate(cached)})
        full = y.copy() if iv == 0 else np.zeros(len(y), dtype='float32')
        if iv != 0:
            for a, c in cached:
                full[a:a + len(c)] = c[:len(full) - a]
        bank[iv] = full
    return bank


def _write_option(d, k, plans, style, y, sr, label=''):
    json.dump({'style': style, 'plans': plans}, open(os.path.join(d, f'opt_{k}.json'), 'w'))
    for f in glob.glob(os.path.join(d, f'opt_{k}_*.wav')):
        os.remove(f)
    bank = _bank(d, y, sr, plans, label)
    voices = []
    for j, (name, plan) in enumerate(plans.items()):
        evs = [dict(start=s, end=e, src=0, target=iv, group=i, onset=True)
               for i, (s, e, iv, t) in enumerate(plan)]
        stem = M.splice_groups(bank, len(y), sr, M.to_segments(evs),
                               np.random.default_rng(k * 10 + j), base_ms=12 + 6 * j,
                               spread_ms=10, level_jitter_db=0.8)
        stem = np.clip(stem, -1, 1)
        path = os.path.join(d, f'opt_{k}_{name}.wav')
        sf.write(path, stem, sr, subtype='PCM_24')
        voices.append({'name': name, 'wav': path, 'notes': len(plan)})
    return {'option': k, 'style': style, 'label': STYLES.get(style, (0, 0, style))[2],
            'voices': voices}


def _read_option(d, k):
    j = json.load(open(os.path.join(d, f'opt_{k}.json')))
    return j.get('style', 'classic'), {n: [tuple(p) for p in pl] for n, pl in j['plans'].items()}


# ------------------------------------------------------------- generation
def _line_plan(notes, key, chords, style, seed):
    feel = M.Feel(**STYLES[style][0])
    ev = M.line(notes, key, feel, chords, seed)
    return {'line': [(e['start'], e['end'], int(e['target'] - e['src']), int(e['target']))
                     for e in ev if e['target'] is not None]}


def _stack_plans(notes, key, chords, style, voices, seed):
    feel = C.ChoirFeel(layout=LAYOUT_FOR[voices], colour=STYLES[style][1], variety=0.8)
    return C.arrange(notes, key, chords, feel, seed=seed)


def _signature(plans):
    """Comparable fingerprint: (start rounded, target) for every note of every voice."""
    return {(n, round(p[0], 2), p[3]) for n, pl in plans.items() for p in pl}


def _too_similar(plans, others):
    sig = _signature(plans)
    for o in others:
        if sig and len(sig & o) / max(len(sig), len(o), 1) > SIMILAR:
            return True
    return False


def _existing(d):
    out = {}
    for f in glob.glob(os.path.join(d, 'opt_*.json')):
        k = int(os.path.basename(f)[4:-5])
        out[k] = _read_option(d, k)
    return out


def _cleanup_old_sessions(session, max_age_h=24):
    parent = os.path.dirname(os.path.abspath(session))
    now = time.time()
    for p in glob.glob(os.path.join(parent, 's*')):
        if os.path.abspath(p) == os.path.abspath(session):
            continue
        try:
            if now - os.path.getmtime(p) > max_age_h * 3600:
                shutil.rmtree(p, ignore_errors=True)
        except OSError:
            pass


def analyze(a):
    os.makedirs(a.session, exist_ok=True)
    _cleanup_old_sessions(a.session)
    _progress(a.session, 'Reading your vocal...')
    y, sr = core.load(a.vocal)
    _progress(a.session, 'Finding the notes...')
    t, m = core.track_pitch(y, sr)
    notes = core.segment_notes(t, m)
    if not notes:
        return _out({'ok': False, 'error': 'No sung notes found in this audio.'})
    key_name, scale = a.key, a.scale
    yb = srb = None
    if a.beat:
        from . import instrumental as I
        _progress(a.session, 'Reading the beat...')
        yb, srb = I.load_mono(a.beat)
    if key_name == 'auto':
        pcs = np.zeros(12)
        for n in notes:
            pcs[int(round(n['pitch'])) % 12] += n['end'] - n['start']
        pcs = pcs / (pcs.sum() + 1e-9)
        if yb is not None:
            import librosa
            pcs = pcs + librosa.feature.chroma_cqt(y=yb, sr=srb).mean(1) / 2
        best = max(((np.corrcoef(pcs, np.roll(p, r))[0, 1], r, q)
                    for r in range(12) for p, q in ((MAJ, 'major'), (MIN, 'minor'))))
        key_name, scale = core.NOTE_NAMES[best[1]], best[2]
    key = H.Key(key_name, scale)
    bpm = None
    if yb is not None:
        from . import instrumental as I
        _progress(a.session, 'Finding the chords...')
        bpm, _, grid = I.fit_grid(yb, srb)
        labels, _, _ = I.chords(yb, srb, grid, key.pcs, key.tonic)
        chords = [(s, e, sorted(c), c.root) for s, e, c in
                  I.timeline_chords(I.segments(labels, grid), key)]
    else:
        chords = [(s, e, sorted(c), H.chord_root(c)) for s, e, c in H.infer_chords(notes, key)]
    sf.write(os.path.join(a.session, 'vocal.wav'), y, sr, subtype='FLOAT')
    json.dump({'key': key_name, 'scale': scale, 'notes': notes, 'chords': chords,
               'bpm': bpm, 'seconds': len(y) / sr, 'source': a.vocal},
              open(os.path.join(a.session, 'meta.json'), 'w'))
    _progress(a.session, '')
    _out({'ok': True, 'key': key_name, 'scale': scale, 'notes': len(notes),
          'chord_changes': len(chords), 'bpm': bpm, 'seconds': round(len(y) / sr, 2),
          'chords_from': 'beat' if a.beat else 'vocal'})


def generate(a):
    meta, y, sr, key, chords = _load(a.session)
    notes = meta['notes']
    rng = np.random.default_rng(a.seed)
    have = _existing(a.session)
    sigs = [_signature(p) for _, p in have.values()]
    nxt = max(have) + 1 if have else 0
    made = []
    for i in range(a.count):
        style = a.style if a.style != 'mix' else str(rng.choice(list(STYLES)))
        _progress(a.session, f'Option {i + 1}/{a.count}: writing the harmony...')
        for _ in range(8):                         # redraw anything too close to what you have
            sd = int(rng.integers(1e9))
            plans = (_line_plan(notes, key, chords, style, sd) if a.voices == 1
                     else _stack_plans(notes, key, chords, style, a.voices, sd))
            if not _too_similar(plans, sigs):
                break
        sigs.append(_signature(plans))
        made.append(_write_option(a.session, nxt + i, plans, style, y, sr,
                                  label=f'Option {i + 1}/{a.count}: '))
    _progress(a.session, '')
    _out({'ok': True, 'options': made})


def reroll(a):
    meta, y, sr, key, chords = _load(a.session)
    style, plans = _read_option(a.session, a.option)
    if a.voice not in plans:
        return _out({'ok': False, 'error': f'No voice {a.voice}'})
    rng = np.random.default_rng(a.seed)
    old = _signature({a.voice: plans[a.voice]})
    for _ in range(8):
        sd = int(rng.integers(1e9))
        if a.voice == 'line':
            new = _line_plan(meta['notes'], key, chords, style, sd)
        else:
            new = C.reroll_voice(plans, a.voice, meta['notes'], key, chords,
                                 C.ChoirFeel(colour=STYLES.get(style, STYLES['classic'])[1]), seed=sd)
        if not _too_similar({a.voice: new[a.voice]}, [old]):
            break
    plans[a.voice] = new[a.voice]
    res = _write_option(a.session, a.option, plans, style, y, sr)
    _progress(a.session, '')
    _out({'ok': True, **res})


def add(a):
    meta, y, sr, key, chords = _load(a.session)
    style, plans = _read_option(a.session, a.option)
    if len(plans) >= 4:
        return _out({'ok': False, 'error': 'Already at 4 harmony voices.'})
    if 'line' in plans:
        plans['high'] = plans.pop('line')
    free = [v for v in ADD_ORDER if v not in plans]
    plans = C.add_voice(plans, free[0], meta['notes'], key, chords,
                        C.ChoirFeel(colour=STYLES.get(style, STYLES['classic'])[1]),
                        seed=a.seed or 0)
    res = _write_option(a.session, a.option, plans, style, y, sr)
    _progress(a.session, '')
    _out({'ok': True, **res})


def remove(a):
    meta, y, sr, key, chords = _load(a.session)
    style, plans = _read_option(a.session, a.option)
    plans.pop(a.voice, None)
    res = _write_option(a.session, a.option, plans, style, y, sr)
    _out({'ok': True, **res})


def main(argv=None):
    ap = argparse.ArgumentParser(prog='vocal-lab-engine')
    ap.add_argument('command', choices=['analyze', 'generate', 'reroll', 'add', 'remove'])
    ap.add_argument('--session', required=True)
    ap.add_argument('--vocal'); ap.add_argument('--beat')
    ap.add_argument('--key', default='auto'); ap.add_argument('--scale', default='minor')
    ap.add_argument('--count', type=int, default=1)
    ap.add_argument('--voices', type=int, default=1, choices=[1, 2, 3, 4])
    ap.add_argument('--style', default='mix', choices=['mix'] + list(STYLES))
    ap.add_argument('--colour', type=float)          # kept for older plugin builds
    ap.add_argument('--option', type=int); ap.add_argument('--voice')
    ap.add_argument('--seed', type=int)
    a = ap.parse_args(argv)
    try:
        {'analyze': analyze, 'generate': generate, 'reroll': reroll,
         'add': add, 'remove': remove}[a.command](a)
    except Exception as e:                          # the plugin shows this text
        _progress(a.session, '')
        _out({'ok': False, 'error': f'{type(e).__name__}: {e}'})
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
