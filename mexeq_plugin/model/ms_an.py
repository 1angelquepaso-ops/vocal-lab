import numpy as np, soundfile as sf, sys, json
sr=44100; N=8192
x=np.load('captures/ms_period.npy'); X=np.fft.rfft(x); f=np.fft.rfftfreq(N,1/sr)
band=np.abs(X)>1e-6*np.abs(X).max()
def frames(path):
    y,fs=sf.read(path,always_2d=True); y=y[:,0]
    nf=len(y)//N; Y=np.fft.rfft(y[:nf*N].reshape(nf,N),axis=1)
    rms=np.sqrt(np.mean(y[:nf*N].reshape(nf,N)**2,axis=1))
    H=np.full(Y.shape,np.nan); H[:,band]=20*np.log10(np.abs(Y[:,band])/np.abs(X[band])+1e-12)
    return H,rms
# log grid
G=np.geomspace(20,20000,240)
def onlog(Hrow):
    m=band&(f>15)
    return np.interp(np.log(G),np.log(f[m]),Hrow[m])
def segments(path,tol=0.08,minlen=3):
    H,rms=frames(path); L=np.array([onlog(h) for h in H])
    active=rms>1e-4
    segs=[];start=None
    for i in range(len(L)):
        ok= active[i] and (i>0 and active[i-1] and np.max(np.abs(L[i]-L[i-1]))<tol)
        if ok and start is None: start=i-1
        if (not ok) and start is not None:
            if i-start>=minlen: segs.append((start,i))
            start=None
    if start is not None and len(L)-start>=minlen: segs.append((start,len(L)))
    out=[]
    for a,b in segs:
        c=np.median(L[a+1:b-1] if b-a>3 else L[a:b],axis=0); out.append((a*N/sr,(b-a)*N/sr,c))
    return out
def describe(c):
    i=np.argmax(np.abs(c)); pk=c[i]; fp=G[i]
    if abs(pk)<0.3: return f"flat (max {pk:+.2f})"
    half=pk/2; sel=np.where(np.sign(pk)*(c-half)>0)[0]
    lo,hi=G[sel.min()],G[sel.max()]
    return f"peak {pk:+6.2f} dB @ {fp:7.0f} Hz | half-gain {lo:6.0f}-{hi:6.0f} Hz  Q~{fp/(hi-lo):.2f} | 30Hz {c[0]:+.1f} 20k {c[-1]:+.1f}"
if __name__=='__main__':
    s=segments(sys.argv[1])
    for k,(t,d,c) in enumerate(s): print(f"{k:2d} t={t:6.1f}s dur={d:4.1f}  {describe(c)}")
