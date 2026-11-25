// Mystery Box Synthesizer by Raygun
#include "Oscillator.h"

namespace mbx
{
namespace
{
inline float polyBlep (float t, float dt) noexcept
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

constexpr float kFmRatios[16] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 11.0f, 13.0f, 14.0f, 1.4142f };

// Modal models: bell, marimba, vibraphone bar, glass, kalimba, gong/plate
constexpr float kModalRatios[6][Oscillator::kModes] = {
    { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f, 8.215f },
    { 1.0f, 3.932f, 9.538f, 16.688f, 24.566f, 31.0f, 2.0f, 6.0f, 12.0f, 20.0f, 28.0f, 35.0f },
    { 1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f, 4.0f, 10.0f, 15.6f, 22.0f, 28.4f, 33.0f },
    { 1.0f, 2.32f, 4.25f, 6.63f, 9.38f, 12.5f, 15.9f, 19.7f, 23.8f, 28.3f, 33.0f, 38.2f },
    { 1.0f, 5.4f, 6.0f, 11.3f, 2.0f, 3.01f, 8.9f, 14.2f, 17.0f, 20.1f, 26.0f, 31.0f },
    { 1.0f, 1.42f, 1.86f, 2.14f, 2.42f, 2.72f, 3.11f, 3.6f, 4.12f, 4.79f, 5.31f, 6.02f } };
constexpr float kModalGains[6][Oscillator::kModes] = {
    { 0.6f, 1.0f, 0.7f, 0.6f, 0.5f, 0.4f, 0.35f, 0.3f, 0.2f, 0.15f, 0.1f, 0.08f },
    { 1.0f, 0.4f, 0.15f, 0.06f, 0.03f, 0.02f, 0.05f, 0.03f, 0.02f, 0.01f, 0.01f, 0.01f },
    { 1.0f, 0.5f, 0.25f, 0.12f, 0.06f, 0.03f, 0.08f, 0.05f, 0.03f, 0.02f, 0.01f, 0.01f },
    { 1.0f, 0.6f, 0.4f, 0.3f, 0.2f, 0.15f, 0.1f, 0.07f, 0.05f, 0.04f, 0.03f, 0.02f },
    { 1.0f, 0.25f, 0.2f, 0.08f, 0.1f, 0.05f, 0.05f, 0.03f, 0.02f, 0.02f, 0.01f, 0.01f },
    { 1.0f, 0.9f, 0.85f, 0.8f, 0.7f, 0.65f, 0.6f, 0.5f, 0.45f, 0.4f, 0.3f, 0.25f } };

inline void equalPower (float pan, float& l, float& r) noexcept
{
    const float p = (clampf (pan, -1.0f, 1.0f) + 1.0f) * 0.125f; // 0..0.25 = quarter cycle
    l = fastSin01 (p + 0.25f);
    r = fastSin01 (p);
}
} // namespace

void Oscillator::prepare (float sampleRate, uint32_t seed)
{
    sr = sampleRate;
    rng = FastRandom (seed);
    // Physical models need up to one period at the lowest supported pitch (~16 Hz).
    const int maxDelay = (int) (sr / 15.0f) + 16;
    lineA.allocate (maxDelay);
    lineB.allocate (maxDelay);
    dc.prepare (sr);
    reset();
}

void Oscillator::reset() noexcept
{
    lineA.clear(); lineB.clear();
    for (int i = 0; i < kMaxUnison; ++i) { phase[i] = modPhase[i] = fbState[i] = triState[i] = 0.0f; }
    for (int i = 0; i < kModes; ++i) { mY1[i] = mY2[i] = 0.0f; }
    for (int i = 0; i < kMaxPartials; ++i) pPhase[i] = 0.0f;
    plLp = 0.0f; lastLevel = 0.0f; exciteCount = exciteLen = 0;
    dc.reset(); exciteLp.reset(); bodyLp.reset(); loopLp.reset();
    activeMode = -1;
}

void Oscillator::updateUnison (const OscControl& c) noexcept
{
    numUnison = std::clamp (c.unison, 1, kMaxUnison);
    const float norm = 1.0f / std::sqrt ((float) numUnison);
    for (int u = 0; u < numUnison; ++u)
    {
        const float pos = numUnison > 1 ? ((float) u / (float) (numUnison - 1)) * 2.0f - 1.0f : 0.0f; // -1..1
        const float cents = pos * c.detuneCents;
        const float hz = c.hz * std::exp2 (cents * (1.0f / 1200.0f));
        inc[u] = clampf (hz / sr, 0.0f, 0.49f);
        float l, r;
        equalPower (clampf (c.pan + pos * c.spread, -1.0f, 1.0f), l, r);
        gainL[u] = l * norm;
        gainR[u] = r * norm;
    }
}

