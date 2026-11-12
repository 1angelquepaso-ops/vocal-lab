#pragma once
// DREAMMIC gear tables. Each mic is its response curve as a short list of
// filters (built from published response data and well-documented character),
// plus capsule/electronics behaviour and the mic's own switches. Each preamp is
// its nonlinearity and tonal tilt. Names are descriptive ("-style"); these are emulations.
//
// v2 rule: lists are APPEND-ONLY. Saved projects store the index, so never reorder or
// remove entries; the editor shows them in groups instead (see MIC_ORDER / PRE_ORDER).
//
// All curves are turned into filters with the MEX-EQ matched-magnitude design (no cramping
// near 20 kHz), and HQ builds its FIR straight from the analog curve.

namespace dm
{
enum class FType { HP, LP, Peak, LowShelf, HighShelf };

struct F { FType t; double f, q, g; };

// A switch on the mic body (low cut / bass roll, presence boost). n = 0 means the mic has none.
struct Switch { const char* label; F f[2]; int n; };
static constexpr Switch NO_SWITCH { "", {}, 0 };

struct MicModel
{
    const char* name;      // display name
    const char* style;     // what it's modelled on
    const char* blurb;
    F filters[8];
    int numFilters;
    double tube;           // 0..1 tube/transformer colour in the mic itself
    double patternA;       // pressure-gradient share of the native pattern (0 omni .. 1 fig-8)
    double proxScale;      // 1 = normal proximity effect; Variable-D dynamics (RE20) much less
    Switch lowCut;         // the mic's own low cut / bass roll switch
    Switch presence;       // presence boost switch
    int group;             // 0 tube condenser, 1 FET condenser, 2 dynamic / ribbon
};

// Response notes behind each curve:
//  C-800G: ~8 dB lift in the 8-10 kHz range, tube.            U47: full lows, upper-mid push, top rolls ~16 kHz.
//  U67: less upper-mid than the U47, smooth ~9 kHz lift.       U87: brighter/open, broad 8-12 kHz presence, FET.
//  M149: modern tube, open 5-12 kHz rise.                      C12: CK12 capsule, ~12 kHz resonance (wider than 251).
//  251: rolls off below ~100 Hz, mesh peak ~5 kHz, narrower ~12 kHz resonance, smoothed top.
//  Manley Ref Cardioid: forward 4-5 kHz + 10 kHz presence.     SM7A: smooth dynamic, thick lows, top rolls off.
//  MA-50: transformerless FET, small lift ~5 kHz.
// v2 additions:
//  SM7B: near-flat dynamic, gentle 2-6 kHz rise, top rolls ~10 kHz; bass-roll + presence switches.
//  TLM 103: transformerless FET, flat to ~5 kHz then broad ~+4 dB rise around 8-12 kHz.
//  C414 XLII: broad presence rise ~+3.5 dB from 3 kHz up; multi-pattern; 80 Hz low cut.
//  RE20: very flat broadcast dynamic, Variable-D = almost no proximity boom; bass-roll switch.
//  MD 421: dip ~1.5 kHz, strong ~5 kHz presence, rolls off up top; 5-position bass roll.
//  SM58: built-in low roll-off, +5 dB presence ~5 kHz, top falls above 10 kHz.
//  NT1-A: bright home-studio condenser, ~+4.5 dB around 12 kHz.
//  U47 fet: smooth, tight lows, mild upper-mid, rolls above 12 kHz; 140 Hz low cut.
//  R-121: figure-8 ribbon, smooth flat mids, highs fall gently above ~8 kHz, big proximity.
static const MicModel MICS[] = {
    { "SN-800 Tube", "Sony C-800G style", "Glossy, airy modern rap / R&B sheen",
      { { FType::HP, 30, 0.7, 0 }, { FType::LowShelf, 120, 0.7, 1.0 }, { FType::Peak, 3000, 1.0, -1.0 },
        { FType::Peak, 9000, 0.9, 7.0 }, { FType::Peak, 14000, 1.2, 2.0 }, { FType::LP, 19000, 0.7, 0 } }, 6, 0.30, 0.5,
      1.0, NO_SWITCH, NO_SWITCH, 0 },
    { "N47 Tube", "Neumann U47 style", "Thick, warm, the classic",
      { { FType::HP, 35, 0.7, 0 }, { FType::LowShelf, 150, 0.7, 2.0 }, { FType::Peak, 250, 0.8, 1.0 },
        { FType::Peak, 2500, 0.8, 2.0 }, { FType::Peak, 6000, 1.0, 2.0 }, { FType::HighShelf, 12000, 0.7, -3.0 },
        { FType::LP, 16500, 0.7, 0 } }, 7, 0.50, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 0 },
    { "N67 Tube", "Neumann U67 style", "Warm, smooth mids, clearer than the 47",
      { { FType::HP, 40, 0.7, 0 }, { FType::LowShelf, 160, 0.7, 1.5 }, { FType::Peak, 3000, 1.0, -1.0 },
        { FType::Peak, 9000, 1.0, 2.5 }, { FType::HighShelf, 13000, 0.7, -2.0 }, { FType::LP, 17500, 0.7, 0 } }, 6, 0.40, 0.5,
      1.0, { "Low cut", { { FType::HP, 80, 0.6, 0 } }, 1 }, NO_SWITCH, 0 },
    { "N87", "Neumann U87 style", "Balanced, present studio standard",
      { { FType::HP, 30, 0.7, 0 }, { FType::Peak, 300, 1.0, -0.5 }, { FType::Peak, 5000, 1.0, 1.0 },
        { FType::Peak, 10000, 1.2, 3.5 }, { FType::HighShelf, 15000, 0.7, -1.0 } }, 5, 0.15, 0.5,
      1.0, { "Low cut 80", { { FType::HP, 80, 0.6, 0 } }, 1 }, NO_SWITCH, 1 },
    { "N149 Tube", "Neumann M149 style", "Modern tube: big, open, detailed",
      { { FType::HP, 25, 0.7, 0 }, { FType::Peak, 200, 0.8, 0.5 }, { FType::Peak, 6000, 0.8, 1.5 },
        { FType::Peak, 11000, 1.0, 3.0 }, { FType::HighShelf, 15000, 0.7, 1.0 } }, 5, 0.30, 0.5,
      1.0, { "Low cut 80", { { FType::HP, 80, 0.6, 0 } }, 1 }, NO_SWITCH, 0 },
    { "A12 Tube", "AKG C12 style", "Silky, open top with that 12k shimmer",
      { { FType::HP, 35, 0.7, 0 }, { FType::LowShelf, 150, 0.7, -1.0 }, { FType::Peak, 6000, 1.0, 1.5 },
        { FType::Peak, 11500, 1.5, 6.0 }, { FType::HighShelf, 16000, 0.7, -1.0 } }, 5, 0.35, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 0 },
    { "T251 Tube", "Telefunken ELA M 251 style", "Rich, sweet, detailed top; lean lows",
      { { FType::HP, 100, 0.6, 0 }, { FType::LowShelf, 200, 0.7, 1.0 }, { FType::Peak, 5000, 1.0, 3.0 },
        { FType::Peak, 12000, 2.0, 3.5 }, { FType::HighShelf, 16000, 0.7, -2.0 } }, 5, 0.40, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 0 },
    { "M-Ref Tube", "Manley Reference Cardioid style", "Bright, forward, aggressive presence",
      { { FType::HP, 30, 0.7, 0 }, { FType::LowShelf, 150, 0.7, 1.0 }, { FType::Peak, 4500, 1.0, 3.0 },
        { FType::Peak, 10000, 1.0, 4.0 }, { FType::HighShelf, 14000, 0.7, 1.0 } }, 5, 0.35, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 0 },
    { "S7 Classic", "Shure SM7A style", "Dark, thick, tight dynamic; no room",
      { { FType::HP, 60, 0.7, 0 }, { FType::LowShelf, 200, 0.7, 1.5 }, { FType::Peak, 4500, 0.9, 2.0 },
        { FType::HighShelf, 9000, 0.7, -6.0 }, { FType::LP, 14000, 0.7, 0 } }, 5, 0.0, 0.5,
      1.0, { "Bass roll", { { FType::LowShelf, 220, 0.6, -5.0 } }, 1 },
           { "Presence", { { FType::Peak, 5000, 0.9, 4.0 } }, 1 }, 2 },
    { "MJ-50", "Mojave MA-50 style", "Your old mic: clear, present FET",
      { { FType::HP, 30, 0.7, 0 }, { FType::Peak, 5000, 0.8, 2.5 }, { FType::Peak, 10000, 1.0, 1.5 },
        { FType::HighShelf, 14000, 0.7, -1.0 } }, 4, 0.0, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 1 },
    // ---- v2 ----
    { "S7B Dynamic", "Shure SM7B style", "The modern rap/podcast dynamic: smooth, close, forgiving",
      { { FType::HP, 50, 0.7, 0 }, { FType::LowShelf, 200, 0.7, 1.0 }, { FType::Peak, 3500, 0.7, 1.5 },
        { FType::HighShelf, 9000, 0.7, -3.0 }, { FType::LP, 16000, 0.7, 0 } }, 5, 0.0, 0.5,
      1.0, { "Bass roll", { { FType::LowShelf, 250, 0.6, -6.0 } }, 1 },
           { "Presence", { { FType::Peak, 5000, 0.9, 4.5 } }, 1 }, 2 },
    { "N103", "Neumann TLM 103 style", "Clean modern FET with a bright, open top",
      { { FType::HP, 25, 0.7, 0 }, { FType::Peak, 5000, 1.0, 0.8 }, { FType::Peak, 9500, 0.6, 4.0 },
        { FType::HighShelf, 16000, 0.7, -2.0 } }, 4, 0.0, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 1 },
    { "A414", "AKG C414 XLII style", "Detailed, airy presence; multi-pattern",
      { { FType::HP, 30, 0.7, 0 }, { FType::Peak, 7000, 0.5, 3.5 }, { FType::HighShelf, 15000, 0.7, -1.0 } }, 3, 0.0, 0.5,
      1.0, { "Low cut 80", { { FType::HP, 80, 0.6, 0 } }, 1 }, NO_SWITCH, 1 },
    { "E-20 Dynamic", "Electro-Voice RE20 style", "Flat broadcast voice; no boom when you get close",
      { { FType::HP, 45, 0.7, 0 }, { FType::Peak, 4500, 0.8, 1.0 }, { FType::HighShelf, 11000, 0.7, -3.0 },
        { FType::LP, 18000, 0.7, 0 } }, 4, 0.0, 0.5,
      0.25, { "Bass roll", { { FType::LowShelf, 180, 0.6, -5.0 } }, 1 }, NO_SWITCH, 2 },
    { "S-421 Dynamic", "Sennheiser MD 421 style", "Punchy, mid-forward, cuts through",
      { { FType::HP, 40, 0.7, 0 }, { FType::Peak, 1500, 1.0, -1.5 }, { FType::Peak, 5000, 1.0, 4.0 },
        { FType::HighShelf, 13000, 0.7, -4.0 }, { FType::LP, 17000, 0.7, 0 } }, 5, 0.0, 0.5,
      1.0, { "Bass roll", { { FType::LowShelf, 200, 0.6, -6.0 } }, 1 }, NO_SWITCH, 2 },
    { "S58 Live", "Shure SM58 style", "The stage mic: mid push, rolled lows, demo grit",
      { { FType::HP, 80, 0.6, 0 }, { FType::Peak, 2000, 0.7, 1.0 }, { FType::Peak, 5000, 1.0, 5.0 },
        { FType::HighShelf, 10000, 0.7, -5.0 }, { FType::LP, 15000, 0.7, 0 } }, 5, 0.0, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 2 },
    { "R-NT1", "Rode NT1-A style", "Crisp home-studio condenser, bright 12k",
      { { FType::HP, 25, 0.7, 0 }, { FType::Peak, 4000, 1.5, -1.0 }, { FType::Peak, 12000, 0.8, 4.5 },
        { FType::HighShelf, 17000, 0.7, -1.0 } }, 4, 0.0, 0.5, 1.0, NO_SWITCH, NO_SWITCH, 1 },
    { "N47 FET", "Neumann U47 fet style", "Smooth and tight, sits up front",
      { { FType::HP, 30, 0.7, 0 }, { FType::LowShelf, 120, 0.7, 1.0 }, { FType::Peak, 4500, 0.8, 1.5 },
        { FType::HighShelf, 12000, 0.7, -2.0 }, { FType::LP, 17000, 0.7, 0 } }, 5, 0.10, 0.5,
      1.0, { "Low cut 140", { { FType::HP, 140, 0.6, 0 } }, 1 }, NO_SWITCH, 1 },
    { "R-121 Ribbon", "Royer R-121 style", "Smooth dark ribbon, tames harsh voices",
      { { FType::HP, 30, 0.7, 0 }, { FType::HighShelf, 8000, 0.6, -4.0 }, { FType::LP, 15000, 0.6, 0 } }, 3, 0.15, 1.0,
      1.0, NO_SWITCH, NO_SWITCH, 2 },
};
constexpr int NUM_MICS = (int) (sizeof (MICS) / sizeof (MICS[0]));
constexpr int MJ50_INDEX = 9;
static const char* MIC_GROUPS[] = { "TUBE CONDENSERS", "FET CONDENSERS", "DYNAMICS + RIBBON" };

// Source mics: what to undo before the target is applied.
//  0 = Flat / unknown, 1 = Samson C01U Pro (measured), 2 = Mojave MA-50, 3.. = every other mic model above
//  (v2: so a take recorded on, say, an SM7B or a TLM 103 can be "un-mic'd" too).
constexpr int NUM_SOURCES = 3 + NUM_MICS - 1;
inline int sourceMicIndex (int source)          // which MICS[] entry this source undoes (-1 = none)
{
    if (source <= 0) return -1;
    if (source <= 2) return MJ50_INDEX;           // C01U is corrected to MA-50 first, then the MA-50 is undone
    const int k = source - 3;                     // all mics except MJ-50, in table order
    return k < MJ50_INDEX ? k : k + 1;
}
// C01U Pro -> MA-50 correction measured from your takes (MIRRORWALL tone-EQ band layout)
static const double C01U_TO_MA50[12] = { -0.0, 0.02, -0.06, 0.2, -0.37, 0.78, 0.84, -4.68, -3.13, -4.45, -8.66, 1.53 };
static const F EQ12[12] = {
    { FType::LowShelf, 100, 0.707, 0 }, { FType::Peak, 160, 1.4, 0 }, { FType::Peak, 250, 1.4, 0 },
    { FType::Peak, 400, 1.4, 0 }, { FType::Peak, 630, 1.4, 0 }, { FType::Peak, 1000, 1.4, 0 },
    { FType::Peak, 1600, 1.4, 0 }, { FType::Peak, 2500, 1.4, 0 }, { FType::Peak, 4000, 1.4, 0 },
    { FType::Peak, 6300, 1.4, 0 }, { FType::Peak, 9000, 1.4, 0 }, { FType::HighShelf, 12500, 0.707, 0 } };
inline double sourcePatternA (int source)
{
    if (source == 1) return 0.63;                 // C01U Pro is supercardioid
    const int m = sourceMicIndex (source);
    return m < 0 ? 0.5 : MICS[m].patternA;
}
inline double sourceProxScale (int source)
{
    const int m = sourceMicIndex (source);
    return (m < 0 || source == 1) ? 1.0 : MICS[m].proxScale;
}

struct PreModel
{
    const char* name;
    const char* style;
    const char* blurb;
    double bias;           // asymmetry -> even harmonics (tubes)
    double knee;           // 0 soft (tube) .. 1 harder (op-amp)
    double xfmr;           // transformer low-frequency saturation amount
    double maxDriveDb;     // drive range of the Warmth knob
    F tilt[4];
    int numTilt;
    int group;             // 0 tube, 1 solid state / console
};

static const PreModel PRES[] = {
    { "AV-737 Tube", "Avalon VT-737sp style", "Class-A tube: smooth, wide, expensive top",
      0.20, 0.0, 0.10, 18, { { FType::LowShelf, 80, 0.7, 0.5 }, { FType::HighShelf, 16000, 0.7, 1.0 } }, 2, 0 },
    { "N-73", "Neve 1073 style", "Thick transformer mids and weight",
      0.05, 0.3, 0.60, 20, { { FType::LowShelf, 120, 0.7, 1.2 }, { FType::Peak, 3200, 0.8, 0.5 },
                             { FType::LP, 22000, 0.6, 0 } }, 3, 1 },
    { "U-610 Tube", "Universal Audio 610 style", "Vintage tube warmth, soft top",
      0.25, 0.0, 0.40, 20, { { FType::LowShelf, 100, 0.7, 1.0 }, { FType::HighShelf, 10000, 0.7, -1.5 } }, 2, 0 },
    { "A-512", "API 512c style", "Punchy, forward, aggressive mids",
      0.03, 0.7, 0.35, 20, { { FType::Peak, 100, 0.8, 0.8 }, { FType::Peak, 2000, 0.7, 1.0 },
                             { FType::HighShelf, 12000, 0.7, 0.5 } }, 3, 1 },
    { "MV-Box Tube", "Manley VOXBOX style", "Tube vocal channel: rich and polished",
      0.18, 0.0, 0.30, 18, { { FType::Peak, 250, 0.9, 0.7 }, { FType::HighShelf, 12000, 0.7, 1.0 } }, 2, 0 },
    { "S-4K E", "SSL 4000 E style", "Clean British console punch",
      0.02, 0.5, 0.10, 16, { { FType::LowShelf, 80, 0.7, -0.3 }, { FType::HighShelf, 10000, 0.7, 0.8 } }, 2, 1 },
    // ---- v2 ----
    { "TF-V76 Tube", "Telefunken V76 style", "Dark, thick German tube (REDD-era Abbey Road)",
      0.22, 0.0, 0.55, 20, { { FType::LowShelf, 100, 0.7, 1.2 }, { FType::HighShelf, 9000, 0.7, -2.0 } }, 2, 0 },
    { "EMI-TG", "EMI TG12345 style", "Crunchy Abbey Road console grit, honky mids",
      0.06, 0.55, 0.65, 22, { { FType::Peak, 120, 0.7, 1.0 }, { FType::Peak, 1500, 0.6, 1.2 },
                              { FType::HighShelf, 12000, 0.7, -1.0 } }, 3, 1 },
    { "H-69", "Helios 69 style", "70s British console bite, forward upper mids",
      0.04, 0.45, 0.50, 20, { { FType::LowShelf, 90, 0.7, 0.8 }, { FType::Peak, 2800, 0.7, 1.5 },
                              { FType::HighShelf, 11000, 0.7, 0.5 } }, 3, 1 },
    { "FC-ISA", "Focusrite ISA 110 style", "Sweet, open class-A, gentle air",
      0.03, 0.35, 0.25, 16, { { FType::Peak, 300, 0.8, -0.3 }, { FType::HighShelf, 14000, 0.7, 0.8 } }, 2, 1 },
    { "HV-Clean", "Millennia HV-3 style", "Ultra-clean and true: no colour, just gain",
      0.0, 0.9, 0.0, 10, {}, 0, 1 },
    { "RN-Silk", "Rupert Neve Portico (Silk) style", "Transformer 'Silk' sheen and soft weight",
      0.08, 0.25, 0.70, 18, { { FType::LowShelf, 100, 0.7, 0.6 }, { FType::HighShelf, 6000, 0.5, 1.0 } }, 2, 1 },
    { "TT-MP1 Tube", "Tube-Tech MP1A style", "Smooth Danish tube, silky and wide",
      0.18, 0.0, 0.30, 18, { { FType::LowShelf, 80, 0.7, 0.6 }, { FType::HighShelf, 15000, 0.7, 0.6 } }, 2, 0 },
};
constexpr int NUM_PRES = (int) (sizeof (PRES) / sizeof (PRES[0]));
static const char* PRE_GROUPS[] = { "TUBE", "SOLID STATE / CONSOLE" };

// Tracking compressor after the preamp (v2). Models + measured offsets are MIRRORWALL 2.0's
// (fitted to clean CLA-2A / CLA-76 / SSL G / RVox captures, Nov 2026; shared code in mirrorwall_plugin/Source/Live.h).
static const char* COMP_NAMES[] = { "Off", "Opto 2A", "FET 76", "VCA SSL G", "Leveller RVox" };
static const char* COMP_TIPS[] = {
    "No compressor",
    "LA-2A style opto (fitted to our CLA-2A capture): smooth, slow, program-dependent release",
    "1176 style FET (fitted to our CLA-76 capture): fast, grabby, rap-lead punch",
    "SSL G bus/channel VCA (fitted to our SSL G capture): very soft wide knee, glue",
    "RVox style leveller (fitted to our RVox capture): evens the line, automatic make-up" };
constexpr int NUM_COMPS = 5;
} // namespace dm
