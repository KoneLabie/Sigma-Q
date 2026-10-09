// Sigma Q regression + stress tests. JUCE-free: builds with any C++17 compiler.
//   g++ -std=c++17 -O1 -Wall -Wextra Tests/test_main.cpp -o sigmaq_tests && ./sigmaq_tests
#include "../Source/Engine.h"
#include "../Source/SpectrumRing.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <random>
#include <string>
#include <vector>

// ---------------------------------------------------------------- allocation counter
static std::atomic<long> g_allocs { 0 };
static std::atomic<bool> g_counting { false };
void* operator new (std::size_t n) { if (g_counting) ++g_allocs; void* p = std::malloc (n ? n : 1); if (! p) throw std::bad_alloc(); return p; }
void* operator new[] (std::size_t n) { if (g_counting) ++g_allocs; void* p = std::malloc (n ? n : 1); if (! p) throw std::bad_alloc(); return p; }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

// ---------------------------------------------------------------- mini framework
static int g_fail = 0, g_checks = 0, g_repro = 0, g_reproMiss = 0;
#define CHECK(cond) do { ++g_checks; if (! (cond)) { ++g_fail; std::printf ("      FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define INFO(...) do { std::printf ("      "); std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)
// REPRO: the ORIGINAL defect (re-created locally) should show up. Reported separately from CHECK.
#define REPRO(cond, what) do { if (cond) { ++g_repro; std::printf ("      [reproduced] %s\n", what); } else { ++g_reproMiss; std::printf ("      [NOT reproduced] %s\n", what); } } while (0)

using Settings = std::array<sigmaq::BandSettings, kNumBands>;
using Vec = std::vector<float>;
constexpr double kTwoPi = 6.283185307179586;

struct Rig
{
    sigmaq::Engine eng;
    Settings bs;
    sigmaq::Globals g;
    double sr;
    explicit Rig (double sampleRate) : sr (sampleRate) { eng.prepare (sr); }

    // process in the same way the plugin does: block -> chunks of <= kSub
    void run (Vec* ch, int nCh, size_t total, double bpm = 120.0, double ppq0 = -1.0, size_t block = 32, size_t startSample = 0)
    {
        for (size_t b0 = 0; b0 < total; b0 += block)
        {
            const size_t bn = std::min (block, total - b0);
            for (size_t c0 = 0; c0 < bn; c0 += sigmaq::kSub)
            {
                const size_t s = b0 + c0, n = std::min<size_t> (sigmaq::kSub, bn - c0);
                float* p[2] = { ch[0].data() + s, nCh > 1 ? ch[1].data() + s : nullptr };
                const double ppq = ppq0 >= 0.0 ? ppq0 + (double) (startSample + s) * bpm / (60.0 * sr) : -1.0;
                eng.processChunk (p, nCh, (int) n, bs.data(), g, ppq, bpm);
            }
        }
    }
};

static Vec sine (double sr, double f, double amp, size_t n, double ph0 = 0.0)
{
    Vec v (n);
    for (size_t i = 0; i < n; ++i) v[i] = (float) (amp * std::sin (kTwoPi * f * (double) i / sr + ph0));
    return v;
}
static Vec noise (size_t n, float amp, unsigned seed)
{
    std::mt19937 r (seed); std::uniform_real_distribution<float> d (-amp, amp);
    Vec v (n); for (auto& x : v) x = d (r); return v;
}
static double rms (const float* x, size_t n) { double s = 0; for (size_t i = 0; i < n; ++i) s += (double) x[i] * x[i]; return std::sqrt (s / (double) std::max<size_t> (1, n)); }
static double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1e-12)); }
static double maxAbs (const Vec& v, size_t a = 0, size_t b = (size_t) -1) { double m = 0; b = std::min (b, v.size()); for (size_t i = a; i < b; ++i) m = std::max (m, (double) std::abs (v[i])); return m; }
static double maxJump (const Vec& v, size_t a, size_t b) { double m = 0; for (size_t i = a + 1; i < std::min (b, v.size()); ++i) m = std::max (m, (double) std::abs (v[i] - v[i - 1])); return m; }
static double maxDiffRange (const Vec& a, const Vec& b, size_t from, size_t to) { double m = 0; for (size_t i = from; i < std::min ({ to, a.size(), b.size() }); ++i) m = std::max (m, (double) std::abs (a[i] - b[i])); return m; }
static bool allFinite (const Vec& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }
static void bell (sigmaq::BandSettings& b, float f, float g, float q) { b = sigmaq::BandSettings(); b.on = true; b.type = 0; b.freq = f; b.gain = g; b.q = q; }

