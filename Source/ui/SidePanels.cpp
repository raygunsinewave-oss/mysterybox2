// Mystery Box Synthesizer by Raygun
#include "SidePanels.h"
#include "../PluginProcessor.h"

namespace mbx
{
using namespace theme;

//==============================================================================
SlidePanel::SlidePanel (const juce::String& title) : panelTitle (title)
{
    closeButton.setTooltip ("Close panel");
    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
    setVisible (false);
    setTitle (title);
}

void SlidePanel::open() { target = 1.0f; setVisible (true); startTimerHz (60); }
void SlidePanel::close() { target = 0.0f; startTimerHz (60); }

void SlidePanel::timerCallback()
{
    position += (target - position) * 0.25f;
    if (std::abs (target - position) < 0.003f) position = target;
    if (onSlide) onSlide (position);
    if (position == target)
    {
        stopTimer();
        if (target == 0.0f) { setVisible (false); if (onClosed) onClosed(); }
    }
}

void SlidePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff121417));
    g.fillRoundedRectangle (b, 8.0f);
    g.setColour (neon.withAlpha (0.5f));
    g.drawRoundedRectangle (b.reduced (0.5f), 8.0f, 1.2f);
    g.setColour (neon);
    g.setFont (stencil (20.0f));
    g.drawText (panelTitle, juce::Rectangle<float> (16.0f, 6.0f, 400.0f, 28.0f), juce::Justification::centredLeft, false);
    g.setColour (metal.withAlpha (0.6f));
    g.drawLine (12.0f, 36.0f, b.getWidth() - 12.0f, 36.0f, 1.0f);
}

//==============================================================================
SettingsPanel::SettingsPanel (MysteryBoxAudioProcessor& p) : SlidePanel ("SETTINGS"), proc (p)
{
    auto addCombo = [this] (juce::ComboBox& c, const juce::StringArray& items, const juce::String& tip)
    {
        c.addItemList (items, 1);
        c.setTooltip (tip);
        c.setTitle (tip);
        c.onChange = [this] { push(); };
        addAndMakeVisible (c);
    };
    addCombo (spinCost, { "250", "500", "950", "1,500" }, "Mystery Box spin cost");
    addCombo (difficulty, { "Easy", "Normal", "Hard", "Nightmare" }, "Game difficulty");
    addCombo (graphics, { "Low (30 FPS, no particles/shake)", "Medium", "High" }, "Graphics quality");
    addCombo (fps, { "30 FPS", "60 FPS" }, "Frame-rate cap for the game view");
    addCombo (uiScale, { "75%", "100%", "125%", "150%", "175%", "200%" }, "UI scale");
    addCombo (stealMode, { "Steal oldest", "Steal quietest", "Steal released first" }, "Voice stealing mode");

    struct T { juce::ToggleButton* b; const char* tip; };
    for (auto t : { T { &freeSpin, "Box costs 0 points: for when you just want to make music" },
                    T { &midiShooting, "Each MIDI note-on fires at the nearest zombie; higher velocity = more damage" },
                    T { &mascot, "1-in-15 chance (after 5 spins) that the box moves away and refunds your points" },
                    T { &preview, "Play a short phrase of the instrument when the box lands" },
                    T { &chimeKey, "Spin chimes follow the key of the notes you play (C minor by default)" },
                    T { &sfxOut, "Game sounds are kept out of your mix by default; enable to hear them in a DAW" },
                    T { &gpu, "Render the game view with OpenGL on the GPU" },
                    T { &oversample, "2x oversampling on the non-linear drive/saturation stages" },
                    T { &mpe, "MPE: per-note pitch bend (48 st), pressure and slide on member channels" },
                    T { &fxFollow, "Loading an instrument also loads its suggested reverb/delay/chorus" } })
    {
        t.b->setTooltip (t.tip);
        t.b->onClick = [this] { push(); };
        addAndMakeVisible (*t.b);
    }
    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix, const juce::String& tip)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        s.setTooltip (tip);
        s.setTitle (tip);
        s.onValueChange = [this] { push(); };
        addAndMakeVisible (s);
    };
    setupSlider (sfxVolume, 0, 100, 1, "%", "Game sound-effects volume");
    setupSlider (polyLimit, 1, 32, 1, "", "Global polyphony limit (CPU safety)");
    setupSlider (a4, 415, 466, 0.1, " Hz", "A4 tuning reference");
    setupSlider (bendRange, 1, 24, 1, " st", "Pitch-bend range in semitones");

    resetPoints.setTooltip ("Set this project's points to zero");
    resetPoints.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Reset points?", "Set the points in this project to 0?", "Reset", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) { proc.setPoints (0); proc.getGame().reset (false); } }));
    };
    resetProgress.setTooltip ("Clear discovered instruments, favourites and Easter-egg progress (all projects)");
    resetProgress.onClick = [this]
    {
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Reset ALL progress?",
            "This clears your collection, favourites and secrets for every project and DAW. This cannot be undone.", "Reset everything", "Cancel", this,
            juce::ModalCallbackFunction::create ([this] (int r) { if (r == 1) proc.getStore().resetProgress(); }));
    };
    about.onClick = [this] { if (onShowAbout) onShowAbout(); };
    about.setTooltip ("Version, credits and open-source licences");
    for (auto* b : { &resetPoints, &resetProgress, &about }) addAndMakeVisible (*b);

    proc.getStore().addChangeListener (this);
    refresh();
}

