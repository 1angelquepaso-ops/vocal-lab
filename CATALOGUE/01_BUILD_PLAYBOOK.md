# Build playbook — how our plugins are made

## Two patterns we use

**A. Plugin + engine (Harmonizer, PIRATA, MIRRORWALL, TITO PUENTE)**
- The plugin (C++ / JUCE 8.0.4, VST3) is the window and the audio player.
- Heavy work (pitch tracking, transcription, fitting) is a Python program frozen into an `.exe` with PyInstaller and
  shipped *inside* the plugin: `PLUGIN.vst3/Contents/Resources/engine/<name>-engine.exe`.
- The plugin calls the engine with `juce::ChildProcess` on a background thread; the engine prints ONE line of JSON
  (`{"ok": true, ...}` or `{"ok": false, "error": ...}`) and writes progress to `progress.txt` as `0.45|Matching EQ`.
- Results (WAV/MIDI) are written to disk, then the plugin loads them. Nothing heavy ever runs on the audio thread.
- Exports go to `Documents\<PLUGIN>\...` so a drag only ever hands FL one file that already exists.

**B. Pure real-time DSP (DREAMMIC, Clean Vocal)**
- Everything runs inside `processBlock`; no engine. Smaller download (DREAMMIC = 3 MB).
- Latency reported to FL with `setLatencySamples` so FL lines audio up (plugin delay compensation).
- Clean Vocal is Rust (nih-plug + DeepFilterNet3); DREAMMIC is C++/JUCE.

## Toolchain
- JUCE 8.0.4 via CMake FetchContent; `juce_add_plugin(... FORMATS VST3 Standalone)`.
- Python 3.11 on the Windows build machine; engines use numpy, scipy, librosa, soundfile, soxr, numba,
  onnxruntime (PIRATA), praat-parselmouth + pyloudnorm (MIRRORWALL), Rubber Band CLI (Harmonizer).
- GitHub Actions `windows-latest`: one workflow per plugin with `paths:` filters; publishes a zip to a release tag.
- Linux dev loop (Claude's workspace): build Standalone, run under Xvfb, drive with xdotool, screenshot; plus a
  command-line render tool per plugin (`mw_render`, `dm_render`) for exact numeric tests.

## Shipping + install (proven steps)
1. Push to `main` → workflow builds engine, smoke-tests it, builds VST3, zips, publishes release asset.
2. Angel downloads the zip in her own browser (the built-in browser download failed once).
3. File Explorer → Downloads → select zip → Extract all → Browse (opens at Common Files › VST3) → Select Folder → Extract.
4. Windows asks for admin permission ("Continue") → Angel clicks it. Replace files if updating.
5. New plugin name → FL rescan (Options → Manage plugins → Find installed plugins). Same name/path → no rescan needed.

## Lessons learned (mistakes + fixes)
| Problem | Fix |
|---|---|
| Plugin crashed on open (setSize before children existed) | Call `setSize` last in the editor constructor; null-guard `resized()` |
| MSVC error: SafePointer captured inside nested lambda | Capture the SafePointer before `Thread::launch` |
| "drag all unmuted" crashed FL | Drag exactly one pre-saved file from a grip handle (`performExternalDragDropOfFiles`) |
| Full-song input too slow in Harmonizer | Sparse region shifting + progress reporting; design for chunks |
| CI couldn't find Visual Studio generator | Don't force `-G`; let CMake pick |
| PyInstaller missed a package (pyloudnorm) | Add `--collect-all <pkg>`; smoke-test the frozen exe in CI |
| numba `cache=True` breaks in a frozen exe | `cache = not sys.frozen` |
| basic-pitch decoder deleted half-step neighbours (lost B in Am9) | Only clear neighbours that look like bleed (`< 0.6 ×` the note) |
| Chord reader thought E was the key (should be A minor) | Re-weigh key with harmony: tonic chord first/last, V7→i |
| EQ fit stuck at 0 dB | Never skip 0 dB bands during fitting (zero gradient) |
| Curves compared at 1 kHz only (BAYWATCH dip) | Normalise curves over 250 Hz–4 kHz |
| De-esser before the big top boost | Put de-esser after the EQ |
| Reverb "tail" metric unreliable on fast rap | Down-weight it; use stereo width + measured RT60 |
| Told Angel the EQ adds latency (wrong) | MIRRORWALL/DREAMMIC LIVE use IIR filters = zero added latency |
| Hour estimates were off | Quote real times from past builds |
| Placed a pattern clip in FL by accident while closing a dialog | Cancel the New project dialog carefully; don't click the playlist |
| Explorer is click-only for Claude (no typing/right-click) | Use Extract all → Browse; Angel clicks UAC prompts |
| FL Plugin Manager can't be controlled by Claude | Angel clicks "Find installed plugins" |
| RBJ/bilinear filters crushed DREAMMIC's top octave (up to 19 dB off the designed curve) | Use MEX-EQ matched-magnitude design (`Design.h`); build HQ FIRs from the analog curve |
| Level match off by up to 6 dB on bright mics | Measure loudness K-weighted (BS.1770) with a speech-shaped test signal, not plain RMS of white noise |
| Same DSP copied into two plugins drifts apart | Share headers via a CMake include dir (`MEXCORE_DIR`) + add them to the other workflow's `paths:` |
| New choice options broke nothing in old projects | Keep gear lists append-only (projects store the index); group them in the editor instead |
| TITO: quiet hats lost under an 808 / room tail | Attack = (post−pre)²/post AND divide every band by its own typical hit level |
| TITO: hats + 808 grouped as one sound | Same fix; then NNLS per hit judged against the sound's OWN bands |
| TITO: clap counted twice (bursts 36 ms apart) | Merge look-alikes within 42 ms, keep the first time |
| TITO: a loudness filter threw away 40 % of ghost notes | Generous onset stage; let NNLS activations drop junk later |
| TITO: hit times drifted 10-24 ms in dense mixes | Steepest-rise on full OR high-passed signal; never walk back through the background |
| TITO: librosa tempo picked 108 for an 85 BPM groove | Own phase-coherence tempo scan (16th/8th/beat), octave inside 80-160 BPM, x2 / ÷2 buttons |
| TITO: engine got "--layers" with an empty value and misread the next flag | Pass optional lists as one token: `--layers=a,b` |
| TITO: sum of normalised pad sounds peaked at 2.3 | −9 dB sampler headroom (peak 0.97 on a 100 s render) |
| PIRATA: basic-pitch note starts ±5-13 ms off the attack (frame-based) | Refine on the >2 kHz transient (2 ms RMS, back from steepest rise to 25 %); full-band envelope ripples on low piano notes |
| PIRATA: 8th-triplet arp at 75 read as 112.5 | Same 3:2 trap as TITO: bass / chord changes must land on whole bars at the true tempo |
| PIRATA: slow pad (1 chord / bar) read at a random tempo | Sparse attacks → take the harmonic rhythm as 4/4 bars |
| Spectral "does this note explain energy" check on piano | Doesn't work: octave ghosts and real octaves share partials. Drums yes (TITO), piano no |
| Google Storage blocked in Claude's workspace | One-time GitHub workflow downloads + verifies the dataset and commits it |

## Parity rule (MIRRORWALL)
Python reference DSP (`mirrorwall/dsp.py`) and C++ twin (`mirrorwall_plugin/Source/Chain.h`) must stay identical:
same formulas, constants and order. Checked: max difference 3e-8 (−152 dB). Re-run the parity test after any change.
