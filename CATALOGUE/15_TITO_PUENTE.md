# TITO PUENTE — drum stem → full-song MIDI on 16 pads

**What:** FL instrument (Channel rack). Load a 2–6 min drum stem. TITO PUENTE finds every hit, groups them into the
song's own sounds, puts one clean chop of each sound on a pad and writes the FULL SONG MIDI on pad notes 36–51
(pad 1 = C1 in GM / note 36). Swap any pad's sound and the composition stays. Adds groove layers and full-kit
drummer fills with real-drummer feel. Drag FULL SONG / GROOVE LAYERS / FILLS / ALL into FL.
**Code:** `vocal-lab/tito/` (Python engine), `vocal-lab/tito_plugin/` (JUCE 8.0.4, VST3 instrument).
Release `tito-latest` (`TITO-PUENTE-Windows.zip`). Workflow `.github/workflows/tito.yml`. v1 built Nov 2026.

## Window (top to bottom)
- Top: Load drum track · BPM box + x2 / ÷2 (half/double-time fix) · Re-analyze · Sensitivity · Keep timing / Quantize (1/16, 1/32, 1/16T, auto).
- Song strip: sections (Intro / Verse / Hook / Bridge / Outro), every hit per pad (song gold, layers blue, fills pink),
  fill spots shaded, playhead, click to seek.
- 16 pads, MPC layout (pad 1 bottom-left). Dot colour = gold chop from the song / green your sound / grey factory.
  Click = select + play (lower on the pad = softer). Drop a sample on a pad to load it. Shuffle kit · Lock pad · Save / Load kit (`.titokit`) · Sound folder.
