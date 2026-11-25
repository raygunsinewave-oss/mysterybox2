// Mystery Box Synthesizer by Raygun
#include "SynthEngine.h"

namespace mbx
{
namespace
{
inline float oscParam (const EngineParams& p, P base, int osc) noexcept
{
    return p.v[(size_t) ((int) base + osc * kOscStride)];
}

inline OscControl makeOscControl (const EngineParams& p, int o, float pitchSemis, const float* mods,
                                  const EngineGlobals& g) noexcept
{
    OscControl c;
    c.mode = (int) (oscParam (p, P::o1Mode, o) + 0.5f);
    c.shape = (int) (oscParam (p, P::o1Shape, o) + 0.5f);
    c.a = clampf (oscParam (p, P::o1A, o) + mods[(int) ModDest::Osc1A + o], 0.0f, 1.0f);
    c.b = clampf (oscParam (p, P::o1B, o) + mods[(int) ModDest::Osc1B + o], 0.0f, 1.0f);
    c.c = clampf (oscParam (p, P::o1C, o) + mods[(int) ModDest::OscC], 0.0f, 1.0f);
    const float semis = pitchSemis + oscParam (p, P::o1Oct, o) * 12.0f + oscParam (p, P::o1Semi, o)
                      + oscParam (p, P::o1Fine, o) * 0.01f + mods[(int) ModDest::Osc1Pitch + o] * 24.0f;
    c.hz = clampf (midiToHz (semis, g.a4), 8.0f, 20000.0f);
    const float lvl = clampf (oscParam (p, P::o1Level, o) + mods[(int) ModDest::Osc1Level + o], 0.0f, 1.0f);
    c.level = lvl * lvl;
    c.pan = oscParam (p, P::o1Pan, o);
    c.unison = (int) (oscParam (p, P::o1Uni, o) + 0.5f);
    c.detuneCents = oscParam (p, P::o1Detune, o) * clampf (1.0f + mods[(int) ModDest::UnisonDetune], 0.0f, 4.0f);
    c.spread = clampf (oscParam (p, P::o1Spread, o) * (1.0f + mods[(int) ModDest::Spread]), 0.0f, 1.0f);
    if (g.overclock > 0.001f && c.mode <= (int) OscMode::FM)
    {
        // The Overclocker: rich unison detune and wider spread
        c.unison = std::min (Oscillator::kMaxUnison, c.unison + (int) std::lround (g.overclock * 4.0f));
        c.detuneCents += g.overclock * 22.0f;
        c.spread = std::max (c.spread, g.overclock * 0.85f);
    }
    return c;
}
} // namespace

//==============================================================================
void Voice::prepare (float sampleRate, uint32_t seed)
{
    sr = sampleRate;
    for (int i = 0; i < 3; ++i) osc[i].prepare (sr, seed * 3u + (uint32_t) i + 1u);
    const int combMax = (int) (sr / 18.0f) + 8;
    f1L.prepare (sr, combMax); f1R.prepare (sr, combMax);
    f2L.prepare (sr, combMax); f2R.prepare (sr, combMax);
    ampEnv.prepare (sr); filterEnv.prepare (sr); modEnv.prepare (sr);
    noiseRng = FastRandom (seed * 0x9E3779B1u + 7u);
    driveOsL.prepare(); driveOsR.prepare();
    for (auto& row : envCache) for (auto& x : row) x = -1.0f;
    kill();
}

void Voice::kill() noexcept
{
    active = gate = false;
    sustained = false;
    stealPending = false;
    stealFadeBlocks = 0;
    ampEnv.kill(); filterEnv.kill(); modEnv.kill();
    note = -1;
}

void Voice::start (int newNote, float vel, int ch, float startPitch, uint64_t newAge,
                   const EngineParams& p, const EngineGlobals& g, const PerformanceState& ps)
{
    const bool wasActive = active;
    note = newNote; channel = ch; age = newAge;
    velocity = clampf (vel, 0.0f, 1.0f);
    active = gate = true;
    sustained = false;
    polyPressure = 0.0f;
    randomValue = noiseRng.nextBipolar();
    targetPitch = (float) newNote;
    pitch = p[P::glide] > 0.0005f ? startPitch : targetPitch;

    if (! wasActive)
    {
        f1L.reset(); f1R.reset(); f2L.reset(); f2R.reset();
        driveOsL.reset(); driveOsR.reset();
        brown = noiseLpL = noiseLpR = 0.0f;
        for (auto& b : pinkB) b = 0.0f;
        lastAmpGain = 0.0f;
        subPhase = 0.0f;
    }
    for (int k = 0; k < 2; ++k)
    {
        const bool retrig = p.getBool (EngineParams::offset (P::lfo1Retrig, k * kLfoStride));
        lfo[k].reset (retrig ? 0.0f : noiseRng.nextUnipolar(), (uint32_t) (newAge * 31u + (uint64_t) k));
    }
    noiseEnv = 1.0f;

    // Compute modulation once so envelope-time macros apply from the first sample.
    computeMods (p, ps, mods);
    for (auto& row : envCache) for (auto& x : row) x = -1.0f;
    ampEnv.noteOn (! wasActive);
    filterEnv.noteOn (! wasActive);
    modEnv.noteOn (! wasActive);

    for (int o = 0; o < 3; ++o)
    {
        if (! p.getBool (EngineParams::offset (P::o1On, o * kOscStride))) continue;
        const auto c = makeOscControl (p, o, pitch, mods, g);
        if (wasActive && c.mode <= (int) OscMode::FM) continue; // keep phases running: no click on re-trigger
        osc[o].noteOn (c, velocity, (uint32_t) newAge * 3u + (uint32_t) o);
    }
}

void Voice::glideTo (int newNote, float vel) noexcept
{
    note = newNote;
    targetPitch = (float) newNote;
    velocity = std::max (velocity * 0.5f, clampf (vel, 0.0f, 1.0f));
    gate = true;
}

void Voice::release() noexcept
{
    gate = false;
    sustained = false;
    ampEnv.noteOff(); filterEnv.noteOff(); modEnv.noteOff();
}

void Voice::computeMods (const EngineParams& p, const PerformanceState& ps, float* dst) noexcept
{
    float src[(int) ModSource::count];
    const float pressure = std::max (polyPressure, ps.globalPressure);
    src[0] = 0.0f;
    src[(int) ModSource::Lfo1] = lfoValue[0];
    src[(int) ModSource::Lfo2] = lfoValue[1];
    src[(int) ModSource::EnvAmp] = ampEnv.getLevel();
    src[(int) ModSource::EnvFilter] = filterEnv.getLevel();
    src[(int) ModSource::EnvMod] = modEnv.getLevel();
    src[(int) ModSource::Velocity] = velocity;
    src[(int) ModSource::ModWheel] = ps.modWheel;
    src[(int) ModSource::Aftertouch] = std::max (pressure, ps.channelPressure[channel & 15]);
    src[(int) ModSource::KeyTrack] = ((float) note - 60.0f) / 60.0f;
    src[(int) ModSource::Random] = randomValue;
    src[(int) ModSource::Slide] = std::max (ps.globalSlide, ps.channelSlide[channel & 15]);
    for (int k = 0; k < 8; ++k)
        src[(int) ModSource::Macro1 + k] = ps.macros[k] - p.v[(size_t) ((int) P::mac1Val + k * kMacroStride)];

    for (int d = 0; d < (int) ModDest::count; ++d) dst[d] = 0.0f;
    for (int s = 0; s < 8; ++s)
    {
        const int base = (int) P::m1Src + s * kModStride;
        const int si = (int) (p.v[(size_t) base] + 0.5f);
        const int di = (int) (p.v[(size_t) base + 1] + 0.5f);
        const float amt = p.v[(size_t) base + 2];
        if (si <= 0 || di <= 0 || si >= (int) ModSource::count || di >= (int) ModDest::count) continue;
        dst[di] += amt * src[si];
    }
    for (int k = 0; k < 8; ++k)
    {
        const int base = (int) P::mac1Val + k * kMacroStride;
        const float v = src[(int) ModSource::Macro1 + k];
        const int d1 = (int) (p.v[(size_t) base + 1] + 0.5f), d2 = (int) (p.v[(size_t) base + 3] + 0.5f);
        if (d1 > 0 && d1 < (int) ModDest::count) dst[d1] += p.v[(size_t) base + 2] * v;
        if (d2 > 0 && d2 < (int) ModDest::count) dst[d2] += p.v[(size_t) base + 4] * v;
    }
}

void Voice::render (float* L, float* R, int n, const EngineParams& p, const EngineGlobals& g, const PerformanceState& ps) noexcept
{
    if (! active) return;
    const float dt = (float) n / sr;

    // --- LFOs (control rate)
    for (int k = 0; k < 2; ++k)
    {
        const int b = (int) P::lfo1Shape + k * kLfoStride;
        const int shape = (int) (p.v[(size_t) b] + 0.5f);
        float hz = p.v[(size_t) b + 1];
        if (p.v[(size_t) b + 2] >= 0.5f)
            hz = (float) (g.bpm / 60.0) / syncDivisionBeats ((int) (p.v[(size_t) b + 3] + 0.5f));
        hz *= std::exp2 (2.0f * clampf (mods[(int) ModDest::Lfo1Rate + k], -2.0f, 2.0f));
        const float depth = clampf (1.0f + 2.0f * mods[(int) ModDest::Lfo1Depth + k], 0.0f, 3.0f);
        lfoValue[k] = lfo[k].advance (shape, hz, dt, p.v[(size_t) b + 5]) * depth;
    }

    // --- Envelope parameters (recomputed only when they change)
    {
        const float atkMul = std::exp2 (3.0f * clampf (mods[(int) ModDest::AmpAttack], -1.5f, 1.5f));
        const float relMul = std::exp2 (3.0f * clampf (mods[(int) ModDest::AmpRelease], -1.5f, 1.5f));
        Adsr* envs[3] = { &ampEnv, &filterEnv, &modEnv };
        for (int e = 0; e < 3; ++e)
        {
            const int b = (int) P::ampA + e * kEnvStride;
            float vals[5] = { p.v[(size_t) b], p.v[(size_t) b + 1], p.v[(size_t) b + 2], p.v[(size_t) b + 3], p.v[(size_t) b + 4] };
            if (e == 0) { vals[0] = clampf (vals[0] * atkMul, 0.0005f, 20.0f); vals[3] = clampf (vals[3] * relMul, 0.001f, 30.0f); }
            if (std::memcmp (vals, envCache[e], sizeof (vals)) != 0)
            {
                std::memcpy (envCache[e], vals, sizeof (vals));
                envs[e]->setParameters (vals[0], vals[1], vals[2], vals[3], vals[4]);
            }
        }
    }
    const float fenv = filterEnv.advance (n);
    modEnv.advance (n);

    computeMods (p, ps, mods);

    // --- Pitch
    const float glideTime = p[P::glide];
    if (glideTime > 0.0005f)
        pitch += (targetPitch - pitch) * (1.0f - std::exp (-dt / (glideTime * 0.3f)));
    else
        pitch = targetPitch;
    float bendSemis;
    if (g.mpe && channel != 0)
        bendSemis = ps.channelBend[channel & 15] * g.mpeBendRange + ps.globalBend * g.bendRange;
    else
        bendSemis = ps.globalBend * g.bendRange;
    const float basePitch = pitch + bendSemis + mods[(int) ModDest::Pitch] * 24.0f;

    // --- Oscillators
    std::fill (bufL, bufL + n, 0.0f);
    std::fill (bufR, bufR + n, 0.0f);
    for (int o = 0; o < 3; ++o)
    {
        if (! p.getBool (EngineParams::offset (P::o1On, o * kOscStride))) continue;
        const auto c = makeOscControl (p, o, basePitch, mods, g);
        osc[o].render (c, bufL, bufR, n);
    }

    // --- Noise
    const float noiseLvl = clampf (p[P::noiseLevel] + mods[(int) ModDest::NoiseLevel], 0.0f, 1.0f);
    if (noiseLvl > 0.0001f)
    {
        const int type = p.getInt (P::noiseType);
        const float fc = 150.0f * std::pow (130.0f, p[P::noiseColor]);
        const float a = 1.0f - std::exp (-kTwoPi * std::min (fc, sr * 0.45f) / sr);
        const float nd = p[P::noiseDecay];
        const float decayMul = nd > 0.001f ? std::exp (-1.0f / (0.003f * std::pow (600.0f, nd) * sr)) : 1.0f;
        const float g2 = noiseLvl * noiseLvl * 0.7f;
        for (int i = 0; i < n; ++i)
        {
            const float w = noiseRng.nextBipolar();
            float x;
            if (type == 0) x = w;
            else if (type == 1)
            {
                pinkB[0] = 0.99886f * pinkB[0] + w * 0.0555179f; pinkB[1] = 0.99332f * pinkB[1] + w * 0.0750759f;
                pinkB[2] = 0.96900f * pinkB[2] + w * 0.1538520f; pinkB[3] = 0.86650f * pinkB[3] + w * 0.3104856f;
                pinkB[4] = 0.55000f * pinkB[4] + w * 0.5329522f; pinkB[5] = -0.7616f * pinkB[5] - w * 0.0168980f;
                x = (pinkB[0] + pinkB[1] + pinkB[2] + pinkB[3] + pinkB[4] + pinkB[5] + pinkB[6] + w * 0.5362f) * 0.2f;
                pinkB[6] = w * 0.115926f;
            }
            else { brown = undenormal (0.995f * brown + 0.06f * w); x = brown * 2.5f; }
            noiseLpL += a * (x - noiseLpL);
            noiseLpL = undenormal (noiseLpL);
            noiseEnv *= decayMul;
            const float y = noiseLpL * g2 * noiseEnv;
            bufL[i] += y; bufR[i] += y;
        }
    }

    // --- Sub oscillator
    const float subLvl = clampf (p[P::subLevel] + mods[(int) ModDest::SubLevel], 0.0f, 1.0f);
    if (subLvl > 0.0001f)
    {
        const float hz = midiToHz (basePitch + 12.0f * std::round (p[P::subOct]), g.a4);
        const float inc = clampf (hz / sr, 0.0f, 0.45f);
        const bool square = p.getBool (P::subShape);
        const float gs = subLvl * subLvl * 0.8f;
        for (int i = 0; i < n; ++i)
        {
            float y;
            if (square)
            {
                y = subPhase < 0.5f ? 0.6f : -0.6f;
                // PolyBLEP edges
                auto blep = [inc] (float t) { if (t < inc) { t /= inc; return t + t - t * t - 1.0f; } if (t > 1.0f - inc) { t = (t - 1.0f) / inc; return t * t + t + t + 1.0f; } return 0.0f; };
                float t2 = subPhase - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
                y += 0.6f * (blep (subPhase) - blep (t2));
            }
            else y = SineTable::get().lookup (subPhase);
            subPhase += inc; if (subPhase >= 1.0f) subPhase -= 1.0f;
            bufL[i] += y * gs; bufR[i] += y * gs;
        }
    }

    // --- Filters
    {
        const float keyOct = ((float) note - 60.0f) / 12.0f;
        const float envAmt1 = clampf (p[P::f1Env] + mods[(int) ModDest::FilterEnvAmt], -1.0f, 1.0f);
        const float oct1 = envAmt1 * fenv * 7.0f + p[P::f1Key] * keyOct + p[P::f1Vel] * 3.0f * (velocity - 0.5f)
                         + mods[(int) ModDest::F1Cutoff] * 6.0f;
        const float cut1 = clampf (p[P::f1Cutoff] * std::exp2 (oct1), 20.0f, 20000.0f);
        const float res1 = clampf (p[P::f1Res] + mods[(int) ModDest::F1Res], 0.0f, 1.0f);
        const int t1 = p.getInt (P::f1Type);
        f1L.set (t1, cut1, res1, p[P::f1Drive]);
        f1R.set (t1, cut1, res1, p[P::f1Drive]);
        const bool useF2 = p.getBool (P::f2On);
        if (useF2)
        {
            const float oct2 = p[P::f2Env] * fenv * 7.0f + p[P::f2Key] * keyOct + mods[(int) ModDest::F2Cutoff] * 6.0f;
            const float cut2 = clampf (p[P::f2Cutoff] * std::exp2 (oct2), 20.0f, 20000.0f);
            const float res2 = clampf (p[P::f2Res] + mods[(int) ModDest::F2Res], 0.0f, 1.0f);
            const int t2 = p.getInt (P::f2Type);
            f2L.set (t2, cut2, res2, 0.0f);
            f2R.set (t2, cut2, res2, 0.0f);
        }
        const bool parallel = p.getBool (P::filterRouting);
        if (useF2 && parallel)
        {
            float tl[kBlock], tr[kBlock];
            std::memcpy (tl, bufL, sizeof (float) * (size_t) n);
            std::memcpy (tr, bufR, sizeof (float) * (size_t) n);
            FilterSlot::processStereo (f1L, f1R, bufL, bufR, n);
            FilterSlot::processStereo (f2L, f2R, tl, tr, n);
            for (int i = 0; i < n; ++i) { bufL[i] = 0.7f * (bufL[i] + tl[i]); bufR[i] = 0.7f * (bufR[i] + tr[i]); }
        }
        else
        {
            FilterSlot::processStereo (f1L, f1R, bufL, bufR, n);
            if (useF2) FilterSlot::processStereo (f2L, f2R, bufL, bufR, n);
        }
    }

    // --- Voice saturation (optionally 2x oversampled)
    const float drive = clampf (p[P::voiceDrive] + mods[(int) ModDest::Drive], 0.0f, 1.0f);
    if (drive > 0.001f)
    {
        const float gain = 1.0f + drive * 7.0f;
        const float norm = 1.0f / (0.6f + 0.4f * std::sqrt (gain));
        for (int i = 0; i < n; ++i)
        {
            if (g.oversample)
            {
                float a, b;
                driveOsL.up (bufL[i] * gain, a, b); bufL[i] = driveOsL.down (fastTanh (a), fastTanh (b)) * norm;
                driveOsR.up (bufR[i] * gain, a, b); bufR[i] = driveOsR.down (fastTanh (a), fastTanh (b)) * norm;
            }
            else
            {
                bufL[i] = fastTanh (bufL[i] * gain) * norm;
                bufR[i] = fastTanh (bufR[i] * gain) * norm;
            }
        }
    }

    // --- Amplifier
    const float velGain = (1.0f - p[P::velAmp]) + p[P::velAmp] * velocity * velocity;
    const float ampMod = clampf (1.0f + mods[(int) ModDest::Amp], 0.0f, 2.0f);
    const float target = velGain * ampMod * dbToGain (p[P::presetGain]);
    float pl, pr;
    {
        const float pan = clampf (mods[(int) ModDest::Pan], -1.0f, 1.0f);
        const float x = (pan + 1.0f) * 0.125f;
        pl = fastSin01 (x + 0.25f) * 1.41421f; pr = fastSin01 (x) * 1.41421f;
    }
    const float dgain = (target - lastAmpGain) / (float) n;
    const float dpl = (pl - lastPanL) / (float) n, dpr = (pr - lastPanR) / (float) n;
    float gn = lastAmpGain, gl = lastPanL, gr = lastPanR;
    float fade = 1.0f, fadeStep = 0.0f;
    if (stealFadeBlocks > 0)
    {
        fade = (float) stealFadeBlocks / 4.0f;
        fadeStep = -0.25f / (float) n;
    }
    for (int i = 0; i < n; ++i)
    {
        const float env = ampEnv.next();
        gn += dgain; gl += dpl; gr += dpr;
        const float a = env * gn * fade;
        L[i] += bufL[i] * a * gl;
        R[i] += bufR[i] * a * gr;
        fade += fadeStep;
    }
    lastAmpGain = target; lastPanL = pl; lastPanR = pr;
    if (stealFadeBlocks > 0) --stealFadeBlocks;

    if (! ampEnv.isActive()) { active = false; gate = false; note = -1; }
}

//==============================================================================
SynthEngine::SynthEngine() = default;

void SynthEngine::prepare (double sampleRate, int)
{
    sr = (float) sampleRate;
    for (int i = 0; i < kMaxVoices; ++i) voices[(size_t) i].prepare (sr, (uint32_t) (i * 7919 + 13));
    reset();
}

void SynthEngine::reset() noexcept
{
    for (auto& v : voices) v.kill();
    std::memset (held, 0, sizeof (held));
    monoStackSize = 0;
    sustainPedal = false;
    fade = Fade::None; fadeGain = 1.0f;
    if (hasPending) { params = pendingParams; hasPending = false; }
}

void SynthEngine::setParams (const EngineParams& p, bool crossfade) noexcept
{
    if (! crossfade || getActiveVoiceCount() == 0)
    {
        params = p;
        params.sanitise();
        hasPending = false;
        if (fade == Fade::Out) { fade = Fade::In; }
        return;
    }
    pendingParams = p;
    pendingParams.sanitise();
    hasPending = true;
    fade = Fade::Out;
    fadeStep = 1.0f / (0.005f * sr);
}

void SynthEngine::setMacroValues (const float* values8) noexcept
{
    for (int i = 0; i < 8; ++i) perf.macros[i] = clampf (values8[i], 0.0f, 1.0f);
}

float SynthEngine::getGlobalOffset (ModDest d) const noexcept
{
    float sum = 0.0f;
    for (int k = 0; k < 8; ++k)
    {
        const int base = (int) P::mac1Val + k * kMacroStride;
        const float v = perf.macros[k] - params.v[(size_t) base];
        if ((int) (params.v[(size_t) base + 1] + 0.5f) == (int) d) sum += params.v[(size_t) base + 2] * v;
        if ((int) (params.v[(size_t) base + 3] + 0.5f) == (int) d) sum += params.v[(size_t) base + 4] * v;
    }
    return sum;
}

int SynthEngine::getActiveVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices) n += v.isActive() ? 1 : 0;
    return n;
}

