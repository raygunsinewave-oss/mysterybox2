// Mystery Box Synthesizer by Raygun
#include "PresetLibrary.h"
#include <cstring>

namespace mbx
{
namespace
{
struct Reader
{
    const uint8_t* p; const uint8_t* end; bool ok = true;
    bool need (size_t n) { if ((size_t) (end - p) < n) ok = false; return ok; }
    uint8_t u8() { if (! need (1)) return 0; return *p++; }
    uint16_t u16() { if (! need (2)) return 0; uint16_t v = (uint16_t) (p[0] | (p[1] << 8)); p += 2; return v; }
    uint32_t u32() { if (! need (4)) return 0; uint32_t v = (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); p += 4; return v; }
    float f32() { uint32_t b = u32(); float f; std::memcpy (&f, &b, 4); return f; }
    std::string str() { const uint8_t n = u8(); if (! need (n)) return {}; std::string s ((const char*) p, n); p += n; return s; }
};

struct Writer
{
    std::vector<uint8_t> out;
    void u8 (uint8_t v) { out.push_back (v); }
    void u16 (uint16_t v) { u8 ((uint8_t) v); u8 ((uint8_t) (v >> 8)); }
    void u32 (uint32_t v) { for (int i = 0; i < 4; ++i) u8 ((uint8_t) (v >> (8 * i))); }
    void f32 (float f) { uint32_t b; std::memcpy (&b, &f, 4); u32 (b); }
    void str (const std::string& s) { const size_t n = std::min<size_t> (s.size(), 255); u8 ((uint8_t) n); out.insert (out.end(), s.begin(), s.begin() + (long) n); }
};
} // namespace

bool PresetLibrary::loadFromMemory (const void* data, size_t size, std::string* error)
{
    presets.clear(); categories.clear();
    auto fail = [&] (const char* why) { presets.clear(); categories.clear(); rebuildIndexes(); if (error) *error = why; return false; };
    Reader r { (const uint8_t*) data, (const uint8_t*) data + size };
    if (size < 8 || std::memcmp (data, "MBXP", 4) != 0) return fail ("bad magic");
    r.p += 4;
    if (r.u32() != 1u) return fail ("unsupported version");

    const int numIds = r.u16();
    std::vector<int> columnToParam ((size_t) numIds, -1);
    for (int i = 0; i < numIds; ++i)
        columnToParam[(size_t) i] = findParamIndex (r.str());   // unknown columns are ignored (forward compatible)
    const int numCats = r.u16();
    for (int i = 0; i < numCats; ++i) categories.push_back (r.str());
    const uint32_t count = r.u32();
    if (! r.ok || count > 100000) return fail ("corrupt header");
    presets.reserve (count);
    for (uint32_t i = 0; i < count && r.ok; ++i)
    {
        PresetInfo info;
        info.id = r.u16();
        info.category = r.u8();
        info.rarity = std::clamp ((int) r.u8(), 0, kNumRarities - 1);
        info.name = r.str();
        info.tags = r.str();
        for (int c = 0; c < numIds; ++c)
        {
            const float v = r.f32();
            if (columnToParam[(size_t) c] >= 0) info.params.v[(size_t) columnToParam[(size_t) c]] = v;
        }
        info.params.sanitise();
        presets.push_back (std::move (info));
    }
    if (! r.ok) return fail ("truncated preset data");
    rebuildIndexes();
    return true;
}

std::vector<uint8_t> PresetLibrary::serialise() const
{
    Writer w;
    w.out.insert (w.out.end(), { 'M', 'B', 'X', 'P' });
    w.u32 (1);
    w.u16 ((uint16_t) kNumEngineParams);
    for (const auto& info : kParamInfo) w.str (std::string (info.id));
    w.u16 ((uint16_t) categories.size());
    for (const auto& c : categories) w.str (c);
    w.u32 ((uint32_t) presets.size());
    for (const auto& p : presets)
    {
        w.u16 ((uint16_t) p.id); w.u8 ((uint8_t) p.category); w.u8 ((uint8_t) p.rarity);
        w.str (p.name); w.str (p.tags);
        for (int i = 0; i < kNumEngineParams; ++i) w.f32 (p.params.v[(size_t) i]);
    }
    return w.out;
}

void PresetLibrary::rebuildIndexes()
{
    idToIndex.assign (65536, -1);
    for (auto& v : byRarity) v.clear();
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        idToIndex[(size_t) (presets[(size_t) i].id & 0xFFFF)] = i;
        byRarity[(size_t) presets[(size_t) i].rarity].push_back (i);
    }
}

int PresetLibrary::indexOfId (int id) const noexcept
{
    if (id < 0 || id >= (int) idToIndex.size()) return -1;
    return idToIndex[(size_t) id];
}

const std::string& PresetLibrary::categoryName (int c) const noexcept
{
    static const std::string unknown = "Unknown";
    return (c >= 0 && c < (int) categories.size()) ? categories[(size_t) c] : unknown;
}

} // namespace mbx
