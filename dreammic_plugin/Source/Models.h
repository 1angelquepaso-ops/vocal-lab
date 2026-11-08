#pragma once
// DREAMMIC gear tables. Each mic is its response curve as a short list of
// filters (built from published response data and well-documented character),
// plus capsule/electronics behaviour. Each preamp is its nonlinearity and
// tonal tilt. Names are descriptive ("-style"); these are emulations.

namespace dm
{
enum class FType { HP, LP, Peak, LowShelf, HighShelf };

struct F { FType t; double f, q, g; };

struct MicModel
{
    const char* name;      // display name
    const char* style;     // what it's modelled on
    const char* blurb;
    F filters[8];
    int numFilters;
    double tube;           // 0..1 tube/transformer colour in the mic itself
    double patternA;       // pressure-gradient share of the native pattern (0 omni .. 1 fig-8)
};

// Response notes behind each curve:
//  C-800G: ~8 dB lift in the 8-10 kHz range, tube.            U47: full lows, upper-mid push, top rolls ~16 kHz.
//  U67: less upper-mid than the U47, smooth ~9 kHz lift.       U87: brighter/open, broad 8-12 kHz presence, FET.
//  M149: modern tube, open 5-12 kHz rise.                      C12: CK12 capsule, ~12 kHz resonance (wider than 251).
//  251: rolls off below ~100 Hz, mesh peak ~5 kHz, narrower ~12 kHz resonance, smoothed top.
//  Manley Ref Cardioid: forward 4-5 kHz + 10 kHz presence.     SM7A: smooth dynamic, thick lows, top rolls off.
//  MA-50: transformerless FET, small lift ~5 kHz.
static const MicModel MICS[] = {
    { "SN-800 Tube", "Sony C-800G style", "Glossy, airy modern rap / R&B sheen",
      { { FType::HP, 30, 0.7, 0 }, { FType::LowShelf, 120, 0.7, 1.0 }, { FType::Peak, 3000, 1.0, -1.0 },
        { FType::Peak, 9000, 0.9, 7.0 }, { FType::Peak, 14000, 1.2, 2.0 }, { FType::LP, 19000, 0.7, 0 } }, 6, 0.30, 0.5 },
    { "N47 Tube", "Neumann U47 style", "Thick, warm, the classic",
      { { FType::HP, 35, 0.7, 0 }, { FType::LowShelf, 150, 0.7, 2.0 }, { FType::Peak, 250, 0.8, 1.0 },
        { FType::Peak, 2500, 0.8, 2.0 }, { FType::Peak, 6000, 1.0, 2.0 }, { FType::HighShelf, 12000, 0.7, -3.0 },
        { FType::LP, 16500, 0.7, 0 } }, 7, 0.50, 0.5 },
    { "N67 Tube", "Neumann U67 style", "Warm, smooth mids, clearer than the 47",
      { { FType::HP, 40, 0.7, 0 }, { FType::LowShelf, 160, 0.7, 1.5 }, { FType::Peak, 3000, 1.0, -1.0 },
        { FType::Peak, 9000, 1.0, 2.5 }, { FType::HighShelf, 13000, 0.7, -2.0 }, { FType::LP, 17500, 0.7, 0 } }, 6, 0.40, 0.5 },
    { "N87", "Neumann U87 style", "Balanced, present studio standard",
      { { FType::HP, 30, 0.7, 0 }, { FType::Peak, 300, 1.0, -0.5 }, { FType::Peak, 5000, 1.0, 1.0 },
        { FType::Peak, 10000, 1.2, 3.5 }, { FType::HighShelf, 15000, 0.7, -1.0 } }, 5, 0.15, 0.5 },
    { "N149 Tube", "Neumann M149 style", "Modern tube: big, open, detailed",
      { { FType::HP, 25, 0.7, 0 }, { FType::Peak, 200, 0.8, 0.5 }, { FType::Peak, 6000, 0.8, 1.5 },
        { FType::Peak, 11000, 1.0, 3.0 }, { FType::HighShelf, 15000, 0.7, 1.0 } }, 5, 0.30, 0.5 },
    { "A12 Tube", "AKG C12 style", "Silky, open top with that 12k shimmer",
      { { FType::HP, 35, 0.7, 0 }, { FType::LowShelf, 150, 0.7, -1.0 }, { FType::Peak, 6000, 1.0, 1.5 },
        { FType::Peak, 11500, 1.5, 6.0 }, { FType::HighShelf, 16000, 0.7, -1.0 } }, 5, 0.35, 0.5 },
    { "T251 Tube", "Telefunken ELA M 251 style", "Rich, sweet, detailed top; lean lows",
      { { FType::HP, 100, 0.6, 0 }, { FType::LowShelf, 200, 0.7, 1.0 }, { FType::Peak, 5000, 1.0, 3.0 },
        { FType::Peak, 12000, 2.0, 3.5 }, { FType::HighShelf, 16000, 0.7, -2.0 } }, 5, 0.40, 0.5 },
    { "M-Ref Tube", "Manley Reference Cardioid style", "Bright, forward, aggressive presence",
      { { FType::HP, 30, 0.7, 0 }, { FType::LowShelf, 150, 0.7, 1.0 }, { FType::Peak, 4500, 1.0, 3.0 },
        { FType::Peak, 10000, 1.0, 4.0 }, { FType::HighShelf, 14000, 0.7, 1.0 } }, 5, 0.35, 0.5 },
    { "S7 Classic", "Shure SM7A style", "Dark, thick, tight dynamic; no room",
      { { FType::HP, 60, 0.7, 0 }, { FType::LowShelf, 200, 0.7, 1.5 }, { FType::Peak, 4500, 0.9, 2.0 },
        { FType::HighShelf, 9000, 0.7, -6.0 }, { FType::LP, 14000, 0.7, 0 } }, 5, 0.0, 0.5 },
    { "MJ-50", "Mojave MA-50 style", "Your old mic: clear, present FET",
      { { FType::HP, 30, 0.7, 0 }, { FType::Peak, 5000, 0.8, 2.5 }, { FType::Peak, 10000, 1.0, 1.5 },
        { FType::HighShelf, 14000, 0.7, -1.0 } }, 4, 0.0, 0.5 },
};
constexpr int NUM_MICS = 10;
constexpr int MJ50_INDEX = 9;

// Source mics: what to undo before the target is applied.
//  0 = Flat / unknown, 1 = Samson C01U Pro (measured), 2 = Mojave MA-50
static const char* SOURCE_NAMES[] = { "Flat / unknown mic", "Samson C01U Pro (measured)", "Mojave MA-50" };
constexpr int NUM_SOURCES = 3;
// C01U Pro -> MA-50 correction measured from your takes (MIRRORWALL tone-EQ band layout)
static const double C01U_TO_MA50[12] = { -0.0, 0.02, -0.06, 0.2, -0.37, 0.78, 0.84, -4.68, -3.13, -4.45, -8.66, 1.53 };
static const F EQ12[12] = {
    { FType::LowShelf, 100, 0.707, 0 }, { FType::Peak, 160, 1.4, 0 }, { FType::Peak, 250, 1.4, 0 },
    { FType::Peak, 400, 1.4, 0 }, { FType::Peak, 630, 1.4, 0 }, { FType::Peak, 1000, 1.4, 0 },
    { FType::Peak, 1600, 1.4, 0 }, { FType::Peak, 2500, 1.4, 0 }, { FType::Peak, 4000, 1.4, 0 },
    { FType::Peak, 6300, 1.4, 0 }, { FType::Peak, 9000, 1.4, 0 }, { FType::HighShelf, 12500, 0.707, 0 } };
static const double SOURCE_PATTERN_A[] = { 0.5, 0.63, 0.5 };    // C01U Pro is supercardioid

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
};

