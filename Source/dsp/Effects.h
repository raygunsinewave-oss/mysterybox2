// Mystery Box Synthesizer by Raygun
// Global stereo FX chain. All memory is allocated in prepare().
#pragma once

#include "DspCommon.h"
#include "Filters.h"
#include "Modulators.h"
#include <array>

namespace mbx
{
enum class FxId : int { Drive = 0, Crusher, Chorus, Phaser, Flanger, Delay, Reverb, Eq, Comp, count };
constexpr int kNumFx = (int) FxId::count;

struct FxUnitParams
{
    bool on = false;
    float mix = 0.5f;
    float p[4] { 0.5f, 0.5f, 0.5f, 0.5f };
};

struct FxParams
{
    std::array<FxUnitParams, kNumFx> unit {};
    std::array<int, kNumFx> order { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    double bpm = 120.0;
    float overclock = 0.0f;     // 0..1 exciter + widener amount
    bool ambienceOn = false;
    float ambienceLevel = 0.3f;
    bool oversample = false;
    // Macro-driven offsets for "Space" style macros (added to mix, clamped)
    float reverbMixOffset = 0.0f, delayMixOffset = 0.0f, chorusMixOffset = 0.0f;
};

/** 2x oversampler using a 47-tap windowed-sinc half-band FIR. */
class Oversampler2x
{
public:
    void prepare();
    void reset() noexcept;
    /** Upsample one input sample into two. */
    inline void up (float x, float& a, float& b) noexcept;
    /** Downsample two samples into one. */
    inline float down (float a, float b) noexcept;

private:
    static constexpr int kTaps = 47;
    float coef[kTaps] {};
    float upHist[kTaps] {}, downHist[kTaps] {};
    int upPos = 0, downPos = 0;
};

class Biquad
{
public:
    void setLowShelf (float sr, float hz, float db) noexcept;
    void setHighShelf (float sr, float hz, float db) noexcept;
    void setPeak (float sr, float hz, float q, float db) noexcept;
    void setLowpass (float sr, float hz, float q) noexcept;
    void setHighpass (float sr, float hz, float q) noexcept;
    void setBandpass (float sr, float hz, float q) noexcept;
    inline float tick (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = undenormal (b1 * x - a1 * y + z2);
        z2 = undenormal (b2 * x - a2 * y);
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }

private:
    void set (double b0, double b1, double b2, double a0, double a1, double a2) noexcept;
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

class FdnReverb
{
public:
    void prepare (float sampleRate);
    void reset() noexcept;
    void setParams (int mode, float size01, float damping01) noexcept;
    void process (float* L, float* R, int n, float mix) noexcept;

private:
    static constexpr int kLines = 8;
    float sr = 48000.0f;
    DelayLine lines[kLines];
    DelayLine pre;
    DelayLine diffusers[4];
    float diffLen[4] {}, lineLen[kLines] {}, gains[kLines] {};
    OnePole damp[kLines];
    OnePole hpL, hpR;
    float lfoPhase = 0.0f, modDepth = 0.0f, preDelay = 0.0f;
    float earlyGain = 0.0f, width = 1.0f;
    int curMode = -1; float curSize = -1, curDamp = -1;
};

class FxChain
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void process (float* L, float* R, int n, const FxParams& params) noexcept;

    /** Linear gain reduction of the output safety limiter (1 = none), for metering. */
    float getLimiterGain() const noexcept { return limGain; }

private:
    void drive (float* L, float* R, int n, const FxUnitParams& u, bool os) noexcept;
    void crusher (float* L, float* R, int n, const FxUnitParams& u) noexcept;
    void chorus (float* L, float* R, int n, const FxUnitParams& u, float mixOffset) noexcept;
    void phaser (float* L, float* R, int n, const FxUnitParams& u) noexcept;
    void flanger (float* L, float* R, int n, const FxUnitParams& u) noexcept;
    void delay (float* L, float* R, int n, const FxUnitParams& u, double bpm, float mixOffset) noexcept;
    void eq (float* L, float* R, int n, const FxUnitParams& u) noexcept;
    void comp (float* L, float* R, int n, const FxUnitParams& u) noexcept;
    void exciterWidener (float* L, float* R, int n, float amount) noexcept;
    void ambience (float* L, float* R, int n, float level) noexcept;
    void limiter (float* L, float* R, int n) noexcept;

    float sr = 48000.0f;
    FastRandom rng { 0x51A7E };

    // Drive
    Oversampler2x osL, osR;
    OnePole driveToneL, driveToneR;
    DcBlocker dcL, dcR;
    // Crusher
    float crushHoldL = 0, crushHoldR = 0, crushCount = 0;
    // Chorus / flanger
    DelayLine choL, choR, flaL, flaR;
    float choPhase = 0, flaPhase = 0, flaFbL = 0, flaFbR = 0;
    // Phaser
    float phPhase = 0, phFbL = 0, phFbR = 0;
    float apL[6] {}, apR[6] {};
    // Delay
    DelayLine dlyL, dlyR;
    LinearSmoother dlyTime;
    OnePole dlyLpL, dlyLpR, dlyHpL, dlyHpR;
    // Reverb
    FdnReverb reverb;
    // EQ
    Biquad eqLow[2], eqMid[2], eqHigh[2];
    float eqCache[4] { -99, -99, -99, -99 };
    // Comp
    float compEnv = 0.0f;
    // Exciter
    OnePole excHpL, excHpR;
    // Ambience
    float windLp1 = 0, windLp2 = 0, windPhase = 0, brownL = 0, brownR = 0;
    Biquad windBpL, windBpR;
    float groanTimer = 3.0f, groanPhase = 0, groanHz = 90, groanAge = 0, groanLen = 0, groanPan = 0;
    FormantCore groanFormant;
    OnePole groanLp;
    // Limiter
    float limGain = 1.0f, limRelease = 0.999f;
};

inline void Oversampler2x::up (float x, float& a, float& b) noexcept
{
    // Zero-stuffed input through the half-band; gain 2 restores level.
    upHist[upPos] = x;
    float s0 = 0.0f, s1 = 0.0f;
    // Even phase (contains the centre tap) and odd phase
    int idx = upPos;
    for (int k = 0; k < kTaps; k += 2)
    {
        s0 += coef[k] * upHist[idx];
        if (k + 1 < kTaps)
        {
            const int j = idx;
            s1 += coef[k + 1] * upHist[j];
        }
        idx = (idx == 0 ? kTaps - 1 : idx - 1);
    }
    upPos = (upPos + 1) % kTaps;
    a = 2.0f * s0;
    b = 2.0f * s1;
}

inline float Oversampler2x::down (float a, float b) noexcept
{
    downHist[downPos] = a; downPos = (downPos + 1) % kTaps;
    downHist[downPos] = b; downPos = (downPos + 1) % kTaps;
    float s = 0.0f;
    int idx = downPos == 0 ? kTaps - 1 : downPos - 1;
    for (int k = 0; k < kTaps; ++k)
    {
        s += coef[k] * downHist[idx];
        idx = (idx == 0 ? kTaps - 1 : idx - 1);
    }
    return s;
}

} // namespace mbx
