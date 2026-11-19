"""PYREX MAGIK render: every stem of one song through the SAME time map -> locked WAVs.

Global lock: one ratio (source BPM / target BPM). The file starts on a bar line (silence padded
when the song's first bar line is before 0 s), so dropping it at playlist bar 1 locks it.
Time-only stretch (Rubber Band R3): pitch is untouched by construction. One resample
(source rate -> FL project rate) after the stretch. Every output gets the identical length.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import soundfile as sf


def rubberband_exe():
    env = os.environ.get('RUBBERBAND')
    if env and os.path.exists(env):
        return env
    here = os.path.dirname(sys.executable if getattr(sys, 'frozen', False) else __file__)
    for n in ('rubberband.exe', 'rubberband'):
        p = os.path.join(here, n)
        if os.path.exists(p):
            return p
    return shutil.which('rubberband') or 'rubberband'


def out_name(title, stem, bpm, key_from, key_to=None):
    k = f'{key_from}→{key_to}' if key_to and key_to != key_from else key_from
    safe = lambda x: ''.join(c for c in str(x) if c not in '\\/:*?"<>|').strip()
    return f'{safe(title)} - PYREX MAGIK - {safe(stem)} - {bpm:g}bpm - {safe(k)}.wav'


NOTES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']


def shift_key(name, st):
    if not name:
        return name
    minor = name.endswith('m')
    root = name[:-1] if minor else name
    if root not in NOTES:
        return name
    return NOTES[(NOTES.index(root) + int(st)) % 12] + ('m' if minor else '')


def plan(an, target_bpm, out_sr=44100, transpose=0, nudge_beats=0, lock=True):
    c = an['clock']
    src_bpm = c['bpm']
    if not lock:                                  # BPM Lock off: keep the song's tempo
        target_bpm = src_bpm
    ratio = src_bpm / target_bpm                  # output duration / input duration
    # <= 0: bar line at or before the file start; a nudge moves the "1" by whole beats
    start = c['file_start_s'] + nudge_beats * 60.0 / src_bpm
    pad = int(round(-start * an['sr'])) if start < 0 else 0
    skip = int(round(start * an['sr'])) if start > 0 else 0
    src_len = an['stems'][0]['frames'] + pad - skip
    out_len = int(np.ceil(src_len / an['sr'] * ratio * out_sr))
    return dict(ratio=ratio, pad=pad, skip=skip, out_len=out_len, out_sr=out_sr, target_bpm=target_bpm,
                src_bpm=src_bpm, start_s=start, transpose=int(transpose), nudge_beats=int(nudge_beats))


def render_stem(path, P, dst, tmp, vocal=False):
    import soxr
    y, sr = sf.read(path, always_2d=True, dtype='float32')
    if P['pad']:
        y = np.concatenate([np.zeros((P['pad'], y.shape[1]), np.float32), y])
    if P['skip']:
        y = y[P['skip']:]
    a = os.path.join(tmp, os.path.basename(dst) + '.in.wav')
    b = os.path.join(tmp, os.path.basename(dst) + '.out.wav')
    sf.write(a, y, sr, subtype='FLOAT')
    if abs(P['ratio'] - 1) < 1e-9 and not P['transpose']:
        shutil.copy(a, b)
    else:
        # ONE pass: time ratio + (optional) pitch shift; voices keep their formants when transposed
        cmd = [rubberband_exe(), '-3', '-q', '-t', f"{P['ratio']:.12f}"]
        if P['transpose']:
            cmd += ['-p', str(P['transpose'])]
            if vocal:
                cmd += ['--formant']
        cmd += [a, b]
        subprocess.run(cmd, check=True, capture_output=True,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    z, _ = sf.read(b, always_2d=True, dtype='float32')
    if sr != P['out_sr']:
        z = soxr.resample(z, sr, P['out_sr'], quality='VHQ').astype(np.float32)
    n = P['out_len']
    z = z[:n] if len(z) >= n else np.concatenate([z, np.zeros((n - len(z), z.shape[1]), np.float32)])
    sf.write(dst, z, P['out_sr'], subtype='FLOAT')
    for f in (a, b):
        os.remove(f)
    return dst


def render_song(an, target_bpm, out_dir, out_sr=44100, workers=None, progress=None, transpose=0,
                nudge_beats=0, lock=True, key_name=None):
    say = progress or (lambda f, m: None)
    P = plan(an, target_bpm, out_sr, transpose, nudge_beats, lock)
    target_bpm = P['target_bpm']
    workers = workers or max(1, min(4, (os.cpu_count() or 2)))
    os.makedirs(out_dir, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='pyrex_r_')
    key = key_name or (an.get('key') or {}).get('key') or 'key'
    key_to = shift_key(key, transpose) if transpose else None
    P['key_from'], P['key_to'] = key, key_to
    jobs = []
    for s in an['stems']:
        if s.get('empty'):
            continue
        dst = os.path.join(out_dir, out_name(an['title'], s['name'], round(target_bpm, 2), key, key_to))
        jobs.append((s['name'], s['path'], dst, s.get('role') == 'vocal'))
    done = 0
    outs = {}
    with ThreadPoolExecutor(max_workers=workers) as ex:
        futs = {ex.submit(render_stem, p, P, d, tmp, v): n for n, p, d, v in jobs}
        for f in futs:
            outs[futs[f]] = f.result()
            done += 1
            say(0.05 + 0.85 * done / len(jobs), f'Rendered {done}/{len(jobs)} stems')
    shutil.rmtree(tmp, ignore_errors=True)
    return dict(plan=P, files=outs)


def compensate(an, res, max_ms=10.0):
    """The stretcher delays transients by a constant ~1.4 ms (Rubber Band R3, measured on the
    drums against the same hits in the source). Shift EVERY stem by that same amount so the
    hits land on the FL lines and the stems stay locked to each other. Length unchanged."""
    from .check import matched_hits
    P, files = res['plan'], res['files']
    _, err = matched_hits(an, P, files[an['clock']['source']])
    e = float(np.median(err))
    n = int(round(e / 1000 * P['out_sr']))
    if abs(e) < 0.3 or abs(e) > max_ms or n == 0:
        res['latency_ms'] = round(e, 2)
        res['compensated_samples'] = 0
        return res
    for f in files.values():
        z, sr = sf.read(f, always_2d=True, dtype='float32')
        pad = np.zeros((abs(n), z.shape[1]), np.float32)
        z = np.concatenate([z[n:], pad]) if n > 0 else np.concatenate([pad, z[:n]])
        sf.write(f, z, sr, subtype='FLOAT')
    res['latency_ms'] = round(e, 2)
    res['compensated_samples'] = n
    return res
