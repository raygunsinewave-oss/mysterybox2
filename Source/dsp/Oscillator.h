// Mystery Box Synthesizer by Raygun
// One per-voice oscillator slot with seven synthesis modes.
#pragma once

#include "DspCommon.h"
#include "Wavetables.h"
#include "../engine/EngineParams.h"

namespace mbx
{
struct OscControl
{
    int mode = 0;          // OscMode
    int shape = 0;         // mode-dependent selector
    float a = 0.5f, b = 0.5f, c = 0.0f;
    float hz = 440.0f;     // centre frequency including pitch modulation
    float level = 0.7f;    // linear gain
    float pan = 0.0f;      // -1..1
    int unison = 1;
    float detuneCents = 0.0f;
    float spread = 0.0f;   // stereo spread of unison voices 0..1
};

class Oscillator
{
public:
    static constexpr int kMaxUnison = 8;
    static constexpr int kModes = 12;
    static constexpr int kMaxPartials = 32;

    void prepare (float sampleRate, uint32_t seed);
    void noteOn (const OscControl& c, float velocity, uint32_t noteSeed);
    /** Renders n samples (n <= 64) and ADDS into outL/outR. Level is ramped from the previous block. */
    void render (const OscControl& c, float* outL, float* outR, int n) noexcept;
    void reset() noexcept;

private:
    void renderVA (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderWT (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderFM (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderPluck (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderModal (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderWaveguide (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void renderAdditive (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept;
    void updateUnison (const OscControl& c) noexcept;
    void setupModal (const OscControl& c) noexcept;
    void setupAdditive (const OscControl& c) noexcept;

    float sr = 48000.0f;
    FastRandom rng;
    float lastLevel = 0.0f;
    int activeMode = -1;

    // Unison state (VA / WT / FM)
    float phase[kMaxUnison] {}, modPhase[kMaxUnison] {}, fbState[kMaxUnison] {}, triState[kMaxUnison] {};
    float inc[kMaxUnison] {}, gainL[kMaxUnison] {}, gainR[kMaxUnison] {};
    int numUnison = 1;

    // Physical models
    DelayLine lineA, lineB;
    float plLp = 0.0f, plCoef = 0.5f;
    DcBlocker dc;
    float excitePhase = 0.0f; int exciteLen = 0, exciteCount = 0;
    float exciteGain = 1.0f;
    OnePole exciteLp, bodyLp, loopLp;
    float bowRamp = 0.0f, velocityAmt = 1.0f;

    // Modal bank
    float mY1[kModes] {}, mY2[kModes] {}, mB1[kModes] {}, mB2[kModes] {}, mIn[kModes] {}, mRatio[kModes] {}, mR[kModes] {};
    int numModes = 0;
    float modalHz = 0.0f;

    // Additive
    float pPhase[kMaxPartials] {}, pAmp[kMaxPartials] {}, pRatio[kMaxPartials] {};
    int numPartials = 0;
    int addShape = -1; float addA = -1, addB = -1, addC = -1;
};

} // namespace mbx