// ================================================================= 1. audio response == drawn curve
static void test_response_matches_analytic()
{
    const double srs[] = { 22050, 44100, 48000, 96000, 192000 };
    const char* names[] = { "Bell", "LowShelf", "HighShelf", "LowCut", "HighCut", "Notch", "BandPass" };
    double worst = 0; int points = 0;
    for (double sr : srs)
        for (int type = 0; type < 7; ++type)
        {
            const float gain = dsp_eq::gainActive (type) ? 9.0f : 0.0f;
            const auto set = dsp_eq::makeBand (type, sr, 1000.0, 1.4, gain, 1);
            for (double ft : { 60.0, 250.0, 700.0, 1000.0, 1500.0, 4000.0, sr * 0.3 })
            {
                if (ft > sr * 0.3) continue;
                Rig r (sr);
                bell (r.bs[0], 1000.f, gain, 1.4f); r.bs[0].type = type; r.bs[0].slope = 1;
                const size_t N = (size_t) (sr * 1.2);
                Vec ch[2] = { sine (sr, ft, 0.25, N), {} };
                const Vec in = ch[0];
                r.run (ch, 1, N);
                const size_t t0 = (size_t) (sr * 0.8);
                const double meas = toDb (rms (ch[0].data() + t0, N - t0)) - toDb (rms (in.data() + t0, N - t0));
                const double expct = dsp_eq::bandMagnitudeDb (set, sr, ft);
                if (expct < -50.0) continue;   // below the measurement floor
                const double err = std::abs (meas - expct);
                worst = std::max (worst, err); ++points;
                if (err > 0.15) INFO ("%s sr=%.0f f=%.0f measured %.3f expected %.3f", names[type], sr, ft, meas, expct);
                CHECK (err <= 0.15);
            }
        }
    INFO ("worst |measured - analytic| = %.5f dB over %d measured points (5 sample rates x 7 shapes)", worst, points);
}

// ================================================================= 2. stereo placement semantics
static void test_placement()
{
    const double sr = 48000; const size_t N = 24000;
    auto go = [&] (int place, int nCh, const Vec& L, const Vec& R, Vec& oL, Vec& oR)
    {
        Rig r (sr); bell (r.bs[0], 1000.f, 12.f, 1.f); r.bs[0].place = place;
        Vec ch[2] = { L, R }; r.run (ch, nCh, N); oL = ch[0]; oR = ch[1];
    };
    const Vec L = sine (sr, 1000, 0.3, N), R = sine (sr, 1300, 0.2, N);
    Vec Rn (N); for (size_t i = 0; i < N; ++i) Rn[i] = -L[i];
    auto same = [] (const Vec& a, const Vec& b) { double m = 0; for (size_t i = 0; i < a.size(); ++i) m = std::max (m, (double) std::abs (a[i] - b[i])); return m <= 1e-7; };
    Vec a, b;

    go (1, 2, L, R, a, b);  CHECK (! same (a, L));  CHECK (same (b, R));          // Left only
    go (2, 2, L, R, a, b);  CHECK (same (a, L));    CHECK (! same (b, R));         // Right only
    go (3, 2, L, L, a, b);  CHECK (! same (a, L));  CHECK (same (a, b));           // Mid only on pure mid: changes, stays centred
    go (3, 2, L, Rn, a, b); CHECK (same (a, L));    CHECK (same (b, Rn));          // Mid only leaves pure side untouched
    go (4, 2, L, L, a, b);  CHECK (same (a, L));    CHECK (same (b, L));           // Side only leaves pure mid untouched
    go (4, 2, L, Rn, a, b); CHECK (! same (a, L));                                 // Side only changes pure side
    go (0, 2, L, R, a, b);  CHECK (! same (a, L));  CHECK (! same (b, R));         // Stereo: both

    // mono: Right-only / Side-only have no channel to act on (were wrongly processed before)
    for (int place = 0; place < 5; ++place)
    {
        go (place, 1, L, {}, a, b);
        const bool acts = place == 0 || place == 1 || place == 3;
        CHECK (same (a, L) == ! acts);
    }
}

