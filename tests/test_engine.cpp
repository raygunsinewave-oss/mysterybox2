// Mystery Box Synthesizer by Raygun -- engine unit tests
#include <catch2/catch_test_macros.hpp>
#include "engine/SfxEngine.h"
#include "engine/SynthEngine.h"
#include <vector>

using namespace mbx;

namespace
{
EngineParams sawPatch()
{
    EngineParams p;
    p[P::o1On] = 1; p[P::o1Mode] = 0; p[P::o1Shape] = 0; p[P::o1Level] = 0.8f;
    p[P::f1Type] = 1; p[P::f1Cutoff] = 3000.0f;
    p[P::ampA] = 0.001f; p[P::ampR] = 0.05f;
    return p;
}

float renderPeak (SynthEngine& e, int samples)
{
    std::vector<float> l ((size_t) samples), r ((size_t) samples);
    e.render (l.data(), r.data(), samples);
    float peak = 0;
    for (int i = 0; i < samples; ++i) peak = std::max ({ peak, std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]) });
    return peak;
}
} // namespace

TEST_CASE ("Engine renders notes and releases them", "[engine]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    e.setParams (sawPatch(), false);
    REQUIRE (renderPeak (e, 512) == 0.0f);
    e.noteOn (0, 60, 1.0f);
    REQUIRE (renderPeak (e, 4800) > 0.01f);
    REQUIRE (e.getActiveVoiceCount() == 1);
    e.noteOff (0, 60);
    renderPeak (e, 48000);
    REQUIRE (e.getActiveVoiceCount() == 0);
}

TEST_CASE ("Polyphony limit and voice stealing", "[engine]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    EngineGlobals g; g.maxPolyphony = 4;
    e.setGlobals (g);
    e.setParams (sawPatch(), false);
    for (int n = 0; n < 10; ++n) { e.noteOn (0, 48 + n, 0.8f); renderPeak (e, 256); }
    REQUIRE (e.getActiveVoiceCount() <= 4);
    REQUIRE (e.getActiveVoiceCount() >= 3);
}

TEST_CASE ("Mono legato keeps a single voice", "[engine]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    auto p = sawPatch();
    p[P::polyMono] = 1; p[P::legato] = 1; p[P::glide] = 0.05f;
    e.setParams (p, false);
    e.noteOn (0, 60, 1.0f); renderPeak (e, 512);
    e.noteOn (0, 64, 1.0f); renderPeak (e, 512);
    e.noteOn (0, 67, 1.0f); renderPeak (e, 512);
    REQUIRE (e.getActiveVoiceCount() == 1);
    e.noteOff (0, 67); e.noteOff (0, 64); e.noteOff (0, 60);
    renderPeak (e, 48000);
    REQUIRE (e.getActiveVoiceCount() == 0);
}

TEST_CASE ("Sustain pedal holds notes until released", "[engine]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    e.setParams (sawPatch(), false);
    e.controller (0, 64, 1.0f);
    e.noteOn (0, 60, 1.0f);
    renderPeak (e, 512);
    e.noteOff (0, 60);
    renderPeak (e, 24000);
    REQUIRE (e.getActiveVoiceCount() == 1);
    e.controller (0, 64, 0.0f);
    renderPeak (e, 24000);
    REQUIRE (e.getActiveVoiceCount() == 0);
}

TEST_CASE ("Preset swap crossfade is click-free and re-triggers held notes", "[engine]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    e.setParams (sawPatch(), false);
    e.noteOn (0, 60, 1.0f);
    renderPeak (e, 4800);
    auto p2 = sawPatch();
    p2[P::o1Shape] = 1;
    e.setParams (p2, true);
    std::vector<float> l (4800), r (4800);
    e.render (l.data(), r.data(), 4800);
    float maxStep = 0;
    for (size_t i = 1; i < l.size(); ++i) maxStep = std::max (maxStep, std::fabs (l[i] - l[i - 1]));
    REQUIRE (maxStep < 0.25f);
    REQUIRE (e.getActiveVoiceCount() == 1);       // held note re-triggered with the new preset
    REQUIRE (e.getParams()[P::o1Shape] == 1.0f);
}

TEST_CASE ("MPE per-channel pitch bend only affects its own note", "[engine][mpe]")
{
    SynthEngine e;
    e.prepare (48000.0, 512);
    EngineGlobals g; g.mpe = true;
    e.setGlobals (g);
    e.setParams (sawPatch(), false);
    e.noteOn (1, 60, 1.0f);
    e.noteOn (2, 64, 1.0f);
    e.pitchBend (1, 1.0f);
    REQUIRE (renderPeak (e, 2048) > 0.0f);
    REQUIRE (e.getActiveVoiceCount() == 2);
}

TEST_CASE ("Engine survives every sample rate and odd block sizes", "[engine]")
{
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int block : { 16, 17, 33, 256, 4096 })
        {
            SynthEngine e;
            e.prepare (sr, block);
            e.setParams (sawPatch(), false);
            e.noteOn (0, 60, 1.0f);
            e.noteOn (0, 72, 1.0f);
            std::vector<float> l ((size_t) block), r ((size_t) block);
            for (int i = 0; i < 20; ++i)
            {
                e.render (l.data(), r.data(), block);
                for (float x : l) REQUIRE (std::isfinite (x));
            }
        }
}

TEST_CASE ("Every game SFX renders finite audio and ends", "[sfx]")
{
    SfxEngine s;
    s.prepare (48000.0);
    for (int t = 0; t < (int) Sfx::count; ++t)
    {
        s.trigger ({ (Sfx) t, 72.0f, 1.0f, 0.0f });
        float l[512], r[512];
        float peak = 0.0f;
        for (int blk = 0; blk < 400; ++blk)
        {
            std::fill (l, l + 512, 0.0f); std::fill (r, r + 512, 0.0f);
            s.render (l, r, 512, 1.0f);
            for (int i = 0; i < 512; ++i) { REQUIRE (std::isfinite (l[i])); peak = std::max (peak, std::fabs (l[i])); }
        }
        INFO ("sfx " << t << " peak " << peak);
        REQUIRE (peak > 0.0005f);
        REQUIRE (peak < 4.0f);
        REQUIRE (s.activeCount() == 0);
    }
}
