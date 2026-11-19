# PYREX MAGIK — Suno stem tempo + key lock (planned → building, Nov 18 2026)

Drop one Suno song's stems (≤12 WAVs) → one clock + one key for every stem → render locked to the FL BPM
(pitch kept) → drag the locked WAVs from the plugin onto the playlist. Pattern A (JUCE window + Python engine).

## Signed-off spec (Nov 18 2026)
| Area | Rule |
|---|---|
| Clock | Drum stem (TITO PUENTE engine). Fallback Percussion → Bass; drums stay the clock unless red |
| BPM | Exact measured value for the math, rounded for display only. Suno label = cross-check (0.5 %) |
| ×2 / ÷2 / 3:2 | Same lattice → Suno label picks the count; 3:2-type → ask; ÷2 / ×2 / 3:2 buttons |
| Bar 1 | PYREX downbeat rule (below) + ±1 beat nudge. File starts on a bar line (silence padded) |
| Key | Keyboard 1.0, Synth/Guitar 0.9, Strings/Bass 0.8, Brass 0.6, Lead vox 0.6; drums never. Top-2 margin < 0.10 → ask. 30 s windows → key-change warning |
| Tuning | Keyboard + vocals only (bass reads were +41 c, unreliable). Correct only if > 10 c and stems agree |
| Switches | BPM Lock (default on), Key Transpose (default off, target = original key, > 3 st warns) |
| Render | Rubber Band R3 (swappable; bake-off vs Signalsmith Stretch). Global / Grid snap / Auto. One time map, trim and length for all stems. Time-only unless Key Transpose. Anchors outside 0.75–1.35 local ratio rejected |
| Output | FL project rate from host (44.1 kHz default), one resample in the render, 32-bit float. `Song - PYREX MAGIK - Stem - 128bpm - F#m.wav` (`Fm→Am` if transposed), own subfolder; originals never touched; Re-lock renders from originals |
| Guard rails | Pre: same length/rate, label vs measured, count, key margin. After: grid error, length, pitch, cross-stem offsets (< 1 ms), independent onset check. Green / amber (flagged sections + markers, drag allowed) / red (no stem gives a clock → drag disabled) |
| Not v1 | Built-in stem player, multi-song batch, editing the playlist |

## Engine (`pyrex/analyze.py`, `pyrex/cli.py`)
`python -m pyrex.cli analyze --folder "<Song> Stems (116BPM)" --out result.json` → one JSON line (house rule),
progress in `progress.txt`. Result: stems + roles, clock (measured/label/final BPM, mode, level, drift, feel,
flagged spans, music bar 1, file start, beat grid from before 0 s to the end), key (+ alt, margin, per stem,
30 s windows, tuning), questions (must ask the user), notes.

### Confidence = drift vs feel (Nov 18 lesson)
Hits of kick / snare / clap only, against straight + triplet lines fine enough to sit ≥ 60 ms apart
(16ths at 116, 32nds at 79). Signed error per hit, then:
- **drift** = 12 s running median = clock error → the stretch must remove it. Spans > 10 ms are flagged.
- **feel** = scatter around it = Suno's human feel → part of the performance, KEPT (stretching it out = warble).
green = drift ≤ 10 ms everywhere; amber = some flagged spans; red = > half the song flagged or no clock.

### Downbeat rule
TITO's bar 1 assumes snare on 2 + 4: on half-time Suno beats (snare once a bar) it lands a beat off.
PYREX: snare/clap share at one beat ≥ 0.7 → half-time, snare = beat 3; two beats 2 apart ≥ 0.7 → backbeat,
kick decides 1; else strongest kick and ALWAYS ask. Rim is not used (TITO names busy hat patterns "rim":
652 hits on La Misma). For a ×2 count both parities of the double grid are tried.

