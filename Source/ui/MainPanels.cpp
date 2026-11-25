// Mystery Box Synthesizer by Raygun
#include "MainPanels.h"
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
} // namespace

//==============================================================================
InstrumentPanel::InstrumentPanel (MysteryBoxAudioProcessor& p) : proc (p)
{
    static const char* macroTips[8] = {
        "Tone: overall brightness (filter cutoff and envelope amount)",
        "Character: resonance plus the instrument's signature timbre control",
        "Movement: depth and speed of the built-in modulation",
        "Space: adds reverb and delay",
        "Attack: softer or snappier note starts",
        "Release: shorter or longer note tails",
        "Grit: saturation and noise",
        "Width: stereo spread, unison detune and chorus" };
    for (int i = 0; i < 8; ++i)
        addAndMakeVisible (macros.add (new Knob (proc, ids::macro[i], ids::macroNames[i], macroTips[i], true)));

    overclockKnob = std::make_unique<Knob> (proc, ids::overclock, "Overclocked", "Overclocked: rich unison detune, harmonic exciter and stereo widening", true);
    overclockKnob->setAccent (wonder);
    addChildComponent (*overclockKnob);

    struct C { const char* id; const char* label; const char* tip; };
    static const C cs[] = {
        { ids::ampA, "Attack", "Amp envelope attack time" }, { ids::ampD, "Decay", "Amp envelope decay time" },
        { ids::ampS, "Sustain", "Amp envelope sustain level" }, { ids::ampR, "Release", "Amp envelope release time" },
        { ids::glide, "Glide", "Portamento time between notes" }, { ids::cutoff, "Cutoff", "Main filter cutoff frequency" },
        { ids::resonance, "Reso", "Main filter resonance" }, { ids::voices, "Voices", "Maximum polyphony for this instance" },
        { ids::volume, "Volume", "Output volume" }, { ids::pan, "Pan", "Output pan" } };
    for (auto& c : cs) addAndMakeVisible (controls.add (new Knob (proc, c.id, c.label, c.tip, false)));

    favButton.setButtonText (juce::CharPointer_UTF8 ("\xe2\x98\x85 FAVORITE"));
    favButton.setTooltip ("Star this instrument: it will be in your Favorites in every project and DAW");
    favButton.setClickingTogglesState (false);
    favButton.onClick = [this]
    {
        const int id = proc.getCurrentInstrumentId();
        if (id <= 0) return;
        auto& st = proc.getStore();
        st.setFavorite (id, ! st.isFavorite (id));
        timerCallback();
    };
    addAndMakeVisible (favButton);
    setTitle ("Current instrument");
    startTimerHz (15);
}

void InstrumentPanel::timerCallback()
{
    const int id = proc.getCurrentInstrumentId();
    const bool oc = proc.isOverclocked();
    const bool fav = id > 0 && proc.getStore().isFavorite (id);
    if (id != shownId || oc != shownOc || fav != shownFav)
    {
        shownId = id; shownOc = oc; shownFav = fav;
        overclockKnob->setVisible (oc);
        favButton.setToggleState (fav, juce::dontSendNotification);
        favButton.setEnabled (id > 0);
        favButton.setColour (juce::TextButton::textColourOffId, fav ? gold : chalk);
        resized();
        repaint();
    }
    pulse += 0.06f;
    if (oc || (id > 0 && proc.getLibrary().findById (id) != nullptr && proc.getLibrary().findById (id)->rarity >= 3))
        repaint (0, 0, 300, getHeight());
}

void InstrumentPanel::resized()
{
    plate.invalidate();
    favButton.setBounds (16, getHeight() - 44, 130, 30);
    const int top = 22;
    const int macroW = 68;
    for (int i = 0; i < 8; ++i) macros[i]->setBounds (304 + i * macroW, top, macroW, getHeight() - top - 8);
    overclockKnob->setBounds (304 + 8 * macroW + 2, top, macroW, getHeight() - top - 8);
    const int x0 = 930, kw = 54, kh = (getHeight() - top - 6) / 2;
    for (int i = 0; i < controls.size(); ++i)
        controls[i]->setBounds (x0 + (i % 5) * kw, top + (i / 5) * kh, kw, kh);
}

