// Mystery Box Synthesizer by Raygun
// Slide-out panels (Settings / FX / Favorites) drawn inside the editor, so
// they work in every DAW without extra windows.
#pragma once

#include <JuceHeader.h>
#include "LookAndFeel.h"
#include "Theme.h"
#include "../util/PersistentStore.h"

namespace mbx
{
class MysteryBoxAudioProcessor;

class SlidePanel : public juce::Component, private juce::Timer
{
public:
    explicit SlidePanel (const juce::String& title);
    void paint (juce::Graphics&) override;
    void open();
    void close();
    bool isOpen() const noexcept { return target > 0.5f; }
    std::function<void()> onClosed;
    std::function<void (float)> onSlide;
    juce::Rectangle<int> getContentArea() const { return getLocalBounds().withTrimmedTop (38).reduced (12, 6); }

protected:
    juce::String panelTitle;
    juce::TextButton closeButton { "CLOSE" };

private:
    void timerCallback() override;
    float position = 0.0f, target = 0.0f;
};

//==============================================================================
class SettingsPanel : public SlidePanel, private juce::ChangeListener
{
public:
    explicit SettingsPanel (MysteryBoxAudioProcessor& p);
    ~SettingsPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    std::function<void()> onShowAbout;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void push();
    MysteryBoxAudioProcessor& proc;
    juce::ComboBox spinCost, difficulty, graphics, fps, uiScale, stealMode;
    juce::ToggleButton freeSpin { "Free Spin / practice mode" }, midiShooting { "MIDI shooting mode" }, mascot { "Mascot event" },
                       preview { "Spin preview sound" }, chimeKey { "Chime follows played key" }, sfxOut { "Send game SFX to plugin output" },
                       gpu { "GPU (OpenGL) rendering" }, oversample { "2x oversampling (drive stages)" }, mpe { "MPE (per-note bend/pressure/slide)" },
                       fxFollow { "FX follow instrument" };
    juce::Slider sfxVolume, polyLimit, a4, bendRange;
    juce::TextButton resetPoints { "RESET POINTS" }, resetProgress { "RESET PROGRESS" }, about { "ABOUT" };
    juce::OwnedArray<juce::Label> labels;
    bool updating = false;
};

//==============================================================================
class FxPanel : public SlidePanel, public juce::DragAndDropContainer
{
public:
    explicit FxPanel (MysteryBoxAudioProcessor& p);
    ~FxPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void moveFx (int fromSlot, int toSlot);

private:
    class Card;
    void layoutCards();
    MysteryBoxAudioProcessor& proc;
    juce::OwnedArray<Card> cards; // indexed by FX id
    juce::ToggleButton ambience { "Round Ambience" };
    juce::Slider ambienceLevel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ambienceLevelAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ambienceAttach;
};

//==============================================================================
class FavoritesPanel : public SlidePanel, private juce::ChangeListener, private juce::ListBoxModel
{
public:
    explicit FavoritesPanel (MysteryBoxAudioProcessor& p);
    ~FavoritesPanel() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    int getNumRows() override { return rows.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    juce::String getTooltipForRow (int row) override;
    void rebuild();
    juce::Rectangle<float> cellRect (int i) const;

    MysteryBoxAudioProcessor& proc;
    juce::TextEditor search;
    juce::ComboBox category, rarity, sort, gridCategory;
    juce::ListBox list { "Favorites", this };
    juce::TextButton importButton { "IMPORT .mbxfav" }, exportButton { "EXPORT .mbxfav" };
    juce::Array<int> rows; // preset ids
    std::unique_ptr<juce::FileChooser> chooser;
    int hoverCell = -1;
    juce::String status;
};

//==============================================================================
class AboutOverlay : public juce::Component
{
public:
    AboutOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override { setVisible (false); }

private:
    juce::TextEditor text;
    juce::TextButton closeButton { "CLOSE" };
};

} // namespace mbx
