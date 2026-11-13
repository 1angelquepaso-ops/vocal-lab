# Planned builds (not built yet)

| Tool | Idea | Notes so far |
|---|---|---|
| Voice clone | Model trained on Angel's own vocals (~60 min audio), FL plugin, offline: load WAV → settings → new WAV | RVC; Colab (free) vs RunPod (GPU) undecided; must not lag/break like the DaVinci MCP setup; would make Harmonizer voices sound human |
| Vocal aligner | Lines doubles / harmony takes up to the lead timing | separate tool (decided) |
| Sample library index | Search ~100k samples (packo on desktop ≈5–8 %, plus external drives) and build beats from own sounds | PowerShell pass 1 planned |
| MIRRORWALL Waves host mode | MIRRORWALL loads installed Waves plugins and sets their knobs to match a reference | only on Angel's PC (licences); optional later |
| Capture tool | Generates the test-signal pack + fits captured gear into plugin models | see 30_SIGNAL_CAPTURE_PROTOCOL.md |
| Suno-style system | Generate beats from own library | discussed early; longer term |
| TITO PUENTE | MPC-style 16-pad drum sampler (FL instrument). Load a 2–6 min drum stem → tempo map + hit transcription → one real hit per pad + FULL SONG MIDI on fixed pad notes 36–51, so drum tones swap without losing the composition | Separate plugin from PIRATA (instrument w/ MIDI in, per-pad outs; PIRATA is a preview effect). Input = drum stems/samples (Drums toggle). Pads: 16 default (kick, kick 2, snare, clap, rim, closed/open/pedal hat, 3 toms, crash, ride, 2 perc, FX). Per pad: start, length, loop, reverse, pitch, decay, volume, choke; drop own sound, shuffle by type, lock, save kit. Feel: keep timing / quantize. Groove layers (hat rolls, ghost snares, shaker, perc, swing) on a BPM-locked grid (1/16, 1/32, triplets). Drummer fills = full-kit (kick/snare/toms/hats → crash) every 8 / 16 bars / before hook, with real-time drummer swing on fills + added layers. Styles dataset: trap, boom bap, afrobeat, rock, metal, Latin, other world/ethnic drum styles + their timing structures; harvest real fills from loaded songs. Layers/fills export as separate MIDI tiles. Build after PIRATA v2: engine + accuracy test → pad sampler → groove layers → fills |
