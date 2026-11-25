// Mystery Box Synthesizer by Raygun
// Procedurally synthesized game sound effects. No samples are used anywhere.
// Runs on the audio thread; triggers arrive through a lock-free FIFO.
#pragma once

#include "../dsp/DspCommon.h"
#include "../dsp/Filters.h"

namespace mbx
{
enum class Sfx : uint8_t
{
    Gunshot = 0, Reload, DryFire, ZombieGroan, ZombieHit, ZombieDeath, BoardRip, BoardRebuild,
    BoxCreak, BoxChime, BoxReveal, WonderRay, MascotLaugh, BoxTeleport, PowerUpDrop, PowerUpGrab,
    Nuke, RoundStart, RoundEnd, PlayerDown, EggSting, KonamiFanfare, UiClick, OverclockZap, Headshot,
    count
};

struct SfxEvent
{
    Sfx type = Sfx::UiClick;
    float note = 72.0f;   // MIDI note for pitched sounds (chimes)
    float gain = 1.0f;
    float pan = 0.0f;     // -1..1
};

class SfxEngine
{
public:
    static constexpr int kVoices = 24;
    void prepare (double sampleRate);
    void trigger (const SfxEvent& e) noexcept;
    /** Renders n samples, ADDING into L/R, scaled by masterGain. */
    void render (float* L, float* R, int n, float masterGain) noexcept;
    int activeCount() const noexcept;
    void stopAll() noexcept;

private:
    struct Voice
    {
        bool active = false;
        SfxEvent ev;
        float t = 0.0f, dur = 1.0f;
        float ph[4] {};
        float lp = 0.0f, lp2 = 0.0f, hp = 0.0f;
        SvfCore svf, svf2;
        FormantCore formant;
        FastRandom rng;
        int step = 0;
    };
    float synth (Voice& v) noexcept;
    float sr = 48000.0f, invSr = 1.0f / 48000.0f;
    Voice voices[kVoices];
    uint32_t seedCounter = 1;
};

} // namespace mbx
