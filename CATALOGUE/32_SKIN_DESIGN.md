# Skins / look

## Now (each plugin has its own dark theme)
| Plugin | Background | Accent | Notes |
|---|---|---|---|
| Harmonizer | #15151B | gold #E0A84A | waveform lanes, gold drag grips |
| PIRATA | #111116 | gold #E0A84A, bass #6FA8DC, melody #E07A9A | piano roll + chord strip, drag tiles |
| MIRRORWALL | #0F1014 | ice blue #9FD3E6, gold grips #E0B25A | raw/processed lanes, knob row |
| DREAMMIC | #0D0E12 | brass #D9B36C, preamp blue #8FC9DE | mic cards, response curve, meters |
| MEX-EQ | #111216, graph #0C0D10 | gold #E0B25A (G bands), 12-colour band palette | Pro-Q style graph + analyzer, own look (Angel's choice, not the family skin) |
| Clean Vocal | own panel (nih-plug egui) | — | 4 controls |
All use JUCE default fonts, rotary sliders, rounded panels.

## Upgrade plan
- One family design: shared colour tokens, one display font, one knob style (custom LookAndFeel shared by every plugin).
- Hardware-style faceplates per gear in DREAMMIC (cards that look like the mic / preamp, without logos).
- Animated meters + curve, hover help text, resizable layouts that scale fonts.
- Optional: a small logo mark per plugin; light/dark toggle.
- Build as a shared `vocal-lab-ui/` LookAndFeel module so every plugin updates together.
