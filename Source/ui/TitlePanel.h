// Mystery Box Synthesizer by Raygun
// Top-left: logotype with failing-fluorescent flicker, status card and the
// hidden Easter egg. Also "The Overclocker" machine that slides out from
// behind the title once the egg has been found.
#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace mbx
{
class MysteryBoxAudioProcessor;

class TitlePanel : public juce::Component, private juce::Timer
{
public:
    explicit TitlePanel (MysteryBoxAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    std::function<void()> onSecretUnlocked;   // 3 clicks within 2 s
    std::function<void()> onOpenOverclocker;  // lever clicked (once unlocked)
    void triggerFlicker (float seconds);

private:
    void timerCallback() override;
    juce::Rectangle<float> titleArea() const;
    juce::Rectangle<float> leverArea() const;

    MysteryBoxAudioProcessor& proc;
    juce::Array<double> clickTimes;
    struct Doodle { juce::Point<float> pos; float life; uint32_t seed; };
    juce::Array<Doodle> doodles;
    double nextFlickerAt = 0.0, flickerUntil = 0.0;
    float flickerAlpha = 1.0f;
    juce::Random rng;
    theme::CachedLayer plate;
    int tick = 0;
};

class OverclockerPanel : public juce::Component, private juce::Timer
{
public:
    explicit OverclockerPanel (MysteryBoxAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void slideOut();
    void slideIn();
    bool isOut() const noexcept { return target > 0.5f; }
    std::function<void()> onUpgraded;
    std::function<void (float)> onSlide; // 0..1, lets the parent position us

    static constexpr int kCost = 5000;

private:
    void timerCallback() override;
    juce::Rectangle<float> buttonArea() const;
    juce::Rectangle<float> closeArea() const;
    MysteryBoxAudioProcessor& proc;
    float position = 0.0f, target = 0.0f, t = 0.0f, zap = 0.0f;
    juce::String message;
};

} // namespace mbx
