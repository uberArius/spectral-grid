#pragma once
#include "Config.h"
#include "WinUtil.h"
namespace sg {
class WindowManager {
public:
    ~WindowManager();
    void create(HINSTANCE instance,WNDPROC procedure,void* app,Settings& settings);
    void applyMode(Settings& settings,int mode);
    void toggleFullscreen(Settings& settings);
    void capturePlacement(Settings& settings);
    void topmost(bool enabled);
    HWND hwnd() const { return hwnd_; }
    int mode() const { return mode_; }
    bool changing() const { return changing_; }
private:
    HWND hwnd_=nullptr;
    int mode_=0;
    bool changing_=false;
};
}
