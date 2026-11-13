# TITO PUENTE — drum stem → full-song MIDI on 16 pads

**What:** FL instrument (Channel rack). Load a 2–6 min drum stem. TITO PUENTE finds every hit, groups them into the
song's own sounds, puts one clean chop of each sound on a pad and writes the FULL SONG MIDI on pad notes 36–51
(pad 1 = C1 in GM / note 36). Swap any pad's sound and the composition stays. Adds groove layers and full-kit
drummer fills with real-drummer feel. Drag FULL SONG / GROOVE LAYERS / FILLS / ALL into FL.
**Code:** `vocal-lab/tito/` (Python engine), `vocal-lab/tito_plugin/` (JUCE 8.0.4, VST3 instrument).
Release `tito-latest` (`TITO-PUENTE-Windows.zip`). Workflow `.github/workflows/tito.yml`. v1 built Nov 2026; v1.1 (field-test fixes) Nov 13 2026.

## Window (top to bottom)
- Top: Load drum track · BPM box + x2 / ÷2 (half/double-time fix) + **3:2 menu** (×2/3, ×3/2, ×3/4, ×4/3 for swung / triplet grooves read at the wrong meter, v1.1) · Re-analyze · Sensitivity · Keep timing / Quantize (1/16, 1/32, 1/16T, auto).
- Song strip: sections (Intro / Verse / Hook / Bridge / Outro), every hit per pad (song gold, layers blue, fills pink),
  fill spots shaded, playhead, click to seek.
- 16 pads, MPC layout (pad 1 bottom-left). Dot colour = gold chop from the song / green your sound / grey factory.
  Click = select + play (lower on the pad = softer). Drop a sample on a pad to load it. Shuffle kit · Lock pad · Save / Load kit (`.titokit`) · Sound folder.
