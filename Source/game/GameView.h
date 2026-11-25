// Mystery Box Synthesizer by Raygun
// Renders the GameWorld (GPU via an attached OpenGLContext when enabled) and
// handles game input. Keyboard focus is only taken when the view is clicked,
// so DAW shortcuts keep working otherwise.
#pragma once

#include <JuceHeader.h>
#include "GameWorld.h"
#include "../ui/Theme.h"

namespace mbx
{
class MysteryBoxAudioProcessor;

class GameView : public juce::Component,
                 private juce::Timer
{
public:
    explicit GameView (MysteryBoxAudioProcessor& p);
    ~GameView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    void setGpuRendering (bool shouldUseGpu);
    void wake(); // restart the loop (after unpause / settings change)
    float getDisplayedPoints() const noexcept { return displayedPoints; }

private:
    void timerCallback() override;
    void updateTimerRate();
    juce::Point<float> toGame (juce::Point<float> p) const;

    void paintBackground (juce::Graphics&);
    void drawBarricade (juce::Graphics&);
    void drawBox (juce::Graphics&);
    void drawBoxOverlay (juce::Graphics&);
    void drawMascot (juce::Graphics&, float t);
    void drawZombie (juce::Graphics&, const GameWorld::Zombie&);
    void drawPlayer (juce::Graphics&);
    void drawParticles (juce::Graphics&);
    void drawDrops (juce::Graphics&);
    void drawHud (juce::Graphics&);
    void drawPowerUpIcon (juce::Graphics&, GameWorld::PowerUp, juce::Point<float> c, float r);

    MysteryBoxAudioProcessor& proc;
    GameWorld& world;
    std::unique_ptr<juce::OpenGLContext> gl;
    theme::CachedLayer background;
    double lastTick = 0.0;
    float displayedPoints = 0.0f;
    bool mouseInside = false;
    bool eHeld = false;
    int konamiIndex = 0;
    float scaleFactor = 1.0f;
    bool lowGraphics = false;
};

} // namespace mbx
