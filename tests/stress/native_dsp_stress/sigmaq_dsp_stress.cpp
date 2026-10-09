#include "Dsp.h"
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

using namespace dsp_eq;

static double poleRadius(const SvfCoeffs& c) {
    const double invg = 1.0 / c.g;
    const double a0 = invg * invg + c.k * invg + 1.0;
    const double a1 = 2.0 - 2.0 * invg * invg;
    const double a2 = invg * invg - c.k * invg + 1.0;
    const auto disc = std::complex<double>(a1*a1 - 4.0*a0*a2, 0.0);
    const auto root = std::sqrt(disc);
    return std::max(std::abs((-a1 + root)/(2.0*a0)), std::abs((-a1 - root)/(2.0*a0)));
}

static double measuredDb(const StageSet& set, double fs, double hz, int& sampleCount) {
    const double w = 2.0*kPi*hz/fs;
    int burn = std::max(4096, int(std::ceil(12.0*18.0*fs/std::max(200.0, hz))));
    int measure = std::max(8192, int(std::ceil(20.0*fs/std::max(200.0, hz))));
    sampleCount += burn + measure;
    SvfState state[4];
    std::complex<double> ph(1,0), rot(std::cos(w), -std::sin(w));
    double ss=0,cc=0,sc=0,ys=0,yc=0;
    for (int i=0; i<burn+measure; ++i) {
        double sn=-ph.imag(), cs=ph.real();
        double x = sn;
        double y=x;
        for (int j=0; j<set.n; ++j) y=state[j].process(set.c[j], y);
        if (i>=burn) { ss+=sn*sn;cc+=cs*cs;sc+=sn*cs;ys+=y*sn;yc+=y*cs; }
        ph *= rot;
    }
    const double det=ss*cc-sc*sc;
    const double sinAmp=(ys*cc-yc*sc)/det;
    const double cosAmp=(yc*ss-ys*sc)/det;
    const double ratio=std::hypot(sinAmp,cosAmp);
    return 20.0*std::log10(std::max(1e-12, ratio));
}

static double runBench(int fs, bool motion, bool cuts) {
    constexpr int bands=12, ch=2, block=32;
    const int samples = fs*3;
    std::array<StageSet,bands> sets;
    std::array<std::array<std::array<SvfState,4>,ch>,bands> st{};
    std::vector<double> input(samples);
    for (int i=0;i<samples;++i) input[i]=0.001*std::sin(2.0*kPi*1000.0*i/fs);
    for (int b=0;b<bands;++b)
        sets[b]=makeBand(cuts ? LowCut : Bell, fs, 200.0*std::pow(1.4,b), 1.0, 6.0, 2);
    double sink=0.0;
    auto t0=std::chrono::steady_clock::now();
    for (int pos=0;pos<samples;pos+=block) {
        if (motion) for (int b=0;b<bands;++b) {
            double phase = 2.0*kPi*(double(pos)/fs*3.0 + b/12.0);
            double f=200.0*std::pow(1.4,b)*std::pow(2.0,std::sin(phase));
            sets[b]=makeBand(cuts ? LowCut : Bell, fs, f, 1.0, 6.0+3.0*std::sin(phase), 2);
        }
        for (int i=pos;i<std::min(pos+block,samples);++i) {
            double in=input[i];
            for (int c=0;c<ch;++c) {
                double v=in;
                for (int b=0;b<bands;++b)
                    for (int s=0;s<sets[b].n;++s)
                        v=st[b][c][s].process(sets[b].c[s],v);
                sink+=v;
            }
        }
    }
    auto t1=std::chrono::steady_clock::now();
    if (!std::isfinite(sink)) std::cerr << "benchmark nonfinite " << sink << '\n';
    return std::chrono::duration<double,std::milli>(t1-t0).count()/3.0;
}

