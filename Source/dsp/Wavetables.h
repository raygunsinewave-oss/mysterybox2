// Mystery Box Synthesizer by Raygun
// 64 original, procedurally generated wavetables (8 families x 8 variants), each
// with 8 morph frames and 10 band-limited mip levels (alias-free playback).
#pragma once

#include "DspCommon.h"
#include <atomic>
#include <vector>

namespace mbx
{
class WavetableBank
{
public:
    static constexpr int kNumTables = 64;
    static constexpr int kFrames = 8;
    static constexpr int kFrameSize = 1024;     // samples per single cycle
    static constexpr int kMaxHarmonics = kFrameSize / 2;
    static constexpr int kMipLevels = 10;       // 512, 256, ... 1 harmonics

    static WavetableBank& get();

    /** Builds all tables. Thread-safe and idempotent; takes ~50-150 ms.
        Called from a background thread at plugin load; the audio thread outputs
        silence for wavetable oscillators until isReady() is true. */
    void build();
    bool isReady() const noexcept { return ready.load (std::memory_order_acquire); }

    static const char* getFamilyName (int table) noexcept;

    /** Pointer to the start of a frame (kFrameSize + 1 samples, last = wrap copy). */
    inline const float* frame (int table, int frameIndex, int mip) const noexcept
    {
        const size_t idx = (((size_t) table * kFrames + (size_t) frameIndex) * kMipLevels + (size_t) mip) * (kFrameSize + 1);
        return data.data() + idx;
    }

    /** Chooses the mip level for a given phase increment (cycles per sample). */
    static inline int mipForIncrement (float inc) noexcept
    {
        // Highest harmonic that stays below Nyquist: 0.5 / inc
        const float maxH = 0.5f / std::max (inc, 1.0e-7f);
        int level = 0;
        float h = (float) kMaxHarmonics;
        while (level < kMipLevels - 1 && h > maxH) { h *= 0.5f; ++level; }
        return level;
    }

    /** Reads table at phase/position with linear interpolation in both dimensions. */
    inline float read (int table, float position01, float phase01, float inc) const noexcept
    {
        const int mip = mipForIncrement (inc);
        const float fp = clampf (position01, 0.0f, 1.0f) * (float) (kFrames - 1);
        const int f0 = std::min ((int) fp, kFrames - 2);
        const float ff = fp - (float) f0;
        const float p = phase01 * (float) kFrameSize;
        const int i = std::min ((int) p, kFrameSize - 1);
        const float fr = p - (float) i;
        const float* a = frame (table, f0, mip);
        const float* b = frame (table, f0 + 1, mip);
        const float va = a[i] + fr * (a[i + 1] - a[i]);
        const float vb = b[i] + fr * (b[i + 1] - b[i]);
        return va + ff * (vb - va);
    }

private:
    WavetableBank() = default;
    std::vector<float> data;
    std::atomic<bool> ready { false };
    std::atomic<bool> building { false };
};

} // namespace mbx
