# Clean Vocal

**What:** AI vocal cleaner. Removes hiss, hum, fan and room noise and leaves the voice alone. Live insert in FL.
**Repo:** `1angelquepaso-ops/clean-vocal-vst` (Rust: nih-plug + DeepFilterNet3). VST3 + CLAP. Installed (v2).

## Controls
| Control | Does | Default |
|---|---|---|
| Strength | How much noise to remove (100% = everything that isn't voice) | 100% |
| Extra Clean | Stronger pass on what's left between phrases | On |
| Hum Cut | Cuts rumble/hum below Hum Cut Freq | On |
| Hum Cut Freq | Where the hum cut starts | 75 Hz |

## Tech notes
- DeepFilterNet3 speech-enhancement model, fully offline (no account/internet/credits).
- ~50 ms latency, reported to FL. Renders are always fully processed.
- Any project sample rate. On a weak PC: bounce once, then turn it off.
- Also has an offline command-line tool (`src/bin/cv_offline.rs`).
- Build: `cargo xtask bundle clean_vocal --release`; GitHub Actions builds Windows bundles on push.
- License GPL-3.0-or-later (VST3 bindings); DeepFilterNet MIT/Apache-2.0.

## Ideas for next version
- Skin matching the new family look (see 32_SKIN_DESIGN.md).
- Optional "de-breath" and "de-click" modes; a learn-noise button for very noisy rooms.