SettingsPanel::~SettingsPanel() { proc.getStore().removeChangeListener (this); }

void SettingsPanel::changeListenerCallback (juce::ChangeBroadcaster*) { refresh(); }

void SettingsPanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto& s = proc.getStore().getSettings();
    spinCost.setSelectedId (s.spinCost == 250 ? 1 : s.spinCost == 500 ? 2 : s.spinCost == 1500 ? 4 : 3, juce::dontSendNotification);
    difficulty.setSelectedId (s.difficulty + 1, juce::dontSendNotification);
    graphics.setSelectedId (s.graphics + 1, juce::dontSendNotification);
    fps.setSelectedId (s.fpsCap == 30 ? 1 : 2, juce::dontSendNotification);
    static const int scales[] = { 75, 100, 125, 150, 175, 200 };
    for (int i = 0; i < 6; ++i) if (scales[i] == s.uiScalePercent) uiScale.setSelectedId (i + 1, juce::dontSendNotification);
    stealMode.setSelectedId (s.stealMode + 1, juce::dontSendNotification);
    freeSpin.setToggleState (s.freeSpin, juce::dontSendNotification);
    midiShooting.setToggleState (s.midiShooting, juce::dontSendNotification);
    mascot.setToggleState (s.mascotEnabled, juce::dontSendNotification);
    preview.setToggleState (s.spinPreview, juce::dontSendNotification);
    chimeKey.setToggleState (s.chimeFollowsKey, juce::dontSendNotification);
    sfxOut.setToggleState (s.sfxToOutput, juce::dontSendNotification);
    gpu.setToggleState (s.gpuRendering, juce::dontSendNotification);
    oversample.setToggleState (s.oversampling, juce::dontSendNotification);
    mpe.setToggleState (s.mpe, juce::dontSendNotification);
    fxFollow.setToggleState (s.fxFollowPreset, juce::dontSendNotification);
    sfxVolume.setValue (s.sfxVolume * 100.0, juce::dontSendNotification);
    polyLimit.setValue (s.polyphonyLimit, juce::dontSendNotification);
    a4.setValue (s.a4, juce::dontSendNotification);
    bendRange.setValue (s.bendRange, juce::dontSendNotification);
    sfxOut.setEnabled (! proc.isStandalone());
}

void SettingsPanel::push()
{
    if (updating) return;
    UserSettings s = proc.getStore().getSettings();
    static const int costs[] = { 250, 500, 950, 1500 };
    static const int scales[] = { 75, 100, 125, 150, 175, 200 };
    s.spinCost = costs[juce::jlimit (0, 3, spinCost.getSelectedId() - 1)];
    s.difficulty = juce::jlimit (0, 3, difficulty.getSelectedId() - 1);
    s.graphics = juce::jlimit (0, 2, graphics.getSelectedId() - 1);
    s.fpsCap = fps.getSelectedId() == 1 ? 30 : 60;
    s.uiScalePercent = scales[juce::jlimit (0, 5, uiScale.getSelectedId() - 1)];
    s.stealMode = juce::jlimit (0, 2, stealMode.getSelectedId() - 1);
    s.freeSpin = freeSpin.getToggleState();
    s.midiShooting = midiShooting.getToggleState();
    s.mascotEnabled = mascot.getToggleState();
    s.spinPreview = preview.getToggleState();
    s.chimeFollowsKey = chimeKey.getToggleState();
    s.sfxToOutput = sfxOut.getToggleState();
    s.gpuRendering = gpu.getToggleState();
    s.oversampling = oversample.getToggleState();
    s.mpe = mpe.getToggleState();
    s.fxFollowPreset = fxFollow.getToggleState();
    s.sfxVolume = (float) sfxVolume.getValue() / 100.0f;
    s.polyphonyLimit = (int) polyLimit.getValue();
    s.a4 = (float) a4.getValue();
    s.bendRange = (int) bendRange.getValue();
    proc.getStore().setSettings (s);
}

