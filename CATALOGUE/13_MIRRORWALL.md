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
## 2.0 (built Nov 2026) — one engine for LIVE and HQ (`mirrorwall_plugin/Source/Live.h`)
- **LIVE**: FL audio runs through any bank preset in real time (stock + every Emulator preset; the bank grows as you build).
  **HQ**: tune (Praat, offline) + render the file with the *same* engine. LIVE vs HQ output is bit-identical (block-size test).
- Any sample rate (tested 44.1/48/96 k), per-sample state, 2 ms look-ahead limiter reported as latency.
- Input: knob or **Auto** rider (to -20 dB, max 3 dB/s; HQ seeds it from the file). Output: fixed make-up per preset,
  calibrated at load with 4 s of vocal-band noise → lands within ~0.5 dB of the preset target.
- EQ: MEX-EQ matched-magnitude design (no cramping). Preamp: anti-aliasing applied to the distortion part only.
- De-esser listens 4.2–10 kHz (male S 4.28–5.6 kHz, female S 4.8–6.6 kHz — Angel's ranges — plus bright t/s).
- Space: Room (fitted FDN) or **Plate** (diffused FDN, steel-plate HF damping from the Abbey Road Plates capture).
- Autotune stays HQ-only (real-time tune dropped unless Waves Tune tech becomes usable).
### Compressor models fitted to clean captures (comp_test staircase, Nov 2026; error = static curve RMS)
| type (preset key) | plugin captured | model | fit |
|---|---|---|---|
| `opto` | CLA-2A | 10 ms attack, two-stage program release, 8 dB knee | PR60 → thr -27 / 4:1, 0.17 dB; PR80 → thr -36 / 6:1, 0.77 dB |
| `fet` | CLA-76 | peak detector, 4 dB knee; labelled ratios bite harder (4:1 ≈ 6:1, 20:1 ≈ limit) | 0.14–0.18 dB |
| `vca` | SSL G channel | RMS, **45 dB knee**, real curve ~15 dB below the panel threshold, auto 30 ms / fast 3 ms | 0.43–0.47 dB |
| `lev` | RVox | RMS leveller, ~6:1, 24 dB knee, auto make-up, curve ~12 dB below the panel value, optional expander | 0.6 dB |
Captures + analysis files: `Documents\MIRRORWALL\captures\comp_test\` (CLA2A_takes, CLA76_clean, SSLG_dyn_clean, RVox, PLATES_clean).
Lesson: everything else on the Master (an earlier RVox) flattened the first CLA-76/CLA-2A takes — always one plugin + a fresh Edison.
### 2.1 additions (Nov 2026)
- **Emulator picks the compressor**: tries opto→FET (1.x chain), CLA-2A, CLA-76, SSL (incl. fast attack) and RVox
  styles; keeps the one matching the reference's loudness spread + transient crest (median peak−RMS of active 50 ms
  frames). Python twins `_vca` / `_lev` + soft knees now match Live.h exactly. Fit time ~32 s on the smoke test.
- **Tape** knob (0 = off) + 15/30 ips: anti-aliased saturation (up to 12 dB drive), head bump (60 Hz / 110 Hz),
  playback HF loss (spacing-loss shelf at 12 kHz, −4 / −2 dB at full), wow 0.6 Hz + flutter 7 Hz.
- **Air** knob: Aphex patent path — 2-pole 4 kHz HP → one-sided soft clip → 2 kHz HP (no DC) → mixed back (≤0.6).
- **Low latency** switch: limiter without look-ahead → 0 samples latency for tracking. **Meters**: IN / GR / OUT in LIVE.
### 2.2 additions (Nov 2026)
- **Build always banks**: falls back to the last raw vocal used (`Documents\MIRRORWALL\last_raw_vocal.txt`) so LIVE users
  don't need a raw file loaded; engine output is NaN-safe (JUCE's JSON reader rejects NaN, which silently lost presets).
- **A/B/C**: RAW → MIRRORWALL → REFERENCE, active-loudness matched, same playhead; third waveform lane for the reference.
- **Save tweaks**: knobs + preamp + Space + tape speed stored as `macros` in the preset (stock chains → "(tweaked)" copy);
  loading the preset restores them.
### Emulator 3 plan (agreed Nov 2026)
Joint perceptual fitting (all settings together, frame-by-frame hearing-weighted loss) · phoneme-aware matching (vowels vs
vowels, S vs S — copies the chain, not the singer) · acapella-aware analysis (AI stems lose reverb tails + carry artefacts) ·
artist profiles from 3–5 acapellas · ear-steered finish ("which is closer?" in A/B/C). Later: verse/hook pairs, autotune
style, double/harmony detection, learning from paired dry/wet sessions.
### Still open
- EQP1A (public WDF circuit model) and the SSL G EQ stage: researched, judged low value (the fitted EQ already matches curves).
