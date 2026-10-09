#pragma once
#include <JuceHeader.h>
#include <array>
#include "Dsp.h"

constexpr int kNumBands = 12;

namespace Names
{
    inline const juce::StringArray types    { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass" };
    inline const juce::StringArray slopes   { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
    inline const juce::StringArray places   { "Stereo", "Left only", "Right only", "Mid only", "Side only" };
    inline const juce::StringArray modes    { "Off", "LFO", "Orbit" };
    inline const juce::StringArray shapes   { "Sine", "Triangle", "Saw", "Random" };
    inline const juce::StringArray divs     { "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
    inline const juce::StringArray analyzer { "Off", "Post EQ", "Pre + Post" };
    inline const juce::StringArray smooth   { "Slow", "Medium", "Fast" };
    inline const juce::StringArray tilt     { "0 dB/oct", "3 dB/oct", "4.5 dB/oct" };
    inline const juce::StringArray arange   { "60 dB", "90 dB", "120 dB" };
    inline const juce::StringArray detail   { "Fine", "1/6 octave", "1/3 octave" };
    inline const juce::StringArray range    { "+/- 6 dB", "+/- 12 dB", "+/- 18 dB", "+/- 30 dB" };
}

inline juce::String pid (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }

class SigmaQProcessor : public juce::AudioProcessor
{
public:
    SigmaQProcessor();
    ~SigmaQProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sigma Q"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    struct BandParams
    {
        std::atomic<float> *on, *type, *freq, *gain, *q, *slope, *place,
                           *mode, *shape, *rate, *sync, *div, *dfreq, *dgain;
    };
    std::array<BandParams, kNumBands> bp;

    std::atomic<float> *pOut, *pBypass, *pMotion, *pAnalyzer, *pSmooth, *pTilt, *pRange, *pTrails, *pARange, *pDetail;

    // Live motion offsets (octaves / dB) so the UI can draw the moving dots
    std::array<std::atomic<float>, kNumBands> motOct, motDb;
    std::array<std::atomic<double>, kNumBands> motCycles;   // for the motion preview

    // Spectrum analyser ring buffers (visual only)
    static constexpr int fftOrder = 14;
    static constexpr int fftSize  = 1 << fftOrder;
    std::array<float, fftSize> preBuf {}, postBuf {};
    std::atomic<int> specPos { 0 };

private:
    static constexpr int kSub = 32;   // coefficient update interval (samples)

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void processSub (float** ch, int nCh, int n, double ppq, double bpm);

    struct BandDsp
    {
        bool active = false;
        int place = 0;
        double sFreqLog = 0, sGain = 0, sQ = 1;
        double cycles = 0;
        double lastF = -1, lastG = 1e9, lastQ = -1;
        int lastType = -1, lastSlope = -1;
        dsp_eq::StageSet set;
        dsp_eq::SvfState st[2][4];
    };
    std::array<BandDsp, kNumBands> bands;

    juce::SmoothedValue<float> outSm;
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SigmaQProcessor)
};
