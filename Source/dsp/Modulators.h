// Mystery Box Synthesizer by Raygun
// ADSR envelopes with curve control, and tempo-syncable LFOs.
#pragma once

#include "DspCommon.h"

namespace mbx
{
/** Analog-style ADSR (exponential segments via overshooting one-pole targets).
    The curve control morphs each segment from near-linear (0) to strongly exponential (1). */
class Adsr
{
public:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void prepare (float sampleRate) noexcept { sr = sampleRate; }

    void setParameters (float attackS, float decayS, float sustain, float releaseS, float curve01) noexcept
    {
        sustainLevel = clampf (sustain, 0.0f, 1.0f);
        const float c = clampf (curve01, 0.0f, 1.0f);
        const float ratioA = lerp (3.0f, 0.08f, c);
        const float ratioDR = lerp (0.6f, 0.0005f, c);
        calc (attackS, ratioA, attackCoef, attackBase, 1.0f);
        calc (decayS, ratioDR, decayCoef, decayBase, 0.0f, sustainLevel);
        calc (releaseS, ratioDR, releaseCoef, releaseBase, 0.0f, 0.0f);
        attackTarget = 1.0f + ratioA;
        decayTarget = sustainLevel - ratioDR;
        releaseTarget = -ratioDR;
    }

    void noteOn (bool fromZero = false) noexcept
    {
        if (fromZero) level = 0.0f;
        stage = Stage::Attack;
    }
    void noteOff() noexcept { if (stage != Stage::Idle) stage = Stage::Release; }
    void kill() noexcept { stage = Stage::Idle; level = 0.0f; }

    inline float next() noexcept
    {
        switch (stage)
        {
            case Stage::Idle: return 0.0f;
            case Stage::Attack:
                level = attackBase + level * attackCoef;
                if (level >= 1.0f) { level = 1.0f; stage = Stage::Decay; }
                break;
            case Stage::Decay:
                level = decayBase + level * decayCoef;
                if (level <= sustainLevel) { level = sustainLevel; stage = Stage::Sustain; }
                break;
            case Stage::Sustain:
                level += 0.002f * (sustainLevel - level); // follows sustain changes smoothly
                break;
            case Stage::Release:
                level = releaseBase + level * releaseCoef;
                if (level <= 1.0e-5f) { level = 0.0f; stage = Stage::Idle; }
                break;
        }
        return level;
    }
    /** Advance n samples, return the final level (used for control-rate envelopes). */
    inline float advance (int n) noexcept
    {
        float v = level;
        for (int i = 0; i < n; ++i) v = next();
        return v;
    }

    bool isActive() const noexcept { return stage != Stage::Idle; }
    bool isReleasing() const noexcept { return stage == Stage::Release; }
    float getLevel() const noexcept { return level; }
    Stage getStage() const noexcept { return stage; }

private:
    void calc (float seconds, float ratio, float& coef, float& base, float targetFrom1, float sustainFor = 0.0f) noexcept
    {
        const float samples = std::max (1.0f, seconds * sr);
        coef = std::exp (-std::log ((1.0f + ratio) / ratio) / samples);
        if (targetFrom1 > 0.5f) base = (1.0f + ratio) * (1.0f - coef);   // attack
        else base = (sustainFor - ratio) * (1.0f - coef);                // decay / release
    }

    float sr = 48000.0f;
    Stage stage = Stage::Idle;
    float level = 0.0f, sustainLevel = 1.0f;
    float attackCoef = 0, attackBase = 0, decayCoef = 0, decayBase = 0, releaseCoef = 0, releaseBase = 0;
    float attackTarget = 1, decayTarget = 0, releaseTarget = 0;
};

/** Beat lengths of the 12 tempo-sync divisions shared by LFOs and the delay. */
inline float syncDivisionBeats (int index) noexcept
{
    static constexpr float beats[12] = { 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.5f, 0.75f };
    return beats[std::clamp (index, 0, 11)];
}
inline const char* syncDivisionName (int index) noexcept
{
    static const char* names[12] = { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/4.", "1/8." };
    return names[std::clamp (index, 0, 11)];
}

/** Control-rate LFO. Shapes: 0 sine, 1 triangle, 2 saw, 3 square, 4 sample&hold, 5 smooth random. */
class Lfo
{
public:
    void reset (float startPhase, uint32_t seed) noexcept
    {
        phase = startPhase;
        rng = FastRandom (seed * 2654435761u + 1u);
        held = rng.nextBipolar();
        prevHeld = held;
        fadeLevel = 0.0f;
    }
    /** Advances by dt seconds at the given frequency and returns a bipolar value. */
    inline float advance (int shape, float hz, float dtSeconds, float fadeSeconds) noexcept
    {
        phase += hz * dtSeconds;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            prevHeld = held;
            held = rng.nextBipolar();
        }
        if (fadeSeconds > 0.001f) fadeLevel = std::min (1.0f, fadeLevel + dtSeconds / fadeSeconds);
        else fadeLevel = 1.0f;
        return valueAt (shape, phase) * fadeLevel;
    }
    inline float valueAt (int shape, float ph) const noexcept
    {
        switch (shape)
        {
            case 0: return fastSin01 (ph);
            case 1: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
            case 2: return 1.0f - 2.0f * ph;
            case 3: return ph < 0.5f ? 1.0f : -1.0f;
            case 4: return held;
            default:
            {
                const float t = ph * ph * (3.0f - 2.0f * ph);
                return prevHeld + (held - prevHeld) * t;
            }
        }
    }
    float phase = 0.0f;

private:
    FastRandom rng;
    float held = 0.0f, prevHeld = 0.0f, fadeLevel = 1.0f;
};

} // namespace mbx
