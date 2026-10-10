#include "WindowManager.h"
#include <algorithm>
namespace sg {
WindowManager::~WindowManager() { if(hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); }
void WindowManager::create(HINSTANCE instance,WNDPROC procedure,void* app,Settings& s) {
    WNDCLASSEXW wc{sizeof(wc)}; wc.style=CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS; wc.lpfnWndProc=procedure; wc.hInstance=instance; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hIcon=LoadIcon(instance,MAKEINTRESOURCE(101)); wc.hIconSm=wc.hIcon; wc.lpszClassName=L"SpectralGridWindow";
    if(!RegisterClassExW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) throw std::runtime_error("RegisterClassEx");
    RECT rect{s.windowX,s.windowY,s.windowX+s.windowWidth,s.windowY+s.windowHeight};
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor);
    int width=std::min(s.windowWidth,int(monitor.rcWork.right-monitor.rcWork.left)),height=std::min(s.windowHeight,int(monitor.rcWork.bottom-monitor.rcWork.top));
    s.windowX=std::clamp(s.windowX,int(monitor.rcWork.left),int(monitor.rcWork.right)-width); s.windowY=std::clamp(s.windowY,int(monitor.rcWork.top),int(monitor.rcWork.bottom)-height);
    hwnd_=CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"Spectral Grid",WS_OVERLAPPEDWINDOW,s.windowX,s.windowY,width,height,nullptr,nullptr,instance,app);
    if(!hwnd_) throw std::runtime_error("CreateWindowEx");
    int requested=s.windowMode; mode_=0;
    if(requested) applyMode(s,requested);
    ShowWindow(hwnd_,s.maximized && requested==0?SW_SHOWMAXIMIZED:SW_SHOW); UpdateWindow(hwnd_); topmost(s.topmost);
}
void WindowManager::capturePlacement(Settings& s) {
    if(changing_ || mode_==2 || !hwnd_ || IsIconic(hwnd_)) return;
    s.maximized=IsZoomed(hwnd_)!=FALSE;
    RECT r;
    if(!s.maximized) GetWindowRect(hwnd_,&r);
    else {
        WINDOWPLACEMENT placement{sizeof(placement)}; GetWindowPlacement(hwnd_,&placement); r=placement.rcNormalPosition;
        MONITORINFO m{sizeof(m)}; GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&m);
        OffsetRect(&r,m.rcWork.left-m.rcMonitor.left,m.rcWork.top-m.rcMonitor.top);
    }
    s.windowX=r.left; s.windowY=r.top; s.windowWidth=r.right-r.left; s.windowHeight=r.bottom-r.top;
}
void WindowManager::applyMode(Settings& s,int mode) {
    mode=std::clamp(mode,0,2); if(mode==mode_) { s.windowMode=mode_; return; }
    capturePlacement(s); bool wasMax=s.maximized; changing_=true;
    if(mode==2) s.previousMode=mode_;
    if(IsZoomed(hwnd_) || IsIconic(hwnd_)) ShowWindow(hwnd_,SW_RESTORE);
    mode_=mode; s.windowMode=mode;
    SetWindowLongPtrW(hwnd_,GWL_STYLE,mode==0?WS_OVERLAPPEDWINDOW|WS_VISIBLE:WS_POPUP|WS_VISIBLE);
    RECT r{s.windowX,s.windowY,s.windowX+s.windowWidth,s.windowY+s.windowHeight};
    if(mode==2) { MONITORINFO m{sizeof(m)}; GetMonitorInfoW(MonitorFromWindow(hwnd_,MONITOR_DEFAULTTONEAREST),&m); r=m.rcMonitor; }
    SetWindowPos(hwnd_,s.topmost?HWND_TOPMOST:HWND_NOTOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_FRAMECHANGED|SWP_NOACTIVATE);
    if(mode==0 && wasMax) ShowWindow(hwnd_,SW_MAXIMIZE);
    s.maximized=wasMax; changing_=false;
}
void WindowManager::toggleFullscreen(Settings& s) { applyMode(s,mode_==2?s.previousMode:2); }
void WindowManager::topmost(bool enabled) { SetWindowPos(hwnd_,enabled?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE); }
}
