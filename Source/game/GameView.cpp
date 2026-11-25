// Mystery Box Synthesizer by Raygun
#include "GameView.h"
#include "../PluginProcessor.h"

namespace mbx
{
using namespace theme;
using Z = GameWorld;

namespace
{
constexpr float W = GameWorld::kWidth, H = GameWorld::kHeight;

juce::String withCommas (int v)
{
    juce::String s (v), out;
    int c = 0;
    for (int i = s.length() - 1; i >= 0; --i) { out = juce::String::charToString (s[i]) + out; if (++c % 3 == 0 && i > 0) out = "," + out; }
    return out;
}

juce::Colour zombieShirt (uint32_t seed)
{
    static const juce::uint32 cols[] = { 0xff4b4f44, 0xff5a4636, 0xff3f4a55, 0xff5b5b52, 0xff4d3b3b, 0xff3d4a3a };
    return juce::Colour (cols[seed % 6]);
}
} // namespace

GameView::GameView (MysteryBoxAudioProcessor& p) : proc (p), world (p.getGame())
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false); // focus only on deliberate clicks (see mouseDown)
    setTitle ("Game view");
    setDescription ("Survive to Spin mini-game. Click to focus; aim with the mouse and click to shoot. "
                    "R reload, E rebuild boards, F open the Mystery Box, Space pause, Escape to release keyboard focus.");
    setAccessible (true);
    setGpuRendering (proc.getStore().getSettings().gpuRendering);
    displayedPoints = (float) proc.getPoints();
}

GameView::~GameView()
{
    stopTimer();
    if (gl != nullptr) gl->detach();
}

void GameView::setGpuRendering (bool shouldUseGpu)
{
    if (shouldUseGpu && gl == nullptr)
    {
        gl = std::make_unique<juce::OpenGLContext>();
        gl->setComponentPaintingEnabled (true);
        gl->setContinuousRepainting (false);
        gl->setMultisamplingEnabled (false);
        gl->attachTo (*this);
    }
    else if (! shouldUseGpu && gl != nullptr)
    {
        gl->detach();
        gl.reset();
    }
}

void GameView::resized()
{
    scaleFactor = (float) getWidth() / W;
    background.invalidate();
}

void GameView::visibilityChanged() { updateTimerRate(); }
void GameView::parentHierarchyChanged() { updateTimerRate(); }

void GameView::wake() { updateTimerRate(); }

void GameView::updateTimerRate()
{
    const auto& s = proc.getStore().getSettings();
    const bool boxBusy = world.boxState != GameWorld::BoxState::Idle;
    // 0% CPU when hidden or paused (unless the Mystery Box is mid-spin)
    if (! isShowing() || (world.isPaused() && ! boxBusy && world.konamiTimer <= 0.0f))
    {
        stopTimer();
        repaint();
        return;
    }
    const int fps = (s.graphics == 0 || s.fpsCap == 30) ? 30 : 60;
    if (! isTimerRunning() || getTimerInterval() != 1000 / fps)
    {
        lastTick = juce::Time::getMillisecondCounterHiRes();
        startTimerHz (fps);
    }
}

void GameView::timerCallback()
{
    if (! isShowing()) { stopTimer(); return; }
    const double now = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jlimit (0.0, 0.1, (now - lastTick) * 0.001);
    lastTick = now;

    // MIDI shooting mode: each incoming note-on fires a shot
    float shots[32];
    const int nShots = proc.popMidiShots (shots, 32);
    if (proc.getStore().getSettings().midiShooting)
        for (int i = 0; i < nShots; ++i) world.midiShot (shots[i]);

    lowGraphics = proc.getStore().getSettings().graphics == 0;
    world.update (dt);
    // Points count-up
    const float target = (float) proc.getPoints();
    displayedPoints += (target - displayedPoints) * juce::jmin (1.0f, dt * 8.0f);
    if (std::abs (target - displayedPoints) < 1.0f) displayedPoints = target;
    repaint();
    if (world.isPaused()) updateTimerRate();
}

juce::Point<float> GameView::toGame (juce::Point<float> p) const { return p / scaleFactor; }

void GameView::mouseMove (const juce::MouseEvent& e) { const auto g = toGame (e.position); world.aim (g.x, g.y); }
void GameView::mouseDrag (const juce::MouseEvent& e) { mouseMove (e); }
void GameView::mouseEnter (const juce::MouseEvent&) { mouseInside = true; setMouseCursor (juce::MouseCursor::NoCursor); }
void GameView::mouseExit (const juce::MouseEvent&) { mouseInside = false; setMouseCursor (juce::MouseCursor::NormalCursor); repaint(); }

void GameView::mouseDown (const juce::MouseEvent& e)
{
    if (! hasKeyboardFocus (false)) grabKeyboardFocus();
    const auto gp = toGame (e.position);
    world.aim (gp.x, gp.y);
    if (world.isPaused()) { world.setPaused (false); wake(); return; }
    // Favourite star on the box result
    if (world.boxState == GameWorld::BoxState::Landed && ! world.resultIsMascot)
    {
        const juce::Rectangle<float> star (world.boxX + 62.0f, world.resultCenterY() - 34.0f, 22.0f, 22.0f);
        if (star.contains (gp))
        {
            auto& st = proc.getStore();
            st.setFavorite (world.resultPreset, ! st.isFavorite (world.resultPreset));
            return;
        }
    }
    if (e.mods.isLeftButtonDown() && ! world.click (gp.x, gp.y)) world.shoot();
    wake();
}

