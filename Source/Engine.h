#pragma once
// JUCE-free audio engine for Sigma Q. Everything the audio callback does lives here so it can be
// unit/stress tested off-line. No locks, no allocation, no exceptions in processChunk().
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "Dsp.h"

constexpr int kNumBands = 12;

namespace sigmaq
{
constexpr int kSub = 32;   // max samples per processChunk call (= coefficient update interval)
constexpr double kMaxDbStep  = 0.8;    // max motion gain change per update (dB)

// Plain snapshot of one band's parameters (read from the atomics once per block)
struct BandSettings
{
    bool on = false, sync = true;
    int type = 0, slope = 1, place = 0, mode = 0, shape = 0, div = 4;
    float freq = 1000.f, gain = 0.f, q = 1.f, rate = 0.5f, dfreq = 1.f, dgain = 3.f;
};

struct Globals { bool bypass = false, motionOn = true; float outDb = 0.f; };

class Engine
{
public:
    // Motion read-outs for the UI (relaxed atomics)
    std::array<std::atomic<float>, kNumBands>  motOct, motDb;
    std::array<std::atomic<double>, kNumBands> motCycles;

    Engine() { clearReadouts(); }

    void prepare (double sampleRate)
    {
        sr = sampleRate > 1000.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        for (auto& b : bands) b = Band();
        primed = false;
        clearReadouts();
    }

    // Test hook: false disables the motion step limiter (this reproduces the original saw / edge clicks)
    void setMotionLimiter (bool on) { motionLimiter = on; }

    // Process one chunk (n <= kSub) in place. ch[0..nCh) are the channel pointers.
    void processChunk (float** ch, int nCh, int n, const BandSettings* bs, const Globals& g,
                       double ppq, double bpm)
    {
        if (n <= 0 || n > kSub) return;
        nCh = std::max (1, std::min (nCh, 2));

        const float outDb   = std::isfinite (g.outDb) ? std::max (-60.f, std::min (g.outDb, 24.f)) : 0.f;
        const float outGain = std::pow (10.0f, outDb / 20.0f);
        if (! primed)
        {
            outRamp.cur = outRamp.target = outGain;
            bypassRamp.cur = bypassRamp.target = g.bypass ? 0.f : 1.f;
            primed = true;
        }

        float dry[2][kSub];
        for (int c = 0; c < nCh; ++c)
            for (int i = 0; i < n; ++i)
            {
                float x = ch[c][i];
                if (! std::isfinite (x)) { x = 0.0f; ch[c][i] = 0.0f; }   // NaN/Inf input becomes silence for that sample
                dry[c][i] = x;
            }

        // The filters ALWAYS run, even while bypassed: internal state and the motion phase never
        // go stale, so un-bypassing never replays an old tail. Bypass itself is a short crossfade.
        runBands (ch, nCh, n, bs, g.motionOn, ppq, bpm);

        outRamp.retarget (outGain, (int) (sr * 0.030));
        bypassRamp.retarget (g.bypass ? 0.f : 1.f, (int) (sr * 0.015));
        for (int i = 0; i < n; ++i)
        {
            const float m = bypassRamp.next();
            const float o = outRamp.next();
            for (int c = 0; c < nCh; ++c)
                ch[c][i] = dry[c][i] * (1.0f - m) + ch[c][i] * o * m;
        }
    }

private:
    struct Ramp
    {
        float cur = 1.f, target = 1.f, step = 0.f;
        void retarget (float t, int samples)
        {
            if (t == target) return;
            target = t;
            step = (t - cur) / (float) std::max (1, samples);
        }
        float next()
        {
            if (cur != target)
            {
                cur += step;
                if ((step > 0.f && cur >= target) || (step < 0.f && cur <= target)) cur = target;
            }
            return cur;
        }
    };

    struct Band
    {
        bool active = false;
        int place = 0;
        double sFreqLog = 0, sGain = 0, sQ = 1;
        double cycles = 0;
        double sOct = 0, sDb = 0;          // motion offsets, slew-limited
        double lastF = -1, lastG = 1e9, lastQ = -1;
        int lastType = -1, lastSlope = -1;
        dsp_eq::StageSet set;
        dsp_eq::SvfState st[2][4];
    };

