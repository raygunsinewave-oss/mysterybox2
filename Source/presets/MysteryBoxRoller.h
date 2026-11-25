// Mystery Box Synthesizer by Raygun
// The Mystery Box draw: weighted rarity, no-duplicate protection and a
// serialisable RNG so reloading a project cannot be used to re-roll.
#pragma once

#include "PresetLibrary.h"
#include <chrono>
#include <random>
#include <sstream>

namespace mbx
{
class MysteryBoxRoller
{
public:
    static constexpr int kHistory = 20;
    // Weights in tenths of a percent: Common 60, Rare 25, Epic 11, Legendary 3.5, Wonder 0.5
    static constexpr int kRarityWeights[kNumRarities] = { 600, 250, 110, 35, 5 };

    MysteryBoxRoller() { reseed(); }

    void reseed()
    {
        std::random_device rd;
        std::seed_seq seq { (uint64_t) rd(), (uint64_t) rd(), (uint64_t) rd(), (uint64_t) rd(),
                            (uint64_t) std::chrono::high_resolution_clock::now().time_since_epoch().count() };
        rng.seed (seq);
    }
    void seed (uint64_t s) { rng.seed (s); }

    struct Result { int presetId = 0; int rarity = 0; bool mascot = false; };

    /** Draws the next result. mascotEnabled: 1-in-15 chance after at least 5 spins of the box "moving". */
    Result roll (const PresetLibrary& lib, bool mascotEnabled)
    {
        Result res;
        ++spinCount;
        if (mascotEnabled && spinCount > 5 && std::uniform_int_distribution<int> (0, 14) (rng) == 0)
        {
            res.mascot = true;
            return res;
        }
        res.rarity = rollRarity();
        // Fall back to a lower tier if a tier happens to be empty
        while (res.rarity > 0 && lib.indicesForRarity (res.rarity).empty()) --res.rarity;
        const auto& pool = lib.indicesForRarity (res.rarity);
        if (pool.empty()) return res;

        int idx = 0;
        for (int attempt = 0; attempt < 256; ++attempt)
        {
            idx = pool[(size_t) std::uniform_int_distribution<int> (0, (int) pool.size() - 1) (rng)];
            if (! inHistory (lib.at (idx).id)) break;
        }
        res.presetId = lib.at (idx).id;
        pushHistory (res.presetId);
        return res;
    }

    int rollRarity()
    {
        const int r = std::uniform_int_distribution<int> (0, 999) (rng);
        int acc = 0;
        for (int i = 0; i < kNumRarities; ++i)
        {
            acc += kRarityWeights[i];
            if (r < acc) return i;
        }
        return 0;
    }

    bool inHistory (int id) const noexcept
    {
        for (int i = 0; i < historyCount; ++i) if (history[i] == id) return true;
        return false;
    }
    void pushHistory (int id) noexcept
    {
        if (historyCount < kHistory) history[historyCount++] = id;
        else { for (int i = 1; i < kHistory; ++i) history[i - 1] = history[i]; history[kHistory - 1] = id; }
    }
    int getHistoryCount() const noexcept { return historyCount; }
    int getHistory (int i) const noexcept { return history[i]; }
    int getSpinCount() const noexcept { return spinCount; }
    void setSpinCount (int n) noexcept { spinCount = n; }

    /** Full engine state + history as text, stored in the plugin state. */
    std::string serialise() const
    {
        std::ostringstream os;
        os << spinCount << ' ' << historyCount;
        for (int i = 0; i < historyCount; ++i) os << ' ' << history[i];
        os << ' ' << rng;
        return os.str();
    }
    bool deserialise (const std::string& s)
    {
        std::istringstream is (s);
        int sc = 0, hc = 0;
        if (! (is >> sc >> hc) || hc < 0 || hc > kHistory) return false;
        int h[kHistory] {};
        for (int i = 0; i < hc; ++i) if (! (is >> h[i])) return false;
        std::mt19937_64 r;
        if (! (is >> r)) return false;
        spinCount = sc; historyCount = hc;
        for (int i = 0; i < hc; ++i) history[i] = h[i];
        rng = r;
        return true;
    }

private:
    std::mt19937_64 rng;
    int history[kHistory] {};
    int historyCount = 0;
    int spinCount = 0;
};

} // namespace mbx
