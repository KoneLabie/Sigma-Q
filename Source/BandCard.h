#pragma once
#include "UI.h"

// Small preview: Tone tab = this band's curve, Motion tab = the live movement pattern
class PreviewStrip : public juce::Component, private juce::Timer
{
public:
    explicit PreviewStrip (SigmaQProcessor& p) : proc (p)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (30);
    }
    void setBand (int b) { band = b; repaint(); }
    void setPage (int p) { page = p; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (Theme::canvas);
        g.fillRoundedRectangle (r, 8.0f);
        if (band < 0) return;
        const auto col = Theme::bandColour (band);
        const auto area = r.reduced (10.0f, 6.0f);
        g.setColour (Theme::line);
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
        if (page == 0) drawTone (g, area, col); else drawMotion (g, area, col);
    }

private:
    void timerCallback() override { if (isVisible()) repaint(); }

    void drawTone (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
    {
        auto& P = proc.bp[(size_t) band];
        const int type = (int) P.type->load();
        const double f = juce::jlimit (20.0, 20000.0, (double) P.freq->load() * std::pow (2.0, (double) proc.motOct[(size_t) band].load()));
        const double gdb = dsp_eq::gainActive (type) ? (double) P.gain->load() + (double) proc.motDb[(size_t) band].load() : 0.0;
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
        const auto set = dsp_eq::makeBand (type, sr, f, P.q->load(), gdb, (int) P.slope->load());

        juce::Path line;
        constexpr int N = 120;
        for (int k = 0; k < N; ++k)
        {
            const float x = area.getX() + area.getWidth() * (float) k / (float) (N - 1);
            const double db = dsp_eq::bandMagnitudeDb (set, sr, 20.0 * std::pow (1000.0, (double) k / (N - 1)));
            const float y = area.getCentreY() - (float) juce::jlimit (-18.0, 18.0, db) / 18.0f * area.getHeight() * 0.5f;
            if (k == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        juce::Path fill (line);
        fill.lineTo (area.getRight(), area.getCentreY());
        fill.lineTo (area.getX(), area.getCentreY());
        fill.closeSubPath();
        g.setColour (col.withAlpha (0.25f));
        g.fillPath (fill);
        g.setColour (col);
        g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
    }

    void drawMotion (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour col)
    {
        auto& P = proc.bp[(size_t) band];
        const int mode = (int) P.mode->load();
        if (mode == 0)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::font (12.0f));
            g.drawText ("No motion - pick LFO or Orbit", area.toNearestInt(), juce::Justification::centred);
            return;
        }

        const float alpha = proc.pMotion->load() > 0.5f ? 1.0f : 0.35f;
        const double cycles = proc.motCycles[(size_t) band].load();
        const double base = std::floor (cycles);
        const double phase = cycles - base;
        const int shape = (int) P.shape->load();
        const float cy = area.getCentreY(), half = area.getHeight() * 0.5f * 0.9f;

        auto valueAt = [&] (int trace, double x)
        {
            if (mode == 1) return dsp_eq::lfoShape (shape, base + x, band);
            return trace == 0 ? std::cos (2.0 * dsp_eq::kPi * x) : std::sin (2.0 * dsp_eq::kPi * x);
        };

        const float px = area.getX() + (float) phase * area.getWidth();
        g.setColour (col.withAlpha (0.25f * alpha));
        g.drawVerticalLine ((int) px, area.getY(), area.getBottom());

        const int traces = mode == 2 ? 2 : 1;
        for (int t = 0; t < traces; ++t)
        {
            juce::Path line;
            constexpr int N = 120;
            for (int k = 0; k < N; ++k)
            {
                const float x = area.getX() + area.getWidth() * (float) k / (float) (N - 1);
                const float y = cy - (float) valueAt (t, (double) k / (N - 1)) * half;
                if (k == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
            }
            const float tAlpha = (t == 0 ? 1.0f : 0.45f) * alpha;
            g.setColour (col.withAlpha (tAlpha));
            g.strokePath (line, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
            const float py = cy - (float) valueAt (t, phase) * half;
            g.setColour (col.withAlpha (alpha));
            g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
        }
    }

    SigmaQProcessor& proc;
    int band = -1, page = 0;
};

//==============================================================================
// Floating card shown next to the selected dot: "Tone" tab and "Motion" tab
class BandCard : public juce::Component
{
public:
    using ComboAtt = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BtnAtt   = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::function<void()> onClose, onRemove;

    explicit BandCard (SigmaQProcessor& p)
        : proc (p),
          preview (p),
          freqKnob ("Frequency", "Where on the spectrum this band acts"),
          gainKnob ("Gain", "Boost or cut at this frequency"),
          qKnob    ("Width (Q)", "Right = narrower and more focused, left = broader and gentler"),
          rateKnob ("Rate", "How fast the dot moves (when Sync is off)"),
          dfKnob   ("Sweep", "How far the dot moves left and right, in octaves"),
          dgKnob   ("Swell", "How far the dot moves up and down, in dB")
    {
        typeBox.addItemList (Names::types, 1);   slopeBox.addItemList (Names::slopes, 1);
        placeBox.addItemList (Names::places, 1); shapeBox.addItemList (Names::shapes, 1);
        divBox.addItemList (Names::divs, 1);

        typeBox.setTooltip ("Shape of this band");
        slopeBox.setTooltip ("How steeply the cut falls off");
        placeBox.setTooltip ("Apply this band to the whole stereo image, one side, or just the middle / sides");
        shapeBox.setTooltip ("The pattern the dot follows");
        divBox.setTooltip ("How long one full movement takes, in musical time");
        syncBtn.setTooltip ("Lock the movement to your project's tempo");

        typeBox.onChange = [this] { refresh(); };

        captionActs.setText ("Acts on", juce::dontSendNotification);
        captionActs.setFont (Theme::font (12.0f));
        captionActs.setColour (juce::Label::textColourId, Theme::muted);

        for (auto* t : { &toneTab, &motionTab })
        {
            t->setClickingTogglesState (true);
            t->setRadioGroupId (2001);
        }
        toneTab.setToggleState (true, juce::dontSendNotification);
        toneTab.onClick   = [this] { page = 0; updatePage(); };
        motionTab.onClick = [this] { page = 1; updatePage(); };

        for (int i = 0; i < 3; ++i)
        {
            modeBtn[i].setButtonText (Names::modes[i]);
            modeBtn[i].onClick = [this, i] { setMode (i); };
        }
        modeBtn[1].setTooltip ("Dot sweeps back and forth on a pattern");
        modeBtn[2].setTooltip ("Dot travels around an oval");

        syncBtn.setClickingTogglesState (true);
        syncBtn.onClick = [this] { refresh(); };
        removeBtn.onClick = [this] { if (onRemove) onRemove(); };
        closeBtn.onClick  = [this] { if (onClose)  onClose(); };

        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &preview, &toneTab, &motionTab, &removeBtn, &closeBtn, &typeBox, &slopeBox, &placeBox, &captionActs,
                 &freqKnob, &gainKnob, &qKnob, &modeBtn[0], &modeBtn[1], &modeBtn[2],
                 &shapeBox, &syncBtn, &divBox, &rateKnob, &dfKnob, &dgKnob })
            addAndMakeVisible (c);

        updatePage();
    }

    void setBand (int b)
    {
        band = b;
        preview.setBand (b);
        auto& A = proc.apvts;
        typeAtt.reset(); slopeAtt.reset(); placeAtt.reset(); shapeAtt.reset(); divAtt.reset(); syncAtt.reset();
        typeAtt  = std::make_unique<ComboAtt> (A, pid (b, "type"),  typeBox);
        slopeAtt = std::make_unique<ComboAtt> (A, pid (b, "slope"), slopeBox);
        placeAtt = std::make_unique<ComboAtt> (A, pid (b, "place"), placeBox);
        shapeAtt = std::make_unique<ComboAtt> (A, pid (b, "shape"), shapeBox);
        divAtt   = std::make_unique<ComboAtt> (A, pid (b, "div"),   divBox);
        syncAtt  = std::make_unique<BtnAtt>   (A, pid (b, "sync"),  syncBtn);
        freqKnob.bind (A, pid (b, "freq"));  gainKnob.bind (A, pid (b, "gain"));  qKnob.bind (A, pid (b, "q"));
        rateKnob.bind (A, pid (b, "rate"));  dfKnob.bind (A, pid (b, "dfreq"));   dgKnob.bind (A, pid (b, "dgain"));
        refresh();
        repaint();
    }

    void refresh()
    {
        if (band < 0) return;
        auto& P = proc.bp[(size_t) band];
        const int type = (int) P.type->load();
        const int mode = (int) P.mode->load();
        const bool sync = P.sync->load() > 0.5f;

        slopeBox.setVisible (page == 0 && dsp_eq::isCut (type));
        gainKnob.setActive (dsp_eq::gainActive (type));
        qKnob.setActive (! dsp_eq::isCut (type));

        for (int i = 0; i < 3; ++i) modeBtn[i].setToggleState (i == mode, juce::dontSendNotification);

        const bool moving = mode > 0;
        shapeBox.setEnabled (mode == 1);
        shapeBox.setAlpha (mode == 1 ? 1.0f : 0.35f);
        syncBtn.setEnabled (moving);   syncBtn.setAlpha (moving ? 1.0f : 0.35f);
        divBox.setVisible (page == 1 && sync);
        divBox.setEnabled (moving);    divBox.setAlpha (moving ? 1.0f : 0.35f);
        rateKnob.setActive (moving && ! sync);
        dfKnob.setActive (moving);
        dgKnob.setActive (moving && dsp_eq::gainActive (type));
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Theme::panel.withAlpha (0.97f));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (Theme::line);
        g.drawRoundedRectangle (r, 14.0f, 1.0f);

        if (band >= 0)
        {
            g.setColour (Theme::bandColour (band));
            g.fillEllipse (16.0f, 19.0f, 10.0f, 10.0f);
            g.setColour (Theme::text);
            g.setFont (Theme::font (14.0f, true));
            g.drawText ("Band " + juce::String (band + 1), 32, 14, 62, 20, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto header = r.removeFromTop (28);
        closeBtn.setBounds (header.removeFromRight (26));
        header.removeFromRight (4);
        removeBtn.setBounds (header.removeFromRight (70));
        header.removeFromLeft (78);
        toneTab.setBounds (header.removeFromLeft (64));
        header.removeFromLeft (6);
        motionTab.setBounds (header.removeFromLeft (74));
        r.removeFromTop (10);
        preview.setBounds (r.removeFromTop (44));
        r.removeFromTop (8);

        auto row1 = r.removeFromTop (28);
        r.removeFromTop (8);
        auto row2 = r.removeFromTop (28);
        r.removeFromTop (6);
        auto knobs = r;

        // tone page
        auto t1 = row1;
        typeBox.setBounds (t1.removeFromLeft (118));
        t1.removeFromLeft (8);
        slopeBox.setBounds (t1.removeFromLeft (104));
        auto t2 = row2;
        captionActs.setBounds (t2.removeFromLeft (60));
        placeBox.setBounds (t2.removeFromLeft (118));

        // motion page
        auto m1 = row1;
        for (auto& b : modeBtn) { b.setBounds (m1.removeFromLeft (84)); m1.removeFromLeft (8); }
        auto m2 = row2;
        shapeBox.setBounds (m2.removeFromLeft (118));
        m2.removeFromLeft (8);
        syncBtn.setBounds (m2.removeFromLeft (64));
        m2.removeFromLeft (8);
        divBox.setBounds (m2.removeFromLeft (90));

        const int kw = knobs.getWidth() / 3;
        auto k = knobs;
        auto a = k.removeFromLeft (kw), b = k.removeFromLeft (kw), c = k;
        freqKnob.setBounds (a); gainKnob.setBounds (b); qKnob.setBounds (c);
        rateKnob.setBounds (a); dfKnob.setBounds (b);   dgKnob.setBounds (c);
    }

private:
    void setMode (int m)
    {
        if (band < 0) return;
        if (auto* p = proc.apvts.getParameter (pid (band, "mode")))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 ((float) m));
            p->endChangeGesture();
        }
        refresh();
    }

    void updatePage()
    {
        const bool tone = page == 0;
        preview.setPage (page);
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &typeBox, &placeBox, &captionActs, &freqKnob, &gainKnob, &qKnob })
            c->setVisible (tone);
        for (juce::Component* c : std::initializer_list<juce::Component*> {
                 &modeBtn[0], &modeBtn[1], &modeBtn[2], &shapeBox, &syncBtn, &rateKnob, &dfKnob, &dgKnob })
            c->setVisible (! tone);
        refresh();
    }

    SigmaQProcessor& proc;
    int band = -1, page = 0;
    PreviewStrip preview;

    juce::TextButton toneTab { "Tone" }, motionTab { "Motion" }, removeBtn { "Remove" }, closeBtn { "X" };
    juce::TextButton modeBtn[3];
    juce::TextButton syncBtn { "Sync" };
    juce::ComboBox typeBox, slopeBox, placeBox, shapeBox, divBox;
    juce::Label captionActs;
    MiniKnob freqKnob, gainKnob, qKnob, rateKnob, dfKnob, dgKnob;

    std::unique_ptr<ComboAtt> typeAtt, slopeAtt, placeAtt, shapeAtt, divAtt;
    std::unique_ptr<BtnAtt> syncAtt;
};
