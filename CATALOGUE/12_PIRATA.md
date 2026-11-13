# PIRATA — chord + MIDI extractor / melody builder

**What:** Load a sample (piano progression, lush pads, harmonies). PIRATA finds key, BPM and every chord with its full
name, gives Simple / As played / Rich chord versions, writes a second melody layer, previews on a real grand piano,
and lets you drag CHORDS / BASS / MELODY / FULL MIDI into FL. Preview only (not an instrument).
**Code:** `vocal-lab/pirata/` (engine), `vocal-lab/pirata_plugin/`. Release `pirata-latest`. Installed v1.
v2.1 (Nov 13 2026) = timing pass with the lessons from TITO PUENTE (see "Timing (v2.1)" below).

## Window additions (v2.1)
- Sample row: BPM box + **÷2 / x2** + **3:2 menu** (×2/3, ×3/2, ×3/4, ×4/3) → re-analyze at that tempo.
- Chords row: **Timing** menu = Grid (1/16 + triplets, default) · 1/16 · 1/32 · 1/16 triplet · 1/8 triplet ·
  **Keep timing (as played)**. Applies to every tile (chords, bass, melody, full) and to the piano preview.
- Info (top right): "triplet feel" or "swing 62%" next to key / BPM / bars; hover = swing %, looseness (ms),
  triplet share, steady / drifting.
- **FL tempo warning** (orange, sample row): FL's tempo is read every block; if it isn't the sample's BPM the
  window says so (dragged MIDI follows FL's tempo, so the MIDI won't line up with the sample).

## Engine
- Transcription: Spotify basic-pitch ICASSP-2022 model (ONNX, Apache-2.0) with our own decoder fix (keeps real
  half-step neighbours like B–C in Am9).
- Cleanup: sub-octave ghosts, weak octave blips, re-strike merging; quantize to 1/16 on the beat grid.
- Tempo: BPM from filename if present, else onset-phase fit over candidates; BPM box override in the UI.
  v2.1 adds the TITO checks below (3:2 / 4:3, exact BPM, harmonic rhythm for pads).
- Chords: 30 qualities (triads, 6, 7, maj7, m7, m7b5, dim7, sus, add9, 6/9, 9, maj9, m9, 7b9, 7#9, 11, m11, maj7#11, 13, m13, maj13),
  slash bass, per-beat evidence = struck notes (55%) + network pitch salience (45%), Viterbi with bar-aware change
  costs, one-beat flicker folding, ring-out trimming, harmony-aware key (tonic first/last, V7→i).
- Chord versions (v2, Nov 2026):
  - **Simple = the sample's melody as ONE note line**, in its real rhythm (root bass still on the BASS tile; preview
    plays the line alone). Lead tracker = Viterbi over onset slots: height, loudness, smooth motion, soft penalty for
    overtone ghosts (octave/12th above a louder note struck together), "struck under a still-ringing melody note =
    accompaniment", legato hold into the next note across gaps ≤ 1.5 beats (piano decay ends notes early).
  - As played (cleaned notes, bass split) — unchanged.
  - **Rich = melody on top + lush in-key chords under it**: extension chosen so the melody note belongs (melody on
    the 9th → add9/m9 etc.), voices 3rd/7th/9th/13th under the tune (min 3), no doubling, no half-step/b9 under the
    melody, no half-step clusters inside the voicing, held voices released when the tune moves onto a rub or crosses
    them; re-strikes where the original re-strikes + chord changes + under long on-beat melody notes; LH root +
    10th (else 7th/5th).
  - v1 bug (why Simple and Rich sounded the same): both rebuilt block chords from the chord LABELS on the same
    re-strike grid, so the pattern was identical and the real melody never appeared (top voice matched the melody on
    6/34 and 2/34 notes).
