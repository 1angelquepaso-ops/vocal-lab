"""Harmony rules engine.

Given detected lead notes, a key, and a mode, decide a semitone interval for
each note of each harmony voice. Intervals are whole semitones relative to the
note the singer is aiming at, so each harmony keeps the lead's own bends and
vibrato.
"""
import numpy as np
import pretty_midi

KEYS = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5,
        'F#': 6, 'Gb': 6, 'G': 7, 'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10,
        'Bb': 10, 'B': 11}
SCALES = {
    'minor':          [0, 2, 3, 5, 7, 8, 10],
    'harmonic_minor': [0, 2, 3, 5, 7, 8, 11],
    'major':          [0, 2, 4, 5, 7, 9, 11],
    'dorian':         [0, 2, 3, 5, 7, 9, 10],
    'phrygian':       [0, 1, 3, 5, 7, 8, 10],
}
CHORD_QUAL = {'': [0, 4, 7], 'm': [0, 3, 7], 'dim': [0, 3, 6],
              '7': [0, 4, 7, 10], 'm7': [0, 3, 7, 10], 'maj7': [0, 4, 7, 11]}

# interval classes that sound like clashes against the lead
CLASH = {1, 2, 6, 10, 11}


class Key:
    def __init__(self, tonic='B', scale='minor'):
        self.tonic = KEYS[tonic]
        self.pcs = sorted((self.tonic + s) % 12 for s in SCALES[scale])
        # every in-scale MIDI note across the audible range
        self.notes = np.array([n for n in range(12, 120) if n % 12 in self.pcs])

    def snap(self, pitch):
        """Nearest in-scale MIDI note to a (possibly off-centre) sung pitch."""
        return int(self.notes[np.argmin(np.abs(self.notes - pitch))])

    def step(self, note, steps):
        """Move `steps` scale degrees from an in-scale note."""
        i = int(np.where(self.notes == note)[0][0])
        return int(self.notes[i + steps])


# --------------------------------------------------------------- chords
def chords_from_midi(path, offset=0.0, track=None):
    """Read a chord timeline from MIDI: list of (start, end, pitch-class set).

    offset shifts the MIDI later (seconds) to line it up with the vocal.
    """
    pm = pretty_midi.PrettyMIDI(path)
    insts = pm.instruments
    if track is not None:
        insts = [i for i in insts if i.name == track] or insts
    else:
        # the instrument with the most simultaneous notes is the chord track
        insts = [max(insts, key=lambda i: _polyphony(i))]
    starts = {}
    for n in insts[0].notes:
        starts.setdefault(round(n.start, 3), []).append(n)
    tl = []
    for s in sorted(starts):
        ns = starts[s]
        tl.append([s + offset, max(n.end for n in ns) + offset,
                   frozenset(n.pitch % 12 for n in ns)])
    for i in range(len(tl) - 1):            # fill to the next change
        tl[i][1] = max(tl[i][1], tl[i + 1][0])
    return [tuple(c) for c in tl]


def _polyphony(inst):
    s = {}
    for n in inst.notes:
        s[round(n.start, 3)] = s.get(round(n.start, 3), 0) + 1
    return np.mean(list(s.values())) if s else 0


def diatonic_triads(key, include_dim=False):
    """Triads built on each scale degree, as pitch-class sets.

    Diminished triads are left out by default; they rarely carry a section in
    this style, and including them lets a lead that lingers on the 2nd degree
    drag the guess onto ii-dim.
    """
    out = {}
    for i, root in enumerate(key.pcs):
        pcs = [key.pcs[(i + k) % 7] for k in (0, 2, 4)]
        if not include_dim and (pcs[2] - pcs[0]) % 12 == 6:
            continue
        out[root] = frozenset(pcs)
    return out


