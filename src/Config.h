#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace sg {
struct Settings {
    int bands=96, columns=128, fftSize=4096;
    float spacing=0.14f, pointSize=0.026f, maxHeight=4.0f;
    float speed=20.0f, gravity=9.81f, bounce=.4f, sensitivity=1.0f;
    float gate=-65.0f, attack=0.025f, smoothing=0.18f;
    float trebleBoostDb=0.0f, compression=0.0f;
    bool normalize=false, smoothingEnabled=false;
    std::array<float,3> low{1,0,0}, high{1,1,0}, background{0,0,0};
    float colorMidpoint=0.6363636f;
    float brightness=1.0f, glow=0.32f, transparency=0.0f;
    bool glowEnabled=true, effects=true;
    std::array<float,4> rotation{-0.350848f,-0.084117f,-0.031661f,0.932108f};
    float zoom=1.0f, panX=0, panY=0, rotationSpeed=1.0f, inertia=0.12f, autoRotation=0.0f;
    int windowMode=0, previousMode=0, windowX=100, windowY=100, windowWidth=1280, windowHeight=800;
    bool maximized=false, topmost=false, vsync=true, diagnostics=false;
    int fpsLimit=60;
    float flightHeight() const { return 3.0f*maxHeight; }
    void validate();
};
class ConfigManager {
public:
    static std::filesystem::path defaultPath();
    static Settings load(const std::filesystem::path& path, std::string* warning=nullptr);
    static bool save(const std::filesystem::path& path,const Settings& settings,std::string* error=nullptr);
};
}
