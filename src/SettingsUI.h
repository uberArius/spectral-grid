#pragma once
#include "Config.h"
#include "AudioCapture.h"
#include <string>
namespace sg {
struct UIActions { bool resetCamera=false,resetAll=false,save=false; };
class SettingsUI {
public:
    void initialize(float dpi);
    void activity(double now) { lastActivity_=now; hidden_=false; }
    void hide() { hidden_=true; }
    UIActions draw(Settings& s,const AudioStatus& audio,double now,float dt,float frameMs,const std::string& notice);
    bool visible() const { return alpha_>.01f; }
private:
    double lastActivity_=0;
    float alpha_=1,scale_=1;
    bool hidden_=false;
};
}
