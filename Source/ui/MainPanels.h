// Mystery Box Synthesizer by Raygun
#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "Theme.h"

namespace mbx
{
class MysteryBoxAudioProcessor;

/** Middle: the current instrument -- identity, macros and key engine controls. */
class InstrumentPanel : public juce::Component, private juce::Timer
{
public:
    explicit InstrumentPanel (MysteryBoxAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    MysteryBoxAudioProcessor& proc;
    juce::OwnedArray<Knob> macros, controls;
    std::unique_ptr<Knob> overclockKnob;
    juce::TextButton favButton;
    int shownId = -1; bool shownOc = false; bool shownFav = false;
    theme::CachedLayer plate;
    float pulse = 0.0f;
};

/** Bottom: keyboard, points counter, CPU meter and the last 10 box results. */
class BottomBar : public juce::Component, private juce::Timer
{
public:
    explicit BottomBar (MysteryBoxAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    juce::String getTooltip() { return tooltip; }

private:
    class TipArea : public juce::Component, public juce::SettableTooltipClient {};
    void timerCallback() override;
    juce::Rectangle<float> tileRect (int i) const;
    MysteryBoxAudioProcessor& proc;
    juce::MidiKeyboardComponent keyboard;
    juce::TextButton panicButton { "PANIC" };
    float shownPoints = 0.0f, cpu = 0.0f, peak = 0.0f;
    juce::String tooltip;
    juce::Array<int> invIds; juce::Array<bool> invTaken;
    int hoverTile = -1;
};

/** Top-right: Settings / FX / Favorites buttons + output meter. */
class SideButtons : public juce::Component, private juce::Timer
{
public:
    explicit SideButtons (MysteryBoxAudioProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (int)> onSelect; // 0 settings, 1 fx, 2 favourites
    void setActive (int index) { active = index; for (int i = 0; i < 3; ++i) buttons[i]->setToggleState (i == index, juce::dontSendNotification); repaint(); }

private:
    void timerCallback() override;
    class IconButton : public juce::Button
    {
    public:
        IconButton (const juce::String& name, int kind) : juce::Button (name), iconKind (kind) {}
        void paintButton (juce::Graphics&, bool over, bool down) override;
        int iconKind;
    };
    MysteryBoxAudioProcessor& proc;
    std::unique_ptr<IconButton> buttons[3];
    int active = -1;
    float meter = 0.0f;
    theme::CachedLayer plate;
};

} // namespace mbx
