// Mystery Box Synthesizer by Raygun -- library integrity + audio validation of all 2,500 presets
#include <catch2/catch_test_macros.hpp>
#include "engine/SynthEngine.h"
#include "presets/PresetLibrary.h"
#include <chrono>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>
#include <atomic>

using namespace mbx;

namespace
{
std::vector<char> readFile()
{
    std::ifstream in (MBX_PRESETS_PATH, std::ios::binary);
    return std::vector<char> ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE ("Library has 2,500 unique presets, 100 per category", "[presets]")
{
    const auto data = readFile();
    PresetLibrary lib;
    std::string err;
    REQUIRE (lib.loadFromMemory (data.data(), data.size(), &err));
    REQUIRE (lib.size() == 2500);
    REQUIRE (lib.getCategoryNames().size() == 25);
    int perCat[25] {};
    std::set<int> ids;
    std::set<std::string> names;
    for (int i = 0; i < lib.size(); ++i)
    {
        const auto& p = lib.at (i);
        REQUIRE (p.id >= 1);
        REQUIRE (p.id <= 2500);
        REQUIRE (p.category >= 0);
        REQUIRE (p.category < 25);
        ++perCat[p.category];
        ids.insert (p.id);
        names.insert (p.name);
        REQUIRE_FALSE (p.name.empty());
        if (p.rarity == (int) Rarity::Wonder) REQUIRE (p.category == 24);
    }
    for (int c : perCat) REQUIRE (c == 100);
    REQUIRE (ids.size() == 2500);
    REQUIRE (names.size() == 2500);
    for (int r = 0; r < kNumRarities; ++r) REQUIRE_FALSE (lib.indicesForRarity (r).empty());
}

TEST_CASE ("Library loads fast and rejects corrupt data", "[presets]")
{
    const auto data = readFile();
    PresetLibrary lib;
    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE (lib.loadFromMemory (data.data(), data.size()));
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    INFO ("full library parse " << ms << " ms");
    REQUIRE (ms < 500.0);

    auto bad = data;
    bad[0] = 'X';
    REQUIRE_FALSE (lib.loadFromMemory (bad.data(), bad.size()));
    REQUIRE_FALSE (lib.loadFromMemory (data.data(), data.size() / 2));
    REQUIRE (lib.size() == 0);
}

TEST_CASE ("All 2,500 presets pass audio validation (audible, finite, no DC, peak below -1 dBFS)", "[presets][audio]")
{
    const auto data = readFile();
    PresetLibrary lib;
    REQUIRE (lib.loadFromMemory (data.data(), data.size()));
    WavetableBank::get().build();
    std::vector<std::string> failures ((size_t) lib.size());
    std::atomic<int> next { 0 };
    std::vector<std::thread> pool;
    const int threads = (int) std::max (1u, std::thread::hardware_concurrency());
    for (int t = 0; t < threads; ++t)
        pool.emplace_back ([&]
        {
            for (int i = next++; i < lib.size(); i = next++)
            {
                const auto& info = lib.at (i);
                SynthEngine e;
                e.prepare (48000.0, 256);
                e.setParams (info.params, false);
                float macros[8];
                for (int k = 0; k < 8; ++k) macros[k] = info.params.v[(size_t) ((int) P::mac1Val + k * kMacroStride)];
                e.setMacroValues (macros);
                float l[256], r[256];
                float peak = 0; double sum = 0; long n = 0; bool finite = true;
                const int blocks = (int) (48000 * std::clamp (info.params[P::ampA] * 1.5f + 0.6f, 1.0f, 4.0f)) / 256;
                // Three separate notes: each note-on draws new random unison phases
                for (int b = 0; b < blocks * 3; ++b)
                {
                    if (b % blocks == 0) { e.allNotesOff (true); e.noteOn (0, 60, 1.0f); }
                    e.render (l, r, 256);
                    for (int s = 0; s < 256; ++s)
                    {
                        if (! std::isfinite (l[s]) || ! std::isfinite (r[s])) finite = false;
                        peak = std::max ({ peak, std::fabs (l[s]), std::fabs (r[s]) });
                        sum += l[s] + r[s]; n += 2;
                    }
                }
                std::string why;
                if (! finite) why = "non-finite";
                else if (peak < dbToGain (-60.0f)) why = "silent";
                else if (peak > dbToGain (-1.0f)) why = "peak above -1 dBFS: " + std::to_string (gainToDb (peak));
                else if (std::fabs (sum / (double) n) > 0.05 * peak) why = "DC offset";
                if (! why.empty()) failures[(size_t) i] = std::to_string (info.id) + " " + info.name + ": " + why;
            }
        });
    for (auto& t : pool) t.join();
    int failed = 0;
    for (const auto& f : failures)
        if (! f.empty()) { ++failed; UNSCOPED_INFO (f); }
    REQUIRE (failed == 0);
}
