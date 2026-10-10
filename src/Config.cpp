#include "Config.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif
namespace sg {
using nlohmann::json;
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, bands, columns, fftSize, spacing, pointSize, maxHeight, speed, gravity, bounce, sensitivity, trebleBoostDb, compression, gate, attack, smoothing, normalize, smoothingEnabled, low, high, background, colorMidpoint, brightness, glow, transparency, glowEnabled, effects, rotation, zoom, panX, panY, rotationSpeed, inertia, autoRotation, windowMode, previousMode, windowX, windowY, windowWidth, windowHeight, maximized, topmost, vsync, diagnostics, fpsLimit)
static void limit(float& v,float lo,float hi,float fallback) { v=std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
void Settings::validate() {
    bands=std::clamp(bands,16,256); columns=std::clamp(columns,16,256);
    if(fftSize!=2048 && fftSize!=4096 && fftSize!=8192 && fftSize!=16384) fftSize=4096;
    limit(spacing,.05f,.4f,.14f); limit(pointSize,.006f,.09f,.026f); limit(maxHeight,.1f,15,4);
    limit(speed,10,240,20); speed=std::round(speed/10)*10; limit(bounce,0,1,.4f); limit(gravity,.1f,30,9.81f); limit(sensitivity,.1f,10,1);
    limit(trebleBoostDb,0,24,0); limit(compression,0,1,0);
    limit(gate,-100,-15,-65); limit(attack,.001f,.5f,.025f); limit(smoothing,.01f,2,.18f);
    for(auto* c:{&low,&high,&background}) for(float& v:*c) limit(v,0,1,0);
    limit(colorMidpoint,.05f,.95f,.6363636f);
    limit(brightness,.1f,4,1); limit(glow,0,1,.32f); limit(transparency,0,100,0);
    float norm=0; for(float& v:rotation) { limit(v,-1,1,0); norm+=v*v; }
    if(norm<.001f) rotation={0,0,0,1}; else for(float& v:rotation) v/=std::sqrt(norm);
    limit(zoom,.3f,4,1); limit(panX,-50,50,0); limit(panY,-50,50,0);
    limit(rotationSpeed,.1f,3,1); limit(inertia,0,.8f,.12f); limit(autoRotation,0,100,0);
    windowMode=std::clamp(windowMode,0,2); previousMode=std::clamp(previousMode,0,1);
    windowWidth=std::clamp(windowWidth,640,7680); windowHeight=std::clamp(windowHeight,400,4320);
    windowX=std::clamp(windowX,-65536,65536); windowY=std::clamp(windowY,-65536,65536);
    if(fpsLimit!=0 && fpsLimit!=30 && fpsLimit!=60 && fpsLimit!=120 && fpsLimit!=144 && fpsLimit!=240) fpsLimit=60;
}
std::filesystem::path ConfigManager::defaultPath() {
#ifdef _WIN32
    wchar_t* value=nullptr; size_t count=0; _wdupenv_s(&value,&count,L"LOCALAPPDATA");
    std::filesystem::path base=value?value:L"."; free(value);
#else
    std::filesystem::path base=std::getenv("HOME")?std::getenv("HOME"):".";
#endif
    return base/"SpectralGrid"/"settings.json";
}
Settings ConfigManager::load(const std::filesystem::path& path,std::string* warning) {
    try { std::ifstream f(path); if(!f) return {}; auto s=json::parse(f).get<Settings>(); s.validate(); return s; }
    catch(const std::exception& e) { if(warning) *warning=e.what(); return {}; }
}
bool ConfigManager::save(const std::filesystem::path& path,const Settings& settings,std::string* error) {
    try {
        auto s=settings; s.validate(); if(path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        auto tmp=path; tmp+=".tmp";
        { std::ofstream f(tmp,std::ios::binary|std::ios::trunc); f.exceptions(std::ios::badbit|std::ios::failbit); f<<json(s).dump(2)<<'\n'; f.flush(); }
#ifdef _WIN32
        if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace settings file");
#else
        std::filesystem::rename(tmp,path);
#endif
        return true;
    } catch(const std::exception& e) { if(error) *error=e.what(); return false; }
}
}