bool GameView::keyPressed (const juce::KeyPress& k)
{
    // Konami code: up up down down left right left right B A
    static const int seq[10] = { juce::KeyPress::upKey, juce::KeyPress::upKey, juce::KeyPress::downKey, juce::KeyPress::downKey,
                                 juce::KeyPress::leftKey, juce::KeyPress::rightKey, juce::KeyPress::leftKey, juce::KeyPress::rightKey, 'B', 'A' };
    const int code = k.getKeyCode();
    const int upper = (code >= 'a' && code <= 'z') ? code - 32 : code;
    if (upper == seq[konamiIndex]) { if (++konamiIndex == 10) { konamiIndex = 0; world.konamiEntered(); wake(); } }
    else konamiIndex = (upper == seq[0]) ? 1 : 0;

    if (k == juce::KeyPress::escapeKey) { world.setRebuilding (false); giveAwayKeyboardFocus(); repaint(); return true; }
    if (k == juce::KeyPress::spaceKey) { world.togglePause(); wake(); return true; }
    if (upper == 'R') { world.reload(); return true; }
    if (upper == 'F') { world.requestSpin(); wake(); return true; }
    if (upper == 'E') return true;
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey
        || upper == 'A' || upper == 'B')
        return true;
    return false; // let the host have everything else
}

bool GameView::keyStateChanged (bool)
{
    const bool e = juce::KeyPress::isKeyCurrentlyDown ('e') || juce::KeyPress::isKeyCurrentlyDown ('E');
    if (e != eHeld) { eHeld = e; world.setRebuilding (e); }
    return e;
}

void GameView::focusGained (FocusChangeType) { repaint(); }
void GameView::focusLost (FocusChangeType) { eHeld = false; world.setRebuilding (false); repaint(); }

//==============================================================================
void GameView::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    juce::Graphics::ScopedSaveState ss (g);
    g.addTransform (juce::AffineTransform::scale (scaleFactor));
    const float t = world.time;
    if (world.shake > 0.0f && ! lowGraphics)
        g.addTransform (juce::AffineTransform::translation (std::sin (t * 91.0f) * world.shake, std::cos (t * 77.0f) * world.shake * 0.7f));

    background.draw (g, { 0, 0, (int) W, (int) H }, [this] (juce::Graphics& bg2) { paintBackground (bg2); });

    // Light beam from the box (brighter when affordable)
    {
        const bool afford = world.canAffordSpin() && world.boxState == GameWorld::BoxState::Idle;
        const float pulse = 0.5f + 0.5f * std::sin (t * 2.4f);
        const float spin = world.boxState == GameWorld::BoxState::Spinning ? 1.0f : 0.0f;
        const float alpha = (0.18f + (afford ? 0.22f : 0.0f) + 0.1f * pulse + 0.35f * spin) * world.boxAlpha;
        const float bw = 26.0f + 10.0f * pulse + 30.0f * spin;
        juce::ColourGradient beam (juce::Colour (0xffeaffd0).withAlpha (alpha), world.boxX, world.boxY - 20.0f,
                                   juce::Colour (0xffeaffd0).withAlpha (0.0f), world.boxX, 0.0f, false);
        g.setGradientFill (beam);
        juce::Path pth;
        pth.addQuadrilateral (world.boxX - bw * 0.5f, world.boxY - 18.0f, world.boxX + bw * 0.5f, world.boxY - 18.0f,
                              world.boxX + bw * 1.3f, 0.0f, world.boxX - bw * 1.3f, 0.0f);
        g.fillPath (pth);
    }

    // Draw zombies behind the box first (y < box), then the box, then the rest
    for (const auto& z : world.zombies) if (z.y < world.boxY + 10.0f) drawZombie (g, z);
    drawBox (g);
    for (const auto& z : world.zombies) if (z.y >= world.boxY + 10.0f && z.state != Z::ZState::Inside) drawZombie (g, z);
    drawBarricade (g);
    for (const auto& z : world.zombies) if (z.state == Z::ZState::Inside) drawZombie (g, z);
    drawPlayer (g);
    drawDrops (g);
    if (! lowGraphics) drawParticles (g);
    drawBoxOverlay (g);

    // Muzzle flash light flicker
    if (world.muzzleFlash > 0.0f && ! lowGraphics)
        drawGlow (g, { Z::kPlayerX, Z::kPlayerY - 40.0f }, 380.0f, juce::Colour (0xffffd27a), world.muzzleFlash * 3.0f);
    if (world.nukeFlash > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (juce::jmin (0.9f, world.nukeFlash)));
        g.fillRect (0.0f, 0.0f, W, H);
    }
    drawHud (g);
}