void InstrumentPanel::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds();
    plate.draw (g, b, [b] (juce::Graphics& pg)
    {
        drawRustPlate (pg, b.toFloat().reduced (2.0f), 3131, 6.0f);
        pg.setColour (juce::Colours::black.withAlpha (0.4f));
        pg.fillRoundedRectangle (juce::Rectangle<float> (296.0f, 8.0f, 622.0f, (float) b.getHeight() - 16.0f), 6.0f);
        pg.fillRoundedRectangle (juce::Rectangle<float> (924.0f, 8.0f, 270.0f, (float) b.getHeight() - 16.0f), 6.0f);
        pg.setColour (chalkDim);
        pg.setFont (stencil (11.0f));
        pg.drawText ("MACROS", juce::Rectangle<float> (304.0f, 10.0f, 200.0f, 12.0f), juce::Justification::left, false);
        pg.drawText ("ENGINE", juce::Rectangle<float> (932.0f, 10.0f, 200.0f, 12.0f), juce::Justification::left, false);
    });

    const auto* info = proc.getLibrary().findById (proc.getCurrentInstrumentId());
    auto r = juce::Rectangle<float> (16.0f, 12.0f, 270.0f, (float) getHeight() - 60.0f);
    if (info == nullptr)
    {
        drawChalkText (g, "EMPTY BOX", r.removeFromTop (44.0f), chalk, 30.0f, juce::Justification::centredLeft, 2);
        g.setColour (chalkDim);
        g.setFont (ui (14.0f));
        g.drawFittedText ("Earn points in the game, then open the Mystery Box to win an instrument.\n"
                          "(Practice / Free Spin mode is in Settings. A plain starter tone plays until then.)", r.toNearestInt(), juce::Justification::topLeft, 4);
        return;
    }
    const bool oc = proc.isOverclocked();
    const auto rc = rarityTint (info->rarity);
    const float pl = 0.5f + 0.5f * std::sin (pulse);
    drawRarityBadge (g, info->rarity, { r.getX(), r.getY(), 96.0f, 18.0f }, pl);
    g.setColour (chalkDim);
    g.setFont (mono (13.0f));
    g.drawText ("#" + juce::String (info->id).paddedLeft ('0', 4), juce::Rectangle<float> (r.getRight() - 80.0f, r.getY(), 80.0f, 18.0f), juce::Justification::centredRight, false);
    r.removeFromTop (24.0f);
    auto nameArea = r.removeFromTop (34.0f);
    if (oc) drawGlow (g, nameArea.getCentre(), 150.0f, wonder, 0.18f + 0.12f * pl);
    g.setColour (oc ? wonder : juce::Colours::white);
    g.setFont (ui (26.0f, true));
    g.drawFittedText (proc.getCurrentInstrumentName(), nameArea.toNearestInt(), juce::Justification::centredLeft, 1);
    auto catRow = r.removeFromTop (26.0f);
    drawCategoryIcon (g, info->category, catRow.removeFromLeft (24.0f).reduced (1.0f), rc);
    catRow.removeFromLeft (6.0f);
    g.setColour (rc);
    g.setFont (ui (15.0f));
    g.drawText (proc.getLibrary().categoryName (info->category), catRow, juce::Justification::centredLeft, true);
    g.setColour (chalkDim);
    g.setFont (ui (12.5f));
    g.drawText (juce::String (info->tags).replace (",", "  |  "), r.removeFromTop (20.0f), juce::Justification::centredLeft, true);
}

//==============================================================================
BottomBar::BottomBar (MysteryBoxAudioProcessor& p)
    : proc (p), keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    keyboard.setAvailableRange (36, 96);
    keyboard.setLowestVisibleKey (36);
    keyboard.setScrollButtonsVisible (false);
    keyboard.setOctaveForMiddleC (4);
    keyboard.setTitle ("On-screen keyboard");
    keyboard.setWantsKeyboardFocus (false); // never steal DAW shortcuts
    addAndMakeVisible (keyboard);
    panicButton.setTooltip ("All notes off (and stop previews/SFX)");
    panicButton.onClick = [this] { proc.panic(); };
    addAndMakeVisible (panicButton);
    shownPoints = (float) proc.getPoints();
    startTimerHz (30);
}

