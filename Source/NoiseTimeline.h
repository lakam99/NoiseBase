#pragma once
#include "NoiseTracking.h"

struct NoiseRegion {int64_t start=0,end=0;NoiseProfile profile;};
using NoiseRegions=std::vector<NoiseRegion>;
inline int noiseRegionAt(const NoiseRegions& regions,int64_t frame){
    auto found=std::upper_bound(regions.begin(),regions.end(),frame,[](int64_t value,const NoiseRegion& region){return value<region.start;});
    return found==regions.begin()?0:(int)(found-regions.begin()-1);
}

class NoiseTimelineBuilder {
public:
    NoiseTimelineBuilder(double rate,int size,int hop);
    void add(int64_t frame,const std::vector<float>& spectrum,bool quiet);
    NoiseRegions finish(int64_t length,const NoiseProfile& fallback);
private:
    void emit();
    int64_t findBoundary(const NoiseProfile& next) const;
    double rate;int size,hop,count=0;
    int first=0,last=-1,previousEvidence=0;
    double energySum=0;
    std::vector<double> sum;
    struct Feature{NoiseBands bands;bool quiet;};
    std::vector<Feature> features;
    NoiseRegions regions;
};
