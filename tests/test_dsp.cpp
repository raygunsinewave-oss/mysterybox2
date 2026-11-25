// Mystery Box Synthesizer by Raygun -- DSP unit tests
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "dsp/Effects.h"
#include "dsp/Filters.h"
#include "dsp/Modulators.h"
#include "dsp/Oscillator.h"
#include "dsp/Wavetables.h"
#include <complex>
#include <vector>

using namespace mbx;

namespace
{
constexpr float kSr = 48000.0f;

/** Power spectrum via a naive DFT on selected bins (accurate, slow enough for tests). */
double binPower (const std::vector<float>& x, double hz)
{
    std::complex<double> acc = 0.0;
    const size_t n = x.size();
    for (size_t i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPiD * (double) i / (double) (n - 1)); // Hann
        acc += (double) x[i] * w * std::polar (1.0, -kTwoPiD * hz * (double) i / kSr);
    }
    return std::norm (acc);
}

/** Ratio (dB) of the strongest alias component to the fundamental for a periodic signal at f0. */
double worstAliasDb (const std::vector<float>& x, double f0)
{
    const double fund = binPower (x, f0);
    double worst = 0.0;
    // Aliases of harmonic k land at |k*f0 - m*sr|; probe a band of non-harmonic frequencies.
    for (int k = 1; k < 60; ++k)
    {
        double f = std::fmod (k * f0, (double) kSr);
        if (f > kSr / 2) f = kSr - f;
        const double nearestHarm = std::round (f / f0) * f0;
        if (std::fabs (f - nearestHarm) < 60.0) continue; // coincides with a real harmonic
        if (f < 100.0) continue;
        worst = std::max (worst, binPower (x, f));
    }
    return 10.0 * std::log10 (std::max (worst, 1e-30) / fund);
}

std::vector<float> renderOsc (int mode, int shape, float hz, int samples, float a = 0.5f)
{
    WavetableBank::get().build();
    Oscillator osc;
    osc.prepare (kSr, 1);
    OscControl c;
    c.mode = mode; c.shape = shape; c.hz = hz; c.level = 1.0f; c.unison = 1; c.a = a;
    osc.noteOn (c, 1.0f, 1);
    std::vector<float> l ((size_t) samples, 0.0f), r ((size_t) samples, 0.0f);
    for (int pos = 0; pos < samples; pos += 16)
        osc.render (c, l.data() + pos, r.data() + pos, std::min (16, samples - pos));
    return l;
}
} // namespace

TEST_CASE ("PolyBLEP saw keeps aliasing well below the fundamental", "[dsp][osc]")
{
    const auto x = renderOsc ((int) OscMode::VirtualAnalog, 0, 1760.0f, 8192);
    const double alias = worstAliasDb (x, 1760.0);
    INFO ("worst alias " << alias << " dB");
    REQUIRE (alias < -30.0);
}

TEST_CASE ("Band-limited wavetables are essentially alias-free", "[dsp][osc]")
{
    for (int table : { 0, 11, 24, 47, 63 })
    {
        const auto x = renderOsc ((int) OscMode::Wavetable, table, 2093.0f, 8192, 0.7f);
        const double alias = worstAliasDb (x, 2093.0);
        INFO ("table " << table << " worst alias " << alias << " dB");
        REQUIRE (alias < -45.0);
    }
}

TEST_CASE ("All oscillator modes produce finite, bounded output across the keyboard", "[dsp][osc]")
{
    for (int mode = 0; mode <= (int) OscMode::Additive; ++mode)
        for (float hz : { 27.5f, 110.0f, 880.0f, 4186.0f, 12000.0f })
        {
            const auto x = renderOsc (mode, mode == 4 ? 2 : 3, hz, 4800);
            float peak = 0.0f;
            for (float v : x) { REQUIRE (std::isfinite (v)); peak = std::max (peak, std::fabs (v)); }
            INFO ("mode " << mode << " hz " << hz << " peak " << peak);
            REQUIRE (peak < 8.0f);
        }
}

TEST_CASE ("Filters remain stable at extreme settings", "[dsp][filter]")
{
    FastRandom rng (7);
    for (int type = 0; type < 8; ++type)
        for (float cutoff : { 20.0f, 200.0f, 5000.0f, 19000.0f, 20000.0f })
            for (float res : { 0.0f, 0.7f, 1.0f })
                for (float drive : { 0.0f, 1.0f })
                {
                    FilterSlot f;
                    f.prepare (kSr, 4096);
                    f.set (type, cutoff, res, drive);
                    float peak = 0.0f;
                    float buf[64];
                    for (int blk = 0; blk < 1500; ++blk)
                    {
                        for (float& s : buf) s = blk < 20 ? rng.nextBipolar() * 2.0f : (blk < 750 ? 0.5f : 0.0f);
                        f.process (buf, 64);
                        for (float s : buf) { REQUIRE (std::isfinite (s)); peak = std::max (peak, std::fabs (s)); }
                    }
                    INFO ("type " << type << " cutoff " << cutoff << " res " << res << " drive " << drive << " peak " << peak);
                    REQUIRE (peak < 50.0f);
                }
}

