#pragma once
#include "Config.h"
#include <array>
#include <complex>
#include <vector>
#include <cstdint>
namespace sg {
struct SpectrumFrame {
    double time=0;
    std::array<float,256> values{};
    int bands=96;
    float rms=0, fftMs=0;
    bool smoothingEnabled=true;
};
class SpectrumAnalyzer {
public:
    void configure(int sampleRate,const Settings& settings);
    bool push(float sample,SpectrumFrame& output);
    void reset();
    static float bandCenter(int band,int count,int sampleRate);
private:
    void analyze(SpectrumFrame& output);
    Settings settings_;
    int rate_=48000, cursor_=0, filled_=0, hop_=0, hopSize_=100;
    std::vector<float> samples_, window_, smoothed_, powers_;
    std::vector<std::complex<float>> fft_,twiddles_;
    std::vector<unsigned> reversed_;
    float normalization_=0.1f;
};
class SpectrumTimeline {
public:
    void add(const SpectrumFrame& f);
    SpectrumFrame sample(double time,int bands,bool smoothingEnabled=true) const;
private:
    std::array<SpectrumFrame,512> frames_{};
    size_t next_=0,count_=0;
};
}