    static int clampi (int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static double clampd (double v, double lo, double hi, double fallback)
    {
        return std::isfinite (v) ? std::max (lo, std::min (v, hi)) : fallback;
    }

    void clearReadouts()
    {
        for (int b = 0; b < kNumBands; ++b)
        {
            motOct[(size_t) b].store (0.f); motDb[(size_t) b].store (0.f); motCycles[(size_t) b].store (0.0);
        }
    }

    void runBands (float** ch, int nCh, int n, const BandSettings* bs, bool motionOn, double ppq, double bpm)
    {
        static const double kBeatsPerCycle[8] = { 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };

        const double dt = (double) n / sr;
        const double smoothA = 1.0 - std::exp (-dt / 0.012);

        // never trust host timing values blindly
        if (! (bpm >= 20.0 && bpm <= 999.0)) bpm = 120.0;
        if (! std::isfinite (ppq) || ppq > 1.0e12) ppq = -1.0;

        for (int b = 0; b < kNumBands; ++b)
        {
            const BandSettings& P = bs[b];
            Band& D = bands[(size_t) b];

            if (! P.on)
            {
                D.active = false;
                motOct[(size_t) b].store (0.f); motDb[(size_t) b].store (0.f); motCycles[(size_t) b].store (0.0);
                continue;
            }

            const int type  = clampi (P.type, 0, 6);
            const int slope = clampi (P.slope, 0, 2);
            const int mode  = clampi (P.mode, 0, 2);
            const int shape = clampi (P.shape, 0, 3);
            const int div   = clampi (P.div, 0, 7);
            const double freq  = clampd (P.freq, 20.0, 20000.0, 1000.0);
            const double gain  = clampd (P.gain, -30.0, 30.0, 0.0);
            const double q     = clampd (P.q, 0.1, 18.0, 1.0);
            const double rate  = clampd (P.rate, 0.02, 10.0, 0.5);
            const double dF    = clampd (P.dfreq, 0.0, 3.0, 0.0);
            const double dG    = clampd (P.dgain, 0.0, 18.0, 0.0);
            const double fl    = std::log2 (freq);

            if (! D.active)
            {
                D.active = true;
                D.sFreqLog = fl;  D.sGain = gain;  D.sQ = q;
                D.lastF = -1;
                D.sOct = D.sDb = 0.0;
                for (auto& s : D.st) for (auto& x : s) x = dsp_eq::SvfState();
            }
            else
            {
                D.sFreqLog += smoothA * (fl - D.sFreqLog);
                D.sGain    += smoothA * (gain - D.sGain);
                D.sQ       += smoothA * (q - D.sQ);
            }

            // ---- motion ----
            double octOff = 0.0, dbOff = 0.0;
            if (motionOn && mode > 0)
            {
                if (P.sync)
                {
                    const double bpc = kBeatsPerCycle[div];
                    if (ppq >= 0.0) D.cycles = ppq / bpc;
                    else            D.cycles += (bpm / 60.0) / bpc * dt;
                }
                else
                    D.cycles += rate * dt;

                if (mode == 1)
                {
                    const double w = dsp_eq::lfoShape (shape, D.cycles, b);
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
            if (! std::isfinite (octOff) || ! std::isfinite (dbOff) || ! std::isfinite (D.cycles))
            {
                octOff = dbOff = 0.0;
                D.cycles = 0.0;
            }
            // Frequency motion needs no limiting: the SVF keeps its state continuous and its output
            // coefficients do not depend on frequency, so a frequency jump cannot create a waveform step.
            // Gain motion can (the output coefficient depends on gain), so only gain is step-limited:
            // a saw wrap or any hard edge becomes a short ramp; smooth LFOs are untouched.
            D.sOct = octOff;
            if (motionLimiter)
                D.sDb += std::max (-kMaxDbStep, std::min (kMaxDbStep, dbOff - D.sDb));
            else
                D.sDb = dbOff;
            octOff = D.sOct;
            dbOff  = D.sDb;
            motOct[(size_t) b].store ((float) octOff, std::memory_order_relaxed);
            motDb[(size_t) b].store ((float) dbOff, std::memory_order_relaxed);
            motCycles[(size_t) b].store (motionOn && mode > 0 ? D.cycles : 0.0, std::memory_order_relaxed);

            const double f = std::max (20.0, std::min (20000.0, std::pow (2.0, D.sFreqLog + octOff)));
            const double gdb = std::max (-30.0, std::min (30.0, D.sGain + dbOff));

            if (std::abs (f - D.lastF) > f * 1.0e-6 || std::abs (gdb - D.lastG) > 1.0e-5
                || std::abs (D.sQ - D.lastQ) > 1.0e-6 || type != D.lastType || slope != D.lastSlope)
            {
                D.set = dsp_eq::makeBand (type, sr, f, D.sQ, gdb, slope);
                D.lastF = f; D.lastG = gdb; D.lastQ = D.sQ; D.lastType = type; D.lastSlope = slope;
            }
            D.place = clampi (P.place, 0, 4);
        }

        // ---- filtering ----
        auto run = [] (Band& D, int stIdx, float* x, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                double v = x[i];
                for (int s = 0; s < D.set.n; ++s) v = D.st[stIdx][s].process (D.set.c[s], v);
                // never let a NaN / blow-up poison the filter state
                if (! (std::abs (v) < 1.0e4)) { for (auto& st : D.st[stIdx]) st = dsp_eq::SvfState(); v = 0.0; }
                x[i] = (float) v;
            }
        };

        for (int b = 0; b < kNumBands; ++b)
        {
            Band& D = bands[(size_t) b];
            if (! D.active) continue;

            if (nCh == 1)   // mono: Right-only / Side-only bands have no channel to act on
            {
                if (D.place != 2 && D.place != 4) run (D, 0, ch[0], n);
                continue;
            }

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

    std::array<Band, kNumBands> bands;
    Ramp outRamp, bypassRamp;
    bool primed = false;
    double sr = 44100.0;
    bool motionLimiter = true;
};
} // namespace sigmaq
