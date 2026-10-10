#include "SettingsUI.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
namespace sg {
void SettingsUI::initialize(float dpi) {
    scale_=dpi/96;
    auto& style=ImGui::GetStyle(); style=ImGuiStyle(); ImGui::StyleColorsDark();
    style.WindowRounding=12; style.FrameRounding=5; style.GrabRounding=5; style.ScrollbarRounding=6;
    style.WindowPadding={20,18}; style.FramePadding={9,6}; style.ItemSpacing={8,10}; style.WindowBorderSize=1;
    auto* c=style.Colors; c[ImGuiCol_WindowBg]={.055f,.06f,.075f,.97f}; c[ImGuiCol_Border]={.22f,.24f,.29f,.5f};
    c[ImGuiCol_FrameBg]={.12f,.13f,.16f,1}; c[ImGuiCol_FrameBgHovered]={.2f,.16f,.18f,1}; c[ImGuiCol_FrameBgActive]={.28f,.13f,.17f,1};
    c[ImGuiCol_SliderGrab]={.92f,.22f,.3f,1}; c[ImGuiCol_SliderGrabActive]={1,.4f,.4f,1};
    c[ImGuiCol_CheckMark]={1,.3f,.36f,1}; c[ImGuiCol_Header]={.2f,.12f,.15f,.7f}; c[ImGuiCol_HeaderHovered]={.36f,.14f,.2f,.8f}; c[ImGuiCol_HeaderActive]={.5f,.16f,.23f,1};
    c[ImGuiCol_Button]={.27f,.12f,.17f,1}; c[ImGuiCol_ButtonHovered]={.46f,.17f,.24f,1}; c[ImGuiCol_ButtonActive]={.65f,.19f,.27f,1};
    style.ScaleAllSizes(scale_);
    auto& io=ImGui::GetIO(); io.Fonts->Clear();
    static const ImWchar ranges[]={0x20,0xFF,0x100,0x17F,0};
    wchar_t win[MAX_PATH]; GetWindowsDirectoryW(win,MAX_PATH); auto path=utf8((std::wstring(win)+L"\\Fonts\\segoeui.ttf").c_str());
    ImFontConfig config; config.OversampleH=2; config.OversampleV=1;
    if(!io.Fonts->AddFontFromFileTTF(path.c_str(),17*scale_,&config,ranges)) io.Fonts->AddFontDefault();
    io.Fonts->Build();
}
UIActions SettingsUI::draw(Settings& s,const AudioStatus& audio,double now,float dt,float frameMs,const std::string& notice) {
    UIActions action;
    if(ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) { lastActivity_=now; hidden_=false; }
    float target=!hidden_ && now-lastActivity_<3?1.0f:0.0f;
    alpha_+=(target-alpha_)*(1-std::exp(-dt*9));
    if(alpha_>.005f) {
        auto& io=ImGui::GetIO();
        ImGui::SetNextWindowPos({18*scale_,18*scale_},ImGuiCond_Always);
        ImGui::SetNextWindowSize({std::min(420*scale_,io.DisplaySize.x-36*scale_),std::max(150.0f,io.DisplaySize.y-36*scale_)},ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha,alpha_);
        auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;
        if(alpha_<.05f) flags|=ImGuiWindowFlags_NoInputs;
        ImGui::Begin("Spectral Grid",nullptr,flags);
        ImGui::TextColored({1,.28f,.35f,1},"S P E C T R A L   G R I D");
        ImGui::TextDisabled("SOUND IN SPACE  /  1.0"); ImGui::Separator();
        ImGui::TextWrapped("%s",audio.device.c_str());
        ImGui::TextDisabled("%d Hz  /  %d channels",audio.sampleRate,audio.channels);
        ImGui::PushItemWidth(-145*scale_);
        if(ImGui::CollapsingHeader("Grid",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderInt("Bands",&s.bands,16,256);
            ImGui::SliderInt("Columns",&s.columns,16,256);
            ImGui::SliderFloat("Point size",&s.pointSize,.006f,.09f,"%.3f");
            ImGui::SliderFloat("Spacing",&s.spacing,.05f,.4f,"%.2f");
            ImGui::SliderFloat("Height",&s.maxHeight,.1f,15,"%.1f");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Flight height: %.1f units (3x multiplier)",s.flightHeight());
            int speedStep=int(std::round(s.speed/10));
            std::string speedLabel=std::to_string(speedStep*10)+" cols/s";
            if(ImGui::SliderInt("Scroll speed",&speedStep,1,24,speedLabel.c_str(),ImGuiSliderFlags_NoInput)) s.speed=float(speedStep*10);
            float bouncePercent=s.bounce*100;
            if(ImGui::SliderFloat("Bounce height",&bouncePercent,0,100,"%.0f %%")) s.bounce=bouncePercent*.01f;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("One bounce to this fraction of the original height. 0%%: no bounce.");
            ImGui::SliderFloat("Gravity",&s.gravity,.1f,30,"%.2f",ImGuiSliderFlags_Logarithmic);
        }
        if(ImGui::CollapsingHeader("Audio")) {
            ImGui::TextWrapped("%s",audio.state.c_str());
            ImGui::SliderFloat("Sensitivity",&s.sensitivity,.1f,10,"%.2f",ImGuiSliderFlags_Logarithmic);
            float compressionPercent=s.compression*100;
            if(ImGui::SliderFloat("Compression",&compressionPercent,0,100,"%.0f %%")) s.compression=compressionPercent*.01f;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("0%%: height proportional to amplitude.\n100%%: logarithmic compression.\nNormalization and height clipping can still equalize bands.");
            ImGui::SliderFloat("Treble boost",&s.trebleBoostDb,0,24,"%.1f dB");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("0 dB: no correction. Gain increases with frequency\non a logarithmic scale, reaching this value at the top of the range.");
            ImGui::SliderFloat("Noise gate",&s.gate,-100,-15,"%.0f dB");
            ImGui::Checkbox("Spectrum smoothing",&s.smoothingEnabled);
            ImGui::BeginDisabled(!s.smoothingEnabled);
            ImGui::SliderFloat("Attack",&s.attack,.001f,.5f,"%.3f s",ImGuiSliderFlags_Logarithmic);
            ImGui::SliderFloat("Release",&s.smoothing,.01f,2,"%.2f s",ImGuiSliderFlags_Logarithmic);
            ImGui::EndDisabled();
            if(!s.smoothingEnabled) ImGui::TextWrapped("No blending between FFT results. Normalization, if enabled, uses only the current window.");
            ImGui::Checkbox("Auto normalization",&s.normalize);
            int index=s.fftSize==2048?0:s.fftSize==4096?1:s.fftSize==8192?2:3; if(ImGui::Combo("FFT size",&index,"2048\0" "4096\0" "8192\0" "16384\0")) s.fftSize=2048<<index;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Larger FFTs improve bass resolution but respond more slowly.\nSpectrum updates roughly 480 times/s regardless of FFT size.");
        }
        if(ImGui::CollapsingHeader("Colors")) {
            ImGui::ColorEdit3("Base",s.low.data()); ImGui::ColorEdit3("Peak",s.high.data()); ImGui::ColorEdit3("Background",s.background.data());
            float midpointPercent=s.colorMidpoint*100;
            if(ImGui::SliderFloat("Color midpoint",&midpointPercent,5,95,"%.1f %%")) s.colorMidpoint=midpointPercent*.01f;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Target height where color reaches half the transition.\n80%%: first half below 80%% height, second half in the remaining 20%%.");
            ImGui::SliderFloat("Brightness",&s.brightness,.1f,4,"%.2f");
            ImGui::Checkbox("Glow",&s.glowEnabled); ImGui::SliderFloat("Glow strength",&s.glow,0,1,"%.2f");
        }
        if(ImGui::CollapsingHeader("Camera")) {
            if(ImGui::Button("Reset camera")) action.resetCamera=true;
            ImGui::SliderFloat("Automatic rotation",&s.autoRotation,0,100,"%.1f %%");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rotation in the grid plane. 0%%: off.\n100%%: one turn every 5 seconds; 50%%: every 10 seconds.");
            ImGui::SliderFloat("Rotation sensitivity",&s.rotationSpeed,.1f,3,"%.2f");
            ImGui::SliderFloat("Zoom",&s.zoom,.3f,4,"%.2f");
            ImGui::SliderFloat("Pan X",&s.panX,-50,50,"%.1f"); ImGui::SliderFloat("Pan Y",&s.panY,-50,50,"%.1f");
            ImGui::SliderFloat("Inertia",&s.inertia,0,.8f,"%.2f s");
            ImGui::TextWrapped("Left-drag horizontally: spin in the grid plane\nLeft-drag vertically: tilt\nRight-drag: pan / Wheel: zoom");
        }
        if(ImGui::CollapsingHeader("Window")) {
            ImGui::Combo("Mode",&s.windowMode,"Windowed\0" "Borderless\0" "Fullscreen\0");
            ImGui::SliderFloat("Transparency",&s.transparency,0,100,"%.0f %%");
            ImGui::Checkbox("Always on top",&s.topmost);
            ImGui::TextWrapped("F11: fullscreen\nAlt + left-drag: move borderless window\nAlt + right-drag: resize borderless window");
        }
        if(ImGui::CollapsingHeader("Performance")) {
            ImGui::Checkbox("VSync",&s.vsync);
            const int limits[]={30,60,120,144,240,0}; int index=0; for(int i=0;i<6;++i) if(s.fpsLimit==limits[i]) index=i;
            if(ImGui::Combo("FPS limit",&index,"30\0" "60\0" "120\0" "144\0" "240\0" "Unlimited\0")) s.fpsLimit=limits[index];
            ImGui::Checkbox("Extra effects",&s.effects); ImGui::Checkbox("Diagnostics",&s.diagnostics);
        }
        if(ImGui::CollapsingHeader("General")) {
            if(ImGui::Button("Save settings")) action.save=true;
            if(ImGui::Button("Restore defaults")) action.resetAll=true;
            ImGui::TextWrapped("Spectral Grid 1.0 by uberArius\nApache-2.0 / github.com/uberArius/spectral-grid\nC++20 / Direct3D 11 / WASAPI\nDear ImGui / nlohmann JSON: MIT");
            ImGui::TextWrapped("Protected streams and apps using a different output device may not be captured.");
        }
        ImGui::PopItemWidth(); ImGui::Separator(); ImGui::TextDisabled("F1: menu   /   R: reset   /   Esc: hide");
        if(!notice.empty()) ImGui::TextWrapped("%s",notice.c_str());
        ImGui::End(); ImGui::PopStyleVar();
    }
    if(s.diagnostics) {
        auto& io=ImGui::GetIO(); ImGui::SetNextWindowPos({io.DisplaySize.x-16*scale_,16*scale_},ImGuiCond_Always,{1,0});
        ImGui::SetNextWindowBgAlpha(.9f);
        ImGui::Begin("Diagnostics",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
        ImGui::Text("%.1f FPS  |  %.2f ms",io.Framerate,frameMs);
        ImGui::Text("Points: %d x %d = %d",s.bands,s.columns,s.bands*s.columns);
        ImGui::Text("FFT: %.3f ms  |  RMS: %.1f dB",audio.fftMs,20*std::log10(std::max(audio.rms,1e-9f)));
        ImGui::Text("%s",audio.device.c_str()); ImGui::Text("%s",audio.state.c_str());
        ImGui::Text("Dropped spectra: %llu",static_cast<unsigned long long>(audio.dropped)); ImGui::End();
    }
    return action;
}
}
