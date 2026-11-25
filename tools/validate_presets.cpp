// Mystery Box Synthesizer by Raygun
// Offline preset validator.
//
//   mbx_validate_presets <in.bin> <out.bin> <report.txt> [--threads N]
//
// Renders every preset through the real engine and REJECTS presets that are
// silent, produce NaN/Inf, clip excessively before normalisation, carry DC
// offset, ring on forever after release, or blow up at extreme notes.
// Passing presets are loudness-matched (RMS target with a -3 dBFS peak cap so
// velocity-127 notes always stay below -1 dBFS) and written to out.bin.
// The report is consumed by tools/generate_presets.py, which regenerates the
// rejected presets and re-runs validation until all 2,500 pass.

#include "engine/SynthEngine.h"
#include "presets/PresetLibrary.h"
#include <atomic>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace mbx;

namespace
{
constexpr double kSr = 48000.0;
constexpr float kTargetRms = 0.126f;     // -18 dBFS
constexpr float kPeakCap = 0.596f;       // -4.5 dBFS (margin for random phase / noise peaks)

struct Stats { float peak = 0, sumSq = 0, sum = 0; long count = 0; bool finite = true; };

void renderNotes (SynthEngine& eng, const std::vector<int>& notes, float vel, float holdS, float maxTailS,
                  Stats& hold, Stats& all, float& tailRms)
{
    // Renders the hold phase, then the release tail until every voice has finished
    // (early exit) or maxTailS elapses. tailRms is the RMS of the last 150 ms if the
    // tail was cut off by the time limit (0 when the voices ended naturally).
    eng.allNotesOff (true);
    for (int n : notes) eng.noteOn (0, n, vel);
    constexpr int block = 256;
    float L[block], R[block];
    const long holdSamples = (long) (holdS * kSr);
    const long total = holdSamples + (long) (maxTailS * kSr);
    const long win = (long) (0.15 * kSr);
    double endSq = 0; long endCount = 0;
    bool released = false;
    long pos = 0;
    for (; pos < total; pos += block)
    {
        if (! released && pos >= holdSamples) { for (int n : notes) eng.noteOff (0, n); released = true; }
        if (released && eng.getActiveVoiceCount() == 0) { tailRms = 0.0f; return; }
        eng.render (L, R, block);
        for (int i = 0; i < block; ++i)
        {
            const float s[2] = { L[i], R[i] };
            for (float x : s)
            {
                if (! std::isfinite (x)) { all.finite = false; continue; }
                const float a = std::fabs (x);
                all.peak = std::max (all.peak, a); all.sumSq += x * x; all.sum += x; ++all.count;
                if (pos + i < holdSamples && pos + i > (long) (0.02 * kSr))
                { hold.peak = std::max (hold.peak, a); hold.sumSq += x * x; ++hold.count; }
                if (pos + i >= total - win) { endSq += x * x; ++endCount; }
            }
        }
    }
    tailRms = endCount ? (float) std::sqrt (endSq / endCount) : 0.0f;
}

struct Result { bool pass = false; std::string reason; float gainDb = 0, peakDb = -120, rmsDb = -120; };

Result validate (const PresetInfo& info, SynthEngine& eng)
{
    Result res;
    EngineParams p = info.params;
    p[P::presetGain] = 0.0f; // measure at unity
    eng.setParams (p, false);
    float macros[8];
    for (int k = 0; k < 8; ++k) macros[k] = p.v[(size_t) ((int) P::mac1Val + k * kMacroStride)];
    eng.setMacroValues (macros);

    const float attack = p[P::ampA];
    const float release = p[P::ampR];
    const float holdS = std::clamp (attack * 1.5f + 0.6f, 1.0f, 4.0f);
    const float tailS = std::clamp (release * 2.5f + 1.0f, 1.5f, 14.0f);

    Stats hold, all; float tail = 0.0f;
    renderNotes (eng, { 60 }, 1.0f, holdS, tailS, hold, all, tail);
    // Random unison phases change the peak from note to note: take the worst of five more note-ons.
    for (int rep = 0; rep < 5; ++rep)
    {
        Stats h2, a2; float t2 = 0.0f;
        renderNotes (eng, { 60 }, 1.0f, holdS, 0.0f, h2, a2, t2);
        if (! a2.finite) all.finite = false;
        all.peak = std::max (all.peak, a2.peak);
    }
    if (! all.finite) { res.reason = "non-finite output"; return res; }
    if (all.peak < 1.0e-4f) { res.reason = "silent"; return res; }
    if (all.peak > 4.0f) { res.reason = "clips excessively before normalisation (raw peak " + std::to_string (gainToDb (all.peak)) + " dB)"; return res; }
    const float mean = all.count ? all.sum / (float) all.count : 0.0f;
    if (std::fabs (mean) > 0.05f * all.peak) { res.reason = "DC offset"; return res; }
    if (tail > 0.05f * all.peak) { res.reason = "does not decay after release"; return res; }

    const float rms = hold.count ? std::sqrt (hold.sumSq / (float) hold.count) : 0.0f;
    float gain = rms > 1.0e-6f ? kTargetRms / rms : kPeakCap / all.peak;
    if (all.peak * gain > kPeakCap) gain = kPeakCap / all.peak;
    const float gainDb = gainToDb (gain);
    if (gainDb > 12.0f) { res.reason = "too quiet (needs > +12 dB)"; return res; }
    if (gainDb < -36.0f) { res.reason = "too loud (needs < -36 dB)"; return res; }

    // Extreme notes must neither explode nor produce garbage
    for (int note : { 36, 84 })
    {
        Stats h2, a2; float t2 = 0.0f;
        renderNotes (eng, { note }, 1.0f, 0.5f, 0.0f, h2, a2, t2);
        if (! a2.finite) { res.reason = "non-finite output at note " + std::to_string (note); return res; }
        if (a2.peak * gain > 1.0f) { res.reason = "too hot at note " + std::to_string (note); return res; }
    }
    // Chord sanity (the safety limiter handles summing, but catch runaway feedback)
    {
        Stats h3, a3; float t3 = 0.0f;
        renderNotes (eng, { 48, 55, 60, 64 }, 0.8f, 0.8f, 0.0f, h3, a3, t3);
        if (! a3.finite || a3.peak * gain > 3.0f) { res.reason = "chord blows up"; return res; }
    }

    res.pass = true;
    res.gainDb = gainDb;
    res.peakDb = gainToDb (all.peak * gain);
    res.rmsDb = gainToDb (rms * gain);
    return res;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && std::string (argv[1]) == "--dump-params")
    {
        for (const auto& info : kParamInfo)
            std::printf ("%.*s %g %g %g\n", (int) info.id.size(), info.id.data(), info.minValue, info.maxValue, info.defaultValue);
        return 0;
    }
    if (argc < 4) { std::cerr << "usage: mbx_validate_presets <in.bin> <out.bin> <report.txt> [--threads N]\n"; return 2; }
    int threads = (int) std::max (1u, std::thread::hardware_concurrency());
    for (int i = 4; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--threads") threads = std::max (1, std::atoi (argv[i + 1]));

    std::ifstream in (argv[1], std::ios::binary);
    std::vector<char> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    PresetLibrary lib;
    std::string err;
    if (! lib.loadFromMemory (data.data(), data.size(), &err)) { std::cerr << "load failed: " << err << "\n"; return 1; }

    WavetableBank::get().build();
    std::vector<Result> results ((size_t) lib.size());
    std::atomic<int> next { 0 };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t)
        pool.emplace_back ([&]
        {
            // A fresh engine per preset keeps validation fully deterministic
            // (independent of thread scheduling and of the previous preset's state).
            for (int i = next++; i < lib.size(); i = next++)
            {
                auto eng = std::make_unique<SynthEngine>();
                eng->prepare (kSr, 256);
                EngineGlobals g; g.maxPolyphony = 32;
                eng->setGlobals (g);
                results[(size_t) i] = validate (lib.at (i), *eng);
            }
        });
    for (auto& th : pool) th.join();

    std::ofstream report (argv[3]);
    int passed = 0;
    for (int i = 0; i < lib.size(); ++i)
    {
        const auto& r = results[(size_t) i];
        auto& info = lib.atMutable (i);
        char line[256];
        if (r.pass)
        {
            ++passed;
            info.params[P::presetGain] = r.gainDb;
            std::snprintf (line, sizeof line, "%d PASS gain=%.2f peak=%.2f rms=%.2f", info.id, r.gainDb, r.peakDb, r.rmsDb);
        }
        else std::snprintf (line, sizeof line, "%d FAIL %s", info.id, r.reason.c_str());
        report << line << "\n";
    }
    const auto bytes = lib.serialise();
    std::ofstream out (argv[2], std::ios::binary);
    out.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    std::cout << "validated " << lib.size() << " presets: " << passed << " passed, " << (lib.size() - passed) << " failed\n";
    return passed == lib.size() ? 0 : 3;
}
