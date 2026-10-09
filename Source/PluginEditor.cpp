#include "PluginEditor.h"

SigmaQEditor::SigmaQEditor (SigmaQProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p), advanced (p)
{
    setLookAndFeel (&laf);

    addAndMakeVisible (display);
    addChildComponent (advanced);
    advanced.onReset = [this] { resetBands(); };

    for (auto* b : { &bypassBtn, &motionBtn, &advBtn })
    {
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    bypassBtn.setTooltip ("Hear the original sound with no EQ");
    motionBtn.setTooltip ("Master switch for all moving dots");
    advBtn.setTooltip ("Analyzer, display and reset options");
    advBtn.onClick = [this] { toggleAdvanced(); };

    bypassAtt = std::make_unique<BtnAtt> (p.apvts, "bypass", bypassBtn);
    motionAtt = std::make_unique<BtnAtt> (p.apvts, "motion", motionBtn);

    outSlider.setTooltip ("Output level. Double-click to reset.");
    outSlider.setDoubleClickReturnValue (true, 0.0);
    outAtt = std::make_unique<SliderAtt> (p.apvts, "out", outSlider);
    outSlider.onValueChange = [this]
    {
        outLabel.setText ("Out  " + outSlider.getTextFromValue (outSlider.getValue()), juce::dontSendNotification);
    };
    outLabel.setFont (Theme::font (13.0f));
    outLabel.setColour (juce::Label::textColourId, Theme::muted);
    outLabel.setJustificationType (juce::Justification::centredRight);
    outLabel.setText ("Out  " + outSlider.getTextFromValue (outSlider.getValue()), juce::dontSendNotification);
    addAndMakeVisible (outSlider);
    addAndMakeVisible (outLabel);

    setResizable (true, true);
    setResizeLimits (780, 500, 1700, 1000);
    setSize (1000, 620);
}

SigmaQEditor::~SigmaQEditor()
{
    setLookAndFeel (nullptr);
}

juce::Rectangle<int> SigmaQEditor::advancedBounds (bool shown) const
{
    const int x = shown ? getWidth() - panelW - 24 : getWidth() + 10;
    return { x, topBar + 24, panelW, panelH };
}

void SigmaQEditor::toggleAdvanced()
{
    const bool show = advBtn.getToggleState();
    auto& anim = juce::Desktop::getInstance().getAnimator();
    if (show)
    {
        advanced.setBounds (advancedBounds (false));
        advanced.setVisible (true);
        advanced.toFront (false);
        anim.animateComponent (&advanced, advancedBounds (true), 1.0f, 200, false, 1.0, 1.0);
    }
    else
    {
        anim.animateComponent (&advanced, advancedBounds (false), 1.0f, 180, false, 1.0, 1.0);
        juce::Component::SafePointer<AdvancedPanel> safe (&advanced);
        juce::Timer::callAfterDelay (200, [safe, this]
        {
            if (safe != nullptr && ! advBtn.getToggleState()) safe->setVisible (false);
        });
    }
}

void SigmaQEditor::resetBands()
{
    for (auto* base : proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (base))
            if (rp->paramID.length() > 1 && rp->paramID[0] == 'b' && juce::CharacterFunctions::isDigit (rp->paramID[1]))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
    display.selectBand (-1);
}

void SigmaQEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::bg);

    g.setFont (Theme::font (24.0f, true));
    g.setColour (Theme::text);
    g.drawText ("Sigma", 24, 10, 90, 34, juce::Justification::centredLeft);
    g.setColour (Theme::accent);
    g.drawText ("Q", 24 + 82, 10, 50, 34, juce::Justification::centredLeft);
}

void SigmaQEditor::resized()
{
    auto r = getLocalBounds().reduced (24, 0);
    auto bar = r.removeFromTop (topBar).reduced (0, 12);
    bar.removeFromLeft (150);                                   // logo area

    advBtn.setBounds    (bar.removeFromRight (96));  bar.removeFromRight (8);
    motionBtn.setBounds (bar.removeFromRight (84));  bar.removeFromRight (8);
    bypassBtn.setBounds (bar.removeFromRight (84));  bar.removeFromRight (24);
    outLabel.setBounds  (bar.removeFromRight (96));
    outSlider.setBounds (bar.removeFromRight (130).withTrimmedRight (8));

    display.setBounds (r.withTrimmedBottom (24));

    if (advanced.isVisible() && advBtn.getToggleState())
        advanced.setBounds (advancedBounds (true));
}