TEST_CASE ("Ladder lowpass attenuates above cutoff", "[dsp][filter]")
{
    FilterSlot f;
    f.prepare (kSr, 4096);
    f.set ((int) FilterType::Ladder24, 500.0f, 0.0f, 0.0f);
    auto measure = [&] (float hz)
    {
        f.reset();
        f.set ((int) FilterType::Ladder24, 500.0f, 0.0f, 0.0f);
        double sum = 0; float buf[48];
        for (int blk = 0; blk < 2000; ++blk)
        {
            for (int i = 0; i < 48; ++i) buf[i] = 0.1f * std::sin (kTwoPi * hz * (float) (blk * 48 + i) / kSr);
            f.process (buf, 48);
            if (blk > 1000) for (float s : buf) sum += (double) s * s;
        }
        return sum;
    };
    const double low = measure (100.0f), high = measure (8000.0f);
    REQUIRE (10.0 * std::log10 (high / low) < -60.0); // 4 octaves above a 24 dB/oct cutoff
}

TEST_CASE ("ADSR timing is accurate", "[dsp][env]")
{
    Adsr env;
    env.prepare (kSr);
    env.setParameters (0.1f, 0.2f, 0.5f, 0.3f, 0.5f);
    env.noteOn (true);
    int attackSamples = 0;
    while (env.getStage() == Adsr::Stage::Attack && attackSamples < 48000) { env.next(); ++attackSamples; }
    REQUIRE (std::abs (attackSamples - 4800) < 480); // within 10 %
    for (int i = 0; i < 48000; ++i) env.next();
    REQUIRE_THAT (env.getLevel(), Catch::Matchers::WithinAbs (0.5, 0.01));
    env.noteOff();
    int releaseSamples = 0;
    while (env.isActive() && releaseSamples < 480000) { env.next(); ++releaseSamples; }
    REQUIRE (releaseSamples > 4800);
    REQUIRE (releaseSamples < 48000);
    REQUIRE (env.getLevel() == 0.0f);
}

TEST_CASE ("Output limiter never exceeds -0.3 dBFS", "[dsp][fx]")
{
    FxChain chain;
    chain.prepare (kSr, 512);
    FxParams p;
    p.unit[(int) FxId::Drive].on = true;
    p.unit[(int) FxId::Drive].mix = 1.0f;
    p.unit[(int) FxId::Drive].p[1] = 1.0f;
    p.unit[(int) FxId::Reverb].on = true;
    p.unit[(int) FxId::Reverb].mix = 0.8f;
    p.unit[(int) FxId::Delay].on = true;
    p.unit[(int) FxId::Delay].mix = 1.0f;
    p.unit[(int) FxId::Delay].p[1] = 1.0f;
    FastRandom rng (3);
    float l[512], r[512];
    const float ceiling = dbToGain (-0.3f) + 1.0e-5f;
    for (int blk = 0; blk < 400; ++blk)
    {
        for (int i = 0; i < 512; ++i) { l[i] = rng.nextBipolar() * 8.0f; r[i] = rng.nextBipolar() * 8.0f; }
        chain.process (l, r, 512, p);
        for (int i = 0; i < 512; ++i)
        {
            REQUIRE (std::fabs (l[i]) <= ceiling);
            REQUIRE (std::fabs (r[i]) <= ceiling);
        }
    }
}

TEST_CASE ("Every FX unit is stable and finite in every chain order", "[dsp][fx]")
{
    FxChain chain;
    chain.prepare (96000.0, 512);
    FxParams p;
    for (auto& u : p.unit) { u.on = true; u.mix = 0.7f; for (float& x : u.p) x = 0.9f; }
    p.order = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
    p.ambienceOn = true;
    p.overclock = 1.0f;
    float l[256], r[256];
    for (int blk = 0; blk < 2000; ++blk)
    {
        for (int i = 0; i < 256; ++i) l[i] = r[i] = (blk < 100) ? 0.5f * std::sin ((float) (blk * 256 + i) * 0.05f) : 0.0f;
        chain.process (l, r, 256, p);
        for (int i = 0; i < 256; ++i) { REQUIRE (std::isfinite (l[i])); REQUIRE (std::isfinite (r[i])); }
    }
}
