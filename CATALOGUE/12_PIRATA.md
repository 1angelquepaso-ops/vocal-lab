# PIRATA — chord + MIDI extractor / melody builder

**What:** Load a sample (piano progression, lush pads, harmonies). PIRATA finds key, BPM and every chord with its full
name, gives Simple / As played / Rich chord versions, writes a second melody layer, previews on a real grand piano,
and lets you drag CHORDS / BASS / MELODY / FULL MIDI into FL. Preview only (not an instrument).
**Code:** `vocal-lab/pirata/` (engine), `vocal-lab/pirata_plugin/`. Release `pirata-latest`. Installed v1.

## Engine
- Transcription: Spotify basic-pitch ICASSP-2022 model (ONNX, Apache-2.0) with our own decoder fix (keeps real
  half-step neighbours like B–C in Am9).
- Cleanup: sub-octave ghosts, weak octave blips, re-strike merging; quantize to 1/16 on the beat grid.
- Tempo: BPM from filename if present, else onset-phase fit over candidates; BPM box override in the UI.
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
## Test results
- Known progression (Am9 Fmaj7 C/E E7sus4 Dm9 G13 Cmaj7 E7 ×2): 16/16 bars correct on piano and on a soft pad.
- Orange instrumental (full beat): 79 BPM, B minor, Bm / Gmaj7 / F#m loop.
- v2 melody regression (`packaging/pirata_melody_test.py`, runs in CI before the build): 3 rendered samples with a
  known melody — held chords 90 BPM, 8th-note stab comping 100 BPM, melody weaving through chord tones 85 BPM.
  Simple: 34/34, 33/34, 30/30 melody notes, 1 / 3 / 0 wrong (wrong ones = transcription overtones), always 1 voice.
  Rich: 0 crossings above the tune, 0 half-step rubs, 0 internal clusters. Grid on the beat in all 4 test samples.
## Limits / next
- No .m4a input; no built-in stem separation (works best on clean samples).
- Chord NAMES on dense comping can still pick the wrong inversion/bass (Em7 read as G6, slash basses off on the
  100 BPM stab test) — affects the BASS tile and Rich's left hand there; next fix = stronger bass-note evidence.
- Lead line can't yet tell two equal-loudness melodies apart (it follows the higher, smoother one).
- Next: chord-name/bass fix above, m4a/ffmpeg support, optional drum/vocal removal, chord-edit in the roll,
  swing/humanise on export. Drum mode → separate plugin TITO PUENTE (see 40_PLANNED_BUILDS.md).
