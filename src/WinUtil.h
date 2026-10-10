#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <stdexcept>
#include <string>
#include <cstdio>
namespace sg {
using Microsoft::WRL::ComPtr;
class HrError:public std::runtime_error {
public:
    HRESULT code;
    HrError(HRESULT hr,const char* where):std::runtime_error(message(hr,where)),code(hr) {}
private:
    static std::string message(HRESULT hr,const char* where) { char hex[32]; sprintf_s(hex," (0x%08lX)",static_cast<unsigned long>(hr)); return std::string(where)+hex; }
};
inline void check(HRESULT hr,const char* where) { if(FAILED(hr)) throw HrError(hr,where); }
inline double clockSeconds() { static const double frequency=[] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return double(f.QuadPart); }(); LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart/frequency; }
inline std::string utf8(const wchar_t* text) { if(!text) return {}; int n=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr); std::string s(n,'\0'); if(n) WideCharToMultiByte(CP_UTF8,0,text,-1,s.data(),n,nullptr,nullptr); if(!s.empty()) s.pop_back(); return s; }
class Handle {
public:
    HANDLE value=nullptr;
    explicit Handle(HANDLE h=nullptr):value(h) {}
    ~Handle() { if(value) CloseHandle(value); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
}
