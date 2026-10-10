#include "Application.h"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <windowsx.h>
#include <nlohmann/json.hpp>
#include <psapi.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <mmsystem.h>
#include <dwmapi.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
namespace sg {
namespace {
// A known-color window behind the visualizer makes the DWM transparency test
// independent of the user's desktop contents. It only exists in --smoke-test.
class TestBackdrop {
    HWND hwnd_=nullptr;
    HBRUSH brush_=nullptr;
public:
    ~TestBackdrop() { if(hwnd_) DestroyWindow(hwnd_); if(brush_) DeleteObject(brush_); }
    void placeBehind(HWND front) {
        if(!hwnd_) {
            brush_=CreateSolidBrush(RGB(24,80,120));
            WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.hbrBackground=brush_; wc.lpszClassName=L"SpectralGridTestBackdrop";
            RegisterClassW(&wc);
            hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"Spectral Grid test background",WS_POPUP,0,0,1,1,nullptr,nullptr,wc.hInstance,nullptr);
        }
        RECT r; GetWindowRect(front,&r);
        SetWindowPos(hwnd_,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        SetWindowPos(front,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(hwnd_,nullptr,TRUE); UpdateWindow(hwnd_);
    }
    COLORREF sampleBehind(HWND front,unsigned height) {
        DwmFlush(); POINT p{8,int(height)-8}; ClientToScreen(front,&p); HDC screen=GetDC(nullptr); COLORREF pixel=GetPixel(screen,p.x,p.y); ReleaseDC(nullptr,screen); return pixel;
    }
};
}
Application::Application():configPath_(ConfigManager::defaultPath()) { settings_=ConfigManager::load(configPath_,&notice_); if(!notice_.empty()) notice_="Invalid settings file - defaults restored."; }
Application::~Application() {
    ready_=false;
    audio_.stop();
    if(dxui_) ImGui_ImplDX11_Shutdown();
    if(imgui_) { ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); imgui_=false; }
    renderer_.shutdown();
}
void Application::save() { if(smoke_) return; window_.capturePlacement(settings_); std::string error; if(!ConfigManager::save(configPath_,settings_,&error)) notice_="Failed to save settings: "+error; }
void Application::recreateRenderer() {
    if(dxui_) { ImGui_ImplDX11_Shutdown(); dxui_=false; }
    renderer_.shutdown(); renderer_.initialize(window_.hwnd());
    if(!ImGui_ImplDX11_Init(renderer_.device(),renderer_.context())) throw std::runtime_error("ImGui DX11");
    dxui_=true; grid_.markAllDirty();
}
LRESULT CALLBACK Application::procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    return app?app->message(hwnd,msg,w,l):DefWindowProcW(hwnd,msg,w,l);
}
LRESULT Application::message(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(imgui_) ImGui_ImplWin32_WndProcHandler(hwnd,msg,w,l);
    switch(msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hwnd,&ps); EndPaint(hwnd,&ps); return 0; }
    case WM_SIZE: if(w!=SIZE_MINIMIZED) { pendingWidth_=LOWORD(l); pendingHeight_=HIWORD(l); } return 0;
    case WM_DPICHANGED: {
        pendingDpi_=float(HIWORD(w)); auto r=reinterpret_cast<const RECT*>(l);
        if(window_.mode()!=2) SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);
        return 0;
    }
    case WM_GETMINMAXINFO: { auto m=reinterpret_cast<MINMAXINFO*>(l); m->ptMinTrackSize={480,320}; return 0; }
    case WM_MOUSEMOVE: { POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)}; if(p.x!=lastMouse_.x || p.y!=lastMouse_.y) ui_.activity(clockSeconds()); lastMouse_=p; break; }
    case WM_MOUSEWHEEL: ui_.activity(clockSeconds()); break;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN:
        ui_.activity(clockSeconds());
        if(window_.mode()==1 && (GetKeyState(VK_MENU)&0x8000) && (!imgui_ || !ImGui::GetIO().WantCaptureMouse)) {
            ReleaseCapture(); SendMessageW(hwnd,WM_SYSCOMMAND,msg==WM_LBUTTONDOWN?SC_MOVE|HTCAPTION:SC_SIZE|WMSZ_BOTTOMRIGHT,0); return 0;
        }
        break;
    case WM_LBUTTONDBLCLK: if(ready_ && !ImGui::GetIO().WantCaptureMouse) camera_.reset(settings_); break;
    case WM_KEYDOWN:
        if(!ready_) break;
        if(w==VK_F11 && !(l&(1<<30))) { window_.toggleFullscreen(settings_); return 0; }
        if(w==VK_F1) { ui_.activity(clockSeconds()); return 0; }
        if(w==VK_ESCAPE) { if(window_.mode()==2) window_.toggleFullscreen(settings_); else ui_.hide(); return 0; }
        if(w=='R' && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().WantCaptureKeyboard) camera_.reset(settings_);
        ui_.activity(clockSeconds()); break;
    case WM_SYSCOMMAND: if((w&0xfff0)==SC_KEYMENU) return 0; break;
    case WM_CLOSE: save(); running_=false; return 0;
    case WM_DESTROY: running_=false; PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
