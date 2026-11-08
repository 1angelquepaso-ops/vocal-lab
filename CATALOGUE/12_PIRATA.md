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
- Chord versions: Simple (triad, voice-led), As played (cleaned notes, bass split), Rich (in-key extensions, rootless
  upper structure + LH 7th/5th).
- Melody modes: Complement (fills gaps), Counter (contrary motion, consonant 3/4/8/9), Amplify (diatonic 3rd/6th
  shadow + grace notes), Random (in key), Arp (up/down/updown/converge/random; 1/8, 1/8T, 1/16, 1/16T; 1–3 oct).
  Register relative to the chord top. No half-step rubs above chord tones on strong beats (tested 0 clashes).
- Piano: Salamander Grand Piano V3 (Yamaha C5, CC-BY 3.0), 4 velocity layers (v5/9/12/15), 7 s trimmed, 24 MB.
## Test results
- Known progression (Am9 Fmaj7 C/E E7sus4 Dm9 G13 Cmaj7 E7 ×2): 16/16 bars correct on piano and on a soft pad.
- Orange instrumental (full beat): 79 BPM, B minor, Bm / Gmaj7 / F#m loop.
## Limits / next
- No .m4a input; no built-in stem separation (works best on clean samples).
- Next: m4a/ffmpeg support, optional drum/vocal removal, chord-edit in the roll, swing/humanise on export.