void GameView::paintBackground (juce::Graphics& g)
{
    // Back wall / yard fading into darkness where the horde comes from
    drawConcrete (g, { 0, 0, W, 300 }, 7, 0.1f);
    juce::ColourGradient fog (juce::Colours::black.withAlpha (0.85f), 0, 0, juce::Colours::black.withAlpha (0.0f), 0, 150, false);
    g.setGradientFill (fog);
    g.fillRect (0.0f, 0.0f, W, 160.0f);
    // Broken chain-link fence silhouette at the top
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    for (float x = -10; x < W + 10; x += 14) { g.drawLine (x, 18, x + 14, 44, 0.8f); g.drawLine (x + 14, 18, x, 44, 0.8f); }
    g.drawLine (0, 18, W, 18, 2.0f);
    for (float x = 20; x < W; x += 120) g.fillRect (x, 6.0f, 4.0f, 46.0f);

    // Floor (player side)
    drawConcrete (g, { 0, 300, W, 140 }, 11, 0.35f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (int i = 0; i < 9; ++i) { const float x = 360.0f + (float) (i - 4) * 120.0f; g.drawLine (x, 440.0f, 360.0f + (x - 360.0f) * 0.4f, 300.0f, 1.0f); }

    // Chalk doodles (original): tally of days, arrows, a crate sketch, notes
    juce::Path doodle;
    doodle.addRectangle (40.0f, 210.0f, 46.0f, 24.0f);
    doodle.startNewSubPath (40.0f, 210.0f); doodle.lineTo (86.0f, 234.0f);
    doodle.startNewSubPath (86.0f, 210.0f); doodle.lineTo (40.0f, 234.0f);
    strokeChalk (g, doodle, chalk.withAlpha (0.35f), 1.4f, 3);
    drawChalkText (g, "?", { 92.0f, 196.0f, 30.0f, 40.0f }, chalk.withAlpha (0.35f), 30.0f, juce::Justification::centred, 9);
    drawChalkText (g, "KEEP THE BEAT", { 560.0f, 205.0f, 150.0f, 26.0f }, chalk.withAlpha (0.3f), 18.0f, juce::Justification::centred, 4);
    juce::Path notes;
    notes.addEllipse (598.0f, 250.0f, 9.0f, 7.0f); notes.startNewSubPath (607.0f, 253.0f); notes.lineTo (607.0f, 232.0f); notes.lineTo (622.0f, 236.0f); notes.lineTo (622.0f, 257.0f);
    notes.addEllipse (613.0f, 254.0f, 9.0f, 7.0f);
    strokeChalk (g, notes, chalk.withAlpha (0.3f), 1.3f, 5);
    juce::Path wave;
    wave.startNewSubPath (130.0f, 268.0f);
    for (int i = 1; i <= 40; ++i) wave.lineTo (130.0f + (float) i * 2.5f, 268.0f + 8.0f * std::sin ((float) i * 0.45f));
    strokeChalk (g, wave, juce::Colour (0xff8fd18f).withAlpha (0.3f), 1.2f, 6);
    drawChalkText (g, "NO WAY OUT. ONLY UP.", { 470.0f, 262.0f, 200.0f, 20.0f }, chalk.withAlpha (0.18f), 13.0f, juce::Justification::centred, 8);

    // Low wall across the screen with a window opening for the barricade
    const float wy = Z::kBarricadeY - 8.0f;
    for (auto r : { juce::Rectangle<float> (0, wy, Z::kBarricadeLeft - 10.0f, 30.0f), juce::Rectangle<float> (Z::kBarricadeRight + 10.0f, wy, W - Z::kBarricadeRight - 10.0f, 30.0f) })
    {
        // Sandbags
        for (float x = r.getX(); x < r.getRight(); x += 34.0f)
            for (int row = 0; row < 2; ++row)
            {
                const auto bag = juce::Rectangle<float> (x + (row ? 17.0f : 0.0f) - 4.0f, r.getY() + (float) row * 13.0f, 36.0f, 16.0f);
                juce::ColourGradient bgc (juce::Colour (0xff6e6450), bag.getX(), bag.getY(), juce::Colour (0xff3d3628), bag.getX(), bag.getBottom(), false);
                g.setGradientFill (bgc);
                g.fillRoundedRectangle (bag, 7.0f);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.drawRoundedRectangle (bag, 7.0f, 0.8f);
            }
    }
    // Window frame posts
    for (float x : { Z::kBarricadeLeft - 12.0f, Z::kBarricadeRight + 2.0f })
    {
        juce::ColourGradient wood (juce::Colour (0xff5b4129), x, 0, juce::Colour (0xff3a2918), x + 10.0f, 0, false);
        g.setGradientFill (wood);
        g.fillRect (x, wy - 36.0f, 10.0f, 66.0f);
    }
}

void GameView::drawBarricade (juce::Graphics& g)
{
    const float y0 = Z::kBarricadeY - 38.0f;
    for (int i = 0; i < world.boards; ++i)
    {
        juce::Path plank;
        const float y = y0 + (float) i * 10.5f;
        plank.addRectangle (Z::kBarricadeLeft - 16.0f, y, Z::kBarricadeRight - Z::kBarricadeLeft + 32.0f, 8.5f);
        const float rot = ((i % 2) ? 0.05f : -0.04f) + 0.012f * (float) ((i * 7) % 3);
        plank.applyTransform (juce::AffineTransform::rotation (rot, 360.0f, y + 4.0f));
        const auto shade = juce::Colour (0xff7a5a36).withMultipliedBrightness (0.8f + 0.05f * (float) (i % 4));
        g.setColour (shade);
        g.fillPath (plank);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.strokePath (plank, juce::PathStrokeType (1.0f));
        // Nails
        g.setColour (juce::Colour (0xffb8b8b0));
        g.fillEllipse (Z::kBarricadeLeft - 8.0f, y + 2.5f, 3.0f, 3.0f);
        g.fillEllipse (Z::kBarricadeRight + 5.0f, y + 2.5f, 3.0f, 3.0f);
    }
    if (world.boards < Z::kMaxBoards && world.rebuildHeld)
    {
        g.setColour (neon.withAlpha (0.6f));
        g.drawRect (Z::kBarricadeLeft, Z::kBarricadeY - 40.0f, Z::kBarricadeRight - Z::kBarricadeLeft, 66.0f, 1.0f);
    }
}

void GameView::drawBox (juce::Graphics& g)
{
    const float a = world.boxAlpha;
    if (a <= 0.01f) return;
    float bx = world.boxX, by = world.boxY;
    if (world.boxState == GameWorld::BoxState::Mascot) by -= juce::jmax (0.0f, world.boxTimer - 1.0f) * 30.0f; // rising away
    juce::Graphics::ScopedSaveState ss (g);
    g.setOpacity (a);

    // Cinder blocks
    for (float ox : { -40.0f, 18.0f })
    {
        const auto blk = juce::Rectangle<float> (bx + ox, by + 14.0f, 24.0f, 14.0f);
        g.setColour (juce::Colour (0xff6c6c68)); g.fillRect (blk);
        g.setColour (juce::Colour (0xff3a3a38)); g.fillRect (blk.reduced (4.0f, 3.0f)); g.drawRect (blk, 1.0f);
    }
    // Crate body
    const auto body = juce::Rectangle<float> (bx - 50.0f, by - 16.0f, 100.0f, 32.0f);
    juce::ColourGradient wood (juce::Colour (0xff6b5232), body.getX(), body.getY(), juce::Colour (0xff3b2b18), body.getX(), body.getBottom(), false);
    g.setGradientFill (wood);
    g.fillRect (body);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = 1; i < 4; ++i) g.drawLine (body.getX(), body.getY() + (float) i * 8.0f, body.getRight(), body.getY() + (float) i * 8.0f, 0.8f);
    // Iron corner brackets + hinges
    g.setColour (juce::Colour (0xff35383b));
    for (auto c : { body.getTopLeft(), body.getTopRight().translated (-8, 0), body.getBottomLeft().translated (0, -8), body.getBottomRight().translated (-8, -8) })
        g.fillRect (c.x, c.y, 8.0f, 8.0f);
    // Stencilled glyph (original): circle with a lightning-question mark, glowing
    const float glow = 0.4f + 0.3f * std::sin (world.time * 3.0f) + (world.canAffordSpin() ? 0.3f : 0.0f);
    drawGlow (g, { bx, by }, 30.0f, neon, 0.18f * glow);
    juce::Path glyph;
    glyph.addEllipse (bx - 11.0f, by - 11.0f, 22.0f, 22.0f);
    glyph.startNewSubPath (bx - 4.0f, by - 5.0f); glyph.quadraticTo (bx - 3.0f, by - 9.0f, bx + 1.0f, by - 8.0f);
    glyph.quadraticTo (bx + 6.0f, by - 6.0f, bx + 1.0f, by - 1.0f); glyph.lineTo (bx - 1.0f, by + 3.0f); glyph.lineTo (bx + 2.0f, by + 2.0f); glyph.lineTo (bx, by + 6.0f);
    g.setColour (neon.withAlpha (juce::jlimit (0.3f, 1.0f, glow)));
    g.strokePath (glyph, juce::PathStrokeType (1.8f));
    g.setFont (stencil (8.0f));
    g.drawText ("RGN-0", juce::Rectangle<float> (body.getX() + 6, body.getY() + 2, 30, 10), juce::Justification::left, false);
    // Rope handles
    g.setColour (juce::Colour (0xffbda57a));
    g.drawEllipse (body.getX() - 7.0f, by - 6.0f, 9.0f, 12.0f, 1.8f);
    g.drawEllipse (body.getRight() - 2.0f, by - 6.0f, 9.0f, 12.0f, 1.8f);

    // Interior glow while open
    const float lid = world.lidAngle;
    if (lid > 0.01f) drawGlow (g, { bx, by - 16.0f }, 70.0f, juce::Colour (0xfff2ffd8), 0.45f * lid);
    // Lid (hinged at the back; lifts and tilts)
    juce::Path lidPath;
    const float lift = lid * 26.0f;
    lidPath.addQuadrilateral (bx - 52.0f, by - 16.0f - lift * 0.15f, bx + 52.0f, by - 16.0f - lift * 0.15f,
                              bx + 50.0f, by - 24.0f - lift, bx - 50.0f, by - 24.0f - lift);
    juce::ColourGradient lidGrad (juce::Colour (0xff7a5e3a), bx, by - 24.0f - lift, juce::Colour (0xff4a3720), bx, by - 16.0f, false);
    g.setGradientFill (lidGrad);
    g.fillPath (lidPath);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.strokePath (lidPath, juce::PathStrokeType (1.0f));

    if (world.boxState == GameWorld::BoxState::Mascot) drawMascot (g, world.boxTimer);
}

