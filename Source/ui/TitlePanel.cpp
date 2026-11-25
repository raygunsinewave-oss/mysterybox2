// Mystery Box Synthesizer by Raygun
#include "TitlePanel.h"
#include "../PluginProcessor.h"

namespace mbx
{
using namespace theme;

namespace
{
juce::String commas (juce::int64 v)
{
    juce::String s (v), out;
    int c = 0;
    for (int i = s.length() - 1; i >= 0; --i) { out = juce::String::charToString (s[i]) + out; if (++c % 3 == 0 && i > 0) out = "," + out; }
    return out;
}
const char* keyName (int root) { static const char* n[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; return n[root % 12]; }
} // namespace

TitlePanel::TitlePanel (MysteryBoxAudioProcessor& p) : proc (p)
{
    setTitle ("Mystery Box Synthesizer by Raygun");
    setDescription ("Title. Some names hide more than they say.");
    nextFlickerAt = juce::Time::getMillisecondCounterHiRes() + 8000.0 + rng.nextDouble() * 7000.0;
    startTimerHz (10); // slow idle tick; speeds up only while animating
}

void TitlePanel::resized() { plate.invalidate(); }

juce::Rectangle<float> TitlePanel::titleArea() const { return { 14.0f, 18.0f, (float) getWidth() - 28.0f, 118.0f }; }
juce::Rectangle<float> TitlePanel::leverArea() const { return { 16.0f, (float) getHeight() - 56.0f, (float) getWidth() - 32.0f, 40.0f }; }

void TitlePanel::triggerFlicker (float seconds)
{
    flickerUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    startTimerHz (30);
}

void TitlePanel::mouseMove (const juce::MouseEvent& e)
{
    const bool overLever = proc.getStore().getFlag ("egg") && leverArea().contains (e.position);
    setMouseCursor (overLever ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void TitlePanel::mouseDown (const juce::MouseEvent& e)
{
    if (proc.getStore().getFlag ("egg") && leverArea().contains (e.position))
    {
        if (onOpenOverclocker) onOpenOverclocker();
        return;
    }
    if (! titleArea().contains (e.position)) return;
    const double now = juce::Time::getMillisecondCounterHiRes();
    clickTimes.add (now);
    while (! clickTimes.isEmpty() && now - clickTimes.getFirst() > 2000.0) clickTimes.remove (0);
    // A small chalk note doodle appears and fades: a hint that something is here
    doodles.add ({ e.position + juce::Point<float> (rng.nextFloat() * 30.0f - 15.0f, -18.0f), 1.5f, (uint32_t) rng.nextInt() });
    if (clickTimes.size() >= 3)
    {
        clickTimes.clear();
        if (onSecretUnlocked) onSecretUnlocked();
    }
    startTimerHz (30);
    repaint();
}

void TitlePanel::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (now >= nextFlickerAt)
    {
        flickerUntil = now + 500.0 + rng.nextDouble() * 500.0;
        nextFlickerAt = now + 8000.0 + rng.nextDouble() * 7000.0; // every 8-15 s
        startTimerHz (30);
    }
    const bool flickering = now < flickerUntil;
    flickerAlpha = flickering ? (rng.nextFloat() < 0.45f ? 0.25f + rng.nextFloat() * 0.3f : 1.0f) : 1.0f;
    for (int i = doodles.size(); --i >= 0;)
    {
        doodles.getReference (i).life -= 1.0f / 30.0f;
        if (doodles[i].life <= 0.0f) doodles.remove (i);
    }
    if (! flickering && doodles.isEmpty() && getTimerInterval() < 100) startTimerHz (10);
    if (++tick % 5 == 0 || flickering || ! doodles.isEmpty()) repaint();
}

void TitlePanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds();
    plate.draw (g, b, [b] (juce::Graphics& pg)
    {
        drawConcrete (pg, b.toFloat(), 21, 0.25f);
        pg.setColour (juce::Colours::black.withAlpha (0.5f));
        pg.drawRect (b.toFloat(), 1.5f);
    });

    // Logotype
    const auto ta = titleArea();
    const float fa = flickerAlpha;
    drawGlow (g, ta.getCentre().translated (0, -16), 120.0f, neon, 0.16f * fa);
    g.setFont (stencil (35.0f));
    g.setColour (neon.withAlpha (0.25f * fa));
    g.drawText ("MYSTERY", ta.withHeight (38).translated (1.5f, 1.5f), juce::Justification::centred, false);
    g.setColour (neon.withAlpha (fa));
    g.drawText ("MYSTERY", ta.withHeight (38), juce::Justification::centred, false);
    g.setColour (neon.withAlpha (0.9f * fa));
    g.drawText ("BOX", ta.withY (ta.getY() + 34).withHeight (38), juce::Justification::centred, false);
    g.setFont (stencil (21.0f));
    g.setColour (chalk.withAlpha (0.92f * juce::jmax (0.6f, fa)));
    g.drawText ("SYNTHESIZER", ta.withY (ta.getY() + 72).withHeight (24), juce::Justification::centred, false);
    drawChalkText (g, "by Raygun", ta.withY (ta.getY() + 96).withHeight (22), chalkDim, 19.0f);

    for (const auto& d : doodles)
    {
        const float a = juce::jlimit (0.0f, 1.0f, d.life / 1.5f);
        juce::Path note;
        const auto p = d.pos;
        note.addEllipse (p.x - 5.0f, p.y + 6.0f, 8.0f, 6.0f);
        note.startNewSubPath (p.x + 3.0f, p.y + 9.0f); note.lineTo (p.x + 3.0f, p.y - 8.0f); note.quadraticTo (p.x + 10.0f, p.y - 4.0f, p.x + 9.0f, p.y + 2.0f);
        strokeChalk (g, note, chalk.withAlpha (a), 1.4f, d.seed);
    }

    // Status card
    auto card = juce::Rectangle<float> (14.0f, 150.0f, (float) getWidth() - 28.0f, 200.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (metal.withAlpha (0.6f));
    g.drawRoundedRectangle (card, 6.0f, 1.0f);
    auto row = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
    {
        auto r = card.removeFromTop (31.0f).reduced (10.0f, 4.0f);
        g.setColour (chalkDim);
        g.setFont (stencil (11.5f));
        g.drawText (label, r, juce::Justification::centredLeft, false);
        g.setColour (c);
        g.setFont (mono (17.0f));
        g.drawText (value, r, juce::Justification::centredRight, false);
    };
    card.removeFromTop (6.0f);
    int root; bool minor;
    proc.getChimeKey (root, minor);
    auto& st = proc.getStore();
    row ("ROUND", juce::String (proc.getGame().round), juce::Colour (0xffd23b2a));
    row ("POINTS", commas (proc.getPoints()), neon);
    row ("SPIN COST", proc.getGame().getSpinCost() == 0 ? juce::String ("FREE") : commas (proc.getGame().getSpinCost()), chalk);
    row ("DISCOVERED", juce::String (st.getDiscoveredCount()) + " / 2,500", gold);
    row ("CHIME KEY", juce::String (keyName (root)) + (minor ? " minor" : " major"), wonder);
    row ("HOST TEMPO", juce::String (proc.getHostBpm(), 1) + " BPM", chalk);

    if (st.getFlag ("egg"))
    {
        const auto lever = leverArea();
        const float pulse = 0.5f + 0.5f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.004f);
        drawGlow (g, lever.getCentre(), lever.getWidth() * 0.6f, wonder, 0.12f + 0.1f * pulse);
        g.setColour (wonder.withAlpha (0.15f));
        g.fillRoundedRectangle (lever, 6.0f);
        g.setColour (wonder);
        g.drawRoundedRectangle (lever, 6.0f, 1.4f);
        g.setFont (stencil (15.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa1 THE OVERCLOCKER")), lever, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (chalkDim.withAlpha (0.35f));
        g.setFont (chalkFont (13.0f));
        g.drawText ("lifetime spins: " + juce::String (st.getLifetimeSpins()), leverArea(), juce::Justification::centred, false);
    }
}

//==============================================================================
OverclockerPanel::OverclockerPanel (MysteryBoxAudioProcessor& p) : proc (p)
{
    setTitle ("The Overclocker");
    setDescription ("Spend 5,000 points to overclock the current instrument.");
    setInterceptsMouseClicks (true, true);
}

void OverclockerPanel::resized() {}

void OverclockerPanel::slideOut() { target = 1.0f; message.clear(); startTimerHz (60); }
void OverclockerPanel::slideIn() { target = 0.0f; startTimerHz (60); }

void OverclockerPanel::timerCallback()
{
    t += 1.0f / 60.0f;
    position += (target - position) * 0.16f;
    if (std::abs (target - position) < 0.002f) position = target;
    zap = juce::jmax (0.0f, zap - 1.0f / 60.0f);
    if (onSlide) onSlide (position);
    setVisible (position > 0.001f);
    if (position == target && target == 0.0f && zap <= 0.0f) stopTimer();
    repaint();
}

juce::Rectangle<float> OverclockerPanel::buttonArea() const { return { 40.0f, (float) getHeight() - 92.0f, (float) getWidth() - 80.0f, 44.0f }; }
juce::Rectangle<float> OverclockerPanel::closeArea() const { return { (float) getWidth() - 34.0f, 8.0f, 26.0f, 26.0f }; }

void OverclockerPanel::mouseDown (const juce::MouseEvent& e)
{
    if (closeArea().contains (e.position)) { slideIn(); return; }
    if (! buttonArea().contains (e.position)) return;
    if (proc.getCurrentInstrumentId() <= 0) { message = "Take an instrument from the box first."; repaint(); return; }
    if (proc.isOverclocked()) { message = "Already overclocked."; repaint(); return; }
    if (proc.getPoints() < kCost) { message = "Not enough points (5,000 needed)."; proc.playSfx ({ Sfx::DryFire, 60, 0.6f, 0 }); repaint(); return; }
    proc.setPoints (proc.getPoints() - kCost);
    proc.setOverclocked (true);
    proc.playSfx ({ Sfx::OverclockZap, 60, 0.9f, 0 });
    message = "OVERCLOCKED!";
    zap = 1.2f;
    startTimerHz (60);
    if (onUpgraded) onUpgraded();
}

void OverclockerPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    drawRustPlate (g, b.reduced (2.0f), 909, 10.0f);
    // Tesla coil with crackling arcs
    const float cx = b.getCentreX();
    g.setColour (juce::Colour (0xff3a3d40));
    g.fillRect (cx - 6.0f, 46.0f, 12.0f, 60.0f);
    for (int i = 0; i < 9; ++i) { g.setColour (juce::Colour (0xffb87333).withAlpha (0.8f)); g.drawLine (cx - 8.0f, 52.0f + (float) i * 6.0f, cx + 8.0f, 54.0f + (float) i * 6.0f, 1.6f); }
    g.setColour (juce::Colour (0xffc0c4c8));
    g.fillEllipse (cx - 22.0f, 30.0f, 44.0f, 22.0f);
    juce::Random r ((juce::int64) (t * 30.0f));
    const float energy = 0.35f + zap;
    for (int a = 0; a < 3; ++a)
    {
        juce::Path arc;
        float x = cx, y = 38.0f;
        arc.startNewSubPath (x, y);
        const float dir = (a - 1) * 0.9f;
        for (int s = 0; s < 7; ++s) { x += (dir * 8.0f) + (r.nextFloat() - 0.5f) * 10.0f; y -= 4.0f + r.nextFloat() * 3.0f; arc.lineTo (x, y); }
        g.setColour (wonder.withAlpha (juce::jmin (1.0f, energy)));
        g.strokePath (arc, juce::PathStrokeType (1.3f));
    }
    drawGlow (g, { cx, 40.0f }, 60.0f, wonder, 0.25f * energy);
    // Title plate
    g.setFont (stencil (24.0f));
    g.setColour (wonder);
    g.drawText ("THE OVERCLOCKER", juce::Rectangle<float> (0, 112, b.getWidth(), 30), juce::Justification::centred, false);
    g.setColour (chalkDim);
    g.setFont (ui (13.0f));
    g.drawText ("Experimental instrument amplifier. Mk. 0", juce::Rectangle<float> (0, 140, b.getWidth(), 18), juce::Justification::centred, false);
    // Gauge
    const juce::Point<float> gc (cx, 222.0f);
    g.setColour (juce::Colour (0xffd9d4c5));
    g.fillEllipse (gc.x - 44.0f, gc.y - 44.0f, 88.0f, 88.0f);
    g.setColour (juce::Colours::black);
    g.drawEllipse (gc.x - 44.0f, gc.y - 44.0f, 88.0f, 88.0f, 3.0f);
    for (int i = 0; i <= 10; ++i)
    {
        const float a = -2.4f + 4.8f * (float) i / 10.0f;
        g.setColour (i >= 8 ? danger : juce::Colours::black);
        g.drawLine (gc.x + std::sin (a) * 34.0f, gc.y - std::cos (a) * 34.0f, gc.x + std::sin (a) * 41.0f, gc.y - std::cos (a) * 41.0f, 1.6f);
    }
    const float needle = proc.isOverclocked() ? 2.0f + 0.15f * std::sin (t * 23.0f) : -1.6f + 0.6f * (float) juce::jmin (proc.getPoints(), kCost) / (float) kCost;
    g.setColour (danger);
    g.drawLine (gc.x, gc.y, gc.x + std::sin (needle) * 36.0f, gc.y - std::cos (needle) * 36.0f, 2.2f);
    g.setColour (juce::Colours::black);
    g.fillEllipse (gc.x - 4.0f, gc.y - 4.0f, 8.0f, 8.0f);
    // Current instrument
    g.setColour (chalk);
    g.setFont (ui (15.0f, true));
    g.drawFittedText ("Current: " + proc.getCurrentInstrumentName(), juce::Rectangle<int> (16, 276, getWidth() - 32, 22), juce::Justification::centred, 1);
    g.setColour (chalkDim);
    g.setFont (ui (12.5f));
    g.drawFittedText ("Adds an 'Overclocked' macro: rich unison detune, harmonic exciter and stereo widening.",
                      juce::Rectangle<int> (20, 298, getWidth() - 40, 40), juce::Justification::centred, 3);
    // Button
    const auto btn = buttonArea();
    const bool can = proc.getPoints() >= kCost && ! proc.isOverclocked() && proc.getCurrentInstrumentId() > 0;
    g.setColour ((can ? wonder : metal).withAlpha (0.2f));
    g.fillRoundedRectangle (btn, 8.0f);
    g.setColour (can ? wonder : metal);
    g.drawRoundedRectangle (btn, 8.0f, 2.0f);
    g.setFont (stencil (17.0f));
    g.drawText (proc.isOverclocked() ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\xa1 OVERCLOCKED")) : juce::String ("OVERCLOCK - 5,000"), btn, juce::Justification::centred, false);
    if (message.isNotEmpty())
    {
        g.setColour (zap > 0.0f ? wonder : danger);
        g.setFont (ui (14.0f, true));
        g.drawText (message, juce::Rectangle<float> (0, btn.getBottom() + 4.0f, b.getWidth(), 20.0f), juce::Justification::centred, false);
    }
    // Close
    const auto cl = closeArea();
    g.setColour (chalk);
    g.drawLine (cl.getX() + 7, cl.getY() + 7, cl.getRight() - 7, cl.getBottom() - 7, 2.0f);
    g.drawLine (cl.getRight() - 7, cl.getY() + 7, cl.getX() + 7, cl.getBottom() - 7, 2.0f);
    if (zap > 0.0f)
    {
        g.setColour (wonder.withAlpha (zap * 0.3f));
        g.fillRect (b);
    }
}

} // namespace mbx
