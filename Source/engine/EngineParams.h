// Mystery Box Synthesizer by Raygun
// The complete per-instrument engine parameter set. Every preset in the
// 2,500-instrument library is one EngineParams value.
//
// The list is an X-macro so the C++ engine, the binary preset loader and the
// Python preset generator (which parses this file) all share a single source
// of truth for parameter names, ranges and defaults.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

// X(enumName, "string_id", min, max, default)
#define MBX_OSC_PARAMS(n, defOn)                                   \
    X (o##n##On,     "osc" #n "_on",      0.0f,   1.0f,  defOn)    \
    X (o##n##Mode,   "osc" #n "_mode",    0.0f,   6.0f,  0.0f)     \
    X (o##n##Shape,  "osc" #n "_shape",   0.0f,  63.0f,  0.0f)     \
    X (o##n##A,      "osc" #n "_a",       0.0f,   1.0f,  0.5f)     \
    X (o##n##B,      "osc" #n "_b",       0.0f,   1.0f,  0.5f)     \
    X (o##n##C,      "osc" #n "_c",       0.0f,   1.0f,  0.0f)     \
    X (o##n##Oct,    "osc" #n "_oct",    -3.0f,   3.0f,  0.0f)     \
    X (o##n##Semi,   "osc" #n "_semi",  -12.0f,  12.0f,  0.0f)     \
    X (o##n##Fine,   "osc" #n "_fine", -100.0f, 100.0f,  0.0f)     \
    X (o##n##Level,  "osc" #n "_level",   0.0f,   1.0f,  0.7f)     \
    X (o##n##Pan,    "osc" #n "_pan",    -1.0f,   1.0f,  0.0f)     \
    X (o##n##Uni,    "osc" #n "_unison",  1.0f,   8.0f,  1.0f)     \
    X (o##n##Detune, "osc" #n "_detune",  0.0f, 100.0f, 12.0f)     \
    X (o##n##Spread, "osc" #n "_spread",  0.0f,   1.0f,  0.5f)

#define MBX_ENV_PARAMS(p, s, a, d, su, r)                          \
    X (p##A,     s "_a",      0.0005f, 20.0f, a)                   \
    X (p##D,     s "_d",      0.001f,  20.0f, d)                   \
    X (p##S,     s "_s",      0.0f,     1.0f, su)                  \
    X (p##R,     s "_r",      0.001f,  30.0f, r)                   \
    X (p##Curve, s "_curve",  0.0f,     1.0f, 0.5f)

#define MBX_LFO_PARAMS(n)                                          \
    X (lfo##n##Shape,  "lfo" #n "_shape",  0.0f,  5.0f, 0.0f)      \
    X (lfo##n##Rate,   "lfo" #n "_rate",   0.01f, 40.0f, 2.0f)     \
    X (lfo##n##Sync,   "lfo" #n "_sync",   0.0f,  1.0f, 0.0f)      \
    X (lfo##n##Div,    "lfo" #n "_div",    0.0f, 11.0f, 5.0f)      \
    X (lfo##n##Retrig, "lfo" #n "_retrig", 0.0f,  1.0f, 1.0f)      \
    X (lfo##n##Fade,   "lfo" #n "_fade",   0.0f,  5.0f, 0.0f)

#define MBX_MOD_SLOT(n)                                            \
    X (m##n##Src, "mod" #n "_src",  0.0f, 19.0f, 0.0f)             \
    X (m##n##Dst, "mod" #n "_dst",  0.0f, 35.0f, 0.0f)             \
    X (m##n##Amt, "mod" #n "_amt", -1.0f,  1.0f, 0.0f)

#define MBX_MACRO_SLOT(n)                                          \
    X (mac##n##Val,  "macro" #n "_value", 0.0f, 1.0f, 0.0f)        \
    X (mac##n##Dst1, "macro" #n "_dst1",  0.0f, 35.0f, 0.0f)       \
    X (mac##n##Amt1, "macro" #n "_amt1", -1.0f, 1.0f, 0.0f)        \
    X (mac##n##Dst2, "macro" #n "_dst2",  0.0f, 35.0f, 0.0f)       \
    X (mac##n##Amt2, "macro" #n "_amt2", -1.0f, 1.0f, 0.0f)

#define MBX_ENGINE_PARAMS                                          \
    MBX_OSC_PARAMS (1, 1.0f)                                       \
    MBX_OSC_PARAMS (2, 0.0f)                                       \
    MBX_OSC_PARAMS (3, 0.0f)                                       \
    X (noiseLevel,  "noise_level",  0.0f, 1.0f, 0.0f)              \
    X (noiseType,   "noise_type",   0.0f, 2.0f, 0.0f)              \
    X (noiseColor,  "noise_color",  0.0f, 1.0f, 0.7f)              \
    X (noiseDecay,  "noise_decay",  0.0f, 1.0f, 0.0f)              \
    X (subLevel,    "sub_level",    0.0f, 1.0f, 0.0f)              \
    X (subOct,      "sub_oct",     -2.0f,-1.0f,-1.0f)              \
    X (subShape,    "sub_shape",    0.0f, 1.0f, 0.0f)              \
    X (f1Type,      "f1_type",      0.0f, 7.0f, 1.0f)              \
    X (f1Cutoff,    "f1_cutoff",   20.0f, 20000.0f, 8000.0f)       \
    X (f1Res,       "f1_res",       0.0f, 1.0f, 0.15f)             \
    X (f1Drive,     "f1_drive",     0.0f, 1.0f, 0.0f)              \
    X (f1Env,       "f1_env",      -1.0f, 1.0f, 0.0f)              \
    X (f1Key,       "f1_key",       0.0f, 1.0f, 0.3f)              \
    X (f1Vel,       "f1_vel",       0.0f, 1.0f, 0.2f)              \
    X (f2On,        "f2_on",        0.0f, 1.0f, 0.0f)              \
    X (f2Type,      "f2_type",      0.0f, 7.0f, 3.0f)              \
    X (f2Cutoff,    "f2_cutoff",   20.0f, 20000.0f, 120.0f)        \
    X (f2Res,       "f2_res",       0.0f, 1.0f, 0.1f)              \
    X (f2Env,       "f2_env",      -1.0f, 1.0f, 0.0f)              \
    X (f2Key,       "f2_key",       0.0f, 1.0f, 0.0f)              \
    X (filterRouting,"filter_routing",0.0f, 1.0f, 0.0f)            \
    MBX_ENV_PARAMS (amp,  "amp",  0.003f, 0.3f, 0.8f, 0.35f)       \
    MBX_ENV_PARAMS (fenv, "fenv", 0.003f, 0.5f, 0.3f, 0.4f)        \
    MBX_ENV_PARAMS (menv, "menv", 0.003f, 0.8f, 0.0f, 0.4f)        \
    MBX_LFO_PARAMS (1)                                             \
    MBX_LFO_PARAMS (2)                                             \
    X (polyMono,    "mono",         0.0f, 1.0f, 0.0f)              \
    X (legato,      "legato",       0.0f, 1.0f, 0.0f)              \
    X (glide,       "glide",        0.0f, 2.0f, 0.0f)              \
    X (velAmp,      "vel_amp",      0.0f, 1.0f, 0.6f)              \
    X (voiceDrive,  "voice_drive",  0.0f, 1.0f, 0.0f)              \
    X (presetGain,  "preset_gain_db",-36.0f, 12.0f, -6.0f)         \
    MBX_MOD_SLOT (1) MBX_MOD_SLOT (2) MBX_MOD_SLOT (3) MBX_MOD_SLOT (4) \
    MBX_MOD_SLOT (5) MBX_MOD_SLOT (6) MBX_MOD_SLOT (7) MBX_MOD_SLOT (8) \
    MBX_MACRO_SLOT (1) MBX_MACRO_SLOT (2) MBX_MACRO_SLOT (3) MBX_MACRO_SLOT (4) \
    MBX_MACRO_SLOT (5) MBX_MACRO_SLOT (6) MBX_MACRO_SLOT (7) MBX_MACRO_SLOT (8) \
    X (fxRevMix,    "fx_reverb_mix", 0.0f, 1.0f, 0.15f)            \
    X (fxRevMode,   "fx_reverb_mode",0.0f, 4.0f, 0.0f)             \
    X (fxRevSize,   "fx_reverb_size",0.0f, 1.0f, 0.5f)             \
    X (fxDlyMix,    "fx_delay_mix",  0.0f, 1.0f, 0.0f)             \
    X (fxDlyDiv,    "fx_delay_div",  0.0f, 11.0f, 6.0f)            \
    X (fxChoMix,    "fx_chorus_mix", 0.0f, 1.0f, 0.0f)

namespace mbx
{
enum class P : int
{
#define X(e, s, mn, mx, df) e,
    MBX_ENGINE_PARAMS
#undef X
    count
};

constexpr int kNumEngineParams = (int) P::count;

struct ParamInfo
{
    std::string_view id;
    float minValue, maxValue, defaultValue;
};

inline constexpr std::array<ParamInfo, kNumEngineParams> kParamInfo {{
#define X(e, s, mn, mx, df) ParamInfo { s, mn, mx, df },
    MBX_ENGINE_PARAMS
#undef X
}};

/** One complete instrument: a flat array of engine parameters. */
struct EngineParams
{
    std::array<float, kNumEngineParams> v {};

    EngineParams() noexcept { setDefaults(); }
    void setDefaults() noexcept
    {
        for (int i = 0; i < kNumEngineParams; ++i)
            v[(size_t) i] = kParamInfo[(size_t) i].defaultValue;
    }
    float operator[] (P p) const noexcept { return v[(size_t) p]; }
    float& operator[] (P p) noexcept { return v[(size_t) p]; }
    float get (P p) const noexcept { return v[(size_t) p]; }
    int getInt (P p) const noexcept { return (int) (v[(size_t) p] + 0.5f); }
    bool getBool (P p) const noexcept { return v[(size_t) p] >= 0.5f; }
    static P offset (P base, int delta) noexcept { return (P) ((int) base + delta); }
    void sanitise() noexcept
    {
        for (int i = 0; i < kNumEngineParams; ++i)
        {
            auto& x = v[(size_t) i];
            const auto& info = kParamInfo[(size_t) i];
            if (! (x == x)) x = info.defaultValue; // NaN guard
            x = x < info.minValue ? info.minValue : (x > info.maxValue ? info.maxValue : x);
        }
    }
};

inline int findParamIndex (std::string_view id) noexcept
{
    for (int i = 0; i < kNumEngineParams; ++i)
        if (kParamInfo[(size_t) i].id == id)
            return i;
    return -1;
}

// Stride helpers so per-oscillator / per-slot code can be written once.
constexpr int kOscStride = (int) P::o2On - (int) P::o1On;
constexpr int kModStride = (int) P::m2Src - (int) P::m1Src;
constexpr int kMacroStride = (int) P::mac2Val - (int) P::mac1Val;
constexpr int kLfoStride = (int) P::lfo2Shape - (int) P::lfo1Shape;
constexpr int kEnvStride = (int) P::fenvA - (int) P::ampA;

enum class OscMode : int { VirtualAnalog = 0, Wavetable, FM, Pluck, Modal, Waveguide, Additive };
enum class FilterType : int { Ladder12 = 0, Ladder24, SvfLP, SvfHP, SvfBP, SvfNotch, Formant, Comb };

enum class ModSource : int
{
    None = 0, Lfo1, Lfo2, EnvAmp, EnvFilter, EnvMod, Velocity, ModWheel, Aftertouch, KeyTrack, Random, Slide,
    Macro1, Macro2, Macro3, Macro4, Macro5, Macro6, Macro7, Macro8,
    count
};

enum class ModDest : int
{
    None = 0, Pitch, Osc1Pitch, Osc2Pitch, Osc3Pitch, Osc1Level, Osc2Level, Osc3Level,
    Osc1A, Osc2A, Osc3A, Osc1B, Osc2B, Osc3B,
    F1Cutoff, F1Res, F2Cutoff, F2Res, Amp, Pan, NoiseLevel, UnisonDetune, Lfo1Rate, Lfo2Rate,
    SubLevel, Drive, AmpAttack, AmpRelease,
    // Global destinations (only meaningful from macros)
    ReverbMix, DelayMix, ChorusMix,
    FilterEnvAmt, OscC, Spread, Lfo1Depth, Lfo2Depth,
    count
};

static_assert ((int) ModDest::count == 36, "keep in sync with mod/macro dst ranges in EngineParams.h");
static_assert ((int) ModSource::count == 20, "keep in sync with mod src range in EngineParams.h");

} // namespace mbx
