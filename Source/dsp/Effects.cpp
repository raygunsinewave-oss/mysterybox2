// Mystery Box Synthesizer by Raygun
#include "Effects.h"

namespace mbx
{
//==============================================================================
void Oversampler2x::prepare()
{
    const int c = kTaps / 2;
    double sum = 0.0;
    double tmp[kTaps];
    for (int k = 0; k < kTaps; ++k)
    {
        const int n = k - c;
        const double sinc = n == 0 ? 0.5 : std::sin (std::numbers::pi * 0.5 * n) / (std::numbers::pi * n);
        const double w = 0.42 - 0.5 * std::cos (kTwoPiD * k / (kTaps - 1)) + 0.08 * std::cos (2.0 * kTwoPiD * k / (kTaps - 1));
        tmp[k] = sinc * w;
        sum += tmp[k];
    }
    for (int k = 0; k < kTaps; ++k) coef[k] = (float) (tmp[k] / sum);
    reset();
}

void Oversampler2x::reset() noexcept
{
    std::fill (std::begin (upHist), std::end (upHist), 0.0f);
    std::fill (std::begin (downHist), std::end (downHist), 0.0f);
    upPos = downPos = 0;
}

//==============================================================================
void Biquad::set (double nb0, double nb1, double nb2, double a0, double na1, double na2) noexcept
{
    b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
    a1 = (float) (na1 / a0); a2 = (float) (na2 / a0);
}
void Biquad::setLowShelf (float sr, float hz, float db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), w = kTwoPiD * hz / sr, cs = std::cos (w), sn = std::sin (w);
    const double al = sn / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
    set (A * ((A + 1) - (A - 1) * cs + sq), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - sq),
         (A + 1) + (A - 1) * cs + sq, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - sq);
}
void Biquad::setHighShelf (float sr, float hz, float db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), w = kTwoPiD * hz / sr, cs = std::cos (w), sn = std::sin (w);
    const double al = sn / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * al;
    set (A * ((A + 1) + (A - 1) * cs + sq), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - sq),
         (A + 1) - (A - 1) * cs + sq, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - sq);
}
void Biquad::setPeak (float sr, float hz, float q, float db) noexcept
{
    const double A = std::pow (10.0, db / 40.0), w = kTwoPiD * hz / sr, al = std::sin (w) / (2.0 * q), cs = std::cos (w);
    set (1 + al * A, -2 * cs, 1 - al * A, 1 + al / A, -2 * cs, 1 - al / A);
}
void Biquad::setLowpass (float sr, float hz, float q) noexcept
{
    const double w = kTwoPiD * clampf (hz, 10.0f, sr * 0.49f) / sr, al = std::sin (w) / (2.0 * q), cs = std::cos (w);
    set ((1 - cs) / 2, 1 - cs, (1 - cs) / 2, 1 + al, -2 * cs, 1 - al);
}
void Biquad::setHighpass (float sr, float hz, float q) noexcept
{
    const double w = kTwoPiD * clampf (hz, 10.0f, sr * 0.49f) / sr, al = std::sin (w) / (2.0 * q), cs = std::cos (w);
    set ((1 + cs) / 2, -(1 + cs), (1 + cs) / 2, 1 + al, -2 * cs, 1 - al);
}
void Biquad::setBandpass (float sr, float hz, float q) noexcept
{
    const double w = kTwoPiD * clampf (hz, 10.0f, sr * 0.49f) / sr, al = std::sin (w) / (2.0 * q), cs = std::cos (w);
    set (al, 0, -al, 1 + al, -2 * cs, 1 - al);
}

//==============================================================================
void FdnReverb::prepare (float sampleRate)
{
    sr = sampleRate;
    for (auto& l : lines) l.allocate ((int) (sr * 0.25f));
    for (auto& d : diffusers) d.allocate ((int) (sr * 0.03f));
    pre.allocate ((int) (sr * 0.2f));
    hpL.setCutoff (60.0f, sr); hpR.setCutoff (60.0f, sr);
    reset();
}