void Oscillator::noteOn (const OscControl& c, float velocity, uint32_t noteSeed)
{
    const bool modeChanged = c.mode != activeMode;
    activeMode = c.mode;
    velocityAmt = velocity;
    rng = FastRandom (noteSeed * 747796405u + 2891336453u);

    switch ((OscMode) c.mode)
    {
        case OscMode::VirtualAnalog:
        case OscMode::Wavetable:
        case OscMode::FM:
        {
            // Free-running random phases for unison (analog feel), deterministic first voice.
            for (int u = 0; u < kMaxUnison; ++u)
            {
                if (modeChanged || u > 0 || c.unison > 1) phase[u] = rng.nextUnipolar();
                modPhase[u] = phase[u];
                fbState[u] = 0.0f;
                triState[u] = 0.0f;
            }
            if (c.unison == 1) phase[0] = 0.0f, modPhase[0] = 0.0f;
            break;
        }
        case OscMode::Pluck:
        {
            lineA.clear();
            const float period = sr / clampf (c.hz, 16.0f, sr * 0.45f);
            const int len = std::max (2, (int) period);
            exciteLp.setCutoff (300.0f + 14000.0f * c.b * (0.6f + 0.4f * velocity), sr);
            // Excitation: filtered noise burst, minus a delayed copy (pick position comb).
            const int pick = std::max (1, (int) (len * (0.05f + 0.45f * c.c)));
            float exc[4096];
            const int n = std::min (len, 4096);
            float mean = 0.0f;
            for (int i = 0; i < n; ++i) { exc[i] = exciteLp.lp (rng.nextBipolar()); mean += exc[i]; }
            mean /= (float) n;
            for (int i = n - 1; i >= 0; --i)
            {
                const float x = exc[i] - mean;
                const float delayed = i >= pick ? exc[i - pick] - mean : 0.0f;
                exc[i] = x - 0.6f * delayed;
            }
            float peak = 1.0e-6f;
            for (int i = 0; i < n; ++i) peak = std::max (peak, std::fabs (exc[i]));
            for (int i = 0; i < n; ++i) lineA.push (exc[i] / peak * velocity);
            for (int i = n; i < len; ++i) lineA.push (0.0f);
            plLp = 0.0f;
            dc.reset();
            break;
        }
        case OscMode::Modal:
        {
            for (int i = 0; i < kModes; ++i) mY1[i] = mY2[i] = 0.0f;
            setupModal (c);
            // Mallet: raised-cosine pulse; harder mallets are shorter.
            exciteLen = std::max (2, (int) (sr * (0.0004f + 0.006f * (1.0f - c.b) * (1.1f - velocity * 0.5f))));
            exciteCount = exciteLen;
            exciteGain = velocity * 2.0f / (float) exciteLen; // constant pulse area regardless of mallet hardness
            break;
        }
        case OscMode::Waveguide:
        {
            lineA.clear(); lineB.clear();
            bowRamp = 0.0f; plLp = 0.0f;
            loopLp.reset(); bodyLp.reset(); dc.reset();
            break;
        }
        case OscMode::Additive:
        {
            addShape = -1;
            setupAdditive (c);
            for (int i = 0; i < kMaxPartials; ++i) pPhase[i] = 0.0f;
            break;
        }
    }
    lastLevel = c.level;
}

void Oscillator::setupModal (const OscControl& c) noexcept
{
    const int model = (int) c.shape % 6;
    const float t60Base = 0.15f * std::pow (60.0f, c.a); // 0.15 s .. 9 s
    const float stretch = 1.0f + 0.12f * c.c;
    const float hardness = c.b;
    numModes = 0;
    modalHz = c.hz;
    for (int m = 0; m < kModes; ++m)
    {
        const float ratio = std::pow (kModalRatios[model][m], stretch);
        const float f = c.hz * ratio;
        mRatio[m] = ratio;
        if (f >= sr * 0.45f) { mIn[m] = 0.0f; mB1[m] = mB2[m] = 0.0f; continue; }
        const float t60 = t60Base / (1.0f + 0.35f * (float) m * (1.2f - hardness * 0.5f));
        const float r = std::exp (-6.9078f / (t60 * sr));
        const float w = kTwoPi * f / sr;
        mR[m] = r;
        mB1[m] = 2.0f * r * std::cos (w);
        mB2[m] = -r * r;
        const float bright = std::pow (1.0f / (1.0f + (float) m), 1.6f * (1.0f - hardness));
        mIn[m] = kModalGains[model][m] * bright * std::sin (w) * 0.35f;
        numModes = m + 1;
    }
}