// ================================================================= 3. bypass: stale state + crossfade
static void test_bypass()
{
    const double sr = 48000;
    const size_t N = (size_t) (sr * 2.0);
    const size_t tByp0 = (size_t) (sr * 1.0), tSilence = (size_t) (sr * 1.1), tByp1 = (size_t) (sr * 1.5);

    auto input = [&] { Vec x = sine (sr, 80, 0.5, N); for (size_t i = tSilence; i < N; ++i) x[i] = 0.f; return x; };
    auto setup = [&] (Rig& r) { bell (r.bs[0], 80.f, 24.f, 12.f); };   // very ringy filter (rings ~0.2 s)

    // Reference: the same signal through the filters with bypass never touched.
    Vec ref = input();
    { Rig r (sr); setup (r); Vec ch[1] = { ref }; r.run (ch, 1, N); ref = ch[0]; }
    const size_t cmpFrom = tByp1 + (size_t) (sr * 0.02);              // crossfade finished by then

    // ---- original behaviour re-created: filters SKIPPED while bypassed, so their state froze ----
    {
        Rig r (sr); setup (r);
        Vec x = input(); Vec ch[1] = { x };
        r.run (ch, 1, tByp0);                                           // processing up to bypass
        for (size_t s = tByp1; s < N; s += sigmaq::kSub)                // (bypassed span: filters not called)
        {
            float* p[2] = { ch[0].data() + s, nullptr };
            r.eng.processChunk (p, 1, (int) std::min<size_t> (sigmaq::kSub, N - s), r.bs.data(), r.g, -1.0, 120.0);
        }
        const double dev = maxDiffRange (ch[0], ref, cmpFrom, N);
        INFO ("original: after un-bypass the output differs from a never-bypassed run by up to %.3f", dev);
        REPRO (dev > 0.5, "frozen filter state replays as a spurious tail after un-bypass");
    }

    // ---- fixed engine ----
    {
        Rig r (sr); setup (r);
        Vec x = input(); const Vec in = x; Vec ch[1] = { x };
        r.run (ch, 1, tByp0);
        for (size_t s = tByp0; s < N; s += sigmaq::kSub)
        {
            r.g.bypass = s < tByp1;
            float* p[2] = { ch[0].data() + s, nullptr };
            r.eng.processChunk (p, 1, (int) std::min<size_t> (sigmaq::kSub, N - s), r.bs.data(), r.g, -1.0, 120.0);
        }
        const Vec& y = ch[0];
        const double dev = maxDiffRange (y, ref, cmpFrom, N);
        INFO ("fixed:    after un-bypass the output differs from a never-bypassed run by up to %.2e", dev);
        CHECK (dev < 1e-5);
        bool exact = true;                                              // fully bypassed => bit-exact passthrough
        for (size_t i = tByp0 + (size_t) (sr * 0.03); i < tByp1; ++i) if (y[i] != in[i]) { exact = false; break; }
        CHECK (exact);
        CHECK (allFinite (y));
    }

    // ---- no click when toggling (crossfade vs hard switch) ----
    {
        Rig r (sr); bell (r.bs[0], 1000.f, 12.f, 1.f);
        const size_t M = (size_t) (sr * 0.8);
        Vec wet = sine (sr, 1000, 0.25, M, kTwoPi / 4.0); const Vec dry = wet;   // 90 deg phase: toggles land on a waveform PEAK
        { Vec ch[1] = { wet }; r.run (ch, 1, M); wet = ch[0]; }
        const double steady = maxJump (wet, M - 4800, M);
        double hard = 0;                                                        // original: instant switch
        for (size_t k = M - 4800; k < M - 4800 + 48; ++k) hard = std::max (hard, (double) std::abs (dry[k] - wet[k - 1]));

        Rig r2 (sr); bell (r2.bs[0], 1000.f, 12.f, 1.f);
        Vec x = dry; Vec ch[1] = { x };
        r2.g.bypass = false; r2.run (ch, 1, M - 4800);
        for (size_t s = M - 4800; s < M; s += sigmaq::kSub)
        {
            r2.g.bypass = s < M - 2400;                 // bypass ON for the first half, back OFF for the second
            const size_t n = std::min<size_t> (sigmaq::kSub, M - s);
            float* p[2] = { ch[0].data() + s, nullptr };
            r2.eng.processChunk (p, 1, (int) n, r2.bs.data(), r2.g, -1.0, 120.0);
        }
        const double fade = maxJump (ch[0], M - 4801, M);   // window starts one sample early: the jump is wet[k-1] -> dry[k]
        INFO ("steady-state max step %.4f | hard switch jump %.4f | crossfade max step %.4f", steady, hard, fade);
        REPRO (hard > 3.0 * steady, "instant bypass switch produces a click");   // (both directions measured below)
        CHECK (fade <= 1.2 * steady);
    }
}

