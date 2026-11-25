// Mystery Box Synthesizer by Raygun
#include "PluginEditor.h"

namespace mbx
{
using namespace theme;

void MysteryBoxAudioProcessorEditor::Overlay::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    if (aura > 0.001f)
    {
        // Glowing cyan aura around the whole UI for an Overclocked instrument
        const float pulse = 0.6f + 0.4f * std::sin (t * 2.2f);
        for (int i = 0; i < 6; ++i)
        {
            g.setColour (wonder.withAlpha (aura * pulse * (0.22f - 0.035f * (float) i)));
            g.drawRoundedRectangle (b.reduced (1.0f + (float) i * 2.5f), 10.0f, 2.5f);
        }
    }
    if (flicker > 0.001f)
    {
        g.setColour (juce::Colours::black.withAlpha (flicker));
        g.fillRect (b);
    }
}

MysteryBoxAudioProcessorEditor::MysteryBoxAudioProcessorEditor (MysteryBoxAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), title (p), game (p), side (p), overclocker (p),
      instrument (p), bottom (p), settings (p), fx (p), favorites (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    setWantsKeyboardFocus (false);
    setTitle ("Mystery Box Synthesizer");

    addAndMakeVisible (root);
    root.addAndMakeVisible (game);
    root.addChildComponent (overclocker);   // z-order: slides out from *behind* the title
    root.addAndMakeVisible (title);
    root.addAndMakeVisible (side);
    root.addAndMakeVisible (instrument);
    root.addAndMakeVisible (bottom);
    root.addChildComponent (settings);
    root.addChildComponent (fx);
    root.addChildComponent (favorites);
    root.addChildComponent (about);
    root.addAndMakeVisible (overlay);

    title.setBounds (0, 0, 240, 440);
    game.setBounds (240, 0, 720, 440);
    side.setBounds (960, 0, 240, 440);
    overclocker.setBounds (0, 0, 300, 440);
    instrument.setBounds (0, 440, 1200, 200);
    bottom.setBounds (0, 640, 1200, 160);
    about.setBounds (0, 0, kDesignW, kDesignH);
    overlay.setBounds (0, 0, kDesignW, kDesignH);

    for (auto* panel : std::initializer_list<SlidePanel*> { &settings, &fx, &favorites })
    {
        panel->setBounds (kDesignW, 440, kDesignW, 360);
        panel->onSlide = [panel] (float pos) { panel->setTopLeftPosition (juce::roundToInt ((1.0f - pos) * (float) kDesignW), 440); };
        panel->onClosed = [this, panel]
        {
            const int idx = panel == &settings ? 0 : panel == &fx ? 1 : 2;
            if (openPanel == idx) { openPanel = -1; side.setActive (-1); }
        };
    }
    side.onSelect = [this] (int i) { showPanel (i); };
    settings.onShowAbout = [this] { about.setVisible (true); about.toFront (true); };
    overclocker.onSlide = [this] (float pos) { overclocker.setTopLeftPosition (juce::roundToInt (pos * 240.0f), 0); };
    overclocker.onUpgraded = [this] { title.triggerFlicker (0.6f); };
    title.onSecretUnlocked = [this] { secretUnlocked(); };
    title.onOpenOverclocker = [this] { if (overclocker.isOut()) overclocker.slideIn(); else overclocker.slideOut(); };

    proc.getStore().addChangeListener (this);
    appliedGpu = proc.getStore().getSettings().gpuRendering;

    setResizable (true, true);
    setResizeLimits (900, 600, 2400, 1600);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kDesignW / (double) kDesignH);
    appliedScale = proc.getStore().getSettings().uiScalePercent;
    setSize (kDesignW * appliedScale / 100, kDesignH * appliedScale / 100);
    startTimerHz (30);
}

MysteryBoxAudioProcessorEditor::~MysteryBoxAudioProcessorEditor()
{
    stopTimer();
    proc.getStore().removeChangeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void MysteryBoxAudioProcessorEditor::paint (juce::Graphics& g) { g.fillAll (bg); }

void MysteryBoxAudioProcessorEditor::resized()
{
    const float s = (float) getWidth() / (float) kDesignW;
    root.setBounds (0, 0, kDesignW, kDesignH);
    root.setTransform (juce::AffineTransform::scale (s));
}

void MysteryBoxAudioProcessorEditor::applyUiScale()
{
    const int pct = proc.getStore().getSettings().uiScalePercent;
    if (pct == appliedScale) return;
    appliedScale = pct;
    setSize (kDesignW * pct / 100, kDesignH * pct / 100);
}

void MysteryBoxAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyUiScale();
    const bool gpu = proc.getStore().getSettings().gpuRendering;
    if (gpu != appliedGpu) { appliedGpu = gpu; game.setGpuRendering (gpu); }
    game.wake();
}

void MysteryBoxAudioProcessorEditor::showPanel (int index)
{
    SlidePanel* panels[3] = { &settings, &fx, &favorites };
    if (openPanel == index) { panels[index]->close(); return; }
    for (int i = 0; i < 3; ++i) if (i != index && panels[i]->isOpen()) panels[i]->close();
    openPanel = index;
    side.setActive (index);
    panels[index]->toFront (false);
    overlay.toFront (false);
    panels[index]->open();
}

void MysteryBoxAudioProcessorEditor::secretUnlocked()
{
    auto& st = proc.getStore();
    const bool first = ! st.getFlag ("egg");
    st.setFlag ("egg", true); // remembered in the user settings, not the project
    shakeTime = 0.9f;
    flickerTime = 1.2f;
    title.triggerFlicker (1.4f);
    proc.playSfx ({ Sfx::EggSting, 60, 0.9f, 0 });
    juce::ignoreUnused (first);
    overclocker.slideOut();
}

void MysteryBoxAudioProcessorEditor::timerCallback()
{
    const float dt = 1.0f / 30.0f;
    overlay.t += dt;
    const float targetAura = proc.isOverclocked() ? 1.0f : 0.0f;
    const float prevAura = overlay.aura, prevFlicker = overlay.flicker;
    overlay.aura += (targetAura - overlay.aura) * 0.1f;
    if (flickerTime > 0.0f)
    {
        flickerTime -= dt;
        overlay.flicker = juce::Random::getSystemRandom().nextFloat() < 0.4f ? 0.55f : 0.0f;
    }
    else overlay.flicker = 0.0f;
    if (shakeTime > 0.0f)
    {
        shakeTime -= dt;
        const float s = (float) getWidth() / (float) kDesignW;
        const float amp = 8.0f * juce::jmax (0.0f, shakeTime);
        auto& r = juce::Random::getSystemRandom();
        root.setTransform (juce::AffineTransform::scale (s).translated ((r.nextFloat() - 0.5f) * amp * s, (r.nextFloat() - 0.5f) * amp * s));
        if (shakeTime <= 0.0f) resized();
    }
    if (overlay.aura > 0.001f || prevAura > 0.001f || overlay.flicker != prevFlicker) overlay.repaint();
}

} // namespace mbx
