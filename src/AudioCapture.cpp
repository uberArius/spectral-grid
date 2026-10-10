#include "AudioCapture.h"
#include "AudioSamples.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <avrt.h>
#include <cstring>
#include <memory>
#include <algorithm>
#include <cmath>
namespace sg {
namespace {
struct Format {
    unsigned channels=0,bytes=0,block=0,bits=0; bool floating=false;
    explicit Format(const WAVEFORMATEX* f) {
        channels=f->nChannels; bits=f->wBitsPerSample; bytes=(bits+7)/8; block=f->nBlockAlign;
        WORD tag=f->wFormatTag;
        if(tag==WAVE_FORMAT_EXTENSIBLE && f->cbSize>=22) {
            auto x=reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
            if(x->SubFormat==KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag=WAVE_FORMAT_IEEE_FLOAT;
            else if(x->SubFormat==KSDATAFORMAT_SUBTYPE_PCM) tag=WAVE_FORMAT_PCM;
        }
        floating=tag==WAVE_FORMAT_IEEE_FLOAT;
        if(!channels || channels>32 || block<channels*bytes || (floating?(bits!=32 && bits!=64):(tag!=WAVE_FORMAT_PCM || (bits!=8 && bits!=16 && bits!=24 && bits!=32))))
            throw std::runtime_error("Unsupported audio device format");
    }
    float mono(const BYTE* p) const { return mixAudioFrame(p,channels,bits,floating); }
};
std::wstring deviceId(IMMDevice* device) { LPWSTR p=nullptr; check(device->GetId(&p),"GetId"); std::wstring id=p; CoTaskMemFree(p); return id; }
}
AudioCapture::AudioCapture():stopEvent_(CreateEventW(nullptr,TRUE,FALSE,nullptr)) { if(!stopEvent_.value) throw std::runtime_error("Cannot create audio event"); }
AudioCapture::~AudioCapture() { stop(); }
void AudioCapture::configure(const Settings& settings) { std::lock_guard lock(mutex_); settings_=settings; }
void AudioCapture::start() { if(!thread_.joinable()) { ResetEvent(stopEvent_.value); thread_=std::thread(&AudioCapture::run,this); } }
void AudioCapture::stop() { SetEvent(stopEvent_.value); if(thread_.joinable()) thread_.join(); }
bool AudioCapture::status(AudioStatus& output) { std::unique_lock lock(mutex_,std::try_to_lock); if(!lock) return false; output=status_; return true; }
bool AudioCapture::pop(SpectrumFrame& frame) {
    size_t r=read_.load(std::memory_order_relaxed); if(r==write_.load(std::memory_order_acquire)) return false;
    frame=queue_[r]; read_.store((r+1)%queue_.size(),std::memory_order_release); return true;
}
void AudioCapture::publish(SpectrumFrame& frame) {
    frame.time=std::max(frame.time,lastPublished_+1e-6); lastPublished_=frame.time;
    size_t w=write_.load(std::memory_order_relaxed),next=(w+1)%queue_.size();
    if(next!=read_.load(std::memory_order_acquire)) { queue_[w]=frame; write_.store(next,std::memory_order_release); }
    else { std::lock_guard lock(mutex_); ++status_.dropped; }
}
void AudioCapture::run() {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(hr)) { std::lock_guard lock(mutex_); status_.state="COM initialization failed"; return; }
    DWORD task=0; HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Audio",&task);
    while(WaitForSingleObject(stopEvent_.value,0)!=WAIT_OBJECT_0) {
        try { session(); }
        catch(const std::exception& e) { std::lock_guard lock(mutex_); status_.state=std::string("Reconnecting: ")+e.what(); status_.rms=0; }
        if(WaitForSingleObject(stopEvent_.value,500)==WAIT_OBJECT_0) break;
    }
    if(mmcss) AvRevertMmThreadCharacteristics(mmcss); CoUninitialize();
}
void AudioCapture::session() {
    ComPtr<IMMDeviceEnumerator> enumerator; check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator)),"MMDeviceEnumerator");
    ComPtr<IMMDevice> device; check(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device),"No default output device");
    auto id=deviceId(device.Get());
    ComPtr<IPropertyStore> props; check(device->OpenPropertyStore(STGM_READ,&props),"OpenPropertyStore");
    PROPVARIANT value; PropVariantInit(&value); props->GetValue(PKEY_Device_FriendlyName,&value);
    { std::lock_guard lock(mutex_); status_.device=value.vt==VT_LPWSTR?utf8(value.pwszVal):"Windows output"; }
    PropVariantClear(&value);
    ComPtr<IAudioClient> client; check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,&client),"IAudioClient");
    WAVEFORMATEX* raw=nullptr; check(client->GetMixFormat(&raw),"GetMixFormat");
    std::unique_ptr<WAVEFORMATEX,decltype(&CoTaskMemFree)> format(raw,&CoTaskMemFree); Format decoder(raw);
    check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK,1000000,0,raw,nullptr),"WASAPI Loopback");
    ComPtr<IAudioCaptureClient> capture; check(client->GetService(IID_PPV_ARGS(&capture)),"IAudioCaptureClient");
    SpectrumAnalyzer analyzer; Settings current;
    { std::lock_guard lock(mutex_); current=settings_; status_.sampleRate=int(raw->nSamplesPerSec); status_.channels=raw->nChannels; status_.state="Capturing (WASAPI Loopback)"; }
    analyzer.configure(int(raw->nSamplesPerSec),current);
    check(client->Start(),"Audio Start");
    struct StopClient { IAudioClient* p; ~StopClient() { p->Stop(); } } stop{client.Get()};
    double nextCheck=0,lastPacket=clockSeconds(),silenceClock=lastPacket,lastSettings=0;
    SpectrumFrame frame;
    while(WaitForSingleObject(stopEvent_.value,8)!=WAIT_OBJECT_0) {
        double now=clockSeconds();
        if(now>nextCheck) {
            nextCheck=now+.5; ComPtr<IMMDevice> active;
            check(enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&active),"Device disconnected");
            if(deviceId(active.Get())!=id) return;
            DWORD state=0; check(device->GetState(&state),"GetState"); if(!(state&DEVICE_STATE_ACTIVE)) return;
        }
        if(now-lastSettings>.05) { { std::lock_guard lock(mutex_); current=settings_; } analyzer.configure(int(raw->nSamplesPerSec),current); lastSettings=now; }
        UINT32 packet=0; check(capture->GetNextPacketSize(&packet),"GetNextPacketSize");
        bool received=false;
        while(packet) {
            BYTE* data=nullptr; UINT32 count=0; DWORD flags=0; UINT64 position=0,qpc=0;
            check(capture->GetBuffer(&data,&count,&flags,&position,&qpc),"GetBuffer");
            struct Release { IAudioCaptureClient* p; UINT32 count; ~Release() { if(p) p->ReleaseBuffer(count); } } release{capture.Get(),count};
            if(flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) analyzer.reset();
            double packetStart=(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)?now-double(count)/raw->nSamplesPerSec:double(qpc)*1e-7;
            for(UINT32 i=0;i<count;++i) {
                float v=(flags&AUDCLNT_BUFFERFLAGS_SILENT)?0:decoder.mono(data+size_t(i)*decoder.block);
                if(analyzer.push(v,frame)) { frame.time=packetStart+double(i)/raw->nSamplesPerSec; publish(frame); }
            }
            received=true;
            // Release the packet before asking for the next one.
            check(capture->ReleaseBuffer(count),"ReleaseBuffer"); release.count=0; release.p=nullptr;
            check(capture->GetNextPacketSize(&packet),"GetNextPacketSize");
        }
        if(received) { lastPacket=now; silenceClock=now; }
        else if(now-lastPacket>.05) {
            int count=std::min(int((now-silenceClock)*raw->nSamplesPerSec),int(raw->nSamplesPerSec/10));
            for(int i=0;i<count;++i) if(analyzer.push(0,frame)) { frame.time=silenceClock+double(i)/raw->nSamplesPerSec; publish(frame); }
            silenceClock=now;
        }
        { std::lock_guard lock(mutex_); status_.rms=frame.rms; status_.fftMs=frame.fftMs; }
    }
}
}
