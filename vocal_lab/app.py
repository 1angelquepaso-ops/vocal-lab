"""vocal-lab Harmonizer window.

Run:  python -m vocal_lab.app   (opens in your browser)

Drop in a dry vocal (and optionally the beat), pick a section, Analyze, then
Randomize to shop 8 harmony options. Lock a time range you like and reroll the
rest, mutate a favourite, and export WAV + MIDI to drag into FL Studio.
"""
import os
import tempfile
import zipfile

import numpy as np
import soundfile as sf
import gradio as gr

from . import core, harmony as H, melody as M, randomize as R, instrumental as I

N = 8
MAJ = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
MIN = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def detect_key(notes):
    pcs = np.zeros(12)
    for n in notes:
        pcs[int(round(n['pitch'])) % 12] += n['end'] - n['start']
    best = max(((np.corrcoef(pcs, np.roll(p, r))[0, 1], r, q)
                for r in range(12) for p, q in ((MAJ, 'major'), (MIN, 'minor'))))
    return core.NOTE_NAMES[best[1]], best[2]


class Session:
    def __init__(self):
        self.y = self.sr = self.beat = None
        self.notes = []; self.key = None; self.chords = None
        self.bank = {}; self.cands = []; self.start = 0.0; self.bpm = 120.0


S = Session()


def analyze(vocal, beat, start, length, key_name, scale):
    if not vocal:
        return 'Load a vocal first.', key_name, scale
    S.start = float(start)
    S.y, S.sr = core.load(vocal, S.start, S.start + float(length))
    t, m = core.track_pitch(S.y, S.sr)
    S.notes = core.segment_notes(t, m)
    if key_name == 'auto':
        key_name, scale = detect_key(S.notes)
    S.key = H.Key(key_name, scale if scale in H.SCALES else 'minor')
    S.chords, S.beat, msg_beat = None, None, 'no beat loaded: chords guessed from the vocal'
    if beat:
        yb, srb = I.load_mono(beat)
        bpm, b0, grid = I.fit_grid(yb, srb)
        labels, _, _ = I.chords(yb, srb, grid, S.key.pcs, S.key.tonic)
        S.chords = I.timeline_chords(I.segments(labels, grid), S.key, S.start, S.start + float(length))
        S.bpm = bpm
        bt, _ = sf.read(beat, dtype='float32', always_2d=True)
        a = int(S.start * S.sr)
        S.beat = bt[a:a + len(S.y)] if bt.shape[0] >= a + len(S.y) else None
        msg_beat = f'beat: {bpm:.1f} BPM, {len(S.chords)} chord changes in this section'
    else:
        S.chords = H.infer_chords(S.notes, S.key)
    S.bank = {}
    return (f'{len(S.notes)} notes · key {key_name} {scale} · {msg_beat}',
            key_name, scale)


def _render(ev, seed=0):
    segs = M.to_segments(ev)
    for iv in {s[2] for s in segs} - set(S.bank):
        S.bank[iv] = core.shift(S.y, S.sr, iv)
    h = M.splice_groups(S.bank, len(S.y), S.sr, segs, np.random.default_rng(seed)) * 10 ** (-5 / 20)
    L = S.y * 0.75 + h * np.cos(0.3 * np.pi / 4); Rr = S.y * 0.75 + h * np.sin(1.7 * np.pi / 4)
    if S.beat is not None:
        L += S.beat[:, 0] * 0.8; Rr += S.beat[:, -1] * 0.8
    mix = np.stack([L, Rr], 1); mix *= min(1, 0.98 / (np.abs(mix).max() + 1e-9))
    return h, mix


def _outputs():
    players, labels = [], []
    for i in range(N):
        if i < len(S.cands):
            c = S.cands[i]
            _, mix = _render(c['events'], i)
            players.append((S.sr, (mix * 32767).astype(np.int16)))
            labels.append(f"#{i+1} {c['style']} · score {c['score']:.2f} · "
                          f"chord fit {c['parts'].get('chord_fit', 0):.0%}")
        else:
            players.append(None); labels.append('')
    return players + [gr.update(value=l) for l in labels]


