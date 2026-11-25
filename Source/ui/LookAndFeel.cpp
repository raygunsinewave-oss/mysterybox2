// Mystery Box Synthesizer by Raygun
#include "LookAndFeel.h"
#include "../PluginProcessor.h"

namespace mbx
{
using namespace theme;

MbxLookAndFeel::MbxLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, bg);
    setColour (juce::Label::textColourId, chalk);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::textColourOffId, chalk);
    setColour (juce::TextButton::textColourOnId, neon);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, chalk);
    setColour (juce::ComboBox::arrowColourId, neon);
    setColour (juce::ComboBox::outlineColourId, metal);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff111316));
    setColour (juce::PopupMenu::textColourId, chalk);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, neon.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff101214));
    setColour (juce::TextEditor::textColourId, chalk);
    setColour (juce::TextEditor::highlightColourId, neon.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, metal);
    setColour (juce::TextEditor::focusedOutlineColourId, neon);
    setColour (juce::CaretComponent::caretColourId, neon);
    setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff121417));
    setColour (juce::ScrollBar::thumbColourId, metal);
    setColour (juce::Slider::textBoxTextColourId, chalk);
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1b1e1b));
    setColour (juce::TooltipWindow::textColourId, chalk);
    setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xffd9d4c5));
    setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1b1c1e));
    setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff6b6659));
    setColour (juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, neon.withAlpha (0.25f));
    setColour (juce::MidiKeyboardComponent::keyDownOverlayColourId, neon.withAlpha (0.75f));
    setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.4f));
    setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, panel);
    setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, neon);
    setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff17191c));
    setColour (juce::AlertWindow::textColourId, chalk);
    setColour (juce::AlertWindow::outlineColourId, neon);
}

void MbxLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float angle = start + pos * (end - start);
    const auto accentVar = s.getProperties()["accent"];
    const juce::Colour accent = accentVar.isVoid() ? neon : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    const bool enabled = s.isEnabled();

    // Track
    const float arcR = radius - 2.0f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Glowing value arc
    if (enabled && pos > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, angle, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    // Rusted metal dial body
    const float kr = radius * 0.72f;
    juce::ColourGradient body (metal.brighter (0.25f), c.x - kr * 0.6f, c.y - kr * 0.6f, juce::Colour (0xff1e1f21), c.x + kr, c.y + kr, true);
    g.setGradientFill (body);
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2);
    // Rust freckles, stable per knob
    juce::Random rnd ((juce::int64) (juce::pointer_sized_int) &s);
    for (int i = 0; i < 6; ++i)
    {
        const float a = rnd.nextFloat() * 6.28f, d = rnd.nextFloat() * kr * 0.8f, sr = kr * (0.15f + 0.25f * rnd.nextFloat());
        const float px = c.x + std::cos (a) * d, py = c.y + std::sin (a) * d;
        juce::ColourGradient rg (rust.withAlpha (0.55f), px, py, rust.withAlpha (0.0f), px + sr, py, true);
        g.setGradientFill (rg);
        g.fillEllipse (px - sr, py - sr, sr * 2, sr * 2);
    }
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2, kr * 2, 1.2f);
    // Knurled grip ticks
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    for (int i = 0; i < 24; ++i)
    {
        const float a = (float) i / 24.0f * juce::MathConstants<float>::twoPi;
        g.drawLine (c.x + std::cos (a) * kr * 0.82f, c.y + std::sin (a) * kr * 0.82f, c.x + std::cos (a) * kr * 0.97f, c.y + std::sin (a) * kr * 0.97f, 1.0f);
    }
    // Pointer
    juce::Path ptr;
    ptr.addRoundedRectangle (-1.6f, -kr * 0.92f, 3.2f, kr * 0.55f, 1.5f);
    ptr.applyTransform (juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    g.setColour (enabled ? accent : chalkDim);
    g.fillPath (ptr);
    if (s.hasKeyboardFocus (true))
    {
        g.setColour (accent.withAlpha (0.6f));
        g.drawEllipse (bounds.reduced (0.5f), 1.0f);
    }
}

void MbxLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (neon.withAlpha (s.isEnabled() ? 0.9f : 0.3f));
    g.fillRoundedRectangle (r.withRight (pos), 3.0f);
    g.setColour (metal.brighter (0.3f));
    g.fillEllipse (pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
    g.setColour (juce::Colours::black);
    g.drawEllipse (pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f, 1.0f);
}

void MbxLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    juce::ColourGradient grad (panel.brighter (over ? 0.25f : 0.12f), r.getX(), r.getY(), panel.darker (0.5f), r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (on ? neon : (over ? metal.brighter (0.4f) : metal));
    g.drawRoundedRectangle (r, 4.0f, on ? 1.8f : 1.1f);
    if (down) { g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRoundedRectangle (r, 4.0f); }
    if (b.hasKeyboardFocus (false)) { g.setColour (neon.withAlpha (0.5f)); g.drawRoundedRectangle (r.reduced (2.0f), 3.0f, 1.0f); }
}

void MbxLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 2);
}

juce::Font MbxLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return stencil (juce::jmin (16.0f, (float) h * 0.5f)); }

void MbxLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const auto sw = r.removeFromLeft (34.0f).withSizeKeepingCentre (30.0f, 15.0f);
    const bool on = b.getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (sw, 7.5f);
    g.setColour (on ? neon.withAlpha (0.35f) : metal.withAlpha (0.3f));
    g.fillRoundedRectangle (sw.reduced (1.5f), 6.0f);
    const float kx = on ? sw.getRight() - 13.5f : sw.getX() + 1.5f;
    g.setColour (on ? neon : (over ? chalk : chalkDim));
    g.fillEllipse (kx, sw.getY() + 1.5f, 12.0f, 12.0f);
    if (on) drawGlow (g, { kx + 6.0f, sw.getCentreY() }, 12.0f, neon, 0.35f);
    g.setColour (chalk.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.setFont (ui (juce::jmin (15.0f, r.getHeight() * 0.7f)));
    g.drawFittedText (b.getButtonText(), r.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 2);
    if (b.hasKeyboardFocus (false)) { g.setColour (neon.withAlpha (0.5f)); g.drawRoundedRectangle (sw.expanded (2.0f), 8.0f, 1.0f); }
}

void MbxLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (juce::Colour (0xff111316));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (box.hasKeyboardFocus (true) ? neon : metal);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 5.0f, ay - 3.0f, ax + 5.0f, ay - 3.0f, ax, ay + 4.0f);
    g.setColour (neon);
    g.fillPath (arrow);
}

juce::Font MbxLookAndFeel::getComboBoxFont (juce::ComboBox& b) { return ui (juce::jmin (15.0f, (float) b.getHeight() * 0.6f)); }
void MbxLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}
juce::Font MbxLookAndFeel::getPopupMenuFont() { return ui (16.0f); }
void MbxLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (findColour (juce::PopupMenu::backgroundColourId));
    g.setColour (neon.withAlpha (0.4f));
    g.drawRect (0, 0, w, h, 1);
}
juce::Font MbxLookAndFeel::getLabelFont (juce::Label& l) { return ui (juce::jmin (16.0f, (float) l.getHeight() * 0.75f)); }

void MbxLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    g.setColour (juce::Colour (0xf01b1e1b));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4.0f);
    g.setColour (neon.withAlpha (0.5f));
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 4.0f, 1.0f);
    g.setColour (chalk);
    g.setFont (ui (15.0f));
    g.drawFittedText (text, 8, 4, w - 16, h - 8, juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> MbxLookAndFeel::getTooltipBounds (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parent)
{
    const auto f = ui (15.0f);
    const int w = juce::jmin (340, (int) juce::GlyphArrangement::getStringWidth (f, tip) + 20);
    const int lines = 1 + (int) (juce::GlyphArrangement::getStringWidth (f, tip) / 320.0f);
    const int h = 10 + lines * 19;
    return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 18,
                                 pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 14, w, h)
        .constrainedWithin (parent);
}

void MbxLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int start, int size, bool over, bool)
{
    g.setColour (juce::Colours::black.withAlpha (0.3f));
    g.fillRect (x, y, w, h);
    const auto thumb = vertical ? juce::Rectangle<int> (x + 2, start, w - 4, size) : juce::Rectangle<int> (start, y + 2, size, h - 4);
    g.setColour ((over ? neon : metal).withAlpha (0.8f));
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void MbxLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&)
{
    g.setColour (juce::Colour (0xff101214));
    g.fillRoundedRectangle (0, 0, (float) w, (float) h, 4.0f);
}
void MbxLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& e)
{
    g.setColour (e.hasKeyboardFocus (true) ? neon : metal);
    g.drawRoundedRectangle (0.5f, 0.5f, (float) w - 1, (float) h - 1, 4.0f, 1.0f);
}
void MbxLookAndFeel::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    g.setColour (juce::Colour (0xff15171a));
    g.fillPath (path);
    g.setColour (neon.withAlpha (0.6f));
    g.strokePath (path, juce::PathStrokeType (1.2f));
}

//==============================================================================
Knob::Knob (MysteryBoxAudioProcessor& p, const juce::String& pid, const juce::String& lbl, const juce::String& tip, bool isLarge)
    : proc (p), paramId (pid), label (lbl), large (isLarge)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTooltip (tip + "\n(Right-click: MIDI Learn / reset)");
    slider.setTitle (lbl);
    slider.setDescription (tip);
    slider.setWantsKeyboardFocus (true);
    slider.onRightClick = [this] { showMenu(); };
    slider.onValueChange = [this] { repaint(); };
    slider.onDragStart = [this] { repaint(); };
    slider.onDragEnd = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getAPVTS(), paramId, slider);
    if (auto* param = proc.getAPVTS().getParameter (paramId))
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    setTitle (lbl);
}

Knob::~Knob() { stopTimer(); }

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (large ? 30 : 26);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto text = r.removeFromBottom (large ? 30.0f : 26.0f);
    const bool showValue = slider.isMouseOverOrDragging();
    const auto accentVar = slider.getProperties()["accent"];
    const juce::Colour accent = accentVar.isVoid() ? neon : juce::Colour ((juce::uint32) (juce::int64) accentVar);
    if (learnFlash > 0)
    {
        g.setColour (accent.withAlpha (0.15f + 0.1f * (float) (learnFlash % 2)));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    }
    if (showValue)
    {
        g.setColour (accent);
        g.setFont (mono (large ? 15.0f : 13.0f));
        g.drawText (slider.getTextFromValue (slider.getValue()), text, juce::Justification::centred, false);
    }
    else
    {
        g.setColour (chalk);
        g.setFont (stencil (large ? 12.0f : 11.0f));
        g.setColour (chalk);
        g.drawText (label.toUpperCase(), text, juce::Justification::centred, false);
    }
    const int cc = proc.getMidiMapping (paramId);
    if (cc >= 0)
    {
        g.setColour (accent.withAlpha (0.8f));
        g.setFont (mono (10.0f));
        g.drawText ("CC" + juce::String (cc), getLocalBounds().removeFromTop (12).toFloat(), juce::Justification::centredRight, false);
    }
}

void Knob::showMenu()
{
    juce::PopupMenu m;
    const int cc = proc.getMidiMapping (paramId);
    m.addSectionHeader (label);
    m.addItem (1, "MIDI Learn");
    m.addItem (2, "Clear MIDI mapping" + (cc >= 0 ? " (CC " + juce::String (cc) + ")" : juce::String()), cc >= 0);
    m.addItem (3, "Reset to default");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&slider), [safe = juce::Component::SafePointer<Knob> (this)] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) { safe->proc.beginMidiLearn (safe->paramId); safe->learnFlash = 40; safe->startTimer (100); }
        if (r == 2) { safe->proc.clearMidiMapping (safe->paramId); safe->repaint(); }
        if (r == 3) if (auto* p = safe->proc.getAPVTS().getParameter (safe->paramId)) p->setValueNotifyingHost (p->getDefaultValue());
    });
}

void Knob::timerCallback()
{
    // Blink while waiting for a CC; stop when learned or after ~4 s.
    if (--learnFlash <= 0 || ! proc.isMidiLearning()) { learnFlash = 0; stopTimer(); }
    repaint();
}

} // namespace mbx