void Oscillator::setupAdditive (const OscControl& c) noexcept
{
    if (c.shape == addShape && c.a == addA && c.b == addB && c.c == addC) return;
    addShape = c.shape; addA = c.a; addB = c.b; addC = c.c;
    numPartials = 1 + (c.shape % kMaxPartials);
    const float slope = 0.3f + (1.0f - c.a) * 2.2f;    // brightness
    const float inharm = c.c * 0.0008f;
    float sum = 0.0f;
    for (int h = 1; h <= numPartials; ++h)
    {
        float amp = std::pow ((float) h, -slope);
        const bool even = (h % 2) == 0;
        // b: 0 = odd partials only (hollow), 0.5 = balanced, 1 = even emphasis
        const float oe = even ? clampf (c.b * 2.0f, 0.0f, 1.0f) : clampf (2.0f - c.b * 2.0f, 0.0f, 1.0f);
        amp *= (h == 1 ? 1.0f : oe);
        pAmp[h - 1] = amp;
        pRatio[h - 1] = (float) h * std::sqrt (1.0f + inharm * (float) (h * h));
        sum += amp;
    }
    const float norm = 1.0f / std::max (0.5f, std::sqrt (sum) * 1.2f);
    for (int h = 0; h < numPartials; ++h) pAmp[h] *= norm;
}

void Oscillator::render (const OscControl& c, float* L, float* R, int n) noexcept
{
    if (c.mode != activeMode) { activeMode = c.mode; }
    const float g0 = lastLevel, g1 = c.level;
    lastLevel = g1;
    if (g0 < 1.0e-6f && g1 < 1.0e-6f && c.mode <= (int) OscMode::FM) return;

    switch ((OscMode) c.mode)
    {
        case OscMode::VirtualAnalog: renderVA (c, L, R, n, g0, g1); break;
        case OscMode::Wavetable:     renderWT (c, L, R, n, g0, g1); break;
        case OscMode::FM:            renderFM (c, L, R, n, g0, g1); break;
        case OscMode::Pluck:         renderPluck (c, L, R, n, g0, g1); break;
        case OscMode::Modal:         renderModal (c, L, R, n, g0, g1); break;
        case OscMode::Waveguide:     renderWaveguide (c, L, R, n, g0, g1); break;
        case OscMode::Additive:      renderAdditive (c, L, R, n, g0, g1); break;
    }
}

void Oscillator::renderVA (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    updateUnison (c);
    const int shape = c.shape & 3;
    const float pw = 0.05f + 0.9f * c.a;
    const float dg = (g1 - g0) / (float) n;
    for (int u = 0; u < numUnison; ++u)
    {
        float ph = phase[u];
        const float dt = inc[u];
        const float gl = gainL[u], gr = gainR[u];
        float g = g0;
        float tri = triState[u];
        for (int i = 0; i < n; ++i)
        {
            float y;
            switch (shape)
            {
                case 0: y = (2.0f * ph - 1.0f) - polyBlep (ph, dt); break;
                case 1:
                {
                    y = (ph < pw ? 1.0f : -1.0f) + polyBlep (ph, dt);
                    float t2 = ph - pw; if (t2 < 0.0f) t2 += 1.0f;
                    y -= polyBlep (t2, dt);
                    y -= (2.0f * pw - 1.0f); // remove DC of asymmetric pulses
                    break;
                }
                case 2:
                {
                    // Band-limited triangle: leaky-integrated band-limited square
                    float sq = (ph < 0.5f ? 1.0f : -1.0f) + polyBlep (ph, dt);
                    float t2 = ph - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
                    sq -= polyBlep (t2, dt);
                    tri = 0.999f * tri + 4.0f * dt * sq;
                    y = tri;
                    break;
                }
                default: y = SineTable::get().lookup (ph); break;
            }
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            L[i] += y * gl * g;
            R[i] += y * gr * g;
            g += dg;
        }
        phase[u] = ph;
        triState[u] = undenormal (tri);
    }
}

