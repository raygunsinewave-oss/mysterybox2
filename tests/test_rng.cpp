// Mystery Box Synthesizer by Raygun -- Mystery Box RNG tests
#include <catch2/catch_test_macros.hpp>
#include "presets/MysteryBoxRoller.h"
#include <fstream>
#include <iterator>

using namespace mbx;

namespace
{
const PresetLibrary& library()
{
    static PresetLibrary lib = []
    {
        PresetLibrary l;
        std::ifstream in (MBX_PRESETS_PATH, std::ios::binary);
        std::vector<char> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        l.loadFromMemory (data.data(), data.size());
        return l;
    }();
    return lib;
}
} // namespace

TEST_CASE ("Rarity distribution matches the published odds within 1% over 1,000,000 spins", "[rng]")
{
    MysteryBoxRoller roller;
    roller.seed (12345);
    int counts[kNumRarities] {};
    constexpr int N = 1000000;
    for (int i = 0; i < N; ++i) ++counts[roller.rollRarity()];
    const double expected[kNumRarities] = { 0.60, 0.25, 0.11, 0.035, 0.005 };
    for (int r = 0; r < kNumRarities; ++r)
    {
        const double observed = (double) counts[r] / N;
        INFO (rarityName (r) << ": observed " << observed * 100.0 << "% expected " << expected[r] * 100.0 << "%");
        REQUIRE (std::fabs (observed - expected[r]) < 0.01);              // within 1 percentage point
        REQUIRE (std::fabs (observed - expected[r]) / expected[r] < 0.05); // and within 5% relative
    }
}

TEST_CASE ("Full spins follow the odds and never repeat any of the last 20 results", "[rng]")
{
    const auto& lib = library();
    REQUIRE (lib.size() == 2500);
    MysteryBoxRoller roller;
    roller.seed (99);
    std::vector<int> recent;
    int counts[kNumRarities] {};
    constexpr int N = 200000;
    for (int i = 0; i < N; ++i)
    {
        const auto res = roller.roll (lib, false);
        REQUIRE_FALSE (res.mascot);
        REQUIRE (res.presetId >= 1);
        for (int id : recent) REQUIRE (id != res.presetId);
        recent.push_back (res.presetId);
        if (recent.size() > 20) recent.erase (recent.begin());
        const auto* p = lib.findById (res.presetId);
        REQUIRE (p != nullptr);
        REQUIRE (p->rarity == res.rarity);
        if (res.rarity == (int) Rarity::Wonder) REQUIRE (p->category == 24);
        ++counts[res.rarity];
    }
    REQUIRE (std::fabs ((double) counts[0] / N - 0.60) < 0.01);
    REQUIRE (std::fabs ((double) counts[4] / N - 0.005) < 0.002);
}

TEST_CASE ("Mascot appears roughly 1 in 15 spins, never in the first 5", "[rng]")
{
    const auto& lib = library();
    MysteryBoxRoller roller;
    roller.seed (7);
    int mascots = 0;
    for (int i = 0; i < 5; ++i) REQUIRE_FALSE (roller.roll (lib, true).mascot);
    constexpr int N = 150000;
    for (int i = 0; i < N; ++i) mascots += roller.roll (lib, true).mascot ? 1 : 0;
    REQUIRE (std::fabs ((double) mascots / N - 1.0 / 15.0) < 0.004);
}

TEST_CASE ("RNG state round-trips exactly (no re-rolling by reloading a project)", "[rng][state]")
{
    const auto& lib = library();
    MysteryBoxRoller a;
    a.reseed();
    for (int i = 0; i < 37; ++i) a.roll (lib, true);
    const std::string saved = a.serialise();

    MysteryBoxRoller b;
    REQUIRE (b.deserialise (saved));
    REQUIRE (b.getSpinCount() == a.getSpinCount());
    REQUIRE (b.getHistoryCount() == a.getHistoryCount());
    for (int i = 0; i < 100; ++i)
    {
        const auto ra = a.roll (lib, true), rb = b.roll (lib, true);
        REQUIRE (ra.presetId == rb.presetId);
        REQUIRE (ra.mascot == rb.mascot);
    }
    MysteryBoxRoller c;
    REQUIRE_FALSE (c.deserialise ("garbage"));
}
