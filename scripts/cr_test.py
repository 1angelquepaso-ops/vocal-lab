"""Call & response on Orange 0:45-1:15 with the beat's real chords and grid."""
import json, sys
import numpy as np, soundfile as sf
from vocal_lab import core, harmony as H, instrumental as I, call_response as CR

SRC = '/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav'
INST = '/home/claude/orange/instrumental.wav'
ANA = '/home/claude/orange/inst_analysis.npz'
TL = '/home/claude/orange/chords_timeline.json'
START, END, OFF = 45.0, 75.0, 0.04
OUT = sys.argv[1] if len(sys.argv) > 1 else 'cr_out'
seeds = [int(s) for s in sys.argv[2:]] or [1, 2, 3]

y, sr = core.load(SRC, START, END)
inst, _ = sf.read(INST, dtype='float32', always_2d=True)
a = int((START + OFF) * sr); inst = inst[a:a + len(y)]
t, m = core.track_pitch(y, sr); notes = core.segment_notes(t, m)
key = H.Key('B', 'minor')
cand = {nm: v[1] for nm, v in I.candidates(key.pcs, key.tonic).items()}
chords = [(s - START, e - START, cand[l]) for s, e, l in json.load(open(TL))
          if e > START and s < END]
grid = np.load(ANA)['grid'] - OFF - START
grid = grid[(grid > -2) & (grid < END - START + 2)]

import os; os.makedirs(OUT, exist_ok=True)
plans = {sd: CR.plan(notes, key, chords, grid, seed=sd) for sd in seeds}
need = sorted({n['target'] - n['src'] for p in plans.values() for r in p for n in r['notes']})
print('intervals', need)
bank = {iv: core.shift(y, sr, iv) for iv in need}
lead_iv = [(n['start'], n['end']) for n in notes]
for sd, p in plans.items():
    stem = CR.render(y, sr, bank, p, ref_rms=CR.lead_level(y, sr, notes))
    # checks: no response note overlaps a lead note; responses on grid; chord fit
    overlap = 0; ongrid = 0; fit = []
    beat = grid[1] - grid[0]
    for r in p:
        if abs(((r['start'] - grid[0]) / (beat / 4)) - round((r["start"] - grid[0]) / (beat / 4))) < 0.02:
            ongrid += 1
        for n in r['notes']:
            s, e = n['src_start'] + r['delta'], n['src_end'] + r['delta']
            overlap += sum(1 for a, b in lead_iv if s < b - 0.02 and e > a + 0.02)
            fit.append(n['target'] % 12 in (H.chord_at(chords, s) or set()))
    kinds = [r['kind'] for r in p]
    lr = 20*np.log10(CR.lead_level(y, sr, notes)); rr = 20*np.log10(np.sqrt(np.mean(stem[np.abs(stem).sum(1)>1e-5]**2))*np.sqrt(2))
    print(f"seed {sd}: response level vs lead {rr-lr:+.1f} dB: | {len(p)} responses {kinds} | on-grid {ongrid}/{len(p)} | notes overlapping lead {overlap} | chord tones {100*np.mean(fit):.0f}% | peak {20*np.log10(np.abs(stem).max()+1e-9):.1f} dBFS")
    mix = inst * 0.8 + np.stack([y, y], 1) * 0.75 + stem
    mix *= min(1, 0.98 / np.abs(mix).max())
    core.save(f'{OUT}/CR_seed{sd}_with_beat.wav', mix, sr)
    core.save(f'{OUT}/CR_seed{sd}_responses_only.wav', stem, sr)
    json.dump(p, open(f'{OUT}/CR_seed{sd}.json', 'w'), default=float)
ref = inst * 0.8 + np.stack([y, y], 1) * 0.75
core.save(f'{OUT}/CR_reference_no_responses.wav', ref * min(1, 0.98 / np.abs(ref).max()), sr)
