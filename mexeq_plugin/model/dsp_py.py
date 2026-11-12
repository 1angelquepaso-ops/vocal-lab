# Python twin of Design.h matched() for experiments
import numpy as np
def asec_at(s,w):
    b0,b1,b2,a0,a1,a2=s; S=1j*w; return (b2*S*S+b1*S+b0)/(a2*S*S+a1*S+a0)
def matched(s,sr):
    b0_,b1_,b2_,a0,a1,a2=s; T=1/sr; nyq=np.pi*sr
    if a2==0:
        p=-a0/a1; da1=-np.exp(p*T); da2=0; pw=abs(p)
    else:
        A=a1/a2; B=a0/a2; disc=A*A-4*B
        if disc<0:
            sig=-A/2; om=np.sqrt(-disc)/2; da1=-2*np.exp(sig*T)*np.cos(om*T); da2=np.exp(2*sig*T)
        else:
            p1=(-A+np.sqrt(disc))/2; p2=(-A-np.sqrt(disc))/2; da1=-(np.exp(p1*T)+np.exp(p2*T)); da2=np.exp((p1+p2)*T)
        pw=np.sqrt(abs(B))
    H2=lambda w: abs(asec_at(s,w))**2
    if b2_==0 and a2==0:
        H0=np.sqrt(H2(0)); Hn=np.sqrt(H2(nyq)); s0=H0*(1+da1); sn=Hn*(1-da1); return ((s0+sn)/2,(s0-sn)/2,0,da1,0)
    A0=(1+da1+da2)**2; A1=(1-da1+da2)**2; A2=-4*da2
    wm=np.clip(pw,0.02*nyq,0.9*nyq)
    s2=np.sin(wm*T/2)**2; phi0=1-s2; phi1=s2; phi2=4*phi0*phi1
    Am=A0*phi0+A1*phi1+A2*phi2; B0=H2(0)*A0; B1=H2(nyq)*A1; B2=(H2(wm)*Am-B0*phi0-B1*phi1)/phi2
    sB0=np.sqrt(max(B0,0)); sB1=np.sqrt(max(B1,0)); W=0.5*(sB0+sB1); b0=0.5*(W+np.sqrt(max(W*W+B2,0)))
    return (b0,0.5*(sB0-sB1),-B2/(4*b0) if b0>1e-30 else 0,da1,da2)
def dig_at(d,f,sr):
    b0,b1,b2,a1,a2=d; z=np.exp(-2j*np.pi*f/sr); return (b0+b1*z+b2*z*z)/(1+a1*z+a2*z*z)

def poles_digital(s,sr,clamp=0.9):
    b0_,b1_,b2_,a0,a1,a2=s; T=1/sr; nyq=np.pi*sr
    if a2==0:
        p=-a0/a1; return -np.exp(p*T),0.0
    A=a1/a2; B=a0/a2; disc=A*A-4*B; w0=np.sqrt(abs(B))
    if disc<0:
        zeta=A/(2*w0)
        if w0>clamp*nyq: w0=clamp*nyq
        sig=-zeta*w0; om=w0*np.sqrt(max(1-zeta*zeta,0))
        return -2*np.exp(sig*T)*np.cos(om*T), np.exp(2*sig*T)
    p1=(-A+np.sqrt(disc))/2; p2=(-A-np.sqrt(disc))/2
    p1=max(p1,-clamp*nyq*3); p2=max(p2,-clamp*nyq*3)
    return -(np.exp(p1*T)+np.exp(p2*T)), np.exp((p1+p2)*T)

def matched_ls(s,sr,K=40):
    b0_,b1_,b2_,a0,a1,a2=s; T=1/sr; nyq=np.pi*sr
    da1,da2=poles_digital(s,sr)
    w=np.concatenate([[0.0],2*np.pi*np.geomspace(10,0.499*sr,K)])
    st=np.sin(w*T/2)**2; phi0=1-st; phi1=st; phi2=4*phi0*phi1
    A0=(1+da1+da2)**2; A1=(1-da1+da2)**2; A2=-4*da2
    Ad=A0*phi0+A1*phi1+A2*phi2
    tgt=np.abs(asec_at(s,w))**2*Ad
    Wt=1/(tgt+1e-12)
    M=np.stack([phi0,phi1,phi2],1)
    if b2_==0 and a2==0:   # first order: B2 = 0
        M=M[:,:2]
    MtW=M.T*Wt; Bv=np.linalg.solve(MtW@M,MtW@tgt)
    B0,B1=Bv[0],Bv[1]; B2=Bv[2] if len(Bv)>2 else 0.0
    # exact DC where the analog has a finite DC gain
    sB0=np.sqrt(max(B0,0)); sB1=np.sqrt(max(B1,0)); W=0.5*(sB0+sB1); b0=0.5*(W+np.sqrt(max(W*W+B2,0)))
    return (b0,0.5*(sB0-sB1),-B2/(4*b0) if b0>1e-30 else 0,da1,da2)

def matched_inv(s,sr):
    b0_,b1_,b2_,a0,a1,a2=s; nyq=np.pi*sr
    if b2_!=0 and a2!=0 and b0_>0 and a0>0:
        wz=np.sqrt(b0_/b2_); wp=np.sqrt(a0/a2)
        zz=b1_/(2*np.sqrt(b0_*b2_)); zp=a1/(2*np.sqrt(a0*a2))
        if zz<zp or (wp>0.9*nyq and wz<wp):
            d=matched((a0,a1,a2,b0_,b1_,b2_),sr)   # inverse section
            b0,b1,b2,da1,da2=d
            return (1/b0, da1/b0, da2/b0, b1/b0, b2/b0)
    return matched(s,sr)

def ls_inv(s,sr):
    b0_,b1_,b2_,a0,a1,a2=s; nyq=np.pi*sr
    if b2_!=0 and a2!=0 and b0_>0 and a0>0:
        wz=np.sqrt(b0_/b2_); wp=np.sqrt(a0/a2)
        zz=b1_/(2*np.sqrt(b0_*b2_)); zp=a1/(2*np.sqrt(a0*a2))
        if zz<zp or (wp>0.9*nyq and wz<wp):
            b0,b1,b2,da1,da2=matched_ls((a0,a1,a2,b0_,b1_,b2_),sr)
            return (1/b0, da1/b0, da2/b0, b1/b0, b2/b0)
    return matched_ls(s,sr)

_FE=np.geomspace(20,20000,20)
def hybrid(s,sr):
    a=matched_inv(s,sr); b=ls_inv(s,sr)
    w=2*np.pi*np.minimum(_FE,0.49*sr)
    an=20*np.log10(np.abs(asec_at(s,w))+1e-15)
    ea=np.max(np.abs(20*np.log10(np.abs(dig_at(a,w/2/np.pi,sr))+1e-15)-an))
    eb=np.max(np.abs(20*np.log10(np.abs(dig_at(b,w/2/np.pi,sr))+1e-15)-an))
    return a if ea<=eb else b
