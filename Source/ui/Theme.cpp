// Mystery Box Synthesizer by Raygun
#include "Theme.h"
#include <BinaryData.h>

namespace mbx::theme
{
namespace
{
struct Fonts
{
    juce::Typeface::Ptr stencil, chalk, mono, ui;
    Fonts()
    {
        stencil = juce::Typeface::createSystemTypefaceFor (BinaryData::BlackOpsOneRegular_ttf, BinaryData::BlackOpsOneRegular_ttfSize);
        chalk = juce::Typeface::createSystemTypefaceFor (BinaryData::CabinSketchBold_ttf, BinaryData::CabinSketchBold_ttfSize);
        mono = juce::Typeface::createSystemTypefaceFor (BinaryData::ShareTechMonoRegular_ttf, BinaryData::ShareTechMonoRegular_ttfSize);
        ui = juce::Typeface::createSystemTypefaceFor (BinaryData::Oswald_ttf, BinaryData::Oswald_ttfSize);
    }
};
Fonts& fonts()
{
    static Fonts f;
    return f;
}

struct Rng
{
    uint32_t s;
    explicit Rng (uint32_t seed) : s (seed * 2654435761u + 0x9E3779B9u) { if (s == 0) s = 1; }
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xFFFFFF) / 16777216.0f; }
    float range (float a, float b) { return a + (b - a) * next(); }
};

juce::Font makeFont (juce::Typeface::Ptr tf, float h, bool bold = false)
{
    if (tf == nullptr) return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
    { juce::Font f (juce::FontOptions (tf).withHeight (h)); return bold ? f.boldened() : f; }
}
} // namespace

juce::Colour rarityTint (int rarity)
{
    static const juce::Colour c[] = { juce::Colour (0xffa8a8a8), juce::Colour (0xff3d8bff), juce::Colour (0xffb04dff), juce::Colour (0xffffc233), juce::Colour (0xff00e5ff) };
    return c[juce::jlimit (0, 4, rarity)];
}

juce::Font stencil (float h) { return makeFont (fonts().stencil, h); }
juce::Font chalkFont (float h) { return makeFont (fonts().chalk, h); }
juce::Font mono (float h) { return makeFont (fonts().mono, h); }
juce::Font ui (float h, bool bold) { return makeFont (fonts().ui, h, bold); }

