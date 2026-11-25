// Mystery Box Synthesizer by Raygun
// Polyphonic hybrid synthesis engine. Real-time safe: no allocation, locks or
// I/O after prepare(). Pure C++20 so it can run in the plugin, tests and tools.
#pragma once

#include "EngineParams.h"
#include "../dsp/DspCommon.h"
#include "../dsp/Filters.h"
#include "../dsp/Modulators.h"
#include "../dsp/Oscillator.h"
#include "../dsp/Effects.h"
#include <array>
#include <atomic>

namespace mbx
{
struct EngineGlobals
{
    float a4 = 440.0f;
    float bendRange = 2.0f;      // semitones
    float mpeBendRange = 48.0f;  // semitones for MPE member channels
    bool mpe = false;
    int maxPolyphony = 32;
    int stealMode = 0;           // 0 oldest, 1 quietest, 2 prefer released
    bool oversample = false;
    float overclock = 0.0f;      // 0..1 (0 when the Overclocker is locked)
    double bpm = 120.0;
};

/** Live performance state shared by all voices (written by the engine on the audio thread). */
struct PerformanceState
{
    float macros[8] {};          // current macro knob values 0..1
    float modWheel = 0.0f;
    float globalBend = 0.0f;     // -1..1
    float globalPressure = 0.0f;
    float globalSlide = 0.0f;
    float channelBend[16] {}, channelPressure[16] {}, channelSlide[16] {};
};

class Voice
{
public:
    static constexpr int kBlock = 16; // control-rate block size in samples

    void prepare (float sampleRate, uint32_t seed);
    void start (int note, float velocity, int channel, float startPitch, uint64_t age,
                const EngineParams& p, const EngineGlobals& g, const PerformanceState& ps);
    void glideTo (int note, float velocity) noexcept;  // legato note change
    void release() noexcept;
    void kill() noexcept;
    /** Renders up to kBlock samples, ADDING into L/R. */
    void render (float* L, float* R, int n, const EngineParams& p, const EngineGlobals& g, const PerformanceState& ps) noexcept;

    bool isActive() const noexcept { return active; }
    bool isGateOn() const noexcept { return gate; }
    bool isReleasing() const noexcept { return active && ! gate; }
    float getLevel() const noexcept { return ampEnv.getLevel(); }
    float getCurrentPitch() const noexcept { return pitch; }

    int note = -1, channel = 0;
    uint64_t age = 0;
    bool sustained = false;
    float polyPressure = 0.0f;

    // Pending note after a steal fade-out
    bool stealPending = false;
    int pendingNote = 0, pendingChannel = 0; float pendingVelocity = 0, pendingStartPitch = 0; uint64_t pendingAge = 0;
    int stealFadeBlocks = 0;


private:
    void computeMods (const EngineParams& p, const PerformanceState& ps, float* dst) noexcept;
    float sr = 48000.0f;
    bool active = false, gate = false;
    float velocity = 1.0f, randomValue = 0.0f;
    float pitch = 60.0f, targetPitch = 60.0f;
    Oscillator osc[3];
    FilterSlot f1L, f1R, f2L, f2R;
    Adsr ampEnv, filterEnv, modEnv;
    Lfo lfo[2];
    float lfoValue[2] {};
    float mods[(int) ModDest::count] {};
    float envCache[3][5] {};
    // noise & sub
    FastRandom noiseRng;
    float pinkB[7] {}, brown = 0.0f, noiseLpL = 0.0f, noiseLpR = 0.0f, noiseEnv = 1.0f;
    float subPhase = 0.0f;
    float lastAmpGain = 0.0f, lastPanL = 0.7071f, lastPanR = 0.7071f;
    Oversampler2x driveOsL, driveOsR;
    float bufL[kBlock] {}, bufR[kBlock] {};
};

class SynthEngine
{
public:
    static constexpr int kMaxVoices = 32;

    SynthEngine();
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    /** Installs a new instrument. With crossfade, the output fades out (5 ms), the
        parameters swap, held notes are re-triggered and the output fades back in (5 ms). */
    void setParams (const EngineParams& p, bool crossfade) noexcept;
    const EngineParams& getParams() const noexcept { return params; }
    EngineParams& editParams() noexcept { return params; }

    void setGlobals (const EngineGlobals& g) noexcept { globals = g; }
    const EngineGlobals& getGlobals() const noexcept { return globals; }
    void setMacroValues (const float* values8) noexcept;

    // MIDI
    void noteOn (int channel, int note, float velocity) noexcept;
    void noteOff (int channel, int note) noexcept;
    void pitchBend (int channel, float value) noexcept;  // -1..1
    void controller (int channel, int cc, float value01) noexcept;
    void channelPressure (int channel, float v) noexcept;
    void polyPressure (int channel, int note, float v) noexcept;
    void allNotesOff (bool hard) noexcept;

    /** Renders n samples, OVERWRITING L and R. */
    void render (float* L, float* R, int n) noexcept;

    int getActiveVoiceCount() const noexcept;
    /** Sum of macro offsets for ReverbMix / DelayMix / ChorusMix (global destinations). */
    float getGlobalOffset (ModDest d) const noexcept;

private:
    void startVoice (int channel, int note, float velocity) noexcept;
    int findVoiceToSteal() noexcept;
    void renderSegment (float* L, float* R, int n) noexcept;
    void retriggerHeldNotes() noexcept;
    void monoNoteOn (int channel, int note, float velocity) noexcept;
    void monoNoteOff (int channel, int note) noexcept;

    float sr = 48000.0f;
    EngineParams params, pendingParams;
    EngineGlobals globals;
    PerformanceState perf;
    std::array<Voice, kMaxVoices> voices;
    uint64_t noteCounter = 0;
    float lastNotePitch = 60.0f;
    bool sustainPedal = false;
    uint8_t held[16][128] {};          // velocity*127 of physically held keys (0 = up)

    // Mono note stack
    int monoStack[32] {}; int monoStackChannel[32] {}; float monoStackVel[32] {}; int monoStackSize = 0;

    // Preset crossfade
    enum class Fade { None, Out, In } fade = Fade::None;
    float fadeGain = 1.0f, fadeStep = 0.0f;
    bool hasPending = false;
};

} // namespace mbx