- Melody modes: Complement (fills gaps), Counter (contrary motion, consonant 3/4/8/9), Amplify (diatonic 3rd/6th
  shadow + grace notes), Random (in key), Arp (up/down/updown/converge/random; 1/8, 1/8T, 1/16, 1/16T; 1–3 oct).
  Register relative to the chord top. No half-step rubs above chord tones on strong beats (tested 0 clashes).
- Piano: Salamander Grand Piano V3 (Yamaha C5, CC-BY 3.0), 4 velocity layers (v5/9/12/15), 7 s trimmed, 24 MB.
- Beat grid fix (v2): the on-beat vs off-beat choice used abs() and could lock the grid half a beat late on
  melody-heavy samples (chords then read across the wrong boundaries, e.g. "Fmaj7/A" for an Am bar). Now signed, bass
  hits weighted ×4, plus a "samples start on a beat" prior.
## Timing (v2.1, lessons from TITO PUENTE)
- **Onset refinement** (`refine_onsets`): basic-pitch note starts are frame-based (11.6 ms frames) and on piano
  scatter ±5 ms median, p90 ±13 ms around the real attack. Each note is moved to where its attack starts:
  energy above 2 kHz (hammer / pluck / stab transient, free of bass and pad tails), 2 ms RMS, followed back from
  the steepest rise to 25 % of the way up from the floor before it. Slow attacks (pads, swells: peak < 1.5× floor)
  keep their frame time. → **±0.7 ms median**. (First try = TITO's full-band steepest-rise on a 1.5 ms |x|
  envelope: no better than the frames on piano - low notes ripple the envelope. The 2 kHz band is what works.)