void BottomBar::resized()
{
    auto r = getLocalBounds();
    auto kb = r.removeFromTop (108);
    keyboard.setBounds (kb.reduced (4, 2));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 36.0f);
    panicButton.setBounds (getWidth() - 86, 116, 76, 30);
}

juce::Rectangle<float> BottomBar::tileRect (int i) const { return { 640.0f + (float) i * 44.0f, 114.0f, 38.0f, 38.0f }; }

void BottomBar::timerCallback()
{
    const float target = (float) proc.getPoints();
    shownPoints += (target - shownPoints) * 0.25f;
    if (std::abs (target - shownPoints) < 1.0f) shownPoints = target;
    cpu = cpu * 0.8f + proc.getCpuLoad() * 0.2f;
    peak = juce::jmax (proc.getOutputPeak(), peak * 0.85f);
    const auto inv = proc.getInventory();
    invIds.clearQuick(); invTaken.clearQuick();
    for (auto& it : inv) { invIds.add (it.id); invTaken.add (it.taken); }
    repaint (0, 108, getWidth(), getHeight() - 108);
}

void BottomBar::mouseMove (const juce::MouseEvent& e)
{
    hoverTile = -1;
    for (int i = 0; i < invIds.size(); ++i) if (tileRect (i).contains (e.position)) hoverTile = i;
}

void BottomBar::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < invIds.size(); ++i)
        if (tileRect (i).contains (e.position))
        {
            if (invTaken[i]) proc.loadInstrument (invIds[i], true, false); // you won it: reload for free
            else proc.playSfx ({ Sfx::DryFire, 60, 0.5f, 0 });
        }
}