def infer_chords(notes, key, window=1.5, change_penalty=0.35, allowed=None):
    """Guess a chord per window from the lead line alone (Viterbi smoothed).

    Long notes count more than short ones. `allowed` limits candidates to a
    set of root pitch classes (e.g. the progression's chords).
    """
    triads = diatonic_triads(key)
    if allowed:
        triads = {r: pc for r, pc in triads.items() if r in allowed}
    roots = list(triads)
    if not notes:
        return []
    end = max(n['end'] for n in notes)
    nw = int(np.ceil(end / window))
    score = np.zeros((nw, len(roots)))
    for n in notes:
        pc = key.snap(n['pitch']) % 12
        w = int(n['start'] // window)
        dur = n['end'] - n['start']
        for j, r in enumerate(roots):
            if pc in triads[r]:
                score[w, j] += dur * (1.6 if pc == r else 1.0)
            else:
                score[w, j] -= dur * 0.5
    # tonic gets a small prior so empty windows don't wander
    tonic = key.tonic
    if tonic in roots:
        score[:, roots.index(tonic)] += 0.05
    # Viterbi
    V = score[0].copy()
    back = np.zeros((nw, len(roots)), dtype=int)
    for w in range(1, nw):
        trans = V[:, None] - change_penalty * (1 - np.eye(len(roots)))
        back[w] = np.argmax(trans, axis=0)
        V = trans[back[w], range(len(roots))] + score[w]
    path = [int(np.argmax(V))]
    for w in range(nw - 1, 0, -1):
        path.append(back[w][path[-1]])
    path = path[::-1]
    tl = []
    for w, j in enumerate(path):
        s, e = w * window, (w + 1) * window
        if tl and tl[-1][2] == triads[roots[j]]:
            tl[-1] = (tl[-1][0], e, tl[-1][2])
        else:
            tl.append((s, e, triads[roots[j]]))
    return tl


def chord_at(timeline, t):
    for s, e, pcs in timeline:
        if s <= t < e:
            return pcs
    return None


# ---------------------------------------------------------------- voices
def voice_plan(notes, key, rule, chords=None, rng=None, prev_bias=0.6):
    """Return [(start, end, interval, target_note)] for one harmony voice.

    rule:
      'third_up' 'third_down' 'fifth_up' 'sixth_down' 'fourth_down'
      'octave_up' 'octave_down' 'unison'
      'chord_up' / 'chord_down'  nearest chord tone above / below the lead
      'random_up' / 'random_down'  in-key, clash-guarded, smooth voice leading
    """
    rng = rng or np.random.default_rng(0)
    steps = {'third_up': 2, 'third_down': -2, 'fifth_up': 4,
             'sixth_down': -5, 'fourth_down': -3, 'unison': 0}
    plan = []
    prev_tgt, prev_iv = None, None
    for n in notes:
        src = key.snap(n['pitch'])
        chord = chord_at(chords or [], n['start'])
        if rule in steps:
            tgt = _safe_step(src, key, steps[rule])
        elif rule == 'octave_up':
            tgt = src + 12
        elif rule == 'octave_down':
            tgt = src - 12
        elif rule in ('chord_up', 'chord_down'):
            tgt = _chord_tone(src, key, chord, up=rule == 'chord_up')
        elif rule in ('random_up', 'random_down'):
            tgt = _random_tone(src, key, chord, rule == 'random_up',
                               prev_tgt, prev_iv, rng, prev_bias)
        else:
            raise ValueError(rule)
        prev_tgt, prev_iv = tgt, tgt - src
        # interval relative to the note the singer is aiming at
        plan.append((n['start'], n['end'], int(tgt - src), int(tgt)))
    return plan


def _safe_step(src, key, steps):
    """Diatonic step, but never land on a clash with the lead.

    In a minor key the in-scale fifth above the 2nd degree is a tritone; when
    that happens, slide to the nearest consonant degree on the same side."""
    tgt = key.step(src, steps)
    if steps == 0 or (abs(tgt - src) % 12) not in CLASH:
        return tgt
    sign = 1 if steps > 0 else -1
    for alt in (steps - sign, steps + sign, steps - 2 * sign):
        if alt == 0:
            continue
        m = key.step(src, alt)
        if (abs(m - src) % 12) not in CLASH:
            return m
    return tgt


def _chord_tone(src, key, chord, up):
    """Nearest chord tone strictly above/below the lead, never the same pitch
    class and never a clash. Falls back to a diatonic third when the chord
    is unknown or has nothing consonant nearby."""
    if chord:
        span = range(src + 1, src + 10) if up else range(src - 1, src - 10, -1)
        for m in span:
            if m % 12 in chord and m % 12 != src % 12 and \
                    (abs(m - src) % 12) not in CLASH:
                return m
    return key.step(src, 2 if up else -2)


def _random_tone(src, key, chord, up, prev_tgt, prev_iv, rng, prev_bias):
    """Pick a consonant in-key note on the chosen side of the lead.

    Candidates are 3rds, 4ths, 5ths and 6ths in the key. Clashes are
    excluded, chord tones are favoured, and small leaps from the previous
    harmony note are favoured so it sings like a part. With probability
    `prev_bias` it keeps the previous interval when that is still legal,
    which stops it flickering between voicings note to note.
    """
    degs = [2, 3, 4, 5] if up else [-2, -3, -4, -5]
    cands = []
    for d in degs:
        m = key.step(src, d)
        if m == src or (abs(m - src) % 12) in CLASH:
            continue
        w = 1.0
        if chord and m % 12 in chord:
            w *= 3.0
        if prev_tgt is not None:
            w /= 1 + abs(m - prev_tgt) / 2.5
        cands.append((m, w))
    if not cands:
        return key.step(src, 2 if up else -2)
    if prev_iv is not None and rng.random() < prev_bias:
        for m, _ in cands:
            if m - src == prev_iv:
                return m
    ms, ws = zip(*cands)
    ws = np.array(ws) / np.sum(ws)
    return int(rng.choice(ms, p=ws))
