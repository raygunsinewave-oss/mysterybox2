// Mystery Box Synthesizer by Raygun
// Zero-delay-feedback filters: TPT state-variable, ZDF ladder (12/24 dB),
// vowel formant bank and a tuned feedback comb.
#pragma once

#include "DspCommon.h"

namespace mbx
{
/** Andrew Simper / Zavalishin TPT state-variable filter. Unconditionally stable. */
struct SvfCore
{
    float ic1 = 0.0f, ic2 = 0.0f;
    float g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

    inline void set (float cutoffHz, float q, float sr) noexcept
    {
        g = std::tan (kPi * clampf (cutoffHz, 10.0f, sr * 0.48f) / sr);
        k = 1.0f / std::max (q, 0.05f);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    struct Out { float lp, bp, hp; };
    inline Out tick (float x) noexcept
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return { v2, v1, x - k * v1 - v2 };
    }
    void reset() noexcept { ic1 = ic2 = 0.0f; }
};

/** Zero-delay-feedback transistor-ladder style lowpass with input saturation.
    The feedback loop is solved analytically for the linear part (ZDF), then the
    loop input is saturated. Specialised at compile time for 2 and 4 poles. */
struct LadderCore
{
    float s[4] {};
    float G = 0.0f, k = 0.0f, drive = 1.0f, invDrive = 1.0f, comp = 1.0f, G2 = 0.0f, G4 = 0.0f, invDen = 1.0f;

    inline void set (float cutoffHz, float res01, float drive01, int numPoles, float sr) noexcept
    {
        const float g = std::tan (kPi * clampf (cutoffHz, 10.0f, sr * 0.45f) / sr);
        G = g / (1.0f + g);
        G2 = G * G; G4 = G2 * G2;
        k = (numPoles == 4 ? 3.95f : 1.9f) * clampf (res01, 0.0f, 1.0f);
        comp = 1.0f + k * (numPoles == 4 ? 0.55f : 0.35f); // passband gain compensation
        drive = 1.0f + drive01 * 5.0f;
        invDrive = 1.0f / drive;
        invDen = 1.0f / (1.0f + k * (numPoles == 4 ? G4 : G2));
    }
    /** Cubic soft clipper (no division): smooth, saturates at +-1. */
    static inline float softClip (float x) noexcept
    {
        x = clampf (x, -1.5f, 1.5f);
        return x - (4.0f / 27.0f) * x * x * x;
    }
    template <int Poles>
    inline float tick (float x) noexcept
    {
        const float a = 1.0f - G;
        float S;
        if constexpr (Poles == 4) S = a * (((G * s[0] + s[1]) * G + s[2]) * G + s[3]);
        else S = a * (G * s[0] + s[1]);
        float u = (x * comp - k * S) * invDen;
        u = softClip (u * drive * 0.75f) * invDrive * 1.3333f;
        float y = u;
        for (int i = 0; i < Poles; ++i)
        {
            const float v = (y - s[i]) * G;
            const float out = v + s[i];
            s[i] = out + v;
            y = out;
        }
        return y;
    }
    void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.0f; }
};

/** Three-band vowel formant filter morphing A-E-I-O-U. */
struct FormantCore
{
    SvfCore bands[3];
    float gains[3] { 1.0f, 0.6f, 0.3f };

    inline void set (float morph01, float res01, float shift, float sr) noexcept
    {
        static constexpr float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        static constexpr float A[5][3] = { { 1.0f, 0.5f, 0.25f }, { 1.0f, 0.3f, 0.25f }, { 1.0f, 0.2f, 0.2f }, { 1.0f, 0.4f, 0.1f }, { 1.0f, 0.25f, 0.06f } };
        const float pos = clampf (morph01, 0.0f, 1.0f) * 4.0f;
        const int i0 = std::min ((int) pos, 3);
        const float fr = pos - (float) i0;
        const float q = 4.0f + res01 * 16.0f;
        for (int b = 0; b < 3; ++b)
        {
            const float f = lerp (F[i0][b], F[i0 + 1][b], fr) * shift;
            gains[b] = lerp (A[i0][b], A[i0 + 1][b], fr);
            bands[b].set (f, q, sr);
        }
    }
    inline float tick (float x) noexcept
    {
        float y = 0.0f;
        // SVF band-pass peaks at Q; scale by k (=1/Q) for unity peak gain per formant
        for (int b = 0; b < 3; ++b) y += bands[b].tick (x).bp * bands[b].k * gains[b];
        return y * 6.0f;
    }
    void reset() noexcept { for (auto& b : bands) b.reset(); }
};

/** Feedback comb tuned to the cutoff frequency (great for metallic/flanged tones). */
struct CombCore
{
    DelayLine line;
    float delaySamples = 100.0f, feedback = 0.5f;
    OnePole damp;

    void allocate (int maxSamples) { line.allocate (maxSamples); }
    inline void set (float hz, float res01, float sr) noexcept
    {
        const float maxDelay = (float) (line.capacity() - 8);
        delaySamples = clampf (sr / std::max (hz, 10.0f), 2.0f, maxDelay);
        feedback = 0.3f + 0.67f * clampf (res01, 0.0f, 1.0f);
        damp.setCutoff (std::min (sr * 0.4f, 12000.0f), sr);
    }
    inline float tick (float x) noexcept
    {
        const float d = line.read (delaySamples);
        const float y = x + feedback * damp.lp (d);
        line.push (undenormal (fastTanh (y * 0.7f) / 0.7f));
        return y * (1.0f - 0.85f * feedback) * 1.2f; // tames the 1/(1-fb) resonance peak
    }
    void reset() noexcept { line.clear(); damp.reset(); }
};