static void stackTest(int type, double testHz) {
    constexpr double sr=44100.0;
    auto set=makeBand(type,sr,1000.0,18.0,30.0,0);
    SvfState st[12];
    double peak=0.0; int bad=0;
    const double outputGain=std::pow(10.0,18.0/20.0);
    for (int i=0;i<int(sr*3);++i) {
        double v=std::sin(2.0*kPi*testHz*i/sr);
        for (auto& s:st) v=s.process(set.c[0],v);
        float out=(float)(v*outputGain);
        if (!std::isfinite(v) || !std::isfinite(out)) bad++;
        if (i>=int(sr*2)) peak=std::max(peak,std::abs((double)out));
    }
    std::cout << "stack type " << type << " hz " << testHz << " predictedDb "
              << (12*bandMagnitudeDb(set,sr,testHz)+18.0) << " peakLinear " << peak
              << " peakDbFS " << 20*std::log10(peak) << " nonfinite " << bad << '\n';
}

static void motionTest(int shape) {
    constexpr double sr=44100.0;
    const int total=int(sr*2),block=32;
    SvfState st;
    double prev=0,maxResetDelta=0,maxBlockDelta=0,maxWithinDelta=0,peak=0;
    int resets=0;
    double prevCycles=-1;
    for (int pos=0;pos<total;pos+=block) {
        double cycles=10.0*pos/sr;
        double w=lfoShape(shape,cycles,0);
        double f=std::clamp(10000.0*std::pow(2.0,3.0*w),20.0,20000.0);
        double gain=18.0*w;
        auto set=makeBand(LowShelf,sr,f,1.0,gain,0);
        bool reset=prevCycles>=0 && std::floor(cycles)!=std::floor(prevCycles);
        if (reset) resets++;
        prevCycles=cycles;
        for (int i=pos;i<std::min(pos+block,total);++i) {
            double out=st.process(set.c[0],0.1);
            peak=std::max(peak,std::abs(out));
            double delta=std::abs(out-prev);
            if (i==pos && reset) maxResetDelta=std::max(maxResetDelta,delta);
            else if (i==pos && i>0) maxBlockDelta=std::max(maxBlockDelta,delta);
            else if (i>0) maxWithinDelta=std::max(maxWithinDelta,delta);
            prev=out;
        }
    }
    std::cout << "motion shape " << shape << " resets " << resets << " peak " << peak
              << " maxResetSampleStep " << maxResetDelta << " maxBlockSampleStep " << maxBlockDelta
              << " maxWithinBlockSampleStep " << maxWithinDelta << '\n';
}

