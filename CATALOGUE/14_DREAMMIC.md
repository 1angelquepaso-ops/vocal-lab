# DREAMMIC — mic + preamp emulator (real time)

**What:** Live insert. Undo your real mic, become one of 19 vocal mics, through one of 13 preamps, with an optional
tracking compressor + de-esser (the MIRRORWALL 2.0 models), Auto input and Level match. 10 factory presets + your own.
**Code:** `vocal-lab/dreammic_plugin/` (`Source/Models.h` = gear tables, `Source/Engine.h` = DSP). Release `dreammic-latest`.
**Shared code (v2):** DREAMMIC includes `mirrorwall_plugin/Source/Live.h` + `Design.h` (+ `GData.h`) straight from MIRRORWALL
(CMake `MEXCORE_DIR`): MEX-EQ matched-magnitude filter design, CLA-2A / CLA-76 / SSL G / RVox comp models, de-esser.
One source of truth: improve them once, both plugins get it. The DREAMMIC workflow also rebuilds when those 3 files change.

## Flow (v2)
input (knob or **Auto** rider → −18 dBFS, max 3 dB/s) → undo your mic → target mic curve (+ its **Low cut / Bass roll** and
**Presence** switches) → proximity (Distance + Pattern) → high-pass → [oversampled + anti-aliased: mic tube colour → input
transformer (lows saturate first) → preamp gain stage] → preamp tilt + Air + 5 Hz DC block → **tracking comp** (linked, one
detector on the mono sum) → **de-esser** (4.2–10 kHz listener) → output (+ **Level match** make-up).
- Distance: physics-based proximity, relative to 15 cm as recorded: H = (s + k·a·c/r_new)/(s + k·a·c/r_ref); a = 0.5 cardioid,
  0.63 supercardioid, 0 omni, 1 figure-8; k = proximity scale (1 normal, 0.25 for the RE20's Variable-D).
- Warmth: from about −12 dB into the stage (almost clean) to the preamp's full drive.
- "Your mic" (v2): Flat, C01U Pro (measured), MA-50 **or any of the 18 other modelled mics** (undoes its curve; HP/LP not undone).
## Modes
| | LIVE | HQ |
|---|---|---|
| Mic curve | MEX-EQ matched-magnitude IIR (v1: RBJ bilinear) | one linear-phase FIR (4096 taps, 8192 above 50 kHz) built from the **analog** curve (v1: from the bilinear biquads) |
| Preamp | 2× IIR halfband + ADAA (v2) | 8× FIR equiripple + ADAA |
| Latency | 3 samples (0.07 ms) | 2112 samples (48 ms) @ 44.1 kHz, reported exactly |
| CPU (Linux test, comp + de-ess on) | ~49× real time | ~14× real time |

## v2 test results (Nov 2026, `dm_test`, also run in CI on Windows)
**1. Mic curves — LIVE filter vs the intended analog curve (max error, 20 Hz–20 kHz @ 44.1 kHz):** v1 worst **18.9 dB**
(S7 Classic), v2 worst **0.69 dB**. Below 12 kHz: v1 up to 1.2 dB (A12's 11.5k peak), v2 ≤ 0.3 dB. 96 kHz: ≤ 0.18 dB.
Lesson: bilinear (RBJ) filters crush everything near Nyquist — v1's LP/shelf top octave was far darker than designed.
**2. LIVE vs HQ:** within 0.32 dB (50 Hz–16 kHz).
**3. Preamps** (220 Hz, −12 dBFS; THD % at Warmth 0/5/10; H2/H3 at 10):
| Preamp | THD 0/5/10 | H2 / H3 dB | | Preamp | THD 0/5/10 | H2 / H3 dB |
|---|---|---|---|---|---|---|
| AV-737 Tube | 0.6 / 2.5 / 9.1 | −23 / −25 | | TF-V76 Tube | 0.7 / 3.0 / 10.6 | −22 / −24 |
| N-73 | 0.1 / 0.5 / 6.9 | −38 / −23 | | EMI-TG | 0.1 / 0.4 / 10.5 | −40 / −20 |
| U-610 Tube | 0.8 / 3.4 / 11.7 | −20 / −24 | | H-69 | 0.1 / 0.4 / 7.4 | −42 / −23 |
| A-512 | 0.0 / 0.2 / 7.8 | −50 / −22 | | FC-ISA | 0.1 / 0.3 / 3.4 | −45 / −30 |
| MV-Box Tube | 0.5 / 2.1 / 7.9 | −24 / −26 | | HV-Clean | 0.0 / 0.0 / 0.3 | – / −51 |
| S-4K E | 0.0 / 0.2 / 3.3 | −50 / −30 | | RN-Silk | 0.2 / 0.8 / 4.5 | −34 / −28 |
| | | | | TT-MP1 Tube | 0.6 / 2.2 / 8.3 | −24 / −26 |
Tubes = mostly 2nd harmonic; consoles/solid state = 3rd at high drive; HV-Clean ≈ a wire.
**Aliasing** (7 kHz sine at −6 dBFS, Warmth 10, LIVE; non-harmonic energy vs fundamental): v1 → v2:
AV-737 −49 → −59 dB, N-73 −45 → −58, U-610 −42 → −53, A-512 −47 → −58, MV-Box −49 → −60 (ADAA ≈ 10–12 dB cleaner).
**4. Tracking comp** — gain reduction on a synthetic sung line at −18 dBFS, Comp 2 / 5 / 8:
Opto 1.0 / 4.1 / 15.4 · FET 1.3 / 5.0 / 10.6 · VCA (SSL G) 2.2 / 4.5 / 7.7 · Leveller (RVox) 1.7 / 4.4 / 8.8 dB.
Thresholds tuned so **Comp 5 ≈ 4–5 dB on every type** (opto −6, FET −8, VCA −5, leveller −7 dB at Comp 0; −2.6 dB per step;
VCA/RVox thresholds passed as panel values +15/+12 dB, as measured in MIRRORWALL). Static make-up = 70 % of the expected GR.
**5. Level match** (K-weighted loudness out vs in, synthetic sung line): off −4.0…+3.4 dB → on −0.9…+1.1 dB.
**6. Old v1 projects** load with the same settings; every v2 feature defaults to off → v1 projects keep their v1 controls.
(The tone itself changes a little: v2 filters are the corrected ones — mostly the top octave.)
**7. Presets:** 10 factory + user save/load (`Documents\DREAMMIC\Presets\*.dmpreset`, XML) OK.

## How the v2 pieces work
- **Level match:** when settings change (debounced 250 ms, off the audio thread) a fresh LIVE copy runs 4 s of speech-shaped
  noise (HP 100 Hz, −6 dB/oct above 2 kHz, 3 syllables/s with gaps) at −18 dBFS; make-up = K-weighted (BS.1770) loudness in − out.
  First try used bandpassed white noise + plain RMS: errors up to 6 dB on bright mics → don't measure loudness without K-weighting.
- **Comp on stereo:** MIRRORWALL's stages are mono and pure gain (y = x·g), so DREAMMIC feeds them the mono sum and applies
  g = y/x to both channels (holds the last g when x = 0). No changes needed in Live.h.
- **Test voice:** no vocals in the cloud workspace, so `dm_test` synthesises a sung line (gliding pitch + vibrato, 30 harmonics
  through 3 moving formants, syllable gaps, an "s" burst every 1.3 s). Good for comps/de-ess/level tests, not for judging tone.

## Gear list → see 20_GEAR_AND_VOCAL_DATA.md
## Limits / next
- Mics and preamps are still modelled from published specs + known character, not captures. Studio captures later
  (30_SIGNAL_CAPTURE_PROTOCOL.md) — swap numbers into `Models.h`, no redesign needed.
- C01U correction covers 1–14 kHz only (the take was pre-filtered below ~100 Hz).
- Comp/de-ess coefficients are rebuilt on the audio thread when their knobs move (small allocations); fine so far.
- Picking the same preset twice in the menu doesn't reload it (ComboBox only fires on change) — pick another first.
- Next: skin upgrade (32_SKIN_DESIGN.md), capture-based models, maybe per-mic pads.
