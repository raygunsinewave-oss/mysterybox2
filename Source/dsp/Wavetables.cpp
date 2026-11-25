// Mystery Box Synthesizer by Raygun
#include "Wavetables.h"
#include <complex>
#include <mutex>
#include <thread>

namespace mbx
{
namespace
{
using cpx = std::complex<double>;

void fft (std::vector<cpx>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = kTwoPiD / (double) len * (inverse ? 1.0 : -1.0);
        const cpx wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            cpx w (1.0);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const cpx u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (auto& x : a) x /= (double) n;
}

constexpr int N = WavetableBank::kFrameSize;
constexpr int H = WavetableBank::kMaxHarmonics;

double saw (double t) { return 2.0 * t - 1.0; }
double square (double t, double pw = 0.5) { return t < pw ? 1.0 : -1.0; }
double tri (double t) { return t < 0.5 ? 4.0 * t - 1.0 : 3.0 - 4.0 * t; }
double sine (double t) { return std::sin (kTwoPiD * t); }

/** Deterministic hash -> [0,1) for spectral families. */
double hash01 (uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return (double) (x & 0xFFFFFF) / 16777216.0;
}

/** Formant amplitude envelope for vocal tables (centre Hz relative to a 130 Hz fundamental). */
double formantGain (double hz, const double* f, const double* bw, const double* g)
{
    double s = 0.0;
    for (int k = 0; k < 3; ++k)
    {
        const double d = (hz - f[k]) / bw[k];
        s += g[k] * std::exp (-0.5 * d * d);
    }
    return s + 0.02;
}

/** Generates either a time-domain frame (returns true) or fills a spectrum (returns false). */
bool generate (int table, int frame, std::vector<double>& time, std::vector<double>& mag, std::vector<double>& phase)
{
    const int family = table / 8;
    const int v = table % 8;
    const double m = (double) frame / (double) (WavetableBank::kFrames - 1); // morph 0..1

    switch (family)
    {
        case 0: // "Classic": sine -> triangle -> saw -> square, variants bias the mix
        {
            const double pos = std::fmod (m * 3.0 + v * 0.35, 3.0);
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / N;
                const double s = sine (t), tr = tri (std::fmod (t + 0.25, 1.0)), sw = saw (std::fmod (t + 0.5, 1.0)), sq = square (t);
                double y;
                if (pos < 1.0) y = s + (tr - s) * pos;
                else if (pos < 2.0) y = tr + (sw - tr) * (pos - 1.0);
                else y = sw + (sq - sw) * (pos - 2.0);
                time[(size_t) i] = y;
            }
            return true;
        }
        case 1: // "Pulse Bunker": PWM sweep with saw blend
        {
            const double pw = 0.5 - m * (0.40 + 0.006 * v);
            const double blend = v * 0.1;
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / N;
                time[(size_t) i] = (1.0 - blend) * square (t, pw) + blend * saw (t);
            }
            return true;
        }
        case 2: // "Sync Siren": hard-sync sweep of a slave saw/sine
        {
            const double ratio = 1.0 + m * (1.5 + v * 0.75);
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / N;
                const double ph = std::fmod (t * ratio, 1.0);
                const double slave = (v & 1) ? sine (ph) : saw (ph);
                const double window = (v >= 4) ? (1.0 - t) : 1.0;
                time[(size_t) i] = slave * window;
            }
            return true;
        }
        case 3: // "Fold Shrapnel": sine wavefolder with increasing gain
        {
            const double gain = 1.0 + m * (2.0 + v * 1.2);
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / N;
                const double x = (sine (t) + 0.15 * (v % 3) * sine (2.0 * t)) * gain;
                time[(size_t) i] = std::sin (x * 1.5707963);
            }
            return true;
        }
        case 4: // "Phase Bolt": FM frames, sin(x + I sin(r x))
        {
            const double r = (double) (v % 4 + 1);
            const double I = m * (1.0 + v * 0.6);
            for (int i = 0; i < N; ++i)
            {
                const double t = (double) i / N;
                time[(size_t) i] = std::sin (kTwoPiD * t + I * std::sin (kTwoPiD * r * t + (v >= 4 ? 0.7 : 0.0)));
            }
            return true;
        }
        case 5: // "Choir Static": vocal formant spectra, morphing a -> e -> i -> o -> u
        {
            static const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            static const double BW[3] = { 90, 120, 180 };
            static const double G[3] = { 1.0, 0.5, 0.25 };
            const double pos = m * 4.0;
            const int a = std::min ((int) pos, 3);
            const double fr = pos - a;
            const double shift = 1.0 + 0.08 * (v - 3.5); // voice type
            double f[3];
            for (int k = 0; k < 3; ++k) f[k] = (F[a][k] + (F[a + 1][k] - F[a][k]) * fr) * shift;
            const double fundamental = 110.0 + v * 15.0;
            for (int h = 1; h < H; ++h)
            {
                mag[(size_t) h] = formantGain (h * fundamental, f, BW, G) / std::sqrt ((double) h);
                phase[(size_t) h] = hash01 ((uint32_t) (h * 7919 + v)) * kTwoPiD;
            }
            return false;
        }
        case 6: // "Drawbar Chapel": organ registrations, morphing between drawbar settings
        {
            static const double reg[8][9] = {
                { 8, 8, 8, 0, 0, 0, 0, 0, 0 }, { 8, 8, 8, 8, 0, 0, 0, 0, 0 }, { 8, 0, 8, 0, 0, 0, 0, 0, 8 },
                { 8, 8, 6, 8, 0, 4, 0, 0, 0 }, { 8, 6, 8, 6, 4, 0, 0, 2, 4 }, { 6, 8, 8, 8, 8, 8, 8, 8, 8 },
                { 8, 3, 8, 0, 0, 3, 0, 0, 0 }, { 8, 8, 8, 8, 8, 8, 8, 8, 8 } };
            static const double harm[9] = { 0.5, 1.5, 1, 2, 3, 4, 5, 6, 8 }; // 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'
            const int ra = v;
            const int rb = (v + 3) % 8;
            // Fundamental is the 16' bar, so harmonics are doubled to stay integer.
            for (int b = 0; b < 9; ++b)
            {
                const int h = (int) (harm[b] * 2.0);
                const double amp = (reg[ra][b] + (reg[rb][b] - reg[ra][b]) * m) / 8.0;
                mag[(size_t) h] += amp * amp;
                phase[(size_t) h] = 0.0;
            }
            return false;
        }
        default: // 7 "Spectral Ash": seeded random spectra with brightness morph
        {
            const double tilt = 1.6 - m * 1.1;
            for (int h = 1; h < H; ++h)
            {
                const double r1 = hash01 ((uint32_t) (h * 131 + v * 977));
                const double r2 = hash01 ((uint32_t) (h * 733 + v * 151 + 17));
                const double rr = r1 + (r2 - r1) * m;
                const double sparse = (v >= 4 && (h % (v - 2)) != 0) ? 0.15 : 1.0;
                mag[(size_t) h] = std::pow ((double) h, -tilt) * (0.25 + rr) * sparse;
                phase[(size_t) h] = hash01 ((uint32_t) (h * 31 + v)) * kTwoPiD;
            }
            return false;
        }
    }
}

