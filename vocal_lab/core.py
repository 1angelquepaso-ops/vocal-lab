"""vocal-lab core: shared pieces for the harmonizer (and later the aligner).

load/save audio, pitch tracking, note segmentation, and a formant-preserving
pitch-shift "bank" built on the Rubber Band R3 engine.
"""
import os
import shutil
import subprocess
import sys
import tempfile

import librosa
import numpy as np
import soundfile as sf

NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
PT_SR = 22050          # analysis rate for pitch tracking
PT_HOP = 256           # ~11.6 ms frames


def note_name(m):
    m = int(round(m))
    return f"{NOTE_NAMES[m % 12]}{m // 12 - 1}"


# ---------------------------------------------------------------- audio I/O
def load(path, start=None, end=None):
    """Load audio as mono float32. Dual-mono files collapse losslessly."""
    info = sf.info(path)
    sr = info.samplerate
    a = int(start * sr) if start else 0
    b = int(end * sr) if end else None
    y, sr = sf.read(path, start=a, stop=b, always_2d=True, dtype='float32')
    return y.mean(axis=1), sr


def save(path, y, sr, subtype='PCM_24'):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    sf.write(path, y, sr, subtype=subtype)


# ---------------------------------------------------------- pitch tracking
def track_pitch(y, sr, fmin='E2', fmax='C6'):
    """pYIN pitch track. Returns (times, midi) with NaN where unvoiced."""
    y22 = librosa.resample(y, orig_sr=sr, target_sr=PT_SR)
    f0, voiced, _ = librosa.pyin(
        y22, fmin=librosa.note_to_hz(fmin), fmax=librosa.note_to_hz(fmax),
        sr=PT_SR, frame_length=2048, hop_length=PT_HOP)
    midi = np.where(voiced & ~np.isnan(f0),
                    69 + 12 * np.log2(np.nan_to_num(f0, nan=1.0) / 440.0),
                    np.nan)
    times = librosa.frames_to_time(np.arange(len(midi)), sr=PT_SR,
                                   hop_length=PT_HOP)
    return times, midi


# ------------------------------------------------------ note segmentation
def segment_notes(times, midi, min_dur=0.09, gap_bridge=0.05, split_jump=0.75):
    """Split a pitch track into notes.

    A note is a run of voiced frames whose pitch stays near one semitone.
    Vibrato and small bends stay inside one note; a jump of more than
    `split_jump` semitones from the running note centre starts a new note.
    Returns a list of dicts: start, end, pitch (float median), note (int).
    """
    hop = times[1] - times[0]
    notes = []
    cur = None

    def close(c):
        if c and c['end'] - c['start'] >= min_dur:
            p = float(np.median(c['vals']))
            notes.append({'start': c['start'], 'end': c['end'],
                          'pitch': p, 'note': int(round(p))})

    gap = 0.0
    for t, m in zip(times, midi):
        if np.isnan(m):
            gap += hop
            if cur and gap > gap_bridge:
                close(cur)
                cur = None
            continue
        gap = 0.0
        if cur is None:
            cur = {'start': t, 'end': t + hop, 'vals': [m]}
            continue
        centre = np.median(cur['vals'][-12:])
        if abs(m - centre) > split_jump:
            close(cur)
            cur = {'start': t, 'end': t + hop, 'vals': [m]}
        else:
            cur['vals'].append(m)
            cur['end'] = t + hop
    close(cur)

    # fix pitch-tracker octave errors: a note 9+ semitones from its
    # neighbourhood is almost always the tracker locking to a harmonic
    if len(notes) > 4:
        p = np.array([n['note'] for n in notes], dtype=float)
        for i, n in enumerate(notes):
            nb = np.median(np.delete(p[max(0, i - 5):i + 6], min(i, 5)))
            while n['note'] - nb > 8.5:
                n['pitch'] -= 12; n['note'] -= 12
            while nb - n['note'] > 8.5:
                n['pitch'] += 12; n['note'] += 12

    # merge neighbours that ended up on the same semitone across a tiny gap
    merged = []
    for n in notes:
        if merged and n['note'] == merged[-1]['note'] and \
                n['start'] - merged[-1]['end'] < gap_bridge * 2:
            merged[-1]['end'] = n['end']
        else:
            merged.append(dict(n))
    return merged


# ------------------------------------------------- pitch-shift bank
def rubberband_exe():
    """Rubber Band CLI: $RUBBERBAND, next to the bundled engine, or on PATH."""
    env = os.environ.get('RUBBERBAND')
    if env and os.path.exists(env):
        return env
    here = os.path.dirname(sys.executable if getattr(sys, 'frozen', False) else __file__)
    for name in ('rubberband.exe', 'rubberband'):
        p = os.path.join(here, name)
        if os.path.exists(p):
            return p
    return shutil.which('rubberband') or 'rubberband'


