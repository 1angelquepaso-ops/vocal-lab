import numpy as np
def shelf_c(kind,f0,g,Q,f):
    s=1j*f/f0; A=10**(g/40); sa=np.sqrt(A)/Q
    return A*(s*s+sa*s+A)/(A*s*s+sa*s+1) if kind=='L' else A*(A*s*s+sa*s+1)/(s*s+sa*s+A)
def bell_c(f0,g,Q,f):
    s=1j*f/f0; A=10**(g/40); return (s*s+s*A/Q+1)/(s*s+s/(A*Q)+1)
def sb(kind,p,f,nb):
    # p: [ln f0, g, ln Q, (ln fb, gb, ln Qb)*nb]
    H=shelf_c(kind,np.exp(p[0]),p[1],np.exp(p[2]),f)
    for i in range(nb):
        H=H*bell_c(np.exp(p[3+3*i]),p[4+3*i],np.exp(p[5+3*i]),f)
    return 20*np.log10(np.abs(H))
