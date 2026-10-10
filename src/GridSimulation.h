#pragma once
#include "SpectrumAnalyzer.h"
#include <vector>
namespace sg {
// RGBA32F GPU state: normalized height, downward velocity, reference time, signed launch target: magnitude for color, negative after first impact.
struct GridPoint { float initial=0, velocity=0, origin=0, colorHeight=0; };
static_assert(sizeof(GridPoint)==16);
class GridSimulation {
public:
    void resize(int bands,int columns,double now);
    void advance(double now,float speed,float gravity,const SpectrumTimeline& timeline,float maxHeight=4.0f,bool smoothingEnabled=true,float bounce=0);
    const GridPoint* row(int physicalRow) const { return points_.data()+physicalRow*bands_; }
    int bands() const { return bands_; }
    int columns() const { return columns_; }
    int rows() const { return columns_+1; }
    int newest() const { return newest_; }
    double phase() const { return phase_; }
    double simulationTime() const { return lastTime_-epoch_; }
    float bounce() const { return bounce_; }
    double acceleration() const { return acceleration_; }
    float height(int age,int band) const;
    float velocity(int age,int band) const;
    const std::vector<int>& dirtyRows() const { return dirty_; }
    void clearDirty() { dirty_.clear(); }
    void markAllDirty();
private:
    void write(int physical,const SpectrumFrame& frame,double origin);
    void rebase(double now);
    int bands_=0,columns_=0,newest_=0;
    double lastTime_=0,phase_=0,epoch_=0,acceleration_=0;
    float bounce_=0;
    std::vector<GridPoint> points_;
    std::vector<int> dirty_;
};
}
