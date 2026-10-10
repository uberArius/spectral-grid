#include "GridSimulation.h"
#include <cmath>
#include <algorithm>
namespace sg {
namespace {
GridPoint evaluate(GridPoint p,double t,double gravity,double bounce) {
    if(gravity<=0) return p;
    double landing=(-p.velocity+std::sqrt(double(p.velocity)*p.velocity+2*gravity*p.initial))/gravity;
    double h=p.initial-p.velocity*t-.5*gravity*t*t,v=p.velocity+gravity*t;
    if(t>=landing) {
        if(p.colorHeight>0 && bounce>0) {
            double elapsed=t-landing,up=std::sqrt(2*gravity*bounce*p.colorHeight);
            h=up*elapsed-.5*gravity*elapsed*elapsed; v=-up+gravity*elapsed;
        }
        p.colorHeight=-std::abs(p.colorHeight); // One impact only; magnitude keeps launch color.
    }
    p.initial=float(std::max(0.0,h));
    p.velocity=(h>0 || (h==0 && v<0))?float(v):0;
    return p;
}
}

void GridSimulation::resize(int bands,int columns,double now) {
    bands_=std::clamp(bands,16,256); columns_=std::clamp(columns,16,256);
    points_.assign(size_t(bands_)*(columns_+1),{}); dirty_.clear(); dirty_.reserve(columns_+1);
    newest_=0; lastTime_=epoch_=now; phase_=acceleration_=0; markAllDirty();
}
void GridSimulation::markAllDirty() {
    dirty_.clear();
    for(int r=0;r<rows();++r) dirty_.push_back(r);
}
void GridSimulation::write(int physical,const SpectrumFrame& frame,double now) {
    for(int i=0;i<bands_;++i) {
        float target=std::clamp(frame.values[i],0.0f,1.0f);
        // Negative downward velocity launches from 70% to the measured apex.
        points_[physical*bands_+i]={.7f*target,-float(std::sqrt(2*acceleration_*.3*target)),float(now-epoch_),target};
    }
    if(std::find(dirty_.begin(),dirty_.end(),physical)==dirty_.end()) dirty_.push_back(physical);
}
void GridSimulation::rebase(double now) {
    // Only acceleration changes and clock rebases touch the entire grid.
    // Evaluate with OLD acceleration, preserving both position and velocity.
    for(auto& p:points_) {
        double age=std::max(0.0,now-epoch_-p.origin);
        p=evaluate(p,age,acceleration_,bounce_);
        p.origin=0;
    }
    epoch_=now;
    markAllDirty();
}
void GridSimulation::advance(double now,float speed,float gravity,const SpectrumTimeline& timeline,float maxHeight,bool smoothingEnabled,float bounce) {
    if(!bands_ || !std::isfinite(now) || now<lastTime_) return;
    speed=std::isfinite(speed)?std::clamp(speed,1.0f,240.0f):24.0f;
    gravity=std::isfinite(gravity)?std::clamp(gravity,.1f,30.0f):9.81f;
    maxHeight=std::isfinite(maxHeight)?std::clamp(maxHeight,.1f,45.0f):4.0f;
    // Gravity uses grid units/s^2. Normalized heights retain the existing height
    // control and palette behavior, with the same world acceleration for all dots.
    bounce=std::isfinite(bounce)?std::clamp(bounce,0.0f,1.0f):.4f;
    double nextAcceleration=double(gravity)/maxHeight;
    if(nextAcceleration!=acceleration_ || bounce!=bounce_ || simulationTime()>=8) {
        rebase(lastTime_);
        acceleration_=nextAcceleration; bounce_=bounce;
    }
    if(now==lastTime_) return;
    double dt=now-lastTime_;
    // Reconstruct only the visible span after suspend, preserving scroll phase.
    if(dt>(columns_+1)/double(speed)) {
        double skippedColumns=std::max(0.0,std::floor(dt*speed)-rows());
        double skipped=skippedColumns/speed;
        newest_=(newest_+int(std::fmod(skippedColumns,rows())))%rows();
        lastTime_+=skipped; dt-=skipped;
        rebase(lastTime_);
    }
    double remaining=dt,t=lastTime_;
    while(phase_+remaining*speed>=1.0-1e-10) {
        double step=(1-phase_)/speed;
        t+=step; remaining=std::max(0.0,remaining-step);
        // The live column follows audio as before. On release into history it
        // launches from 70% of the last audio height towards that height.
        write(newest_,timeline.sample(t,bands_,smoothingEnabled),t);
        newest_=(newest_+1)%rows(); phase_=0;
        write(newest_,timeline.sample(t,bands_,smoothingEnabled),t);
    }
    phase_+=remaining*speed;
    write(newest_,timeline.sample(now,bands_,smoothingEnabled),now); lastTime_=now;
    if(simulationTime()>=8) rebase(now);
}
float GridSimulation::height(int age,int band) const {
    int physical=(newest_-age+rows())%rows(); const auto& p=points_[physical*bands_+band];
    double t=std::max(0.0,simulationTime()-p.origin);
    return evaluate(p,t,acceleration_,bounce_).initial;
}
float GridSimulation::velocity(int age,int band) const {
    int physical=(newest_-age+rows())%rows(); const auto& p=points_[physical*bands_+band];
    return evaluate(p,std::max(0.0,simulationTime()-p.origin),acceleration_,bounce_).velocity;
}
}
