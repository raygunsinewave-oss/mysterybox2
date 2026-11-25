// Mystery Box Synthesizer by Raygun
#include "SfxEngine.h"

namespace mbx
{
namespace
{
constexpr float kDur[(int) Sfx::count] = {
    0.35f, 0.9f, 0.08f, 1.3f, 0.12f, 0.8f, 0.45f, 0.45f,
    0.9f, 1.1f, 1.4f, 1.3f, 1.6f, 1.8f, 0.45f, 0.7f,
    2.8f, 2.6f, 2.0f, 1.8f, 3.2f, 1.1f, 0.05f, 1.0f, 0.25f };

inline float expEnv (float t, float tau) noexcept { return std::exp (-t / tau); }
inline float sine (float& ph, float hz, float invSr) noexcept
{
    const float y = fastSin01 (ph);
    ph += hz * invSr; if (ph >= 1.0f) ph -= std::floor (ph);
    return y;
}
inline float saw (float& ph, float hz, float invSr) noexcept
{
    const float y = 2.0f * ph - 1.0f;
    ph += hz * invSr; if (ph >= 1.0f) ph -= std::floor (ph);
    return y;
}
inline float square (float& ph, float hz, float invSr) noexcept
{
    const float y = ph < 0.5f ? 1.0f : -1.0f;
    ph += hz * invSr; if (ph >= 1.0f) ph -= std::floor (ph);
    return y;
}
/** Music-box tine: fundamental plus inharmonic overtones, short bright decay. */
inline float tine (float* ph, float hz, float t, float invSr) noexcept
{
    return sine (ph[0], hz, invSr) * expEnv (t, 0.55f)
         + 0.35f * sine (ph[1], hz * 3.01f, invSr) * expEnv (t, 0.18f)
         + 0.15f * sine (ph[2], hz * 5.43f, invSr) * expEnv (t, 0.07f);
}
} // namespace

void SfxEngine::prepare (double sampleRate)
{
    sr = (float) sampleRate;
    invSr = 1.0f / sr;
    stopAll();
}

void SfxEngine::stopAll() noexcept
{
    for (auto& v : voices) v.active = false;
}

int SfxEngine::activeCount() const noexcept
{
    int n = 0;
    for (const auto& v : voices) n += v.active ? 1 : 0;
    return n;
}

void SfxEngine::trigger (const SfxEvent& e) noexcept
{
    // Take a free voice, otherwise the oldest one
    Voice* target = nullptr;
    float oldest = -1.0f;
    for (auto& v : voices)
    {
        if (! v.active) { target = &v; break; }
        if (v.t > oldest) { oldest = v.t; target = &v; }
    }
    if (target == nullptr) return;
    auto& v = *target;
    v = Voice();
    v.active = true;
    v.ev = e;
    v.t = 0.0f;
    v.dur = kDur[(int) e.type];
    v.rng = FastRandom (++seedCounter * 2654435761u);
    v.formant.reset();
}

float SfxEngine::synth (Voice& v) noexcept
{
    const float t = v.t;
    auto& r = v.rng;
    const float hzNote = midiToHz (v.ev.note);
    switch (v.ev.type)
    {
        case Sfx::Gunshot:
        {
            const float n = r.nextBipolar();
            v.svf.set (2600.0f * expEnv (t, 0.05f) + 400.0f, 0.9f, sr);
            const float body = v.svf.tick (n).bp * expEnv (t, 0.045f) * 2.2f;
            const float crack = (n - v.lp) * expEnv (t, 0.004f) * 1.4f; v.lp = n;
            const float thump = sine (v.ph[0], 140.0f * expEnv (t, 0.03f) + 45.0f, invSr) * expEnv (t, 0.06f) * 1.3f;
            const float tail = v.svf2.tick (n).lp * expEnv (t, 0.12f) * 0.4f;
            if (t < 0.001f) v.svf2.set (900.0f, 0.7f, sr);
            return fastTanh ((body + crack + thump + tail) * 1.4f) * 0.8f;
        }
        case Sfx::Headshot:
        {
            const float n = r.nextBipolar();
            v.svf.set (5200.0f, 3.0f, sr);
            return v.svf.tick (n).bp * expEnv (t, 0.03f) * 1.4f + sine (v.ph[0], 1800.0f, invSr) * expEnv (t, 0.05f) * 0.3f;
        }
        case Sfx::Reload:
        {
            // Magazine out (0), slide (0.25..0.5), magazine in (0.55), rack (0.75)
            float y = 0.0f;
            const float hits[3] = { 0.0f, 0.55f, 0.75f };
            v.svf.set (3200.0f, 6.0f, sr);
            float exc = 0.0f;
            for (float h : hits) if (t >= h && t < h + 0.012f) exc += r.nextBipolar();
            y += v.svf.tick (exc).bp * 1.6f;
            if (t > 0.25f && t < 0.5f) { v.svf2.set (1800.0f + (t - 0.25f) * 4000.0f, 2.0f, sr); y += v.svf2.tick (r.nextBipolar()).bp * 0.15f; }
            return y;
        }
        case Sfx::DryFire:
        {
            v.svf.set (4200.0f, 8.0f, sr);
            return v.svf.tick (t < 0.003f ? r.nextBipolar() : 0.0f).bp * 2.0f;
        }
        case Sfx::ZombieGroan:
        case Sfx::ZombieDeath:
        {
            const bool death = v.ev.type == Sfx::ZombieDeath;
            const float base = (death ? 120.0f : 85.0f) * (0.8f + 0.4f * std::fabs (v.ev.note - 60.0f) / 24.0f);
            const float hz = base * (1.0f - (death ? 0.45f : 0.15f) * t / v.dur) * (1.0f + 0.05f * fastSin01 (t * 6.3f));
            const float src = saw (v.ph[0], hz, invSr) + 0.35f * r.nextBipolar();
            if ((v.step++ & 31) == 0) v.formant.set (0.55f + 0.35f * fastSin01 (t * 0.9f), 0.5f, death ? 1.1f : 0.9f, sr);
            const float env = std::sin (kPi * std::min (1.0f, t / v.dur)) * (death ? expEnv (t, 0.35f) * 1.5f : 1.0f);
            return v.formant.tick (src) * env * 0.5f;
        }
        case Sfx::ZombieHit:
        {
            const float n = r.nextBipolar();
            v.lp += 0.15f * (n - v.lp);
            return (sine (v.ph[0], 200.0f * expEnv (t, 0.03f) + 60.0f, invSr) * 0.9f + v.lp * 1.5f) * expEnv (t, 0.035f);
        }
        case Sfx::BoardRip:
        {
            const float crackle = (r.nextUnipolar() > 0.985f ? r.nextBipolar() * 3.0f : 0.0f);
            v.svf.set (700.0f + 900.0f * r.nextUnipolar(), 4.0f, sr);
            const float wood = v.svf.tick (crackle + r.nextBipolar() * 0.2f).bp;
            return wood * expEnv (t, 0.18f) * 1.2f;
        }
        case Sfx::BoardRebuild:
        {
            const float knocks[3] = { 0.0f, 0.13f, 0.26f };
            float y = 0.0f;
            for (int k = 0; k < 3; ++k)
                if (t >= knocks[k])
                {
                    const float tk = t - knocks[k];
                    y += fastSin01 (tk * 240.0f) * expEnv (tk, 0.025f) * 0.8f;
                }
            v.svf.set (1100.0f, 3.0f, sr);
            float exc = 0.0f;
            for (float k : knocks) if (t >= k && t < k + 0.004f) exc = r.nextBipolar();
            return y + v.svf.tick (exc).bp * 1.5f;
        }
        case Sfx::BoxCreak:
        {
            // Stick-slip creak: irregular pulse train through a sweeping resonator
            const float rate = 55.0f + 35.0f * fastSin01 (t * 1.7f) + 10.0f * r.nextBipolar();
            v.ph[0] += rate * invSr;
            float pulse = 0.0f;
            if (v.ph[0] >= 1.0f) { v.ph[0] -= 1.0f; pulse = 1.0f; }
            v.svf.set (500.0f + 900.0f * (t / v.dur), 12.0f, sr);
            v.svf2.set (1300.0f + 600.0f * (t / v.dur), 10.0f, sr);
            const float env = std::sin (kPi * std::min (1.0f, t / v.dur));
            return (v.svf.tick (pulse).bp + 0.6f * v.svf2.tick (pulse).bp) * env * 2.5f;
        }
        case Sfx::BoxChime:
            return tine (v.ph, hzNote, t, invSr) * 0.55f;
        case Sfx::BoxReveal:
        {
            const float sweep = r.nextBipolar();
            v.svf.set (800.0f + 6000.0f * (t / v.dur), 1.5f, sr);
            const float whoosh = v.svf.tick (sweep).bp * std::sin (kPi * std::min (1.0f, t / v.dur)) * 0.35f;
            float chord = tine (v.ph, hzNote, t, invSr);
            if (t > 0.08f) chord += 0.8f * fastSin01 ((t - 0.08f) * hzNote * 1.498f) * expEnv (t - 0.08f, 0.6f);
            if (t > 0.16f) chord += 0.6f * fastSin01 ((t - 0.16f) * hzNote * 2.0f) * expEnv (t - 0.16f, 0.7f);
            return whoosh + chord * 0.4f;
        }
        case Sfx::WonderRay:
        {
            // Rising FM "ray" with a wobbling index and a ring-modulated shimmer
            const float hz = 300.0f + 1500.0f * (1.0f - expEnv (t, 0.25f));
            const float idx = 3.0f + 2.5f * fastSin01 (t * 9.0f);
            const float m = fastSin01 (v.ph[1]); v.ph[1] += hz * 1.5f * invSr; if (v.ph[1] >= 1.0f) v.ph[1] -= 1.0f;
            float cp = v.ph[0] + idx * m * 0.159f; cp -= std::floor (cp);
            v.ph[0] += hz * invSr; if (v.ph[0] >= 1.0f) v.ph[0] -= 1.0f;
            const float ring = fastSin01 (cp) * (0.6f + 0.4f * fastSin01 (t * 37.0f));
            return ring * std::sin (kPi * std::min (1.0f, t / v.dur)) * 0.5f;
        }
        case Sfx::MascotLaugh:
        {
            // Tin-robot "ha ha ha ha": formant syllables, ring-modulated for a metallic voice
            const float syll = t * 5.0f;
            const int s = (int) syll;
            const float ft = syll - (float) s;
            const float env = (s < 6) ? std::sin (kPi * std::min (1.0f, ft * 1.4f)) * (ft < 0.7f ? 1.0f : 0.0f) : 0.0f;
            const float hz = 260.0f - 12.0f * (float) s + 25.0f * (1.0f - ft);
            const float src = saw (v.ph[0], hz, invSr);
            if ((v.step++ & 31) == 0) v.formant.set (0.05f + 0.1f * ft, 0.7f, 1.25f, sr);
            const float ring = fastSin01 (v.ph[1]); v.ph[1] += 73.0f * invSr; if (v.ph[1] >= 1.0f) v.ph[1] -= 1.0f;
            return v.formant.tick (src) * (0.6f + 0.4f * ring) * env * 0.6f;
        }
        case Sfx::BoxTeleport:
        {
            const float n = r.nextBipolar();
            v.svf.set (200.0f + 8000.0f * (t / v.dur) * (t / v.dur), 2.0f, sr);
            const float whoosh = v.svf.tick (n).bp * std::sin (kPi * std::min (1.0f, t / v.dur));
            const float down = sine (v.ph[0], 900.0f * expEnv (t, 0.5f) + 60.0f, invSr) * expEnv (t, 0.8f) * 0.4f;
            const float sparkle = (r.nextUnipolar() > 0.997f ? 1.0f : 0.0f);
            v.svf2.set (6000.0f, 20.0f, sr);
            return whoosh * 0.6f + down + v.svf2.tick (sparkle).bp * 2.0f;
        }
        case Sfx::PowerUpDrop:
        {
            const int k = std::min (2, (int) (t / 0.09f));
            const float semis[3] = { 0.0f, 7.0f, 12.0f };
            const float tk = t - 0.09f * (float) k;
            return sine (v.ph[0], hzNote * std::exp2 (semis[k] / 12.0f), invSr) * expEnv (tk, 0.12f) * 0.5f;
        }
        case Sfx::PowerUpGrab:
        {
            float y = 0.0f;
            const float ratios[3] = { 1.0f, 1.26f, 1.498f };
            for (int k = 0; k < 3; ++k) y += square (v.ph[k], hzNote * ratios[k] * (1.0f + 0.3f * t), invSr);
            v.lp += 0.2f * (y - v.lp);
            return v.lp * expEnv (t, 0.25f) * 0.18f;
        }
        case Sfx::Nuke:
        {
            const float n = r.nextBipolar();
            v.lp += (0.02f + 0.1f * expEnv (t, 0.2f)) * (n - v.lp);
            const float boom = sine (v.ph[0], 70.0f * expEnv (t, 0.4f) + 30.0f, invSr) * expEnv (t, 0.9f);
            return fastTanh ((v.lp * 3.0f * expEnv (t, 1.0f) + boom) * 1.5f) * 0.8f;
        }
        case Sfx::RoundStart:
        case Sfx::RoundEnd:
        {
            const bool end = v.ev.type == Sfx::RoundEnd;
            const float a = 55.0f, b = end ? 55.0f * 0.749f : 55.0f * 1.189f;
            const float hz = t < 0.9f ? a : b;
            const float src = saw (v.ph[0], hz, invSr) + saw (v.ph[1], hz * 1.004f, invSr) + 0.5f * saw (v.ph[2], hz * 2.0f, invSr);
            const float env = std::min (1.0f, t * 3.0f) * expEnv (std::max (0.0f, t - 1.2f), 0.5f);
            v.svf.set (300.0f + 900.0f * env, 2.0f, sr);
            const float bell = t > 1.2f ? fastSin01 ((t - 1.2f) * 440.0f) * expEnv (t - 1.2f, 0.4f) * 0.25f : 0.0f;
            return v.svf.tick (src).lp * env * 0.35f + bell;
        }
        case Sfx::PlayerDown:
        {
            float y = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float tk = t - 0.45f * (float) k;
                if (tk >= 0.0f) y += fastSin01 (tk * 55.0f) * expEnv (tk, 0.08f) + 0.6f * (tk > 0.12f ? fastSin01 ((tk - 0.12f) * 50.0f) * expEnv (tk - 0.12f, 0.07f) : 0.0f);
            }
            const float drone = saw (v.ph[0], 41.2f, invSr);
            v.lp += 0.01f * (drone - v.lp);
            return y * 0.7f + v.lp * 0.4f * std::min (1.0f, t);
        }
        case Sfx::EggSting:
        {
            // Spooky minor music-box run over a swelling low drone
            static constexpr float run[6] = { 0.0f, 3.0f, 7.0f, 11.0f, 14.0f, 6.0f };
            float y = 0.0f;
            for (int k = 0; k < 6; ++k)
            {
                const float tk = t - 0.18f * (float) k;
                if (tk >= 0.0f)
                {
                    const float hz = midiToHz (72.0f + run[k]);
                    y += (fastSin01 (tk * hz) + 0.3f * fastSin01 (tk * hz * 3.01f) * expEnv (tk, 0.15f)) * expEnv (tk, 0.7f) * 0.35f;
                }
            }
            const float drone = saw (v.ph[0], 36.7f, invSr) + saw (v.ph[1], 36.9f * 1.5f, invSr);
            v.svf.set (200.0f + 500.0f * std::min (1.0f, t * 0.6f), 3.0f, sr);
            const float swell = v.svf.tick (drone).lp * std::sin (kPi * std::min (1.0f, t / v.dur)) * 0.45f;
            return y + swell;
        }
        case Sfx::KonamiFanfare:
        {
            static constexpr float notes[5] = { 0.0f, 4.0f, 7.0f, 12.0f, 16.0f };
            const int k = std::min (4, (int) (t / 0.11f));
            const float tk = t - 0.11f * (float) k;
            const float hz = midiToHz (72.0f + notes[k]);
            return square (v.ph[0], hz, invSr) * (k == 4 ? expEnv (tk, 0.4f) : expEnv (tk, 0.09f)) * 0.22f;
        }
        case Sfx::UiClick:
        {
            v.svf.set (2500.0f, 4.0f, sr);
            return v.svf.tick (t < 0.002f ? r.nextBipolar() : 0.0f).bp * 0.8f;
        }
        case Sfx::OverclockZap:
        {
            const float buzz = square (v.ph[0], 60.0f + 900.0f * (t / v.dur), invSr) * (r.nextUnipolar() > 0.3f ? 1.0f : 0.2f);
            v.svf.set (1500.0f + 3000.0f * (t / v.dur), 4.0f, sr);
            const float up = sine (v.ph[1], 220.0f * std::exp2 (2.0f * t / v.dur), invSr);
            return (v.svf.tick (buzz).bp * 0.4f + up * 0.3f) * std::sin (kPi * std::min (1.0f, t / v.dur));
        }
        default: return 0.0f;
    }
}

void SfxEngine::render (float* L, float* R, int n, float masterGain) noexcept
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const float pan = (clampf (v.ev.pan, -1.0f, 1.0f) + 1.0f) * 0.125f;
        const float gl = fastSin01 (pan + 0.25f) * v.ev.gain * masterGain;
        const float gr = fastSin01 (pan) * v.ev.gain * masterGain;
        for (int i = 0; i < n; ++i)
        {
            float y = synth (v);
            // 5 ms fade at the very end to avoid clicks
            const float remain = v.dur - v.t;
            if (remain < 0.005f) y *= std::max (0.0f, remain / 0.005f);
            L[i] += y * gl;
            R[i] += y * gr;
            v.t += invSr;
            if (v.t >= v.dur) { v.active = false; break; }
        }
    }
}

} // namespace mbx
