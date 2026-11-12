# Signal-flow learning — teaching the plugins from real gear

Goal: replace "modelled from specs" with "measured from the real thing". Works for hardware (studio mics/preamps)
and for our installed software (Waves etc.) — we measure what comes OUT, we never open their code.
Keep captures of commercial plugins for our own use (their licences may restrict selling clones).

## Three ways to learn
1. **Paired dry/wet (best for MIRRORWALL):** render the SAME raw take through a real chain in FL (nothing else changed,
   same start and length). MIRRORWALL can then fit settings almost exactly.
2. **Device capture (DREAMMIC, MIRRORWALL stages):** play test signals through the device/plugin and record the output.
3. **Mic capture (DREAMMIC mics):** record the same voice on the target mic and on a reference mic at the same time
   (same position, same distance) → difference = the mic's curve. Do 3 distances (5, 15, 30 cm) for proximity.

## Test signal pack (to build: `capture_signals.wav`)
| Part | Length | Teaches |
|---|---|---|
| Log sine sweep 20 Hz–20 kHz at −30, −18, −12, −6 dBFS | 4 × 10 s | frequency response at each level (EQ + how it changes when driven) |
| Sine bursts 100 Hz / 1 kHz / 5 kHz, level steps −40→0 dB in 2 dB steps | ~60 s | saturation curve, harmonics (2nd vs 3rd) |
| Level-step tones (−30 → −6 → −30 dB) with fast and slow steps | ~30 s | compressor threshold, ratio, attack, release |
| Pink noise bursts + silence | ~20 s | gate thresholds, noise floor, release |
| Clicks / impulses | ~10 s | latency, delay times, reverb tails |
| 30 s dry vocal (our raw MA-50 + C01U takes) | 30 s | real-world check |

## Settings matrix per unit (keep it small)
- Preamps: Gain/Drive at 3 settings (clean, medium, hot) × EQ flat.
- Compressors: 3 thresholds × 2 attack/release settings × 2 ratios.
- Mics: 3 distances × pattern(s) the mic has.
File names: `<unit>_<setting>_<level>.wav`, 24-bit, 44.1 or 48 kHz, no other processing.

## What we do with captures
- Fit EQ curves (we already do this to ±0.4 dB), saturation curves (polynomial / waveshaper fit), compressor timing.
- Swap the new numbers into `dreammic_plugin/Source/Models.h` or MIRRORWALL stages — no redesign needed.
- Keep a "capture library" folder in the repo (`captures/`) with the raw recordings + fitted JSON.

## Priority list
1. Our Waves vocal plugins (RVox, the C-type compressors) → MIRRORWALL stages that match our old chains.
2. Rebuild one old chain in FL → paired dry/wet → near-exact MIRRORWALL preset.
3. Studio day: C-800G, U47/U67/U87, 251, SM7A + Avalon / Neve / UA 610 / API / SSL (≈15 min each).

## Fastest method: Edison multisine (used for MEX-EQ, Nov 2026)
Angel's idea: put Edison after the plugin and turn the knobs while a signal plays; Claude knows each step it made.
- Signal: **looping multisine** — every FFT bin 15 Hz–20.5 kHz at once, flat, low crest (Schroeder phases), period 8192 samples,
  −20 dBFS RMS (`model/make_ms.py`, file `MEX_multisine_150s.wav`). Every 8192-sample slice of the recording = one exact curve.
- Setup: audio clip in the playlist → plugin on the Master → **Edison** after it ("On input", armed). Other plugins in the
  chain switched off with the **green light** in the mixer slot (Angel's rule).
- Claude steps the knobs with mouse-wheel scrolls (10 ticks) and holds ~1.2 s per step; ~20–60 steps per take, 2.5 min.
- Analysis (`model/ms_an.py`): FFT every period, divide by the source spectrum, split into steady segments automatically
  (consecutive curves within 0.08 dB), median per segment → one curve per knob setting. No timestamps needed.
- Result: ~150 settings of the SSL G-EQ in 3 takes (vs 1 setting per render before). Accuracy ≈ 0.01 dB.
- Gotchas: hold each step ≥ 1 s; reset knobs with **Alt-click** (Waves); FL Ctrl+R export needs "Enable master effects" ticked
  for sweep renders; typing in the Claude reply box steals FL's keyboard focus — Angel keeps hands off during runs.
- Waves ANALOG switch: adds constant hiss even in silence — measure with it OFF (curves), harmonics only if ever needed.
