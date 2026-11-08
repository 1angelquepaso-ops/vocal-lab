import numpy as np, json
from vocal_lab import core, harmony as H, melody as M
from collections import Counter
SRC='/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav'; START=20
y,sr=core.load(SRC,START,START+30)
t,m=core.track_pitch(y,sr); notes=core.segment_notes(t,m)
key=H.Key('B','minor'); chords=H.infer_chords(notes,key,allowed={11,9,7})
VARS={
 'M1_floating_above':  (M.Feel(side='up',movement=0.4,contrary=0.6,split=0.5,spice=0.25,hold=0.15), 11, -0.40),
 'M2_busy_counterline':(M.Feel(side='up',movement=0.75,contrary=0.5,split=0.85,spice=0.45,hold=0.08,rest=0.05), 23, -0.45),
 'M3_low_countermelody':(M.Feel(side='down',movement=0.45,contrary=0.85,split=0.35,spice=0.2,hold=0.3,rest=0.1,lo=3,hi=10), 37, +0.45),
}
lines={}; stats={}
par3=[key.step(key.snap(n['pitch']),2) for n in notes]
for name,(feel,seed,pan) in VARS.items():
    ev=M.line(notes,key,feel,chords,seed); lines[name]=ev
    sung=[e for e in ev if e['target'] is not None]
    inkey=np.mean([e['target']%12 in key.pcs for e in sung])
    ons=[e for e in sung if e['onset']]
    cons=np.mean([(abs(e['target']-e['src'])%12) not in H.CLASH for e in ons])
    passing=sum((abs(e['target']-e['src'])%12) in H.CLASH for e in sung if not e['onset'])
    mot=Counter(); contr=0; nmov=0
    for a,b in zip(sung,sung[1:]):
        l=abs(b['target']-a['target']); mot['repeat' if l==0 else 'step' if l<=2 else 'third' if l<=4 else 'leap']+=1
        ld=np.sign(b['src']-a['src']); hd=np.sign(b['target']-a['target'])
        if ld!=0: nmov+=1; contr+= hd==-ld
    differs=np.mean([e['target']!=par3[e['group']] for e in ons])
    stats[name]=dict(slots=len(ev),sung=len(sung),rests=len({e['group'] for e in ev if e['target'] is None}),
        moves_inside_held_notes=sum(1 for e in sung if not e['onset']),
        in_key=f"{100*inkey:.0f}%", onsets_consonant=f"{100*cons:.0f}%", passing_tones=passing,
        motion=dict(mot), contrary_motion=f"{100*contr/max(nmov,1):.0f}%",
        different_from_plain_3rd=f"{100*differs:.0f}%",
        distinct_notes=len({e['target'] for e in sung}),
        range=f"{core.note_name(min(e['target'] for e in sung))}-{core.note_name(max(e['target'] for e in sung))}")
print(json.dumps(stats,indent=1))
# render
segs={n:M.to_segments(ev) for n,ev in lines.items()}
need=sorted({s[2] for v in segs.values() for s in v}); print('intervals to render', need)
bank={iv:core.shift(y,sr,iv) for iv in need}
import os; os.makedirs('mel',exist_ok=True)
for i,(name,(feel,seed,pan)) in enumerate(VARS.items()):
    v=M.splice_groups(bank,len(y),sr,segs[name],np.random.default_rng(seed+1),base_ms=20+4*i)*10**(-6/20)
    core.save(f'mel/{name}_harmony_only.wav',v,sr)
    th=(pan+1)*np.pi/4; L=y*0.7071+v*np.cos(th); R=y*0.7071+v*np.sin(th)
    mx=np.stack([L,R],1); mx*=min(1,0.98/np.abs(mx).max())
    core.save(f'mel/{name}_with_lead.wav',mx,sr)
M.write_midi('mel/ORANGE_melody_harmonies_80bpm.mid',lines,lead_evs=lines['M1_floating_above'],offset=START,bpm=80)
json.dump({n:[dict(e,target=None if e['target'] is None else int(e['target'])) for e in ev] for n,ev in lines.items()},open('mel/lines.json','w'))
print('rendered')
