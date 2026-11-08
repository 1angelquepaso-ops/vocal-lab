# Gear + vocal data (measured)

## Our old vocal chains (from E:\EXTRACTION\IDEAS) — measured fingerprints
Recorded on the Mojave MA-50. Chains used: Antares Mic Mod, tube saturators, Avalon, SSL EQ, FabFilter EQ,
convolver / LuxVerb, Waves C-type compressors, RVox gate-compressor.

| File | LUFS | Peak | Floor dBFS | Spread dB | Crest dB | Width dB | Tuned (≤10¢) | Sibilance idx | Slap | RT60 |
|---|---|---|---|---|---|---|---|---|---|---|
| BAYWATCH 2023 ACAPELLA 2 | −24.6 | −9.5 | −48 | 20.2 | 12.0 | −14.8 | 71 % | 18.5 | 482 ms (weak) | – |
| blue faygo prepro acapella | −26.2 | −12.0 | −140 (gated) | 15.3 | 12.2 | −11.6 | 66 % | 14.4 | – | long |
| daysinn prepro acapella | −20.1 | −6.5 | −39 | 15.3 | 14.9 | −9.3 | 24 % (barely tuned) | 11.5 | – | – |
| drake demo_5 acapella | −18.1 | −3.2 | −78 | 19.2 | 13.4 | −9.8 | 58 % | 16.1 | 134 ms | ~5 s |
| SAIN JHN PEPRO acapella APRIL | −22.7 | −4.6 | −126 (gated) | 15.5 | 15.1 | −17.0 | 80 % (hard tune) | 10.8 | 81 ms stereo slap | – |
| SNOW BUTTON prepro_4 ACAPELLA | −22.2 | −6.8 | −91 (gated) | 20.9 | 13.9 | −16.4 | 65 % | 11.6 | 279 ms | 2.4 s |
| baywatch prepro_2 RAW HARMZ OG (harmony chain) | −26.0 | −7.2 | digital silence | 24.0 | 11.4 | **+4.1 (very wide)** | 59 % | 20.0 | 81 ms | 1.75 s |
| rich shit MEX RAW VOX (raw MA-50) | −19.1 | −2.9 | −86 | 18.4 | 10.1 | mono | 36 % | 34.8 (spiky esses) | – | – |

Shared lead-chain shape vs the raw MA-50 (median): ~−20 dB @125 Hz, −9 dB @500 Hz, +8 dB 2–5 kHz, +18 dB @8 kHz,
+9 dB @12.5 kHz, roll-off above ~16 kHz. Harmony chain: band-limited (HPF ~300 Hz+), very wide (stacked/panned doubles).

## Stock preset settings (MIRRORWALL, fitted)
| Preset | Tune (ms/amount) | Opto | FET | HPF/LPF | De-ess thr | Gate | Doubler | Slap | Reverb | Fit |
|---|---|---|---|---|---|---|---|---|---|---|
| BAYWATCH | 30/1.0 | off | −8 | –/12.5k | 24 | off | 0.25 | – | 1.6 s, 0.02 | 0.52 dB |
| BLUE FAYGO | 30/1.0 | −22 | −8 | 100/16k | 8 | 80 dB | 0.25 | – | 1.6 s, 0.24 | 0.66 |
| DAYSINN | off | −22 | −8 | 100/12.5k | 14 | off | 0.25 | – | 1.6 s, 0.24 | 0.37 |
| DRAKE DEMO | 30/1.0 | off | −8 | 190/12.5k | 20 | 35 dB | 0.25 | 134 ms 0.5 | 3.5 s, 0.24 | 0.39 |
| SAIN JHN | 8/1.0 (hard) | −22 | −8 | 350/12.5k | 8 | 80 dB | – | 81 ms 0.5 | 1.6 s, 0.02 | 0.56 |
| SNOW BUTTON | 30/1.0 | off | −8 | –/12.5k | 14 | 80 dB | – | 279 ms 0.5 | 2.4 s, 0.07 | 0.45 |
| LEAD BLEND | 30/1.0 | −22 | −8 | 70/12.5k | 8 | 35 dB | 0.25 | 134 ms 0.35 | 3.5 s, 0.02 | 0.50 |
| HARMONY | 30/1.0 | off | −8 | 100/16k | 14 | 80 dB | 0.9 (side 1.0) | 81 ms 0.35 | 1.75 s, 0.02 | 1.40 |
Full JSONs: `vocal-lab/mirrorwall/presets/*.json`.

## Our mics
- **Mojave MA-50** (old, gone): transformerless large-diaphragm, cardioid; small lift ~5 kHz; can be bright; sibilance can
  be an issue (matches our raw: spikiest esses of all files).
- **Samson C01U Pro** (now): USB, 19 mm diaphragm, supercardioid, 20 Hz–18 kHz, 16-bit 44.1/48 kHz, 130 dB max SPL.
  Measured vs MA-50 (same singer): C01U is ~5–11 dB brighter 2–10 kHz. Correction (12-band layout, dB):
  `[0, 0.02, −0.06, 0.2, −0.37, 0.78, 0.84, −4.68, −3.13, −4.45, −8.66, +1.53]` (100 Hz shelf … 12.5 kHz shelf).
  Through the lead chains: 5.9 dB too bright without the match → 1.9 dB with it.
  Recording tips: peaks −10 to −6 dB, steady hand-width distance, expect the gate to matter more.
- Orange take (`ORANGE_dry_vocal_1.wav`, C01U): had been gated + high-passed (~100 Hz) before analysis.

## DREAMMIC mic models (response basis)
| Name | Modelled on | Curve notes |
|---|---|---|
| SN-800 Tube | Sony C-800G | ~+7–8 dB 8–10 kHz, +2 @14k, tube |
| N47 Tube | Neumann U47 | +2 dB low shelf, upper-mid push 2.5 & 6k, top rolls ~16 kHz |
| N67 Tube | Neumann U67 | less upper-mid (−1 @3k), +2.5 @9k |
| N87 | Neumann U87 | +3.5 @10k, +1 @5k, FET |
| N149 Tube | Neumann M149 | open 6–11k rise, +1 shelf 15k |
| A12 Tube | AKG C12 | +6 @11.5k (wide 12k resonance) |
| T251 Tube | Telefunken ELA M 251 | rolls off below ~100 Hz, +3 @5k (mesh), +3.5 @12k (narrow) |
| M-Ref Tube | Manley Reference Cardioid | +3 @4.5k, +4 @10k, bright/forward |
| S7 Classic | Shure SM7A (original) | thick lows, +2 @4.5k, −6 shelf 9k, rolls ~14k, dynamic |
| MJ-50 | Mojave MA-50 | +2.5 @5k, +1.5 @10k, FET |
## DREAMMIC preamps
| Name | Modelled on | Behaviour |
|---|---|---|
| AV-737 Tube | Avalon VT-737sp | class-A tube, even harmonics, airy top (+1 @16k) |
| N-73 | Neve 1073 | strong transformer low saturation, +1.2 low shelf, slight 3.2k |
| U-610 Tube | Universal Audio 610 | tube + transformer, +1 lows, −1.5 above 10k |
| A-512 | API 512c | firm op-amp knee, +1 @2k forward mids |
| MV-Box Tube | Manley VOXBOX | tube, +0.7 @250, +1 above 12k |
| S-4K E | SSL 4000 E | clean, +0.8 above 10k |
Sources used: Sound On Sound + Recording Magazine (MA-50), Samson (C01U Pro), Front End Audio (C-800G),
Recordinghacks (ELA M 251), Producer Hive (U47/U67/U87), Shure SM7A guide.