void GameView::drawMascot (juce::Graphics& g, float t)
{
    // "Clank" -- an original tin-can robot on a jack-in-the-box spring
    const float bx = world.boxX, by = world.boxY - 16.0f;
    const float pop = juce::jmin (1.0f, t / 0.35f);
    const float bounce = std::sin (t * 14.0f) * 4.0f * (1.0f - juce::jmin (1.0f, t / 2.0f));
    const float headY = by - 18.0f - 46.0f * pop + bounce;
    juce::Path spring;
    spring.startNewSubPath (bx, by);
    for (int i = 1; i <= 12; ++i) spring.lineTo (bx + ((i % 2) ? 7.0f : -7.0f), by + (headY + 18.0f - by) * (float) i / 12.0f);
    g.setColour (juce::Colour (0xffb0b4b8));
    g.strokePath (spring, juce::PathStrokeType (1.6f));
    const auto head = juce::Rectangle<float> (bx - 18.0f, headY - 18.0f, 36.0f, 34.0f);
    juce::ColourGradient tin (juce::Colour (0xffc9cdd1), head.getX(), head.getY(), juce::Colour (0xff6f757a), head.getRight(), head.getBottom(), false);
    g.setGradientFill (tin);
    g.fillRoundedRectangle (head, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (head, 5.0f, 1.2f);
    // Rivets, antenna with a blinking bulb
    for (float rx : { head.getX() + 4.0f, head.getRight() - 6.0f }) g.fillEllipse (rx, head.getY() + 3.0f, 2.5f, 2.5f);
    g.drawLine (bx, head.getY(), bx + 4.0f, head.getY() - 14.0f, 1.5f);
    const bool blink = std::fmod (t, 0.4f) < 0.2f;
    drawGlow (g, { bx + 4.0f, head.getY() - 15.0f }, 10.0f, juce::Colour (0xffff5050), blink ? 0.8f : 0.2f);
    // Glowing eyes and a laughing hinged jaw
    for (float ex : { bx - 8.0f, bx + 8.0f }) { drawGlow (g, { ex, headY - 6.0f }, 7.0f, wonder, 0.9f); g.setColour (juce::Colours::white); g.fillEllipse (ex - 2.0f, headY - 8.0f, 4.0f, 4.0f); }
    const float jaw = 3.0f + 4.0f * std::abs (std::sin (t * 16.0f));
    g.setColour (juce::Colour (0xff222426));
    g.fillRect (bx - 10.0f, headY + 4.0f, 20.0f, jaw);
    g.setColour (juce::Colour (0xff9aa0a5));
    for (int i = 0; i < 5; ++i) g.fillRect (bx - 9.0f + (float) i * 4.0f, headY + 4.0f, 2.0f, 2.0f);
    drawChalkText (g, "HA HA HA!", { bx - 60.0f, headY - 52.0f, 120.0f, 20.0f }, wonder, 16.0f);
}

void GameView::drawBoxOverlay (juce::Graphics& g)
{
    const float bx = world.boxX, by = world.boxY;
    const auto state = world.boxState;
    if (state == GameWorld::BoxState::Idle && world.boxAlpha > 0.9f)
    {
        const int cost = world.getSpinCost();
        const bool afford = world.canAffordSpin();
        const juce::String txt = cost == 0 ? "OPEN - FREE" : (afford ? "OPEN - " + withCommas (cost) : "NEED " + withCommas (cost));
        const auto r = juce::Rectangle<float> (bx - 70.0f, by - 50.0f, 140.0f, 18.0f);
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (afford ? neon : chalkDim);
        g.setFont (stencil (12.0f));
        g.drawText (txt, r, juce::Justification::centred, false);
        return;
    }
    if (state == GameWorld::BoxState::Spinning || state == GameWorld::BoxState::Landed)
    {
        const bool landed = state == GameWorld::BoxState::Landed;
        const int id = world.displayPreset;
        const int cat = proc.gamePresetCategory (id);
        const int rarity = proc.gamePresetRarity (id);
        const float hover = landed ? std::sin (world.resultHover * 3.0f) * 3.0f : 0.0f;
        const float cy = world.resultCenterY() + hover;
        juce::ignoreUnused (by);
        const auto colour = landed ? rarityTint (rarity) : chalk;
        if (landed)
        {
            const float pulse = rarity == 4 ? 0.5f + 0.5f * std::sin (world.time * 8.0f) : 0.5f;
            drawGlow (g, { bx, cy }, 80.0f + 20.0f * pulse, colour, 0.35f + 0.2f * pulse);
        }
        const auto panel = juce::Rectangle<float> (bx - 92.0f, cy - 32.0f, 184.0f, landed ? 76.0f : 46.0f);
        g.setColour (juce::Colours::black.withAlpha (0.72f));
        g.fillRoundedRectangle (panel, 6.0f);
        g.setColour (colour.withAlpha (0.8f));
        g.drawRoundedRectangle (panel, 6.0f, landed ? 1.8f : 1.0f);
        drawCategoryIcon (g, cat, { panel.getX() + 8.0f, cy - 26.0f, 32.0f, 32.0f }, colour);
        g.setColour (juce::Colours::white);
        g.setFont (ui (15.0f, true));
        g.drawFittedText (juce::String (juce::CharPointer_UTF8 (proc.gamePresetName (id))), juce::Rectangle<int> ((int) panel.getX() + 46, (int) cy - 28, 132, 22), juce::Justification::centredLeft, 1);
        g.setColour (chalkDim);
        g.setFont (ui (11.0f));
        g.drawText (proc.getLibrary().categoryName (cat), juce::Rectangle<float> (panel.getX() + 46.0f, cy - 8.0f, 132.0f, 14.0f), juce::Justification::centredLeft, true);
        if (landed)
        {
            drawRarityBadge (g, rarity, { panel.getX() + 8.0f, cy + 14.0f, 76.0f, 16.0f }, 0.5f + 0.5f * std::sin (world.time * 5.0f));
            // TAKE button
            const auto take = juce::Rectangle<float> (panel.getRight() - 80.0f, cy + 12.0f, 72.0f, 22.0f);
            g.setColour (neon.withAlpha (0.2f + 0.1f * std::sin (world.time * 6.0f)));
            g.fillRoundedRectangle (take, 4.0f);
            g.setColour (neon);
            g.drawRoundedRectangle (take, 4.0f, 1.4f);
            g.setFont (stencil (14.0f));
            g.drawText ("TAKE", take, juce::Justification::centred, false);
            // Countdown ring (it sinks back into the box after 8 s)
            const float remaining = 1.0f - juce::jlimit (0.0f, 1.0f, world.boxTimer / 8.0f);
            juce::Path ring;
            ring.addCentredArc (panel.getX() + 92.0f, cy + 23.0f, 7.0f, 7.0f, 0.0f, 0.0f, remaining * juce::MathConstants<float>::twoPi, true);
            g.setColour (remaining < 0.3f ? danger : chalk);
            g.strokePath (ring, juce::PathStrokeType (2.0f));
            // Favourite star
            const bool fav = proc.getStore().isFavorite (id);
            juce::Path star;
            star.addStar ({ bx + 73.0f, cy - 23.0f }, 5, 4.0f, 9.0f, 0.0f);
            g.setColour (fav ? gold : chalkDim);
            if (fav) g.fillPath (star); else g.strokePath (star, juce::PathStrokeType (1.4f));
        }
    }
}

void GameView::drawZombie (juce::Graphics& g, const GameWorld::Zombie& z)
{
    juce::Graphics::ScopedSaveState ss (g);
    const float s = z.scale;
    float alpha = 1.0f, sink = 0.0f, tilt = 0.0f;
    if (z.state == Z::ZState::Dying)
    {
        const float k = 1.0f - juce::jlimit (0.0f, 1.0f, z.deathTimer / 0.6f);
        alpha = 1.0f - k; sink = k * 10.0f; tilt = k * (z.seed % 2 ? 1.2f : -1.2f);
    }
    g.setOpacity (alpha);
    g.addTransform (juce::AffineTransform::rotation (tilt, z.x, z.y).translated (0.0f, sink));
    const float walk = std::sin (z.anim);
    const auto skin = juce::Colour (0xff76925f).withMultipliedBrightness (0.85f + 0.1f * (float) (z.seed % 3));
    const auto shirt = zombieShirt (z.seed);
    const float x = z.x, y = z.y;

    // Shadow
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillEllipse (x - 12.0f * s, y - 3.0f * s, 24.0f * s, 7.0f * s);

    if (z.type == Z::ZType::Crawler)
    {
        g.setColour (shirt);
        g.fillRoundedRectangle (x - 16.0f * s, y - 12.0f * s, 30.0f * s, 11.0f * s, 4.0f * s);
        g.setColour (skin);
        g.fillEllipse (x + 9.0f * s, y - 16.0f * s, 12.0f * s, 11.0f * s);
        g.setColour (skin.darker (0.2f));
        g.drawLine (x + 12.0f * s, y - 6.0f * s, x + 24.0f * s, y + walk * 3.0f * s, 3.0f * s);
        g.drawLine (x - 4.0f * s, y - 6.0f * s, x + 6.0f * s, y - walk * 3.0f * s, 3.0f * s);
        g.setColour (juce::Colour (0xffffe066));
        g.fillEllipse (x + 15.0f * s, y - 13.0f * s, 2.2f * s, 2.2f * s);
    }
    else
    {
        const bool brute = z.type == Z::ZType::Brute, runner = z.type == Z::ZType::Runner;
        const float lean = runner ? 0.18f : 0.0f;
        g.addTransform (juce::AffineTransform::rotation (lean + std::sin (z.anim * 0.5f) * 0.05f, x, y));
        // Legs
        g.setColour (juce::Colour (0xff2c2f33));
        g.drawLine (x - 4.0f * s, y - 12.0f * s, x - 5.0f * s + walk * 3.0f * s, y, 4.0f * s);
        g.drawLine (x + 4.0f * s, y - 12.0f * s, x + 5.0f * s - walk * 3.0f * s, y, 4.0f * s);
        // Torso (ragged hem)
        const float tw = (brute ? 30.0f : 20.0f) * s * 0.5f;
        juce::Path torso;
        torso.startNewSubPath (x - tw, y - 34.0f * s);
        torso.lineTo (x + tw, y - 34.0f * s);
        torso.lineTo (x + tw * 0.9f, y - 12.0f * s);
        for (int i = 0; i < 5; ++i) torso.lineTo (x + tw * 0.9f - (float) i * tw * 0.45f, y - (i % 2 ? 10.0f : 14.0f) * s);
        torso.lineTo (x - tw * 0.9f, y - 12.0f * s);
        torso.closeSubPath();
        g.setColour (shirt);
        g.fillPath (torso);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.strokePath (torso, juce::PathStrokeType (0.8f));
        // Arms reaching forward
        g.setColour (skin.darker (0.15f));
        const float reach = std::sin (z.anim * 0.8f) * 3.0f * s;
        const float armW = (brute ? 5.5f : 3.5f) * s;
        g.drawLine (x - tw, y - 30.0f * s, x - tw - 4.0f * s, y - 18.0f * s + reach, armW);
        g.drawLine (x + tw, y - 30.0f * s, x + tw + 4.0f * s, y - 18.0f * s - reach, armW);
        // Head
        const float hr = (brute ? 6.5f : 7.5f) * s;
        const float hx = x + std::sin (z.anim * 0.5f) * 1.2f * s, hy = y - 40.0f * s;
        g.setColour (skin);
        g.fillEllipse (hx - hr, hy - hr, hr * 2.0f, hr * 2.1f);
        g.setColour (skin.darker (0.5f));
        g.fillEllipse (hx - hr * 0.6f, hy + hr * 0.35f, hr * 1.2f, hr * 0.35f); // mouth
        // Glowing eyes
        const auto eye = z.type == Z::ZType::Runner ? juce::Colour (0xffff6a3d) : juce::Colour (0xffffe066);
        g.setColour (eye);
        g.fillEllipse (hx - hr * 0.5f, hy - hr * 0.25f, hr * 0.32f, hr * 0.32f);
        g.fillEllipse (hx + hr * 0.18f, hy - hr * 0.25f, hr * 0.32f, hr * 0.32f);
    }
    if (z.hitFlash > 0.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (z.hitFlash * 3.0f));
        g.fillEllipse (x - 14.0f * s, y - 50.0f * s, 28.0f * s, 52.0f * s);
    }
    // Health bar for tough zombies
    if (z.maxHealth > 2.5f && z.state != Z::ZState::Dying)
    {
        const float fr = juce::jlimit (0.0f, 1.0f, z.health / z.maxHealth);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (x - 12.0f, y - 58.0f * s, 24.0f, 3.0f);
        g.setColour (neon.withAlpha (0.8f));
        g.fillRect (x - 12.0f, y - 58.0f * s, 24.0f * fr, 3.0f);
    }
}