/** One complete per-channel filter slot: picks the core by FilterType. */
class FilterSlot
{
public:
    void prepare (float sampleRate, int combMaxSamples)
    {
        sr = sampleRate;
        comb.allocate (combMaxSamples);
        reset();
    }
    void reset() noexcept { svf.reset(); ladder.reset(); formant.reset(); comb.reset(); }

    /** type: 0..7 (see FilterType). Call at control rate. */
    inline void set (int newType, float cutoffHz, float res01, float drive01, float formantShift = 1.0f) noexcept
    {
        if (newType != type) { type = newType; reset(); }
        switch (type)
        {
            case 0: ladder.set (cutoffHz, res01, drive01, 2, sr); break;
            case 1: ladder.set (cutoffHz, res01, drive01, 4, sr); break;
            case 2: case 3: case 4: case 5:
                svf.set (cutoffHz, 0.5f + res01 * res01 * 24.0f, sr);
                inDrive = 1.0f + drive01 * 3.0f;
                invSqrtDrive = 1.0f / std::sqrt (inDrive);
                break;
            case 6:
            {
                // Map the cutoff (20 Hz..20 kHz, log) onto the vowel morph axis.
                const float morph = std::log2 (clampf (cutoffHz, 20.0f, 20000.0f) / 20.0f) / 9.9658f;
                formant.set (morph, res01, formantShift, sr);
                inDrive = 1.0f + drive01 * 3.0f;
                invSqrtDrive = 1.0f / std::sqrt (inDrive);
                break;
            }
            default:
                comb.set (cutoffHz, res01, sr);
                inDrive = 1.0f + drive01 * 3.0f;
                invSqrtDrive = 1.0f / std::sqrt (inDrive);
                break;
        }
    }

    /** Filters a block in place. The type switch happens once per block. */
    inline void process (float* x, int n) noexcept
    {
        const bool d = inDrive > 1.001f;
        switch (type)
        {
            case 0: for (int i = 0; i < n; ++i) x[i] = ladder.tick<2> (x[i]); break;
            case 1: for (int i = 0; i < n; ++i) x[i] = ladder.tick<4> (x[i]); break;
            case 2: for (int i = 0; i < n; ++i) x[i] = svf.tick (d ? drv (x[i]) : x[i]).lp; break;
            case 3: for (int i = 0; i < n; ++i) x[i] = svf.tick (d ? drv (x[i]) : x[i]).hp; break;
            case 4: { const float kk = std::sqrt (svf.k) * 1.4f; for (int i = 0; i < n; ++i) x[i] = svf.tick (d ? drv (x[i]) : x[i]).bp * kk; } break;
            case 5: for (int i = 0; i < n; ++i) { const auto o = svf.tick (d ? drv (x[i]) : x[i]); x[i] = o.lp + o.hp; } break;
            case 6: for (int i = 0; i < n; ++i) x[i] = formant.tick (d ? drv (x[i]) : x[i]); break;
            default: for (int i = 0; i < n; ++i) x[i] = comb.tick (d ? drv (x[i]) : x[i]); break;
        }
    }
    inline float tick (float x) noexcept { process (&x, 1); return x; }

    /** Filters a stereo pair with two identical slots in one interleaved loop, giving the
        CPU two independent dependency chains to overlap (roughly 2x throughput). */
    static inline void processStereo (FilterSlot& a, FilterSlot& b, float* L, float* R, int n) noexcept
    {
        if (a.type != b.type) { a.process (L, n); b.process (R, n); return; }
        const bool d = a.inDrive > 1.001f;
        switch (a.type)
        {
            case 0: for (int i = 0; i < n; ++i) { L[i] = a.ladder.tick<2> (L[i]); R[i] = b.ladder.tick<2> (R[i]); } break;
            case 1: for (int i = 0; i < n; ++i) { L[i] = a.ladder.tick<4> (L[i]); R[i] = b.ladder.tick<4> (R[i]); } break;
            case 2: for (int i = 0; i < n; ++i) { L[i] = a.svf.tick (d ? a.drv (L[i]) : L[i]).lp; R[i] = b.svf.tick (d ? b.drv (R[i]) : R[i]).lp; } break;
            case 3: for (int i = 0; i < n; ++i) { L[i] = a.svf.tick (d ? a.drv (L[i]) : L[i]).hp; R[i] = b.svf.tick (d ? b.drv (R[i]) : R[i]).hp; } break;
            default: a.process (L, n); b.process (R, n); break;
        }
    }

private:
    inline float drv (float x) const noexcept { return fastTanh (x * inDrive) * invSqrtDrive; }
    float sr = 48000.0f, inDrive = 1.0f, invSqrtDrive = 1.0f;
    int type = -1;
    SvfCore svf;
    LadderCore ladder;
    FormantCore formant;
    CombCore comb;
};

} // namespace mbx