void drawConcrete (juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed, float darkness)
{
    Rng rng (seed);
    g.setColour (concrete.darker (0.25f + darkness));
    g.fillRect (r);
    // Large stains / damp patches
    for (int i = 0; i < 7; ++i)
    {
        const float cx = r.getX() + rng.next() * r.getWidth(), cy = r.getY() + rng.next() * r.getHeight();
        const float rad = rng.range (0.15f, 0.45f) * juce::jmax (r.getWidth(), r.getHeight());
        juce::ColourGradient grad (juce::Colours::black.withAlpha (rng.range (0.05f, 0.18f)), cx, cy,
                                   juce::Colours::transparentBlack, cx + rad, cy, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
    }
    // Speckle grain
    const int speckles = (int) (r.getWidth() * r.getHeight() / 55.0f);
    for (int i = 0; i < speckles; ++i)
    {
        const float x = r.getX() + rng.next() * r.getWidth(), y = r.getY() + rng.next() * r.getHeight();
        const float sz = rng.range (0.6f, 2.2f);
        const bool light = rng.next() < 0.5f;
        g.setColour ((light ? juce::Colours::white : juce::Colours::black).withAlpha (rng.range (0.03f, 0.11f)));
        g.fillRect (x, y, sz, sz);
    }
    // Cracks
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    const int cracks = 2 + (int) (rng.next() * 3.0f);
    for (int c = 0; c < cracks; ++c)
    {
        juce::Path p;
        float x = r.getX() + rng.next() * r.getWidth(), y = r.getY() + rng.next() * r.getHeight();
        p.startNewSubPath (x, y);
        float ang = rng.range (0.0f, 6.28f);
        const int segs = 6 + (int) (rng.next() * 10);
        for (int s = 0; s < segs; ++s)
        {
            ang += rng.range (-0.7f, 0.7f);
            x += std::cos (ang) * rng.range (6.0f, 18.0f);
            y += std::sin (ang) * rng.range (6.0f, 18.0f);
            p.lineTo (x, y);
        }
        g.strokePath (p, juce::PathStrokeType (rng.range (0.6f, 1.4f)));
    }
}

void drawRustPlate (juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed, float cornerRadius)
{
    Rng rng (seed);
    juce::ColourGradient base (metal.darker (0.3f), r.getX(), r.getY(), metal.darker (0.9f), r.getRight(), r.getBottom(), false);
    g.setGradientFill (base);
    g.fillRoundedRectangle (r, cornerRadius);
    juce::Graphics::ScopedSaveState ss (g);
    juce::Path clip; clip.addRoundedRectangle (r, cornerRadius);
    g.reduceClipRegion (clip);
    // Rust blotches
    for (int i = 0; i < 18; ++i)
    {
        const float cx = r.getX() + rng.next() * r.getWidth(), cy = r.getY() + rng.next() * r.getHeight();
        const float rad = rng.range (6.0f, 40.0f);
        juce::ColourGradient grad ((rng.next() < 0.5f ? rust : rustDark).withAlpha (rng.range (0.25f, 0.6f)), cx, cy,
                                   juce::Colours::transparentBlack, cx + rad, cy, true);
        g.setGradientFill (grad);
        g.fillEllipse (cx - rad, cy - rad, rad * 2, rad * 2);
    }
    const int scratches = (int) (r.getWidth() * r.getHeight() / 900.0f) + 6;
    for (int i = 0; i < scratches; ++i)
    {
        const float x = r.getX() + rng.next() * r.getWidth(), y = r.getY() + rng.next() * r.getHeight();
        g.setColour (juce::Colours::white.withAlpha (rng.range (0.03f, 0.1f)));
        g.drawLine (x, y, x + rng.range (-20.0f, 20.0f), y + rng.range (-3.0f, 3.0f), 0.6f);
    }
    // Bevel + rivets
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (1.0f), cornerRadius, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r, cornerRadius, 1.2f);
    const float inset = 8.0f;
    for (auto p : { r.getTopLeft() + juce::Point<float> (inset, inset), r.getTopRight() + juce::Point<float> (-inset, inset),
                    r.getBottomLeft() + juce::Point<float> (inset, -inset), r.getBottomRight() + juce::Point<float> (-inset, -inset) })
    {
        juce::ColourGradient rv (metal.brighter (0.4f), p.x - 1.5f, p.y - 1.5f, metal.darker (1.0f), p.x + 2.5f, p.y + 2.5f, true);
        g.setGradientFill (rv);
        g.fillEllipse (p.x - 3.0f, p.y - 3.0f, 6.0f, 6.0f);
    }
}

