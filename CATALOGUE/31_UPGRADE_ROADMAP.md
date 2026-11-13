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
| PIRATA | ~~Simple = melody line, Rich = melody + lush chords, grid phase fix~~ (v2 done); chord-name/bass accuracy on dense comping; .m4a input; optional stem separation; edit chords in the roll; swing/humanise export; skin |
| MIRRORWALL | 2.0 live (above); capture-based stages (RVox, C-type comps, Avalon); paired-mode fitting UI; wider harmony stacks; preset share/export; skin |
| DREAMMIC | ~~MEX-EQ filters, more mics/preamps, mic switches, presets, comp/de-ess, level match~~ (v2 done); capture-based mic + preamp models; skin |
| TITO PUENTE | link duplicate pads / stronger merge; better names for hats/toms; drum separation from full mixes; tempo-map MIDI for live drift; skin |
