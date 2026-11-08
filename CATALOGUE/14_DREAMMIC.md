# DREAMMIC — mic + preamp emulator (real time)

**What:** Live insert. Undo your real mic, become one of 10 vocal mics, through one of 6 preamps.
**Code:** `vocal-lab/dreammic_plugin/` (`Source/Models.h` = gear tables, `Source/Engine.h` = DSP). Release `dreammic-latest`. 3 MB.

## Flow
input gain → undo your mic (C01U Pro measured / MA-50 / Flat) → target mic curve → proximity (Distance + Pattern) →
high-pass → [oversampled: mic tube colour → input transformer (lows saturate first) → preamp gain stage] → preamp tilt
→ Air (14 kHz shelf) → output gain.
- Distance: physics-based proximity, relative to 15 cm as recorded: H = (s + a·c/r_new)/(s + a·c/r_ref); a = 0.5 cardioid,
  0.63 supercardioid, 0 omni, 1 figure-8. Pattern "Native" keeps the recorded pattern.
- Warmth: from about −12 dB into the stage (almost clean) to the preamp's full drive; output level-compensated.
## Modes
| | LIVE | HQ |
|---|---|---|
| Mic curve | min-phase IIR filters | one linear-phase FIR (4096 taps, 8192 above 50 kHz) |
| Preamp oversampling | 2× IIR halfband | 8× FIR equiripple |
| Latency | ~3 samples (0.07 ms) | 2112 samples (48 ms) @ 44.1 kHz, reported exactly |
| CPU | ~75× faster than real time | ~16× faster than real time |
LIVE vs HQ tone match: within 0.4 dB.
## Preamp test (220 Hz sine, −12 dBFS, Warmth 0/5/10)
Tubes (AV-737, U-610, MV-Box): mostly 2nd harmonic (THD up to 8–11 % at 10). Solid state (N-73, A-512): 3rd at high drive.
S-4K E cleanest (3 % at 10). Level stays within ~2–3 dB.
## Gear list → see 20_GEAR_AND_VOCAL_DATA.md
## Limits / next
- Models from published specs + known character, not captures. Studio captures later (30_SIGNAL_CAPTURE_PROTOCOL.md).
- C01U correction covers 1–14 kHz only (the take was pre-filtered below ~100 Hz).
- Next: capture-based models, presence/bass-roll switches per mic (SM7A), skin upgrade, preset saving.
