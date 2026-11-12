# G-mode low-pass: LP2(f1,q1) x (zero pair fz,qz)/(pole pair fp,qp), table vs -3 dB frequency
from fitlib import *
from scipy.optimize import least_squares
import json
U='/mnt/user-data/uploads/Documents/MEX-EQ/captures/'
s=segments(U+'E7_gch_lowpass.wav')
def m(p,fr):
    s_=1j*fr; f1,q1,fz,qz,fp,qp=np.exp(p)
    H=1/((s_/f1)**2+s_/(f1*q1)+1)*((s_/fz)**2+s_/(fz*qz)+1)/((s_/fp)**2+s_/(fp*qp)+1); return 20*np.log10(np.abs(H))
rows=[]; prev=None
for k in range(10,2,-1):     # strongest (lowest cutoff) first for continuity
    c=s[k][2]; f3=float(np.exp(np.interp(-3,c[::-1],np.log(G)[::-1])))
    x0=prev if prev is not None else np.log([f3*1.07,.67,17000,1.56,28000,.61])
    lb=np.log([200,.3,12000,.5,20000,.3]); ub=np.log([30000,3,24000,5,60000,3])
    r=least_squares(lambda p: np.concatenate([m(p,G)-c,0.01*(p-x0)]),np.clip(x0,lb+1e-6,ub-1e-6),bounds=(lb,ub))
    e=np.max(np.abs(m(r.x,G)-c)); prev=r.x
    rows.append([f3]+list(np.exp(r.x))); print(f'f3 {f3:7.0f}  LP {np.exp(r.x[0]):7.0f} q {np.exp(r.x[1]):.3f} | zero {np.exp(r.x[2]):6.0f} q {np.exp(r.x[3]):.2f} | pole {np.exp(r.x[4]):6.0f} q {np.exp(r.x[5]):.2f}  max err {e:.3f}')
rows=sorted(rows)
json.dump(rows,open('lp_table.json','w'))
