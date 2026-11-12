import json,numpy as np, sys
from dsp_py import *
lp=np.array(json.load(open('lp_table.json'))); sh=json.load(open('shelf_tables.json'))
F=np.geomspace(20,20000,300)
def secs_bell(f0,g,Q):
    w=2*np.pi*f0; A=10**(g/40); return (w*w,A*w/Q,1,w*w,w/(A*Q),1)
def secs_lshelf(f0,g,Q):
    w=2*np.pi*f0;A=10**(g/40);sa=np.sqrt(A)/Q; return (A*A*w*w,A*sa*w,A,w*w,sa*w,A)
def secs_hshelf(f0,g,Q):
    w=2*np.pi*f0;A=10**(g/40);sa=np.sqrt(A)/Q; return (A*w*w,A*sa*w,A*A,A*w*w,sa*w,1)
cases=[]
for row in lp:
    f3,f1,q1,fz,qz,fp,qp=row; w1=2*np.pi*f1; wz=2*np.pi*fz; wp=2*np.pi*fp
    cases.append(('LP %.0f'%f3,[(w1*w1,0,0,w1*w1,w1/q1,1),(1,1/(wz*qz),1/(wz*wz),1,1/(wp*qp),1/(wp*wp))]))
for kind in 'LH':
    D=sh[kind]; T=np.array(D['table'])
    for ki in range(0,T.shape[0],4):
        for gj in range(0,T.shape[1],5):
            p=T[ki,gj]
            for scale in (1.0,3.0):
                sh_=secs_lshelf if kind=='L' else secs_hshelf
                cases.append((f'{kind}S k{ki} g{D["gs"][gj]} x{scale}',[sh_(np.exp(p[0])*scale,p[1],np.exp(p[2])),secs_bell(np.exp(p[3])*scale,p[4],np.exp(p[5])),secs_bell(np.exp(p[6])*scale,p[7],np.exp(p[8]))]))
for f0 in [30,100,1000,5000,10000,15000,19000,21000]:
    for g in [-21.5,-12,-3,3,12,21.5]:
        for Q in [0.2,0.7,4,20]: cases.append((f'bell {f0} {g} {Q}',[secs_bell(f0,g,Q)]))
        for Q in [0.5,0.7071,1.2]:
            cases.append((f'lshelf {f0} {g} {Q}',[secs_lshelf(f0,g,Q)])); cases.append((f'hshelf {f0} {g} {Q}',[secs_hshelf(f0,g,Q)]))
def run(method,srs=(44100,48000,88200)):
    for sr in srs:
        res=[]
        for name,ss in cases:
            an=np.prod([asec_at(s,2*np.pi*F) for s in ss],axis=0)
            dg=np.prod([dig_at(method(s,sr),F,sr) for s in ss],axis=0)
            res.append((np.max(np.abs(20*np.log10(abs(dg)+1e-15)-20*np.log10(abs(an)+1e-15))),name))
        es=sorted(res,reverse=True); E=[e for e,_ in es]
        print(sr,'worst %.2f | 99pct %.3f | 95pct %.3f | median %.4f'%(E[0],np.percentile(E,99),np.percentile(E,95),np.median(E)),'| worst:',[(round(e,2),n) for e,n in es[:5]])
if __name__=='__main__':
    import dsp_py; run(getattr(dsp_py,sys.argv[1]))