void Oscillator::renderWT (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    auto& bank = WavetableBank::get();
    if (! bank.isReady()) return;
    updateUnison (c);
    const int table = std::clamp (c.shape, 0, WavetableBank::kNumTables - 1);
    const float dg = (g1 - g0) / (float) n;
    for (int u = 0; u < numUnison; ++u)
    {
        float ph = phase[u];
        const float dt = inc[u];
        float g = g0;
        for (int i = 0; i < n; ++i)
        {
            const float y = bank.read (table, c.a, ph, dt);
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            L[i] += y * gainL[u] * g;
            R[i] += y * gainR[u] * g;
            g += dg;
        }
        phase[u] = ph;
    }
}

void Oscillator::renderFM (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    updateUnison (c);
    const float ratio = kFmRatios[std::clamp ((int) (c.a * 15.0f + 0.5f), 0, 15)];
    // Reduce index as the modulator approaches Nyquist to limit aliasing.
    const float modHz = c.hz * ratio;
    const float nyqScale = clampf (1.0f - (modHz - sr * 0.15f) / (sr * 0.25f), 0.0f, 1.0f);
    const float index = c.b * c.b * 12.0f * nyqScale;
    const float fb = c.c * 1.2f;
    const float dg = (g1 - g0) / (float) n;
    const auto& sine = SineTable::get();
    for (int u = 0; u < numUnison; ++u)
    {
        float ph = phase[u], mph = modPhase[u], prev = fbState[u];
        const float dt = inc[u];
        const float mdt = clampf (dt * ratio, 0.0f, 0.49f);
        float g = g0;
        for (int i = 0; i < n; ++i)
        {
            float mp = mph + fb * prev * 0.159f;
            mp -= std::floor (mp);
            const float m = sine.lookup (mp);
            prev = 0.5f * (prev + m);
            float cp = ph + index * m * 0.159f;
            cp -= std::floor (cp);
            const float y = sine.lookup (cp);
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            mph += mdt; if (mph >= 1.0f) mph -= 1.0f;
            L[i] += y * gainL[u] * g;
            R[i] += y * gainR[u] * g;
            g += dg;
        }
        phase[u] = ph; modPhase[u] = mph; fbState[u] = prev;
    }
}

void Oscillator::renderPluck (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    const float hz = clampf (c.hz, 16.0f, sr * 0.45f);
    const float t60 = 0.25f * std::pow (50.0f, c.a);               // 0.25 s .. 12.5 s
    const float loopGain = std::pow (10.0f, -3.0f / (hz * t60));
    const float coef = clampf (0.08f + 0.9f * c.b * c.b, 0.05f, 0.995f);
    const float lpDelay = (1.0f - coef) / coef;                     // phase delay of the loop filter
    const float period = std::max (2.0f, sr / hz - lpDelay);
    float l, r; equalPower (c.pan, l, r);
    const float dg = (g1 - g0) / (float) n;
    float g = g0;
    for (int i = 0; i < n; ++i)
    {
        const float y = lineA.read (period);
        plLp += coef * (y - plLp);
        plLp = undenormal (plLp);
        lineA.push (plLp * loopGain);
        const float out = dc.process (y) * g;
        L[i] += out * l; R[i] += out * r;
        g += dg;
    }
}

void Oscillator::renderModal (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    if (std::fabs (c.hz - modalHz) > modalHz * 0.0005f)
    {
        // Pitch moved (bend / glide): retune resonators, keep their energy.
        modalHz = c.hz;
        for (int m = 0; m < numModes; ++m)
        {
            const float f = c.hz * mRatio[m];
            if (f >= sr * 0.45f) { mIn[m] = 0.0f; continue; }
            mB1[m] = 2.0f * mR[m] * std::cos (kTwoPi * f / sr);
        }
    }
    float l, r; equalPower (c.pan, l, r);
    const float dg = (g1 - g0) / (float) n;
    float g = g0;
    for (int i = 0; i < n; ++i)
    {
        float x = 0.0f;
        if (exciteCount > 0)
        {
            const float t = 1.0f - (float) exciteCount / (float) exciteLen;
            x = 0.5f * (1.0f - fastSin01 (t + 0.25f)) * exciteGain;
            --exciteCount;
        }
        float y = 0.0f;
        for (int m = 0; m < numModes; ++m)
        {
            const float v = mB1[m] * mY1[m] + mB2[m] * mY2[m] + x * mIn[m];
            mY2[m] = mY1[m];
            mY1[m] = undenormal (v);
            y += v;
        }
        y *= g;
        L[i] += y * l; R[i] += y * r;
        g += dg;
    }
}