void GameView::drawPlayer (juce::Graphics& g)
{
    const float px = Z::kPlayerX, py = Z::kPlayerY;
    const float ang = std::atan2 (world.aimY - (py - 26.0f), world.aimX - px);
    const float kick = world.recoil * 4.0f;
    // Shoulders / back
    juce::ColourGradient jacket (juce::Colour (0xff3e4a36), px, py - 30.0f, juce::Colour (0xff1f261a), px, py + 20.0f, false);
    g.setGradientFill (jacket);
    g.fillRoundedRectangle (px - 22.0f, py - 26.0f, 44.0f, 50.0f, 12.0f);
    // Arm + pistol rotated toward the crosshair
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (ang, px + 8.0f, py - 22.0f));
        g.setColour (juce::Colour (0xff35402f));
        g.fillRoundedRectangle (px + 8.0f - kick, py - 26.0f, 26.0f, 8.0f, 3.0f);
        g.setColour (juce::Colour (0xff1b1c1e));
        g.fillRect (px + 32.0f - kick, py - 27.0f, 16.0f, 6.0f);
        g.fillRect (px + 34.0f - kick, py - 22.0f, 5.0f, 7.0f);
        if (world.muzzleFlash > 0.0f)
        {
            juce::Path flash;
            flash.addStar ({ px + 54.0f - kick, py - 24.0f }, 6, 3.0f, 11.0f, world.time * 20.0f);
            g.setColour (juce::Colour (0xffffe28a));
            g.fillPath (flash);
        }
    }
    // Head (beanie)
    g.setColour (juce::Colour (0xff2a2622));
    g.fillEllipse (px - 11.0f, py - 46.0f, 22.0f, 22.0f);
    g.setColour (juce::Colour (0xff5c2a22));
    g.fillRoundedRectangle (px - 12.0f, py - 48.0f, 24.0f, 10.0f, 5.0f);
}

