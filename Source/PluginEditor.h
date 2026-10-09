#pragma once
#include "EQDisplay.h"

// Slide-in drawer with global settings only (keeps the default view clean)
class AdvancedPanel : public juce::Component
{
public:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BtnAtt   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::function<void()> onReset;

    explicit AdvancedPanel (SigmaQProcessor& p)
    {
        auto setup = [this] (juce::ComboBox& box, const juce::StringArray& items, const juce::String& tip)
        {
            box.addItemList (items, 1);
            box.setTooltip (tip);
            addAndMakeVisible (box);
        };
        setup (analyzerBox, Names::analyzer, "Show the live spectrum behind the EQ curve");
        setup (aRangeBox,   Names::arange,   "How many dB of signal level the spectrum covers (its scale is on the right edge)");
        setup (detailBox,   Names::detail,   "Smooths the spectrum across frequency. Wider = calmer, finer = more detail");
        setup (smoothBox,   Names::smooth,   "How quickly the spectrum reacts over time");
        setup (tiltBox,     Names::tilt,     "Tilts the spectrum so it looks flatter, like how we hear");
        setup (rangeBox,    Names::range,    "How many dB the EQ grid shows above and below the centre line (left scale)");

        trailsBtn.setClickingTogglesState (true);
        trailsBtn.setTooltip ("Show a fading trail behind moving dots");
        resetBtn.setTooltip ("Remove all bands and start fresh");
        resetBtn.onClick = [this] { if (onReset) onReset(); };
        addAndMakeVisible (trailsBtn);
        addAndMakeVisible (resetBtn);

        const char* labels[] = { "Analyzer", "Range", "Detail", "Speed", "Tilt", "EQ range", "Motion trails" };
        for (int i = 0; i < 7; ++i)
        {
            rowLabels[i].setText (labels[i], juce::dontSendNotification);
            rowLabels[i].setFont (Theme::font (13.0f));
            rowLabels[i].setColour (juce::Label::textColourId, Theme::muted);
            addAndMakeVisible (rowLabels[i]);
        }

        a1 = std::make_unique<ComboAtt> (p.apvts, "analyzer", analyzerBox);
        a2 = std::make_unique<ComboAtt> (p.apvts, "arange",   aRangeBox);
        a3 = std::make_unique<ComboAtt> (p.apvts, "detail",   detailBox);
        a4 = std::make_unique<ComboAtt> (p.apvts, "smooth",   smoothBox);
        a5 = std::make_unique<ComboAtt> (p.apvts, "tilt",     tiltBox);
        a6 = std::make_unique<ComboAtt> (p.apvts, "range",    rangeBox);
        a7 = std::make_unique<BtnAtt>   (p.apvts, "trails",   trailsBtn);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Theme::panel.withAlpha (0.98f));
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r, 16.0f, 1.0f);

        g.setColour (Theme::text);
        g.setFont (Theme::font (16.0f, true));
        g.drawText ("Advanced", 20, 14, 200, 24, juce::Justification::centredLeft);

        g.setColour (Theme::teal.withAlpha (0.85f));
        g.setFont (Theme::font (11.0f, true));
        g.drawText ("SPECTRUM (right scale, dBFS)", 20, 50, 240, 14, juce::Justification::centredLeft);
        g.setColour (Theme::muted);
        g.drawText ("EQ GRID (left scale, dB)", 20, 68 + 5 * 36 + 6, 240, 14, juce::Justification::centredLeft);
        g.drawText ("BANDS", 20, 68 + 5 * 36 + 22 + 2 * 36 + 6, 200, 14, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (20, 0);
        r.removeFromTop (68);
        auto row = [&r] { auto x = r.removeFromTop (28); r.removeFromTop (8); return x; };
        auto place = [&] (int i, juce::Component& c)
        {
            auto x = row();
            rowLabels[i].setBounds (x.removeFromLeft (100));
            c.setBounds (x);
        };
        place (0, analyzerBox);
        place (1, aRangeBox);
        place (2, detailBox);
        place (3, smoothBox);
        place (4, tiltBox);
        r.removeFromTop (22);
        place (5, rangeBox);
        place (6, trailsBtn);
        r.removeFromTop (22);
        resetBtn.setBounds (row().removeFromLeft (150));
    }

private:
    juce::ComboBox analyzerBox, aRangeBox, detailBox, smoothBox, tiltBox, rangeBox;
    juce::TextButton trailsBtn { "On" }, resetBtn { "Reset all bands" };
    juce::Label rowLabels[7];
    std::unique_ptr<ComboAtt> a1, a2, a3, a4, a5, a6;
    std::unique_ptr<BtnAtt> a7;
};

//==============================================================================
class SigmaQEditor : public juce::AudioProcessorEditor
{
public:
    explicit SigmaQEditor (SigmaQProcessor&);
    ~SigmaQEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using BtnAtt    = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;

    void toggleAdvanced();
    void resetBands();
    juce::Rectangle<int> advancedBounds (bool shown) const;

    SigmaQProcessor& proc;
    MotionLookAndFeel laf;
    juce::TooltipWindow tooltips { this, 450 };

    EQDisplay display;
    AdvancedPanel advanced;

    juce::TextButton bypassBtn { "Bypass" }, motionBtn { "Motion" }, advBtn { "Advanced" };
    juce::Slider outSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label outLabel;

    std::unique_ptr<BtnAtt> bypassAtt, motionAtt;
    std::unique_ptr<SliderAtt> outAtt;

    static constexpr int topBar = 56, panelW = 290, panelH = 410;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SigmaQEditor)
};
