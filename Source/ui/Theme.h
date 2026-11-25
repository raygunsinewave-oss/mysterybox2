// Mystery Box Synthesizer by Raygun
// Visual theme: colours, bundled OFL fonts, procedural textures and icons.
// Everything is drawn in code -- there are no bitmap art assets.
#pragma once

#include <JuceHeader.h>

namespace mbx::theme
{
// Palette: concrete greys, rust, chalk, neon green + "wonder" cyan
const juce::Colour bg        { 0xff15171a };
const juce::Colour concrete  { 0xff2a2d31 };
const juce::Colour concreteL { 0xff3a3e43 };
const juce::Colour panel     { 0xff1d2024 };
const juce::Colour rust      { 0xff8a4a22 };
const juce::Colour rustDark  { 0xff4a2612 };
const juce::Colour metal     { 0xff5d6166 };
const juce::Colour chalk     { 0xffe8e4d8 };
const juce::Colour chalkDim  { 0xff9c998f };
const juce::Colour neon      { 0xff39ff14 };
const juce::Colour wonder    { 0xff00e5ff };
const juce::Colour danger    { 0xffff4a3d };
const juce::Colour gold      { 0xffffc233 };

juce::Colour rarityTint (int rarity);

/** Logical design size of the editor; everything scales from here. */
constexpr int kDesignW = 1200, kDesignH = 800;

// Fonts (Google Fonts, SIL Open Font License -- see Resources/fonts/*-OFL.txt)
juce::Font stencil (float height);   // Black Ops One
juce::Font chalkFont (float height); // Cabin Sketch Bold
juce::Font mono (float height);      // Share Tech Mono
juce::Font ui (float height, bool bold = false); // Oswald

/** Deterministic grungy concrete texture (with cracks and stains) for a rectangle. */
void drawConcrete (juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed, float darkness = 0.0f);
/** A rusted metal plate with rivets. */
void drawRustPlate (juce::Graphics& g, juce::Rectangle<float> r, uint32_t seed, float cornerRadius = 6.0f);
/** Wobbly chalk-like stroke along a path. */
void strokeChalk (juce::Graphics& g, const juce::Path& p, juce::Colour c, float thickness, uint32_t seed);
/** Text with a slight chalk smear. */
void drawChalkText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float height,
                    juce::Justification j = juce::Justification::centred, uint32_t seed = 1);
/** Soft radial glow. */
void drawGlow (juce::Graphics& g, juce::Point<float> c, float radius, juce::Colour colour, float alpha);
/** Category icon (0..24) drawn as vector art into r. */
void drawCategoryIcon (juce::Graphics& g, int category, juce::Rectangle<float> r, juce::Colour c);
/** Rarity badge pill. */
void drawRarityBadge (juce::Graphics& g, int rarity, juce::Rectangle<float> r, float pulse = 0.0f);

/** Renders a static layer once at the device's physical pixel density and re-uses it. */
class CachedLayer
{
public:
    template <typename PaintFn>
    void draw (juce::Graphics& g, juce::Rectangle<int> logicalBounds, PaintFn&& paintFn)
    {
        const float scale = juce::jmax (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        if (! cache.isValid() || cachedScale != scale || cachedSize != juce::Point<int> (logicalBounds.getWidth(), logicalBounds.getHeight()) || dirty)
        {
            cachedScale = scale; cachedSize = { logicalBounds.getWidth(), logicalBounds.getHeight() }; dirty = false;
            const int w = juce::jmax (1, juce::roundToInt ((float) cachedSize.x * scale));
            const int h = juce::jmax (1, juce::roundToInt ((float) cachedSize.y * scale));
            cache = juce::Image (juce::Image::ARGB, w, h, true);
            juce::Graphics ig (cache);
            ig.addTransform (juce::AffineTransform::scale (scale));
            paintFn (ig);
        }
        g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cachedScale)
                                           .translated ((float) logicalBounds.getX(), (float) logicalBounds.getY()));
    }
    void invalidate() noexcept { dirty = true; }

private:
    juce::Image cache;
    float cachedScale = 0.0f;
    juce::Point<int> cachedSize;
    bool dirty = true;
};

} // namespace mbx::theme