void SettingsPanel::resized()
{
    closeButton.setBounds (getWidth() - 96, 6, 84, 26);
    auto area = getContentArea();
    const int colW = area.getWidth() / 3;
    const int rowH = 30;
    auto col = [&] (int i) { return juce::Rectangle<int> (area.getX() + i * colW, area.getY() + 18, colW - 16, area.getHeight() - 18); };
    auto place = [&] (juce::Rectangle<int>& c, juce::Component& comp, int labelW = 0)
    {
        auto r = c.removeFromTop (rowH);
        comp.setBounds (r.withTrimmedLeft (labelW).reduced (0, 3));
    };
    // Column 1: game
    auto c1 = col (0);
    place (c1, spinCost, 120); place (c1, difficulty, 120);
    for (auto* b : { &freeSpin, &midiShooting, &mascot, &preview, &chimeKey, &sfxOut }) place (c1, *b);
    place (c1, sfxVolume, 120);
    // Column 2: graphics + engine
    auto c2 = col (1);
    place (c2, graphics, 120); place (c2, fps, 120); place (c2, uiScale, 120); place (c2, gpu);
    place (c2, polyLimit, 120); place (c2, stealMode, 120); place (c2, oversample); place (c2, mpe); place (c2, fxFollow);
    // Column 3: tuning + data
    auto c3 = col (2);
    place (c3, a4, 120); place (c3, bendRange, 120);
    c3.removeFromTop (88);
    auto btnRow = c3.removeFromTop (34);
    resetPoints.setBounds (btnRow.removeFromLeft (btnRow.getWidth() / 2).reduced (2));
    resetProgress.setBounds (btnRow.reduced (2));
    c3.removeFromTop (6);
    about.setBounds (c3.removeFromTop (34).reduced (2));
}

void SettingsPanel::paint (juce::Graphics& g)
{
    SlidePanel::paint (g);
    auto area = getContentArea();
    const int colW = area.getWidth() / 3;
    g.setFont (stencil (12.0f));
    const char* heads[3] = { "GAME", "GRAPHICS & ENGINE", "TUNING, MIDI & DATA" };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (neon.withAlpha (0.8f));
        g.drawText (heads[i], juce::Rectangle<int> (area.getX() + i * colW, area.getY(), colW, 16), juce::Justification::left, false);
    }
    g.setFont (ui (14.0f));
    g.setColour (chalk);
    auto lbl = [&] (juce::Component& c, const char* text) { g.drawText (text, juce::Rectangle<int> (c.getX() - 120, c.getY(), 116, c.getHeight()), juce::Justification::centredLeft, false); };
    lbl (spinCost, "Spin cost"); lbl (difficulty, "Difficulty"); lbl (sfxVolume, "SFX volume");
    lbl (graphics, "Graphics"); lbl (fps, "FPS cap"); lbl (uiScale, "UI scale"); lbl (polyLimit, "Polyphony limit"); lbl (stealMode, "Voice stealing");
    lbl (a4, "A4 reference"); lbl (bendRange, "Pitch-bend range");
    auto c3 = juce::Rectangle<int> (area.getX() + 2 * colW, area.getY() + 18 + 60, colW - 16, 88);
    g.setColour (chalkDim);
    g.setFont (ui (13.0f));
    g.drawFittedText ("MIDI Learn: right-click any knob, choose 'MIDI Learn', then move a controller.\n"
                      "Progress file: " + proc.getStore().getStorageFile().getParentDirectory().getFullPathName(),
                      c3.reduced (0, 4), juce::Justification::topLeft, 5);
}

