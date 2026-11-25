// Mystery Box Synthesizer by Raygun
#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "game/GameView.h"
#include "ui/LookAndFeel.h"
#include "ui/MainPanels.h"
#include "ui/SidePanels.h"
#include "ui/TitlePanel.h"

namespace mbx
{
class MysteryBoxAudioProcessorEditor : public juce::AudioProcessorEditor,
                                       private juce::ChangeListener,
                                       private juce::Timer
{
public:
    explicit MysteryBoxAudioProcessorEditor (MysteryBoxAudioProcessor&);
    ~MysteryBoxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Root content at the 1200x800 design size; scaled as a whole with one transform. */
    class Root : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override { g.fillAll (theme::bg); }
    };
    /** Non-interactive overlay: Overclocked aura + Easter-egg light flicker. */
    class Overlay : public juce::Component
    {
    public:
        Overlay() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        float aura = 0.0f, flicker = 0.0f, t = 0.0f;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void showPanel (int index);
    void secretUnlocked();
    void applyUiScale();

    MysteryBoxAudioProcessor& proc;
    MbxLookAndFeel lnf;
    Root root;
    TitlePanel title;
    GameView game;
    SideButtons side;
    OverclockerPanel overclocker;
    InstrumentPanel instrument;
    BottomBar bottom;
    SettingsPanel settings;
    FxPanel fx;
    FavoritesPanel favorites;
    AboutOverlay about;
    Overlay overlay;
    juce::TooltipWindow tooltips { &root, 600 };
    int openPanel = -1;
    float shakeTime = 0.0f, flickerTime = 0.0f;
    int appliedScale = 0;
    bool appliedGpu = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MysteryBoxAudioProcessorEditor)
};

} // namespace mbx
