# Exact formulas used by the C++ engine for Clean-mode orders (Parametric EQ 2 style)
import numpy as np
def proto(p,M,r,qs):
    H=np.ones_like(p,dtype=complex)
    for k in range(M//2):
        a=2*np.sin((2*k+1)*np.pi/(2*M))/qs
        H*= (p*p+a*r*p+r*r)/(p*p+a*p+1)
    if M%2: H*=(p+r)/(p+1)
    return H
def low_shelf(f,f0,g,M,qs):
    r=10**(g/20/M); return proto(1j*f/f0*np.sqrt(r),M,r,qs)
def high_shelf(f,f0,g,M,qs):
    r=10**(g/20/M); return proto(np.sqrt(r)*f0/(1j*f),M,r,qs)
def peak(f,f0,g,M,Q,qs):
    r=10**(g/20/M); s=1j*f/f0; return proto(np.sqrt(r)*Q*(s+1/s),M,r,qs)