std::once_flag buildOnce;
} // namespace

WavetableBank& WavetableBank::get()
{
    static WavetableBank bank;
    return bank;
}

const char* WavetableBank::getFamilyName (int table) noexcept
{
    static const char* names[] = { "Classic", "Pulse Bunker", "Sync Siren", "Fold Shrapnel", "Phase Bolt", "Choir Static", "Drawbar Chapel", "Spectral Ash" };
    return names[(table / 8) & 7];
}

void WavetableBank::build()
{
    std::call_once (buildOnce, [this]
    {
        building = true;
        std::vector<float> out ((size_t) kNumTables * kFrames * kMipLevels * (kFrameSize + 1), 0.0f);
        std::vector<double> time ((size_t) N), mag ((size_t) H), phase ((size_t) H);
        std::vector<cpx> spec ((size_t) N), work ((size_t) N);

        for (int t = 0; t < kNumTables; ++t)
        {
            for (int f = 0; f < kFrames; ++f)
            {
                std::fill (time.begin(), time.end(), 0.0);
                std::fill (mag.begin(), mag.end(), 0.0);
                std::fill (phase.begin(), phase.end(), 0.0);

                if (generate (t, f, time, mag, phase))
                {
                    for (int i = 0; i < N; ++i) spec[(size_t) i] = cpx (time[(size_t) i], 0.0);
                    fft (spec, false);
                }
                else
                {
                    std::fill (spec.begin(), spec.end(), cpx (0.0));
                    for (int h = 1; h < H; ++h)
                    {
                        const cpx c = std::polar (mag[(size_t) h], phase[(size_t) h] - kTwoPiD * 0.25);
                        spec[(size_t) h] = c * (double) (N / 2);
                        spec[(size_t) (N - h)] = std::conj (c) * (double) (N / 2);
                    }
                }
                spec[0] = 0.0; // remove DC

                // Normalise using the full-bandwidth level so all mips share a gain.
                double peak = 0.0;
                for (int mip = 0; mip < kMipLevels; ++mip)
                {
                    const int maxH = kMaxHarmonics >> mip;
                    work = spec;
                    for (int h = maxH + 1; h <= N - maxH - 1; ++h)
                        work[(size_t) h] = 0.0;
                    work[(size_t) (N / 2)] = 0.0;
                    fft (work, true);
                    float* dst = out.data() + (((size_t) t * kFrames + (size_t) f) * kMipLevels + (size_t) mip) * (kFrameSize + 1);
                    for (int i = 0; i < N; ++i)
                    {
                        dst[i] = (float) work[(size_t) i].real();
                        if (mip == 0) peak = std::max (peak, std::abs (work[(size_t) i].real()));
                    }
                }
                const float norm = peak > 1.0e-9 ? (float) (0.95 / peak) : 0.0f;
                for (int mip = 0; mip < kMipLevels; ++mip)
                {
                    float* dst = out.data() + (((size_t) t * kFrames + (size_t) f) * kMipLevels + (size_t) mip) * (kFrameSize + 1);
                    for (int i = 0; i < N; ++i) dst[i] *= norm;
                    dst[N] = dst[0];
                }
            }
        }
        data = std::move (out);
        ready.store (true, std::memory_order_release);
    });
}

} // namespace mbx
