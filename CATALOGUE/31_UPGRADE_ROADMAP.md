# Upgrade roadmap

## Order (agreed)
1. **MIRRORWALL 2.0 (live)** — reuse DREAMMIC's real-time base: real-time pitch correction (pitch track + shift with a few
   ms delay in REC mode; offline-quality tuning in HQ/bounce), live processing on the insert, REC/LIVE vs HQ switch,
   input gain knob + meter with green zone (+ Auto that sets the knob), fixed output gain stored per preset.
   Remove "Key from beat"? (undecided — currently kept).
2. **Captures** (30_SIGNAL_CAPTURE_PROTOCOL.md): Waves vocal plugins → MIRRORWALL stages; paired chains; studio gear → DREAMMIC.
3. **Skins** (32_SKIN_DESIGN.md): one family look for all plugins.

## Per plugin
| Plugin | Next upgrades |
|---|---|
| Clean Vocal | skin; de-breath / de-click; learn-noise |
| Harmonizer | call & response + choir in UI; voice-clone voices instead of pitch-shifted; skin |
| PIRATA | .m4a input; optional stem separation; edit chords in the roll; swing/humanise export; skin |
| MIRRORWALL | 2.0 live (above); capture-based stages (RVox, C-type comps, Avalon); paired-mode fitting UI; wider harmony stacks; preset share/export; skin |
| DREAMMIC | capture-based mic + preamp models; SM7A presence/bass switches; save/load presets; skin |
| MEX-EQ | presets; band solo/listen; bypass per band in UI; auto-gain; A/B; E-series mode (needs E-Channel); measure ANALOG harmonics minus hiss (optional) |