void Oscillator::renderWaveguide (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    const float hz = clampf (c.hz, 20.0f, sr * 0.3f);
    float l, r; equalPower (c.pan, l, r);
    const float dg = (g1 - g0) / (float) n;
    float g = g0;
    const float rampInc = 1.0f / (0.03f * sr);
    loopLp.setCutoff (800.0f + 12000.0f * c.b, sr);

    if ((c.shape & 1) == 0)
    {
        // Blown (single-reed): bore delay line + reed reflection table.
        const float delay = std::max (2.0f, sr / hz * 0.5f - 1.5f);
        const float breath = (0.55f + 0.42f * c.a) * (0.85f + 0.15f * velocityAmt);
        const float noiseAmt = c.c * 0.25f;
        for (int i = 0; i < n; ++i)
        {
            bowRamp = std::min (1.0f, bowRamp + rampInc);
            const float pressure = breath * bowRamp * (1.0f + noiseAmt * rng.nextBipolar());
            const float boreOut = lineA.read (delay);
            plLp = 0.5f * (boreOut + plLp); // one-zero reflection filter
            const float filtered = loopLp.lp (plLp);
            const float diff = -0.95f * filtered - pressure;
            const float reed = clampf (0.7f - 0.3f * diff, -1.0f, 1.0f);
            lineA.push (undenormal (pressure + diff * reed));
            const float out = dc.process (boreOut) * g * 0.25f;
            L[i] += out * l; R[i] += out * r;
            g += dg;
        }
    }
    else
    {
        // Bowed string: nut + bridge delay lines with a friction (bow) table.
        const float base = std::max (4.0f, sr / hz - 4.0f);
        const float beta = 0.08f + 0.2f * c.c;
        const float bridgeD = std::max (2.0f, base * beta);
        const float neckD = std::max (2.0f, base * (1.0f - beta));
        const float slope = 5.0f - 4.0f * c.a;
        const float maxVel = 0.03f + 0.2f * velocityAmt;
        const float stringPole = clampf (0.75f - 0.2f * 22050.0f / sr, 0.0f, 0.99f);
        bodyLp.setCutoff (500.0f + 6000.0f * c.b, sr);
        for (int i = 0; i < n; ++i)
        {
            bowRamp = std::min (1.0f, bowRamp + rampInc * 0.5f);
            const float bowVel = maxVel * bowRamp;
            const float neckOut = lineA.read (neckD);
            const float bridgeOut = lineB.read (bridgeD);
            plLp = (1.0f - stringPole) * bridgeOut + stringPole * plLp;
            plLp = undenormal (plLp);
            const float bridgeRefl = -0.95f * plLp;
            const float nutRefl = -neckOut;
            const float stringVel = bridgeRefl + nutRefl;
            const float velDiff = bowVel - stringVel;
            float bt = std::pow (std::fabs ((velDiff + 0.001f) * slope) + 0.75f, -4.0f);
            bt = std::min (bt, 1.0f);
            const float newVel = velDiff * bt;
            lineA.push (bridgeRefl + newVel);
            lineB.push (nutRefl + newVel);
            const float out = dc.process (bodyLp.lp (bridgeOut)) * 4.0f * g;
            L[i] += out * l; R[i] += out * r;
            g += dg;
        }
    }
}

void Oscillator::renderAdditive (const OscControl& c, float* L, float* R, int n, float g0, float g1) noexcept
{
    setupAdditive (c);
    float l, r; equalPower (c.pan, l, r);
    const auto& sine = SineTable::get();
    const float base = c.hz / sr;
    const float dg = (g1 - g0) / (float) n;
    for (int h = 0; h < numPartials; ++h)
    {
        const float dt = base * pRatio[h];
        if (dt >= 0.48f) break;
        // Gentle fade of partials approaching Nyquist
        const float amp = pAmp[h] * clampf ((0.48f - dt) * 20.0f, 0.0f, 1.0f);
        float ph = pPhase[h];
        float g = g0;
        for (int i = 0; i < n; ++i)
        {
            const float y = sine.lookup (ph) * amp * g;
            ph += dt; if (ph >= 1.0f) ph -= 1.0f;
            L[i] += y * l; R[i] += y * r;
            g += dg;
        }
        pPhase[h] = ph;
    }
}

} // namespace mbx