int SynthEngine::findVoiceToSteal() noexcept
{
    const int maxPoly = std::clamp (globals.maxPolyphony, 1, kMaxVoices);
    int best = 0;
    if (globals.stealMode == 1) // quietest
    {
        float lowest = 1.0e9f;
        for (int i = 0; i < maxPoly; ++i)
            if (! voices[(size_t) i].stealPending && voices[(size_t) i].getLevel() < lowest) { lowest = voices[(size_t) i].getLevel(); best = i; }
        return best;
    }
    if (globals.stealMode == 2) // prefer released voices, oldest first
    {
        uint64_t oldest = ~0ull; int found = -1;
        for (int i = 0; i < maxPoly; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (v.isReleasing() && ! v.stealPending && v.age < oldest) { oldest = v.age; found = i; }
        }
        if (found >= 0) return found;
    }
    uint64_t oldest = ~0ull;
    for (int i = 0; i < maxPoly; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (! v.stealPending && v.age < oldest) { oldest = v.age; best = i; }
    }
    return best;
}

void SynthEngine::startVoice (int channel, int note, float velocity) noexcept
{
    const int maxPoly = std::clamp (globals.maxPolyphony, 1, kMaxVoices);
    const float startPitch = lastNotePitch;
    ++noteCounter;
    for (int i = 0; i < maxPoly; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.isActive() && ! v.stealPending)
        {
            v.start (note, velocity, channel, startPitch, noteCounter, params, globals, perf);
            return;
        }
    }
    auto& v = voices[(size_t) findVoiceToSteal()];
    v.stealPending = true;
    v.pendingNote = note; v.pendingChannel = channel; v.pendingVelocity = velocity;
    v.pendingStartPitch = startPitch; v.pendingAge = noteCounter;
    v.stealFadeBlocks = 4; // ~1.3 ms fade at 48 kHz
}

