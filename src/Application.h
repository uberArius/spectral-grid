#pragma once
#include "WindowManager.h"
#include "Renderer.h"
#include "SettingsUI.h"
namespace sg {
class Application {
public:
    Application();
    ~Application();
    int run(HINSTANCE instance,bool smokeTest,int benchmarkSeconds=0);
private:
    static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam);
    LRESULT message(HWND hwnd,UINT message,WPARAM wparam,LPARAM lparam);
    void recreateRenderer();
    void save();
    Settings settings_;
    WindowManager window_;
    Renderer renderer_;
    Camera camera_;
    SettingsUI ui_;
    AudioCapture audio_;
    AudioStatus audioStatus_;
    SpectrumTimeline timeline_;
    GridSimulation grid_;
    std::filesystem::path configPath_;
    std::string notice_;
    bool ready_=false,imgui_=false,dxui_=false,running_=true,smoke_=false;
    unsigned pendingWidth_=0,pendingHeight_=0;
    float pendingDpi_=0;
    POINT lastMouse_{-1,-1};
};
}