void GameView::drawParticles (juce::Graphics& g)
{
    for (const auto& p : world.particles)
    {
        const float a = juce::jlimit (0.0f, 1.0f, p.life / p.maxLife);
        switch (p.type)
        {
            case Z::PType::Dust:  g.setColour (juce::Colour (0xff9a9488).withAlpha (a * 0.5f)); g.fillEllipse (p.x - p.size, p.y - p.size, p.size * 2, p.size * 2); break;
            case Z::PType::Chalk: g.setColour (chalk.withAlpha (a * 0.6f)); g.fillEllipse (p.x - p.size, p.y - p.size, p.size * 2, p.size * 2); break;
            case Z::PType::Ichor: g.setColour (juce::Colour (0xff6cff3a).withAlpha (a * 0.9f)); g.fillEllipse (p.x - p.size * 0.5f, p.y - p.size * 0.5f, p.size, p.size); break;
            case Z::PType::Spark: g.setColour (juce::Colour (0xffffe08a).withAlpha (a)); g.fillRect (p.x, p.y, 1.6f, 1.6f); break;
            case Z::PType::Ember: drawGlow (g, { p.x, p.y }, p.size * 2.5f, gold, a * 0.8f); break;
            case Z::PType::Shell:
            {
                juce::Path sh; sh.addRoundedRectangle (-2.5f, -1.2f, 5.0f, 2.4f, 1.0f);
                sh.applyTransform (juce::AffineTransform::rotation (p.rot).translated (p.x, p.y));
                g.setColour (juce::Colour (0xffd9a441).withAlpha (a));
                g.fillPath (sh);
                break;
            }
        }
    }
}

