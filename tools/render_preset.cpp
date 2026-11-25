// Mystery Box Synthesizer by Raygun
// Renders a preset to a 16-bit stereo WAV for auditioning / analysis.
//   mbx_render <presets.bin> <presetId> <out.wav> [--fx] [--chord] [--seconds S]
#include "engine/SynthEngine.h"
#include "presets/PresetLibrary.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace mbx;

static void writeWav (const std::string& path, const std::vector<float>& l, const std::vector<float>& r, int sr)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t frames = (uint32_t) l.size();
    f.write ("RIFF", 4); u32 (36 + frames * 4); f.write ("WAVE", 4);
    f.write ("fmt ", 4); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 4); u16 (4); u16 (16);
    f.write ("data", 4); u32 (frames * 4);
    for (uint32_t i = 0; i < frames; ++i)
    {
        const int16_t a = (int16_t) std::lround (clampf (l[i], -1.0f, 1.0f) * 32767.0f);
        const int16_t b = (int16_t) std::lround (clampf (r[i], -1.0f, 1.0f) * 32767.0f);
        f.write ((const char*) &a, 2); f.write ((const char*) &b, 2);
    }
}

int main (int argc, char** argv)
{
    if (argc < 4) { std::cerr << "usage: mbx_render <presets.bin> <id> <out.wav> [--fx] [--chord] [--seconds S]\n"; return 2; }
    bool fx = false, chord = false; float seconds = 3.0f;
    for (int i = 4; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--fx") fx = true;
        else if (a == "--chord") chord = true;
        else if (a == "--seconds" && i + 1 < argc) seconds = (float) std::atof (argv[++i]);
    }
    std::ifstream in (argv[1], std::ios::binary);
    std::vector<char> data ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    PresetLibrary lib;
    if (! lib.loadFromMemory (data.data(), data.size())) { std::cerr << "bad library\n"; return 1; }
    const auto* p = lib.findById (std::atoi (argv[2]));
    if (! p) { std::cerr << "no such id\n"; return 1; }
    std::cout << p->id << " " << p->name << " [" << lib.categoryName (p->category) << ", " << rarityName (p->rarity) << "]\n";

    constexpr int sr = 48000;
    WavetableBank::get().build();
    SynthEngine eng; eng.prepare (sr, 256);
    eng.setParams (p->params, false);
    float macros[8];
    for (int k = 0; k < 8; ++k) macros[k] = p->params.v[(size_t) ((int) P::mac1Val + k * kMacroStride)];
    eng.setMacroValues (macros);
    FxChain chain; chain.prepare (sr, 256);
    FxParams fp;
    fp.unit[(int) FxId::Reverb].on = p->params[P::fxRevMix] > 0.01f;
    fp.unit[(int) FxId::Reverb].mix = p->params[P::fxRevMix];
    fp.unit[(int) FxId::Reverb].p[0] = p->params[P::fxRevMode] / 4.0f;
    fp.unit[(int) FxId::Reverb].p[1] = p->params[P::fxRevSize];
    fp.unit[(int) FxId::Delay].on = p->params[P::fxDlyMix] > 0.01f;
    fp.unit[(int) FxId::Delay].mix = p->params[P::fxDlyMix];
    fp.unit[(int) FxId::Delay].p[0] = p->params[P::fxDlyDiv] / 11.0f;
    fp.unit[(int) FxId::Delay].p[1] = 0.35f;
    fp.unit[(int) FxId::Chorus].on = p->params[P::fxChoMix] > 0.01f;
    fp.unit[(int) FxId::Chorus].mix = p->params[P::fxChoMix];

    std::vector<float> L, R;
    const std::vector<int> melody = chord ? std::vector<int> { 48, 55, 60, 64 } : std::vector<int> { 60 };
    for (int n : melody) eng.noteOn (0, n, 0.9f);
    const int total = (int) ((seconds + 2.5f) * sr);
    float bl[256], br[256];
    for (int pos = 0; pos < total; pos += 256)
    {
        if (pos >= (int) (seconds * sr) && pos - 256 < (int) (seconds * sr))
            for (int n : melody) eng.noteOff (0, n);
        eng.render (bl, br, 256);
        if (fx) chain.process (bl, br, 256, fp);
        L.insert (L.end(), bl, bl + 256); R.insert (R.end(), br, br + 256);
    }
    writeWav (argv[3], L, R, sr);
    return 0;
}