def shift(y, sr, semitones, formant=True):
    """Offline R3 pitch shift with formant preservation. Length-exact."""
    if semitones == 0:
        return y.copy()
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, 'in.wav'), os.path.join(d, 'out.wav')
        sf.write(src, y, sr, subtype='FLOAT')
        cmd = [rubberband_exe(), '-3', '-q', '-p', str(semitones)]
        if formant:
            cmd.append('-F')
        cmd += [src, dst]
        subprocess.run(cmd, check=True, capture_output=True,
                       creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        out, _ = sf.read(dst, dtype='float32')
    if out.ndim > 1:
        out = out.mean(axis=1)
    # enforce exact length so every bank entry is sample-aligned
    if len(out) < len(y):
        out = np.pad(out, (0, len(y) - len(out)))
    return out[:len(y)]


def merge_regions(regions, pad=0.0, gap=0.0):
    """Sort, pad and merge (start, end) second ranges that touch or nearly touch."""
    out = []
    for s, e in sorted((max(0.0, s - pad), e + pad) for s, e in regions):
        if out and s <= out[-1][1] + gap:
            out[-1][1] = max(out[-1][1], e)
        else:
            out.append([s, e])
    return [tuple(r) for r in out]


def shift_regions(y, sr, semitones, regions, formant=True, pad=0.3):
    """Pitch-shift only the given (start, end) second ranges, in ONE Rubber
    Band pass: the ranges (plus padding) are laid end to end with short
    silences between, shifted together, then put back in place. Everything
    outside the ranges stays silent. For a sparse harmony this is many times
    faster than shifting the whole file.
    Returns [(start_sample, audio), ...] chunks."""
    regs = merge_regions(regions, pad=pad, gap=2 * pad)
    if not regs:
        return []
    sil = np.zeros(int(pad * sr), dtype='float32')
    parts, spans, cur = [], [], 0
    for s, e in regs:
        a, b = int(s * sr), min(int(e * sr), len(y))
        if b <= a:
            continue
        parts += [y[a:b], sil]
        spans.append((a, cur, b - a))
        cur += (b - a) + len(sil)
    if not parts:
        return []
    joined = np.concatenate(parts).astype('float32')
    shifted = shift(joined, sr, semitones, formant)
    return [(a, shifted[o:o + n].copy()) for a, o, n in spans]


def build_bank(y, sr, intervals, formant=True):
    return {iv: shift(y, sr, iv, formant) for iv in sorted(set(intervals))}


def splice(bank, n_samples, sr, plan, xfade=0.025):
    """Build one voice from the bank.

    plan: list of (start_sec, end_sec, interval). Gaps between plan entries
    are silent; the voice is gated so it only sings where the lead sings.
    Each segment gets an equal-power fade in/out of `xfade` seconds.
    """
    out = np.zeros(n_samples, dtype='float32')
    fl = int(xfade * sr)
    for s, e, iv in plan:
        a = max(int(s * sr) - fl // 2, 0)
        b = min(int(e * sr) + fl // 2, n_samples)
        if b <= a:
            continue
        seg = bank[iv][a:b].copy()
        L = len(seg)
        f = min(fl, L // 2)
        if f > 0:
            ramp = np.sin(np.linspace(0, np.pi / 2, f)) ** 2
            seg[:f] *= ramp
            seg[-f:] *= ramp[::-1]
        out[a:b] += seg
    return out


def splice_human(bank, n_samples, sr, plan, rng, base_ms=22, spread_ms=14,
                 xfade=0.06, level_jitter_db=1.5):
    """Like splice(), but each segment gets its own timing offset and level.

    Offsets follow a slow random walk around base_ms, so the voice drifts
    ahead and behind the lead the way a second singer does, instead of
    sitting at one fixed delay. Longer crossfades smooth interval changes.
    """
    out = np.zeros(n_samples, dtype='float32')
    fl = int(xfade * sr)
    off = base_ms
    for s, e, iv in plan:
        off = float(np.clip(off + rng.normal(0, spread_ms / 2),
                            base_ms - spread_ms, base_ms + spread_ms))
        d = int(max(off, 0) / 1000 * sr)
        a = max(int(s * sr) - fl // 2, 0)
        b = min(int(e * sr) + fl // 2, n_samples)
        if b <= a:
            continue
        seg = bank[iv][a:b].copy() * 10 ** (rng.normal(0, level_jitter_db) / 20)
        L = len(seg)
        f = min(fl, L // 2)
        if f > 0:
            ramp = np.sin(np.linspace(0, np.pi / 2, f)) ** 2
            seg[:f] *= ramp
            seg[-f:] *= ramp[::-1]
        a2, b2 = a + d, min(b + d, n_samples)
        out[a2:b2] += seg[:b2 - a2]
    return out
