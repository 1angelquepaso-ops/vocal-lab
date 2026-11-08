"""Polyphonic note transcription.

A trimmed, dependency-light port of Spotify's basic-pitch inference and note
decoding (Apache-2.0, see BASIC_PITCH_LICENSE.txt), running the bundled
ICASSP-2022 ONNX model with onnxruntime. Returns raw note events plus the
frame/onset activation maps so later stages can re-check evidence.
"""
from __future__ import annotations

import os
import sys
from typing import List, Optional, Tuple

import numpy as np
import scipy.signal

SR = 22050
FFT_HOP = 256
ANNOT_FPS = SR // FFT_HOP                      # 86
WINDOW_S = 2
ANNOT_N_FRAMES = ANNOT_FPS * WINDOW_S          # 172
AUDIO_N_SAMPLES = SR * WINDOW_S - FFT_HOP      # 43844
OVERLAP_FRAMES = 30
OVERLAP_LEN = OVERLAP_FRAMES * FFT_HOP
HOP_SIZE = AUDIO_N_SAMPLES - OVERLAP_LEN
MIDI_OFFSET = 21
MAX_FREQ_IDX = 87
MAGIC_ALIGNMENT_OFFSET = 0.0018

_session = None


def _model_path() -> str:
    here = os.path.dirname(os.path.abspath(__file__))
    cands = [os.path.join(here, "nmp.onnx")]
    if getattr(sys, "frozen", False):
        base = getattr(sys, "_MEIPASS", os.path.dirname(sys.executable))
        cands += [os.path.join(base, "pirata", "nmp.onnx"), os.path.join(base, "nmp.onnx")]
    for c in cands:
        if os.path.exists(c):
            return c
    raise FileNotFoundError("nmp.onnx model not found")


def _model():
    global _session
    if _session is None:
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.log_severity_level = 3
        _session = ort.InferenceSession(_model_path(), sess_options=so,
                                        providers=["CPUExecutionProvider"])
    return _session


def _unwrap(out: np.ndarray, n_orig: int) -> np.ndarray:
    n_olap = OVERLAP_FRAMES // 2
    out = out[:, n_olap:-n_olap, :]
    out = out.reshape(out.shape[0] * out.shape[1], out.shape[2])
    n_frames_per_window = ANNOT_N_FRAMES - OVERLAP_FRAMES
    return out[: int(n_orig / HOP_SIZE * n_frames_per_window), :]


