# MEX-EQ — SSL G-series character with FabFilter Pro-Q control (real time)

**What:** 36-band EQ. Every band can be **G** (curves measured from our Waves SSL G-EQ / G-Channel) or **Clean**
(exact textbook / FL Parametric EQ 2 shapes). Pro-Q style graph, Parametric EQ 2 right-click menu, dynamic EQ,
LEARN (auto problem-frequency finder), noise-free Drive, LIVE / HQ / AUTO quality.
**Code:** `vocal-lab/mexeq_plugin/` — `Source/Design.h` (all filter math), `Source/GData.h` (generated measured data),
`Source/Engine.h` (real-time core), `Source/Learn.h`, `model/` (Python that built GData.h from the captures).
**Release tag:** `mexeq-latest` → `MEX-EQ-Windows.zip`. Test tool: `me_render` (offline render, any parameter, `learn=1`).
**Captures:** `Documents\MEX-EQ\captures\` on Angel's PC (E1–E8 Edison takes + 00–06 sweep renders).

## Decisions (Angel, 9 Nov 2026)
| Topic | Decision |
|---|---|
| SSL model | G-series (G-EQ = G-Channel EQ, measured identical). E-series left out (not owned). |
| Gain range | G bands ±21.5 dB (the real SSL range). Clean bands ±24 dB. |
| ANALOG switch | Not measured / not cloned: it adds a constant hiss even in silence. Drive is noise-free instead. |
| Q testing | Q only matters together with frequency + gain, so it was always measured at several gains. |
| ÷3 / ×3 buttons | Kept: they change the tone (shape), not just the range → "SSL shape" menu (Auto, LMF, LMF ÷3, HMF, HMF ×3). Auto = default. |
| Menu | Right-click a point = Parametric EQ 2 layout and names: Reset, Type, Order (Gentle 8…2…Steep 8, + Steep 12/16/Brickwall), Key (note → octave). Plus Character, SSL shape, Stereo, Dynamic, Delete. |
| Look | Own Pro-Q style dark skin (not the family skin). |
| Latency | LIVE / HQ button (+ AUTO: LIVE while playing, HQ when FL exports). |
| Dynamic EQ + LEARN | In build 1. |

## Signal flow
input → [36 bands, in order; each Stereo / Left / Right / Mid / Side; static or dynamic] → Drive → Output.
- **LIVE:** every band = analog sections → digital biquads at 1×, zero latency.
- **HQ Natural:** same at 2× (polyphase IIR oversampling), 4 samples latency @44.1k.
- **HQ Linear Low/Medium/High/Max:** static bands → two linear-phase FIRs (L/R pair, then M/S pair) built from the exact
  analog curve; FIR 4096/8192/16384/32768 taps (×2 above 50 kHz). Latency = FIR length (two FIRs of N/2).
  Dynamic bands stay natural-phase after the FIR (no extra latency).
- **AUTO:** reports the HQ-Natural latency all the time; while playing it runs LIVE + a matching delay, on export (FL
  non-realtime) it runs HQ Natural → no timing jump between playback and bounce.

## How the curves are made (Design.h)
1. Each band → list of **analog** sections H(s) (bell, shelf, HP/LP stages, measured G sections).
2. Each section → digital biquad with a **hybrid matched design**: (a) Vicanek 3-point magnitude match, (b) least-squares
   numerator over 20 Hz–Nyquist with poles clamped below Nyquist; both with **inversion** (if the zeros are sharper than the
   poles — cuts — or the poles sit above Nyquist, design the inverse and flip it). Keep whichever is closer to the analog
   curve. Result on 1856 test sections: median 0.035 dB, 95 % < 0.54 dB, worst 1.6 dB (±21.5 dB shelves / Q-4 bells at 15–21 kHz).
   No cramping near 20 kHz, zero latency.

## G (SSL) model — what we measured and how it is used
**Method:** looping multisine (all frequencies, 8192-sample period) → plugin → Edison recording while Claude steps the knobs
and holds ~1.2 s per step; every 8192-sample slice is one exact curve; steps found automatically. ~150 settings in 3 takes.

| Part | Measured (Waves G-EQ / G-Channel, ANALOG off) | MEX-EQ rule |
|---|---|---|
| Bell shape | = analog bell (LMF 0.04 dB, HMF ≤ 0.48 dB) | analog bell |
| Bell gain | ±21.5 dB real range (knob says ±20) | clamp ±21.5 |
| Centre drift | none below ~9.5 dB boost / 7.6 dB cut, then rises with gain; at 21.5 dB: +3.4 % @203 Hz, +13.3 % @465, +30.9 % @1.6 k, +20.7 % @3 k, +35.9 % @4.8 k | `f = f_user × (1 + D(f) × h(g))` (D, h tables in GData.h) |
| Gain law h(g) boost | 0 to 9.5 dB, .14 @12.3, .34 @14.5, .50 @17, .68 @18.5, .82 @20.1, 1 @21.5 | table |
| Gain law h(g) cut | 0 to 7.6 dB, .31 @10.3, .62 @14.1, .93 @21.5 | table |
| Q vs gain | ×0.94 at +7, ×1.18 at +21.5; cuts ×1.08 at −7.6, ×1.28 at −21.5 (same at every Q/frequency) | `Q = Q_user × QF(g)` |
| Q knob | 0.19 (wide) … 0.69 (narrow) at low gain; centre does not move | user Q = Q at low gain |
| LMF ÷3 | range ~53–404 Hz; Q × 1.20 (low end) … 1.06 (top) | SSL shape LMF ÷3 |
| HMF ×3 | range ~2–16 kHz; Q × 1.0 up to 3.6 k → 0.745 @16 k (wider up top) | SSL shape HMF ×3 |
| HF shelf | broad, dip −0.3…−0.7 dB below the corner; half-gain corner 353 Hz…2.7 kHz (low gain), moves up with gain (1.6 → 4.2 kHz at default) | dense table |
| LF shelf | dip −0.1…−0.43 dB above the corner; half-gain corner 125…783 Hz; moves down at high gain (208 → 143 Hz) | dense table |
| Shelf cut | close to the mirror of boost but not exact (0.2–0.8 dB) | own cut data |
| HP (FLTR IN) | exact 3rd-order Butterworth, 18 dB/oct, 25–320 Hz, no bump (0.006 dB fit) | HP1 × HP2(Q=1) |
| LP (G-Channel) | 2nd-order LP (Q .64–.72, moves) × fixed resonant pair (zero ~17–20 kHz Q 1.55–2.2 / pole ~27 kHz Q .61), 3.1–15 kHz | table vs −3 dB freq (0.001 dB fit) |
| Band interaction | none (LMF+HMF together = sum within 0.03 dB) | bands independent |
| G-EQ vs G-Channel | identical EQ (≤ 0.01 dB) | one G mode |

**Shelf tables:** model per node = analog shelf (f, g, Q) × 2 small correction bells (±6 dB max). Grid 73 positions × 87 gains.
Built by normalising every capture to (half-gain corner, shape), blending between the two real position sweeps (low / high
gain) with the default-position gain sweep as guide, fitting with continuation (strongest gains first, neighbour-anchored),
then repairing 2 LF seams. Table vs every measured curve: **LF ≤ 0.14 dB, HF ≤ 0.37 dB (median 0.02–0.03)**; smooth between
nodes (≤ 0.17 dB HF, ≤ 0.3 dB LF). G-shelf frequency = the half-gain corner at low gain (outside the measured range the
nearest shape is frequency-scaled). Above 18 dB (LF) / 19.4 dB (HF) up to 21.5 = extrapolated (no data).

## Clean (FL Parametric EQ 2) orders — measured (E8)
- **Gentle N** (pass filters): N/2 identical stages, Q × 1.215 / 1.296 / 1.338 (G4/6/8). On Peaking and Shelves Gentle = "2".
- **Steep N** pass: N/2 stages with Q multipliers S4 [0.91, 2.41], S6 [0.90, 1.40, 3.42], S8 [0.88, 1.36, 1.57, 4.77].
- **Steep shelves / peaks:** Butterworth-type (Holters–Zölzer) prototype; shelf damping scale 0.933 (S4) / 0.95 (S8); peak Q ×1.68 / 1.77 / 1.80 (S4/6/8), damping .765 / .641 / .724.
- MEX-EQ vs real Parametric EQ 2: **0.01–0.19 dB** on all 14 measured shapes. Steep 12 / 16 / Brickwall = Butterworth 12 / 16 / 32.

## Accuracy — MEX-EQ render vs the real Waves SSL (same settings, multisine)
| Test | LIVE | HQ |
|---|---|---|
| LMF bell +7.25 / +21.5 / −21.35 | 0.03 / 0.03 / 0.03 dB | 0.03 / 0.03 / 0.11 |
| LMF 1.6 k +21.6 | 0.29 | 0.19 |
| HMF +20.9 @3 k / +20.7 @4.8 k | 0.54 / 0.73 | 0.62 / 0.90 (Q read at low gain; real SSL skirt ≠ pure analog bell) |
| LF shelf +18 / −18.2 / +11.7 high pos | 0.11 / 0.14 / 0.06 | same |
| HF shelf +19.4 / +3.4 / −19.9 / +19.4 low pos | 0.75 / 0.01 / 0.79 / 0.12 | 0.36 / 0.01 / 0.38 / 0.08 |
| HP 18 dB/oct 190 Hz | 0.08 | 0.08 |
| LP 3.7 kHz | 0.02 | 0.02 |
Mode match: HQ Natural vs LIVE 0.05 dB; Linear High/Max 0.07 dB; Linear Low 0.65 dB (short FIR can't resolve a 40 Hz cut — same as Pro-Q).
Linear with no bands: perfectly flat. CPU: LIVE ~530× real time with 4 bands, Linear Max ~35×.

## Dynamic EQ
Detector = band-pass at the band frequency/Q on the band input (or the external sidechain bus), attack/release ballistics,
level in dBFS (sine-referenced). Gain change = Range × clamp((level − threshold)/10 dB, 0..1). Threshold = fixed (dBFS) or
**Auto = the band's own 2.5 s average + offset** ("ABOVE AVG" knob, −24…+24 dB). Test: range −10, thr −20 → quiet part
0.00 dB, loud part −10.00 dB.

## LEARN
Press, play 10–30 s, press again (auto-stops at 30 s). 4096-pt frames, 1/24-octave bands 40 Hz–18 kHz, silent frames skipped.
- Resonance score = long-term level − its 1-octave neighbourhood. → band with Auto offset **−10 dB** (held down while the
  voice is there), attack 10 ms, release 150 ms.
- Ess / harshness score = (90th percentile − median) − the typical spread of the whole spectrum. → Auto offset **+3 dB**
  (dips only on bursts), attack 2 ms, release 60 ms.
- Up to 6 bands, ≥ 1/3 octave apart, Q from the problem width (1.5–16), range −1.2 × score (3–12 dB), Clean Peaking,
  marked "learned" (dashed ring); STRENGTH knob scales all learned ranges (0–200 %). Re-learning replaces them.
- Test (noise + 2.5 kHz ringing resonance + 8 kHz ess bursts): found 2.52 kHz (Q 11.5, −8 dB) and 8.0 kHz (Q 2.9, −6.3 dB);
  resonance band −4.9 dB, ess bursts −4.2 dB, rest of the spectrum 0.0 dB.

## Drive
Console-style soft saturation (tanh with a small offset → mostly 3rd + some 2nd harmonic), first-order anti-aliasing (ADAA),
level-compensated. **Digital silence in = 0.0 out** (no hiss). 220 Hz −12 dBFS: Drive 3 → 0.6 % THD, 6 → 2.1 %, 10 → 7.7 %;
level within −0.1 / −0.4 / −1.7 dB.

## Interface
Graph 10 Hz–30 kHz, ±24 dB; analyzer post (or pre + post), +4.5 dB/oct tilt. Double-click = new band (G, Peaking; < 30 Hz →
High pass, > 17 kHz → Low pass). Drag = freq + gain (Band pass/stop: drag up/down = Q), Shift = fine, wheel = Q (Clean pass
filters: wheel = Order). Gold ring + "G" = SSL band; "C" = G set but the SSL has no such filter (runs Clean). Dynamic bands show
their range bar and a live ring. Band panel: Type, Order, Stereo, Character, SSL shape, Delete, Freq/Gain/Q, Dynamic, Auto thr,
Ext SC, Range, Threshold/Above avg, Attack, Release. Top: LIVE/HQ/AUTO, HQ phase, LEARN + STRENGTH, analyzer, DRIVE, OUTPUT.

## Limits / next
- E-series not measured (not owned) — 10 min with the same Edison method if the E-Channel is ever installed.
- Shelf gains above 18 / 19.4 dB are extrapolated; LMF ÷3 and HMF ×3 measured at one gain only (drift assumed same law).
- Analyzer is mono (L+R); FIR mode treats Stereo/L/R bands first, then Mid/Side bands.
- Next: preset save/load, band solo/listen, per-band bypass in UI, auto-gain, A/B, resizable fonts, measure ANALOG harmonics
  (with the hiss removed) if Angel wants them back, E mode.
