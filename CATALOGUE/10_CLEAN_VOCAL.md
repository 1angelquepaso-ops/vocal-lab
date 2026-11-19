# Clean Vocal

**What:** AI vocal cleaner. Removes hiss, hum, fan and room noise and leaves the voice alone. Live insert in FL.
**Repo:** `1angelquepaso-ops/clean-vocal-vst` (Rust: nih-plug + DeepFilterNet3 + nnnoiseless/RNNoise). VST3 + CLAP.
**Version:** v3 (Nov 19 2026) — installed; Angel confirmed it works great.

## Modes
| Mode | Engine | Delay (44.1k) | For |
|---|---|---|---|
| LIVE | RNNoise on the audio thread | 30 ms | Monitoring through the plugin while tracking |
| STANDARD | DeepFilterNet3 on a worker thread | 50 ms render / ~65 ms live at 128 buffer / ~74 ms at 512 | Everyday mixing |
| HQ | DF3: cut ≥ 30 dB, Extra Clean on, release ≤ 80 ms | same | Bouncing very noisy takes |
| GENTLE | DF3: cut ≤ 8 dB, Extra Clean off, release ≥ 250 ms | same | Natural, keeps air/breaths |

## Controls
| Control | Does | Default |
|---|---|---|
| Mode | LIVE / STANDARD / HQ / GENTLE | STANDARD |
| Max Cut | Most noise may be turned down (60 = MAX, no limit). Replaces v2 "Strength" | 18 dB |
| Release | How slowly cleaning closes after a phrase (keeps tails) | 100 ms |
| Mix | Dry/wet | 100% |
| Extra Clean | DF post-filter (STANDARD; always on in HQ) | Off |
| Hum Cut / Hum Cut Freq | 2-pole high-pass, always running, crossfaded on/off | On / 75 Hz |
| Mono | Clean L+R together (half the CPU) | On |
Panel top-right: current delay + "catch-ups" (times the AI was late and dry covered).

## Signal flow
in → hum cut → dry (delay line) → [mono sum] → 48 kHz → denoiser → host rate → wet
→ crossfade wet/dry (5 ms, for catch-ups) → release fill (expander-style: g follows wet/dry envelope ratio,
3 ms attack, Release ms release; adds back dry × (g − r)) → Mix.

## v2 → v3: bugs found and fixed
| Bug (v2) | Cause | Fix (v3) |
|---|---|---|
| Very extreme gate | `with_thresholds(-15, 35, 35)`: frames < −15 dB local SNR got a zero mask (hard mute); Strength 100% = unlimited | Thresholds (−100, 100, 100); depth set only by Max Cut (atten limit), default 18 dB |
| Tails chopped / pop when sound returns | DeepFilterNet returns zeros for any frame with mean-square < 1e-7 (≈ −70 dBFS) and doesn't advance its buffers | Add −66 dBFS dither to such frames before the model |
| Flip-flop on loud phrases | Frames > 35 dB SNR skipped processing entirely | Same threshold fix |
| Pops/clicks at low buffer | Whole DF3 frame computed inside FL's audio callback (≈10 ms of work in a 3 ms window on the N150) | Worker thread + rtrb ring buffers; audio thread worst block 0.46 ms at 128 buffer |
| Hard clicks on dropouts | Empty queue played 0.0 | Crossfade to delayed dry; late samples skipped ("debt") so it re-aligns exactly |
| Hitch on play/record | `reset()` cloned the model on the audio thread and wiped its noise memory | Models keep running across transport restarts |
| Zipper / clicks on knob moves | No smoothing; Hum Cut toggle used stale filter state | Cut slews 1.5 dB/frame, Mix 20 ms, Hum Freq glides, Hum Cut crossfades 10 ms, latency switch crossfades 10 ms |
| Stereo = 2× CPU for a mono mic | Model ran per channel | Mono switch (default on) |

## Test results (Nov 19 2026, synthetic TTS vocal + −55 dBFS pink noise + 60/120 Hz hum, 44.1k)
| | v2 default | v3 STANDARD |
|---|---|---|
| Quiet phrase (−40 dBFS) frames chopped > 12 dB | 2.3 % | 0 % |
| Gaps: noise left (input −50.5 dBFS) | −88 dBFS (gated) | −66 dBFS (18 dB cut) |
| Booth take (−78 dBFS noise): gaps | digital zero (gate) | −80 dBFS |
| Alignment vs input | −1 sample | −1 sample |
| Render speed (2-core cloud box, mono) | 6.6× realtime | 6.1× (LIVE 104×) |
- Live playback simulation (paced, 128 buffer): output identical to the render, sample for sample, when no catch-ups.
  One run of three had a 0.2 s catch-up from a machine hiccup; covered by dry, no click.
- "Fast host" (realtime flag but called faster than realtime, like a render): detected → waits → 0 catch-ups.
- Mode switches mid-stream (STANDARD→LIVE→HQ→GENTLE→STANDARD): no spikes, no NaN.
- Test tool: `cv_offline in.wav out.wav --mode .. --host render|fast|paced --block N --switch ms:mode`.
- Not yet measured on Angel's N150: DF3 speed there (expect ~2–3× slower than the cloud box → still > realtime mono).

## Tech notes
- Fully offline (no account/internet/credits). Any sample rate where sr/100 is whole.
- Live margin = min(host max buffer, 1024) + 10 ms + 2 ms; offline renders use no margin and wait for the worker.
- Render detection: ProcessMode::Offline, or 3 callbacks in a row arriving < 30 % of block duration apart.
- Switching LIVE ↔ AI modes changes latency (reported to FL via set_latency_samples).
- Build: `cargo xtask bundle clean_vocal --release`; GitHub Actions builds Windows bundles on push to main.
  Linux build needs libgl-dev, libx11-xcb-dev, libxcursor-dev, libxcb-icccm4-dev, libxcb-dri2-0-dev.
- License GPL-3.0-or-later (VST3 bindings); DeepFilterNet MIT/Apache-2.0; nnnoiseless BSD-3.

## Ideas for next version
- Skin matching the new family look (see 32_SKIN_DESIGN.md).
- Learn Noise (spectral subtraction pre-stage) — deferred: adds ~10 ms delay; DF3 already adapts to the room.
- De-breath and de-click modes.
- Auto-adapt the live margin to the real FL buffer instead of the reported max.