void BottomBar::paint (juce::Graphics& g)
{
    auto bar = getLocalBounds().toFloat().withTrimmedTop (108.0f);
    g.setColour (panel.darker (0.3f));
    g.fillRect (bar);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawLine (0.0f, bar.getY(), (float) getWidth(), bar.getY(), 1.5f);

    // Points
    g.setColour (chalkDim);
    g.setFont (stencil (11.0f));
    g.drawText ("POINTS", juce::Rectangle<float> (14.0f, 116.0f, 80.0f, 12.0f), juce::Justification::left, false);
    g.setColour (neon);
    g.setFont (mono (26.0f));
    g.drawText (commas ((juce::int64) shownPoints), juce::Rectangle<float> (14.0f, 126.0f, 200.0f, 28.0f), juce::Justification::left, false);

    // CPU + voices + output meter
    auto meterBox = juce::Rectangle<float> (240.0f, 118.0f, 150.0f, 30.0f);
    g.setColour (chalkDim);
    g.setFont (stencil (11.0f));
    g.drawText ("CPU", meterBox.withWidth (40.0f).withHeight (12.0f), juce::Justification::left, false);
    const float cpuPct = juce::jlimit (0.0f, 1.0f, cpu);
    auto cpuBar = juce::Rectangle<float> (meterBox.getX(), meterBox.getY() + 15.0f, 110.0f, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (cpuBar, 3.0f);
    g.setColour (cpuPct > 0.7f ? danger : (cpuPct > 0.4f ? gold : neon));
    g.fillRoundedRectangle (cpuBar.withWidth (juce::jmax (2.0f, cpuBar.getWidth() * cpuPct)), 3.0f);
    g.setColour (chalk);
    g.setFont (mono (13.0f));
    g.drawText (juce::String (cpu * 100.0f, 1) + "%", juce::Rectangle<float> (cpuBar.getRight() + 4.0f, cpuBar.getY() - 4.0f, 50.0f, 16.0f), juce::Justification::left, false);

    g.setColour (chalkDim);
    g.setFont (stencil (11.0f));
    g.drawText ("VOICES", juce::Rectangle<float> (410.0f, 118.0f, 60.0f, 12.0f), juce::Justification::left, false);
    g.setColour (chalk);
    g.setFont (mono (16.0f));
    g.drawText (juce::String (proc.getActiveVoices()), juce::Rectangle<float> (410.0f, 130.0f, 60.0f, 18.0f), juce::Justification::left, false);

    g.setColour (chalkDim);
    g.setFont (stencil (11.0f));
    g.drawText ("OUT", juce::Rectangle<float> (480.0f, 118.0f, 60.0f, 12.0f), juce::Justification::left, false);
    auto outBar = juce::Rectangle<float> (480.0f, 133.0f, 120.0f, 8.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (outBar, 3.0f);
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    const float fr = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? danger : neon);
    g.fillRoundedRectangle (outBar.withWidth (outBar.getWidth() * fr), 3.0f);

    // Inventory: last 10 box results
    g.setColour (chalkDim);
    g.setFont (stencil (10.0f));
    g.drawText ("LAST 10", juce::Rectangle<float> (610.0f, 112.0f, 40.0f, 44.0f), juce::Justification::centredLeft, false);
    const auto& lib = proc.getLibrary();
    for (int i = 0; i < 10; ++i)
    {
        const auto r = tileRect (i);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r, 4.0f);
        if (i >= invIds.size()) { g.setColour (metal.withAlpha (0.35f)); g.drawRoundedRectangle (r, 4.0f, 1.0f); continue; }
        const auto* info = lib.findById (invIds[i]);
        if (info == nullptr) continue;
        const auto c = rarityTint (info->rarity).withAlpha (invTaken[i] ? 1.0f : 0.35f);
        g.setColour (c);
        g.drawRoundedRectangle (r, 4.0f, i == hoverTile ? 2.0f : 1.2f);
        drawCategoryIcon (g, info->category, r.reduced (8.0f), c);
        if (! invTaken[i])
        {
            g.setColour (danger.withAlpha (0.7f));
            g.drawLine (r.getX() + 6, r.getY() + 6, r.getRight() - 6, r.getBottom() - 6, 1.5f);
        }
        if (info->id == proc.getCurrentInstrumentId() && invTaken[i]) { g.setColour (neon); g.fillEllipse (r.getRight() - 7.0f, r.getY() + 3.0f, 4.0f, 4.0f); }
    }
    if (hoverTile >= 0 && hoverTile < invIds.size())
        if (const auto* info = lib.findById (invIds[hoverTile]))
        {
            g.setColour (chalk);
            g.setFont (ui (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 (info->name.c_str())) + (invTaken[hoverTile] ? "  (click to load)" : "  (lost to the box)"),
                        juce::Rectangle<float> (640.0f, 152.0f, 440.0f, 10.0f).withY ((float) getHeight() - 12.0f), juce::Justification::left, false);
        }
}

//==============================================================================
void SideButtons::IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const bool on = getToggleState();
    g.setColour (juce::Colours::black.withAlpha (down ? 0.6f : 0.45f));
    g.fillRoundedRectangle (r, 8.0f);
    if (on) drawGlow (g, r.getCentre(), r.getWidth() * 0.6f, neon, 0.18f);
    g.setColour (on ? neon : (over ? chalk : metal));
    g.drawRoundedRectangle (r, 8.0f, on ? 2.0f : 1.2f);
    auto icon = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.22f);
    const auto c = on ? neon : chalk;
    g.setColour (c);
    if (iconKind == 0) // gear
    {
        juce::Path gear;
        const auto cc = icon.getCentre();
        const float ro = icon.getWidth() * 0.5f, ri = ro * 0.72f;
        for (int i = 0; i < 16; ++i)
        {
            const float a = (float) i / 16.0f * juce::MathConstants<float>::twoPi;
            const float rr = (i % 2 == 0) ? ro : ri;
            const float a2 = a + juce::MathConstants<float>::twoPi / 16.0f;
            if (i == 0) gear.startNewSubPath (cc.x + std::cos (a) * rr, cc.y + std::sin (a) * rr);
            else gear.lineTo (cc.x + std::cos (a) * rr, cc.y + std::sin (a) * rr);
            gear.lineTo (cc.x + std::cos (a2) * rr, cc.y + std::sin (a2) * rr);
        }
        gear.closeSubPath();
        gear.addEllipse (cc.x - ro * 0.3f, cc.y - ro * 0.3f, ro * 0.6f, ro * 0.6f);
        gear.setUsingNonZeroWinding (false);
        g.fillPath (gear);
    }
    else if (iconKind == 1) // FX: waveform through a knob
    {
        juce::Path wave;
        wave.startNewSubPath (icon.getX(), icon.getCentreY());
        for (int i = 1; i <= 30; ++i) { const float fx = (float) i / 30.0f; wave.lineTo (icon.getX() + fx * icon.getWidth(), icon.getCentreY() + std::sin (fx * 12.0f) * icon.getHeight() * 0.35f * (1.0f - fx * 0.5f)); }
        g.strokePath (wave, juce::PathStrokeType (2.2f));
    }
    else // star
    {
        juce::Path star;
        star.addStar (icon.getCentre(), 5, icon.getWidth() * 0.22f, icon.getWidth() * 0.5f, 0.0f);
        g.fillPath (star);
    }
    g.setFont (stencil (19.0f));
    g.drawText (getName(), r.withTrimmedLeft (6.0f), juce::Justification::centredLeft, false);
}

