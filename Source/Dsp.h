#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

// Filter maths shared by the audio thread and the UI (so the drawn curve matches what you hear).
// Filters are Cytomic/Simper trapezoidal-integrated state-variable filters: they stay stable
// and click-free while frequency/gain are being modulated, which is what the animation needs.
namespace dsp_eq
{
constexpr double kPi = 3.14159265358979323846;

enum BandType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass };
enum SvfKind  { kBell, kLowShelf, kHighShelf, kLowPass, kHighPass, kNotch, kBandPass };

// Shelf "Q" above this adds a huge resonant peak (+25 dB over the set gain at Q 18), so the effective
// shelf Q is capped. Stored parameter values are untouched, so presets load exactly as before.
constexpr double kShelfQMax = 1.5;

inline bool gainActive (int type) { return type == Bell || type == LowShelf || type == HighShelf; }
inline bool isCut (int type)      { return type == LowCut || type == HighCut; }

struct SvfCoeffs { double g = 1, k = 1, a1 = 0, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0; };

inline SvfCoeffs makeSvf (SvfKind kind, double fs, double f, double q, double gainDb)
{
    SvfCoeffs c;
    f = std::max (5.0, std::min (f, fs * 0.45));
    q = std::max (q, 0.05);
    const double A = std::pow (10.0, gainDb / 40.0);
    double g = std::tan (kPi * f / fs);
    double k = 1.0 / q;

    switch (kind)
    {
        case kBell:      k = 1.0 / (q * A); c.m0 = 1;     c.m1 = k * (A * A - 1.0);   c.m2 = 0;        break;
        case kLowShelf:  g /= std::sqrt (A); c.m0 = 1;     c.m1 = k * (A - 1.0);       c.m2 = A * A - 1.0; break;
        case kHighShelf: g *= std::sqrt (A); c.m0 = A * A; c.m1 = k * (1.0 - A) * A;   c.m2 = 1.0 - A * A; break;
        case kLowPass:   c.m0 = 0; c.m1 = 0;  c.m2 = 1;  break;
        case kHighPass:  c.m0 = 1; c.m1 = -k; c.m2 = -1; break;
        case kNotch:     c.m0 = 1; c.m1 = -k; c.m2 = 0;  break;
        case kBandPass:  c.m0 = 0; c.m1 = k;  c.m2 = 0;  break;   // unity gain at centre
    }
    c.g = g;  c.k = k;
    c.a1 = 1.0 / (1.0 + g * (g + k));
    c.a2 = g * c.a1;
    c.a3 = g * c.a2;
    return c;
}

// One band = 1..4 cascaded stages (cuts use several stages for steeper slopes)
struct StageSet { SvfCoeffs c[4]; int n = 0; };

inline StageSet makeBand (int type, double fs, double f, double q, double gainDb, int slope)
{
    StageSet s;
    f = std::max (20.0, std::min (f, 20000.0));
    gainDb = std::max (-30.0, std::min (gainDb, 30.0));

    switch (type)
    {
        case Bell:      s.c[0] = makeSvf (kBell, fs, f, q, gainDb);      s.n = 1; break;
        case LowShelf:  s.c[0] = makeSvf (kLowShelf, fs, f, std::min (q, kShelfQMax), gainDb);  s.n = 1; break;
        case HighShelf: s.c[0] = makeSvf (kHighShelf, fs, f, std::min (q, kShelfQMax), gainDb); s.n = 1; break;
        case Notch:     s.c[0] = makeSvf (kNotch, fs, f, q, 0);          s.n = 1; break;
        case BandPass:  s.c[0] = makeSvf (kBandPass, fs, f, q, 0);       s.n = 1; break;
        case LowCut:
        case HighCut:
        {
            // Butterworth Q values for 2nd / 4th / 8th order
            static const double q2[1] = { 0.7071067811865476 };
            static const double q4[2] = { 0.5411961001461970, 1.3065629648763766 };
            static const double q8[4] = { 0.5097955791041592, 0.6013448869350453,
                                          0.8999762231364156, 2.5629154477415055 };
            const SvfKind kind = type == LowCut ? kHighPass : kLowPass;
            const double* qs = slope == 0 ? q2 : (slope == 1 ? q4 : q8);
            s.n = slope == 0 ? 1 : (slope == 1 ? 2 : 4);
            for (int i = 0; i < s.n; ++i) s.c[i] = makeSvf (kind, fs, f, qs[i], 0);
            break;
        }
        default: break;
    }
    return s;
}

struct SvfState
{
    double ic1 = 0, ic2 = 0;
    inline double process (const SvfCoeffs& c, double v0)
    {
        const double v3 = v0 - ic2;
        const double v1 = c.a1 * ic1 + c.a2 * v3;
        const double v2 = ic2 + c.a2 * ic1 + c.a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return c.m0 * v0 + c.m1 * v1 + c.m2 * v2;
    }
};

// Magnitude response (dB) of the bilinear-transformed filter at frequency f
inline double stageMagnitudeDb (const SvfCoeffs& c, double fs, double f)
{
    const double w = std::tan (kPi * std::min (f, fs * 0.499) / fs) / c.g;
    const std::complex<double> x (0.0, w);
    const std::complex<double> D = x * x + c.k * x + 1.0;
    const std::complex<double> H = c.m0 + (c.m1 * x + c.m2) / D;
    return 10.0 * std::log10 (std::max (std::norm (H), 1.0e-20));
}

inline double bandMagnitudeDb (const StageSet& s, double fs, double f)
{
    double db = 0.0;
    for (int i = 0; i < s.n; ++i) db += stageMagnitudeDb (s.c[i], fs, f);
    return db;
}

// deterministic noise in -1..1 (same result every playback / bounce)
inline double hashNoise (long long i)
{
    uint32_t x = (uint32_t) (i & 0xffffffffLL) * 747796405u + 2891336453u;
    x = ((x >> ((x >> 28u) + 4u)) ^ x) * 277803737u;
    x = (x >> 22u) ^ x;
    return (double) x / 2147483647.5 - 1.0;
}

// LFO waveform shared by the audio engine and the UI preview
inline double lfoShape (int shape, double cycles, int salt)
{
    const double f = cycles - std::floor (cycles);
    switch (shape)
    {
        case 0:  return std::sin (2.0 * kPi * f);
        case 1:  return f < 0.25 ? 4.0 * f : (f < 0.75 ? 2.0 - 4.0 * f : 4.0 * f - 4.0);
        case 2:  return 2.0 * f - 1.0;
        default:
        {
            const long long idx = (long long) std::floor (cycles) + (long long) salt * 7919;
            const double a = hashNoise (idx), b = hashNoise (idx + 1);
            const double t = f * f * (3.0 - 2.0 * f);
            return a + (b - a) * t;
        }
    }
}
} // namespace dsp_eq
