import json, sys
import numpy as np, soundfile as sf, librosa
from vocal_lab import core
out=sys.argv[1]
rep=json.load(open(f'{out}/report.json')); plans=json.load(open(f'{out}/plans.json'))
y,sr=core.load('/mnt/user-data/uploads/Downloads/ORANGE_dry_vocal_CLEAN.wav', rep['start'], rep['start']+30)
t,lm=core.track_pitch(y,sr)
def env(x):
    return librosa.feature.rms(y=x,frame_length=1024,hop_length=256)[0]
le=env(y)
print(f"{'voice':12s} {'notes':>5s} {'med err':>8s} {'<25c':>6s} {'<50c':>6s} {'lag ms':>7s} {'peak dBFS':>9s} {'leak dB':>8s}")
for v,sp in rep['voices'].items():
    x,_=sf.read(f"{out}/{sp['file']}",dtype='float32')
    d=int(sp['delay']/1000*sr); xa=np.concatenate([x[d:],np.zeros(d,'float32')])
    tv,vm=core.track_pitch(xa,sr)
    errs=[]
    for s,e,iv,tgt in plans[v]:
        a,b=np.searchsorted(t,s+0.03),np.searchsorted(t,e-0.03)
        if b-a<3: continue
        lp=lm[a:b]; hp=vm[a:b]; ok=~np.isnan(lp)&~np.isnan(hp)
        if ok.sum()<3: continue
        exp=lp[ok]+iv+sp['detune']/100
        errs.append(np.median(np.abs(hp[ok]-exp))*100)
    errs=np.array(errs)
    # lag via envelope xcorr
    ve=env(x); n=min(len(le),len(ve))
    c=np.correlate(ve[:n]-ve[:n].mean(), le[:n]-le[:n].mean(), 'full')
    lag=(np.argmax(c)-(n-1))*256/sr*1000
    # leakage: harmony energy where lead is silent (< -55 dBFS rms)
    silent=20*np.log10(le[:n]+1e-9)<-55
    leak=20*np.log10(np.sqrt(np.mean(ve[:n][silent]**2))+1e-9) if silent.any() else float('nan')
    pk=20*np.log10(np.max(np.abs(x))+1e-9)
    print(f"{v:12s} {len(errs):5d} {np.median(errs):7.1f}c {100*(errs<25).mean():5.0f}% {100*(errs<50).mean():5.0f}% {lag:7.1f} {pk:9.1f} {leak:8.1f}")
