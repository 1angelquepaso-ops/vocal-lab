"""vocal-lab harmonizer v1.

Load a dry vocal, pick a key and harmony styles, get one WAV per harmony
voice (same length and start as the source, so it drops straight onto the
FL playlist under the lead) plus a stereo preview mix.

  python harmonize.py vocal.wav --key B --scale minor \
      --voices third_up sixth_down chord_up --start 20 --end 50 --out outdir
"""
import argparse
import json
import os

import numpy as np

from . import core
from . import harmony as H

# per-style defaults: level vs lead (dB), pan (-1..1), delay ms, detune cents
STYLE = {
    'third_up':    (-6, -0.45, 18, +5),
    'sixth_down':  (-7, +0.45, 24, -5),
    'third_down':  (-7, +0.45, 24, -5),
    'fifth_up':    (-8, +0.60, 20, -4),
    'fourth_down': (-8, -0.60, 26, +4),
    'octave_up':   (-10, -0.25, 12, +3),
    'octave_down': (-9, +0.25, 14, -3),
    'unison':      (-6, -0.70, 22, +7),
    'chord_up':    (-6, -0.50, 18, +5),
    'chord_down':  (-7, +0.50, 25, -5),
    'random_up':   (-7, -0.35, 20, +6),
    'random_down': (-8, +0.35, 27, -6),
}


def segments(plan, preroll=0.04, release=0.06, merge_gap=0.12):
    """Turn a note plan into render segments.

    Adds a short pre-roll so the consonant before each note comes along, and
    a release tail. Consecutive notes on the same interval with only a small
    gap are joined so the voice doesn't chop between syllables.
    """
    segs = []
    for s, e, iv, _ in plan:
        s, e = max(s - preroll, 0.0), e + release
        if segs and segs[-1][2] == iv and s - segs[-1][1] < merge_gap:
            segs[-1][1] = e
        else:
            if segs and s < segs[-1][1]:          # overlap with a new interval
                mid = (s + segs[-1][1]) / 2
                segs[-1][1], s = mid, mid
            segs.append([s, e, iv])
    return [tuple(x) for x in segs]


def run(path, key='B', scale='minor', voices=('third_up', 'sixth_down'),
        start=None, end=None, out='out', chords_midi=None, chords_offset=0.0,
        allowed_chords=None, seed=7, formant=True, humanize=True):
    y, sr = core.load(path, start, end)
    times, midi = core.track_pitch(y, sr)
    notes = core.segment_notes(times, midi)
    k = H.Key(key, scale)

    if chords_midi:
        chords = H.chords_from_midi(chords_midi, offset=chords_offset)
        chord_src = f'MIDI ({os.path.basename(chords_midi)})'
    else:
        allowed = {H.KEYS[c.rstrip('m')] for c in allowed_chords} \
            if allowed_chords else None
        chords = H.infer_chords(notes, k, allowed=allowed)
        chord_src = 'inferred from the lead'

    os.makedirs(out, exist_ok=True)
    rng = np.random.default_rng(seed)
    plans, specs = {}, {}
    for v in voices:
        gain, pan, delay, det = STYLE[v]
        if not humanize:
            delay, det = 0, 0
        plans[v] = H.voice_plan(notes, k, v, chords=chords,
                                rng=np.random.default_rng(rng.integers(1e9)))
        specs[v] = dict(gain=gain, pan=pan, delay=delay, detune=det)

    # one Rubber Band render per (interval + detune) actually needed
    needed = {(iv, specs[v]['detune']) for v in voices for _, _, iv, _ in plans[v]}
    bank = {}
    for iv, det in sorted(needed):
        bank[(iv, det)] = core.shift(y, sr, iv + det / 100.0, formant=formant)

    lead_peak = float(np.max(np.abs(y))) or 1.0
    mixL = y.copy() * 0.7071
    mixR = y.copy() * 0.7071
    report = {'source': os.path.basename(path), 'start': start or 0.0,
              'key': f'{key} {scale}', 'chords': chord_src,
              'notes_detected': len(notes), 'voices': {}}
    for v in voices:
        sp = specs[v]
        vb = {iv: bank[(iv, sp['detune'])] for _, _, iv, _ in plans[v]}
        voice = core.splice(vb, len(y), sr, segments(plans[v]))
        d = int(sp['delay'] / 1000 * sr)
        voice = np.concatenate([np.zeros(d, 'float32'), voice])[:len(y)]
        voice *= 10 ** (sp['gain'] / 20)
        fn = os.path.join(out, f'harmony_{v}.wav')
        core.save(fn, voice, sr)
        th = (sp['pan'] + 1) * np.pi / 4
        mixL += voice * np.cos(th)
        mixR += voice * np.sin(th)
        report['voices'][v] = {**sp, 'file': os.path.basename(fn),
                               'intervals': sorted({p[2] for p in plans[v]})}

    mix = np.stack([mixL, mixR], axis=1)
    pk = np.max(np.abs(mix))
    if pk > 0.98:
        mix *= 0.98 / pk
    core.save(os.path.join(out, 'preview_mix.wav'), mix, sr)
    with open(os.path.join(out, 'plans.json'), 'w') as f:
        json.dump({v: plans[v] for v in voices}, f)
    with open(os.path.join(out, 'report.json'), 'w') as f:
        json.dump(report, f, indent=2)
    return report, notes, plans, y, sr


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawTextHelpFormatter)
    ap.add_argument('vocal')
    ap.add_argument('--key', default='B')
    ap.add_argument('--scale', default='minor', choices=list(H.SCALES))
    ap.add_argument('--voices', nargs='+', default=['third_up', 'sixth_down'],
                    choices=list(STYLE))
    ap.add_argument('--start', type=float)
    ap.add_argument('--end', type=float)
    ap.add_argument('--chords-midi')
    ap.add_argument('--chords-offset', type=float, default=0.0)
    ap.add_argument('--allowed-chords', nargs='+',
                    help='limit inferred chords, e.g. Bm A G')
    ap.add_argument('--seed', type=int, default=7)
    ap.add_argument('--no-formant', action='store_true')
    ap.add_argument('--no-humanize', action='store_true')
    ap.add_argument('--out', default='out')
    a = ap.parse_args()
    rep, *_ = run(a.vocal, a.key, a.scale, a.voices, a.start, a.end, a.out,
                  a.chords_midi, a.chords_offset, a.allowed_chords, a.seed,
                  not a.no_formant, not a.no_humanize)
    print(json.dumps(rep, indent=2))
