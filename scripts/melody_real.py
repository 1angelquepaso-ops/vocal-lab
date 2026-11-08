import numpy as np, json, soundfile as sf, pretty_midi
from vocal_lab import core, harmony as H, melody as M, instrumental as I
SRC='/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav'; START,END=20,50; OFF=0.04
y,sr=core.load(SRC,START,END)
inst,_=sf.read('/home/claude/orange/instrumental.wav',dtype='float32',always_2d=True)
a=int((START+OFF)*sr); inst=inst[a:a+len(y)]
t,m=core.track_pitch(y,sr); notes=core.segment_notes(t,m); key=H.Key('B','minor')
cand={nm:v[1] for nm,v in I.candidates(key.pcs,key.tonic).items()}
TL=json.load(open('/home/claude/orange/chords_timeline.json'))
chords=[(s-START,e-START,cand[l]) for s,e,l in TL if e>START and s<END]
print('chords in section:', ' | '.join(f"{max(s,0):.1f} {l}" for s,e,l in [(s-START,e,l) for s,e,l in TL if e>START and s<END]))
VARS={
 'R1_floating_above':  (M.Feel(side='up',movement=0.4,contrary=0.6,split=0.5,spice=0.25,hold=0.15), 11, -0.40),
 'R2_busy_counterline':(M.Feel(side='up',movement=0.75,contrary=0.5,split=0.85,spice=0.45,hold=0.08,rest=0.05), 23, -0.45),
 'R3_low_countermelody':(M.Feel(side='down',movement=0.45,contrary=0.85,split=0.35,spice=0.2,hold=0.3,rest=0.1,lo=3,hi=12), 37, +0.45),
}
lines={n:M.line(notes,key,f,chords,s) for n,(f,s,p) in VARS.items()}
for n,ev in lines.items():
    ons=[e for e in ev if e['target'] is not None and e['onset']]
    ct=np.mean([e['target']%12 in (H.chord_at(chords,e['start']) or set()) for e in ons])
    print(f"{n}: syllable notes that are chord tones of the beat {100*ct:.0f}%")
segs={n:M.to_segments(ev) for n,ev in lines.items()}
bank={iv:core.shift(y,sr,iv) for iv in sorted({s[2] for v in segs.values() for s in v})}
import os; os.makedirs('real',exist_ok=True)
# reference: vocal + beat, no harmony
def mixdown(voc,harm,pan):
    L=inst[:,0]*0.8+voc*0.75; R=inst[:,1]*0.8+voc*0.75
    if harm is not None:
        th=(pan+1)*np.pi/4; L=L+harm*np.cos(th); R=R+harm*np.sin(th)
    x=np.stack([L,R],1); return x*min(1,0.98/np.abs(x).max())
core.save('real/R0_vocal_and_beat_NO_harmony.wav',mixdown(y,None,0),sr)
for i,(n,(f,s,p)) in enumerate(VARS.items()):
    h=M.splice_groups(bank,len(y),sr,segs[n],np.random.default_rng(s+1),base_ms=20+4*i)*10**(-5/20)
    core.save(f'real/{n}_with_beat.wav',mixdown(y,h,p),sr)
    core.save(f'real/{n}_harmony_only.wav',h,sr)
M.write_midi('real/ORANGE_harmonies_realchords_79bpm.mid',lines,lead_evs=lines['R1_floating_above'],offset=START,bpm=79.0)
# full-song chord MIDI of the beat, for FL
pm=pretty_midi.PrettyMIDI(initial_tempo=79.0); ins=pretty_midi.Instrument(program=0,name='Orange beat chords (detected)')
for s,e,l in TL:
    for pc in sorted(cand[l]):
        ins.notes.append(pretty_midi.Note(70,48+((pc-11)%12)+11 if False else 48+pc if pc>=11-12 else 48+pc,max(s+OFF,0),e+OFF))
pm.instruments.append(ins); pm.write('real/ORANGE_beat_chords_79bpm.mid')
json.dump({n:[dict(e,target=None if e['target'] is None else int(e['target'])) for e in ev] for n,ev in lines.items()},open('real/lines.json','w'))
print('done')