- Pad editor: waveform with draggable start / end, Start, Length, Pitch (±24 st), Decay (100 % = sample's own),
  Volume, Pan, Loop, Reverse, Choke group (open hat / closed / pedal default group 1), Output (main or pad out 2–17),
  Load sound, Shuffle pad, Song chop (back to the song's hit), Factory, **Link pads** (v1.1: the same drum found on 2+ pads is
  linked - loading / shuffling a sound on one changes them all; two blue rings on linked pads).
- Groove layers: Hat rolls · Ghost snares · Shaker · Perc · Open hats, Grid (1/16, 1/32, 1/16T, 1/32T, 1/8T, BPM-locked),
  Swing, Drummer feel, Style (36 + Auto + From this song).
- Drummer fills: Every 8 bars · Every 16 bars · Before hook, Length (1 beat, 2 beats, 1 bar, 2 bars), Intensity,
  Crash after, Make room (clears the groove under each fill), New fills + layers (re-roll).
- Play preview through the pads (Song / Layers / Fills / Original stem A/B), drag tiles, Open folder.
- **FL tempo warning** (v1.1, orange, transport row): FL's tempo is read every block; if it isn't the song's BPM the window says so
  (dragged MIDI follows FL's tempo, so a 130 project plays a 140 song 7 % slow). Live-tempo songs get the File > Import tip instead.
- Files: `Documents\TITO PUENTE\<song>\` (pads\*.wav chops + MIDI), kits in `Documents\TITO PUENTE\kits`,
  sound-folder index cached in `Documents\TITO PUENTE\library_*.txt` (scanned once).

## Engine (`tito/transcribe.py`)
1. Onsets: band-balanced superflux (lows / mids / highs each normalised, so a quiet hat counts like a loud 808),
   generous thresholds (junk is removed later). Refined to ~1 ms: steepest rise on full signal OR 200 Hz high-passed.
2. Attack spectrum per hit = energy the hit ADDED: (post − pre)² / post per mel band, then every band divided by its
   own 95th-percentile hit level (else 808 tails / room wobble dominate hats).
3. Look-alikes within 42 ms merged (clap bursts, flammed samples).
4. Grouping (v1.1 **adaptive**): leader clustering → average linkage, cosine thr 0.09 for drum machines; if the sounds
   cover < 80 % of the song's loudness (live drums / room / separator smear: no two hits alike) the threshold loosens
   step by step (0.13 → 0.30). Then split by ring-out → near-duplicate merge (cos > 0.90) → combo groups dropped by
   NNLS → junk filter (judged against the song's 99th-percentile hit, **not its single loudest** - one freak transient
   wiped out every sound in "peso p" in v1).
5. Every hit decomposed over the sounds (NNLS) → a hat under a kick is its own note (judged against its OWN bands).
   v1.1 **ghost-note rule**: a 2nd/3rd note on the same hit stays only if dropping it worsens the band-balanced fit by
   ≥ 2 % of the hit (`KNOBS["unique"]`) - look-alike sounds sharing one hit become one note.
6. Layer discovery: what the sounds leave unexplained, if it repeats, is a sound (crash + kick that only ever hit with the 808).
7. Duplicate pruning: a sound whose own hits are explained almost as well without it (< 10 %) is merged away.
8. Names (v1.1 rework): two tone measurements per sound - the v1 mel template (robust to an 808 gliding under a hat)
   and a **full-resolution solo-hit spectrum** (1/3-octave bands to 20 kHz, only bands that jump ≥ 3 dB at the hit,
   hits where nothing else is struck; sees hats above the 11 kHz analysis ceiling). Each name is scored on whichever
   view suits it (`PROTO` / `PROTO_S`), plus a **song-relative** view (dark / lo-fi kits scaled so the song's
   brightest frequent sound sits where a factory hat sits), plus **physical sanity** (a kick has lows, a hat/cymbal
   doesn't, a tom with no lows and bright = not a tom) and **rhythm** (a crash is rare and on a beat, a closed hat
   keeps time, busy + short = closed hat not ride/crash, a snare is on the backbeat; second naming pass once the
   grid is known). Busiest short hat = Closed hat. Then the v1 rhythm swap (sound on 2&4 = snare, low on 1 = kick).
   Tonality capped (v1 gave hats / sub 808s 10¹² and dumped them on Perc / FX).
9. Tempo: phase coherence of all hits on 16th/8th/beat grids (55–200 BPM), octave inside 80–160. v1.1: **3:2 / 4:3
   check** (at the true tempo kick + snare land on beats; at 3:2-off half fall between). **Steady test on the hits**
   (16th-grid phase of every 16-beat window must agree; a linear phase drift corrects the period) → exact BPM
   (CSIRAC = 140.00, v1 said 139.55 and drifted ~0.5 s over the song). Grid phase locked on the 16th grid first
   (v1's beat-level phase was dragged ~20 ms late by syncopation). Drifting songs: **smooth beat map anchored on the
   hits** (local tempo + 16th phase per 16-beat window, smoothing spline) instead of the tracker's jittery beats.
   Bar 1 from kick-on-1 / snare-on-2&4 / crash-on-1 + a mild "songs start on a 1" nudge.
10. Sections (v1.1): novelty from pattern + **audio level per bar** + hit density, bounded threshold + local peak;
    neighbours that are the same part merge; labels Intro / Verse / Hook / **Breakdown** (≥ 6 dB under the song or
    < 45 % of its hits) / **Build** (short part rising into a hook) / Bridge (one per song) / Outro; what comes back
    loud after a breakdown is the Hook. Fills already in the song marked.
11. Chops: the cleanest loud isolated hit per sound, cut to its ring time (×1.6), fades, normalised. v1.1: chop
    choice penalises hits with another sound struck or still ringing underneath; a sound with no lows whose only hits
    sit on an 808 gets the borrowed sub high-passed out (v1: Fa$ "Snare" chop was 91 % sub).
12. Linked pads (v1.1): same family + look-alike attack (cos > 0.8) + they take turns (< 15 % together) → `links`.

## MIDI export (`tito/cli.py`, `tito/common.py`)
- One tempo for steady songs. Drifting songs with Keep timing: **tempo map** (`--tempo-map auto|on|off`, a set_tempo
  per beat from the engine's beat map) - the MIDI plays back on the band's real timing (0.3–1.6 ms on real live
  stems). FL: drag = notes on FL's grid at the project tempo; File > Import > MIDI keeps the tempo changes.

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

## Field test 1 (Nov 13 2026): Ninajirachi "CSIRAC (Dylan Brady remix)" drum stem (fadr split)
What went wrong in v1 and what fixed it (all now in the CI test as `electro_pulse_dark` + field-test checks):
- Render played 7.7 % slow: FL project was at its default 130 BPM (dragged MIDI ignores the file's tempo) AND v1 read
  139.55 instead of 140 → FL tempo warning + hit-anchored exact BPM.
- A quarter-note hat pulse (214 hits) was named **Crash**, other bright sounds Tom mid / Tom high / Clap / Perc, hat
  pads empty → Shuffle kit loaded crash / tom samples on hats. → naming rework (two views, song-relative, rhythm).
- Section labels: all "Verse", no hook → "Before hook" fills had nowhere to go → loudness-aware sections (now
  Verse / Breakdown 35–56 / Build / Hook).
- Timing / detection were already good: 93 % of audible onsets, 5 ms median.

## Real-stem bench (`python -m tito.realbench FOLDER`, 18 real drum stems: Suno stems of our songs + fadr splits)
No answer key, so it measures: recall of audible onsets (24 dB window), precision vs a sensitive detector, timing,
BPM vs the BPM in the file name, naming red flags measured on the chops (crash_busy, tom_bright, hat_missing,
kick_thin, snare_boom), share of stacked notes. Name files `<song>__<bpm>.wav` (0 = unknown).

| | v1 | v1.1 |
|---|---|---|
| recall (mean of 18) | 0.859 | 0.911 |
| precision | 0.985 | 0.967 |
| worst recall | peso p 0.30 (1 pad), Sinnerman 0.50 (2 pads) | Sinnerman 0.74, Dutch Melrose 0.75, mansn 0.76 |
| naming red flags | 8 | 3 |
| steady grid found | 10 / 18 | 16 / 18 (the 2 left really drift: stepper 130→133, Clave Irony 97–100) |
| known BPMs right | 6 / 6 | 6 / 6 (+ CSIRAC 140, peso p 112, Orange demo / PEACE / Chris Grey 93, Lithe 122 now exact) |

## Synthetic bench v1.1 (`python -m tito.bench`, 23 cases incl. 4 new dark / processed electronic kits)

| | v1 | v1.1 |
|---|---|---|
| mean F1 | 0.621 | 0.633 |
| names (hits on a correctly named pad) | 0.384 | 0.417 |
| tempo right | 21 / 23 | 22 / 23 (live rock 96 now right; GMD reggae 141 still read 106 - swung so hard a 106 straight grid fits better; 3:2 menu ×4/3 fixes it) |

CI (`packaging/tito_test.py`) v1.1 adds floors for `electro_pulse_dark` 0.50, `boombap_dark` 0.70, `house_dark` 0.33,
the field-test checks (steady + exact 139.55 BPM, Crash ≤ 1 hit per bar, quiet middle found as Breakdown) and the
tempo-map round trip (drifting live case: MIDI vs performance median < 5 ms; measured 0.4 ms).

## Limits / next (v1.x tweaks)
- Naming is still the weakest part (0.42): snare vs clap, closed vs pedal hat, and house (kick + clap + ride all on
  the beat: a clap that never plays without the kick is learned as one sound, F1 ~0.4). Next: peel a known
  co-occurring sound out of a combo template.
- Recall on heavily bled fadr stems (Dutch Melrose, mansn) ~0.75: soft hits under other instruments' bleed.
- Bar 1 on syncopated electronic tracks is a best guess (sections and fills use it).
- Not yet: separating drums from a full mix (input must be a drum stem; `audio-separator` is already a repo dependency).
