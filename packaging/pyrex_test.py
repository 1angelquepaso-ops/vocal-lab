"""PYREX MAGIK CI test: synthetic Suno-style stems with a KNOWN answer.
Song: 117 BPM half-time trap (kick on 1, snare on 3, hats 8ths), Am chords (Am F C G), a sine "vocal" in A minor,
an empty stem, 48 kHz, 2.3 s of intro before the drums (the first bar line is BEFORE 0 s), 1.2 ms of human-feel
jitter. Checks: analysis reads 117 / Am / half-time; render to 120 + transpose 0 is green with grid offset
< 1 ms; render with transpose +3 -> Cm and still aligned."""
import json
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

SR = 48000
BPM = 117.0
P = 60 / BPM
DUR = 48.0
rng = np.random.default_rng(7)


def place(buf, t, x):
    i = int(round(t * SR))
    if 0 <= i < len(buf):
        n = min(len(x), len(buf) - i)
        buf[i:i + n] += x[:n]


def kick():
    t = np.arange(int(0.35 * SR)) / SR
    f = 50 + 120 * np.exp(-t * 35)
    return 0.9 * np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t * 9)


def snare():
    t = np.arange(int(0.25 * SR)) / SR
    return (0.5 * rng.standard_normal(len(t)) * np.exp(-t * 22) + 0.3 * np.sin(2 * np.pi * 190 * t) * np.exp(-t * 30))


def hat():
    t = np.arange(int(0.06 * SR)) / SR
    x = rng.standard_normal(len(t))
    x = np.diff(np.concatenate([[0], x]))
    return 0.18 * x * np.exp(-t * 80)


def tone(freqs, dur, amp=0.12):
    t = np.arange(int(dur * SR)) / SR
    env = np.minimum(1, t / 0.01) * np.exp(-t * 0.8)
    return amp * env * sum(np.sin(2 * np.pi * f * t) + 0.3 * np.sin(4 * np.pi * f * t) for f in freqs)


def make(folder):
    os.makedirs(folder, exist_ok=True)
    n = int(DUR * SR)
    first_bar = 2.3 + 0.0               # drums start at 2.3 s on a downbeat; earlier bar lines are before 0 s
    drums, keys, voc = np.zeros(n), np.zeros(n), np.zeros(n)
    A, F, C, G = 220.0, 174.61, 261.63, 196.0
    chords = [(A, 261.63, 329.63), (F, 220.0, 261.63), (C, 329.63, 392.0), (G, 246.94, 293.66)]
    bar = 4 * P
    b = first_bar - 2 * bar             # keys + vocal start before the drums (intro)
    k = 0
    while b < DUR:
        place(keys, b, tone(chords[k % 4], bar))
        if b >= first_bar - 1e-6:
            j = lambda: rng.normal(0, 0.0012)
            place(drums, b + j(), kick())
            place(drums, b + 2.5 * P + j(), 0.7 * kick())
            place(drums, b + 2 * P + j(), snare())
            for e in range(8):
                place(drums, b + e * P / 2 + j(), hat())
        mel = [440.0, 523.25, 493.88, 440.0][k % 4]
        place(voc, b + P, tone([mel], 2 * P, 0.15))
        b += bar
        k += 1
    sf.write(os.path.join(folder, '0 Lead Vocals.wav'), np.stack([voc, voc], 1), SR, subtype='PCM_16')
    sf.write(os.path.join(folder, '2 Drums.wav'), np.stack([drums, drums], 1) * 0.8, SR, subtype='PCM_16')
    sf.write(os.path.join(folder, '5 Keyboard.wav'), np.stack([keys, keys], 1), SR, subtype='PCM_16')
    sf.write(os.path.join(folder, '8 Other.wav'), np.zeros((n, 2)), SR, subtype='PCM_16')
    # a SPARSE stem: three loud hits in 48 s -> average level far below -50 dB, but real audio (must NOT be dropped)
    sp = np.zeros(n)
    for t in (10.0, 20.0, 30.0):
        place(sp, t, 0.02 * kick())
    sf.write(os.path.join(folder, '6 Percussion.wav'), np.stack([sp, sp], 1), SR, subtype='PCM_16')
    # a vocal-only copy of the song in its own folder (no drums at all)
    vo = os.path.join(os.path.dirname(folder), 'Vocal Only Stems (117BPM)')
    os.makedirs(vo, exist_ok=True)
    sf.write(os.path.join(vo, '0 Lead Vocals.wav'), np.stack([voc, voc], 1), SR, subtype='PCM_16')
    return first_bar


