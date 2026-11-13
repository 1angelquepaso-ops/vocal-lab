# VST CATALOGUE — index

Angel Marquez (XngelMarqz) · FL Studio 2024 on Windows · plugins built with Claude, Nov 2026.
This folder is the bank of everything we know about our plugins: what each one does, how it is built,
the data behind it, what we tested, and where each one goes next. Use it to rebuild, upgrade, or start new plugins.

## The plugins

| # | Plugin | What it does | Status | Code (GitHub) | Download (release tag) |
|---|---|---|---|---|---|
| 1 | **Clean Vocal** | AI noise / hum / room cleaner for vocals (DeepFilterNet3), live insert | v2 installed | `1angelquepaso-ops/clean-vocal-vst` | repo Releases |
| 2 | **vocal-lab Harmonizer** | Theory-based harmony generator: load a dry vocal chunk (+ optional beat for analysis), generate harmonies one at a time, drag stems into FL | v2 installed | `1angelquepaso-ops/vocal-lab` (`vocal_lab/`, `plugin/`) | `latest` |
| 3 | **PIRATA** | Chord + MIDI extractor for samples, Simple (melody line) / As-played / Rich (melody + lush chords), melody layer (complement, counter, amplify, random, arp), grand-piano preview, drag MIDI | v2 built (Nov 2026) | `vocal-lab` (`pirata/`, `pirata_plugin/`) | `pirata-latest` |
| 4 | **MIRRORWALL** | Vocal chain emulator: 8 stock chains fitted from our own old vocals + EMULATOR (reference acapella → new preset), autotune with key/scale, tweak knobs, mic match | v1 (offline) installed | `vocal-lab` (`mirrorwall/`, `mirrorwall_plugin/`) | `mirrorwall-latest` |
| 5 | **DREAMMIC** | Real-time mic + preamp emulator: 19 vocal mics (+ their switches), 13 preamps, tracking comp (MIRRORWALL's fitted models) + de-esser, Auto input, Level match, presets, LIVE + HQ | v2 built (Nov 2026) | `vocal-lab` (`dreammic_plugin/`) | `dreammic-latest` |
| 6 | **TITO PUENTE** | Drum stem → full-song MIDI on 16 MPC pads (one chop per sound, swap sounds, keep the composition), groove layers + real-drummer fills in 36 styles, drag MIDI | v1 built (Nov 2026) | `vocal-lab` (`tito/`, `tito_plugin/`) | `tito-latest` |

Install folder on Angel's PC: `C:\Program Files\Common Files\VST3\` → then FL: Options → Manage plugins → Find installed plugins.
Audio material lives in `E:\EXTRACTION\IDEAS` (acapellas, raw vocals, stems).

## Files in this catalogue

| File | Read it for |
|---|---|
| 01_BUILD_PLAYBOOK.md | How every plugin is built, shipped and installed; all the mistakes we hit and their fixes |
| 10_CLEAN_VOCAL.md … 15_TITO_PUENTE.md | One file per plugin: features, design, engine, data, test results, limits, next steps |
| 20_GEAR_AND_VOCAL_DATA.md | Measured data: our 6 old vocal chains, the harmony chain, mics (MA-50, C01U Pro), mic & preamp models |
| 30_SIGNAL_CAPTURE_PROTOCOL.md | How to "teach" the plugins from real gear or our Waves plugins (signal-flow learning) |
| 31_UPGRADE_ROADMAP.md | Next versions of every plugin, in order |
| 32_SKIN_DESIGN.md | Current look of each plugin and the plan for better skins |
| 40_PLANNED_BUILDS.md | Tools not built yet (voice clone, aligner, sample index, Waves host, …) |

## House rules we work by
- Discuss in chat first; build after Angel says go.
- Real times, not guesses: PIRATA took ~35 min to build + ~6 min Windows build + ~10 min install.
- Every plugin is its own repo folder, its own GitHub Actions workflow, its own release tag.
- Test before shipping: Linux build + screenshots, measured numbers, demo audio for Angel to judge by ear.
- Repo stays public (Angel's choice).