//==============================================================================
class FxPanel::Card : public juce::Component, public juce::DragAndDropTarget
{
public:
    Card (FxPanel& o, MysteryBoxAudioProcessor& p, int fxId) : owner (o), proc (p), id (fxId)
    {
        const auto& d = ids::fx[(size_t) id];
        on.setButtonText ("");
        on.setTooltip (juce::String ("Enable ") + d.name);
        on.setTitle (juce::String (d.name) + " on");
        addAndMakeVisible (on);
        onAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getAPVTS(), ids::fxOn (id), on);
        mix = std::make_unique<Knob> (proc, ids::fxMix (id), "Mix", juce::String (d.name) + " wet/dry mix");
        addAndMakeVisible (*mix);
        for (int i = 0; i < d.numParams; ++i)
        {
            const auto& pd = d.params[(size_t) i];
            if (pd.kind == ids::FxParamKind::Choice)
            {
                auto* cb = combos.add (new juce::ComboBox());
                cb->addItemList (juce::StringArray::fromTokens (pd.choices, "|", ""), 1);
                cb->setTooltip (juce::String (d.name) + ": " + pd.name);
                cb->setTitle (pd.name);
                addAndMakeVisible (cb);
                comboAttach.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (proc.getAPVTS(), ids::fxParam (id, i), *cb));
                comboIndex.add (i);
            }
            else if (pd.kind == ids::FxParamKind::Bool)
            {
                auto* tb = toggles.add (new juce::ToggleButton (pd.name));
                tb->setTooltip (juce::String (d.name) + ": " + pd.name);
                addAndMakeVisible (tb);
                toggleAttach.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (proc.getAPVTS(), ids::fxParam (id, i), *tb));
            }
            else addAndMakeVisible (knobs.add (new Knob (proc, ids::fxParam (id, i), pd.name, juce::String (d.name) + ": " + pd.name)));
        }
        setTitle (d.name);
        setDescription ("Drag the handle to reorder the FX chain");
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto head = r.removeFromTop (22);
        head.removeFromLeft (26);
        on.setBounds (head.removeFromRight (36));
        auto row = r;
        mix->setBounds (row.removeFromLeft (52));
        for (auto* k : knobs) k->setBounds (row.removeFromLeft (52));
        auto rest = row.reduced (2, 8);
        for (auto* c : combos) c->setBounds (rest.removeFromTop (26).withWidth (juce::jmin (rest.getWidth(), 110))), rest.removeFromTop (6);
        for (auto* t : toggles) t->setBounds (rest.removeFromTop (26).withWidth (juce::jmin (rest.getWidth(), 120)));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& d = ids::fx[(size_t) id];
        auto b = getLocalBounds().toFloat().reduced (2.0f);
        const bool active = on.getToggleState();
        g.setColour (juce::Colours::black.withAlpha (dragOver ? 0.2f : 0.45f));
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (dragOver ? gold : (active ? neon.withAlpha (0.7f) : metal.withAlpha (0.6f)));
        g.drawRoundedRectangle (b, 6.0f, dragOver ? 2.0f : 1.0f);
        // Drag handle + slot number
        g.setColour (chalkDim);
        for (int i = 0; i < 3; ++i) g.fillRect (12.0f, 12.0f + (float) i * 4.0f, 12.0f, 2.0f);
        g.setColour (active ? neon : chalk);
        g.setFont (stencil (13.0f));
        g.drawText (juce::String (slot + 1) + ". " + d.shortName, juce::Rectangle<float> (30.0f, 6.0f, 200.0f, 20.0f), juce::Justification::centredLeft, false);
        if (id == (int) FxId::Comp)
        {
            g.setColour (chalkDim);
            g.setFont (ui (11.0f));
            g.drawText ("+ safety limiter -0.3 dBFS (always on)", juce::Rectangle<float> (b.getRight() - 230.0f, b.getBottom() - 16.0f, 224.0f, 14.0f), juce::Justification::right, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { dragStartX = e.x; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStartX < 30 && e.getDistanceFromDragStart() > 4)
            owner.startDragging (juce::var (id), this, juce::ScaledImage(), true);
    }
    bool isInterestedInDragSource (const SourceDetails& s) override { return s.description.isInt() && (int) s.description != id; }
    void itemDragEnter (const SourceDetails&) override { dragOver = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dragOver = false; repaint(); }
    void itemDropped (const SourceDetails& s) override
    {
        dragOver = false;
        const auto order = proc.getFxOrder();
        int from = -1;
        for (int i = 0; i < kNumFx; ++i) if (order[(size_t) i] == (int) s.description) from = i;
        owner.moveFx (from, slot);
    }

    int slot = 0;
    const int id;

private:
    FxPanel& owner;
    MysteryBoxAudioProcessor& proc;
    juce::ToggleButton on;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAttach;
    std::unique_ptr<Knob> mix;
    juce::OwnedArray<Knob> knobs;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttach;
    juce::Array<int> comboIndex;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> toggleAttach;
    int dragStartX = 100;
    bool dragOver = false;
};

