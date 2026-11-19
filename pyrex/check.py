"""After-checks: the render is judged by measuring the OUTPUT again, not by trusting the process.
1 length   every stem has the identical sample count
2 grid     drums re-analysed from scratch (TITO, no hint): BPM = target, backbone hits vs the IDEAL
           FL grid (bar lines at k * 4 * 60 / target from 0 s): drift + feel, as in analysis
3 bar 1    downbeat of the output = a bar line of the FL grid (no beat offset)
4 pitch    tuning (cents) + key of harmonic stems before vs after: unchanged
5 align    every stem vs its own source through the time map: same offset for all stems (< 1 ms)
"""
from __future__ import annotations

import numpy as np
import soundfile as sf


def _env(y, sr, hop):
    import librosa
    return librosa.onset.onset_strength(y=y, sr=sr, hop_length=hop)


def stem_offset_ms(src_path, out_path, P, seconds=(30, 90)):
    """Lag between the output and the source mapped through the time map, from onset envelopes
    (1.45 ms hop, parabolic peak). Same lag on every stem = stems still aligned to each other."""
    import soxr
    sr = 44100
    hop = 64
    y, ssr = sf.read(src_path, always_2d=True, dtype='float32')
    z, _ = sf.read(out_path, always_2d=True, dtype='float32')
    y = soxr.resample(y.mean(1), ssr, sr)
    z = z.mean(1)
    a, b = seconds
    # output window [a, b) <- source times start + t / ratio
    t_out = np.arange(int(a * sr), int(b * sr)) / sr
    t_src = P['start_s'] + t_out / P['ratio']
    ys = np.interp(t_src * sr, np.arange(len(y)), y, left=0, right=0)
    e1 = _env(ys.astype(np.float32), sr, hop)
    e2 = _env(z[int(a * sr):int(b * sr)].astype(np.float32), sr, hop)
    n = min(len(e1), len(e2))
    e1, e2 = e1[:n] - e1[:n].mean(), e2[:n] - e2[:n].mean()
    if e1.std() < 1e-6 or e2.std() < 1e-6:
        return None
    L = int(0.03 * sr / hop)
    cc = np.array([np.dot(e1[max(0, -l):n - max(0, l)], e2[max(0, l):n - max(0, -l)]) for l in range(-L, L + 1)])
    i = int(np.argmax(cc))
    d = 0.0
    if 0 < i < len(cc) - 1:
        den = cc[i - 1] - 2 * cc[i] + cc[i + 1]
        d = 0.5 * (cc[i - 1] - cc[i + 1]) / den if den else 0.0
    return round((i - L + d) * hop / sr * 1000, 2)


def matched_hits(an, P, out_drums):
    """The SAME backbone hits in source and output: each source kick / snare / clap is mapped
    through the time map and its real transient is found in the output on the waveform
    (TITO's sample-accurate steepest-rise refine). Returns pseudo hits at output times plus the
    stretcher's own timing error (found - predicted)."""
    import soxr
    from tito.transcribe import ASR, HOP, refine_times
    from .analyze import CLOCK_PADS, clock_from
    src = next(s['path'] for s in an['stems'] if s['name'] == an['clock']['source'])
    hs = [h for h in clock_from(src)['hits'] if h['key'] in CLOCK_PADS]
    v = np.array([h['vel'] for h in hs], float)
    hs = [h for h, x in zip(hs, v) if x >= np.percentile(v, 30)]
    z, sr = sf.read(out_drums, always_2d=True, dtype='float32')
    m44 = z.mean(1) if sr == 44100 else soxr.resample(z.mean(1), sr, 44100)
    pred = np.array([(h['t'] - P['start_s']) * P['ratio'] for h in hs])
    keep = (pred > 0.02) & (pred < len(m44) / 44100 - 0.02)
    pred = pred[keep]
    hs = [h for h, k in zip(hs, keep) if k]
    found = refine_times(np.round(pred * ASR / HOP).astype(int), m44)
    err = (found - pred) * 1000
    return [dict(t=float(t), vel=h['vel'], key=h['key']) for t, h in zip(found, hs)], err


