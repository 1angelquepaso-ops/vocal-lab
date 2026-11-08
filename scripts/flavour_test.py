"""Colour tones and dark flavours on Orange 0:20-0:50, checked note by note."""
import json, sys, os
import numpy as np, soundfile as sf
from collections import Counter
from vocal_lab import core, harmony as H, instrumental as I, melody as M, choir as C

SRC = '/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav'
INST = '/home/claude/orange/instrumental.wav'
START = float(os.environ.get("START", 20)); END, OFF = START + 30, 0.04
OUT = sys.argv[1] if len(sys.argv) > 1 else 'flav_out'
y, sr = core.load(SRC, START, END)
t, m = core.track_pitch(y, sr); notes = core.segment_notes(t, m)
key = H.Key('B', 'minor')
chords = I.timeline_chords(json.load(open('/home/claude/orange/chords_timeline.json')), key, START, END)

def report(name, ev):
    sung = [e for e in ev if e['target'] is not None]
    ons = [e for e in sung if e['onset']]
    fl = [i for i, e in enumerate(ev) if e.get('flavour')]
    resolved = sum(1 for i in fl if i + 1 < len(ev) and ev[i + 1].get('resolves')
                   and abs(ev[i + 1]['target'] - ev[i]['target']) == 1)
    bad_chord = sum(1 for i in fl if not H.flavour_ok(FL, key, H.chord_at(chords, ev[i]['start'])))
    on_syll = sum(1 for i in fl if ev[i]['onset'])
    print(f"{name:20s} syllables on chord tones {100*np.mean([e['target']%12 in (H.chord_at(chords,e['start']) or set()) for e in ons]):3.0f}%"
          f" | colour tones on syllables {sum(e.get('colour',False) for e in ons):2d}"
          f" | flavour notes {len(fl)} (resolved by half step {resolved}, on a syllable {on_syll}, over a clashing chord {bad_chord})"
          f" | out-of-key notes {sum(e['target']%12 not in key.pcs for e in sung)}")

lines = {}
base = dict(side='up', movement=0.4, contrary=0.6, split=0.6, spice=0.25, hold=0.12)
for name, extra, seed in [('plain_R1', {}, 11),
                          ('colour', {'colour': 0.9}, 11),
                          ('phrygian_dark', {'flavour': 'phrygian', 'flavour_rate': 0.8, 'split': 0.85}, 11),
                          ('harmonic_dark', {'flavour': 'harmonic', 'flavour_rate': 0.8, 'split': 0.85}, 11)]:
    FL = extra.get('flavour', 'phrygian')
    ev = M.line(notes, key, M.Feel(**{**base, **extra}), chords, seed)
    lines[name] = ev
    report(name, ev)

ch_plain = C.arrange(notes, key, chords, C.ChoirFeel(layout='around2', colour=0.0), seed=5)
ch_col = C.arrange(notes, key, chords, C.ChoirFeel(layout='around2', colour=0.9), seed=5)
for nm, p in (('choir plain', ch_plain), ('choir colour', ch_col)):
    r = C.check(p, notes, key, chords)
    col = sum(1 for v in p.values() for s, e, iv, tg in v
              if tg % 12 in H.colour_tones(H.chord_at(chords, s), key))
    print(f"{nm:20s} chord tones {r['chord_tones']} | colour-tone notes {col} | clashes {r['hard_clashes']} | parallels {r['parallel_5ths_8ves']}")

if '--render' in sys.argv:
    os.makedirs(OUT, exist_ok=True)
    inst, _ = sf.read(INST, dtype='float32', always_2d=True)
    a = int((START + OFF) * sr); inst = inst[a:a + len(y)]
    segs = {n: M.to_segments(ev) for n, ev in lines.items() if n != 'plain_R1'}
    chsegs = {v: [(s, e, iv, i) for i, (s, e, iv, tg) in enumerate(p)] for v, p in ch_col.items()}
    need = sorted({x[2] for v in segs.values() for x in v} | {x[2] for v in chsegs.values() for x in v})
    bank = {iv: core.shift(y, sr, iv) for iv in need}
    def mix(stems):
        L = inst[:, 0] * 0.8 + y * 0.75; R = inst[:, 1] * 0.8 + y * 0.75
        for v, pan in stems:
            th = (pan + 1) * np.pi / 4; L = L + v * np.cos(th); R = R + v * np.sin(th)
        x = np.stack([L, R], 1); return x * min(1, 0.98 / np.abs(x).max())
    for n, sg in segs.items():
        v = M.splice_groups(bank, len(y), sr, sg, np.random.default_rng(3)) * 10 ** (-5 / 20)
        core.save(f'{OUT}/{n}_with_beat.wav', mix([(v, -0.4)]), sr)
    st = []
    for j, (vn, sg) in enumerate(chsegs.items()):
        sg2 = M.to_segments([dict(start=s, end=e, src=0, target=iv, group=i, onset=True) for s, e, iv, i in sg])
        st.append((M.splice_groups(bank, len(y), sr, sg2, np.random.default_rng(9 + j), base_ms=16 + 7 * j) * 10 ** (-8 / 20), (-0.5, 0.5)[j % 2]))
    core.save(f'{OUT}/choir_colour_with_beat.wav', mix(st), sr)
    M.write_midi(f'{OUT}/colour_and_dark_lines_79bpm.mid', {k: v for k, v in lines.items() if k != 'plain_R1'}, offset=START, bpm=79.0)
    print('rendered')