FxPanel::FxPanel (MysteryBoxAudioProcessor& p) : SlidePanel ("FX CHAIN"), proc (p)
{
    for (int i = 0; i < kNumFx; ++i) addAndMakeVisible (cards.add (new Card (*this, proc, i)));
    ambience.setTooltip ("Round Ambience: subtle, synthesized distant groans and wind under the synth");
    addAndMakeVisible (ambience);
    ambienceAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getAPVTS(), ids::ambienceOn, ambience);
    ambienceLevel.setSliderStyle (juce::Slider::LinearHorizontal);
    ambienceLevel.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    ambienceLevel.setTooltip ("Round Ambience level");
    ambienceLevel.setTitle ("Round Ambience level");
    addAndMakeVisible (ambienceLevel);
    ambienceLevelAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getAPVTS(), ids::ambienceLevel, ambienceLevel);
}

FxPanel::~FxPanel() = default;

void FxPanel::moveFx (int fromSlot, int toSlot)
{
    if (fromSlot < 0 || toSlot < 0 || fromSlot == toSlot) return;
    auto order = proc.getFxOrder();
    std::vector<int> v (order.begin(), order.end());
    const int moved = v[(size_t) fromSlot];
    v.erase (v.begin() + fromSlot);
    v.insert (v.begin() + toSlot, moved);
    for (int i = 0; i < kNumFx; ++i) order[(size_t) i] = v[(size_t) i];
    proc.setFxOrder (order);
    layoutCards();
}

void FxPanel::layoutCards()
{
    auto area = getContentArea();
    const int cw = area.getWidth() / 3, ch = area.getHeight() / 3;
    const auto order = proc.getFxOrder();
    for (int s = 0; s < kNumFx; ++s)
    {
        auto* c = cards[order[(size_t) s]];
        c->slot = s;
        c->setBounds (area.getX() + (s % 3) * cw, area.getY() + (s / 3) * ch, cw - 4, ch - 4);
        c->repaint();
    }
}

void FxPanel::resized()
{
    closeButton.setBounds (getWidth() - 96, 6, 84, 26);
    ambience.setBounds (getWidth() - 420, 7, 170, 24);
    ambienceLevel.setBounds (getWidth() - 250, 8, 140, 22);
    layoutCards();
}

void FxPanel::paint (juce::Graphics& g)
{
    SlidePanel::paint (g);
    g.setColour (chalkDim);
    g.setFont (ui (12.0f));
    g.drawText ("Drag a card's handle onto another card to reorder. Signal flows 1 -> 9.", juce::Rectangle<float> (180.0f, 10.0f, 420.0f, 20.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
FavoritesPanel::FavoritesPanel (MysteryBoxAudioProcessor& p) : SlidePanel ("FAVORITES & COLLECTION"), proc (p)
{
    search.setTextToShowWhenEmpty ("Search favourites...", chalkDim);
    search.onTextChange = [this] { rebuild(); };
    search.setTitle ("Search favourites");
    addAndMakeVisible (search);
    category.addItem ("All categories", 1);
    gridCategory.setTooltip ("Collection grid category");
    const auto& cats = proc.getLibrary().getCategoryNames();
    for (int i = 0; i < (int) cats.size(); ++i) { category.addItem (cats[(size_t) i], i + 2); gridCategory.addItem (cats[(size_t) i], i + 1); }
    category.setSelectedId (1, juce::dontSendNotification);
    gridCategory.setSelectedId (1, juce::dontSendNotification);
    rarity.addItemList ({ "All rarities", "Common", "Rare", "Epic", "Legendary", "Wonder" }, 1);
    rarity.setSelectedId (1, juce::dontSendNotification);
    sort.addItemList ({ "Recently added", "Name", "Rarity", "Category", "ID" }, 1);
    sort.setSelectedId (1, juce::dontSendNotification);
    for (auto* c : { &category, &rarity, &sort }) { c->onChange = [this] { rebuild(); }; addAndMakeVisible (*c); }
    category.setTooltip ("Filter by category"); rarity.setTooltip ("Filter by rarity"); sort.setTooltip ("Sort order");
    gridCategory.onChange = [this] { repaint(); };
    addAndMakeVisible (gridCategory);
    list.setRowHeight (26);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setTitle ("Favourites list");
    addAndMakeVisible (list);
    importButton.setTooltip ("Import favourites from a .mbxfav file (only instruments you have discovered)");
    exportButton.setTooltip ("Export your favourites to a .mbxfav file");
    importButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Import favourites", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.mbxfav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile()) { const int n = proc.getStore().importFavorites (fc.getResult()); status = "Imported " + juce::String (n) + " favourites."; repaint(); }
        });
    };
    exportButton.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Export favourites", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MyFavorites.mbxfav"), "*.mbxfav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            auto f = fc.getResult();
            if (f != juce::File()) { f = f.withFileExtension ("mbxfav"); status = proc.getStore().exportFavorites (f) ? "Exported to " + f.getFileName() : juce::String ("Export failed."); repaint(); }
        });
    };
    addAndMakeVisible (importButton);
    addAndMakeVisible (exportButton);
    proc.getStore().addChangeListener (this);
    rebuild();
}

