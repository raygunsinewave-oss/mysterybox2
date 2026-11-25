// Mystery Box Synthesizer by Raygun
// Host-automatable parameter IDs and FX parameter layout.
#pragma once

#include <JuceHeader.h>
#include <array>

namespace mbx::ids
{
inline constexpr const char* macro[8] = { "macro1", "macro2", "macro3", "macro4", "macro5", "macro6", "macro7", "macro8" };
inline constexpr const char* macroNames[8] = { "Tone", "Character", "Movement", "Space", "Attack", "Release", "Grit", "Width" };
inline constexpr const char* overclock = "overclock";
inline constexpr const char* ampA = "amp_attack";
inline constexpr const char* ampD = "amp_decay";
inline constexpr const char* ampS = "amp_sustain";
inline constexpr const char* ampR = "amp_release";
inline constexpr const char* cutoff = "cutoff";
inline constexpr const char* resonance = "resonance";
inline constexpr const char* glide = "glide";
inline constexpr const char* voices = "voices";
inline constexpr const char* volume = "volume";
inline constexpr const char* pan = "pan";
inline constexpr const char* ambienceOn = "ambience_on";
inline constexpr const char* ambienceLevel = "ambience_level";

enum class FxParamKind { Float, Choice, Bool };
struct FxParamDef
{
    const char* name;
    FxParamKind kind;
    float defaultValue; // normalised (Float), index (Choice) or 0/1 (Bool)
    const char* choices; // '|' separated for Choice params
};
struct FxDef
{
    const char* name;
    const char* shortName;
    bool defaultOn;
    float defaultMix;
    int numParams;
    std::array<FxParamDef, 4> params;
};

inline constexpr std::array<FxDef, 9> fx {{
    { "Drive / Saturation", "DRIVE", false, 1.0f, 3, {{ { "Mode", FxParamKind::Choice, 0, "Tape|Tube|Hard Clip" }, { "Drive", FxParamKind::Float, 0.35f, "" }, { "Tone", FxParamKind::Float, 0.7f, "" }, {} }} },
    { "Bitcrusher", "CRUSH", false, 1.0f, 2, {{ { "Bits", FxParamKind::Float, 0.45f, "" }, { "Downsample", FxParamKind::Float, 0.3f, "" }, {}, {} }} },
    { "Chorus / Ensemble", "CHORUS", false, 0.4f, 2, {{ { "Rate", FxParamKind::Float, 0.3f, "" }, { "Depth", FxParamKind::Float, 0.5f, "" }, {}, {} }} },
    { "Phaser", "PHASER", false, 0.5f, 3, {{ { "Rate", FxParamKind::Float, 0.2f, "" }, { "Depth", FxParamKind::Float, 0.7f, "" }, { "Feedback", FxParamKind::Float, 0.5f, "" }, {} }} },
    { "Flanger", "FLANGER", false, 0.5f, 3, {{ { "Rate", FxParamKind::Float, 0.15f, "" }, { "Depth", FxParamKind::Float, 0.5f, "" }, { "Feedback", FxParamKind::Float, 0.65f, "" }, {} }} },
    { "Stereo Delay", "DELAY", false, 0.25f, 4, {{ { "Time", FxParamKind::Choice, 11, "4 bars|2 bars|1 bar|1/2|1/4|1/8|1/8T|1/16|1/16T|1/32|1/4.|1/8." }, { "Feedback", FxParamKind::Float, 0.4f, "" }, { "Ping-Pong", FxParamKind::Bool, 1, "" }, { "Tone", FxParamKind::Float, 0.6f, "" } }} },
    { "Reverb", "REVERB", true, 0.18f, 3, {{ { "Mode", FxParamKind::Choice, 0, "Hall|Room|Plate|Bunker|Asylum" }, { "Size", FxParamKind::Float, 0.5f, "" }, { "Damping", FxParamKind::Float, 0.4f, "" }, {} }} },
    { "3-Band EQ", "EQ", false, 1.0f, 4, {{ { "Low", FxParamKind::Float, 0.5f, "" }, { "Mid", FxParamKind::Float, 0.5f, "" }, { "Mid Freq", FxParamKind::Float, 0.5f, "" }, { "High", FxParamKind::Float, 0.5f, "" } }} },
    { "Compressor", "COMP", false, 1.0f, 4, {{ { "Threshold", FxParamKind::Float, 0.6f, "" }, { "Ratio", FxParamKind::Float, 0.3f, "" }, { "Release", FxParamKind::Float, 0.3f, "" }, { "Makeup", FxParamKind::Float, 0.0f, "" } }} },
}};

inline juce::String fxOn (int i) { return "fx" + juce::String (i) + "_on"; }
inline juce::String fxMix (int i) { return "fx" + juce::String (i) + "_mix"; }
inline juce::String fxParam (int i, int p) { return "fx" + juce::String (i) + "_p" + juce::String (p); }

} // namespace mbx::ids
