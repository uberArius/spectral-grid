#pragma once
#include "SpectrumAnalyzer.h"
#include "WinUtil.h"
#include <atomic>
#include <thread>
#include <mutex>
namespace sg {
struct AudioStatus { std::string device="Waiting for device", state="Starting"; int sampleRate=0, channels=0; float rms=0,fftMs=0; uint64_t dropped=0; };
class AudioCapture {
public:
    AudioCapture();
    ~AudioCapture();
    void configure(const Settings& settings);
    void start();
    void stop();
    bool pop(SpectrumFrame& frame);
    bool status(AudioStatus& output);
private:
    void run();
    void session();
    void publish(SpectrumFrame& frame);
    Settings settings_;
    AudioStatus status_;
    std::mutex mutex_;
    Handle stopEvent_;
    std::thread thread_;
    std::array<SpectrumFrame,1024> queue_{};
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
    double lastPublished_=0;
};
}