FavoritesPanel::~FavoritesPanel() { proc.getStore().removeChangeListener (this); }
void FavoritesPanel::changeListenerCallback (juce::ChangeBroadcaster*) { rebuild(); repaint(); }

void FavoritesPanel::rebuild()
{
    const auto& lib = proc.getLibrary();
    const auto q = search.getText().trim().toLowerCase();
    const int cat = category.getSelectedId() - 2, rar = rarity.getSelectedId() - 2;
    juce::Array<FavoriteEntry> favs (proc.getStore().getFavorites());
    rows.clear();
    juce::Array<std::pair<FavoriteEntry, const PresetInfo*>> items;
    for (auto& f : favs)
        if (const auto* info = lib.findById (f.id))
        {
            if (cat >= 0 && info->category != cat) continue;
            if (rar >= 0 && info->rarity != rar) continue;
            if (q.isNotEmpty() && ! juce::String (info->name).toLowerCase().contains (q) && ! juce::String (info->tags).toLowerCase().contains (q)
                && ! juce::String (lib.categoryName (info->category)).toLowerCase().contains (q)) continue;
            items.add ({ f, info });
        }
    const int mode = sort.getSelectedId();
    std::sort (items.begin(), items.end(), [mode] (const auto& a, const auto& b)
    {
        switch (mode)
        {
            case 2: return a.second->name < b.second->name;
            case 3: return a.second->rarity != b.second->rarity ? a.second->rarity > b.second->rarity : a.second->name < b.second->name;
            case 4: return a.second->category != b.second->category ? a.second->category < b.second->category : a.second->name < b.second->name;
            case 5: return a.second->id < b.second->id;
            default: return a.first.addedTime > b.first.addedTime;
        }
    });
    for (auto& it : items) rows.add (it.second->id);
    list.updateContent();
    list.repaint();
}

void FavoritesPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    const auto* info = proc.getLibrary().findById (rows[row]);
    if (info == nullptr) return;
    const auto c = rarityTint (info->rarity);
    if (selected) { g.setColour (neon.withAlpha (0.15f)); g.fillRect (0, 0, w, h); }
    drawCategoryIcon (g, info->category, { 6.0f, 4.0f, 18.0f, 18.0f }, c);
    g.setColour (info->id == proc.getCurrentInstrumentId() ? neon : chalk);
    g.setFont (ui (15.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 (info->name.c_str())), 32, 0, w / 2, h, juce::Justification::centredLeft, true);
    g.setColour (chalkDim);
    g.setFont (ui (12.0f));
    g.drawText (proc.getLibrary().categoryName (info->category), w / 2 + 30, 0, w / 2 - 110, h, juce::Justification::centredLeft, true);
    g.setColour (c);
    g.drawText (rarityName (info->rarity), w - 80, 0, 74, h, juce::Justification::centredRight, false);
}

void FavoritesPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        const int id = rows[row];
        juce::PopupMenu m;
        m.addItem (1, "Load");
        m.addItem (2, "Remove from favourites");
        m.showMenuAsync ({}, [this, id] (int r) { if (r == 1) proc.loadInstrument (id, true, false); if (r == 2) proc.getStore().setFavorite (id, false); });
        return;
    }
    proc.loadInstrument (rows[row], true, false); // already won: no points needed
    list.repaint();
}
void FavoritesPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) { listBoxItemClicked (row, e); }
juce::String FavoritesPanel::getTooltipForRow (int row)
{
    const auto* info = proc.getLibrary().findById (rows[row]);
    return info ? "Click to load " + juce::String (juce::CharPointer_UTF8 (info->name.c_str())) + " (free). Right-click for options." : juce::String();
}

juce::Rectangle<float> FavoritesPanel::cellRect (int i) const
{
    const auto area = getContentArea();
    const float gx = (float) area.getX() + (float) area.getWidth() * 0.5f + 12.0f, gy = (float) area.getY() + 64.0f;
    const float cs = juce::jmin (((float) area.getWidth() * 0.5f - 24.0f) / 20.0f, ((float) area.getHeight() - 70.0f) / 5.0f);
    return { gx + (float) (i % 20) * cs, gy + (float) (i / 20) * cs, cs - 3.0f, cs - 3.0f };
}

void FavoritesPanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1;
    for (int i = 0; i < 100; ++i) if (cellRect (i).contains (e.position)) h = i;
    if (h != hoverCell) { hoverCell = h; repaint(); }
}

void FavoritesPanel::mouseDown (const juce::MouseEvent& e)
{
    const int cat = gridCategory.getSelectedId() - 1;
    for (int i = 0; i < 100; ++i)
        if (cellRect (i).contains (e.position))
        {
            const int id = cat * 100 + i + 1;
            auto& st = proc.getStore();
            if (st.isDiscovered (id)) st.setFavorite (id, ! st.isFavorite (id)); // star / unstar
        }
}

void FavoritesPanel::resized()
{
    closeButton.setBounds (getWidth() - 96, 6, 84, 26);
    auto area = getContentArea();
    auto left = area.removeFromLeft (area.getWidth() / 2).reduced (0, 0);
    auto top = left.removeFromTop (30);
    search.setBounds (top.removeFromLeft (190).reduced (0, 2));
    top.removeFromLeft (6);
    category.setBounds (top.removeFromLeft (150).reduced (0, 2));
    top.removeFromLeft (6);
    rarity.setBounds (top.removeFromLeft (100).reduced (0, 2));
    top.removeFromLeft (6);
    sort.setBounds (top.reduced (0, 2));
    auto bottom = left.removeFromBottom (32);
    importButton.setBounds (bottom.removeFromLeft (150).reduced (0, 3));
    bottom.removeFromLeft (8);
    exportButton.setBounds (bottom.removeFromLeft (150).reduced (0, 3));
    left.removeFromTop (4);
    list.setBounds (left.reduced (0, 2));
    gridCategory.setBounds (area.getX() + 12, area.getY() + 30, 240, 26);
}