static const PreModel PRES[] = {
    { "AV-737 Tube", "Avalon VT-737sp style", "Class-A tube: smooth, wide, expensive top",
      0.20, 0.0, 0.10, 18, { { FType::LowShelf, 80, 0.7, 0.5 }, { FType::HighShelf, 16000, 0.7, 1.0 } }, 2 },
    { "N-73", "Neve 1073 style", "Thick transformer mids and weight",
      0.05, 0.3, 0.60, 20, { { FType::LowShelf, 120, 0.7, 1.2 }, { FType::Peak, 3200, 0.8, 0.5 },
                             { FType::LP, 22000, 0.6, 0 } }, 3 },
    { "U-610 Tube", "Universal Audio 610 style", "Vintage tube warmth, soft top",
      0.25, 0.0, 0.40, 20, { { FType::LowShelf, 100, 0.7, 1.0 }, { FType::HighShelf, 10000, 0.7, -1.5 } }, 2 },
    { "A-512", "API 512c style", "Punchy, forward, aggressive mids",
      0.03, 0.7, 0.35, 20, { { FType::Peak, 100, 0.8, 0.8 }, { FType::Peak, 2000, 0.7, 1.0 },
                             { FType::HighShelf, 12000, 0.7, 0.5 } }, 3 },
    { "MV-Box Tube", "Manley VOXBOX style", "Tube vocal channel: rich and polished",
      0.18, 0.0, 0.30, 18, { { FType::Peak, 250, 0.9, 0.7 }, { FType::HighShelf, 12000, 0.7, 1.0 } }, 2 },
    { "S-4K E", "SSL 4000 E style", "Clean British console punch",
      0.02, 0.5, 0.10, 16, { { FType::LowShelf, 80, 0.7, -0.3 }, { FType::HighShelf, 10000, 0.7, 0.8 } }, 2 },
};
constexpr int NUM_PRES = 6;
} // namespace dm
