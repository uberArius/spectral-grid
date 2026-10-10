#pragma once
#include "Config.h"
#include <DirectXMath.h>
namespace sg {
class Camera {
public:
    void reset(Settings& s);
    void rotate(Settings& s,float fromX,float fromY,float toX,float toY,float width,float height,float dt);
    void update(Settings& s,float dt,bool dragging);
    void pan(Settings& s,float dx,float dy,float height);
    DirectX::XMMATRIX matrix(const Settings& s,float aspect) const;
    float distance(const Settings& s) const;
private:
    static void applyRotation(Settings& s,float spin,float tilt);
    float spinVelocity_=0,tiltVelocity_=0;
};
}