void SynthEngine::noteOn (int channel, int note, float velocity) noexcept
{
    channel &= 15; note &= 127;
    if (velocity <= 0.0f) { noteOff (channel, note); return; }
    held[channel][note] = (uint8_t) std::clamp ((int) (velocity * 127.0f + 0.5f), 1, 127);
    if (fade == Fade::Out) return; // held notes are re-triggered after the preset swap
    if (params.getBool (P::polyMono)) monoNoteOn (channel, note, velocity);
    else startVoice (channel, note, velocity);
    lastNotePitch = (float) note;
}

void SynthEngine::noteOff (int channel, int note) noexcept
{
    channel &= 15; note &= 127;
    held[channel][note] = 0;
    if (params.getBool (P::polyMono)) { monoNoteOff (channel, note); return; }
    for (auto& v : voices)
    {
        if (v.isActive() && v.isGateOn() && v.note == note && v.channel == channel)
        {
            if (sustainPedal) v.sustained = true;
            else v.release();
        }
    }
}

void SynthEngine::monoNoteOn (int channel, int note, float velocity) noexcept
{
    // Remove duplicates, push on top of the stack
    int w = 0;
    for (int i = 0; i < monoStackSize; ++i)
        if (! (monoStack[i] == note && monoStackChannel[i] == channel))
        { monoStack[w] = monoStack[i]; monoStackChannel[w] = monoStackChannel[i]; monoStackVel[w] = monoStackVel[i]; ++w; }
    monoStackSize = w;
    if (monoStackSize >= 32) { std::memmove (monoStack, monoStack + 1, sizeof (int) * 31); std::memmove (monoStackChannel, monoStackChannel + 1, sizeof (int) * 31); std::memmove (monoStackVel, monoStackVel + 1, sizeof (float) * 31); monoStackSize = 31; }
    monoStack[monoStackSize] = note; monoStackChannel[monoStackSize] = channel; monoStackVel[monoStackSize] = velocity; ++monoStackSize;

    auto& v = voices[0];
    ++noteCounter;
    if (v.isActive() && v.isGateOn() && params.getBool (P::legato))
        v.glideTo (note, velocity);
    else
        v.start (note, velocity, channel, v.isActive() ? v.getCurrentPitch() : lastNotePitch, noteCounter, params, globals, perf);
}