int Application::run(HINSTANCE instance,bool smokeTest,int benchmarkSeconds) {
    smoke_=smokeTest || benchmarkSeconds>0;
    if(smoke_) { settings_=Settings{}; settings_.diagnostics=true; std::filesystem::create_directories("test-output"); }
    window_.create(instance,procedure,this,settings_);
    if(smoke_) ShowWindow(window_.hwnd(),SW_SHOW);
    renderer_.initialize(window_.hwnd());
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename=nullptr; ImGui::GetIO().LogFilename=nullptr; ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(window_.hwnd()); imgui_=true;
    ui_.initialize(float(GetDpiForWindow(window_.hwnd())));
    if(!ImGui_ImplDX11_Init(renderer_.device(),renderer_.context())) throw std::runtime_error("ImGui DX11 init"); dxui_=true;
    audio_.configure(settings_); audio_.start();
    double start=clockSeconds(),last=start,nextFrame=start,nextSave=start+3; float frameMs=0;
    grid_.resize(settings_.bands,settings_.columns,start-.08); ui_.activity(start); ready_=true;
    timeBeginPeriod(1); struct EndPeriod { ~EndPeriod() { timeEndPeriod(1); } } period;
    Handle timer(CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS));
    if(!timer.value) timer.value=CreateWaitableTimerW(nullptr,FALSE,nullptr);
    nlohmann::json report; int stage=-1; uint64_t frames=0; double renderSum=0; float peakRms=0;
    TestBackdrop backdrop;
    report["gpu"]=renderer_.adapterName();
    if(smokeTest) { renderer_.verifyGravity(); renderer_.verifyLight(); report["lightGpuValidation"]={{"passed",true}}; grid_.markAllDirty(); report["gravityGpuValidation"]={{"passed",true},{"cases",16},{"pointsPerCase",272},{"tolerance",2e-5}}; }
    bool appliedTopmost=settings_.topmost;
    double nextMemorySample=start+5; uint64_t occluded=0;
    FILETIME creation,exit,kernelStart,userStart;
    GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernelStart,&userStart);
    auto fileTime=[](FILETIME f) { return (uint64_t(f.dwHighDateTime)<<32)|f.dwLowDateTime; };
    PROCESS_MEMORY_COUNTERS memory{}; memory.cb=sizeof(memory); GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)); report["initialWorkingSet"]=memory.WorkingSetSize;
    while(running_) {
        MSG msg; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { if(msg.message==WM_QUIT) running_=false; TranslateMessage(&msg); DispatchMessageW(&msg); }
        if(!running_) break;
        double now=clockSeconds();
        SpectrumFrame spectrum; while(audio_.pop(spectrum)) timeline_.add(spectrum);
        if(IsIconic(window_.hwnd())) {
            grid_.advance(now-.08,settings_.speed,settings_.gravity,timeline_,settings_.flightHeight(),settings_.smoothingEnabled,settings_.bounce);
            MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE); continue;
        }
        if(now<nextFrame) {
            LARGE_INTEGER due; due.QuadPart=-std::max<LONGLONG>(1,LONGLONG((nextFrame-now)*1e7));
            if(timer.value && SetWaitableTimer(timer.value,&due,0,nullptr,nullptr,FALSE)) MsgWaitForMultipleObjectsEx(1,&timer.value,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            else MsgWaitForMultipleObjectsEx(0,nullptr,DWORD(std::max(1.0,(nextFrame-now)*1000)),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            continue;
        }
        float cameraDt=float(now-last);
        float dt=float(std::min(now-last,.1)); last=now; double frameStart=now;
        try {
            if(pendingWidth_ && pendingHeight_) { renderer_.resize(pendingWidth_,pendingHeight_); pendingWidth_=pendingHeight_=0; }
            if(pendingDpi_) { ImGui_ImplDX11_InvalidateDeviceObjects(); ui_.initialize(pendingDpi_); pendingDpi_=0; }
            audio_.status(audioStatus_); peakRms=std::max(peakRms,audioStatus_.rms);
            if(smokeTest) {
                int current=int(now-start);
                if(current!=stage) {
                    stage=current;
                    if(stage==1) { window_.applyMode(settings_,1); settings_.transparency=100; }
                    if(stage==2) { window_.applyMode(settings_,2); settings_.transparency=50; }
                    if(stage==3) { window_.applyMode(settings_,0); settings_.transparency=0; }
                    if(stage==4) { settings_.bands=256; settings_.columns=256; settings_.gravity=.1f; }
                    if(stage==5) { settings_.gravity=30; SetWindowPos(window_.hwnd(),nullptr,140,100,1000,700,SWP_NOZORDER); }
                    if(stage==6) { settings_.gravity=9.81f; recreateRenderer(); }
                    if(stage==7) { settings_.bands=96; settings_.columns=128; ui_.activity(now); }
                    if(stage==8) { settings_.transparency=100; ui_.hide(); }
                    if(stage==9) { settings_.transparency=0; ui_.hide(); }
                    if(stage==10) { settings_.trebleBoostDb=18; settings_.bands=256; settings_.columns=256; settings_.vsync=false; settings_.fpsLimit=240; settings_.speed=240; settings_.smoothingEnabled=false; }
                    if(stage==11) { settings_.fpsLimit=240; settings_.smoothingEnabled=true; }
                    settings_.topmost=true;
                    backdrop.placeBehind(window_.hwnd());
                }
            }
            ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
            auto actions=ui_.draw(settings_,audioStatus_,now,dt,frameMs,notice_);
            if(actions.resetAll) { settings_=Settings{}; camera_.reset(settings_); }
            if(actions.resetCamera) camera_.reset(settings_);
            settings_.validate();
            if(actions.save) { save(); if(notice_.empty()) notice_="Settings saved."; }
            if(settings_.windowMode!=window_.mode()) window_.applyMode(settings_,settings_.windowMode);
            if(appliedTopmost!=settings_.topmost) { window_.topmost(settings_.topmost); appliedTopmost=settings_.topmost; }
            auto& io=ImGui::GetIO(); bool rotating=false;
            if(!io.WantCaptureMouse && !(GetKeyState(VK_MENU)&0x8000)) {
                rotating=ImGui::IsMouseDragging(ImGuiMouseButton_Left,0);
                if(rotating) camera_.rotate(settings_,io.MousePos.x-io.MouseDelta.x,io.MousePos.y-io.MouseDelta.y,io.MousePos.x,io.MousePos.y,float(renderer_.width()),float(renderer_.height()),dt);
                if(ImGui::IsMouseDragging(ImGuiMouseButton_Right,0)) camera_.pan(settings_,io.MouseDelta.x,io.MouseDelta.y,float(renderer_.height()));
                if(io.MouseWheel) settings_.zoom=std::clamp(settings_.zoom*std::exp(-io.MouseWheel*.1f),.3f,4.0f);
            }
            camera_.update(settings_,cameraDt,rotating || io.WantCaptureMouse);
            if(pendingWidth_ && pendingHeight_) { renderer_.resize(pendingWidth_,pendingHeight_); pendingWidth_=pendingHeight_=0; }
            audio_.configure(settings_);
            if(grid_.bands()!=settings_.bands || grid_.columns()!=settings_.columns) grid_.resize(settings_.bands,settings_.columns,now-.08);
            grid_.advance(now-.08,settings_.speed,settings_.gravity,timeline_,settings_.flightHeight(),settings_.smoothingEnabled,settings_.bounce);
            renderer_.draw(settings_,grid_,camera_);
            if(smokeTest && stage>=0 && !report["stages"].contains(std::to_string(stage))) {
                float actual=renderer_.backgroundAlpha(),expected=1-settings_.transparency*.01f;
                report["stages"][std::to_string(stage)]={{"mode",settings_.windowMode},{"bands",settings_.bands},{"columns",settings_.columns},{"backgroundAlpha",actual},{"expectedAlpha",expected},{"passed",std::abs(actual-expected)<.01f}};
                auto pixels=renderer_.pixelStats();
                report["stages"][std::to_string(stage)]["gravity"]=settings_.gravity;
                report["stages"][std::to_string(stage)]["smoothingEnabled"]=settings_.smoothingEnabled;
                report["stages"][std::to_string(stage)]["trebleBoostDb"]=settings_.trebleBoostDb;
                report["stages"][std::to_string(stage)]["columnsPerSecond"]=settings_.speed;
                report["stages"][std::to_string(stage)]["fpsLimit"]=settings_.fpsLimit;
                report["stages"][std::to_string(stage)]["litPixels"]=pixels.litPixels;
                report["stages"][std::to_string(stage)]["premultipliedViolations"]=pixels.premultipliedViolations;
                report["stages"][std::to_string(stage)]["maxPointRed"]=pixels.maxRed;
                if(!pixels.litPixels || pixels.premultipliedViolations) throw std::runtime_error("Invalid points or premultiplied alpha");
                if(std::abs(actual-expected)>.01f) throw std::runtime_error("Invalid background alpha in GPU test");
            }
            ImGui::Render(); ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            if(smokeTest && (stage==7 || stage==9) && now-start>stage+.7 && !report.contains(stage==7?"menuScreenshot":"gridScreenshot")) {
                const char* name=stage==7?"menuScreenshot":"gridScreenshot"; std::string path=stage==7?"test-output/menu.bmp":"test-output/grid.bmp"; renderer_.screenshot(path); report[name]=path;
            }
            auto hr=renderer_.present(settings_.vsync);
            if(hr==DXGI_STATUS_OCCLUDED) { ++occluded; MsgWaitForMultipleObjectsEx(0,nullptr,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE); }
            if(hr==DXGI_ERROR_DEVICE_REMOVED || hr==DXGI_ERROR_DEVICE_RESET) { recreateRenderer(); notice_="Graphics device restored."; }
            else check(hr,"Present");
            if(smokeTest && (stage==1 || stage==2 || stage==8) && now-start>stage+.7 && !report["stages"][std::to_string(stage)].contains("composedPixel")) {
                auto color=backdrop.sampleBehind(window_.hwnd(),renderer_.height());
                auto& r=report["stages"][std::to_string(stage)];
                r["composedPixel"]={GetRValue(color),GetGValue(color),GetBValue(color)};
                float a=settings_.transparency*.01f;
                r["compositionPassed"]=std::abs(int(GetRValue(color))-24*a)<12 && std::abs(int(GetGValue(color))-80*a)<12 && std::abs(int(GetBValue(color))-120*a)<12;
            }
        } catch(const HrError& e) {
            if(e.code==DXGI_ERROR_DEVICE_REMOVED || e.code==DXGI_ERROR_DEVICE_RESET) recreateRenderer(); else throw;
        }
        frameMs=float((clockSeconds()-frameStart)*1000); renderSum+=frameMs; ++frames;
        if(smokeTest) {
            auto& stageReport=report["stages"][std::to_string(stage)];
            if(!stageReport.contains("firstFrame")) stageReport["firstFrame"]=now-start;
            stageReport["lastFrame"]=now-start;
            stageReport["frameCount"]=stageReport.value("frameCount",0)+1;
        }
        if(smoke_ && now>=nextMemorySample) {
            GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
            report["memorySamples"].push_back({{"seconds",now-start},{"workingSet",memory.WorkingSetSize},{"pagefile",memory.PagefileUsage}});
            nextMemorySample=now+5;
        }
        if(now>nextSave) { save(); nextSave=now+3; }
        nextFrame=settings_.fpsLimit?std::max(nextFrame+1.0/settings_.fpsLimit,now):now;
        if((smokeTest && now-start>=12) || (benchmarkSeconds>0 && now-start>=benchmarkSeconds)) running_=false;
    }
    if(smoke_) {
        if(smokeTest) for(auto& stageReport:report["stages"]) {
            double duration=stageReport.value("lastFrame",0.0)-stageReport.value("firstFrame",0.0);
            if(duration>0) stageReport["measuredFps"]=(stageReport.value("frameCount",1)-1)/duration;
        }
        GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory));
        report["finalWorkingSet"]=memory.WorkingSetSize; report["frames"]=frames; report["seconds"]=clockSeconds()-start;
        report["averageFps"]=frames/(clockSeconds()-start); report["averageFrameMs"]=renderSum/std::max<uint64_t>(frames,1);
        report["peakAudioRms"]=peakRms; report["audioDevice"]=audioStatus_.device; report["audioState"]=audioStatus_.state; report["fftMs"]=audioStatus_.fftMs;
        report["droppedSpectra"]=audioStatus_.dropped;
        report["occludedFrames"]=occluded;
        FILETIME kernelEnd,userEnd; GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernelEnd,&userEnd);
        report["cpuSeconds"]=double(fileTime(kernelEnd)+fileTime(userEnd)-fileTime(kernelStart)-fileTime(userStart))*1e-7;
        report["cpuPercentOfOneCore"]=double(report["cpuSeconds"])/(clockSeconds()-start)*100;
        std::ofstream(smokeTest?"test-output/integration.json":"test-output/benchmark.json")<<report.dump(2);
    }
    save(); return 0;
}
}
