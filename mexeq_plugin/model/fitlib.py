import sys; sys.path.insert(0,'/home/claude/mexeq')
from ms_an import *
from scipy.optimize import least_squares
def peak_db(f0,g,Q,fr):
    s=1j*fr/f0; A=10**(g/40); H=(s**2+s*A/Q+1)/(s**2+s/(A*Q)+1); return 20*np.log10(np.abs(H))
def fitbell(c):
    i=np.argmax(np.abs(c)); x0=[G[i],c[i],0.5]
    r=least_squares(lambda p: peak_db(p[0],p[1],p[2],G)-c, x0, bounds=([10,-30,0.05],[30000,30,10]))
    return r.x, np.sqrt(np.mean(r.fun**2))
def report(path):
    s=segments(path)
    for k,(t,d,c) in enumerate(s):
        x=''
        if abs(c).max()>0.3:
            p,e=fitbell(c); x=f' || bell f0 {p[0]:.0f} g {p[1]:+.2f} Q {p[2]:.2f} rms {e:.2f}'
        print(f"{k:2d} t={t:6.1f}s dur={d:4.1f}  {describe(c)}{x}")
    return s
