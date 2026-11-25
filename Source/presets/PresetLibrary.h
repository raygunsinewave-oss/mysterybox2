// Mystery Box Synthesizer by Raygun
// The 2,500-instrument library: binary format reader + indexes. Pure C++.
//
// presets.bin layout (little endian):
//   char[4] "MBXP", u32 version (1)
//   u16 numParamIds, then numParamIds x (u8 len, chars)   -- column order of the value table
//   u16 numCategories, then numCategories x (u8 len, chars)
//   u32 numPresets, then per preset:
//     u16 id, u8 category, u8 rarity, u8 nameLen, name, u8 tagsLen, tags, float[numParamIds]
#pragma once

#include "../engine/EngineParams.h"
#include <string>
#include <vector>

namespace mbx
{
enum class Rarity : int { Common = 0, Rare, Epic, Legendary, Wonder, count };
constexpr int kNumRarities = (int) Rarity::count;

inline const char* rarityName (int r) noexcept
{
    static const char* n[] = { "Common", "Rare", "Epic", "Legendary", "Wonder" };
    return n[r < 0 ? 0 : (r > 4 ? 4 : r)];
}
/** Rarity colours as 0xAARRGGBB */
inline uint32_t rarityColour (int r) noexcept
{
    static const uint32_t c[] = { 0xffa8a8a8, 0xff3d8bff, 0xffb04dff, 0xffffc233, 0xff00e5ff };
    return c[r < 0 ? 0 : (r > 4 ? 4 : r)];
}

struct PresetInfo
{
    int id = 0;            // 1..2500
    int category = 0;      // 0..24
    int rarity = 0;        // Rarity
    std::string name;
    std::string tags;
    EngineParams params;
};

class PresetLibrary
{
public:
    static constexpr int kNumCategories = 25;

    /** Parses a presets.bin image. Returns false (and leaves the library empty) on malformed data. */
    bool loadFromMemory (const void* data, size_t size, std::string* error = nullptr);
    /** Serialises the library back to the binary format (used by the validator tool). */
    std::vector<uint8_t> serialise() const;

    int size() const noexcept { return (int) presets.size(); }
    bool isLoaded() const noexcept { return ! presets.empty(); }
    const PresetInfo& at (int index) const noexcept { return presets[(size_t) index]; }
    PresetInfo& atMutable (int index) noexcept { return presets[(size_t) index]; }
    /** Index of the preset with the given 1-based id, or -1. */
    int indexOfId (int id) const noexcept;
    const PresetInfo* findById (int id) const noexcept { const int i = indexOfId (id); return i >= 0 ? &presets[(size_t) i] : nullptr; }

    const std::vector<int>& indicesForRarity (int r) const noexcept { return byRarity[(size_t) std::clamp (r, 0, kNumRarities - 1)]; }
    const std::string& categoryName (int c) const noexcept;
    const std::vector<std::string>& getCategoryNames() const noexcept { return categories; }

private:
    void rebuildIndexes();
    std::vector<PresetInfo> presets;
    std::vector<std::string> categories;
    std::vector<int> idToIndex;
    std::vector<int> byRarity[kNumRarities];
};

} // namespace mbx
