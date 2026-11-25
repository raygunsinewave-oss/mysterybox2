// Mystery Box Synthesizer by Raygun
#pragma once

#include <JuceHeader.h>
#include "Theme.h"

namespace mbx
{
class MbxLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MbxLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool down) override;
    void fillTextEditorBackground (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int w, int h, juce::TextEditor&) override;
    void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
};

/** Slider whose right-click opens our MIDI-learn menu instead of starting a drag. */
class MbxSlider : public juce::Slider
{
public:
    std::function<void()> onRightClick;
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onRightClick) { onRightClick(); return; }
        juce::Slider::mouseDown (e);
    }
};

class MysteryBoxAudioProcessor;

/** Labelled rotary knob bound to a host parameter, with right-click MIDI Learn. */
class Knob : public juce::Component, private juce::Timer
{
public:
    Knob (MysteryBoxAudioProcessor& p, const juce::String& paramId, const juce::String& label, const juce::String& tooltip,
          bool large = false);
    ~Knob() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void setAccent (juce::Colour c) { slider.getProperties().set ("accent", (juce::int64) c.getARGB()); repaint(); }
    juce::Slider& getSlider() noexcept { return slider; }
    void setLabel (const juce::String& l) { label = l; repaint(); }

private:
    void timerCallback() override;
    void showMenu();
    MysteryBoxAudioProcessor& proc;
    juce::String paramId, label;
    MbxSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    bool large;
    int learnFlash = 0;
};

} // namespace mbx
