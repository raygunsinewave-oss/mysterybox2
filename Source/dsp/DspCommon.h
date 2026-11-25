// Mystery Box Synthesizer by Raygun
// Shared DSP helpers. Pure C++20, no framework dependencies, so the engine can
// be compiled into the plugin, the unit tests and the offline preset validator.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <numbers>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
 #include <xmmintrin.h>
#endif
#if defined(_M_ARM64)
 #include <intrin.h>
#endif

namespace mbx
{
constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr double kTwoPiD = 2.0 * std::numbers::pi;

inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerp (float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }
inline float midiToHz (float note, float a4 = 440.0f) noexcept { return a4 * std::exp2 ((note - 69.0f) * (1.0f / 12.0f)); }

/** Cheap rational tanh approximation, accurate to ~1e-3 and monotonic, clamps at +-1. */
inline float fastTanh (float x) noexcept
{
    x = clampf (x, -4.97f, 4.97f);
    const float x2 = x * x;
    const float a = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
    const float b = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
    return clampf (a / b, -1.0f, 1.0f);
}

/** RAII guard enabling flush-to-zero / denormals-are-zero for the current thread
    (x86 SSE MXCSR or AArch64 FPCR). The engine uses it in every render call. */
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
        prev = getCsr();
        setCsr (prev | 0x8040u);
#elif defined(__aarch64__) || defined(_M_ARM64)
        prev = getCsr();
        setCsr (prev | (1ull << 24));
#endif
    }
    ~ScopedFlushDenormals() noexcept
    {
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86) || defined(__aarch64__) || defined(_M_ARM64)
        setCsr (prev);
#endif
    }
    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    static unsigned getCsr() noexcept { return _mm_getcsr(); }
    static void setCsr (unsigned v) noexcept { _mm_setcsr (v); }
    unsigned prev = 0;
#elif defined(__aarch64__) && ! defined(_MSC_VER)
    static uint64_t getCsr() noexcept { uint64_t v; asm volatile ("mrs %0, fpcr" : "=r"(v)); return v; }
    static void setCsr (uint64_t v) noexcept { asm volatile ("msr fpcr, %0" : : "r"(v)); }
    uint64_t prev = 0;
#elif defined(_M_ARM64)
    static uint64_t getCsr() noexcept { return (uint64_t) _ReadStatusReg (ARM64_FPCR); }
    static void setCsr (uint64_t v) noexcept { _WriteStatusReg (ARM64_FPCR, (__int64) v); }
    uint64_t prev = 0;
#endif
};

/** Fast 2^x for modulation maths (control rate). */
inline float fastExp2 (float x) noexcept { return std::exp2 (x); }

/** Flushes tiny values to zero. Used in feedback paths as belt-and-braces on top of FTZ/DAZ. */
inline float undenormal (float v) noexcept { return std::fabs (v) < 1.0e-15f ? 0.0f : v; }

/** Small, fast, deterministic PRNG for audio-rate noise (xorshift32). Not for the Mystery Box. */
struct FastRandom
{
    uint32_t state = 0x9E3779B9u;
    explicit FastRandom (uint32_t seed = 0x9E3779B9u) noexcept : state (seed ? seed : 0x9E3779B9u) {}
    inline uint32_t nextU32() noexcept
    {
        uint32_t x = state;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        return state = x;
    }
    /** Uniform in [-1, 1). */
    inline float nextBipolar() noexcept { return (float) (int32_t) nextU32() * (1.0f / 2147483648.0f); }
    /** Uniform in [0, 1). */
    inline float nextUnipolar() noexcept { return (float) (nextU32() >> 8) * (1.0f / 16777216.0f); }
};

