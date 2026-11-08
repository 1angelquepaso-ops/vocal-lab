# MIRRORWALL — vocal chain emulator

**What:** Load a raw vocal → pick one of our stock chains (fitted from our own old vocals) or build a NEW preset from any
reference acapella (EMULATOR) → preview A/B → drag the mixed vocal into FL. Autotune key/scale per song. v1 is offline.
**Code:** `vocal-lab/mirrorwall/` (Python engine + reference DSP), `vocal-lab/mirrorwall_plugin/` (JUCE; `Source/Chain.h`
is the C++ twin of the chain). Release `mirrorwall-latest`. Installed v1 (with C01U→MA-50 mic match).

## Signal chain (identical in Python + C++)
input trim to −20 dBFS active RMS → mic match EQ → preamp (tube 737-style / console 73-style / clean) → opto comp →
FET comp → tone EQ (HPF + 12 bands + LPF + 26 third-octave detail bands) → de-esser (4.5–11 kHz) → gate (RVox-style)
→ doubler (14/22 ms modulated) → slap delay (R channel, 300 Hz–6 kHz) → FDN reverb (8 lines) → loudness → limiter (−1 dB).
Knobs (1.0 = as fitted): Drive, Compression (±10 dB threshold), De-ess, Gate, FX/Width, Delay, Reverb, Output.
Autotune: key Auto or C–B; scales major, minor, harmonic minor, phrygian, chromatic; Retune ms, Amount, Humanize; Key from beat.

## EMULATOR (fit) — how a reference becomes a preset
1. Measure the reference: tone curve (1/3 octave, normalised 250 Hz–4 kHz), loudness spread, crest, sibilance index,
   stereo width, RT60, slap delay lag, tuning tightness (% of notes within 10 cents), noise floor (gated or not).
2. Tune the raw (if the reference is tuned), then fit in order: compression → tone EQ (3 passes + detail EQ 2 passes)
   → de-esser → gate → space (doubler/slap/reverb vs width).
3. Takes ~70 s on Linux for a 45 s window; preset JSON saved to `Documents\MIRRORWALL\Presets\`.
## Stock chains (fit error = tone curve vs the reference)
BAYWATCH 0.52 dB · BLUE FAYGO 0.66 · DAYSINN 0.37 · DRAKE DEMO 0.39 · SAIN JHN 0.56 · SNOW BUTTON 0.45 · LEAD BLEND 0.50 · HARMONY 1.40
(full numbers in 20_GEAR_AND_VOCAL_DATA.md). Angel's ears: "Lead blend" and "Harmony" both sound close.
## Engine commands
`tune --in --out --key --scale --speed --amount --humanize` (2.5 s for 114 s) · `key --file` · `fit --ref --raw --out --name --pre`.
## Limits
- v1 offline; tuning is Praat PSOLA offline.  - Harmony chain width short of target (+4.1 vs ~0.7).
- Reference-only fits can't separate effects perfectly; paired dry/wet fits would be near-exact.
## 2.0 plan (live)
Real-time pitch correction · process the mixer insert live · LIVE/REC vs HQ switch · input gain knob + meter (+Auto) and
fixed output gain per preset · no instrumental upload. Reuse DREAMMIC's real-time foundation.