void FdnReverb::reset() noexcept
{
    for (auto& l : lines) l.clear();
    for (auto& d : diffusers) d.clear();
    for (auto& d : damp) d.reset();
    pre.clear(); hpL.reset(); hpR.reset();
}

void FdnReverb::setParams (int mode, float size01, float damping01) noexcept
{
    if (mode == curMode && std::fabs (size01 - curSize) < 1.0e-4f && std::fabs (damping01 - curDamp) < 1.0e-4f) return;
    curMode = mode; curSize = size01; curDamp = damping01;

    // Mode character: base line lengths (ms), RT60 range, damping, modulation, pre-delay
    static constexpr float baseMs[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
    float scale = 1.0f, rtMin = 0.5f, rtMax = 4.0f, dampHz = 6000.0f;
    switch (mode)
    {
        case 0: scale = 1.4f; rtMin = 1.2f; rtMax = 8.0f;  dampHz = 7000.0f;  modDepth = 6.0f;  preDelay = 0.022f; earlyGain = 0.2f; width = 1.0f; break;  // Hall
        case 1: scale = 0.55f; rtMin = 0.25f; rtMax = 1.6f; dampHz = 9000.0f; modDepth = 2.0f;  preDelay = 0.006f; earlyGain = 0.5f; width = 0.8f; break;  // Room
        case 2: scale = 0.8f; rtMin = 0.8f; rtMax = 5.0f;  dampHz = 14000.0f; modDepth = 3.0f;  preDelay = 0.0f;   earlyGain = 0.0f; width = 1.0f; break;  // Plate
        case 3: scale = 0.42f; rtMin = 0.6f; rtMax = 2.8f; dampHz = 3200.0f;  modDepth = 0.5f;  preDelay = 0.012f; earlyGain = 0.8f; width = 0.6f; break;  // Bunker (concrete slap, boxy)
        default: scale = 1.8f; rtMin = 3.0f; rtMax = 14.0f; dampHz = 2500.0f; modDepth = 24.0f; preDelay = 0.045f; earlyGain = 0.1f; width = 1.0f; break; // Asylum (dark, warbling)
    }
    const float sizeScale = scale * (0.6f + 0.8f * size01);
    const float rt60 = lerp (rtMin, rtMax, size01);
    for (int i = 0; i < kLines; ++i)
    {
        lineLen[i] = std::min (baseMs[i] * 0.001f * sr * sizeScale, (float) lines[i].capacity() - 64.0f);
        gains[i] = std::pow (10.0f, -3.0f * lineLen[i] / (rt60 * sr));
        damp[i].setCutoff (dampHz * (1.0f - 0.75f * damping01), sr);
    }
    static constexpr float diffMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
    for (int i = 0; i < 4; ++i) diffLen[i] = diffMs[i] * 0.001f * sr * (mode == 2 ? 1.4f : 1.0f);
}

void FdnReverb::process (float* L, float* R, int n, float mix) noexcept
{
    if (mix <= 0.0001f) return;
    const float wet = std::sqrt (mix), dry = std::sqrt (1.0f - mix);
    const float lfoInc = 0.37f / sr;
    const float preSamples = std::max (1.0f, preDelay * sr);
    for (int s = 0; s < n; ++s)
    {
        const float inL = L[s], inR = R[s];
        pre.push ((inL + inR) * 0.5f);
        float x = pre.read (preSamples);
        // Input diffusion (series allpasses)
        for (int d = 0; d < 4; ++d)
        {
            const float delayed = diffusers[d].read (diffLen[d]);
            const float v = x + 0.6f * delayed;
            diffusers[d].push (undenormal (v));
            x = delayed - 0.6f * v;
        }
        lfoPhase += lfoInc; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        float o[kLines];
        for (int i = 0; i < kLines; ++i)
        {
            const float mod = modDepth * fastSin01 (lfoPhase + (float) i * 0.125f);
            o[i] = damp[i].lp (lines[i].readCubic (lineLen[i] + mod)) * gains[i];
        }
        // Householder mixing (energy preserving): y = o - 2/N * sum
        float sum = 0.0f;
        for (int i = 0; i < kLines; ++i) sum += o[i];
        sum *= 2.0f / (float) kLines;
        for (int i = 0; i < kLines; ++i)
            lines[i].push (undenormal (o[i] - sum + x * (i & 1 ? -0.35f : 0.35f)));

        float wl = o[0] + o[2] + o[4] + o[6];
        float wr = o[1] + o[3] + o[5] + o[7];
        // Early reflections tapped from the pre-delay line (the "concrete" slap in Bunker mode)
        if (earlyGain > 0.0f)
        {
            wl += earlyGain * (pre.read (preSamples + sr * 0.011f) + 0.6f * pre.read (preSamples + sr * 0.023f));
            wr += earlyGain * (pre.read (preSamples + sr * 0.017f) + 0.6f * pre.read (preSamples + sr * 0.031f));
        }
        const float mid = (wl + wr) * 0.5f, side = (wl - wr) * 0.5f * width;
        wl = hpL.hp (mid + side) * 0.35f;
        wr = hpR.hp (mid - side) * 0.35f;
        L[s] = inL * dry + wl * wet;
        R[s] = inR * dry + wr * wet;
    }
}

//==============================================================================
void FxChain::prepare (double sampleRate, int)
{
    sr = (float) sampleRate;
    osL.prepare(); osR.prepare();
    dcL.prepare (sr); dcR.prepare (sr);
    choL.allocate ((int) (sr * 0.05f)); choR.allocate ((int) (sr * 0.05f));
    flaL.allocate ((int) (sr * 0.02f)); flaR.allocate ((int) (sr * 0.02f));
    dlyL.allocate ((int) (sr * 6.0f)); dlyR.allocate ((int) (sr * 6.0f));
    dlyTime.reset (sr, 0.25);
    reverb.prepare (sr);
    excHpL.setCutoff (3500.0f, sr); excHpR.setCutoff (3500.0f, sr);
    windBpL.setBandpass (sr, 400.0f, 0.7f); windBpR.setBandpass (sr, 500.0f, 0.7f);
    groanLp.setCutoff (1400.0f, sr);
    limRelease = std::exp (-1.0f / (0.08f * sr));
    reset();
}

void FxChain::reset() noexcept
{
    osL.reset(); osR.reset(); dcL.reset(); dcR.reset();
    driveToneL.reset(); driveToneR.reset();
    choL.clear(); choR.clear(); flaL.clear(); flaR.clear();
    dlyL.clear(); dlyR.clear();
    dlyTime.setImmediate (sr * 0.25f);
    reverb.reset();
    for (int i = 0; i < 6; ++i) apL[i] = apR[i] = 0.0f;
    phFbL = phFbR = flaFbL = flaFbR = 0.0f;
    for (auto* b : { &eqLow[0], &eqLow[1], &eqMid[0], &eqMid[1], &eqHigh[0], &eqHigh[1] }) b->reset();
    compEnv = 0.0f; limGain = 1.0f;
    groanFormant.reset();
}

void FxChain::process (float* L, float* R, int n, const FxParams& p) noexcept
{
    ScopedFlushDenormals noDenormals;
    for (int slot = 0; slot < kNumFx; ++slot)
    {
        const int id = p.order[(size_t) slot];
        if (id < 0 || id >= kNumFx) continue;
        const auto& u = p.unit[(size_t) id];
        const bool macroForcedOn = (id == (int) FxId::Reverb && p.reverbMixOffset > 0.0f)
                                || (id == (int) FxId::Delay && p.delayMixOffset > 0.0f)
                                || (id == (int) FxId::Chorus && p.chorusMixOffset > 0.0f);
        if (! u.on && ! macroForcedOn) continue;
        switch ((FxId) id)
        {
            case FxId::Drive:   drive (L, R, n, u, p.oversample); break;
            case FxId::Crusher: crusher (L, R, n, u); break;
            case FxId::Chorus:  chorus (L, R, n, u, p.chorusMixOffset); break;
            case FxId::Phaser:  phaser (L, R, n, u); break;
            case FxId::Flanger: flanger (L, R, n, u); break;
            case FxId::Delay:   delay (L, R, n, u, p.bpm, p.delayMixOffset); break;
            case FxId::Reverb:
            {
                reverb.setParams ((int) (u.p[0] * 4.0f + 0.5f), u.p[1], u.p[2]);
                const float mix = clampf ((u.on ? u.mix : 0.0f) + p.reverbMixOffset, 0.0f, 1.0f);
                reverb.process (L, R, n, mix);
                break;
            }
            case FxId::Eq:      eq (L, R, n, u); break;
            case FxId::Comp:    comp (L, R, n, u); break;
            default: break;
        }
    }
    if (p.ambienceOn) ambience (L, R, n, p.ambienceLevel);
    if (p.overclock > 0.001f) exciterWidener (L, R, n, p.overclock);
    limiter (L, R, n);
}

void FxChain::drive (float* L, float* R, int n, const FxUnitParams& u, bool os) noexcept
{
    const int mode = (int) (u.p[0] * 2.0f + 0.5f);
    const float gain = dbToGain (u.p[1] * 36.0f);
    const float makeup = 1.0f / std::sqrt (gain);
    driveToneL.setCutoff (800.0f * std::pow (20.0f, u.p[2]), sr);
    driveToneR.setCutoff (800.0f * std::pow (20.0f, u.p[2]), sr);
    const float mix = u.mix;
    auto shape = [mode] (float x) noexcept
    {
        switch (mode)
        {
            case 0: return fastTanh (x);                                                           // tape
            case 1: return x >= 0.0f ? fastTanh (x) : fastTanh (x * 0.6f) / 0.6f * 0.8f;            // tube (asymmetric)
            default: return clampf (x, -0.9f, 0.9f);                                               // hard clip
        }
    };
    for (int i = 0; i < n; ++i)
    {
        float yl, yr;
        if (os)
        {
            float a, b;
            osL.up (L[i] * gain, a, b); yl = osL.down (shape (a), shape (b));
            osR.up (R[i] * gain, a, b); yr = osR.down (shape (a), shape (b));
        }
        else { yl = shape (L[i] * gain); yr = shape (R[i] * gain); }
        yl = dcL.process (driveToneL.lp (yl)) * makeup * 1.4f;
        yr = dcR.process (driveToneR.lp (yr)) * makeup * 1.4f;
        L[i] += (yl - L[i]) * mix;
        R[i] += (yr - R[i]) * mix;
    }
}

void FxChain::crusher (float* L, float* R, int n, const FxUnitParams& u) noexcept
{
    const float bits = lerp (16.0f, 2.0f, u.p[0]);
    const float steps = std::exp2 (bits - 1.0f);
    const float factor = 1.0f + std::floor (u.p[1] * u.p[1] * 40.0f);
    for (int i = 0; i < n; ++i)
    {
        if (++crushCount >= factor)
        {
            crushCount = 0;
            crushHoldL = std::round (L[i] * steps) / steps;
            crushHoldR = std::round (R[i] * steps) / steps;
        }
        L[i] += (crushHoldL - L[i]) * u.mix;
        R[i] += (crushHoldR - R[i]) * u.mix;
    }
}

void FxChain::chorus (float* L, float* R, int n, const FxUnitParams& u, float mixOffset) noexcept
{
    const float rate = 0.05f + u.p[0] * u.p[0] * 6.0f;
    const float depth = (0.5f + u.p[1] * 6.0f) * 0.001f * sr;
    const float base = 0.012f * sr;
    const float mix = clampf ((u.on ? u.mix : 0.0f) + mixOffset, 0.0f, 1.0f);
    const float inc = rate / sr;
    for (int i = 0; i < n; ++i)
    {
        choL.push (L[i]); choR.push (R[i]);
        choPhase += inc; if (choPhase >= 1.0f) choPhase -= 1.0f;
        // Three-voice ensemble, voices offset by 120 degrees
        float wl = 0.0f, wr = 0.0f;
        for (int v = 0; v < 3; ++v)
        {
            const float ph = choPhase + (float) v / 3.0f;
            wl += choL.readCubic (base + depth * (1.0f + fastSin01 (ph)) * 0.5f);
            wr += choR.readCubic (base + depth * (1.0f + fastSin01 (ph + 0.25f)) * 0.5f);
        }
        wl *= 0.333f; wr *= 0.333f;
        L[i] = L[i] * (1.0f - mix * 0.5f) + wl * mix;
        R[i] = R[i] * (1.0f - mix * 0.5f) + wr * mix;
    }
}

void FxChain::phaser (float* L, float* R, int n, const FxUnitParams& u) noexcept
{
    const float rate = 0.02f + u.p[0] * u.p[0] * 5.0f;
    const float depth = u.p[1];
    const float fb = u.p[2] * 0.85f;
    const float inc = rate / sr;
    for (int i = 0; i < n; ++i)
    {
        phPhase += inc; if (phPhase >= 1.0f) phPhase -= 1.0f;
        const float lfoL = 0.5f + 0.5f * fastSin01 (phPhase);
        const float lfoR = 0.5f + 0.5f * fastSin01 (phPhase + 0.25f);
        const float fL = 200.0f * std::exp2 (lfoL * depth * 4.5f);
        const float fR = 200.0f * std::exp2 (lfoR * depth * 4.5f);
        const float tL = std::tan (kPi * std::min (fL, sr * 0.45f) / sr), aL = (tL - 1.0f) / (tL + 1.0f);
        const float tR = std::tan (kPi * std::min (fR, sr * 0.45f) / sr), aR = (tR - 1.0f) / (tR + 1.0f);
        float xl = L[i] + phFbL * fb, xr = R[i] + phFbR * fb;
        for (int s = 0; s < 6; ++s)
        {
            const float yl = aL * xl + apL[s]; apL[s] = undenormal (xl - aL * yl); xl = yl;
            const float yr = aR * xr + apR[s]; apR[s] = undenormal (xr - aR * yr); xr = yr;
        }
        phFbL = xl; phFbR = xr;
        L[i] += (0.5f * (L[i] + xl) - L[i]) * u.mix;
        R[i] += (0.5f * (R[i] + xr) - R[i]) * u.mix;
    }
}

void FxChain::flanger (float* L, float* R, int n, const FxUnitParams& u) noexcept
{
    const float rate = 0.02f + u.p[0] * u.p[0] * 3.0f;
    const float depth = (0.2f + u.p[1] * 5.0f) * 0.001f * sr;
    const float fb = (u.p[2] * 2.0f - 1.0f) * 0.9f;
    const float base = 0.0008f * sr;
    const float inc = rate / sr;
    for (int i = 0; i < n; ++i)
    {
        flaPhase += inc; if (flaPhase >= 1.0f) flaPhase -= 1.0f;
        const float dl = base + depth * (0.5f + 0.5f * fastSin01 (flaPhase));
        const float dr = base + depth * (0.5f + 0.5f * fastSin01 (flaPhase + 0.25f));
        const float wl = flaL.readCubic (dl), wr = flaR.readCubic (dr);
        flaL.push (undenormal (L[i] + wl * fb));
        flaR.push (undenormal (R[i] + wr * fb));
        L[i] += (0.5f * (L[i] + wl) - L[i]) * u.mix;
        R[i] += (0.5f * (R[i] + wr) - R[i]) * u.mix;
    }
}

void FxChain::delay (float* L, float* R, int n, const FxUnitParams& u, double bpm, float mixOffset) noexcept
{
    const int div = std::clamp ((int) (u.p[0] * 11.0f + 0.5f), 0, 11);
    const float seconds = std::min (5.5f, (float) (60.0 / std::max (bpm, 20.0)) * syncDivisionBeats (div));
    dlyTime.setTarget (seconds * sr);
    const float fb = u.p[1] * 0.92f;
    const bool ping = u.p[2] >= 0.5f;
    dlyLpL.setCutoff (1500.0f * std::pow (10.0f, u.p[3]), sr);
    dlyLpR.setCutoff (1500.0f * std::pow (10.0f, u.p[3]), sr);
    dlyHpL.setCutoff (120.0f, sr); dlyHpR.setCutoff (120.0f, sr);
    const float mix = clampf ((u.on ? u.mix : 0.0f) + mixOffset, 0.0f, 1.0f);
    for (int i = 0; i < n; ++i)
    {
        const float t = dlyTime.next();
        const float wl = dlyL.readCubic (t), wr = dlyR.readCubic (t);
        const float fl = dlyHpL.hp (dlyLpL.lp (wl)) * fb;
        const float fr = dlyHpR.hp (dlyLpR.lp (wr)) * fb;
        if (ping)
        {
            dlyL.push (undenormal ((L[i] + R[i]) * 0.5f + fr));
            dlyR.push (undenormal (fl));
        }
        else
        {
            dlyL.push (undenormal (L[i] + fl));
            dlyR.push (undenormal (R[i] + fr));
        }
        L[i] += wl * mix;
        R[i] += wr * mix;
    }
}

void FxChain::eq (float* L, float* R, int n, const FxUnitParams& u) noexcept
{
    const float lo = (u.p[0] - 0.5f) * 30.0f, mid = (u.p[1] - 0.5f) * 30.0f, hi = (u.p[3] - 0.5f) * 30.0f;
    const float midHz = 200.0f * std::pow (30.0f, u.p[2]);
    if (lo != eqCache[0] || mid != eqCache[1] || midHz != eqCache[2] || hi != eqCache[3])
    {
        eqCache[0] = lo; eqCache[1] = mid; eqCache[2] = midHz; eqCache[3] = hi;
        for (int c = 0; c < 2; ++c)
        {
            eqLow[c].setLowShelf (sr, 180.0f, lo);
            eqMid[c].setPeak (sr, midHz, 0.9f, mid);
            eqHigh[c].setHighShelf (sr, 6000.0f, hi);
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const float yl = eqHigh[0].tick (eqMid[0].tick (eqLow[0].tick (L[i])));
        const float yr = eqHigh[1].tick (eqMid[1].tick (eqLow[1].tick (R[i])));
        L[i] += (yl - L[i]) * u.mix;
        R[i] += (yr - R[i]) * u.mix;
    }
}

void FxChain::comp (float* L, float* R, int n, const FxUnitParams& u) noexcept
{
    const float threshDb = -40.0f + u.p[0] * 40.0f;
    const float ratio = 1.0f + u.p[1] * u.p[1] * 19.0f;
    const float rel = std::exp (-1.0f / ((0.02f + u.p[2] * 0.98f) * sr));
    const float att = std::exp (-1.0f / (0.004f * sr));
    const float makeup = dbToGain (u.p[3] * 24.0f);
    for (int i = 0; i < n; ++i)
    {
        const float peak = std::max (std::fabs (L[i]), std::fabs (R[i]));
        const float c = peak > compEnv ? att : rel;
        compEnv = undenormal (peak + c * (compEnv - peak));
        const float envDb = gainToDb (compEnv);
        float grDb = 0.0f;
        const float over = envDb - threshDb;
        const float knee = 6.0f;
        if (over > knee * 0.5f) grDb = over * (1.0f - 1.0f / ratio);
        else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; grDb = (1.0f - 1.0f / ratio) * x * x / (2.0f * knee); }
        const float g = dbToGain (-grDb) * makeup;
        L[i] += (L[i] * g - L[i]) * u.mix;
        R[i] += (R[i] * g - R[i]) * u.mix;
    }
}

void FxChain::exciterWidener (float* L, float* R, int n, float amount) noexcept
{
    const float drive = 1.0f + amount * 6.0f;
    for (int i = 0; i < n; ++i)
    {
        // Harmonic exciter: saturate the top octave band and blend it back in
        const float hl = excHpL.hp (L[i]), hr = excHpR.hp (R[i]);
        float l = L[i] + fastTanh (hl * drive) * 0.25f * amount;
        float r = R[i] + fastTanh (hr * drive) * 0.25f * amount;
        // Mid/side widening
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * (1.0f + amount * 0.8f);
        L[i] = mid + side;
        R[i] = mid - side;
    }
}

void FxChain::ambience (float* L, float* R, int n, float level) noexcept
{
    // "Round Ambience": distant wind and the occasional far-off groan, all synthesized.
    const float g = level * 0.12f;
    const float dt = 1.0f / sr;
    for (int i = 0; i < n; ++i)
    {
        windPhase += 0.07f * dt; if (windPhase >= 1.0f) windPhase -= 1.0f;
        const float gust = 0.55f + 0.45f * fastSin01 (windPhase) * fastSin01 (windPhase * 2.7f + 0.3f);
        brownL = undenormal (0.996f * brownL + 0.04f * rng.nextBipolar());
        brownR = undenormal (0.996f * brownR + 0.04f * rng.nextBipolar());
        float wl = windBpL.tick (brownL) * gust * 2.5f;
        float wr = windBpR.tick (brownR) * gust * 2.5f;

        float groan = 0.0f;
        if (groanAge < groanLen)
        {
            groanAge += dt;
            const float t = groanAge / groanLen;
            const float env = std::sin (kPi * t) * std::sin (kPi * t);
            const float hz = groanHz * (1.0f - 0.25f * t) * (1.0f + 0.03f * fastSin01 (groanAge * 5.5f));
            groanPhase += hz * dt; if (groanPhase >= 1.0f) groanPhase -= 1.0f;
            const float saw = 2.0f * groanPhase - 1.0f + 0.3f * rng.nextBipolar();
            if (((int) (groanAge * sr)) % 64 == 0) groanFormant.set (0.6f - 0.5f * t, 0.4f, 1.0f, sr);
            groan = groanLp.lp (groanFormant.tick (saw)) * env * 0.35f;
        }
        else
        {
            groanTimer -= dt;
            if (groanTimer <= 0.0f)
            {
                groanTimer = 5.0f + rng.nextUnipolar() * 9.0f;
                groanAge = 0.0f;
                groanLen = 1.4f + rng.nextUnipolar() * 1.6f;
                groanHz = 70.0f + rng.nextUnipolar() * 60.0f;
                groanPan = rng.nextBipolar() * 0.8f;
            }
        }
        wl += groan * (1.0f - groanPan) * 0.5f;
        wr += groan * (1.0f + groanPan) * 0.5f;
        L[i] += wl * g;
        R[i] += wr * g;
    }
}

void FxChain::limiter (float* L, float* R, int n) noexcept
{
    // Always-on output safety limiter, ceiling -0.3 dBFS. Instant attack, 80 ms release,
    // followed by a hard ceiling so nothing above -0.3 dBFS can ever leave the plugin.
    constexpr float ceiling = 0.96605f;
    for (int i = 0; i < n; ++i)
    {
        if (! std::isfinite (L[i])) L[i] = 0.0f;
        if (! std::isfinite (R[i])) R[i] = 0.0f;
        const float peak = std::max (std::fabs (L[i]), std::fabs (R[i]));
        const float target = peak > ceiling ? ceiling / peak : 1.0f;
        if (target < limGain) limGain = target;
        else limGain = target + limRelease * (limGain - target);
        L[i] = clampf (L[i] * limGain, -ceiling, ceiling);
        R[i] = clampf (R[i] * limGain, -ceiling, ceiling);
    }
}

} // namespace mbx