void SynthEngine::monoNoteOff (int channel, int note) noexcept
{
    int w = 0;
    for (int i = 0; i < monoStackSize; ++i)
        if (! (monoStack[i] == note && monoStackChannel[i] == channel))
        { monoStack[w] = monoStack[i]; monoStackChannel[w] = monoStackChannel[i]; monoStackVel[w] = monoStackVel[i]; ++w; }
    monoStackSize = w;
    auto& v = voices[0];
    if (! v.isActive() || v.note != note) return;
    if (monoStackSize == 0)
    {
        if (sustainPedal) v.sustained = true;
        else v.release();
        return;
    }
    const int top = monoStackSize - 1;
    if (params.getBool (P::legato)) v.glideTo (monoStack[top], monoStackVel[top]);
    else { ++noteCounter; v.start (monoStack[top], monoStackVel[top], monoStackChannel[top], v.getCurrentPitch(), noteCounter, params, globals, perf); }
}

void SynthEngine::pitchBend (int channel, float value) noexcept
{
    channel &= 15;
    perf.channelBend[channel] = clampf (value, -1.0f, 1.0f);
    if (! globals.mpe || channel == 0) perf.globalBend = clampf (value, -1.0f, 1.0f);
}

void SynthEngine::controller (int channel, int cc, float value01) noexcept
{
    channel &= 15;
    switch (cc)
    {
        case 1: perf.modWheel = value01; break;
        case 64:
        {
            const bool down = value01 >= 0.5f;
            if (sustainPedal && ! down)
                for (auto& v : voices)
                    if (v.isActive() && v.sustained) v.release();
            sustainPedal = down;
            break;
        }
        case 74:
            perf.channelSlide[channel] = value01;
            if (! globals.mpe) perf.globalSlide = value01;
            break;
        case 120: allNotesOff (true); break;
        case 123: allNotesOff (false); break;
        default: break;
    }
}