- Pad editor: waveform with draggable start / end, Start, Length, Pitch (±24 st), Decay (100 % = sample's own),
  Volume, Pan, Loop, Reverse, Choke group (open hat / closed / pedal default group 1), Output (main or pad out 2–17),
  Load sound, Shuffle pad, Song chop (back to the song's hit), Factory.
- Groove layers: Hat rolls · Ghost snares · Shaker · Perc · Open hats, Grid (1/16, 1/32, 1/16T, 1/32T, 1/8T, BPM-locked),
  Swing, Drummer feel, Style (36 + Auto + From this song).
- Drummer fills: Every 8 bars · Every 16 bars · Before hook, Length (1 beat, 2 beats, 1 bar, 2 bars), Intensity,
  Crash after, Make room (clears the groove under each fill), New fills + layers (re-roll).
- Play preview through the pads (Song / Layers / Fills / Original stem A/B), drag tiles, Open folder.
- Files: `Documents\TITO PUENTE\<song>\` (pads\*.wav chops + MIDI), kits in `Documents\TITO PUENTE\kits`,
  sound-folder index cached in `Documents\TITO PUENTE\library_*.txt` (scanned once).

## Engine (`tito/transcribe.py`)
1. Onsets: band-balanced superflux (lows / mids / highs each normalised, so a quiet hat counts like a loud 808),
   generous thresholds (junk is removed later). Refined to ~1 ms: steepest rise on full signal OR 200 Hz high-passed.
2. Attack spectrum per hit = energy the hit ADDED: (post − pre)² / post per mel band, then every band divided by its
   own 95th-percentile hit level (else 808 tails / room wobble dominate hats).
3. Look-alikes within 42 ms merged (clap bursts, flammed samples).
4. Grouping: leader clustering → average linkage (cosine, thr 0.09) → split by ring-out (closed vs open hat) →
   near-duplicate merge (cos > 0.90) → combo groups (two sounds together) dropped by NNLS → junk filter.
5. Every hit decomposed over the sounds (NNLS) → a hat under a kick is its own note (judged against its OWN bands).
6. Layer discovery: what the sounds leave unexplained, if it repeats, is a sound (crash + kick that only ever hit with the 808).
7. Duplicate pruning: a sound whose own hits are explained almost as well without it (< 10 %) is merged away.
8. Names: nearest reference profile (centroid, low share, high share, decay, tonality; measured on the factory kit)
   + Hungarian assignment, toms by pitch, hats by ring time, then RHYTHM check (sound on 2 & 4 / 3 = snare, low sound on 1 = kick).
9. Tempo: phase coherence of all hits on 16th/8th/beat grids over 55–200 BPM (precise to 0.01), octave picked inside
   80–160 BPM. Steady → straight grid; drifting → beat map (positions kept beat-relative). Bar 1 from kick-on-1 /
   snare-on-2&4 / crash-on-1. Sections from 4-bar pattern novelty; fills already in the song marked.
10. Chops: the cleanest loud isolated hit per sound, cut to its ring time (×1.6), fades, normalised.

## Grooves, fills, feel (`tito/generate.py`, `tito/styles.py`, `tito/library.py`)
- Style library `tito/data/styles.json` (323 kB): 36 styles + 77 GMD styles; **647 real fills** from the Groove
  MIDI Dataset (13.6 h, real drummers on a Roland TD-11, CC BY 4.0, `tito/data/groove-v1.0.0-midionly.zip`).
- Drummer feel per style learned from GMD beats: per-16th mean push/pull + spread (e.g. rock/funk off-16ths ~10 % late,
  jazz much more), applied with the Drummer feel knob; Swing knob = up to triplet feel on the off-16ths.
- Fill families (procedural, intensity-aware): toms_down, snare_roll, trap_roll, drop, chop, timbal, abanico, tarola
  (banda), talking (afro), log_roll (amapiano), blast (metal), build, kick_run (jersey), darbuka; real GMD fills
  picked by density ~ intensity, trimmed to keep the END (where it lands). "From this song" reuses the song's own fills.
- Styles: trap, drill, boom bap, hip-hop, R&B, reggaeton, corridos tumbados, banda, cumbia, salsa, bachata, merengue,
  samba/bossa, afro-cuban, dancehall, reggae, afrobeats, afrobeat, amapiano, highlife, rock, punk, metal, funk, jazz,
  gospel, New Orleans, country, pop, house, techno, DnB, jersey club, UK garage, Middle Eastern, bhangra.
- Auto style: 808 present → trap (≥120 BPM) / boom bap; else by tempo.

## Plugin (`tito_plugin/`)
- Instrument, MIDI in, notes 36–51 → pads; one-shot (Loop pads stop on note-off). 64 voices, choke groups, −9 dB headroom.
- Outputs: Main + 16 optional stereo pad outs (enable in FL's plugin wrapper → each pad to its own mixer track).
- Preview sequencer plays the built MIDI through the pads (so swaps are heard at once); Original = stem A/B via t0.
- State saved in the FL project (pads, params, files, settings, analysis/build).
- Offline check tool `tito_render` (`-DTITO_RENDER_TOOL=ON`): preview path + MIDI path → WAV.

## Test results (v1, `python -m tito.bench`, factory-kit variants, F1 of hits per sound, 30 ms)
| Test | F1 | Notes |
|---|---|---|
| Trap 140 / 143 | 0.84 / 0.86 | kick layered under 808+crash not separated (4 hits) |
| Boom bap 90 | 0.82 | |
| Afrobeats 108 | 0.78 | |
| Reggaeton 95 | 0.74 | |
| Drill 142 / House 124 | 0.58 / 0.40 | ride + hats always together merge |
| Real drummers (GMD, 11 styles) | 0.28–0.84, mean ≈ 0.6 | kick/snare 0.8–1.0; hat articulations (closed/pedal/open) weak |
| Tempo | 17 / 19 right (x2 / ÷2 counted right) | misses: live rock 96 read 144 (3:2), reggae 141 read 106 |
| Timing | median 0.5–3 ms (soft shakers / open hats up to ~11 ms) | |
| Plugin render (tito_render) | pads fire within 1.3–1.9 ms of the MIDI, peak 0.97 over 100 s | |
CI (`packaging/tito_test.py`) fails the build if trap < 0.72, boom bap < 0.70, reggaeton / afrobeats < 0.62 or tempo wrong.

## Limits / next (v1.x tweaks)
- Over-splitting on long songs: one sound can land on 2–3 pads (e.g. hats) — composition still right, but swapping
  that sound means swapping each pad. Next: "link pads" or stronger duplicate merge.
- Names can be off (a closed hat named Open hat, an 808 named Tom low); hits are still on the right pad.
- Hat articulations on live kits; ride + hat played together on every note merge into one pad.
- Sections: obvious changes found; subtle verse/hook differences not always.
- Not yet: separating drums from a full mix (input must be a drum stem), MIDI tempo map export for drifting live tracks.