## Bench: 5 Suno songs from Downloads (drums, bass, lead vox, keyboard)
| Song | Label | Final | Mode | Level | Drift | Feel | Bar 1 | Key |
|---|---|---|---|---|---|---|---|---|
| La Misma Condena | 116 | 116 | global | green | 6.1 ms | 0.6 ms | half-time 0.86 | F#m |
| Ódiame mañana | 124 (123.999…) | 124 | global | amber (2 spans) | 13.4 | 3.3 | backbeat 0.13 → ask | F#m vs F# → ask |
| Orange extended | 79 | 79 (read 158) | global | amber (4) | 17.5 | 6.7 | kick → ask | Bm |
| mansn (acapella) | 79 | 79 (read 158) | global | amber (3) | 24.5 | 2.9 | backbeat 0.79 | Fm |
| Clave Irony | none | 98.76 | grid (93–106) | amber (3) | 15.9 | 8.8 | kick → ask | Eb |

Findings
- Suno labels were RIGHT on all 4 labelled songs (exact fit 116.002 / 124.000 / 158.008 = 2×79). An early quick
  librosa read said 124.28 for Ódiame: that was the tracker's error, not Suno's. Labels still only cross-check.
- Half/double is common: both 79 BPM songs read 158 on the drums.
- Long intros: drums start at 6 s / 26 s; vocals start before them → grid extended back past 0 s.
- All stem sets: identical lengths, 48 kHz / 16-bit.
- Suno IS whole-number BPM: TITO's integer snap (< 0.05 BPM) was checked and is right here.

## Step 2: Global-lock render + after-checks (Nov 18 2026)
`pyrex-engine render --analysis result.json --bpm 120 --sr 44100 --out-dir DIR --out render.json`
Pad to the bar line before 0 s → Rubber Band R3 time-only (`-3 -t ratio`) per stem (2 at a time) → one soxr VHQ
resample to the FL rate → exact equal length → **stretcher-delay compensation** → checks.

Test: La Misma Condena, 9 stems, 116 → 120 BPM (5:22 song), 3 min 22 s on 2 cores (analysis 14 s).
| Check | Result |
|---|---|
| Length | 9 / 9 identical (13,743,882 samples) |
| Drums re-measured from scratch | 120.000 BPM |
| Backbone hits vs FL grid | offset −0.03 ms, median 1.8 ms, drift 4.7 ms, feel 1.1 ms, 97 % within 10 ms → green |
| Bar 1 | on an FL bar line (half-time read) |
| Pitch | keys unchanged on all 6 harmonic/vocal stems, tuning within ±1 c (bass ±1 c) |
| Stems vs each other | offsets spread 0.69 ms (< 1 ms) |

Lessons
- **Don't re-detect pads to check timing.** TITO renames sounds on every run (kick ↔ rim), so a fresh analysis
  measures different hits → ±4 ms noise. The checker maps the SAME source kicks/snares through the time map and
  finds each real transient on the output waveform (TITO `refine_times`).
- Checker noise floor (no stretch): median 0.01 ms, p95 6.3 ms = the refine picking a neighbour attack in dense
  hits, not stretch error.
- Rubber Band R3 delays transients by a constant **+1.46 ms** (spread +~1 ms over the floor) → measured per render
  on the drums and every stem shifted by the same samples (65 @ 44.1 kHz). Grid offset +1.37 → −0.03 ms.
- Phase bias: hits sat 2 ms early vs TITO's fitted grid → grid shifted by the backbone hits' median offset.
- Drift flags need ≥ 8 backbone hits in the 12 s window (a sparse break flagged 10.7 ms that wasn't real).
- Engines on the drums (stretcher error median / p95): R3 1.46 / 7.8 ms (32 s), R3 `--window-short` 2.46 / 8.6,
  **R2 `-c 6` 1.39 / 6.7 ms (7 s, 4× faster)**, R2 percussive 4.2 / 9.3. Bake-off (step 5) must include ears on vocals.
- Delivery from Claude's workspace is capped (~30–55 MB worked) → test sets go out as 24-bit FLAC (−138 dB vs
  the float WAV). The plugin itself writes 32-bit float WAV locally.

## Limits / next
- Drift spans on Ódiame / Orange / mansn: grid snap should follow them; check in the render step (step 2–3).
- Bar 1 still asks on 3 of 5 songs; a bass-note / chord-change vote is the next check.
- Key: major/minor margin on Ódiame 0.08 (asks).
- Build order: 1 analysis ✅ → 2 global render + checks ✅ → 3 grid snap + markers → 4 JUCE window + drag-out →
  5 engine bake-off → 6 CI tests + release `pyrex-latest`.
