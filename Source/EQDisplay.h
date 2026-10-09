#pragma once
#include <algorithm>
#include <deque>
#include "BandCard.h"

// The main canvas: grid, spectrum, EQ curve, draggable/animated dots, and the band card
class EQDisplay : public juce::Component, private juce::Timer
{
public:
    explicit EQDisplay (SigmaQProcessor& p) : proc (p), card (p)
    {
        setWantsKeyboardFocus (true);
        curveDb.fill (0.f); preDb.fill (-120.f); postDb.fill (-120.f); fftData.fill (0.f);
        addChildComponent (card);
        card.setSize (360, 278);
        card.onClose  = [this] { selectBand (-1); };
        card.onRemove = [this] { removeBand (selected); };
        startTimerHz (30);
    }

    ~EQDisplay() override { stopTimer(); }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        g.setColour (Theme::canvas);
        g.fillRoundedRectangle (area, 16.0f);

        const auto plot = getPlot();
        drawGrid (g, plot);

        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());

        const int analyzerMode = (int) proc.pAnalyzer->load();
        if (analyzerMode == 2) drawSpectrum (g, preDb, Theme::muted, false);
        if (analyzerMode >= 1) drawSpectrum (g, postDb, Theme::teal, true);

        drawCurve (g, plot);
        drawMotionGuides (g);
        drawTrails (g);
        g.restoreState();

        drawDots (g);

        if (countActive() == 0)
        {
            g.setColour (Theme::muted);
            g.setFont (Theme::font (16.0f));
            g.drawText ("Double-click anywhere to add a band", plot.toNearestInt(), juce::Justification::centred);
        }
    }

    void resized() override { positionCard(); }

    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitTest (e.position);
        if (h != hovered) { hovered = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override { hovered = -1; repaint(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const int h = hitTest (e.position);

        if (e.mods.isPopupMenu())
        {
            if (h >= 0) { selectBand (h); showBandMenu (h); }
            return;
        }

        if (h >= 0)
        {
            selectBand (h);
            dragging = h;
            gesture (pid (h, "freq"), true);
            if (dsp_eq::gainActive ((int) proc.bp[(size_t) h].type->load())) gesture (pid (h, "gain"), true);
        }
        else
            selectBand (-1);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging < 0) return;
        auto& P = proc.bp[(size_t) dragging];
        const double f = xToFreq (e.position.x) / std::pow (2.0, (double) proc.motOct[(size_t) dragging].load());
        setRaw (pid (dragging, "freq"), (float) juce::jlimit (20.0, 20000.0, f));
        if (dsp_eq::gainActive ((int) P.type->load()))
        {
            const double gdb = yToDb (e.position.y) - (double) proc.motDb[(size_t) dragging].load();
            setRaw (pid (dragging, "gain"), (float) juce::jlimit (-30.0, 30.0, gdb));
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging < 0) return;
        gesture (pid (dragging, "freq"), false);
        gesture (pid (dragging, "gain"), false);
        dragging = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (hitTest (e.position) >= 0) return;
        if (! getPlot().contains (e.position)) return;
        addBandAt (e.position);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int b = hovered >= 0 ? hovered : selected;
        if (b < 0) return;
        const int type = (int) proc.bp[(size_t) b].type->load();
        if (dsp_eq::isCut (type)) return;
        const float q = proc.bp[(size_t) b].q->load();
        const float dir = w.isReversed ? -1.0f : 1.0f;
        setP (pid (b, "q"), juce::jlimit (0.1f, 18.0f, q * std::exp (w.deltaY * dir * 0.8f)));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && selected >= 0)
        {
            removeBand (selected);
            return true;
        }
        return false;
    }

    //==========================================================================
    void selectBand (int b)
    {
        selected = b;
        if (b >= 0)
        {
            card.setBand (b);
            updateOrder();                                   // bring this band to the front
            zOrder.erase (std::remove (zOrder.begin(), zOrder.end(), b), zOrder.end());
            zOrder.push_back (b);
        }
        positionCard();
        repaint();
    }

    void removeBand (int b)
    {
        if (b < 0) return;
        setP (pid (b, "mode"), 0.0f);
        setP (pid (b, "on"), 0.0f);
        trails[(size_t) b].clear();
        selectBand (-1);
    }