void SynthEngine::channelPressure (int channel, float v) noexcept
{
    channel &= 15;
    perf.channelPressure[channel] = clampf (v, 0.0f, 1.0f);
    if (! globals.mpe) perf.globalPressure = clampf (v, 0.0f, 1.0f);
}

void SynthEngine::polyPressure (int channel, int note, float v) noexcept
{
    for (auto& voice : voices)
        if (voice.isActive() && voice.note == note && voice.channel == (channel & 15))
            voice.polyPressure = clampf (v, 0.0f, 1.0f);
}

void SynthEngine::allNotesOff (bool hard) noexcept
{
    std::memset (held, 0, sizeof (held));
    monoStackSize = 0;
    sustainPedal = false;
    for (auto& v : voices)
    {
        if (hard) v.kill();
        else if (v.isActive()) v.release();
    }
}

void SynthEngine::retriggerHeldNotes() noexcept
{
    const bool mono = params.getBool (P::polyMono);
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (held[ch][n] > 0)
            {
                const float vel = (float) held[ch][n] / 127.0f;
                if (mono) monoNoteOn (ch, n, vel);
                else startVoice (ch, n, vel);
            }
}

void SynthEngine::renderSegment (float* L, float* R, int n) noexcept
{
    for (auto& v : voices)
    {
        if (! v.isActive()) continue;
        v.render (L, R, n, params, globals, perf);
        if (v.stealPending && v.stealFadeBlocks == 0)
        {
            const int note = v.pendingNote, ch = v.pendingChannel;
            v.kill();
            v.start (note, v.pendingVelocity, ch, v.pendingStartPitch, v.pendingAge, params, globals, perf);
            if (held[ch][note] == 0) // released while we were fading the stolen voice
            {
                if (sustainPedal) v.sustained = true;
                else v.release();
            }
        }
    }
}

void SynthEngine::render (float* L, float* R, int n) noexcept
{
    ScopedFlushDenormals noDenormals;
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (Voice::kBlock, n - pos);
        renderSegment (L + pos, R + pos, len);

        if (fade != Fade::None)
        {
            for (int i = 0; i < len; ++i)
            {
                if (fade == Fade::Out) fadeGain = std::max (0.0f, fadeGain - fadeStep);
                else if (fade == Fade::In) fadeGain = std::min (1.0f, fadeGain + fadeStep);
                L[pos + i] *= fadeGain;
                R[pos + i] *= fadeGain;
            }
            if (fade == Fade::Out && fadeGain <= 0.0f)
            {
                for (auto& v : voices) v.kill();
                if (hasPending) { params = pendingParams; hasPending = false; }
                monoStackSize = 0;
                retriggerHeldNotes();
                fade = Fade::In;
            }
            else if (fade == Fade::In && fadeGain >= 1.0f) fade = Fade::None;
        }
        pos += len;
    }
}

} // namespace mbx