- **3:2 / 4:3 metric check** (`metric_check`): a triplet arp at 75 read as 112.5 (8th-triplets at 75 = straight
  8ths at 112.5, the fine grid can't tell). At the true tempo the bass + chord changes land on whole 4-beat bars;
  at the 3:2-off tempo every 1.5 bars. Bar + half-bar coherence of the low notes picks the meter (switch only if
  clearly better and the fine grid stays tight; the fine grid counts straight OR triplet subdivisions).
- **Harmonic rhythm for sparse samples** (`harmonic_rhythm_bpm`): a slow pad with one chord per bar has no rhythm
  to fit - old and new engines both read a 92 pad as 149.5. When attacks are sparse (< 1.6 groups / s), the tempo
  (70-150) whose 4/4 bars best line up with the chord attacks wins (octave tie → nearer 95).
- **Exact BPM** (`refine_period`): residuals of the notes against the 16th (or 8th-triplet) grid fitted as a line
  over time, grown from the start of the sample outward (window 30 % → 60 % → 100 %) - a slope means the period is
  off. Then rounding to a whole BPM within 0.35.
- **Grid phase re-anchored on beat / 8th-line notes** (TITO: beat-level phase dragged late by syncopation).
  Swung 16ths pulled v2's phase 46 ms late; now the phase is the median of notes within 0.1 beat of a beat / 8th line.
- **Auto triplet grid** (`quantize`): per beat, straight 1/16 unless the notes clearly sit on 8th- or 16th-note
  triplets (a note > 0.06 beat off the 1/16 grid and triplet error < 45 % of straight error). Every note keeps its
  real position `ts` / `te` (beats) next to the grid one.
- **Timing on export** (`cli.retime`, `--timing grid|16|32|16T|8T|keep`): Keep = each sample note at its own real
  time; generated notes (Rich voicings, bass, melody layer) go through a **warp map** (per grid slot the median offset
  of the sample's notes, interpolated) so they take on the sample's feel. Grid choices snap the real times.
  Notes pushed ahead of bar 1 start the bar (piano renderer crashed on negative starts - fixed in `piano.render` too).
- `timing` in analysis.json: steady, spread_ms, loose_ms, **swing_16** (50 straight, 66.7 triplet; needs ≥ 15 % of notes
  on off-16ths), triplet_share, metric_fix. Push / lay-back is NOT reported: a lone sample has no click to be late
  against (the grid is anchored on its own beats).
- **Tried, not shipped - TITO's ghost-note rule** (keep a 2nd note on a hit only if it explains real energy): on piano an
  overtone ghost (octave / 12th above a chord tone) and a real octave doubling share the same partials. Spectral excess
  vs a learned per-song octave ratio dropped 15 real notes for 5 ghosts on the stab test; amp ratio, onset activation,
  timing and duration don't separate them either (fully overlapping). Ghosts stay handled by the soft penalty in the
  lead tracker. Next idea if needed: per-song note templates from notes that sound alone (TITO-style NNLS).

## Test results
- Known progression (Am9 Fmaj7 C/E E7sus4 Dm9 G13 Cmaj7 E7 ×2): 16/16 bars correct on piano and on a soft pad.
- Orange instrumental (full beat): 79 BPM, B minor, Bm / Gmaj7 / F#m loop.
- v2 melody regression (`packaging/pirata_melody_test.py`, runs in CI before the build): 3 rendered samples with a
  known melody — held chords 90 BPM, 8th-note stab comping 100 BPM, melody weaving through chord tones 85 BPM.
  Simple: 34/34, 33/34, 30/30 melody notes, 1 / 3 / 0 wrong (wrong ones = transcription overtones), always 1 voice.
  Rich: 0 crossings above the tune, 0 half-step rubs, 0 internal clusters. Grid on the beat in all 4 test samples.
- v2.1 timing regression (`packaging/pirata_timing_test.py`, CI before the build; rendered on the grand, NO BPM in
  the file names): steady 140 / 128, humanised 92 (18 ms late ±9 ms), 8th-triplet arp 75, 62 % swung 16ths 84, slow pad 92.
  | | v2 | v2.1 |
  |---|---|---|
  | BPM right (6 cases) | 4 / 6 (triplet arp 112.5, pad 149.5) | 6 / 6 exact |
  | Keep timing vs real attacks | - (always 1/16) | median 0.4-0.9 ms, p90 1.5-3.4 ms, 95-100 % of notes |
  | triplet arp on 1/3-beat slots | 0 % | 57 % (= the 2 off-beat notes of every 3) |
  | swing measured | - | 62 % (played 62 %) |
  Melody regression unchanged (34/34 · 33/34 · 30/30, wrong notes 1 / 2 / 0 - was 1 / 3 / 0).
- Real-sample bench (`python -m pirata.realbench FOLDER`, TITO-style, no answer key): name files `<name>__<bpm>.wav`
  (or "... 90bpm ..."); the engine is NOT given the BPM. Reports tempo vs name (3:2 counted wrong), recall of audible
  attacks (librosa, 24 dB window) with a note within 30 ms + median ms, steady, looseness, swing, triplet share,
  chord changes per bar. On the 9 rendered test files: tempo 9/9, recall 0.87, 2.8 ms. **Still to run on real loops
  from Angel's library** (packo / Suno stems).

## Limits / next
- No .m4a input; no built-in stem separation (works best on clean samples).
- Chord NAMES on dense comping can still pick the wrong inversion/bass (Em7 read as G6, slash basses off on the
  100 BPM stab test) — affects the BASS tile and Rich's left hand there; next fix = stronger bass-note evidence.
- Lead line can't yet tell two equal-loudness melodies apart (it follows the higher, smoother one).
- Next: chord-name/bass fix above, m4a/ffmpeg support, optional drum/vocal removal, chord-edit in the roll.
  (Swing/humanise on export → done in v2.1 as Keep timing / triplet grids.)
- Drifting tempo (live-played loops): Keep timing follows the notes, but there is no tempo map in the MIDI yet
  (TITO has one: `--tempo-map`) - add if real loops show drift in the bench. Drum mode → separate plugin TITO PUENTE (see 40_PLANNED_BUILDS.md).