def main(work):
    from pyrex.analyze import analyze_song, label_bpm, song_title
    from pyrex.check import check_song
    from pyrex.render import compensate, render_song
    folder = os.path.join(work, 'Test Song Stems (117BPM)')
    first_bar = make(folder)
    paths = sorted(os.path.join(folder, f) for f in os.listdir(folder))
    an = analyze_song(paths, label=label_bpm(folder))
    an['title'] = song_title(folder)
    c = an['clock']
    print('analysis', c['bpm'], c['level'], c['downbeat_style'], an['key']['key'],
          'bar1 %.4f (true %.4f)' % (c['music_bar1_s'], first_bar), 'start', c['file_start_s'])
    fails = []
    if abs(c['bpm'] - BPM) > 0.01: fails.append(f"bpm {c['bpm']}")
    if an['key']['key'] != 'Am': fails.append(f"key {an['key']['key']}")
    if abs(c['music_bar1_s'] - first_bar) > 0.004: fails.append(f"bar1 {c['music_bar1_s']}")
    if c['file_start_s'] > 0: fails.append('file start after 0 s')
    if not any(s['empty'] for s in an['stems']): fails.append('empty stem not detected')
    sparse = next(x for x in an['stems'] if x['name'].lower() == 'percussion')
    print('sparse stem', sparse['rms_db'], 'dB rms', sparse['peak_db'], 'dB peak, empty =', sparse['empty'])
    if sparse["empty"] or sparse["rms_db"] > -50: fails.append(f"sparse stem misjudged {sparse['rms_db']} {sparse['empty']}")
    for tr, want in ((0, 'Am'), (3, 'Cm')):
        res = render_song(an, 120.0, os.path.join(work, f'out_{tr}'), transpose=tr)
        res = compensate(an, res)
        ck = check_song(an, res)
        g = ck['checks']['grid']
        print(f'render +{tr}', ck['level'], ck['problems'], 'offset', g['offset_ms'], 'median', g['median_ms'],
              'drift', g['drift_ms'], 'align', ck['checks']['align']['spread_ms'], 'latency', res['latency_ms'],
              {n: v['key_after'] for n, v in ck['checks']['pitch']['stems'].items()})
        if ck['level'] != 'green': fails.append(f'render +{tr}: {ck["problems"]}')
        if abs(g['offset_ms']) > 1.0: fails.append(f'render +{tr}: offset {g["offset_ms"]}')
        names = [os.path.basename(f) for f in res['files'].values()]
        if not all(f' - PYREX MAGIK - ' in x and ('120bpm - ' + (want if not tr else 'Am→' + want)) in x for x in names):
            fails.append(f'names {names}')
    # every stem that was loaded comes out (silent and sparse ones too)
    if len(res['files']) != len(an['stems']):
        fails.append(f"rendered {len(res['files'])} of {len(an['stems'])} stems")
    # vocal only: no drums -> estimated clock, never refused, never green
    vp = [os.path.join(work, 'Vocal Only Stems (117BPM)', '0 Lead Vocals.wav')]
    av = analyze_song(vp, label=117.0)
    av['title'] = 'Vocal Only'
    if not av.get('ok'):
        fails.append(f"vocal-only refused: {av.get('error')}")
    else:
        cv = av['clock']
        beats = np.array(cv['beats'])
        ph = np.abs(((beats - first_bar) / P + 0.5) % 1 - 0.5) * P * 1000
        print('vocal-only', cv['bpm'], cv['level'], 'approx', cv['approx'], 'phase error ms median %.1f' % np.median(ph))
        if not cv['approx'] or cv['level'] == 'green': fails.append('vocal-only should be an amber estimate')
        if abs(cv['bpm'] - BPM) > 0.6: fails.append(f"vocal-only bpm {cv['bpm']}")
        if np.median(ph) > 40: fails.append(f"vocal-only phase {np.median(ph):.1f} ms")
        rv = compensate(av, render_song(av, 120.0, os.path.join(work, 'out_voc')))
        ckv = check_song(av, rv)
        print('vocal-only render', ckv['level'], ckv['problems'])
        if ckv['level'] == 'red': fails.append(f"vocal-only render {ckv['problems']}")
    if fails:
        print('FAILED:', fails)
        return 1
    print('PYREX test OK')
    return 0


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[1] == '--make-only':      # smoke test material for the frozen exe
        make(os.path.join(sys.argv[2], 'Test Song Stems (117BPM)'))
        sys.exit(0)
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'pyrex_test'))