int main(int argc, char** argv) {
    std::cout << std::setprecision(9);
    if (argc>1 && std::string(argv[1])=="plot") {
        std::cout << "type,hz,predicted_db,measured_db\n";
        int dummy=0;
        for (int type:{Bell,LowShelf}) {
            auto set=makeBand(type,44100.0,1000.0,18.0,30.0,0);
            std::vector<double> hzList;
            for (int i=0;i<=400;++i) hzList.push_back(20.0*std::pow(1000.0,i/400.0));
            hzList.push_back(1000.0);
            std::sort(hzList.begin(),hzList.end());
            for (double hz:hzList) {
                std::cout << type << ',' << hz << ',' << bandMagnitudeDb(set,44100.0,hz)
                          << ',' << measuredDb(set,44100.0,hz,dummy) << '\n';
            }
        }
        return 0;
    }
    const std::array<double,6> srs={22050,32000,44100,48000,96000,192000};
    const std::array<double,6> fs={20,60,1000,10000,16000,20000};
    const std::array<double,4> qs={0.1,0.70710678,1.0,18.0};
    const std::array<double,3> gains={-30,0,30};
    int cases=0, bad=0, outOfRange=0;
    double maxPole=0, maxMagnitude=0;
    for (double sr:srs) for (int type=Bell;type<=BandPass;++type)
    for (double f:fs) for (double q:qs) for (double gain:gains)
    for (int slope=0;slope<3;++slope) {
        auto set=makeBand(type,sr,f,q,gain,slope);
        cases++;
        for (int i=0;i<set.n;++i) {
            auto& c=set.c[i];
            const double coeffs[]={c.g,c.k,c.a1,c.a2,c.a3,c.m0,c.m1,c.m2};
            for (double v:coeffs) if (!std::isfinite(v)) bad++;
            double p=poleRadius(c);
            if (!std::isfinite(p)) bad++;
            maxPole=std::max(maxPole,p);
            if (p>=1.0) outOfRange++;
        }
        for (double fTest: {20.0,100.0,1000.0,5000.0,10000.0,std::min(20000.0,sr*0.49)}) {
            double d=bandMagnitudeDb(set,sr,fTest);
            if (!std::isfinite(d)) bad++;
            maxMagnitude=std::max(maxMagnitude,std::abs(d));
        }
    }
    std::cout << "grid cases " << cases << " nonfinite " << bad << " unstablePoles " << outOfRange
              << " maxPoleRadius " << maxPole << " maxAbsCurveDb " << maxMagnitude << '\n';

    double maxErr=0, maxErrAboveMinus80=0;
    int measured=0, sampleCount=0;
    int wt=-1,ws=-1; double wf=0,wq=0,wg=0,wh=0,wa=0,wm=0;
    int wt80=-1,ws80=-1; double wf80=0,wq80=0,wg80=0,wh80=0,wa80=0,wm80=0;
    for (double sr:{44100.0,96000.0}) for (int type=Bell;type<=BandPass;++type)
    for (double f:{200.0,1000.0,10000.0}) for (double q:{0.1,1.0,18.0})
    for (double gain:{-30.0,30.0}) for (int slope=0;slope<(isCut(type)?3:1);++slope) {
        auto set=makeBand(type,sr,f,q,gain,slope);
        for (double hz:{std::max(20.0,f*0.5),f,std::min(f*2.0,sr*0.45)}) {
            double a=bandMagnitudeDb(set,sr,hz);
            double m=measuredDb(set,sr,hz,sampleCount);
            double e=std::abs(m-a);
            if (e>maxErr) {maxErr=e;wt=type;ws=slope;wf=f;wq=q;wg=gain;wh=hz;wa=a;wm=m;}
            if (a>-80.0 && e>maxErrAboveMinus80) {
                maxErrAboveMinus80=e;wt80=type;ws80=slope;wf80=f;wq80=q;wg80=gain;wh80=hz;wa80=a;wm80=m;
            }
            measured++;
        }
    }
    std::cout << "response comparisons " << measured << " samples " << sampleCount
              << " maxErrorDb " << maxErr << " maxErrorWhenCurveAboveMinus80 " << maxErrAboveMinus80
              << " worst type " << wt << " slope " << ws << " f " << wf << " q " << wq
              << " gain " << wg << " hz " << wh << " analytic " << wa << " measured " << wm << '\n';
    std::cout << "response worstAboveMinus80 type " << wt80 << " slope " << ws80 << " f " << wf80
              << " q " << wq80 << " gain " << wg80 << " hz " << wh80 << " analytic " << wa80
              << " measured " << wm80 << '\n';

    double lo=1e100,hi=-1e100;
    int nonfinite=0, escaped=0;
    for (int shape=0;shape<4;++shape) for (int salt=0;salt<12;++salt)
    for (int i=-20000;i<=20000;++i) {
        double x=lfoShape(shape,double(i)/97.0,salt);
        if (!std::isfinite(x)) nonfinite++;
        if (x<-1.000000001 || x>1.000000001) escaped++;
        lo=std::min(lo,x);hi=std::max(hi,x);
    }
    std::cout << "lfo values " << (4*12*40001) << " min " << lo << " max " << hi
              << " nonfinite " << nonfinite << " escaped " << escaped << '\n';

    stackTest(Bell,1000.0);
    for (int type:{LowShelf,HighShelf}) {
        auto set=makeBand(type,44100.0,1000.0,18.0,30.0,0);
        double bestHz=0,bestDb=-1e100;
        for (int i=0;i<=10000;++i) {
            double hz=200.0*std::pow(25.0,i/10000.0);
            double db=bandMagnitudeDb(set,44100.0,hz);
            if (db>bestDb) {bestDb=db;bestHz=hz;}
        }
        stackTest(type,bestHz);
    }
    motionTest(0);
    motionTest(2);

    for (int sr:{44100,96000,192000}) {
        double bs=runBench(sr,false,false), bm=runBench(sr,true,false);
        double cs=runBench(sr,false,true), cm=runBench(sr,true,true);
        std::cout << "bench fs " << sr << " audioMsPerSec bellStatic " << bs << " bellMotion " << bm
                  << " cut48Static " << cs << " cut48Motion " << cm << '\n';
    }
    return (bad == 0 && outOfRange == 0 && nonfinite == 0 && escaped == 0
            && maxErrAboveMinus80 < 0.05) ? 0 : 1;
}
