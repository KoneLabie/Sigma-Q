#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

namespace Theme
{
    const juce::Colour bg      { 0xff090b10 };
    const juce::Colour canvas  { 0xff0f121a };
    const juce::Colour panel   { 0xff171b25 };
    const juce::Colour panel2  { 0xff202635 };
    const juce::Colour line    { 0xff262d3d };
    const juce::Colour text    { 0xffe9ecf3 };
    const juce::Colour muted   { 0xff7f889b };
    const juce::Colour accent  { 0xff8f84ff };
    const juce::Colour teal    { 0xff3fd0c9 };

    inline juce::Colour bandColour (int b)
    {
        return juce::Colour::fromHSV (std::fmod (0.60f + (float) b * 0.0833f, 1.0f), 0.55f, 0.98f, 1.0f);
    }
    inline juce::Font font (float size, bool bold = false)
    {
        return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    }
}

//==============================================================================
class MotionLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MotionLookAndFeel()
    {
        setColour (juce::TextButton::textColourOffId, Theme::text);
        setColour (juce::TextButton::textColourOnId,  Theme::bg);
        setColour (juce::ComboBox::backgroundColourId, Theme::panel2);
        setColour (juce::ComboBox::outlineColourId,    Theme::line);
        setColour (juce::ComboBox::textColourId,       Theme::text);
        setColour (juce::ComboBox::arrowColourId,      Theme::muted);
        setColour (juce::PopupMenu::backgroundColourId,            Theme::panel);
        setColour (juce::PopupMenu::textColourId,                  Theme::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Theme::accent);
        setColour (juce::PopupMenu::highlightedTextColourId,       Theme::bg);
        setColour (juce::TooltipWindow::backgroundColourId, Theme::panel);
        setColour (juce::TooltipWindow::textColourId,       Theme::text);
        setColour (juce::TooltipWindow::outlineColourId,    Theme::line);
        setColour (juce::Label::textColourId, Theme::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto b = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
        const auto c = b.getCentre();
        const float arcR = radius - 2.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const float thick = 4.0f;

        juce::Path track;
        track.addCentredArc (c.x, c.y, arcR, arcR, 0.f, startAngle, endAngle, true);
        g.setColour (Theme::line);
        g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const double mn = s.getMinimum(), mx = s.getMaximum();
        const float zeroProp = mn < 0.0 ? (float) ((0.0 - mn) / (mx - mn)) : 0.0f;
        const float zeroAngle = startAngle + zeroProp * (endAngle - startAngle);
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0.f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        g.setColour (s.isEnabled() ? Theme::accent : Theme::muted);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float inner = arcR - 8.0f;
        g.setColour (Theme::panel2);
        g.fillEllipse (c.x - inner, c.y - inner, inner * 2.f, inner * 2.f);

        juce::Path p;
        p.addRoundedRectangle (-1.25f, -inner + 3.0f, 2.5f, inner * 0.4f, 1.2f);
        g.setColour (Theme::text);
        g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        juce::Rectangle<float> track ((float) x, cy - 2.0f, (float) w, 4.0f);
        g.setColour (Theme::line);
        g.fillRoundedRectangle (track, 2.0f);

        const float zeroX = (float) x + (float) s.valueToProportionOfLength (0.0) * (float) w;
        g.setColour (Theme::accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zeroX, sliderPos), cy - 2.0f,
                                                        std::abs (sliderPos - zeroX), 4.0f), 2.0f);
        g.setColour (Theme::text);
        g.fillEllipse (sliderPos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float corner = r.getHeight() * 0.5f;
        g.setColour (on ? Theme::accent : Theme::panel2.brighter (down ? 0.2f : (highlighted ? 0.1f : 0.0f)));
        g.fillRoundedRectangle (r, corner);
        if (! on) { g.setColour (Theme::line); g.drawRoundedRectangle (r, corner, 1.0f); }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return Theme::font (13.0f, true); }
};

//==============================================================================
// Small knob with a name and live value, rebindable to a different parameter
class MiniKnob : public juce::Component
{
public:
    explicit MiniKnob (const juce::String& title, const juce::String& tip)
    {
        slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        slider.setMouseDragSensitivity (200);
        slider.setTooltip (tip);
        slider.onValueChange = [this] { updateValue(); };

        name.setText (title, juce::dontSendNotification);
        name.setJustificationType (juce::Justification::centred);
        name.setFont (Theme::font (13.0f, true));
        name.setInterceptsMouseClicks (false, false);
        value.setJustificationType (juce::Justification::centred);
        value.setFont (Theme::font (12.0f));
        value.setColour (juce::Label::textColourId, Theme::muted);
        value.setInterceptsMouseClicks (false, false);

        addAndMakeVisible (slider);
        addAndMakeVisible (name);
        addAndMakeVisible (value);
    }

    void bind (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        attachment.reset();
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, id, slider);
        if (auto* p = apvts.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        updateValue();
    }

    void setActive (bool a) { slider.setEnabled (a); setAlpha (a ? 1.0f : 0.35f); }

    void resized() override
    {
        auto r = getLocalBounds();
        value.setBounds (r.removeFromBottom (16));
        name.setBounds  (r.removeFromBottom (18));
        slider.setBounds (r);
    }

private:
    void updateValue() { value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification); }

    juce::Slider slider;
    juce::Label name, value;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};
