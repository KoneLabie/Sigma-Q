#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const double kBeatsPerCycle[8] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout SigmaQProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    using B = juce::AudioParameterBool;
    using C = juce::AudioParameterChoice;
    using R = juce::NormalisableRange<float>;
    using A = juce::AudioParameterFloatAttributes;

    auto dbStr  = [] (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; };
    auto hzStr  = [] (float v, int) { return v >= 1000.f ? juce::String (v / 1000.f, 2) + " kHz"
                                                         : juce::String ((int) std::round (v)) + " Hz"; };
    auto qStr   = [] (float v, int) { return juce::String (v, 2); };
    auto rateStr= [] (float v, int) { return juce::String (v, 2) + " Hz"; };
    auto octStr = [] (float v, int) { return juce::String (v, 2) + " oct"; };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    static const float defFreq[kNumBands] = { 60, 120, 250, 500, 1000, 2000, 3500, 5000, 8000, 12000, 16000, 300 };

    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::String n = "Band " + juce::String (b + 1) + " ";

        R fr (20.f, 20000.f, 0.01f);  fr.setSkewForCentre (1000.f);
        R qr (0.1f, 18.f, 0.001f);    qr.setSkewForCentre (1.0f);
        R rr (0.02f, 10.f, 0.001f);   rr.setSkewForCentre (0.5f);

        layout.add (std::make_unique<B> (juce::ParameterID { pid (b, "on"), 1 }, n + "On", false));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "type"), 1 }, n + "Type", Names::types, 0));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "freq"), 1 }, n + "Frequency", fr, defFreq[b],
                                         A().withStringFromValueFunction (hzStr)));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "gain"), 1 }, n + "Gain", R (-30.f, 30.f, 0.01f), 0.f,
                                         A().withStringFromValueFunction (dbStr)));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "q"), 1 }, n + "Q", qr, 1.0f,
                                         A().withStringFromValueFunction (qStr)));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "slope"), 1 }, n + "Slope", Names::slopes, 1));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "place"), 1 }, n + "Stereo Placement", Names::places, 0));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "mode"), 1 }, n + "Motion Mode", Names::modes, 0));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "shape"), 1 }, n + "Motion Shape", Names::shapes, 0));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "rate"), 1 }, n + "Motion Rate", rr, 0.5f,
                                         A().withStringFromValueFunction (rateStr)));
        layout.add (std::make_unique<B> (juce::ParameterID { pid (b, "sync"), 1 }, n + "Motion Sync", true));
        layout.add (std::make_unique<C> (juce::ParameterID { pid (b, "div"), 1 }, n + "Motion Division", Names::divs, 4));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "dfreq"), 1 }, n + "Motion Sweep", R (0.f, 3.f, 0.01f), 1.0f,
                                         A().withStringFromValueFunction (octStr)));
        layout.add (std::make_unique<F> (juce::ParameterID { pid (b, "dgain"), 1 }, n + "Motion Swell", R (0.f, 18.f, 0.01f), 3.0f,
                                         A().withStringFromValueFunction (dbStr)));
    }

    layout.add (std::make_unique<F> (juce::ParameterID { "out", 1 }, "Output", R (-18.f, 18.f, 0.1f), 0.f,
                                     A().withStringFromValueFunction (dbStr)));
    layout.add (std::make_unique<B> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<B> (juce::ParameterID { "motion", 1 }, "Motion Master", true));
    layout.add (std::make_unique<C> (juce::ParameterID { "analyzer", 1 }, "Analyzer", Names::analyzer, 2));
    layout.add (std::make_unique<C> (juce::ParameterID { "smooth", 1 }, "Analyzer Speed", Names::smooth, 1));
    layout.add (std::make_unique<C> (juce::ParameterID { "tilt", 1 }, "Analyzer Tilt", Names::tilt, 1));
    layout.add (std::make_unique<C> (juce::ParameterID { "range", 1 }, "Display Range", Names::range, 1));
    layout.add (std::make_unique<B> (juce::ParameterID { "trails", 1 }, "Motion Trails", true));
    return layout;
}

SigmaQProcessor::SigmaQProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto get = [this, b] (const char* n) { return apvts.getRawParameterValue (pid (b, n)); };
        bp[(size_t) b] = { get ("on"), get ("type"), get ("freq"), get ("gain"), get ("q"), get ("slope"),
                           get ("place"), get ("mode"), get ("shape"), get ("rate"), get ("sync"),
                           get ("div"), get ("dfreq"), get ("dgain") };
        motOct[(size_t) b].store (0.f);
        motDb[(size_t) b].store (0.f);
        motCycles[(size_t) b].store (0.0);
    }
    pOut      = apvts.getRawParameterValue ("out");
    pBypass   = apvts.getRawParameterValue ("bypass");
    pMotion   = apvts.getRawParameterValue ("motion");
    pAnalyzer = apvts.getRawParameterValue ("analyzer");
    pSmooth   = apvts.getRawParameterValue ("smooth");
    pTilt     = apvts.getRawParameterValue ("tilt");
    pRange    = apvts.getRawParameterValue ("range");
    pTrails   = apvts.getRawParameterValue ("trails");
}

bool SigmaQProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SigmaQProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    outSm.reset (sr, 0.03);
    outSm.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    for (auto& b : bands) b = BandDsp();
    preBuf.fill (0.f);
    postBuf.fill (0.f);
}

