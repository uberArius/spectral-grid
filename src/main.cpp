#include "Application.h"
#include <shellapi.h>
#include <memory>
#include <fstream>
#include <algorithm>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    bool smoke=false; int benchmark=0,argc=0; auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int i=1;i<argc;++i) { if(std::wstring(args[i])==L"--smoke-test") smoke=true; if(std::wstring(args[i])==L"--benchmark" && i+1<argc) benchmark=std::clamp(_wtoi(args[++i]),1,3600); }
    LocalFree(args);
    HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED); int result=1;
    try { auto app=std::make_unique<sg::Application>(); result=app->run(instance,smoke,benchmark); }
    catch(const std::exception& e) {
        if(smoke || benchmark) { std::filesystem::create_directories("test-output"); std::ofstream("test-output/error.txt")<<e.what(); }
        else { std::string error=std::string("Failed to start Spectral Grid:\n\n")+e.what(); int n=MultiByteToWideChar(CP_UTF8,0,error.c_str(),-1,nullptr,0); std::wstring wide(n,L'\0'); MultiByteToWideChar(CP_UTF8,0,error.c_str(),-1,wide.data(),n); MessageBoxW(nullptr,wide.c_str(),L"Spectral Grid",MB_OK|MB_ICONERROR); }
    }
    if(SUCCEEDED(com)) CoUninitialize(); return result;
}
