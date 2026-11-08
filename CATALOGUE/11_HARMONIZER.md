# vocal-lab Harmonizer

**What:** Load a dry vocal chunk (hook / bridge / phrase, not a whole song) + optional beat (analysis only, never played).
Generate harmony options one at a time, audition, mute/solo/reroll voices, drag each dry harmony stem into FL.
**Code:** `vocal-lab/vocal_lab/` (engine), `vocal-lab/plugin/` (JUCE). Release tag `latest`. Installed v2.

## Plugin (v2)
- Source row: Load vocal / Load beat; key + BPM readout.
- OPTIONS list (Keep / Delete), Style dropdown (8), Voices (1–4), "+ Generate 1" / "Generate 4".
- Lead lane + up to 4 voice lanes (waveform, playhead, click-to-seek, M / S / reroll / x, gold drag grip on the right).
- "+ add voice", Play / Loop / time / hear lead / Open folder, progress bar.
- Exports pre-saved to `Documents\vocal-lab\harmonies\<vocal>\` on a background thread.

## Engine (Python: `vocal_lab/engine_cli.py`)
Commands: `analyze`, `generate --count 1 --voices 1..4 --style …`, `reroll`, `add`, `remove`.
Styles: mix, classic, colour, floating, busy, low, dark_phrygian, dark_harmonic. Similar-option redraw at 0.75 similarity.
- Pitch: pYIN (librosa) → note segments with octave-error fix.
- Shifting: Rubber Band R3 CLI (`-3 -F`, formant preserving); sparse region shifting; humanised splicing.
- Theory: key snapping, diatonic steps, clash set {1,2,6,10,11}, chord timeline with roots, colour tones (9/11/13),
  dark flavours (Phrygian b2, harmonic #7 as resolving passing tones).
- Modes: melody (counter-melodies, contrary motion, splits/holds/rests/spice), choir stack (no pair clashes, no parallel
  5ths/8ves, penalty 12), call & response (phrase gaps, transforms), randomize (generate/score/mutate/lock-reroll).
- Instrumental analysis: beat-grid line fit, chroma + bass-chroma chord Viterbi (bass fmin A0), register map,
  UVR-MDX-NET-Inst_HQ_3 separation available.
- Speed (15 s hook): analyze 1.9 s, generate 1 option 3.1 s, 4 two-voice stacks 7 s, reroll 1.3 s.

## Known limits
- Pitch-shifted harmonies of a real voice still sound a bit processed ("robotic") — the real fix is the voice clone
  (see 40_PLANNED_BUILDS.md).
## Next
- Call & response and choir modes inside the plugin UI; skin polish; use voice-clone renders when ready.