void strokeChalk (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, uint32_t seed)
{
    Rng rng (seed);
    for (int pass = 0; pass < 3; ++pass)
    {
        juce::Path jittered;
        const float j = thickness * 0.35f;
        jittered.addPath (p, juce::AffineTransform::translation (rng.range (-j, j), rng.range (-j, j)));
        g.setColour (c.withAlpha (pass == 0 ? 0.85f : 0.25f));
        g.strokePath (jittered, juce::PathStrokeType (thickness * (pass == 0 ? 1.0f : 1.6f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void drawChalkText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float height,
                    juce::Justification j, uint32_t seed)
{
    Rng rng (seed);
    g.setFont (chalkFont (height));
    g.setColour (c.withAlpha (0.18f));
    g.drawText (text, r.translated (rng.range (-1.2f, 1.2f), rng.range (-1.0f, 1.0f)), j, false);
    g.setColour (c.withAlpha (0.92f));
    g.drawText (text, r, j, false);
}

void drawGlow (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float alpha)
{
    if (alpha <= 0.001f || radius <= 0.5f) return;
    juce::ColourGradient grad (colour.withAlpha (juce::jlimit (0.0f, 1.0f, alpha)), c.x, c.y, colour.withAlpha (0.0f), c.x + radius, c.y, true);
    g.setGradientFill (grad);
    g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
}

void drawRarityBadge (juce::Graphics& g, int rarity, juce::Rectangle<float> r, float pulse)
{
    const auto c = rarityTint (rarity);
    if (rarity >= 3) drawGlow (g, r.getCentre(), r.getWidth() * 0.8f, c, 0.25f + 0.2f * pulse);
    g.setColour (c.withAlpha (0.2f));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (c);
    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.4f);
    g.setFont (stencil (r.getHeight() * 0.62f));
    static const char* names[] = { "COMMON", "RARE", "EPIC", "LEGENDARY", "WONDER" };
    g.drawText (names[juce::jlimit (0, 4, rarity)], r, juce::Justification::centred, false);
}

void drawCategoryIcon (juce::Graphics& g, int cat, juce::Rectangle<float> r, juce::Colour c)
{
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    auto X = [&] (float fx) { return x + fx * w; };
    auto Y = [&] (float fy) { return y + fy * h; };
    const float t = juce::jmax (1.2f, w * 0.07f);
    juce::PathStrokeType stroke (t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    g.setColour (c);
    switch (cat)
    {
        case 0: // grand piano lid + keys
            p.startNewSubPath (X (0.1f), Y (0.7f)); p.lineTo (X (0.1f), Y (0.35f)); p.quadraticTo (X (0.5f), Y (0.1f), X (0.9f), Y (0.5f)); p.lineTo (X (0.9f), Y (0.7f)); p.closeSubPath();
            g.strokePath (p, stroke);
            for (int k = 0; k < 5; ++k) g.fillRect (X (0.18f + 0.15f * (float) k), Y (0.58f), w * 0.06f, h * 0.12f);
            break;
        case 1: // electric piano: tine bars
            for (int k = 0; k < 5; ++k) g.fillRoundedRectangle (X (0.12f + 0.16f * (float) k), Y (0.25f + 0.08f * (float) k), w * 0.08f, h * (0.5f - 0.08f * (float) k), w * 0.03f);
            p.startNewSubPath (X (0.05f), Y (0.85f)); p.lineTo (X (0.95f), Y (0.85f)); g.strokePath (p, stroke);
            break;
        case 2: // organ pipes
            for (int k = 0; k < 5; ++k) { const float ph = 0.3f + 0.12f * (float) (k < 3 ? k : 4 - k); g.drawRect (X (0.1f + 0.17f * (float) k), Y (0.85f - ph), w * 0.12f, h * ph, t * 0.7f); }
            break;
        case 3: // bell
            p.startNewSubPath (X (0.2f), Y (0.75f)); p.quadraticTo (X (0.3f), Y (0.15f), X (0.5f), Y (0.15f)); p.quadraticTo (X (0.7f), Y (0.15f), X (0.8f), Y (0.75f)); p.closeSubPath();
            g.strokePath (p, stroke); g.fillEllipse (X (0.44f), Y (0.76f), w * 0.12f, h * 0.12f);
            break;
        case 4: // guitar body
            g.drawEllipse (X (0.15f), Y (0.45f), w * 0.4f, h * 0.4f, t); g.drawEllipse (X (0.22f), Y (0.3f), w * 0.28f, h * 0.28f, t);
            p.startNewSubPath (X (0.45f), Y (0.55f)); p.lineTo (X (0.9f), Y (0.1f)); g.strokePath (p, stroke);
            break;
        case 5: // violin f-holes + bow
            p.startNewSubPath (X (0.3f), Y (0.2f)); p.cubicTo (X (0.15f), Y (0.4f), X (0.45f), Y (0.6f), X (0.3f), Y (0.8f));
            p.startNewSubPath (X (0.55f), Y (0.2f)); p.cubicTo (X (0.4f), Y (0.4f), X (0.7f), Y (0.6f), X (0.55f), Y (0.8f));
            p.startNewSubPath (X (0.1f), Y (0.9f)); p.lineTo (X (0.95f), Y (0.15f)); g.strokePath (p, stroke);
            break;
        case 6: // trumpet
            p.startNewSubPath (X (0.05f), Y (0.5f)); p.lineTo (X (0.6f), Y (0.5f)); p.lineTo (X (0.9f), Y (0.25f)); p.lineTo (X (0.9f), Y (0.75f)); p.lineTo (X (0.6f), Y (0.5f));
            p.addRoundedRectangle (X (0.25f), Y (0.3f), w * 0.25f, h * 0.18f, w * 0.05f); g.strokePath (p, stroke);
            break;
        case 7: // flute with holes
            g.drawRoundedRectangle (X (0.05f), Y (0.42f), w * 0.9f, h * 0.16f, h * 0.08f, t);
            for (int k = 0; k < 4; ++k) g.fillEllipse (X (0.3f + 0.15f * (float) k), Y (0.46f), w * 0.07f, h * 0.07f);
            break;
        case 8: // saw wave with spark
            p.startNewSubPath (X (0.05f), Y (0.75f)); for (int k = 0; k < 3; ++k) { p.lineTo (X (0.05f + 0.3f * (float) (k + 1)), Y (0.25f)); p.lineTo (X (0.05f + 0.3f * (float) (k + 1)), Y (0.75f)); }
            g.strokePath (p, stroke);
            break;
        case 9: // square wave
            p.startNewSubPath (X (0.05f), Y (0.7f)); p.lineTo (X (0.05f), Y (0.3f)); p.lineTo (X (0.35f), Y (0.3f)); p.lineTo (X (0.35f), Y (0.7f)); p.lineTo (X (0.65f), Y (0.7f)); p.lineTo (X (0.65f), Y (0.3f)); p.lineTo (X (0.95f), Y (0.3f));
            g.strokePath (p, stroke);
            break;
        case 10: // bass clef-ish
            p.startNewSubPath (X (0.25f), Y (0.35f)); p.cubicTo (X (0.3f), Y (0.05f), X (0.85f), Y (0.15f), X (0.65f), Y (0.5f)); p.quadraticTo (X (0.5f), Y (0.75f), X (0.2f), Y (0.9f));
            g.strokePath (p, stroke); g.fillEllipse (X (0.8f), Y (0.28f), w * 0.1f, h * 0.1f); g.fillEllipse (X (0.8f), Y (0.5f), w * 0.1f, h * 0.1f); g.fillEllipse (X (0.18f), Y (0.3f), w * 0.14f, h * 0.14f);
            break;
        case 11: // speaker cone
            g.drawEllipse (X (0.1f), Y (0.1f), w * 0.8f, h * 0.8f, t); g.drawEllipse (X (0.3f), Y (0.3f), w * 0.4f, h * 0.4f, t); g.fillEllipse (X (0.43f), Y (0.43f), w * 0.14f, h * 0.14f);
            break;
        case 12: // wobble wave
            p.startNewSubPath (X (0.05f), Y (0.5f));
            for (int k = 1; k <= 40; ++k) { const float fx = (float) k / 40.0f; p.lineTo (X (0.05f + 0.9f * fx), Y (0.5f + 0.32f * std::sin (fx * 18.0f) * std::sin (fx * 3.14f))); }
            g.strokePath (p, stroke);
            break;
        case 13: // cloud (warm pad)
            p.addEllipse (X (0.1f), Y (0.4f), w * 0.35f, h * 0.35f); p.addEllipse (X (0.3f), Y (0.25f), w * 0.4f, h * 0.45f); p.addEllipse (X (0.55f), Y (0.4f), w * 0.35f, h * 0.35f);
            g.fillPath (p);
            break;
        case 14: // crescent moon (dark pad)
        {
            juce::Path a; a.addEllipse (X (0.15f), Y (0.15f), w * 0.7f, h * 0.7f);
            juce::Path b; b.addEllipse (X (0.35f), Y (0.05f), w * 0.6f, h * 0.6f);
            g.fillPath (a);
            g.setColour (juce::Colours::black.withAlpha (0.85f)); g.fillPath (b);
            break;
        }
        case 15: // spiral
            p.startNewSubPath (X (0.5f), Y (0.5f));
            for (int k = 1; k <= 60; ++k) { const float a = (float) k * 0.3f, rr = 0.006f * (float) k; p.lineTo (X (0.5f + rr * std::cos (a)), Y (0.5f + rr * std::sin (a))); }
            g.strokePath (p, stroke);
            break;
        case 16: // guitar pick
            p.startNewSubPath (X (0.5f), Y (0.9f)); p.cubicTo (X (0.1f), Y (0.5f), X (0.15f), Y (0.1f), X (0.5f), Y (0.12f)); p.cubicTo (X (0.85f), Y (0.1f), X (0.9f), Y (0.5f), X (0.5f), Y (0.9f));
            g.strokePath (p, stroke);
            break;
        case 17: // stairs (arp)
            p.startNewSubPath (X (0.05f), Y (0.85f)); for (int k = 0; k < 4; ++k) { p.lineTo (X (0.05f + 0.22f * (float) k), Y (0.85f - 0.2f * (float) (k + 1))); p.lineTo (X (0.05f + 0.22f * (float) (k + 1)), Y (0.85f - 0.2f * (float) (k + 1))); }
            g.strokePath (p, stroke);
            break;
        case 18: // open mouth / choir
            g.drawEllipse (X (0.2f), Y (0.25f), w * 0.6f, h * 0.5f, t); g.fillEllipse (X (0.35f), Y (0.42f), w * 0.3f, h * 0.2f);
            break;
        case 19: // globe
            g.drawEllipse (X (0.1f), Y (0.1f), w * 0.8f, h * 0.8f, t); g.drawEllipse (X (0.32f), Y (0.1f), w * 0.36f, h * 0.8f, t * 0.8f);
            p.startNewSubPath (X (0.1f), Y (0.5f)); p.lineTo (X (0.9f), Y (0.5f)); g.strokePath (p, stroke);
            break;
        case 20: // pixel invader-free 8-bit heart
        {
            static const char* rows[] = { ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..." };
            const float cs = w / 8.0f;
            for (int ry = 0; ry < 6; ++ry) for (int rx = 0; rx < 7; ++rx) if (rows[ry][rx] == 'X') g.fillRect (x + cs * (0.5f + (float) rx), y + cs * (1.0f + (float) ry), cs * 0.9f, cs * 0.9f);
            break;
        }
        case 21: // starburst stab
            for (int k = 0; k < 8; ++k) { const float a = (float) k * 0.785f; p.startNewSubPath (X (0.5f), Y (0.5f)); p.lineTo (X (0.5f + 0.42f * std::cos (a)), Y (0.5f + 0.42f * std::sin (a))); }
            g.strokePath (p, stroke);
            break;
        case 22: // rising arrow
            p.startNewSubPath (X (0.1f), Y (0.85f)); p.quadraticTo (X (0.5f), Y (0.8f), X (0.8f), Y (0.2f)); p.startNewSubPath (X (0.6f), Y (0.22f)); p.lineTo (X (0.82f), Y (0.15f)); p.lineTo (X (0.88f), Y (0.38f));
            g.strokePath (p, stroke);
            break;
        case 23: // film clapper
            g.drawRect (X (0.1f), Y (0.4f), w * 0.8f, h * 0.45f, t);
            for (int k = 0; k < 4; ++k) { p.startNewSubPath (X (0.1f + 0.2f * (float) k), Y (0.4f)); p.lineTo (X (0.25f + 0.2f * (float) k), Y (0.18f)); }
            p.startNewSubPath (X (0.1f), Y (0.4f)); p.lineTo (X (0.85f), Y (0.15f)); g.strokePath (p, stroke);
            break;
        default: // 24 wonder: atom with orbiting electrons
            for (int k = 0; k < 3; ++k)
            {
                juce::Path o; o.addEllipse (X (0.08f), Y (0.36f), w * 0.84f, h * 0.28f);
                o.applyTransform (juce::AffineTransform::rotation ((float) k * 1.047f, X (0.5f), Y (0.5f)));
                g.strokePath (o, juce::PathStrokeType (t * 0.8f));
            }
            g.fillEllipse (X (0.42f), Y (0.42f), w * 0.16f, h * 0.16f);
            break;
    }
}

} // namespace mbx::theme