// ================================================================= 4. motion clicks (saw wrap)
static void test_motion_clicks()
{
    const double sr = 48000; const size_t N = (size_t) (sr * 2.0);
    auto measure = [&] (double slewMs, int shape, int mode)
    {
        Rig r (sr); r.eng.setMotionLimiter (slewMs > 0.0);
        bell (r.bs[0], 1000.f, 0.f, 2.f);
        auto& b = r.bs[0]; b.mode = mode; b.shape = shape; b.sync = false; b.rate = 3.f; b.dfreq = 0.f; b.dgain = 18.f;
        Vec ch[1] = { sine (sr, 1000, 0.1, N) };
        r.run (ch, 1, N);
        const size_t a = (size_t) (sr * 0.3);
        const double steadySlope = maxAbs (ch[0], a, N) * kTwoPi * 1000.0 / sr;
        return maxJump (ch[0], a, N) / steadySlope;
    };
    const double legacy = measure (0.0, 2, 1), fixedSaw = measure (4.0, 2, 1);
    INFO ("saw LFO, max step / steady slope: original %.2f -> fixed %.2f", legacy, fixedSaw);
    REPRO (legacy > 2.0, "hard saw wrap causes a discontinuity (click)");
    CHECK (fixedSaw <= 1.3);

    // other shapes / orbit must also stay smooth
    for (int shape = 0; shape < 4; ++shape) { const double v = measure (4.0, shape, 1); CHECK (v <= 1.3); INFO ("shape %d: %.2f", shape, v); }
    const double orb = measure (4.0, 0, 2); CHECK (orb <= 1.3); INFO ("orbit: %.2f", orb);

    // frequency wrap: a narrow boost leaps across a steady tone at every saw wrap. Investigated: this can NOT
    // click (SVF state is continuous and output coefficients are frequency-independent), so it is a guard only.
    for (double q : { 2.0, 18.0 })
    {
        auto sweep = [&] (bool limiter)
        {
            Rig r (sr); r.eng.setMotionLimiter (limiter);
            bell (r.bs[0], 1000.f, 24.f, (float) q);
            auto& b = r.bs[0]; b.mode = 1; b.shape = 2; b.sync = false; b.rate = 2.f; b.dfreq = 1.0f; b.dgain = 0.f;
            Vec ch[1] = { sine (sr, 1990, 0.1, N) };   // tone sits where the saw wraps (2 kHz -> 500 Hz)
            r.run (ch, 1, N);
            const size_t a = (size_t) (sr * 0.3);
            return maxJump (ch[0], a, N) / (maxAbs (ch[0], a, N) * kTwoPi * 1990.0 / sr);
        };
        const double sw = sweep (true), swOld = sweep (false);
        INFO ("saw frequency wrap, Q %.0f: max step / steady slope = %.2f (original engine %.2f: no defect here)", q, sw, swOld);
        CHECK (sw <= 1.5);
    }
}

