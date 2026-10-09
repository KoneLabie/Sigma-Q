#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    layout.add (std::make_unique<C> (juce::ParameterID { "tilt", 1 }, "Analyzer Tilt", Names::tilt, 0));
    layout.add (std::make_unique<C> (juce::ParameterID { "arange", 1 }, "Analyzer Range", Names::arange, 1));
    layout.add (std::make_unique<C> (juce::ParameterID { "detail", 1 }, "Analyzer Detail", Names::detail, 1));
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
    }
    pOut      = apvts.getRawParameterValue ("out");
    pBypass   = apvts.getRawParameterValue ("bypass");
    pMotion   = apvts.getRawParameterValue ("motion");
    pAnalyzer = apvts.getRawParameterValue ("analyzer");
    pSmooth   = apvts.getRawParameterValue ("smooth");
    pTilt     = apvts.getRawParameterValue ("tilt");
    pRange    = apvts.getRawParameterValue ("range");
    pTrails   = apvts.getRawParameterValue ("trails");
    pARange   = apvts.getRawParameterValue ("arange");
    pDetail   = apvts.getRawParameterValue ("detail");
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
    engine.prepare (sampleRate);
    ring.clear();
}

//==============================================================================
// Audio callback: no locks, no allocation. Parameters are copied from atomics into the
// pre-allocated snapshot, then the JUCE-free engine does the work in <= kSub sample chunks.
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

    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& P = bp[(size_t) b];
        auto& S = snapshot[(size_t) b];
        S.on    = P.on->load() > 0.5f;
        S.sync  = P.sync->load() > 0.5f;
        S.type  = (int) (P.type->load()  + 0.5f);
        S.slope = (int) (P.slope->load() + 0.5f);
        S.place = (int) (P.place->load() + 0.5f);
        S.mode  = (int) (P.mode->load()  + 0.5f);
        S.shape = (int) (P.shape->load() + 0.5f);
        S.div   = (int) (P.div->load()   + 0.5f);
        S.freq = P.freq->load();   S.gain = P.gain->load();   S.q = P.q->load();
        S.rate = P.rate->load();   S.dfreq = P.dfreq->load(); S.dgain = P.dgain->load();
    }

    sigmaq::Globals g;
    g.bypass   = pBypass->load() > 0.5f;
    g.motionOn = pMotion->load() > 0.5f;
    g.outDb    = pOut->load();
    const bool analyze = pAnalyzer->load() > 0.5f;

    for (int start = 0; start < total; start += sigmaq::kSub)
    {
        const int n = juce::jmin (sigmaq::kSub, total - start);
        float* ch[2] = { nullptr, nullptr };
        for (int c = 0; c < nCh; ++c) ch[c] = buffer.getWritePointer (c) + start;

        float pre[sigmaq::kSub];
        for (int i = 0; i < n; ++i)
            pre[i] = nCh == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i];

        const double ppqHere = (playing && ppq >= 0.0) ? ppq + (double) start * bpm / (60.0 * sr) : -1.0;
        engine.processChunk (ch, nCh, n, snapshot.data(), g, ppqHere, bpm);

        if (analyze)
            for (int i = 0; i < n; ++i)
                ring.push (pre[i], nCh == 2 ? 0.5f * (ch[0][i] + ch[1][i]) : ch[0][i]);
    }
    if (analyze) ring.publish();
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
