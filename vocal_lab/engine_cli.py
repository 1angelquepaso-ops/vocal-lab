"""Engine entry point for the plugin.

Every call prints one JSON object on stdout. State lives in a session folder:
  meta.json            key, notes, chords, chunk info
  vocal.wav            the chunk (mono)
  bank/iv_<n>.npy      pitch-shifted copies, built on demand and reused
  opt_<k>.json         an option: {voice: [[start, end, interval, target], ...]}
  opt_<k>_<voice>.wav  that voice as a DRY stem: same length as the chunk,
                       no pan / level / effects

Commands
  analyze  --session D --vocal V [--beat B] [--key auto|B] [--scale minor]
  generate --session D [--count 8] [--voices 1-4] [--colour 0.3]
  reroll   --session D --option K --voice NAME
  add      --session D --option K
  remove   --session D --option K --voice NAME
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf

from . import core, harmony as H, choir as C, melody as M, randomize as R

LAYOUT_FOR = {2: 'around2', 3: 'wide3', 4: 'full4'}
ADD_ORDER = ['mid', 'high', 'low', 'bass']
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def _out(obj):
    sys.stdout.write(json.dumps(obj) + '\n')
    sys.stdout.flush()


def _load(d):
    meta = json.load(open(os.path.join(d, 'meta.json')))
    y, sr = sf.read(os.path.join(d, 'vocal.wav'), dtype='float32')
    key = H.Key(meta['key'], meta['scale'])
    chords = [(s, e, H.make_chord(pcs, root)) for s, e, pcs, root in meta['chords']]
    return meta, y, sr, key, chords


def _bank(d, y, sr, ivs):
    os.makedirs(os.path.join(d, 'bank'), exist_ok=True)
    bank = {}
    for iv in sorted(set(ivs)):
        p = os.path.join(d, 'bank', f'iv_{iv}.npy')
        if os.path.exists(p):
            bank[iv] = np.load(p)
        else:
            bank[iv] = core.shift(y, sr, iv)
            np.save(p, bank[iv])
    return bank


def _write_option(d, k, plans, y, sr):
    json.dump(plans, open(os.path.join(d, f'opt_{k}.json'), 'w'))
    for f in os.listdir(d):
        if f.startswith(f'opt_{k}_') and f.endswith('.wav'):
            os.remove(os.path.join(d, f))
    bank = _bank(d, y, sr, [p[2] for pl in plans.values() for p in pl])
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
    return voices


def analyze(a):
    os.makedirs(a.session, exist_ok=True)
    y, sr = core.load(a.vocal)
    t, m = core.track_pitch(y, sr)
    notes = core.segment_notes(t, m)
    if not notes:
        return _out({'ok': False, 'error': 'No sung notes found in this audio.'})
    key_name, scale = a.key, a.scale
    if key_name == 'auto':
        pcs = np.zeros(12)
        src = notes
        for n in src:
            pcs[int(round(n['pitch'])) % 12] += n['end'] - n['start']
        if a.beat:
            from . import instrumental as I
            yb, srb = I.load_mono(a.beat)
            import librosa
            pcs = pcs / (pcs.sum() + 1e-9) + librosa.feature.chroma_cqt(
                y=yb, sr=srb).mean(1) / 12 * 6
        best = max(((np.corrcoef(pcs, np.roll(p, r))[0, 1], r, q)
                    for r in range(12) for p, q in ((MAJ, 'major'), (MIN, 'minor'))))
        key_name, scale = core.NOTE_NAMES[best[1]], best[2]
    key = H.Key(key_name, scale)
    chords, bpm = [], None
    if a.beat:
        from . import instrumental as I
        yb, srb = I.load_mono(a.beat)
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
    _out({'ok': True, 'key': key_name, 'scale': scale, 'notes': len(notes),
          'chord_changes': len(chords), 'bpm': bpm, 'seconds': round(len(y) / sr, 2),
          'chords_from': 'beat' if a.beat else 'vocal'})


def generate(a):
    meta, y, sr, key, chords = _load(a.session)
    notes = meta['notes']
    rng = np.random.default_rng(a.seed if a.seed is not None else None)
    opts = []
    for k in range(a.count):
        sd = int(rng.integers(1e9))
        if a.voices == 1:
            c = R.generate(notes, key, chords, n=1, seed=sd)[0]
            plans = {'line': [(e['start'], e['end'], int(e['target'] - e['src']), int(e['target']))
                              for e in c['events'] if e['target'] is not None]}
        else:
            feel = C.ChoirFeel(layout=LAYOUT_FOR[a.voices], colour=a.colour,
                               variety=0.8)
            plans = C.arrange(notes, key, chords, feel, seed=sd)
        voices = _write_option(a.session, k, plans, y, sr)
        opts.append({'option': k, 'voices': voices})
    _out({'ok': True, 'options': opts})


def _option(a):
    meta, y, sr, key, chords = _load(a.session)
    plans = {n: [tuple(p) for p in pl] for n, pl in
             json.load(open(os.path.join(a.session, f'opt_{a.option}.json'))).items()}
    return meta, y, sr, key, chords, plans


def reroll(a):
    meta, y, sr, key, chords, plans = _option(a)
    if a.voice not in plans:
        return _out({'ok': False, 'error': f'No voice {a.voice}'})
    if a.voice == 'line':
        c = R.generate(meta['notes'], key, chords, n=1, seed=a.seed)[0]
        plans['line'] = [(e['start'], e['end'], int(e['target'] - e['src']), int(e['target']))
                         for e in c['events'] if e['target'] is not None]
    else:
        plans = C.reroll_voice(plans, a.voice, meta['notes'], key, chords,
                               C.ChoirFeel(colour=a.colour), seed=a.seed or 0)
    _out({'ok': True, 'option': a.option,
          'voices': _write_option(a.session, a.option, plans, y, sr)})


def add(a):
    meta, y, sr, key, chords, plans = _option(a)
    free = [v for v in ADD_ORDER if v not in plans]
    if len(plans) >= 4 or not free:
        return _out({'ok': False, 'error': 'Already at 4 harmony voices.'})
    if 'line' in plans:
        plans['high'] = plans.pop('line')
        free = [v for v in ADD_ORDER if v not in plans]
    plans = C.add_voice(plans, free[0], meta['notes'], key, chords,
                        C.ChoirFeel(colour=a.colour), seed=a.seed or 0)
    _out({'ok': True, 'option': a.option,
          'voices': _write_option(a.session, a.option, plans, y, sr)})


def remove(a):
    meta, y, sr, key, chords, plans = _option(a)
    plans.pop(a.voice, None)
    _out({'ok': True, 'option': a.option,
          'voices': _write_option(a.session, a.option, plans, y, sr)})


def main(argv=None):
    ap = argparse.ArgumentParser(prog='vocal-lab-engine')
    ap.add_argument('command', choices=['analyze', 'generate', 'reroll', 'add', 'remove'])
    ap.add_argument('--session', required=True)
    ap.add_argument('--vocal'); ap.add_argument('--beat')
    ap.add_argument('--key', default='auto'); ap.add_argument('--scale', default='minor')
    ap.add_argument('--count', type=int, default=8)
    ap.add_argument('--voices', type=int, default=2, choices=[1, 2, 3, 4])
    ap.add_argument('--colour', type=float, default=0.3)
    ap.add_argument('--option', type=int); ap.add_argument('--voice')
    ap.add_argument('--seed', type=int)
    a = ap.parse_args(argv)
    try:
        {'analyze': analyze, 'generate': generate, 'reroll': reroll,
         'add': add, 'remove': remove}[a.command](a)
    except Exception as e:                          # the plugin shows this text
        _out({'ok': False, 'error': f'{type(e).__name__}: {e}'})
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
