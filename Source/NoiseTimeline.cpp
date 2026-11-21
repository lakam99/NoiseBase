#include "NoiseTimeline.h"
#include <numeric>

NoiseTimelineBuilder::NoiseTimelineBuilder(double sr,int fftSize,int step):rate(sr),size(fftSize),hop(step),sum((size_t)fftSize/2+1,0){}
void NoiseTimelineBuilder::add(int64_t position,const std::vector<float>& spectrum,bool quiet){
    int index=(int)features.size();features.push_back({noiseBands(spectrum,rate,size),quiet});
    if(last>=0&&(index-last)*hop>rate*.18)emit();
    if(!quiet)return;
    double energy=std::accumulate(spectrum.begin(),spectrum.end(),0.0);
    // Reject an isolated impact within this short block, not all later clips
    // that happen to have a louder background than the beginning of the file.
    if(count>6&&energy>energySum/count*10)return;
    if(!count)first=index;last=index;++count;energySum+=energy;
    for(size_t k=0;k<sum.size();++k)sum[k]+=spectrum[k];
    if(count*hop>=rate*.4)emit();
    juce::ignoreUnused(position);
}
int64_t NoiseTimelineBuilder::findBoundary(const NoiseProfile& next) const {
    if(regions.empty())return 0;
    int lo=std::max((int)(regions.back().start/hop),previousEvidence-(int)(rate*.5/hop));
    int hi=std::min(last,(int)features.size()-1);lo=juce::jlimit(0,hi,lo);
    auto oldBands=noiseBands(regions.back().profile.power,rate,size),newBands=noiseBands(next.power,rate,size);
    double cost=0;for(int i=lo;i<=hi;++i)cost+=noiseFit(features[(size_t)i].bands,newBands)*(features[(size_t)i].quiet?1.0:.35);
    double best=cost;int split=lo;
    for(int i=lo;i<=hi;++i){auto& feature=features[(size_t)i];cost+=(noiseFit(feature.bands,oldBands)-noiseFit(feature.bands,newBands))*(feature.quiet?1.0:.35);if(cost<best){best=cost;split=i+1;}}
    return std::max(regions.back().start+hop,(int64_t)split*hop+size/2);
}
void NoiseTimelineBuilder::emit(){
    if(count*hop>=rate*.18){
        NoiseProfile profile;profile.sampleRate=rate;profile.fftSize=size;profile.quietSeconds=count*hop/rate;profile.estimated=profile.quietSeconds<.35;profile.power.resize(sum.size());
        for(size_t k=0;k<sum.size();++k){size_t a=k? k-1:k,b=std::min(k+1,sum.size()-1);profile.power[k]=(float)std::max(1e-16,(sum[a]*.25+sum[k]*.5+sum[b]*.25)/count);}
        if(regions.empty())regions.push_back({0,0,std::move(profile)});
        else if(differentNoise(noiseBands(profile.power,rate,size),noiseBands(regions.back().profile.power,rate,size))){
            auto boundary=findBoundary(profile);regions.back().end=boundary;regions.push_back({boundary,0,std::move(profile)});
        }else{
            auto& existing=regions.back().profile;double weight=std::min(2.0,existing.quietSeconds),total=weight+profile.quietSeconds;
            for(size_t k=0;k<existing.power.size();++k)existing.power[k]=(float)((existing.power[k]*weight+profile.power[k]*profile.quietSeconds)/total);
            existing.quietSeconds+=profile.quietSeconds;existing.estimated=existing.quietSeconds<.35;
        }
        previousEvidence=last;
    }
    count=0;energySum=0;std::fill(sum.begin(),sum.end(),0);last=-1;
}
NoiseRegions NoiseTimelineBuilder::finish(int64_t length,const NoiseProfile& fallback){
    emit();if(regions.empty())regions.push_back({0,length,fallback});regions.back().end=length;return std::move(regions);
}
