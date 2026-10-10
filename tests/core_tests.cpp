#include "SpectrumAnalyzer.h"
#include "GridSimulation.h"
#include "Config.h"
#include "AudioSamples.h"
#ifdef _WIN32
#include "Camera.h"
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <functional>
#include <chrono>
using namespace sg;
static int passed=0,failed=0;
static void require(bool ok,const char* text) { if(!ok) throw std::runtime_error(text); }
static void near(double a,double b,double eps,const char* text) { if(std::abs(a-b)>eps) throw std::runtime_error(std::string(text)+": "+std::to_string(a)+" vs "+std::to_string(b)); }
static void test(const std::string& name,const std::function<void()>& fn) { try { fn(); ++passed; std::cout<<"PASS "<<name<<'\n'; } catch(const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; } }
static SpectrumFrame signal(const std::vector<float>& tones,float amplitude,Settings s={},int rate=48000) {
    SpectrumAnalyzer analyzer; analyzer.configure(rate,s); SpectrumFrame f;
    for(int i=0;i<rate;++i) { float sample=0; for(float tone:tones) sample+=float(amplitude*std::sin(2*std::numbers::pi_v<double>*tone*i/rate)); analyzer.push(sample,f); }
    for(float v:f.values) require(std::isfinite(v) && v>=0 && v<=1,"Finite bounded output");
    return f;
}
static int peak(const SpectrumFrame& f) { return int(std::max_element(f.values.begin(),f.values.begin()+f.bands)-f.values.begin()); }
int main() {
    test("PCM8/16/24/32 and valid bits",[] {
        const uint8_t u8[]={0,128,255}; near(decodeAudioSample(u8,8,false),-1,1e-8,"PCM8 negative"); near(decodeAudioSample(u8+1,8,false),0,1e-8,"PCM8 midpoint");
        int16_t s16=-16384; near(decodeAudioSample(reinterpret_cast<uint8_t*>(&s16),16,false),-.5,1e-8,"PCM16");
        const uint8_t s24[]={0,0,0xC0}; near(decodeAudioSample(s24,24,false),-.5,1e-8,"PCM24 sign extension");
        int32_t s32=1073741824; near(decodeAudioSample(reinterpret_cast<uint8_t*>(&s32),32,false),.5,1e-8,"PCM32 and 24-in-32");
    });
    test("Float32/64 conversion and NaN",[] { float f=.25f; double d=-.75; near(decodeAudioSample(reinterpret_cast<uint8_t*>(&f),32,true),.25,1e-8,"Float32"); near(decodeAudioSample(reinterpret_cast<uint8_t*>(&d),64,true),-.75,1e-8,"Float64"); f=std::numeric_limits<float>::quiet_NaN(); near(decodeAudioSample(reinterpret_cast<uint8_t*>(&f),32,true),0,1e-8,"Invalid float"); });
    test("Multichannel downmix",[] { float channels[]={.25f,.5f,.75f,0,1,-1}; near(mixAudioFrame(reinterpret_cast<uint8_t*>(channels),6,32,true),.25,1e-8,"Six-channel mean"); });
#ifdef _WIN32
    test("Horizontal spin preserves tilted plane and projected center",[] {
        using namespace DirectX;
        Settings s; s.validate(); s.panX=2; s.panY=-1; Camera camera;
        auto q=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
        auto normal=XMVector3Rotate(XMVectorSet(0,1,0,0),q);
        auto tangent=XMVector3Rotate(XMVectorSet(1,0,0,0),q);
        auto center=XMVector3TransformCoord(XMVectorZero(),camera.matrix(s,1.6f));
        camera.rotate(s,850,100,970,100,1000,800,.016f);
        q=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
        near(XMVectorGetX(XMVector3Length(normal-XMVector3Rotate(XMVectorSet(0,1,0,0),q))),0,1e-6,"Plane normal unchanged");
        require(XMVectorGetX(XMVector3Length(tangent-XMVector3Rotate(XMVectorSet(1,0,0,0),q)))>.1f,"Points rotate in plane");
        near(XMVectorGetX(XMVector3Length(center-XMVector3TransformCoord(XMVectorZero(),camera.matrix(s,1.6f)))),0,1e-6,"Projected center fixed");
        for(int i=0;i<120;++i) camera.update(s,1.0f/120,false);
        q=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
        near(XMVectorGetX(XMVector3Length(normal-XMVector3Rotate(XMVectorSet(0,1,0,0),q))),0,1e-5,"Inertia preserves plane normal");
    });
    test("Vertical tilt independent of spin and cursor location",[] {
        using namespace DirectX;
        Settings a,b; a.validate(); b=a; Camera ca,cb;
        cb.rotate(b,500,400,700,400,1000,800,.016f);
        ca.rotate(a,100,100,100,220,1000,800,.016f);
        cb.rotate(b,800,500,800,620,1000,800,.016f);
        auto normal=[](const Settings& s) { return XMVector3Rotate(XMVectorSet(0,1,0,0),XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()))); };
        near(XMVectorGetX(XMVector3Length(normal(a)-normal(b))),0,1e-6,"Same tilt regardless of spin or cursor position");
        Settings original; original.validate();
        require(XMVectorGetX(XMVector3Length(normal(a)-normal(original)))>.1f,"Vertical drag tilts plane");
    });
    test("Holding mouse still stops residual inertia",[] {
        Settings s; Camera camera;
        camera.rotate(s,500,400,620,400,1000,800,.016f);
        camera.rotate(s,620,400,620,400,1000,800,.016f);
        auto before=s.rotation; camera.update(s,.1f,false);
        require(s.rotation==before,"Stationary drag clears velocity");
    });
    test("Automatic spin period plane and frame rate",[] {
        using namespace DirectX;
        for(int fps:{5,30,60,144,240}) {
            Settings s; s.validate(); s.autoRotation=100; s.inertia=0; Camera c;
            auto start=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
            auto normal=XMVector3Rotate(XMVectorSet(0,1,0,0),start);
            for(int i=0;i<fps*5;++i) c.update(s,1.0f/fps,true);
            auto end=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
            near(std::abs(XMVectorGetX(XMVector4Dot(start,end))),1,1e-5,"Full rotation in five seconds even with UI captured");
            near(XMVectorGetX(XMVector3Length(normal-XMVector3Rotate(XMVectorSet(0,1,0,0),end))),0,1e-5,"Plane stays fixed");
            s.rotation={0,0,0,1}; s.autoRotation=50; c.update(s,2.5f,false);
            auto tangent=XMVector3Rotate(XMVectorSet(1,0,0,0),XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data())));
            near(XMVectorGetX(tangent),0,1e-5,"50 percent gives quarter turn in 2.5 seconds");
            s.autoRotation=0; auto stopped=s.rotation; c.update(s,1,false); require(s.rotation==stopped,"Zero stops auto rotation without jump");
        }
    });
    test("Camera rotations maintain unit quaternion",[] {
        Settings s; s.rotation={0,0,0,1}; Camera camera;
        camera.rotate(s,500,400,620,400,1000,800,.016f); require(std::abs(s.rotation[1])>.01f,"Yaw rotation");
        camera.reset(s); s.rotation={0,0,0,1}; camera.rotate(s,500,400,500,520,1000,800,.016f); require(std::abs(s.rotation[0])>.01f,"Pitch rotation");
        for(int i=0;i<10000;++i) camera.rotate(s,600,400,600,410,1000,800,.016f);
        float norm=0; for(float v:s.rotation) norm+=v*v; near(norm,1,1e-5,"No quaternion drift");
        float determinant=DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(camera.matrix(s,1.6f))); require(std::isfinite(determinant) && std::abs(determinant)>1e-6,"Valid perspective transform");
    });
