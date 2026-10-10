#include "SpectrumAnalyzer.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <chrono>
namespace sg {
void SpectrumAnalyzer::configure(int rate,const Settings& settings) {
    bool rebuild=samples_.empty() || rate!=rate_ || settings.fftSize!=settings_.fftSize || settings.bands!=settings_.bands;
    settings_=settings; settings_.validate(); rate_=std::clamp(rate,8000,384000);
    hopSize_=std::max(1,std::min(settings_.fftSize/4,rate_/480));
    if(!rebuild) return;
    int n=settings_.fftSize;
    samples_.assign(n,0); window_.resize(n); fft_.resize(n); twiddles_.resize(n/2); reversed_.resize(n);
    powers_.resize(n/2+1); smoothed_.assign(settings_.bands,0);
    unsigned bits=0; while((1u<<bits)<unsigned(n)) ++bits;
    for(int i=0;i<n;++i) {
        window_[i]=0.5f-0.5f*std::cos(2*std::numbers::pi_v<float>*i/(n-1));
        unsigned r=0,v=unsigned(i); for(unsigned j=0;j<bits;++j) { r=(r<<1)|(v&1); v>>=1; } reversed_[i]=r;
    }
    for(int i=0;i<n/2;++i) twiddles_[i]=std::polar(1.0f,-2*std::numbers::pi_v<float>*i/n);
    reset();
}
void SpectrumAnalyzer::reset() { std::fill(samples_.begin(),samples_.end(),0.0f); std::fill(smoothed_.begin(),smoothed_.end(),0.0f); cursor_=filled_=hop_=0; normalization_=.1f; }
float SpectrumAnalyzer::bandCenter(int band,int count,int sampleRate) { return 20*std::pow(std::min(20000.0f,sampleRate*.5f)/20,(band+.5f)/count); }
bool SpectrumAnalyzer::push(float sample,SpectrumFrame& output) {
    if(samples_.empty()) configure(rate_,settings_);
    samples_[cursor_]=std::isfinite(sample)?std::clamp(sample,-8.0f,8.0f):0;
    cursor_=(cursor_+1)%settings_.fftSize; filled_=std::min(filled_+1,settings_.fftSize); ++hop_;
    if(filled_<settings_.fftSize || hop_<hopSize_) return false;
    hop_=0; analyze(output); return true;
}
void SpectrumAnalyzer::analyze(SpectrumFrame& out) {
    auto start=std::chrono::steady_clock::now(); int n=settings_.fftSize; double energy=0;
    for(int i=0;i<n;++i) { float v=samples_[(cursor_+i)%n]; energy+=v*v; fft_[reversed_[i]]={v*window_[i],0}; }
    for(int len=2;len<=n;len*=2) for(int base=0;base<n;base+=len) for(int j=0;j<len/2;++j) {
        auto a=fft_[base+j], b=fft_[base+j+len/2]*twiddles_[j*n/len]; fft_[base+j]=a+b; fft_[base+j+len/2]=a-b;
    }
    out.rms=float(std::sqrt(energy/n)); out.bands=settings_.bands; out.values.fill(0);
    out.smoothingEnabled=settings_.smoothingEnabled;
    const float scale=4.0f/n;
    for(int i=0;i<=n/2;++i) powers_[i]=std::norm(fft_[i])*scale*scale;
    const float dt=float(hopSize_)/rate_, top=std::min(20000.0f,rate_*.5f);
    float peak=0;
    for(int band=0;band<settings_.bands;++band) {
        float lo=20*std::pow(top/20,float(band)/settings_.bands)*n/rate_;
        float hi=20*std::pow(top/20,float(band+1)/settings_.bands)*n/rate_;
        // Integrate a piecewise-linear spectrum instead of a constant bin value.
        // Narrow bass bands no longer all copy the same FFT bin. This is spectral
        // interpolation, not extra frequency resolution; a longer FFT still helps.
        float sum=0,weight=0;
        for(int k=std::max(0,int(std::floor(lo)));k<std::min(n/2,int(std::ceil(hi)));++k) {
            float a=std::max(lo,float(k)),b=std::min(hi,float(k+1));
            float w=std::max(0.0f,b-a);
            float left=std::lerp(powers_[k],powers_[k+1],a-k);
            float right=std::lerp(powers_[k],powers_[k+1],b-k);
            sum+=(left+right)*.5f*w; weight+=w;
        }
        // Integrate band energy (Hann equivalent noise bandwidth = 1.5 bins).
        // Averaging wide bands would incorrectly weaken high tones as FFT grows.
        float amplitude=std::sqrt(sum/std::max(std::min(weight,1.5f),1e-8f));
        float db=20*std::log10(std::max(amplitude,1e-9f));
        float gateGain=std::clamp((db-settings_.gate)/8.0f,0.0f,1.0f);
        // Equal spacing of bands is logarithmic in Hz. Gain in dB rises
        // smoothly from 0 at 20 Hz to the selected boost at the upper limit.
        float frequencyPosition=(band+.5f)/settings_.bands;
        float trebleGain=std::pow(10.0f,settings_.trebleBoostDb*frequencyPosition/20.0f);
        float linear=amplitude*settings_.sensitivity*trebleGain;
        float logarithmic=std::log1p(12*linear)/std::log(13.0f);
        float compressed=std::lerp(linear,logarithmic,settings_.compression)*gateGain;
        out.values[band]=compressed; peak=std::max(peak,compressed);
    }
    if(settings_.smoothingEnabled)
        normalization_+=(peak-normalization_)*(1-std::exp(-dt/(peak>normalization_?.08f:2.0f)));
    else
        normalization_=peak; // Optional gain depends only on this FFT window.
    float gain=settings_.normalize?std::min(3.0f,.8f/std::max(.08f,normalization_)):1;
    for(int i=0;i<settings_.bands;++i) {
        float target=std::clamp(out.values[i]*gain,0.0f,1.0f);
        if(settings_.smoothingEnabled) {
            float tau=target>smoothed_[i]?settings_.attack:settings_.smoothing;
            smoothed_[i]+=(target-smoothed_[i])*(1-std::exp(-dt/tau));
        } else {
            smoothed_[i]=target; // No attack/release state contributes to the output.
        }
        out.values[i]=smoothed_[i];
    }
    out.fftMs=std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-start).count();
}
void SpectrumTimeline::add(const SpectrumFrame& f) {
    if(count_ && f.time<frames_[(next_+511)%512].time) return;
    frames_[next_]=f; next_=(next_+1)%512; count_=std::min(count_+1,size_t(512));
}
SpectrumFrame SpectrumTimeline::sample(double time,int bands,bool smoothingEnabled) const {
    SpectrumFrame out; out.time=time; out.bands=bands; out.smoothingEnabled=smoothingEnabled;
    if(!count_) return out;
    const SpectrumFrame* older=nullptr; const SpectrumFrame* newer=nullptr;
    // Column boundary sums can differ by a few double ULPs across render rates.
    // A nanosecond tolerance selects the same measurement at an exact boundary.
    for(size_t j=0;j<count_;++j) { auto& f=frames_[(next_+511-j)%512]; if(f.time<=time+1e-9) { older=&f; break; } newer=&f; }
    if(!older) return out;
    if(older->bands!=bands || older->smoothingEnabled!=smoothingEnabled) return out;
    out=*older; out.time=time;
    if(!smoothingEnabled) {
        // Hold the most recent complete measurement, without blending frames or
        // inventing a release envelope when capture stops delivering samples.
        if(time-older->time>.1) out.values.fill(0);
        return out;
    }
    if(newer && newer->bands==bands && newer->smoothingEnabled==smoothingEnabled && newer->time>older->time) {
        float t=std::clamp(float((time-older->time)/(newer->time-older->time)),0.0f,1.0f);
        for(int i=0;i<bands;++i) out.values[i]=std::lerp(older->values[i],newer->values[i],t);
    } else if(time-older->time>.1) {
        float decay=float(std::exp(-8*(time-older->time-.1))); for(float& v:out.values) v*=decay;
    }
    return out;
}
}