void FavoritesPanel::paint (juce::Graphics& g)
{
    SlidePanel::paint (g);
    auto area = getContentArea();
    const auto& lib = proc.getLibrary();
    auto& st = proc.getStore();
    // Collection tracker
    const int found = st.getDiscoveredCount();
    auto right = area.withTrimmedLeft (area.getWidth() / 2 + 12);
    g.setColour (gold);
    g.setFont (stencil (16.0f));
    g.drawText ("DISCOVERED " + juce::String (found) + " / 2,500", right.removeFromTop (24).toFloat(), juce::Justification::centredLeft, false);
    auto barR = juce::Rectangle<float> ((float) right.getX() + 250.0f, (float) area.getY() + 36.0f, (float) right.getWidth() - 256.0f, 10.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (barR, 4.0f);
    g.setColour (gold); g.fillRoundedRectangle (barR.withWidth (barR.getWidth() * (float) found / 2500.0f), 4.0f);

    const int cat = gridCategory.getSelectedId() - 1;
    int catFound = 0;
    for (int i = 0; i < 100; ++i)
    {
        const int id = cat * 100 + i + 1;
        const auto r = cellRect (i);
        const auto* info = lib.findById (id);
        const bool disc = st.isDiscovered (id);
        catFound += disc ? 1 : 0;
        g.setColour (juce::Colours::black.withAlpha (disc ? 0.35f : 0.6f));
        g.fillRoundedRectangle (r, 3.0f);
        if (disc && info != nullptr)
        {
            const auto c = rarityTint (info->rarity);
            drawCategoryIcon (g, info->category, r.reduced (r.getWidth() * 0.2f), c);
            g.setColour (c.withAlpha (i == hoverCell ? 1.0f : 0.6f));
            g.drawRoundedRectangle (r, 3.0f, i == hoverCell ? 1.6f : 1.0f);
            if (st.isFavorite (id)) { juce::Path s; s.addStar ({ r.getRight() - 4.0f, r.getY() + 4.0f }, 5, 1.5f, 3.5f); g.setColour (gold); g.fillPath (s); }
        }
        else
        {
            g.setColour (metal.withAlpha (0.5f));
            g.setFont (stencil (r.getHeight() * 0.6f));
            g.drawText ("?", r, juce::Justification::centred, false); // locked silhouette
        }
    }
    g.setColour (chalkDim);
    g.setFont (ui (12.5f));
    const auto last = cellRect (99);
    juce::String hover;
    if (hoverCell >= 0)
    {
        const int id = cat * 100 + hoverCell + 1;
        if (const auto* info = lib.findById (id); info != nullptr && st.isDiscovered (id))
            hover = "#" + juce::String (id) + " " + juce::String (juce::CharPointer_UTF8 (info->name.c_str())) + " (" + rarityName (info->rarity) + ") - click to star";
        else hover = "#" + juce::String (id) + " ??? - not yet discovered";
    }
    g.drawText (juce::String (catFound) + "/100 in this category   " + hover, juce::Rectangle<float> (cellRect (0).getX(), last.getBottom() + 4.0f, 560.0f, 16.0f), juce::Justification::left, true);
    if (status.isNotEmpty())
    {
        g.setColour (neon);
        g.drawText (status, juce::Rectangle<float> ((float) area.getX() + 320.0f, (float) area.getBottom() - 28.0f, 280.0f, 24.0f), juce::Justification::centredLeft, true);
    }
    if (rows.isEmpty())
    {
        g.setColour (chalkDim);
        g.setFont (ui (14.0f));
        g.drawFittedText (st.getFavorites().isEmpty() ? "No favourites yet. Star an instrument from the box result or the instrument panel, or click a discovered cell in the collection grid."
                                                      : "No favourites match the filters.",
                          list.getBounds().reduced (20), juce::Justification::centred, 4);
    }
}

//==============================================================================
AboutOverlay::AboutOverlay()
{
    text.setMultiLine (true);
    text.setReadOnly (true);
    text.setScrollbarsShown (true);
    text.setFont (ui (15.0f));
    text.setText (juce::String ("MYSTERY BOX SYNTHESIZER v") + JucePlugin_VersionString + "\nby Raygun\n\n"
        "A hybrid synthesizer whose 2,500 instruments are won from a Mystery Box by surviving a zombie mini-game.\n\n"
        "CREDITS\nDesign, sound engine, instruments, game, art and sound effects: Raygun. "
        "All art is drawn in code and every sound effect is synthesized procedurally: no third-party samples or game assets are used.\n\n"
        "OPEN-SOURCE LICENCES\n"
        "- JUCE 8 (juce.com): dual licensed AGPLv3 / JUCE commercial licence. See LICENSE_NOTES.md.\n"
        "- VST is a trademark of Steinberg Media Technologies GmbH. The VST3 SDK ships with JUCE under its own licence terms.\n"
        "- Fonts (SIL Open Font License 1.1): Black Ops One (The Black-Ops Project Authors), Cabin Sketch (The Cabin Project Authors), "
        "Share Tech Mono (Carrois Type Design), Oswald (The Oswald Project Authors). Licence texts are bundled in Resources/fonts.\n"
        "- Catch2 (Boost Software License 1.0) is used for the unit tests only.\n\n"
        "This software is provided as-is, without warranty of any kind.");
    addAndMakeVisible (text);
    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);
    setTitle ("About");
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.7f));
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (640.0f, 460.0f);
    drawRustPlate (g, r, 77, 10.0f);
    g.setColour (neon);
    g.setFont (stencil (26.0f));
    g.drawText ("ABOUT", r.removeFromTop (48.0f), juce::Justification::centred, false);
}

void AboutOverlay::resized()
{
    auto r = getLocalBounds().withSizeKeepingCentre (640, 460);
    r.removeFromTop (48);
    closeButton.setBounds (r.removeFromBottom (44).withSizeKeepingCentre (120, 30));
    text.setBounds (r.reduced (20, 6));
}

} // namespace mbx