def do_randomize(styles):
    if S.y is None:
        raise gr.Error('Analyze a vocal first.')
    S.cands = R.generate(S.notes, S.key, S.chords, n=N, styles=styles or None,
                         seed=int(np.random.SeedSequence().entropy % 1e9))
    return _outputs()


def do_mutate(idx):
    i = int(idx) - 1
    base = S.cands[i]
    S.cands = [base] + [R.mutate(base, S.notes, S.key, S.chords, seed=k + int(np.random.SeedSequence().entropy % 1e6))
                        for k in range(N - 1)]
    S.cands[1:] = sorted(S.cands[1:], key=lambda c: -c['score'])
    return _outputs()


def do_lock(idx, a, b):
    i = int(idx) - 1
    base = S.cands[i]
    rng = np.random.default_rng()
    S.cands = [base] + [R.lock_reroll(base, [(float(a), float(b))], S.notes, S.key, S.chords,
                                      seed=int(rng.integers(1e9))) for _ in range(N - 1)]
    return _outputs()


def do_export(idx):
    i = int(idx) - 1
    c = S.cands[i]
    h, mix = _render(c['events'], i)
    d = tempfile.mkdtemp()
    sf.write(os.path.join(d, f'harmony_{i+1}.wav'), h, S.sr, subtype='PCM_24')
    sf.write(os.path.join(d, f'preview_{i+1}.wav'), mix, S.sr, subtype='PCM_24')
    M.write_midi(os.path.join(d, f'harmony_{i+1}.mid'), {f'harmony_{i+1}': c['events']},
                 offset=S.start, bpm=round(S.bpm, 2))
    z = os.path.join(d, f'harmony_{i+1}_start_{S.start:.0f}s.zip')
    with zipfile.ZipFile(z, 'w') as f:
        for n in os.listdir(d):
            if not n.endswith('.zip'):
                f.write(os.path.join(d, n), n)
    return z


def build():
    with gr.Blocks(title='vocal-lab Harmonizer') as ui:
        gr.Markdown('## vocal-lab Harmonizer\nDry vocal in → shop harmony parts → WAV + MIDI out.')
        with gr.Row():
            vocal = gr.File(label='Dry vocal (WAV)', type='filepath')
            beat = gr.File(label='Beat / instrumental (optional)', type='filepath')
        with gr.Row():
            start = gr.Number(value=0, label='Section start (s)')
            length = gr.Number(value=30, label='Section length (s)')
            key = gr.Dropdown(['auto'] + core.NOTE_NAMES, value='auto', label='Key')
            scale = gr.Dropdown(list(H.SCALES), value='minor', label='Scale')
        go = gr.Button('Analyze', variant='secondary')
        status = gr.Markdown()
        go.click(analyze, [vocal, beat, start, length, key, scale], [status, key, scale])
        styles = gr.CheckboxGroup(list(R.STYLES), value=list(R.STYLES), label='Styles')
        rnd = gr.Button('🎲 Randomize', variant='primary')
        players, labels = [], []
        for r in range(N // 2):
            with gr.Row():
                for c in range(2):
                    with gr.Column():
                        labels.append(gr.Markdown())
                        players.append(gr.Audio(label=f'#{2*r+c+1}', interactive=False))
        outs = players + labels
        rnd.click(do_randomize, [styles], outs)
        with gr.Row():
            pick = gr.Number(value=1, precision=0, label='Option #')
            a = gr.Number(value=0, label='Lock from (s)')
            b = gr.Number(value=8, label='Lock to (s)')
        with gr.Row():
            gr.Button('Mutate').click(do_mutate, [pick], outs)
            gr.Button('Lock range & reroll rest').click(do_lock, [pick, a, b], outs)
            exp = gr.Button('Export WAV + MIDI')
        f = gr.File(label='Download')
        exp.click(do_export, [pick], f)
    return ui


if __name__ == '__main__':
    build().launch(inbrowser=True)
