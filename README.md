# vocal-lab

Vocal tools for FL Studio, built offline: load a WAV, pick options, get WAVs and MIDI back.

## Harmonizer (working, v1)

Generates harmony parts from a dry lead vocal, in key, timed to the song.

- **Fixed styles:** third above, sixth below, fifth up, octaves, chord tones above/below.
- **Melody mode:** independent counter-melodies chosen note by note from the key:
  stepwise vs leaps, contrary motion against the lead, moves while the lead holds,
  holds while the lead moves, rests, passing "spice" notes mid-syllable. Seeded, so
  every run is a new variation.
- **Beat-aware:** reads the instrumental for a steady tempo grid, per-beat chords
  (diatonic + sus2/maj7 + borrowed colours) and bass, and where the beat leaves
  room in pitch. Harmony favours the beat's real chord tones.
- **Output:** one WAV per harmony voice (same start/length as the source) plus MIDI
  of every line and of the beat's detected chords, at the song's tempo.

Rendering is formant-preserving pitch shifting (Rubber Band R3), so it still sounds
processed. The plan is to re-sing the parts through a voice-clone model.

### Requires

Python 3.11+, the packages in `requirements.txt`, and the `rubberband` command-line tool.

### Layout

- `vocal_lab/core.py` — audio I/O, pitch tracking, note segmentation, pitch-shift bank, splicing
- `vocal_lab/harmony.py` — keys/scales, chord timelines, fixed-interval rules, clash guards
- `vocal_lab/melody.py` — melody mode (counter-melodies), humanized splicing, MIDI export
- `vocal_lab/instrumental.py` — beat grid, chords, bass, register map from an instrumental
- `vocal_lab/harmonize.py` — command-line harmonizer for the fixed styles
- `scripts/` — the Orange test runs and the verification script

## Planned

- More modes: call & response, choir stack, dark flavours (Phrygian / harmonic minor), colour tones
- Randomize: generate candidates, score on theory, lock & reroll, mutate
- A small window app and a Windows build
- Vocal aligner (doubles), voice-clone re-singing