/** Lookup-table sine with linear interpolation. Phase in [0, 1). */
class SineTable
{
public:
    static constexpr int kSize = 4096;
    static const SineTable& get() noexcept
    {
        static const SineTable table;
        return table;
    }
    inline float lookup (float phase01) const noexcept
    {
        const float p = phase01 * (float) kSize;
        const int i = (int) p;
        const float f = p - (float) i;
        const int i0 = i & (kSize - 1);
        return data[(size_t) i0] + f * (data[(size_t) i0 + 1] - data[(size_t) i0]);
    }

private:
    SineTable() noexcept
    {
        for (int i = 0; i <= kSize; ++i)
            data[(size_t) i] = (float) std::sin (kTwoPiD * (double) i / (double) kSize);
    }
    std::array<float, kSize + 1> data {};
};

inline float fastSin01 (float phase01) noexcept
{
    phase01 -= std::floor (phase01);
    return SineTable::get().lookup (phase01);
}

/** One-pole linear smoother (per-sample ramp to target over a fixed time). */
struct LinearSmoother
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampLen = 1;

    void reset (double sampleRate, double seconds) noexcept { rampLen = std::max (1, (int) (sampleRate * seconds)); }
    void setImmediate (float v) noexcept { current = target = v; remaining = 0; step = 0.0f; }
    void setTarget (float v) noexcept
    {
        if (v == target) return;
        target = v;
        remaining = rampLen;
        step = (target - current) / (float) rampLen;
    }
    inline float next() noexcept
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    bool isSmoothing() const noexcept { return remaining > 0; }
};

/** Simple one-pole lowpass used for DC blocking / tone controls. */
struct OnePole
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (float hz, float sr) noexcept { a = 1.0f - std::exp (-kTwoPi * clampf (hz, 1.0f, sr * 0.49f) / sr); }
    inline float lp (float x) noexcept { z += a * (x - z); z = undenormal (z); return z; }
    inline float hp (float x) noexcept { return x - lp (x); }
    void reset() noexcept { z = 0.0f; }
};

struct DcBlocker
{
    float x1 = 0.0f, y1 = 0.0f, r = 0.995f;
    void prepare (float sr) noexcept { r = 1.0f - (kTwoPi * 12.0f / sr); }
    inline float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x; y1 = undenormal (y);
        return y;
    }
    void reset() noexcept { x1 = y1 = 0.0f; }
};

/** Fixed-capacity delay line. Memory is allocated in prepare(), never on the audio thread. */
class DelayLine
{
public:
    void allocate (int maxSamples)
    {
        size = 1;
        while (size < maxSamples + 4) size <<= 1;
        mask = size - 1;
        buffer.reset (new float[(size_t) size]);
        clear();
    }
    void clear() noexcept { if (buffer) std::memset (buffer.get(), 0, sizeof (float) * (size_t) size); writePos = 0; }
    inline void push (float x) noexcept { buffer[(size_t) writePos] = x; writePos = (writePos + 1) & mask; }
    /** Read with fractional delay in samples (>= 1), linear interpolation. */
    inline float read (float delaySamples) const noexcept
    {
        delaySamples = clampf (delaySamples, 1.0f, (float) (size - 3));
        const float rp = (float) writePos - delaySamples;
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float a = buffer[(size_t) (i & mask)];
        const float b = buffer[(size_t) ((i + 1) & mask)];
        return a + f * (b - a);
    }
    /** Cubic (Hermite) interpolated read, for modulated delays. */
    inline float readCubic (float delaySamples) const noexcept
    {
        delaySamples = clampf (delaySamples, 2.0f, (float) (size - 4));
        const float rp = (float) writePos - delaySamples;
        const int i = (int) std::floor (rp);
        const float f = rp - (float) i;
        const float xm1 = buffer[(size_t) ((i - 1) & mask)];
        const float x0 = buffer[(size_t) (i & mask)];
        const float x1 = buffer[(size_t) ((i + 1) & mask)];
        const float x2 = buffer[(size_t) ((i + 2) & mask)];
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }
    int capacity() const noexcept { return size; }

private:
    std::unique_ptr<float[]> buffer;
    int size = 0, mask = 0, writePos = 0;
};

} // namespace mbx