//==============================================================================
void SigmaQProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int total = buffer.getNumSamples();
    const int nCh   = juce::jmin (2, getTotalNumInputChannels());
    if (nCh == 0) return;

    for (int c = nCh; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, total);

    double ppq = -1.0, bpm = 120.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p;
        }

    const bool bypass = pBypass->load() > 0.5f;
    outSm.setTargetValue (juce::Decibels::decibelsToGain (pOut->load()));
    int wp = specPos.load (std::memory_order_relaxed);

    for (int start = 0; start < total; start += kSub)
    {
        const int n = juce::jmin (kSub, total - start);
        float* ch[2] = { nullptr, nullptr };
        for (int c = 0; c < nCh; ++c) ch[c] = buffer.getWritePointer (c) + start;

        float pre[kSub];
        for (int i = 0; i < n; ++i)
            pre[i] = nCh == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];

        if (! bypass)
        {
            const double ppqHere = (playing && ppq >= 0.0) ? ppq + (double) start * bpm / (60.0 * sr) : -1.0;
            processSub (ch, nCh, n, ppqHere, bpm);
            for (int i = 0; i < n; ++i)
            {
                const float g = outSm.getNextValue();
                for (int c = 0; c < nCh; ++c) ch[c][i] *= g;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            const float post = nCh == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];
            preBuf[(size_t) wp]  = pre[i];
            postBuf[(size_t) wp] = post;
            wp = (wp + 1) & (fftSize - 1);
        }
    }
    specPos.store (wp, std::memory_order_relaxed);
}

void SigmaQProcessor::processSub (float** ch, int nCh, int n, double ppq, double bpm)
{
    const bool motionOn = pMotion->load() > 0.5f;
    const double dt = (double) n / sr;
    const double smoothA = 1.0 - std::exp (-dt / 0.012);

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& P = bp[(size_t) b];
        auto& D = bands[(size_t) b];

        if (P.on->load() < 0.5f)
        {
            D.active = false;
            motOct[(size_t) b].store (0.f);
            motDb[(size_t) b].store (0.f);
            motCycles[(size_t) b].store (0.0);
            continue;
        }

        const int type  = (int) P.type->load();
        const int slope = (int) P.slope->load();
        const double fl = std::log2 ((double) P.freq->load());

        if (! D.active)
        {
            D.active = true;
            D.sFreqLog = fl;  D.sGain = P.gain->load();  D.sQ = P.q->load();
            D.lastF = -1;
            for (auto& s : D.st) for (auto& x : s) x = dsp_eq::SvfState();
        }
        else
        {
            D.sFreqLog += smoothA * (fl - D.sFreqLog);
            D.sGain    += smoothA * ((double) P.gain->load() - D.sGain);
            D.sQ       += smoothA * ((double) P.q->load() - D.sQ);
        }

        // ---- motion ----
        double octOff = 0.0, dbOff = 0.0;
        const int mode = (int) P.mode->load();
        if (motionOn && mode > 0)
        {
            if (P.sync->load() > 0.5f)
            {
                const double bpc = kBeatsPerCycle[juce::jlimit (0, 7, (int) P.div->load())];
                if (ppq >= 0.0) D.cycles = ppq / bpc;
                else            D.cycles += (bpm / 60.0) / bpc * dt;
            }
            else
                D.cycles += (double) P.rate->load() * dt;

            const double dF = P.dfreq->load(), dG = P.dgain->load();
            if (mode == 1)
            {
                const double w = dsp_eq::lfoShape ((int) P.shape->load(), D.cycles, b);
                octOff = dF * w;
                dbOff  = dG * w;
            }
            else
            {
                const double ph = 2.0 * dsp_eq::kPi * (D.cycles - std::floor (D.cycles));
                octOff = dF * std::cos (ph);
                dbOff  = dG * std::sin (ph);
            }
        }
        motOct[(size_t) b].store ((float) octOff);
        motDb[(size_t) b].store ((float) dbOff);
        motCycles[(size_t) b].store (motionOn && mode > 0 ? D.cycles : 0.0);

        const double f = juce::jlimit (20.0, 20000.0, std::pow (2.0, D.sFreqLog + octOff));
        const double g = juce::jlimit (-30.0, 30.0, D.sGain + dbOff);

        if (std::abs (f - D.lastF) > f * 1.0e-6 || std::abs (g - D.lastG) > 1.0e-5
            || std::abs (D.sQ - D.lastQ) > 1.0e-6 || type != D.lastType || slope != D.lastSlope)
        {
            D.set = dsp_eq::makeBand (type, sr, f, D.sQ, g, slope);
            D.lastF = f; D.lastG = g; D.lastQ = D.sQ; D.lastType = type; D.lastSlope = slope;
        }
        D.place = (int) P.place->load();
    }

    // ---- filtering ----
    auto run = [] (BandDsp& D, int stIdx, float* x, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            double v = x[i];
            for (int s = 0; s < D.set.n; ++s) v = D.st[stIdx][s].process (D.set.c[s], v);
            x[i] = (float) v;
        }
    };

    for (int b = 0; b < kNumBands; ++b)
    {
        auto& D = bands[(size_t) b];
        if (! D.active) continue;

        if (nCh == 1) { run (D, 0, ch[0], n); continue; }

        switch (D.place)
        {
            case 0:  run (D, 0, ch[0], n); run (D, 1, ch[1], n); break;
            case 1:  run (D, 0, ch[0], n); break;
            case 2:  run (D, 1, ch[1], n); break;
            default:
            {
                float m[kSub], s[kSub];
                for (int i = 0; i < n; ++i) { m[i] = 0.5f * (ch[0][i] + ch[1][i]); s[i] = 0.5f * (ch[0][i] - ch[1][i]); }
                if (D.place == 3) run (D, 0, m, n); else run (D, 1, s, n);
                for (int i = 0; i < n; ++i) { ch[0][i] = m[i] + s[i]; ch[1][i] = m[i] - s[i]; }
                break;
            }
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* SigmaQProcessor::createEditor() { return new SigmaQEditor (*this); }

void SigmaQProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SigmaQProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SigmaQProcessor(); }