// ================================================================= 5. non-finite + extreme inputs
static void test_nonfinite_and_extremes()
{
    const double sr = 48000;
    const double bad[] = { NAN, INFINITY, -INFINITY, -5.0, 0.0, -120.0, 1e300, 1e13, 1e9 };
    int combos = 0;
    for (double ppq : bad)
        for (double bpm : bad)
        {
            Rig r (sr); bell (r.bs[0], 800.f, 12.f, 3.f);
            r.bs[0].mode = 2; r.bs[0].sync = true; r.bs[0].dfreq = 2.f; r.bs[0].dgain = 12.f;
            bell (r.bs[1], 3000.f, 6.f, 1.f); r.bs[1].mode = 1; r.bs[1].shape = 3; r.bs[1].sync = true;
            const size_t N = (size_t) (sr * 0.3);
            Vec ch[1] = { noise (N, 0.5f, 7) };
            for (size_t s = 0; s < N; s += sigmaq::kSub)
            {
                float* p[2] = { ch[0].data() + s, nullptr };
                r.eng.processChunk (p, 1, (int) std::min<size_t> (sigmaq::kSub, N - s), r.bs.data(), r.g, ppq, bpm);
            }
            CHECK (allFinite (ch[0]) && maxAbs (ch[0]) < 1e3);
            ++combos;
        }
    INFO ("%d bad host ppq/bpm combinations: output always finite and bounded", combos);

    // NaN / Inf in the audio input must not poison the filters
    {
        const size_t N = (size_t) (sr * 1.0);
        Rig clean (sr), dirty (sr);
        for (auto* r : { &clean, &dirty }) bell (r->bs[0], 1000.f, 12.f, 2.f);
        Vec a[1] = { sine (sr, 700, 0.3, N) }, b[1] = { a[0] };
        b[0][24000] = NAN; b[0][24100] = INFINITY; b[0][24200] = -INFINITY;
        clean.run (a, 1, N); dirty.run (b, 1, N);
        CHECK (allFinite (b[0]));
        double dev = 0; for (size_t i = 26000; i < N; ++i) dev = std::max (dev, (double) std::abs (a[0][i] - b[0][i]));
        INFO ("after NaN/Inf injection: max deviation from clean run after 40 ms = %.2e", dev);
        CHECK (dev < 1e-3);
    }

    // fuzz: random / out-of-range / NaN settings at many sample rates
    {
        std::mt19937 rng (1234);
        auto U = [&] (double lo, double hi) { return std::uniform_real_distribution<double> (lo, hi) (rng); };
        auto wild = [&] (double lo, double hi) { switch (rng() % 12) { case 0: return (double) NAN; case 1: return (double) INFINITY; case 2: return -1e30; case 3: return 1e30; default: return U (lo, hi); } };
        const double srs[] = { 22050, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
        int ok = 0; const int ITER = 600;
        for (int it = 0; it < ITER; ++it)
        {
            const double sr2 = srs[rng() % 8];
            Rig r (sr2);
            for (auto& b : r.bs)
            {
                b.on = rng() % 3 != 0; b.sync = rng() & 1;
                b.type = (int) (rng() % 12) - 3; b.slope = (int) (rng() % 7) - 2; b.place = (int) (rng() % 9) - 2;
                b.mode = (int) (rng() % 5) - 1; b.shape = (int) (rng() % 7) - 1; b.div = (int) (rng() % 12) - 2;
                b.freq = (float) wild (-100, 30000); b.gain = (float) wild (-80, 80); b.q = (float) wild (-2, 40);
                b.rate = (float) wild (-5, 50); b.dfreq = (float) wild (-3, 9); b.dgain = (float) wild (-9, 60);
            }
            r.g.bypass = rng() & 1; r.g.motionOn = rng() & 1; r.g.outDb = (float) wild (-100, 100);
            const int nCh = 1 + (int) (rng() % 2);
            const size_t N = (size_t) (sr2 * 0.15);
            Vec ch[2] = { noise (N, 0.5f, (unsigned) it), noise (N, 0.5f, (unsigned) it + 99) };
            r.run (ch, nCh, N, 120.0, (rng() & 1) ? 3.7 : -1.0);
            bool fine = allFinite (ch[0]) && (nCh == 1 || allFinite (ch[1]));
            if (fine) ++ok; else INFO ("fuzz iteration %d produced non-finite output", it);
            CHECK (fine);
        }
        INFO ("fuzz: %d / %d random configurations stayed finite", ok, ITER);
    }
}

// ================================================================= 6. level survey (valid settings only)
static void test_gain_survey()
{
    const double srs[] = { 22050, 44100, 48000, 96000, 192000 };
    const char* names[] = { "Bell", "LowShelf", "HighShelf", "LowCut", "HighCut", "Notch", "BandPass" };
    double globalWorst = -999;
    for (double sr : srs)
    {
        double worstSr = -999; int worstType = 0; double worstQ = 0;
        for (int type = 0; type < 7; ++type)
            for (double q : { 0.1, 0.71, 1.0, 4.0, 18.0 })
                for (double f : { 20.0, 100.0, 1000.0, 8000.0, 20000.0 })
                    for (double g : { -30.0, 30.0 })
                    {
                        const auto set = dsp_eq::makeBand (type, sr, f, q, g, 2);
                        double peak = -999;
                        for (int k = 0; k < 400; ++k)
                            peak = std::max (peak, dsp_eq::bandMagnitudeDb (set, sr, 20.0 * std::pow (sr * 0.49 / 20.0, k / 399.0)));
                        if (peak > worstSr) { worstSr = peak; worstType = type; worstQ = q; }
                    }
        INFO ("sr %.0f: worst single-band peak = %+.1f dB (%s, Q %.2f)", sr, worstSr, names[worstType], worstQ);
        globalWorst = std::max (globalWorst, worstSr);
    }
    INFO ("worst single-band peak across all rates = %+.1f dB (nominal max boost is +30 dB)", globalWorst);
    CHECK (globalWorst <= 34.5);   // nominal +30 dB plus <= ~4 dB shelf resonance (shelf Q is capped)
}

// ================================================================= 7. block-size invariance
static void test_block_size_invariance()
{
    const double sr = 48000; const size_t N = (size_t) (sr * 1.5);
    auto render = [&] (size_t block)
    {
        Rig r (sr);
        bell (r.bs[0], 500.f, 6.f, 1.f); r.bs[0].mode = 1; r.bs[0].shape = 0; r.bs[0].sync = true; r.bs[0].div = 2;
        bell (r.bs[1], 120.f, 0.f, 1.f); r.bs[1].type = 3; r.bs[1].slope = 1;
        bell (r.bs[2], 4000.f, 3.f, 0.7f); r.bs[2].type = 2; r.bs[2].mode = 2; r.bs[2].sync = true; r.bs[2].div = 3;
        Vec ch[2] = { noise (N, 0.2f, 5), noise (N, 0.2f, 6) };
        r.run (ch, 2, N, 128.0, 8.0, block);
        return ch[0];
    };
    const Vec ref = render (32);
    double worst = -999;
    for (size_t blk : { (size_t) 1, (size_t) 7, (size_t) 64, (size_t) 480, (size_t) 1024 })
    {
        const Vec y = render (blk);
        double d = 0; for (size_t i = 0; i < N; ++i) { const double e = y[i] - ref[i]; d += e * e; }
        const double rel = toDb (std::sqrt (d / (double) N) / rms (ref.data(), N));
        INFO ("host block %4zu vs 32: deviation %.1f dB", blk, rel);
        worst = std::max (worst, rel);
    }
    CHECK (worst < -35.0);
}

// ================================================================= 8. real-time safety
static void test_no_allocation()
{
    const double sr = 48000; Rig r (sr);
    for (int b = 0; b < kNumBands; ++b)
    {
        bell (r.bs[(size_t) b], 100.f * (float) (b + 1), 6.f, 1.f);
        r.bs[(size_t) b].type = b % 7; r.bs[(size_t) b].mode = 1 + b % 2; r.bs[(size_t) b].shape = b % 4; r.bs[(size_t) b].place = b % 5;
    }
    Vec ch[2] = { noise (sigmaq::kSub, 0.3f, 1), noise (sigmaq::kSub, 0.3f, 2) };
    r.run (ch, 2, sigmaq::kSub);   // warm up
    SpectrumRing ring;
    g_allocs = 0; g_counting = true;
    for (int i = 0; i < 20000; ++i)
    {
        r.g.bypass = (i / 3000) & 1;
        float* p[2] = { ch[0].data(), ch[1].data() };
        r.eng.processChunk (p, 2, sigmaq::kSub, r.bs.data(), r.g, (double) i * 0.01, 120.0);
        for (int k = 0; k < sigmaq::kSub; ++k) ring.push (ch[0][(size_t) k], ch[1][(size_t) k]);
        ring.publish();
    }
    g_counting = false;
    INFO ("heap allocations during 20000 audio chunks (engine + ring): %ld", g_allocs.load());
    CHECK (g_allocs.load() == 0);

    // timing sanity (not a guarantee): 12 bands, motion on
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000; ++i)
    {
        float* p[2] = { ch[0].data(), ch[1].data() };
        r.eng.processChunk (p, 2, sigmaq::kSub, r.bs.data(), r.g, -1.0, 120.0);
    }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    INFO ("12 bands + motion: %.1fx real-time on this machine (unoptimised -O1 build)", (20000.0 * sigmaq::kSub / sr) / sec);
}