void GameView::drawPowerUpIcon (juce::Graphics& g, GameWorld::PowerUp t, juce::Point<float> c, float r)
{
    switch (t)
    {
        case GameWorld::PowerUp::DoublePoints:
            g.setColour (gold); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
            g.setColour (juce::Colours::black); g.setFont (stencil (r * 1.05f)); g.drawText ("x2", juce::Rectangle<float> (c.x - r, c.y - r, r * 2, r * 2), juce::Justification::centred, false);
            break;
        case GameWorld::PowerUp::InstaKill:
        {
            g.setColour (danger); g.drawEllipse (c.x - r, c.y - r, r * 2, r * 2, 2.0f);
            juce::Path bolt; bolt.startNewSubPath (c.x + r * 0.2f, c.y - r * 0.8f); bolt.lineTo (c.x - r * 0.4f, c.y + r * 0.1f); bolt.lineTo (c.x + r * 0.05f, c.y + r * 0.1f);
            bolt.lineTo (c.x - r * 0.2f, c.y + r * 0.8f); bolt.lineTo (c.x + r * 0.45f, c.y - r * 0.15f); bolt.lineTo (c.x, c.y - r * 0.15f); bolt.closeSubPath();
            g.fillPath (bolt);
            break;
        }
        case GameWorld::PowerUp::MaxAmmo:
            g.setColour (juce::Colour (0xff4f5b3a)); g.fillRoundedRectangle (c.x - r, c.y - r * 0.6f, r * 2, r * 1.3f, 2.0f);
            g.setColour (gold); for (int i = 0; i < 3; ++i) g.fillRoundedRectangle (c.x - r * 0.7f + (float) i * r * 0.55f, c.y - r * 1.1f, r * 0.35f, r * 0.9f, r * 0.15f);
            break;
        case GameWorld::PowerUp::Nuke:
        {
            g.setColour (juce::Colour (0xffffd33d)); g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
            g.setColour (juce::Colours::black);
            for (int i = 0; i < 3; ++i)
            {
                juce::Path blade; const float a0 = (float) i * 2.094f - 1.57f;
                blade.addPieSegment (c.x - r * 0.85f, c.y - r * 0.85f, r * 1.7f, r * 1.7f, a0 - 0.5f, a0 + 0.5f, 0.35f);
                g.fillPath (blade);
            }
            break;
        }
    }
}

void GameView::drawDrops (juce::Graphics& g)
{
    for (const auto& d : world.drops)
    {
        if (d.life < 4.0f && std::fmod (d.life, 0.4f) < 0.15f) continue; // blink before vanishing
        const float bob = std::sin (world.time * 4.0f + d.x) * 3.0f;
        drawGlow (g, { d.x, d.y + bob }, 26.0f, neon, 0.35f);
        drawPowerUpIcon (g, d.type, { d.x, d.y + bob }, 11.0f);
    }
}

