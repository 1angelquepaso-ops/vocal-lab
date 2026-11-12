import numpy as np, soundfile as sf, subprocess, json, sys
from fitlib import *
from build_shelf import plateau_corner
U='/mnt/user-data/uploads/Documents/MEX-EQ/captures/'
R='/home/claude/mxbuild/me_render_artefacts/Release/me_render'
x=np.load('captures/ms_period.npy'); sr=44100; N=8192
sig=np.tile(x,24); sf.write('verify/ms.wav',np.stack([sig,sig],1).astype(np.float32),sr,subtype='FLOAT')
X=np.fft.rfft(x); f=np.fft.rfftfreq(N,1/sr); band=np.abs(X)>1e-6*np.abs(X).max()
def curve(path):
    y,_=sf.read(path,always_2d=True); y=y[:,0]; fr=y[N*16:N*17]
    H=20*np.log10(np.abs(np.fft.rfft(fr)[band])/np.abs(X[band])+1e-12)
    m=band&(f>15); return np.interp(np.log(G),np.log(f[m]),H)
def render(params,tag):
    out=f'verify/{tag}.wav'
    r=subprocess.run([R,'verify/ms.wav',out]+[f'{k}={v}' for k,v in params.items()],capture_output=True,text=True)
    if r.returncode: print(r.stdout,r.stderr); sys.exit(1)
    return curve(out), json.loads(r.stdout.strip().splitlines()[-1])
S={}
def seg(fn,k):
    if fn not in S: S[fn]=segments(U+fn)
    return S[fn][k][2]
L=json.load(open('shelf_tables.json'))
def fn_at(kind,k):
    fn=np.array(L[kind]['fn']); return float(np.exp(np.interp(k,np.arange(len(fn)),np.log(fn))))
tests=[
 ('LMF bell +7.25 (465 Hz)', dict(b1_type=6,b1_freq=465,b1_gain=7.25,b1_q=0.50), ('E1_geq_lmf_takes.wav',4)),
 ('LMF bell max +21.5', dict(b1_type=6,b1_freq=465,b1_gain=21.47,b1_q=0.50), ('E1_geq_lmf_takes.wav',8)),
 ('LMF bell cut -21.35', dict(b1_type=6,b1_freq=465,b1_gain=-21.35,b1_q=0.50), ('E2_geq_bands_takes.wav',5)),
 ('LMF 1.6k +21.6', dict(b1_type=6,b1_freq=1626,b1_gain=21.59,b1_q=0.50), ('E5_gch_q_drift.wav',37)),
 ('HMF bell +20.9 (3 kHz)', dict(b1_type=6,b1_freq=3030,b1_gain=20.89,b1_q=0.445), ('E2_geq_bands_takes.wav',11)),
 ('HMF 4.8k +20.7', dict(b1_type=6,b1_freq=4825,b1_gain=20.68,b1_q=0.43), ('E5_gch_q_drift.wav',43)),
 ('LF shelf +18 (default)', dict(b1_type=5,b1_freq=fn_at('L',L['L']['kd']),b1_gain=None), ('E2_geq_bands_takes.wav',40)),
 ('LF shelf -18.2', dict(b1_type=5,b1_freq=fn_at('L',L['L']['kd']),b1_gain=None), ('E3_geq_hf_hp_div3.wav',22)),
 ('LF shelf +11.7 high pos', dict(b1_type=5,b1_freq=fn_at('L',7),b1_gain=None), ('E2_geq_bands_takes.wav',50)),
 ('HF shelf +19.4 (default)', dict(b1_type=7,b1_freq=fn_at('H',5),b1_gain=None), ('E2_geq_bands_takes.wav',57)),
 ('HF shelf +3.4 (default)', dict(b1_type=7,b1_freq=fn_at('H',5),b1_gain=None), ('E6_gch_lp_shelves.wav',19)),
 ('HF shelf -19.9', dict(b1_type=7,b1_freq=fn_at('H',5),b1_gain=None), ('E3_geq_hf_hp_div3.wav',16)),
 ('HF shelf +19.4 low pos', dict(b1_type=7,b1_freq=fn_at('H',1),b1_gain=None), ('E3_geq_hf_hp_div3.wav',3)),
 ('High pass 18 dB/oct 190 Hz', dict(b1_type=3,b1_freq=190), ('E3_geq_hf_hp_div3.wav',31)),
 ('Low pass 3.7 kHz', dict(b1_type=1,b1_freq=3682), ('E7_gch_lowpass.wav',8)),
]
mode=sys.argv[1] if len(sys.argv)>1 else '2'
res=[]
for name,p,(fn,k) in tests:
    c=seg(fn,k)
    if p.get('b1_gain',0) is None:
        kind='H' if p['b1_type']==7 else 'L'; p['b1_gain']=round(float(plateau_corner(c,kind)[0]),3)
    q={'b1_used':1,'b1_char':1,'quality':mode}; q.update(p)
    m,info=render(q,'t')
    lim=(G>20)&(G<20000)
    e=np.max(np.abs(m-c)[lim]); res.append(e)
    print(f'{name:28s} max diff vs real SSL {e:5.2f} dB   (mode: {info["mode"]}, latency {info["latency"]})')
print('WORST %.2f dB, median %.2f dB'%(max(res),np.median(res)))