#endif
    for(int fft:{2048,4096,8192,16384}) for(float hz:{100.0f,1000.0f,10000.0f}) {
        test("Sine "+std::to_string(int(hz))+"Hz FFT "+std::to_string(fft),[=] {
            Settings s; s.fftSize=fft; auto f=signal({hz},.5f,s); float center=SpectrumAnalyzer::bandCenter(peak(f),s.bands,48000);
            require(std::abs(center-hz)<std::max(48000.0f/fft,hz*.13f),"Peak must be in the expected frequency band"); require(f.values[peak(f)]>.2f,"Tone must produce visible energy");
        });
    }
    test("Composite signal",[] { auto f=signal({100,1000,10000},.2f); for(float hz:{100.0f,1000.0f,10000.0f}) { float maximum=0; for(int b=0;b<f.bands;++b) if(std::abs(std::log(SpectrumAnalyzer::bandCenter(b,f.bands,48000)/hz))<.18f) maximum=std::max(maximum,f.values[b]); require(maximum>.1f,"Every tone needs a responding band"); } });
    test("Silence",[] { auto f=signal({},0); for(float v:f.values) near(v,0,1e-8,"Silence zero"); });
    test("Treble gain follows logarithmic frequency and preserves silence",[] {
        Settings plain,boost; plain.compression=boost.compression=1; boost.trebleBoostDb=24;
        float previous=0;
        for(float hz:{100.0f,1000.0f,10000.0f}) {
            auto a=signal({hz},.01f,plain),b=signal({hz},.01f,boost); int band=peak(a);
            double input=std::expm1(a.values[band]*std::log(13.0));
            double output=std::expm1(b.values[band]*std::log(13.0));
            double gain=output/input;
            near(gain,std::pow(10.0,24*(band+.5)/plain.bands/20),2e-5,"Gain before compression matches log-frequency curve");
            require(gain>previous,"Higher frequency gets more gain"); previous=float(gain);
        }
        auto silent=signal({},0,boost); for(float v:silent.values) near(v,0,1e-8,"Boost does not create sound");
        boost.normalize=true; auto normalized=signal({100,1000,10000},.02f,boost);
        require(normalized.values[peak(normalized)]>0,"Boost works with normalization");
    });
    test("Compression off preserves amplitude ratios and full restores old curve",[] {
        Settings s; auto low=signal({1000},.02f,s),high=signal({1000},.2f,s); int b=peak(low);
        near(high.values[b]/low.values[b],10,1e-4,"Linear amplitude ratio");
        s.compression=1; auto full=signal({1000},.02f,s);
        near(full.values[b],std::log1p(12*low.values[b])/std::log(13.0),1e-6,"Previous logarithmic curve");
        s.compression=.5f; auto middle=signal({1000},.02f,s);
        near(middle.values[b],(low.values[b]+full.values[b])*.5,1e-6,"Continuous intermediate setting");
    });
    test("Low amplitude",[] { auto low=signal({1000},.002f); auto high=signal({1000},.5f); require(low.values[peak(low)]>0,"Small signal is represented"); require(low.values[peak(low)]<high.values[peak(high)],"Dynamics preserved"); });
    test("Loud signal and NaN",[] { auto f=signal({1000},4); require(f.values[peak(f)]>.9f,"Loud tone responds"); SpectrumAnalyzer a; a.configure(48000,{}); for(int i=0;i<10000;++i) a.push(std::numeric_limits<float>::quiet_NaN(),f); for(float v:f.values) require(std::isfinite(v),"NaN rejected"); });
    test("Sensitivity",[] { Settings a,b; a.sensitivity=.1f; b.sensitivity=10; auto low=signal({1000},.01f,a),high=signal({1000},.01f,b); require(high.values[peak(high)]>low.values[peak(low)]*5,"Sensitivity clearly changes energy"); });
    test("Noise gate",[] { Settings s; s.gate=-30; auto f=signal({1000},.0001f,s); near(f.values[peak(f)],0,1e-6,"Gate rejects small signal"); });
    test("Nyquist and band resize",[] { Settings s; s.bands=256; auto f=signal({3000},.5f,s,8000); require(f.bands==256,"256 bands"); require(SpectrumAnalyzer::bandCenter(255,256,8000)<4000,"Nyquist cap"); });
    test("FFT refresh supports 240 independent column measurements",[] {
        for(int rate:{44100,48000,96000}) for(int n:{2048,4096,8192,16384}) {
            Settings s; s.fftSize=n; SpectrumAnalyzer a; a.configure(rate,s); SpectrumFrame f; int count=0,last=-1;
            for(int i=0;i<n+rate;++i) if(a.push(0,f)) {
                if(last>=0) require(i-last==rate/480,"Fixed sample hop independent of FFT size");
                last=i; ++count;
            }
            require(count>=480,"At least 480 spectra per second after warmup");
        }
    });
    test("Narrow bass bands interpolate instead of copying bins",[] {
        Settings s; s.bands=256; auto f=signal({40},.3f,s);
        int differing=0;
        for(int i=1;i<20;++i) if(std::abs(f.values[i]-f.values[i-1])>1e-6) ++differing;
        require(differing>=15,"Bass bins contain a spectral slope rather than long flat runs");
    });
    test("Auto normalization bounded",[] { Settings s; s.normalize=true; auto f=signal({1000},.01f,s); require(f.values[peak(f)]>0 && f.values[peak(f)]<=1,"Bounded AGC"); });
    test("Raw FFT output has no dependence on prior windows or AGC history",[] {
        for(bool normalize:{false,true}) {
            Settings s; s.smoothingEnabled=false; s.normalize=normalize;
            SpectrumAnalyzer loud,quiet; loud.configure(48000,s); quiet.configure(48000,s);
            SpectrumFrame a,b;
            for(int i=0;i<s.fftSize*4;++i) {
                loud.push(float(.8*std::sin(2*std::numbers::pi_v<double>*1000*i/48000)),a);
                quiet.push(0,b);
            }
            for(int i=0;i<s.fftSize+48000/480;++i) {
                float sample=float(.005*std::sin(2*std::numbers::pi_v<double>*1000*i/48000));
                loud.push(sample,a); quiet.push(sample,b);
            }
            require(!a.smoothingEnabled && !b.smoothingEnabled,"Frames carry raw mode");
            for(int band=0;band<s.bands;++band) near(a.values[band],b.values[band],1e-7,"Same window gives same result after different histories");
            require(a.values[peak(a)]>0,"Current tone is represented");
        }
    });
    test("Disabling smoothing discards attack and release state",[] {
        Settings s; s.smoothingEnabled=true; s.attack=.5f; s.smoothing=2;
        SpectrumAnalyzer a; a.configure(48000,s); SpectrumFrame f;
        for(int i=0;i<s.fftSize*8;++i) a.push(float(.8*std::sin(2*std::numbers::pi_v<double>*1000*i/48000)),f);
        s.smoothingEnabled=false; a.configure(48000,s);
        for(int i=0;i<s.fftSize;++i) a.push(0,f);
        for(float value:f.values) near(value,0,1e-8,"No release tail after a complete silent window");
        s.smoothingEnabled=true; a.configure(48000,s);
        for(int i=0;i<s.fftSize;++i) a.push(float(.8*std::sin(2*std::numbers::pi_v<double>*1000*i/48000)),f);
        require(f.smoothingEnabled && f.values[peak(f)]>0,"Smoothing can be enabled again");
    });
    test("Raw timeline never blends adjacent FFT results",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.smoothingEnabled=false; f.values.fill(.2f); t.add(f);
        f.time=.02; f.values.fill(.8f); t.add(f);
        near(t.sample(.01,16,false).values[0],.2,1e-7,"Most recent raw measurement");
        near(t.sample(.02,16,false).values[0],.8,1e-7,"Next measurement without interpolation");
        near(t.sample(.2,16,false).values[0],0,1e-7,"Stale input is zero without synthetic release");
        near(t.sample(.01,16,true).values[0],0,1e-7,"Wrong-mode frames rejected");
    });
    test("Queued smoothed frames cannot leak into raw mode",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(.9f); t.add(f);
        near(t.sample(.01,16,false).values[0],0,1e-7,"Previous smoothed result rejected");
        f.time=.02; f.smoothingEnabled=false; f.values.fill(.1f); t.add(f);
        near(t.sample(.02,16,false).values[0],.1,1e-7,"First raw result used directly");
        near(t.sample(.01,16,true).values[0],.9,1e-7,"No cross-mode interpolation");
    });
    test("240 columns per second and raw histories at different render FPS",[] {
        auto t=std::make_unique<SpectrumTimeline>();
        for(int i=0;i<500;++i) { SpectrumFrame f; f.time=i*.01; f.bands=16; f.smoothingEnabled=false; f.values.fill(float(.5+.2*std::sin(i*.07))); t->add(f); }
        std::vector<float> reference;
        for(int fps:{30,60,120,144,240}) {
            GridSimulation g; g.resize(16,256,0);
            for(int i=1;i<=fps;++i) g.advance(double(i)/fps,240,1,*t,4,false);
            require(g.newest()==240,"240 column boundaries in one second"); near(g.phase(),0,1e-7,"No speed cap at 60");
            for(int age=0;age<256;++age) {
                float h=g.height(age,0);
                if(fps==30) reference.push_back(h); else near(h,reference[age],2e-5,"Raw heights independent of render FPS");
            }
        }
    });
    auto timeline=std::make_unique<SpectrumTimeline>();
    for(int i=0;i<500;++i) { SpectrumFrame f; f.time=i*.01; f.bands=16; f.values.fill(float(.4+.2*std::sin(i*.02))); timeline->add(f); }
    test("Timeline interpolation",[&] { auto f=timeline->sample(.005,16); near(f.values[0],.4+.1*std::sin(.02),1e-6,"Interpolation"); });
    test("History only updates newest",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.time=0; f.values.fill(.8f); t.add(f);
        GridSimulation g; g.resize(16,16,0); g.advance(.11,10,1,t); float old=g.height(1,0);
        f.time=.12; f.values.fill(.1f); t.add(f); g.advance(.15,10,1,t);
        near(g.height(1,0),old+std::sqrt(2*.25*.3*.8)*.04-.5*.25*(.05*.05-.01*.01),1e-5,"Older column follows gravity without new audio"); require(g.height(0,0)<.2f,"New audio only in newest");
    });
    test("Frame-rate independence 30/60/120/144",[&] {
        std::vector<float> reference;
        for(int fps:{30,60,120,144}) { GridSimulation g; g.resize(16,32,0); for(int frame=1;frame<=fps*2;++frame) g.advance(double(frame)/fps,17.3f,.7f,*timeline);
            near(g.phase(),.6,2e-5,"Scroll phase"); std::vector<float> heights; for(int i=0;i<32;++i) heights.push_back(g.height(i,0));
            if(reference.empty()) reference=heights; else for(int i=0;i<32;++i) near(heights[i],reference[i],1e-5,"Identical heights independent of FPS");
        }
    });
    test("Continuous gravity change",[&] { GridSimulation g; g.resize(16,32,0); g.advance(.35,10,1,*timeline); float old=g.height(2,0),v=g.velocity(2,0); g.advance(.35,10,8,*timeline); near(g.height(2,0),old,1e-7,"No position jump"); near(g.velocity(2,0),v,1e-7,"No velocity jump"); g.advance(.350001,10,8,*timeline); near(g.height(2,0),old-v*.000001-.5*2*1e-12,1e-6,"New acceleration applied"); });
    test("Speed change preserves phase",[&] { GridSimulation g; g.resize(16,32,0); g.advance(.035,10,1,*timeline); double phase=g.phase(); g.advance(.035001,60,1,*timeline); near(g.phase(),phase+.00006,1e-8,"Continuous position after speed change"); });
    test("Ring wrapping and maximum size",[&] { GridSimulation g; g.resize(256,256,0); for(int i=1;i<20000;++i) { g.advance(i*.01,60,1,*timeline); g.clearDirty(); require(g.newest()>=0 && g.newest()<257,"Row within allocation"); } require(g.rows()==257,"One extra boundary row"); });
    test("Resize all independent combinations",[&] { GridSimulation g; for(int b:{16,64,128,256}) for(int c:{16,64,128,256}) { g.resize(b,c,0); g.advance(.1,10,1,*timeline); require(g.bands()==b && g.columns()==c,"Independent axes"); near(g.height(c, b-1),0,1e-6,"Valid last point"); } });
    test("Silence keeps grid moving",[] { SpectrumTimeline t; GridSimulation g; g.resize(96,128,0); g.advance(10,24,.9f,t); require(g.columns()==128 && g.bands()==96,"Grid retained"); near(g.height(5,5),0,1e-8,"Flat"); require(g.newest()!=0,"History continues"); });
    test("Long suspend and clock rebasing",[&] { GridSimulation g; g.resize(16,16,0); g.advance(100000,60,8,*timeline); g.advance(100001,60,8,*timeline); require(std::isfinite(g.height(15,0)),"Long runtime finite"); require(g.simulationTime()<8,"Rebased clock"); });
    test("Launch from 70 percent reaches target and then accelerates downward",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,16,0); g.advance(1,1,2,t,1);
        double v=std::sqrt(1.2),apex=v/2;
        near(g.height(1,0),.7,1e-7,"70 percent launch height"); near(g.velocity(1,0),-v,1e-7,"Upward launch velocity");
        g.advance(1+apex,1,2,t,1); near(g.height(1,0),1,1e-6,"Target is apex"); near(g.velocity(1,0),0,1e-6,"Stops rising at apex");
        g.advance(1+apex+.1,1,2,t,1); near(g.height(1,0),.99,1e-6,"Starts falling"); near(g.velocity(1,0),.2,1e-6,"Downward acceleration");
        g.advance(1+apex+.2,1,2,t,1); near(g.height(1,0),.96,1e-6,"Accelerating descent");
    });
    test("Launch color persists through rise fall landing and rebase",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); f.values[1]=.25f; t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,32,0); g.advance(1,1,2,t,1); int launched=(g.newest()+g.rows()-1)%g.rows();
        near(g.height(1,0),.7,1e-6,"Launch below target");
        for(double time:{1.2,1.6,2.5,3.0,9.0}) {
            g.advance(time,1,time<2?2.0f:4.0f,t,1);
            near(std::abs(g.row(launched)[0].colorHeight),1,0,"Full target color remains from launch");
            near(std::abs(g.row(launched)[1].colorHeight),.25,0,"Lower target retains its own color");
        }
        near(g.height(9,0),0,1e-6,"Landed without losing assigned color");
    });
    test("Single rebound reaches forty percent independently of FPS",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        double landing=(std::sqrt(1.2)+std::sqrt(4.0))/2,peak=1+landing+std::sqrt(1.6)/2;
        for(int fps:{30,60,144,240}) {
            GridSimulation g; g.resize(16,32,0);
            for(int i=1;double(i)/fps<peak;++i) g.advance(double(i)/fps,1,2,t,1,true,.4f);
            g.advance(peak,1,2,t,1,true,.4f);
            near(g.height(int(std::floor(peak)),0),.4,2e-6,"Rebound apex is forty percent");
            near(g.velocity(int(std::floor(peak)),0),0,2e-6,"Rebound apex velocity zero");
            g.advance(6,1,2,t,1,true,.4f); near(g.height(6,0),0,1e-6,"No second rebound");
            g.advance(6,1,.1f,t,1,true,.8f); g.advance(7,1,.1f,t,1,true,.8f); near(g.height(7,0),0,1e-6,"Landed point stays landed after changes");
        }
    });
    test("Rebound retains position velocity and color when controls change",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,32,0); g.advance(2.7,1,2,t,1,true,.4f);
        float h=g.height(2,0),v=g.velocity(2,0);
        require(v<0,"Already rising after rebound");
        g.advance(2.7,1,4,t,1,true,.8f);
        near(g.height(2,0),h,1e-6,"No position jump"); near(g.velocity(2,0),v,1e-6,"No velocity reset");
        g.advance(2.8,1,4,t,1,true,.8f);
        near(g.height(2,0),h-v*.1-.5*4*.01,2e-6,"New acceleration applies to existing bounce");
        g.advance(9,1,4,t,1,true,.8f); near(g.height(9,0),0,1e-6,"No repeated rebound after rebase");
    });
    test("Independent heights, floor contact and no bounce",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); f.values[1]=.01f; t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,16,0); g.advance(1.2,1,2,t,1);
        near(g.height(1,0),.7+std::sqrt(1.2)*.2-.04,1e-6,"High point still rising"); near(g.height(1,1),0,1e-8,"Low point landed"); near(g.velocity(1,1),0,1e-8,"Landed point stopped");
        g.advance(3,1,2,t,1); near(g.height(3,0),0,1e-7,"Exact landing"); near(g.velocity(3,0),0,1e-7,"No underground velocity");
        g.advance(3,1,.1f,t,1); g.advance(4,1,.1f,t,1); near(g.height(4,0),0,1e-7,"No resurrection when gravity decreases");
    });
    test("Piecewise gravity preserves velocity when raised and lowered",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,16,0); g.advance(1.2,1,2,t,1);
        g.advance(1.2,1,10,t,1); near(g.height(1,0),.7+std::sqrt(1.2)*.2-.04,1e-6,"Continuous position"); near(g.velocity(1,0),.4-std::sqrt(1.2),1e-6,"Velocity retained");
        g.advance(1.3,1,10,t,1); near(g.height(1,0),.57+std::sqrt(1.2)*.3,1e-6,"Analytic piecewise integration"); near(g.velocity(1,0),1.4-std::sqrt(1.2),1e-6,"New acceleration");
        g.advance(1.3,1,.1f,t,1); g.advance(1.4,1,.1f,t,1); near(g.height(1,0),.4295+std::sqrt(1.2)*.4,1e-6,"Small gravity does not brake existing fall"); near(g.velocity(1,0),1.41-std::sqrt(1.2),1e-6,"No velocity reset");
    });
    test("Gravity is world acceleration at every maximum height",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        for(float h:{1.0f,4.0f,15.0f}) { GridSimulation g; g.resize(16,16,0); g.advance(1.25,1,2,t,h); near(g.height(1,0)*h,.7*h+std::sqrt(1.2*h)*.25-.0625,2e-6,"World ballistic trajectory"); }
    });
    test("Long-lived falling point survives clock rebase",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        GridSimulation g; g.resize(16,32,0);
        for(int i=1;i<=600;++i) g.advance(i/60.0,1,.1f,t,15);
        near(g.height(10,0)*15,.7*15+std::sqrt(.9)*9-.5*.1*81,2e-5,"Nine seconds of motion across rebase");
        near(g.velocity(10,0)*15,.9-std::sqrt(.9),2e-6,"Velocity across rebase");
    });
    test("Changing gravity remains independent of FPS",[] {
        SpectrumTimeline t; SpectrumFrame f; f.bands=16; f.values.fill(1); t.add(f); f.time=100; t.add(f);
        for(int fps:{30,60,120,144}) {
            GridSimulation g; g.resize(16,32,0);
            for(int i=1;i<=fps*3;++i) {
                float gravity=i<=fps*3/2?.1f:i<=fps*2?1.0f:.2f;
                g.advance(double(i)/fps,1,gravity,t,1);
            }
            // Drop: .5*.1*.5^2 + (.05*.5 + .5*1*.5^2) + (.55*1 + .5*.2*1^2).
            near(g.height(3,0),.7+std::sqrt(.06)*2-.8125,3e-6,"Piecewise analytic result at every FPS");
            near(g.velocity(3,0),.75-std::sqrt(.06),3e-6,"Same velocity at every FPS");
        }
    });
    auto dir=std::filesystem::current_path()/"test-config"; std::filesystem::create_directories(dir);
    test("Rebound and integer scroll preferences",[&] {
        Settings s; near(s.bounce,.4,1e-7,"Default rebound"); s.speed=67.6f; s.validate(); near(s.speed,70,0,"Nearest ten columns");
        s.bounce=.65f; require(ConfigManager::save(dir/"rebound.json",s),"Save rebound"); auto r=ConfigManager::load(dir/"rebound.json"); near(r.bounce,.65,1e-6,"Rebound roundtrip"); near(r.speed,70,0,"Integer speed roundtrip");
    });
    test("Automatic spin preferences persist",[&] {
        Settings s; require(s.autoRotation==0,"Off by default"); s.autoRotation=75;
        require(ConfigManager::save(dir/"spin.json",s),"Save spin");
        near(ConfigManager::load(dir/"spin.json").autoRotation,75,0,"Restore spin speed");
        s.autoRotation=200; s.validate(); near(s.autoRotation,100,0,"Maximum speed");
        s.autoRotation=-1; s.validate(); near(s.autoRotation,0,0,"Minimum speed");
    });
    test("Compression setting defaults persists and clamps",[&] {
        Settings s; require(s.compression==0,"Default linear"); s.compression=.65f;
        require(ConfigManager::save(dir/"compression.json",s),"Save compression");
        near(ConfigManager::load(dir/"compression.json").compression,.65,1e-6,"Restore compression");
        s.compression=2; s.validate(); near(s.compression,1,0,"Upper bound");
        s.compression=-1; s.validate(); near(s.compression,0,0,"Lower bound");
    });
    test("Treble boost config persists and clamps",[&] {
        Settings s; near(s.trebleBoostDb,0,0,"Neutral default"); s.trebleBoostDb=18;
        require(ConfigManager::save(dir/"treble.json",s),"Save boost"); near(ConfigManager::load(dir/"treble.json").trebleBoostDb,18,0,"Restore boost");
        s.trebleBoostDb=100; s.validate(); near(s.trebleBoostDb,24,0,"Upper bound");
        s.trebleBoostDb=-1; s.validate(); near(s.trebleBoostDb,0,0,"Lower bound");
        s.trebleBoostDb=std::numeric_limits<float>::quiet_NaN(); s.validate(); near(s.trebleBoostDb,0,0,"Invalid default");
    });
    test("Smoothing switch and both 240 limits persist",[&] {
        Settings s; s.smoothingEnabled=false; s.speed=240; s.fpsLimit=240;
        require(ConfigManager::save(dir/"raw-240.json",s),"Save settings");
        auto restored=ConfigManager::load(dir/"raw-240.json");
        require(!restored.smoothingEnabled && restored.fpsLimit==240 && restored.speed==240,"Restore raw mode and 240 limits");
        s.speed=999; s.validate(); near(s.speed,240,1e-7,"Speed upper bound");
    });
    test("Legacy settings preserve preferences and default gravity",[&] {
        { std::ofstream f(dir/"legacy.json"); f<<R"({"bands":64,"transparency":75,"decay":7})"; }
        auto s=ConfigManager::load(dir/"legacy.json"); require(s.bands==64 && s.transparency==75 && !s.smoothingEnabled,"Legacy preferences retained; smoothing defaults off"); near(s.gravity,9.81,1e-6,"Safe new gravity default");
    });
    test("Gravity config bounds and invalid numeric values",[] {
        Settings s; s.gravity=-1; s.validate(); near(s.gravity,.1,1e-7,"Minimum");
        s.gravity=100; s.validate(); near(s.gravity,30,1e-7,"Maximum");
        s.gravity=std::numeric_limits<float>::quiet_NaN(); s.validate(); near(s.gravity,9.81,1e-6,"NaN default");
    });
    test("JSON full round trip",[&] { Settings s; s.bands=256; s.columns=64; s.transparency=83; s.low={.2f,.4f,.8f}; s.windowMode=2; s.fftSize=8192; s.inertia=.25f; s.gravity=12.5f; s.vsync=false; std::string err; require(ConfigManager::save(dir/"settings.json",s,&err),"Save"); auto r=ConfigManager::load(dir/"settings.json",&err); require(err.empty(),"No errors"); require(r.bands==256 && r.columns==64 && r.fftSize==8192 && r.windowMode==2 && !r.vsync,"Integer and boolean properties"); near(r.gravity,12.5,1e-6,"Gravity saved"); near(r.transparency,83,1e-6,"Transparency saved"); near(r.low[2],.8f,1e-6,"Palette saved"); });
    test("Malformed JSON safe defaults",[&] { { std::ofstream f(dir/"bad.json"); f<<"{invalid"; } std::string error; auto s=ConfigManager::load(dir/"bad.json",&error); require(!error.empty(),"Error reported"); require(s.bands==96 && s.columns==128,"Defaults loaded"); });
    test("Invalid values clamped",[&] { { std::ofstream f(dir/"invalid.json"); f<<R"({"bands":99999,"columns":-100,"speed":-3,"fftSize":12,"transparency":500,"zoom":0,"rotation":[0,0,0,0]})"; } auto s=ConfigManager::load(dir/"invalid.json"); require(s.bands==256 && s.columns==16 && s.fftSize==4096,"Safe bounds"); near(s.speed,10,1e-8,"Speed bound"); near(s.transparency,100,1e-8,"Alpha bound"); near(s.rotation[3],1,1e-8,"Quaternion normalized"); });
    test("Wrong JSON type safe defaults",[&] { { std::ofstream f(dir/"type.json"); f<<R"({"bands":"oops"})"; } auto s=ConfigManager::load(dir/"type.json"); require(s.bands==96,"Fallback on type error"); });
    test("Missing settings safe defaults",[&] { auto s=ConfigManager::load(dir/"does-not-exist.json"); require(s.bands==96 && !s.smoothingEnabled,"Default count and smoothing off"); });
    test("Explicit smoothing preference retained",[&] { Settings s; s.smoothingEnabled=true; require(ConfigManager::save(dir/"smooth.json",s),"Save enabled smoothing"); require(ConfigManager::load(dir/"smooth.json").smoothingEnabled,"Explicit enabled preference retained"); require(!Settings{}.smoothingEnabled,"Reset defaults disable smoothing"); });
    test("Nonfinite config normalized",[] { Settings s; s.speed=std::numeric_limits<float>::infinity(); s.zoom=std::numeric_limits<float>::quiet_NaN(); s.validate(); require(std::isfinite(s.speed) && std::isfinite(s.zoom),"Config finite"); });
    std::cout<<"\n"<<passed<<" passed, "<<failed<<" failed\n"; return failed?1:0;
}
