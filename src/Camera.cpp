#include "Camera.h"
#include <algorithm>
#include <cmath>
namespace sg {
using namespace DirectX;
void Camera::reset(Settings& s) { Settings defaults; s.rotation=defaults.rotation; s.zoom=1; s.panX=s.panY=0; spinVelocity_=tiltVelocity_=0; }
void Camera::applyRotation(Settings& s,float spin,float tilt) {
    auto q=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
    // Local Y is perpendicular to the grid's XZ plane. Apply spin BEFORE its
    // existing orientation, so even a tilted grid turns within its own plane.
    q=XMQuaternionMultiply(XMQuaternionRotationAxis(XMVectorSet(0,1,0,0),spin),q);
    // Vertical dragging tilts around screen-right, independently of local spin.
    q=XMQuaternionMultiply(q,XMQuaternionRotationAxis(XMVectorSet(1,0,0,0),tilt));
    XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(s.rotation.data()),XMQuaternionNormalize(q));
}
void Camera::rotate(Settings& s,float fromX,float fromY,float toX,float toY,float width,float height,float dt) {
    float scale=s.rotationSpeed/std::max(100.0f,std::min(width,height)*.48f);
    float spin=-(toX-fromX)*scale,tilt=-(toY-fromY)*scale;
    applyRotation(s,spin,tilt);
    spinVelocity_=spin/std::max(dt,.004f);
    tiltVelocity_=tilt/std::max(dt,.004f);
}
void Camera::update(Settings& s,float dt,bool dragging) {
    if(!std::isfinite(dt) || dt<=0) return;
    if(s.autoRotation>0) applyRotation(s,float(std::fmod(double(dt)*s.autoRotation*.01*(XM_2PI/5.0),double(XM_2PI))),0);
    if(dragging) return;
    if(s.inertia<=0 || std::max(std::abs(spinVelocity_),std::abs(tiltVelocity_))<.001f) { spinVelocity_=tiltVelocity_=0; return; }
    float damping=std::exp(-dt/s.inertia),integral=s.inertia*(1-damping);
    applyRotation(s,spinVelocity_*integral,tiltVelocity_*integral);
    spinVelocity_*=damping; tiltVelocity_*=damping;
}
float Camera::distance(const Settings& s) const { return (std::hypot(float(s.columns),float(s.bands))*s.spacing*.95f+s.maxHeight)*s.zoom; }
void Camera::pan(Settings& s,float dx,float dy,float height) { float unit=distance(s)*.9f/std::max(height,1.0f); s.panX=std::clamp(s.panX+dx*unit,-50.0f,50.0f); s.panY=std::clamp(s.panY-dy*unit,-50.0f,50.0f); }
XMMATRIX Camera::matrix(const Settings& s,float aspect) const {
    auto q=XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(s.rotation.data()));
    auto model=XMMatrixRotationQuaternion(q)*XMMatrixTranslation(s.panX,s.panY,0);
    float d=distance(s); auto view=XMMatrixLookAtLH(XMVectorSet(0,0,-d,1),XMVectorZero(),XMVectorSet(0,1,0,0));
    return model*view*XMMatrixPerspectiveFovLH(XMConvertToRadians(46),aspect,.05f,2000);
}
}