SideButtons::SideButtons (MysteryBoxAudioProcessor& p) : proc (p)
{
    const char* names[3] = { "SETTINGS", "FX", "FAVES" };
    const char* tips[3] = { "Settings: game, graphics, engine, tuning, reset and about", "FX chain: drag to reorder, 9 effects + Round Ambience",
                            "Favorites & collection tracker (load any starred instrument for free)" };
    for (int i = 0; i < 3; ++i)
    {
        buttons[i] = std::make_unique<IconButton> (names[i], i);
        buttons[i]->setTooltip (tips[i]);
        buttons[i]->setTitle (names[i]);
        buttons[i]->onClick = [this, i] { if (onSelect) onSelect (i); };
        addAndMakeVisible (*buttons[i]);
    }
    startTimerHz (20);
}

void SideButtons::resized()
{
    plate.invalidate();
    for (int i = 0; i < 3; ++i) buttons[i]->setBounds (16, 18 + i * 88, getWidth() - 32, 76);
}

void SideButtons::timerCallback()
{
    meter = juce::jmax (proc.getOutputPeak(), meter * 0.85f);
    repaint (16, 290, getWidth() - 32, 140);
}

void SideButtons::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds();
    plate.draw (g, b, [b] (juce::Graphics& pg) { drawConcrete (pg, b.toFloat(), 33, 0.25f); });
    // Output level + a little status
    auto r = juce::Rectangle<float> (16.0f, 290.0f, (float) getWidth() - 32.0f, 130.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (chalkDim);
    g.setFont (stencil (11.0f));
    g.drawText ("OUTPUT", r.reduced (10.0f, 8.0f).removeFromTop (12.0f), juce::Justification::left, false);
    const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
    auto bar = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 28.0f, r.getWidth() - 20.0f, 10.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f)); g.fillRoundedRectangle (bar, 3.0f);
    g.setColour (db > -1.0f ? danger : neon);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)), 3.0f);
    g.setColour (chalk);
    g.setFont (mono (12.0f));
    g.drawText (juce::String (db, 1) + " dBFS  (limit -0.3)", bar.translated (0, 14).withHeight (14.0f), juce::Justification::left, false);
    const auto& s = proc.getStore().getSettings();
    g.setFont (ui (12.5f));
    g.setColour (chalkDim);
    auto info = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 60.0f, r.getWidth() - 20.0f, 64.0f);
    juce::String txt;
    txt << (s.freeSpin ? "Free Spin mode ON" : "Spin cost: " + juce::String (s.spinCost)) << "\n"
        << (proc.isStandalone() || s.sfxToOutput ? "Game SFX: audible" : "Game SFX: muted in DAW (Settings)") << "\n"
        << (s.midiShooting ? "MIDI shooting: ON" : "MIDI shooting: off");
    g.drawFittedText (txt, info.toNearestInt(), juce::Justification::topLeft, 4);
}

} // namespace mbx