// ================================================================= 9. spectrum ring semantics
static void test_ring()
{
    SpectrumRing ring;
    for (int i = 0; i < 20000; ++i) ring.push ((float) i, (float) -i);
    ring.publish();
    const int p = ring.readPos();
    CHECK (p == 20000 % SpectrumRing::size);
    // newest sample sits just before the read position
    CHECK (ring.pre[(size_t) ((p - 1 + SpectrumRing::size) % SpectrumRing::size)].load() == 19999.f);
    CHECK (ring.post[(size_t) ((p - 1 + SpectrumRing::size) % SpectrumRing::size)].load() == -19999.f);
}

// ================================================================= 10. parameter round-trip (pluginval hypothesis)
// Models JUCE's NormalisableRange<float> maths in float to check whether "randomise -> save -> restore"
// can drift purely through range snapping / text conversion. (Serialization itself is unchanged.)
struct Range
{
    float start, end, interval, skew = 1.f;
    void skewForCentre (float c) { skew = std::log (0.5f) / std::log ((c - start) / (end - start)); }
    float to01 (float v) const { float p = std::max (0.f, std::min (1.f, (v - start) / (end - start))); return skew == 1.f ? p : std::pow (p, skew); }
    float from01 (float p) const { p = std::max (0.f, std::min (1.f, p)); if (skew != 1.f && p > 0.f) p = std::exp (std::log (p) / skew); return start + (end - start) * p; }
    float snap (float v) const { if (interval > 0) v = start + interval * std::floor ((v - start) / interval + 0.5f); return (v <= start || end <= start) ? start : (v >= end ? end : v); }
    float set (float norm) const { return snap (from01 (norm)); }   // setValueNotifyingHost(norm) -> stored real value
};
static void test_param_roundtrip()
{
    struct P { const char* name; Range r; float centre; } ps[] = {
        { "freq",  { 20.f, 20000.f, 0.01f }, 1000.f }, { "gain",  { -30.f, 30.f, 0.01f }, 0.f },
        { "q",     { 0.1f, 18.f, 0.001f },  1.0f   }, { "rate",  { 0.02f, 10.f, 0.001f }, 0.5f },
        { "dfreq", { 0.f, 3.f, 0.01f },     0.f    }, { "dgain", { 0.f, 18.f, 0.01f },    0.f  },
        { "out",   { -18.f, 18.f, 0.1f },   0.f    } };
    std::mt19937 rng (99); std::uniform_real_distribution<float> U (0.f, 1.f);
    for (auto& p : ps)
    {
        if (p.centre > 0.f) p.r.skewForCentre (p.centre);
        long drift = 0, textLoss = 0; double worst = 0, worstN = 0;
        for (int i = 0; i < 2000000; ++i)
        {
            const float v1 = p.r.set (U (rng));                     // value after "randomise"
            char buf[64]; std::snprintf (buf, sizeof buf, "%.15g", (double) v1);   // saved as text in the XML state
            const float parsed = (float) std::strtod (buf, nullptr);
            if (parsed != v1) ++textLoss;
            const float v2 = p.r.set (p.r.to01 (parsed));           // value after "restore"
            if (v2 != v1) { ++drift; worst = std::max (worst, (double) std::abs (v2 - v1)); worstN = std::max (worstN, (double) std::abs (p.r.to01 (v2) - p.r.to01 (v1))); }
        }
        INFO ("%-5s: %ld / 2e6 values changed on restore (worst %.2e, normalised %.2e), %ld text round-trip losses", p.name, drift, worst, worstN, textLoss);
        CHECK (textLoss == 0);
        CHECK (worstN < 1e-6);
        if (drift) INFO ("      ^ restore is NOT exactly idempotent for '%s' (float precision of the skewed range)", p.name);
    }
}

