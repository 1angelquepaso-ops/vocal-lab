"""Instrumental analysis: tempo grid, chord timeline, bass, melody register.

Everything is read per beat on a fixed-tempo grid, so the harmonizer can
make decisions in musical time and export MIDI that lands on FL's grid.
"""
import numpy as np
import librosa
import soundfile as sf

NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
QUAL = {'': (0, 4, 7), 'm': (0, 3, 7), '7': (0, 4, 7, 10),
        'm7': (0, 3, 7, 10), 'maj7': (0, 4, 7, 11), 'sus2': (0, 2, 7),
        'dim': (0, 3, 6)}


def load_mono(path, sr=22050):
    y, s = sf.read(path, dtype='float32', always_2d=True)
    return librosa.resample(y.mean(1), orig_sr=s, target_sr=sr), sr


def fit_grid(y, sr, bpm_hint=None):
    """Steady-tempo beat grid: least-squares line through tracked beats."""
    oe = librosa.onset.onset_strength(y=y, sr=sr, aggregate=np.median)
    _, b = librosa.beat.beat_track(onset_envelope=oe, sr=sr, units='time',
                                   tightness=400)
    k = np.arange(len(b))
    period, start = np.polyfit(k, b, 1)
    bpm = 60 / period
    # fold double-time trap readings to the felt tempo
    while bpm > 115:
        period *= 2
        bpm /= 2
    if bpm_hint:
        bpm = bpm_hint
        period = 60 / bpm
    n = int((len(y) / sr - start) / period) + 1
    return float(bpm), float(start), start + period * np.arange(n)


def candidates(key_pcs, tonic):
    """Chord vocabulary: diatonic chords of the key, their 7ths, plus the
    usual borrowed colours (bII, major V, major IV)."""
    out = {}
    for root in range(12):
        for q, iv in QUAL.items():
            pcs = frozenset((root + i) % 12 for i in iv)
            diat = pcs <= set(key_pcs)
            borrowed = (root, q) in {((tonic + 1) % 12, ''),     # Phrygian bII
                                     ((tonic + 7) % 12, ''),     # major V
                                     ((tonic + 7) % 12, '7'),
                                     ((tonic + 5) % 12, '')}     # major IV (dorian)
            if (diat and q != 'dim') or borrowed:
                out[f'{NOTE[root]}{q}'] = (root, pcs, diat)
    return out


def chords(y, sr, grid, key_pcs, tonic, stay=0.82):
    """Per-beat chord labels via template matching + Viterbi.

    Uses harmonic chroma for the upper voices and a separate low-band chroma
    for the bass, so an 808 under the chord pulls toward the right root.
    """
    yh = librosa.effects.harmonic(y, margin=3.0)
    hop = 512
    ch = librosa.feature.chroma_cqt(y=yh, sr=sr, hop_length=hop,
                                    fmin=librosa.note_to_hz('C3'), n_octaves=4)
    bass = librosa.feature.chroma_cqt(y=yh, sr=sr, hop_length=hop,
                                      fmin=librosa.note_to_hz('A0'), n_octaves=3)
    fr = librosa.time_to_frames(grid, sr=sr, hop_length=hop)
    fr = np.clip(fr, 0, ch.shape[1] - 1)
    # sync() adds a segment before the first beat; drop it so column i is
    # the span from beat i to beat i+1
    C = librosa.util.sync(ch, fr, aggregate=np.median)[:, 1:]
    B = librosa.util.sync(bass, fr, aggregate=np.median)[:, 1:]
    C = C / (C.max(0, keepdims=True) + 1e-9)
    B = B / (B.max(0, keepdims=True) + 1e-9)

    cand = candidates(key_pcs, tonic)
    names = list(cand)
    S = np.zeros((C.shape[1], len(names)))
    for j, nm in enumerate(names):
        root, pcs, diat = cand[nm]
        t = np.array([1.0 if p in pcs else -0.35 for p in range(12)])
        S[:, j] = (t @ C) / len(pcs) + 0.55 * B[root] - (0 if diat else 0.08)
        if len(pcs) == 4:
            S[:, j] -= 0.04          # 7ths must earn it
    # Viterbi: chords tend to last; beat-to-beat flicker is penalised
    n, m = S.shape
    lp = np.log(np.full((m, m), (1 - stay) / (m - 1)))
    np.fill_diagonal(lp, np.log(stay))
    V = S[0] * 6
    back = np.zeros((n, m), int)
    for i in range(1, n):
        T = V[:, None] + lp
        back[i] = T.argmax(0)
        V = T.max(0) + S[i] * 6
    path = [int(V.argmax())]
    for i in range(n - 1, 0, -1):
        path.append(back[i][path[-1]])
    path = path[::-1]
    labels = [names[j] for j in path]
    conf = [float(S[i, path[i]] - np.sort(S[i])[-2]) for i in range(n)]
    return labels, conf, {nm: cand[nm][1] for nm in names}


def segments(labels, grid):
    out = []
    for i, lab in enumerate(labels):
        s = grid[i]
        e = grid[i + 1] if i + 1 < len(grid) else grid[i] + (grid[1] - grid[0])
        if out and out[-1][2] == lab:
            out[-1][1] = e
        else:
            out.append([s, e, lab])
    return out


def register_map(y, sr, grid, lo=23, hi=96):
    """Per-beat energy across MIDI notes lo..hi from the harmonic part.
    Tells the harmonizer where the beat is crowded and where there's room."""
    yh = librosa.effects.harmonic(y, margin=3.0)
    C = np.abs(librosa.cqt(yh, sr=sr, hop_length=512,
                           fmin=librosa.midi_to_hz(lo), n_bins=hi - lo,
                           bins_per_octave=12))
    fr = np.clip(librosa.time_to_frames(grid, sr=sr, hop_length=512), 0,
                 C.shape[1] - 1)
    R = librosa.util.sync(C, fr, aggregate=np.median)[:, 1:]
    R = librosa.amplitude_to_db(R, ref=np.max)
    return R, np.arange(lo, hi)


def top_line(R, notes, floor_db=-30, min_note=60):
    """The beat's most prominent pitch per beat above min_note (its lead /
    melody line), or None where nothing stands out."""
    sel = notes >= min_note
    out = []
    for i in range(R.shape[1]):
        col = R[sel, i]
        j = int(np.argmax(col))
        # must stand out from its neighbours, not just be the loudest of mush
        if col[j] > floor_db and col[j] - np.median(col) > 12:
            out.append(int(notes[sel][j]))
        else:
            out.append(None)
    return out


def bass_line(R, notes, max_note=55, floor_db=-25):
    sel = notes <= max_note
    out = []
    for i in range(R.shape[1]):
        col = R[sel, i]
        j = int(np.argmax(col))
        out.append(int(notes[sel][j]) if col[j] > floor_db else None)
    return out