def activations(y22: np.ndarray) -> dict:
    """Run the network on mono audio at 22050 Hz."""
    sess = _model()
    n_orig = len(y22)
    a = np.concatenate([np.zeros(OVERLAP_LEN // 2, np.float32), y22.astype(np.float32)])
    outs = {"note": [], "onset": [], "contour": []}
    for i in range(0, len(a), HOP_SIZE):
        w = a[i:i + AUDIO_N_SAMPLES]
        if len(w) < AUDIO_N_SAMPLES:
            w = np.pad(w, (0, AUDIO_N_SAMPLES - len(w)))
        x = w[None, :, None].astype(np.float32)
        r = sess.run(["StatefulPartitionedCall:1", "StatefulPartitionedCall:2",
                      "StatefulPartitionedCall:0"], {"serving_default_input_2:0": x})
        for k, v in zip(["note", "onset", "contour"], r):
            outs[k].append(v)
    return {k: _unwrap(np.concatenate(v), n_orig) for k, v in outs.items()}


def frames_to_time(n: int) -> np.ndarray:
    t = np.arange(n) * FFT_HOP / SR
    win = np.floor(np.arange(n) / ANNOT_N_FRAMES)
    off = (FFT_HOP / SR) * (ANNOT_N_FRAMES - AUDIO_N_SAMPLES / FFT_HOP) + MAGIC_ALIGNMENT_OFFSET
    return t - off * win


def _inferred_onsets(onsets, frames, n_diff=2):
    diffs = []
    for n in range(1, n_diff + 1):
        fa = np.concatenate([np.zeros((n, frames.shape[1])), frames])
        diffs.append(fa[n:, :] - fa[:-n, :])
    fd = np.min(diffs, axis=0)
    fd[fd < 0] = 0
    fd[:n_diff, :] = 0
    if fd.max() > 0:
        fd = onsets.max() * fd / fd.max()
    return np.max([onsets, fd], axis=0)


def _suppress_neighbours(rem, frames, a, b, f, ratio=0.6):
    """Clear semitone neighbours only where they look like bleed from note f.

    basic-pitch clears both neighbours unconditionally, which deletes real
    half-step clusters (B-C in a maj7, E-F in an add9...). Keep a neighbour
    wherever its own activation is a decent fraction of this note's."""
    ref = frames[a:b, f]
    for g in (f - 1, f + 1):
        if 0 <= g <= MAX_FREQ_IDX:
            bleed = frames[a:b, g] < ratio * ref
            rem[a:b, g][bleed] = 0


def decode(frames: np.ndarray, onsets: np.ndarray, onset_thresh=0.5, frame_thresh=0.3,
           min_note_len=11, energy_tol=11, melodia=True) -> List[Tuple[int, int, int, float]]:
    """basic-pitch's polyphonic decoder: (start_frame, end_frame, midi, amplitude)."""
    frames = frames.copy()
    n = frames.shape[0]
    onsets = _inferred_onsets(onsets.copy(), frames)
    peak = np.zeros(onsets.shape)
    pk = scipy.signal.argrelmax(onsets, axis=0)
    peak[pk] = onsets[pk]
    oi = np.where(peak >= onset_thresh)
    rem = frames.copy()
    ev = []
    for s, f in zip(oi[0][::-1], oi[1][::-1]):
        if s >= n - 1:
            continue
        i, k = s + 1, 0
        while i < n - 1 and k < energy_tol:
            k = k + 1 if rem[i, f] < frame_thresh else 0
            i += 1
        i -= k
        if i - s <= min_note_len:
            continue
        rem[s:i, f] = 0
        _suppress_neighbours(rem, frames, s, i, f)
        ev.append((int(s), int(i), int(f + MIDI_OFFSET), float(np.mean(frames[s:i, f]))))
    if melodia:
        while rem.max() > frame_thresh:
            im, f = np.unravel_index(np.argmax(rem), rem.shape)
            rem[im, f] = 0
            i, k = im + 1, 0
            while i < n - 1 and k < energy_tol:
                k = k + 1 if rem[i, f] < frame_thresh else 0
                rem[i, f] = 0
                _suppress_neighbours(rem, frames, i, i + 1, f)
                i += 1
            ie = i - 1 - k
            i, k = im - 1, 0
            while i > 0 and k < energy_tol:
                k = k + 1 if rem[i, f] < frame_thresh else 0
                rem[i, f] = 0
                _suppress_neighbours(rem, frames, i, i + 1, f)
                i -= 1
            ist = i + 1 + k
            if ie - ist <= min_note_len:
                continue
            ev.append((int(ist), int(ie), int(f + MIDI_OFFSET), float(np.mean(frames[ist:ie, f]))))
    return ev


def transcribe(y22: np.ndarray, onset_thresh=0.5, frame_thresh=0.3, min_note_ms=90.0):
    """Notes as dicts {start, end, pitch, amp} in seconds, plus activations."""
    act = activations(y22)
    min_len = max(3, int(round(min_note_ms / 1000 * ANNOT_FPS)))
    ev = decode(act["note"], act["onset"], onset_thresh, frame_thresh, min_len)
    t = frames_to_time(act["note"].shape[0] + 1)
    notes = [dict(start=float(t[s]), end=float(t[min(e, len(t) - 1)]), pitch=p, amp=a)
             for s, e, p, a in ev]
    notes.sort(key=lambda n: (n["start"], n["pitch"]))
    return notes, act
