import numpy as np, soundfile as sf
sr=44100; N=8192; rng=np.random.default_rng(7)
f=np.fft.rfftfreq(N,1/sr); X=np.zeros(len(f),complex)
band=(f>=15)&(f<=20500)
# Schroeder-like phases for low crest; flat magnitude
k=np.arange(len(f)); ph=-np.pi*k*(k-1)/band.sum()
X[band]=np.exp(1j*ph[band])
x=np.fft.irfft(X,N); x/=np.sqrt(np.mean(x**2)); x*=10**(-20/20)
print('crest dB',20*np.log10(np.max(abs(x))/np.sqrt(np.mean(x**2))),'peak dBFS',20*np.log10(np.max(abs(x))))
P=int(np.ceil(240*sr/N)); y=np.tile(x,P)
sf.write('captures/MEX_multisine_loop.wav',y.astype(np.float32),sr,subtype='PCM_24')
np.save('captures/ms_period.npy',x); print(len(y)/sr,'s')