private:
    static constexpr int numPts = 320, curvePts = 360, kTrail = 28;

    SigmaQProcessor& proc;
    BandCard card;
    int selected = -1, hovered = -1, dragging = -1;
    unsigned tick = 0;
    std::vector<int> zOrder;                       // back -> front
    std::array<std::array<float, 360>, kNumBands> bandCurves {};

    juce::dsp::FFT fft { SigmaQProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) SigmaQProcessor::fftSize,
                                                 juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * SigmaQProcessor::fftSize> fftData;
    std::array<float, numPts> preDb, postDb;
    std::array<float, curvePts> curveDb;
    std::array<std::deque<std::pair<float, float>>, kNumBands> trails;

    //---------------------------------------------------------------- helpers
    juce::Rectangle<float> getPlot() const
    {
        return getLocalBounds().toFloat().withTrimmedLeft (40).withTrimmedRight (46)
                                         .withTrimmedTop (14).withTrimmedBottom (26);
    }
    float rangeDb() const
    {
        static const float r[4] = { 6.f, 12.f, 18.f, 30.f };
        return r[juce::jlimit (0, 3, (int) proc.pRange->load())];
    }
    float analyzerRange() const
    {
        static const float r[3] = { 60.f, 90.f, 120.f };
        return r[juce::jlimit (0, 2, (int) proc.pARange->load())];
    }
    float freqToX (double f) const
    {
        const auto p = getPlot();
        return p.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * p.getWidth();
    }
    double xToFreq (float x) const
    {
        const auto p = getPlot();
        const double t = juce::jlimit (0.0, 1.0, (double) ((x - p.getX()) / p.getWidth()));
        return 20.0 * std::pow (1000.0, t);
    }
    float dbToY (double db) const
    {
        const auto p = getPlot();
        return p.getCentreY() - (float) (db / rangeDb()) * p.getHeight() * 0.5f;
    }
    double yToDb (float y) const
    {
        const auto p = getPlot();
        return (double) (p.getCentreY() - y) / (p.getHeight() * 0.5) * rangeDb();
    }
    float specY (float db) const
    {
        const auto p = getPlot();
        const float ar = analyzerRange();
        return p.getBottom() - juce::jlimit (0.f, 1.f, (db + ar) / ar) * p.getHeight();
    }

    bool isOn (int b) const { return proc.bp[(size_t) b].on->load() > 0.5f; }
    int countActive() const { int n = 0; for (int b = 0; b < kNumBands; ++b) n += isOn (b) ? 1 : 0; return n; }
    double sampleRate() const { const double s = proc.getSampleRate(); return s > 0 ? s : 44100.0; }

    // effective (base + motion) values
    double effFreq (int b) const
    {
        return juce::jlimit (20.0, 20000.0, (double) proc.bp[(size_t) b].freq->load()
                                              * std::pow (2.0, (double) proc.motOct[(size_t) b].load()));
    }
    double effGain (int b) const
    {
        return dsp_eq::gainActive ((int) proc.bp[(size_t) b].type->load())
                   ? juce::jlimit (-30.0, 30.0, (double) proc.bp[(size_t) b].gain->load()
                                                  + (double) proc.motDb[(size_t) b].load())
                   : 0.0;
    }
    juce::Point<float> dotPos (int b) const
    {
        const double R = rangeDb();
        return { freqToX (effFreq (b)), dbToY (juce::jlimit (-R, R, effGain (b))) };
    }
    dsp_eq::StageSet bandSet (int b) const
    {
        auto& P = proc.bp[(size_t) b];
        return dsp_eq::makeBand ((int) P.type->load(), sampleRate(), effFreq (b), P.q->load(),
                                 effGain (b), (int) P.slope->load());
    }

    void setP (const juce::String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
    }
    void setRaw (const juce::String& id, float v)
    {
        if (auto* p = proc.apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    void gesture (const juce::String& id, bool begin)
    {
        if (auto* p = proc.apvts.getParameter (id)) { if (begin) p->beginChangeGesture(); else p->endChangeGesture(); }
    }

    void updateOrder()
    {
        zOrder.erase (std::remove_if (zOrder.begin(), zOrder.end(), [this] (int b) { return ! isOn (b); }), zOrder.end());
        for (int b = 0; b < kNumBands; ++b)
            if (isOn (b) && std::find (zOrder.begin(), zOrder.end(), b) == zOrder.end())
                zOrder.push_back (b);
    }

    // topmost dot under the cursor wins
    int hitTest (juce::Point<float> pos)
    {
        updateOrder();
        for (int i = (int) zOrder.size() - 1; i >= 0; --i)
            if (dotPos (zOrder[(size_t) i]).getDistanceFrom (pos) <= 15.0f) return zOrder[(size_t) i];
        return -1;
    }

    void addBandAt (juce::Point<float> pos)
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            if (isOn (b)) continue;
            setP (pid (b, "type"), 0.0f);
            setP (pid (b, "freq"), (float) juce::jlimit (20.0, 20000.0, xToFreq (pos.x)));
            setP (pid (b, "gain"), (float) juce::jlimit ((double) -rangeDb(), (double) rangeDb(), yToDb (pos.y)));
            setP (pid (b, "q"), 1.0f);
            setP (pid (b, "slope"), 1.0f);
            setP (pid (b, "place"), 0.0f);
            setP (pid (b, "mode"), 0.0f);
            setP (pid (b, "on"), 1.0f);
            selectBand (b);
            return;
        }
    }

    void showBandMenu (int b)
    {
        juce::PopupMenu m;
        const int cur = (int) proc.bp[(size_t) b].type->load();
        for (int i = 0; i < Names::types.size(); ++i) m.addItem (i + 1, Names::types[i], true, i == cur);
        m.addSeparator();
        m.addItem (100, "Delete band");
        juce::Component::SafePointer<EQDisplay> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, b] (int r)
        {
            if (safe == nullptr || r == 0) return;
            if (r == 100) safe->removeBand (b);
            else { safe->setP (pid (b, "type"), (float) (r - 1)); safe->card.refresh(); safe->repaint(); }
        });
    }

    void positionCard()
    {
        if (selected < 0) { card.setVisible (false); return; }
        const auto p = dotPos (selected);
        const int w = card.getWidth(), h = card.getHeight();
        int x = (int) p.x + 30;
        if (x + w > getWidth() - 8) x = (int) p.x - 30 - w;
        x = juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), x);
        const int y = juce::jlimit (8, juce::jmax (8, getHeight() - h - 8), (int) p.y - h / 2);
        card.setTopLeftPosition (x, y);
        card.setVisible (true);
        card.toFront (false);
    }

    //---------------------------------------------------------------- timer
    void timerCallback() override
    {
        updateOrder();
        if ((tick++ & 1u) == 0) updateSpectra();      // spectrum at half rate: same look, half the CPU
        updateCurve();
        updateTrails();
        repaint();
    }

    void updateTrails()
    {
        const bool motionOn = proc.pMotion->load() > 0.5f && proc.pTrails->load() > 0.5f;
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& t = trails[(size_t) b];
            if (motionOn && isOn (b) && proc.bp[(size_t) b].mode->load() > 0.5f)
            {
                t.emplace_back ((float) effFreq (b), (float) effGain (b));
                while ((int) t.size() > kTrail) t.pop_front();
            }
            else
                t.clear();
        }
    }

    void updateCurve()
    {
        std::array<dsp_eq::StageSet, kNumBands> sets;
        std::array<bool, kNumBands> on {};
        for (int b = 0; b < kNumBands; ++b) { on[(size_t) b] = isOn (b); if (on[(size_t) b]) sets[(size_t) b] = bandSet (b); }
        const double sr = sampleRate();
        for (int k = 0; k < curvePts; ++k)
        {
            const double f = 20.0 * std::pow (1000.0, (double) k / (double) (curvePts - 1));
            double total = 0.0;
            for (int b = 0; b < kNumBands; ++b)
                if (on[(size_t) b])
                {
                    const double d = dsp_eq::bandMagnitudeDb (sets[(size_t) b], sr, f);
                    bandCurves[(size_t) b][(size_t) k] = (float) d;
                    total += d;
                }
            curveDb[(size_t) k] = (float) total;
        }
    }

    void updateSpectra()
    {
        const int mode = (int) proc.pAnalyzer->load();
        if (mode == 0 || proc.getSampleRate() <= 0.0) return;
        static const float smoothA[3] = { 0.08f, 0.2f, 0.5f };
        static const float tilts[3]   = { 0.0f, 3.0f, 4.5f };
        const float a = smoothA[juce::jlimit (0, 2, (int) proc.pSmooth->load())];
        const float tilt = tilts[juce::jlimit (0, 2, (int) proc.pTilt->load())];
        computeSpectrum (proc.postBuf, postDb, a, tilt);
        if (mode == 2) computeSpectrum (proc.preBuf, preDb, a, tilt);
    }

    void computeSpectrum (const std::array<std::atomic<float>, SigmaQProcessor::fftSize>& src,
                          std::array<float, numPts>& pts, float a, float tilt)
    {
        constexpr int N = SigmaQProcessor::fftSize;
        const double sr = sampleRate();
        const int wp = proc.specPos.load (std::memory_order_acquire);
        for (int i = 0; i < N; ++i) fftData[(size_t) i] = src[(size_t) ((wp + i) & (N - 1))].load (std::memory_order_relaxed);
        std::fill (fftData.begin() + N, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        std::array<float, numPts> raw;
        for (int k = 0; k < numPts; ++k)
        {
            const double f0 = 20.0 * std::pow (1000.0, (double) k / numPts);
            const double f1 = 20.0 * std::pow (1000.0, (double) (k + 1) / numPts);
            const double b0 = f0 * N / sr, b1 = f1 * N / sr;
            if (f0 >= 0.49 * sr) { raw[(size_t) k] = -120.0f; continue; }   // above Nyquist: no data
            float mag;
            if (b1 - b0 < 1.0)
            {
                const double bc = 0.5 * (b0 + b1);
                const int i0 = juce::jlimit (1, N / 2 - 2, (int) std::floor (bc));
                const float fr = (float) (bc - i0);
                mag = fftData[(size_t) i0] * (1.0f - fr) + fftData[(size_t) i0 + 1] * fr;
            }
            else
            {
                const int i0 = juce::jlimit (1, N / 2 - 2, (int) std::ceil (b0));
                const int i1 = juce::jlimit (i0, N / 2 - 1, (int) std::floor (b1));
                double sum = 0;
                for (int i = i0; i <= i1; ++i) sum += (double) fftData[(size_t) i] * fftData[(size_t) i];
                mag = (float) std::sqrt (sum / (double) (i1 - i0 + 1));
            }
            const float fc = (float) std::sqrt (f0 * f1);
            float db = juce::Decibels::gainToDecibels (mag / (N / 4.0f), -120.0f);
            db += tilt * std::log2 (fc / 1000.0f);
            raw[(size_t) k] = db;
        }

        // fractional-octave smoothing (averaged in power, like most analyzers)
        const int detail = (int) proc.pDetail->load();
        const int half = detail == 0 ? 0 : (detail == 1 ? 3 : 5);
        for (int k = 0; k < numPts; ++k)
        {
            float sm = raw[(size_t) k];
            if (half > 0)
            {
                double sum = 0.0;
                const int j0 = juce::jmax (0, k - half), j1 = juce::jmin (numPts - 1, k + half);
                for (int j = j0; j <= j1; ++j) sum += std::pow (10.0, (double) raw[(size_t) j] * 0.1);
                sm = (float) (10.0 * std::log10 (sum / (double) (j1 - j0 + 1)));
            }
            auto& p = pts[(size_t) k];
            p += a * (sm - p);
        }
    }

    //---------------------------------------------------------------- drawing
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const float R = rangeDb();
        const int step = R <= 6.f ? 3 : (R <= 18.f ? 6 : 10);
        g.setFont (Theme::font (11.0f));
        for (int v = -(int) R; v <= (int) R; v += step)
        {
            const float y = dbToY (v);
            g.setColour (v == 0 ? Theme::line.brighter (0.5f) : Theme::line.withAlpha (0.55f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (Theme::muted);
            g.drawText ((v > 0 ? "+" : "") + juce::String (v), 4, (int) y - 7, 32, 14, juce::Justification::centredRight);
        }
        if ((int) proc.pAnalyzer->load() > 0)
        {
            const float ar = analyzerRange();
            const int stepA = ar <= 60.f ? 10 : 20;
            g.setColour (Theme::teal.withAlpha (0.75f));
            g.setFont (Theme::font (11.0f));
            for (int v = 0; v >= -(int) ar; v -= stepA)
                g.drawText (juce::String (v), (int) plot.getRight() + 6, (int) specY ((float) v) - 7, 34, 14,
                            juce::Justification::centredLeft);
            g.setFont (Theme::font (10.0f, true));
            g.drawText ("dBFS", (int) plot.getRight() + 6, (int) plot.getBottom() + 5, 36, 14, juce::Justification::centredLeft);
        }
        for (double d : { 10.0, 100.0, 1000.0, 10000.0 })
            for (int m = 1; m <= 9; ++m)
            {
                const double f = m * d;
                if (f < 20.0 || f > 20000.0) continue;
                const bool major = m == 1 || m == 2 || m == 5;
                const float x = freqToX (f);
                g.setColour (Theme::line.withAlpha (major ? 0.7f : 0.3f));
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                if (major)
                {
                    g.setColour (Theme::muted);
                    const juce::String t = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                    g.drawText (t, (int) x - 20, (int) plot.getBottom() + 5, 40, 14, juce::Justification::centred);
                }
            }
    }

    void drawSpectrum (juce::Graphics& g, const std::array<float, numPts>& pts, juce::Colour c, bool fill)
    {
        const auto plot = getPlot();
        juce::Path path;
        for (int k = 0; k < numPts; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * ((float) k + 0.5f) / (float) numPts;
            const float y = specY (pts[(size_t) k]);
            if (k == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
        }
        if (fill)
        {
            juce::Path area (path);
            area.lineTo (plot.getRight(), plot.getBottom());
            area.lineTo (plot.getX(), plot.getBottom());
            area.closeSubPath();
            g.setGradientFill (juce::ColourGradient (c.withAlpha (0.38f), 0.f, plot.getY(),
                                                     c.withAlpha (0.02f), 0.f, plot.getBottom(), false));
            g.fillPath (area);
        }
        g.setColour (c.withAlpha (fill ? 0.85f : 0.5f));
        g.strokePath (path, juce::PathStrokeType (fill ? 1.6f : 1.2f));
    }

    juce::Path curvePath (const float* db, bool closeToZero) const
    {
        const auto plot = getPlot();
        const float R = rangeDb();
        juce::Path p;
        for (int k = 0; k < curvePts; ++k)
        {
            const float x = plot.getX() + plot.getWidth() * (float) k / (float) (curvePts - 1);
            const float y = dbToY (juce::jlimit (-R * 1.5f, R * 1.5f, db[k]));
            if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        if (closeToZero)
        {
            p.lineTo (plot.getRight(), dbToY (0)); p.lineTo (plot.getX(), dbToY (0)); p.closeSubPath();
        }
        return p;
    }

    void drawCurve (juce::Graphics& g, juce::Rectangle<float>)
    {
        if (countActive() == 0) return;

        // combined result (neutral white), kept calm so the coloured band shapes read clearly
        const auto total = curvePath (curveDb.data(), false);
        g.setColour (Theme::text.withAlpha (0.10f));
        g.strokePath (total, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved));
        g.setColour (Theme::text.withAlpha (0.85f));
        g.strokePath (total, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

        // every band's own shape in its own colour, back to front (selected band last = on top)
        for (int b : zOrder)
        {
            if (! isOn (b)) continue;
            const bool sel = (b == selected);
            const auto c = Theme::bandColour (b);
            const float* d = bandCurves[(size_t) b].data();

            g.setColour (c.withAlpha (sel ? 0.28f : 0.09f));
            g.fillPath (curvePath (d, true));

            const auto line = curvePath (d, false);
            if (sel)
            {
                g.setColour (c.withAlpha (0.20f));
                g.strokePath (line, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
            }
            g.setColour (c.withAlpha (sel ? 1.0f : 0.55f));
            g.strokePath (line, juce::PathStrokeType (sel ? 2.6f : 1.5f, juce::PathStrokeType::curved));
        }
    }

    void drawMotionGuides (juce::Graphics& g)
    {
        if (proc.pMotion->load() < 0.5f) return;
        const float pxPerOct = getPlot().getWidth() / (float) std::log2 (1000.0);
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& P = proc.bp[(size_t) b];
            const int mode = (int) P.mode->load();
            if (! isOn (b) || mode == 0) continue;
            const int type = (int) P.type->load();
            const float cx = freqToX (P.freq->load());
            const float cy = dbToY (dsp_eq::gainActive (type) ? (double) P.gain->load() : 0.0);
            const float rx = P.dfreq->load() * pxPerOct;
            const float ry = dsp_eq::gainActive (type) ? (float) P.dgain->load() / rangeDb() * getPlot().getHeight() * 0.5f : 0.f;
            g.setColour (Theme::bandColour (b).withAlpha (b == selected ? 0.55f : 0.25f));
            if (mode == 2 && ry > 1.5f && rx > 1.5f)
                g.drawEllipse (cx - rx, cy - ry, rx * 2, ry * 2, 1.4f);
            else
            {
                const juce::Line<float> l (cx - rx, mode == 2 ? cy : cy + ry, cx + rx, mode == 2 ? cy : cy - ry);
                const float dash[2] = { 5.f, 4.f };
                g.drawDashedLine (l, dash, 2, 1.4f);
            }
        }
    }

    void drawTrails (juce::Graphics& g)
    {
        const double R = rangeDb();
        for (int b = 0; b < kNumBands; ++b)
        {
            auto& t = trails[(size_t) b];
            const int n = (int) t.size();
            for (int i = 0; i < n; ++i)
            {
                const float k = (float) (i + 1) / (float) n;
                const float x = freqToX (t[(size_t) i].first);
                const float y = dbToY (juce::jlimit (-R, R, (double) t[(size_t) i].second));
                g.setColour (Theme::bandColour (b).withAlpha (k * 0.45f));
                const float r = 1.5f + 2.5f * k;
                g.fillEllipse (x - r, y - r, r * 2, r * 2);
            }
        }
    }

    void drawDots (juce::Graphics& g)
    {
        for (int b : zOrder)                                   // same back-to-front order as the shapes
        {
            if (! isOn (b)) continue;
            const auto p = dotPos (b);
            const auto c = Theme::bandColour (b);
            const bool hot = b == selected || b == hovered || b == dragging;
            const float r = hot ? 10.0f : 8.5f;

            g.setColour (c.withAlpha (hot ? 0.30f : 0.16f));
            g.fillEllipse (p.x - r - 6, p.y - r - 6, (r + 6) * 2, (r + 6) * 2);
            g.setColour (c);
            g.fillEllipse (p.x - r, p.y - r, r * 2, r * 2);
            g.setColour (Theme::bg.withAlpha (0.85f));
            g.setFont (Theme::font (11.0f, true));
            g.drawText (juce::String (b + 1), (int) (p.x - r), (int) (p.y - r), (int) (r * 2), (int) (r * 2),
                        juce::Justification::centred);

            if (b == hovered || b == dragging)
            {
                const double f = effFreq (b);
                juce::String t = f >= 1000.0 ? juce::String (f / 1000.0, 2) + " kHz" : juce::String ((int) std::round (f)) + " Hz";
                if (dsp_eq::gainActive ((int) proc.bp[(size_t) b].type->load()))
                    t += "   " + juce::String (effGain (b), 1) + " dB";
                juce::GlyphArrangement ga;
                ga.addLineOfText (Theme::font (12.0f, true), t, 0.0f, 0.0f);
                const int w = (int) ga.getBoundingBox (0, -1, true).getWidth() + 18;
                juce::Rectangle<float> tag (p.x - (float) w * 0.5f, p.y - r - 32.0f, (float) w, 22.0f);
                tag = tag.constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
                g.setColour (Theme::panel);
                g.fillRoundedRectangle (tag, 11.0f);
                g.setColour (c.withAlpha (0.8f));
                g.drawRoundedRectangle (tag, 11.0f, 1.2f);
                g.setColour (Theme::text);
                g.setFont (Theme::font (12.0f, true));
                g.drawText (t, tag.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EQDisplay)
};
