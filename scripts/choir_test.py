"""Choir stack on Orange 0:20-0:50 with the beat's real chords."""
import json, sys, os, time
import numpy as np, soundfile as sf
from vocal_lab import core, harmony as H, instrumental as I, melody as M, choir as C

SRC = '/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav'
INST = '/home/claude/orange/instrumental.wav'
TL = '/home/claude/orange/chords_timeline.json'
START, END, OFF = 20.0, 50.0, 0.04
OUT = sys.argv[1] if len(sys.argv) > 1 else 'choir_out'
y, sr = core.load(SRC, START, END)
inst, _ = sf.read(INST, dtype='float32', always_2d=True)
a = int((START + OFF) * sr); inst = inst[a:a + len(y)]
t, m = core.track_pitch(y, sr); notes = core.segment_notes(t, m)
key = H.Key('B', 'minor')
cand = {nm: v[1] for nm, v in I.candidates(key.pcs, key.tonic).items()}
chords = [(s - START, e - START, cand[l]) for s, e, l in json.load(open(TL)) if e > START and s < END]
os.makedirs(OUT, exist_ok=True)
RUNS = [('around2', 0.3, 5), ('above2', 0.3, 8), ('full4', 0.4, 13)]
arr = {}
for layout, colour, seed in RUNS:
    t0 = time.time()
    p = C.arrange(notes, key, chords, C.ChoirFeel(layout=layout, colour=colour), seed=seed)
    arr[layout] = p
    print(f"{layout:8s} ({time.time()-t0:.1f}s):", json.dumps(C.check(p, notes, key, chords)))
need = sorted({x[2] for p in arr.values() for v in p.values() for x in v})
bank = {iv: core.shift(y, sr, iv) for iv in need}
PAN = {'high': -0.55, 'mid': 0.35, 'low': -0.25, 'bass': 0.55}
GAIN = {'high': -9, 'mid': -8, 'low': -8, 'bass': -10}
for k, (layout, colour, seed) in enumerate(RUNS):
    L = inst[:, 0] * 0.8 + y * 0.75; R = inst[:, 1] * 0.8 + y * 0.75
    rng = np.random.default_rng(seed)
    lines = {}
    for j, (vn, plan) in enumerate(arr[layout].items()):
        segs = [(s, e, iv, i) for i, (s, e, iv, tg) in enumerate(plan)]
        segs = M.to_segments([dict(start=s, end=e, src=0, target=iv, group=i, onset=True) for s, e, iv, i in segs])
        v = M.splice_groups(bank, len(y), sr, segs, rng, base_ms=16 + 7 * j) * 10 ** (GAIN[vn] / 20)
        th = (PAN[vn] + 1) * np.pi / 4; L += v * np.cos(th); R += v * np.sin(th)
        lines[f'{layout}_{vn}'] = [dict(start=s, end=e, src=0, target=tg, group=i, onset=True) for i, (s, e, iv, tg) in enumerate(plan)]
    mix = np.stack([L, R], 1); mix *= min(1, 0.98 / np.abs(mix).max())
    core.save(f'{OUT}/CHOIR_{layout}_with_beat.wav', mix, sr)
    M.write_midi(f'{OUT}/CHOIR_{layout}_79bpm.mid', lines, offset=START, bpm=79.0)
print('rendered')