void GameView::drawHud (juce::Graphics& g)
{
    // Round tally in red chalk (top-left)
    {
        const auto red = juce::Colour (0xffd23b2a);
        const int r = world.round;
        if (r <= 20)
        {
            juce::Path tally;
            float x = 14.0f;
            for (int i = 0; i < r; ++i)
            {
                const int inGroup = i % 5;
                if (inGroup < 4) { tally.startNewSubPath (x + (float) inGroup * 7.0f, 12.0f + (float) (i % 3)); tally.lineTo (x + (float) inGroup * 7.0f + 1.5f, 42.0f); }
                else { tally.startNewSubPath (x - 4.0f, 38.0f); tally.lineTo (x + 26.0f, 16.0f); x += 40.0f; }
            }
            strokeChalk (g, tally, red, 3.0f, (uint32_t) r);
        }
        else drawChalkText (g, juce::String (r), { 10.0f, 4.0f, 90.0f, 46.0f }, red, 44.0f, juce::Justification::centredLeft, (uint32_t) r);
    }
    // Points (top-right)
    {
        g.setColour (chalkDim);
        g.setFont (stencil (11.0f));
        g.drawText ("POINTS", juce::Rectangle<float> (W - 170.0f, 8.0f, 158.0f, 12.0f), juce::Justification::centredRight, false);
        g.setColour (world.doublePointsTime > 0.0f ? gold : neon);
        g.setFont (mono (26.0f));
        g.drawText (withCommas ((int) displayedPoints), juce::Rectangle<float> (W - 220.0f, 18.0f, 208.0f, 28.0f), juce::Justification::centredRight, false);
    }
    // Active power-ups
    {
        float x = W * 0.5f - 40.0f;
        if (world.doublePointsTime > 0.0f) { drawPowerUpIcon (g, GameWorld::PowerUp::DoublePoints, { x, 20.0f }, 9.0f); g.setColour (chalk); g.setFont (mono (12.0f)); g.drawText (juce::String ((int) world.doublePointsTime), juce::Rectangle<float> (x + 11, 13, 24, 14), juce::Justification::left, false); x += 50.0f; }
        if (world.instaKillTime > 0.0f) { drawPowerUpIcon (g, GameWorld::PowerUp::InstaKill, { x, 20.0f }, 9.0f); g.setColour (chalk); g.setFont (mono (12.0f)); g.drawText (juce::String ((int) world.instaKillTime), juce::Rectangle<float> (x + 11, 13, 24, 14), juce::Justification::left, false); }
    }
    // Ammo (bottom-right)
    {
        const auto box = juce::Rectangle<float> (W - 128.0f, H - 40.0f, 118.0f, 30.0f);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (box, 4.0f);
        for (int i = 0; i < Z::kMagSize; ++i)
        {
            g.setColour (i < world.ammo ? gold : juce::Colours::white.withAlpha (0.15f));
            g.fillRoundedRectangle (box.getX() + 6.0f + (float) i * 7.0f, box.getY() + 6.0f, 4.5f, 18.0f, 2.0f);
        }
        g.setColour (chalk);
        g.setFont (mono (16.0f));
        g.drawText (juce::String (world.ammo) + "/" + juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x9e")), box.withTrimmedLeft (62.0f), juce::Justification::centred, false);
        if (world.reloadTimer > 0.0f)
        {
            g.setColour (neon);
            g.fillRect (box.getX(), box.getBottom() - 3.0f, box.getWidth() * (1.0f - world.reloadTimer / 1.3f), 3.0f);
            drawChalkText (g, "RELOADING", { box.getX() - 10.0f, box.getY() - 20.0f, 130.0f, 18.0f }, chalk, 14.0f);
        }
        else if (world.ammo == 0) drawChalkText (g, "PRESS R", { box.getX(), box.getY() - 20.0f, 118.0f, 18.0f }, danger, 15.0f);
    }
    // Boards
    {
        g.setColour (chalkDim);
        g.setFont (stencil (10.0f));
        g.drawText ("BOARDS " + juce::String (world.boards) + "/6" + (world.boards < Z::kMaxBoards ? "  (HOLD E)" : ""),
                    juce::Rectangle<float> (10.0f, H - 22.0f, 220.0f, 14.0f), juce::Justification::centredLeft, false);
    }
    // Round banner / break countdown
    if (world.roundBanner > 0.0f)
    {
        const float a = juce::jmin (1.0f, world.roundBanner);
        drawChalkText (g, (world.roundState == GameWorld::RoundState::Active ? "ROUND " : "ROUND CLEARED ") + juce::String (world.roundState == GameWorld::RoundState::Active ? world.round : world.round - 1),
                       { 0.0f, 170.0f, W, 50.0f }, juce::Colour (0xffd23b2a).withAlpha (a), 44.0f, juce::Justification::centred, (uint32_t) world.round);
    }
    else if (world.roundState == GameWorld::RoundState::Break && world.roundTimer > 0.0f)
        drawChalkText (g, "NEXT ROUND IN " + juce::String ((int) std::ceil (world.roundTimer)), { 0.0f, 196.0f, W, 26.0f }, chalk.withAlpha (0.7f), 20.0f);

    // Floating score text
    for (const auto& t : world.texts)
    {
        const float a = juce::jlimit (0.0f, 1.0f, t.life);
        g.setColour (juce::Colour (t.colour).withAlpha (a));
        g.setFont (stencil (14.0f * t.scale));
        g.drawText (juce::String (t.text), juce::Rectangle<float> (t.x - 130.0f, t.y - 10.0f, 260.0f, 20.0f), juce::Justification::centred, false);
    }

    if (world.downTimer > 0.0f)
    {
        juce::ColourGradient vign (juce::Colours::transparentBlack, W * 0.5f, H * 0.5f, juce::Colour (0xaa7a0000), 0.0f, 0.0f, true);
        g.setGradientFill (vign);
        g.fillRect (0.0f, 0.0f, W, H);
        drawChalkText (g, "YOU'RE DOWN", { 0.0f, 160.0f, W, 60.0f }, danger, 50.0f);
        drawChalkText (g, "points kept - back to round 1", { 0.0f, 216.0f, W, 26.0f }, chalk, 18.0f);
    }
    if (world.konamiTimer > 0.0f)
    {
        const float a = juce::jmin (1.0f, world.konamiTimer);
        drawChalkText (g, juce::String (world.konamiMessage), { 0.0f, 120.0f, W, 70.0f }, neon.withAlpha (a), 54.0f, juce::Justification::centred, 77);
        drawChalkText (g, "+10,000", { 0.0f, 186.0f, W, 30.0f }, gold.withAlpha (a), 26.0f);
    }
    if (world.isPaused())
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect (0.0f, 0.0f, W, H);
        drawChalkText (g, "PAUSED", { 0.0f, 160.0f, W, 60.0f }, chalk, 52.0f);
        drawChalkText (g, "click or press SPACE to resume", { 0.0f, 218.0f, W, 26.0f }, chalkDim, 18.0f);
    }
    else if (! hasKeyboardFocus (false))
    {
        g.setColour (chalkDim.withAlpha (0.7f));
        g.setFont (ui (12.0f));
        g.drawText ("Click to play  |  R reload  E rebuild  F spin  SPACE pause  ESC release keys", juce::Rectangle<float> (0.0f, H - 22.0f, W, 16.0f), juce::Justification::centred, false);
    }
    // Crosshair
    if (mouseInside && ! world.isPaused())
    {
        const float cx = world.aimX, cy = world.aimY, gap = 4.0f + world.recoil * 4.0f;
        const auto c = world.isPointOnBox (cx, cy) || world.isPointOnResult (cx, cy) ? gold : neon;
        g.setColour (c);
        g.drawLine (cx - 12.0f, cy, cx - gap, cy, 1.6f); g.drawLine (cx + gap, cy, cx + 12.0f, cy, 1.6f);
        g.drawLine (cx, cy - 12.0f, cx, cy - gap, 1.6f); g.drawLine (cx, cy + gap, cx, cy + 12.0f, 1.6f);
        g.fillEllipse (cx - 1.2f, cy - 1.2f, 2.4f, 2.4f);
    }
    // Focus outline
    if (hasKeyboardFocus (false))
    {
        g.setColour (neon.withAlpha (0.35f));
        g.drawRect (0.0f, 0.0f, W, H, 1.5f);
    }
}

} // namespace mbx