// ================================================================= runner
int main()
{
    struct T { const char* name; void (*fn)(); } tests[] = {
        { "response matches drawn curve (5 rates x 7 shapes)", test_response_matches_analytic },
        { "stereo / mono placement semantics",                  test_placement },
        { "bypass: stale state + click-free crossfade",         test_bypass },
        { "motion: saw wrap / edge clicks",                      test_motion_clicks },
        { "non-finite host/input values + parameter fuzz",       test_nonfinite_and_extremes },
        { "level survey across sample rates (valid settings)",   test_gain_survey },
        { "block-size invariance",                               test_block_size_invariance },
        { "no heap allocation in the audio path",                test_no_allocation },
        { "spectrum ring semantics",                             test_ring },
        { "parameter round-trip (pluginval hypothesis)",         test_param_roundtrip },
    };
    int failedTests = 0;
    for (auto& t : tests)
    {
        const int before = g_fail;
        std::printf ("[ RUN  ] %s\n", t.name);
        t.fn();
        const bool ok = g_fail == before;
        std::printf ("[ %s ] %s\n\n", ok ? " OK " : "FAIL", t.name);
        failedTests += ok ? 0 : 1;
    }
    std::printf ("==== %d checks, %d failed | original defects reproduced: %d, not reproduced: %d ====\n",
                 g_checks, g_fail, g_repro, g_reproMiss);
    return failedTests == 0 ? 0 : 1;
}
