#pragma once
#include "DenoiseEngine.h"

using NoiseBands=std::array<float,8>;
inline NoiseBands noiseBands(const std::vector<float>& spectrum,double rate,int size){
    constexpr double edges[]{125,250,500,1000,2000,4000,8000};
    std::array<double,8> sums{};std::array<int,8> counts{};
    for(size_t k=1;k<spectrum.size();++k){double hz=k*rate/size;int band=0;while(band<7&&hz>=edges[band])++band;sums[(size_t)band]+=spectrum[k];++counts[(size_t)band];}
    NoiseBands result{};for(size_t b=0;b<8;++b)result[b]=(float)(10*std::log10(std::max(1e-20,sums[b]/std::max(1,counts[b]))));return result;
}
inline bool differentNoise(const NoiseBands& a,const NoiseBands& b){
    std::array<float,8> difference{};int count=0;float peak=std::max(*std::max_element(a.begin(),a.end()),*std::max_element(b.begin(),b.end()));
    for(size_t i=0;i<8;++i)if(std::max(a[i],b[i])>peak-40&&std::max(a[i],b[i])>-120)difference[(size_t)count++]=a[i]-b[i];
    if(!count)return false;
    auto sorted=difference;std::sort(sorted.begin(),sorted.begin()+count);float level=sorted[(size_t)(count/2)],shape=0;
    for(int i=0;i<count;++i)shape+=(difference[(size_t)i]-level)*(difference[(size_t)i]-level);
    return std::abs(level)>4||std::sqrt(shape/count)>6;
}
inline float noiseFit(const NoiseBands& observed,const NoiseBands& reference){
    std::array<float,8> errors{};int count=0;
    for(size_t i=0;i<8;++i)if(reference[i]>-120)errors[(size_t)count++]=std::min(30.0f,std::abs(observed[i]-reference[i]));
    if(!count)return 0;std::sort(errors.begin(),errors.begin()+count);int keep=std::max(1,(count+1)/2);float cost=0;for(int i=0;i<keep;++i)cost+=errors[(size_t)i];return cost/keep;
}
