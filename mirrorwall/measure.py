"""Measure the mix chain fingerprint of each vocal file."""
import glob, json, os, sys
import numpy as np, soundfile as sf, librosa, pyloudnorm as pyln
from scipy.signal import welch, butter, sosfiltfilt

D = "/mnt/user-data/uploads/IDEAS"
SR = 44100


def bands(y, sr):
    f, P = welch(y, sr, nperseg=8192)
    return f, P


def third_oct(f, P):
    cents = 1000 * 2 ** (np.arange(-17, 15) / 3)       # ~20 Hz .. 20 kHz
    out = []
    for c in cents:
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(P[m].mean() + 1e-20) if m.any() else np.nan)
    return cents, np.array(out)


def analyze(path_or_array, sr_in=None):
    if isinstance(path_or_array, str):
        x, sr = sf.read(path_or_array, always_2d=True)
        name = os.path.basename(path_or_array)
    else:
        x, sr = path_or_array, sr_in
        x = x if x.ndim == 2 else np.stack([x, x], 1)
        name = "array"
    if sr != SR:
        x = np.stack([librosa.resample(x[:, c], orig_sr=sr, target_sr=SR) for c in range(2)], 1)
        sr = SR
    L, R = x[:, 0], x[:, 1]
    mid = (L + R) / 2
    side = (L - R) / 2
    meter = pyln.Meter(sr)
    lufs = meter.integrated_loudness(x)
    peak = 20 * np.log10(np.abs(x).max() + 1e-12)

    # activity: 50 ms RMS frames
    hop = int(0.05 * sr)
    n = len(mid) // hop
    fr = mid[: n * hop].reshape(n, hop)
    rms = 20 * np.log10(np.sqrt((fr ** 2).mean(1)) + 1e-12)
    top = np.percentile(rms, 95)
    act = rms > top - 30
    # noise floor / gate: quietest 10% of frames
    floor = np.percentile(rms, 5)
    digital_silence = float((rms < -90).mean())

    # dynamics on active frames: spread of short-term level
    st = rms[act]
    spread = np.percentile(st, 95) - np.percentile(st, 10)
    # crest factor: peak vs rms per 400 ms on active audio
    seg = int(0.4 * sr)
    cf = []
    for i in range(0, len(mid) - seg, seg):
        s = mid[i:i + seg]
        r = np.sqrt((s ** 2).mean())
        if 20 * np.log10(r + 1e-12) > top - 20:
            cf.append(20 * np.log10(np.abs(s).max() / (r + 1e-12)))
    crest = float(np.median(cf)) if cf else np.nan

    # spectrum on active audio
    mask = np.repeat(act, hop)
    ya = mid[: n * hop][mask]
    f, P = bands(ya, sr)
    c, toct = third_oct(f, P)
    ref = toct[np.argmin(abs(c - 1000))]
    toct = toct - ref

    def band_db(lo, hi):
        m = (f >= lo) & (f < hi)
        return 10 * np.log10(P[m].sum() + 1e-20)
    tilt = {
        "sub<80": band_db(20, 80) - band_db(800, 1600),
        "low 80-250": band_db(80, 250) - band_db(800, 1600),
        "lowmid 250-600": band_db(250, 600) - band_db(800, 1600),
        "presence 2-5k": band_db(2000, 5000) - band_db(800, 1600),
        "sibil 5-10k": band_db(5000, 10000) - band_db(800, 1600),
        "air 10-16k": band_db(10000, 16000) - band_db(800, 1600),
        "top 16k+": band_db(16000, 21000) - band_db(800, 1600),
    }
    rolloff = float(np.median(librosa.feature.spectral_rolloff(y=ya[: sr * 60].astype(np.float32), sr=sr, roll_percent=0.99)))

    # sibilance peaks: 5-10k band level spikes vs body, 10 ms frames
    sos = butter(4, [5000, 10000], "bandpass", fs=sr, output="sos")
    sib = sosfiltfilt(sos, ya[: sr * 90])
    h2 = int(0.01 * sr)
    k = len(sib) // h2
    sibr = 20 * np.log10(np.sqrt((sib[: k * h2].reshape(k, h2) ** 2).mean(1)) + 1e-12)
    allr = 20 * np.log10(np.sqrt((ya[: k * h2].reshape(k, h2) ** 2).mean(1)) + 1e-12)
    rel = sibr - allr
    sib_peak = float(np.percentile(rel, 99) - np.percentile(rel, 50))

    # stereo
    width = 10 * np.log10((side ** 2).sum() / ((mid ** 2).sum() + 1e-20) + 1e-12)

    # reverb / tails: after strong phrase ends, how fast does level fall?
    tails = []
    for i in range(5, n - 40):
        if act[i - 5:i].all() and rms[i] > top - 12 and rms[i + 1] < rms[i] - 6:
            seg_db = rms[i + 1:i + 40]
            if np.all(seg_db < rms[i] - 3):
                tails.append(rms[i + 1:i + 30] - rms[i])
    if tails:
        T = np.median(np.array(tails), axis=0)
        t = np.arange(len(T)) * 0.05
        # level of tail 300-1000 ms after phrase end
        tail_level = float(np.median(T[6:20]))
        slope = np.polyfit(t[4:20], T[4:20], 1)[0]   # dB/s
        rt60 = float(-60 / slope) if slope < -1 else np.inf
    else:
        tail_level, rt60 = np.nan, np.nan

    # delay: autocorrelation of the side/mid envelope in 80 ms..1.2 s
    env = np.abs(librosa.onset.onset_strength(y=mid[: sr * 90].astype(np.float32), sr=sr, hop_length=256))
    ac = np.correlate(env - env.mean(), env - env.mean(), "full")[len(env) - 1:]
    ac /= ac[0]
    lags = np.arange(len(ac)) * 256 / sr
    m = (lags > 0.08) & (lags < 1.2)
    li = np.argmax(ac[m])
    delay_lag, delay_corr = float(lags[m][li]), float(ac[m][li])
    # side-only echoes are a cleaner delay signal
    envs = np.abs(librosa.onset.onset_strength(y=side[: sr * 90].astype(np.float32), sr=sr, hop_length=256))
    acs = np.correlate(envs - envs.mean(), envs - envs.mean(), "full")[len(envs) - 1:]
    acs /= acs[0] + 1e-12
    ls = np.argmax(acs[m])
    side_lag, side_corr = float(lags[m][ls]), float(acs[m][ls])

    # pitch correction: how tightly do voiced notes sit on semitones
    yy = librosa.resample(mid[: sr * 75].astype(np.float32), orig_sr=sr, target_sr=16000)
    f0, vf, vp = librosa.pyin(yy, fmin=70, fmax=900, sr=16000, frame_length=1024, hop_length=160)
    f0 = f0[vf & (vp > 0.6)]
    midi = librosa.hz_to_midi(f0)
    # best global tuning offset, then deviation
    best = None
    for off in np.linspace(-0.5, 0.5, 41):
        d = (midi - off) - np.round(midi - off)
        s = np.mean(np.abs(d))
        if best is None or s < best[0]:
            best = (s, off, d)
    dev_cents = best[2] * 100
    within10 = float(np.mean(np.abs(dev_cents) < 10))
    # transition speed: semitone jumps per frame
    dm = np.abs(np.diff(midi))
    snap = float(np.mean((dm > 0.6) & (dm < 3)))     # big frame-to-frame jumps = hard tune
    return dict(
        file=name, lufs=lufs, peak=peak, floor=float(floor),
        silence=digital_silence, spread=float(spread), crest=crest, tilt=tilt,
        toct=toct.tolist(), toct_f=c.tolist(), rolloff=rolloff, sib_peak=sib_peak,
        width=float(width), tail_level=tail_level, rt60=rt60,
        delay_lag=delay_lag, delay_corr=delay_corr, side_lag=side_lag, side_corr=side_corr,
        tune_within10=within10, tune_mad=float(best[0] * 100), tune_snap=snap,
        tuning_offset=float(best[1] * 100),
    )