def check_song(an, res, progress=None):
    import librosa
    from .analyze import clock_from, downbeat, grid_confidence, key_scores, load_mono
    say = progress or (lambda f, m: None)
    P, files = res['plan'], res['files']
    out = dict(checks={}, level='green', problems=[])
    # 1 length
    lens = {n: sf.info(f).frames for n, f in files.items()}
    out['checks']['length'] = dict(ok=len(set(lens.values())) == 1, frames=sorted(set(lens.values())))
    # 2 + 3 grid / bar 1 on the output drums
    say(0.2, 'Re-reading the output drums')
    dname = an['clock']['source']
    dc = clock_from(files[dname])
    period = 60 / P['target_bpm']
    dur = lens[dname] / P['out_sr']
    ideal = np.arange(0, dur + period, period)
    mh, err = matched_hits(an, P, files[dname])
    g = grid_confidence(mh, ideal)
    ph, style, _ = downbeat(ideal, mh)
    meas = dc['bpm']
    bpm_ok = any(abs(meas * f / P['target_bpm'] - 1) < 0.002 for f in (1, 0.5, 2))
    out['checks']['grid'] = dict(ok=bool(bpm_ok and g['drift_ms'] <= 10 and abs(g['offset_ms']) <= 3),
                                 measured_bpm=meas, target_bpm=P['target_bpm'], offset_ms=g['offset_ms'],
                                 drift_ms=g['drift_ms'], feel_ms=g['feel_ms'], median_ms=g['median_ms'],
                                 on_grid_10ms=g['on_grid_10ms'], n=g['n'], flagged=g['bad'],
                                 stretcher_err_ms=dict(median=round(float(np.median(err)), 2),
                                                       p95=round(float(np.percentile(np.abs(err), 95)), 2)))
    out['checks']['bar1'] = dict(ok=ph == 0, downbeat_beat=int(ph), style=style)
    # 4 pitch
    say(0.5, 'Checking pitch')
    pitch = {}
    for s in an['stems']:
        n = s['name']
        if n not in files or s.get('role') not in ('harmony', 'vocal'):
            continue
        y0, r0 = load_mono(s['path'])
        y1, r1 = load_mono(files[n])
        if r0 < -50:
            continue
        t0 = float(librosa.estimate_tuning(y=y0, sr=22050)) * 100
        t1 = float(librosa.estimate_tuning(y=y1, sr=22050)) * 100
        k0 = key_scores(librosa.feature.chroma_cqt(y=y0, sr=22050, hop_length=2048).sum(1))[0][1]
        k1 = key_scores(librosa.feature.chroma_cqt(y=y1, sr=22050, hop_length=2048).sum(1))[0][1]
        pitch[n] = dict(cents_before=round(t0, 1), cents_after=round(t1, 1), key_before=k0, key_after=k1,
                        ok=k0 == k1 and ('bass' in n.lower() or abs(t1 - t0) <= 5))
    out['checks']['pitch'] = dict(ok=all(v['ok'] for v in pitch.values()), stems=pitch)
    # 5 cross-stem alignment
    say(0.75, 'Checking stems line up')
    offs = {}
    for s in an['stems']:
        if s['name'] in files:
            offs[s['name']] = stem_offset_ms(s['path'], files[s['name']], P)
    vals = [v for v in offs.values() if v is not None]
    spread = (max(vals) - min(vals)) if vals else 0
    out['checks']['align'] = dict(ok=spread <= 1.0, offsets_ms=offs, spread_ms=round(spread, 2))
    # verdict
    for k, v in out['checks'].items():
        if not v['ok']:
            out['problems'].append(k)
    if 'length' in out['problems'] or 'align' in out['problems'] or 'pitch' in out['problems']:
        out['level'] = 'red'
    elif out['problems']:
        out['level'] = 'amber'
    return _plain(out)


def _plain(o):
    """numpy scalars -> plain Python (JSON-safe)."""
    if isinstance(o, dict):
        return {k: _plain(v) for k, v in o.items()}
    if isinstance(o, (list, tuple)):
        return [_plain(v) for v in o]
    return o.item() if hasattr(o, 'item') else o
