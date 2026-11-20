#include "ProceduralNoise.h"

bool validAmbienceProfile(const NoiseProfile& p){
    if(!std::isfinite(p.sampleRate)||p.sampleRate<8000||p.sampleRate>384000||p.fftSize<256||p.fftSize>8192||(p.fftSize&(p.fftSize-1))||p.estimated||!std::isfinite(p.quietSeconds)||p.quietSeconds<.35||p.sourceChannels<1||p.sourceChannels>2||!std::isfinite(p.variationDb)||p.variationDb<0||p.variationDb>3)return false;
    size_t bins=(size_t)p.fftSize/2+1;
    if(p.power.size()!=bins||p.ambienceCorrelation.size()!=bins)return false;
    double energy=0;
    for(int c=0;c<2;++c){if(p.ambiencePower[(size_t)c].size()!=bins)return false;for(float v:p.ambiencePower[(size_t)c]){if(!std::isfinite(v)||v<0||v>1e9f)return false;energy+=v;}}
    for(float v:p.ambienceCorrelation)if(!std::isfinite(v)||std::abs(v)>1)return false;
    for(float v:p.power)if(!std::isfinite(v)||v<0||v>1e9f)return false;
    return energy>1e-12;
}
juce::ValueTree ambienceProfileState(const NoiseProfile& p){
    if(!validAmbienceProfile(p))return {};
    juce::MemoryOutputStream bytes;
    bytes.writeInt(1);bytes.writeInt(p.fftSize);bytes.writeDouble(p.sampleRate);bytes.writeDouble(p.quietSeconds);bytes.writeInt(p.sourceChannels);bytes.writeFloat(p.variationDb);
    for(auto* values:{&p.power,&p.ambiencePower[0],&p.ambiencePower[1],&p.ambienceCorrelation})for(float v:*values)bytes.writeFloat(v);
    juce::ValueTree state("AmbienceProfile");state.setProperty("data",bytes.getMemoryBlock().toBase64Encoding(),nullptr);return state;
}
bool readAmbienceProfile(const juce::ValueTree& state,NoiseProfile& result){
    if(!state.hasType("AmbienceProfile"))return false;
    auto encoded=state["data"].toString();if(encoded.length()>200000)return false;
    juce::MemoryBlock bytes;if(!bytes.fromBase64Encoding(encoded))return false;
    juce::MemoryInputStream input(bytes,false);NoiseProfile p;
    if(input.readInt()!=1)return false;p.fftSize=input.readInt();p.sampleRate=input.readDouble();p.quietSeconds=input.readDouble();p.sourceChannels=input.readInt();p.variationDb=input.readFloat();
    if(p.fftSize<256||p.fftSize>8192||(p.fftSize&(p.fftSize-1))||bytes.getSize()!=32+((size_t)p.fftSize/2+1)*16)return false;
    for(auto* values:{&p.power,&p.ambiencePower[0],&p.ambiencePower[1],&p.ambienceCorrelation}){values->resize((size_t)p.fftSize/2+1);for(float& v:*values)v=input.readFloat();}
    if(!validAmbienceProfile(p))return false;result=std::move(p);return true;
}
void ProceduralNoise::prepare(double sr,int count,uint64_t seed){
    rate=sr;channels=juce::jlimit(1,2,count);int order=juce::jlimit(8,13,(int)std::round(std::log2(sr*.046)));size=1<<order;hop=size/4;capacity=size*4;position=0;countdown=0;hasProfile=false;variation=0;
    fft=std::make_unique<juce::dsp::FFT>(order);window.resize((size_t)size);correlation.assign((size_t)size/2+1,0);modulation.fill(0);
    for(int i=0;i<size;++i)window[(size_t)i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/size);
    for(int c=0;c<2;++c){target[(size_t)c].assign((size_t)size/2+1,0);power[(size_t)c].assign((size_t)size/2+1,0);data[(size_t)c].assign((size_t)size*2,0);overlap[(size_t)c].assign((size_t)capacity,0);}
    static std::atomic<uint64_t> serial{0};random.seed(seed?seed:(uint64_t)juce::Time::getHighResolutionTicks()+(++serial)*0x9e3779b97f4a7c15ULL);gaussian.reset();
}
bool ProceduralNoise::setProfile(const NoiseProfile& p){
    if(!validAmbienceProfile(p))return false;
    float scale=(float)((rate*size)/(p.sampleRate*p.fftSize));
    for(int k=0;k<=size/2;++k){double index=(k*rate/size)*p.fftSize/p.sampleRate;
        int a=juce::jlimit(0,(int)p.power.size()-1,(int)index),b=std::min(a+1,(int)p.power.size()-1);float f=(float)juce::jlimit(0.0,1.0,index-a);
        auto sample=[&](const std::vector<float>& values){return values[(size_t)a]+f*(values[(size_t)b]-values[(size_t)a]);};
        float left=sample(p.ambiencePower[0])*scale,right=sample(p.ambiencePower[1])*scale,rho=juce::jlimit(-1.0f,1.0f,sample(p.ambienceCorrelation));
        // Above the reference Nyquist there is no measured energy.
        if(k*rate/size>p.sampleRate*.5)left=right=0;
        if(channels==1&&p.sourceChannels==2)left=.25f*(left+right+2*rho*std::sqrt(left*right));
        target[0][(size_t)k]=std::max(0.0f,left);target[1][(size_t)k]=std::max(0.0f,right);correlation[(size_t)k]=rho;
    }
    target[0][0]=target[1][0]=0;variation=p.variationDb;hasProfile=true;return true;
}
void ProceduralNoise::frame(){
    // Eight broad bands drift over several seconds, with a bounded learned depth.
    for(size_t band=0;band<modulation.size();++band){float alpha=(float)std::exp(-hop/(rate*(2.0+band*.7)));modulation[band]=alpha*modulation[band]+std::sqrt(1-alpha*alpha)*gaussian(random);}
    for(auto& values:data)std::fill(values.begin(),values.end(),0);
    float slew=(float)(1-std::exp(-hop/(rate*.15)));
    for(int k=1;k<=size/2;++k){
        float band=juce::jlimit(0.0f,7.0f,(float)(std::log2(std::max(30.0,k*rate/size)/30.0)*.72));int a=(int)band,b=std::min(a+1,7);float movement=modulation[(size_t)a]+(band-a)*(modulation[(size_t)b]-modulation[(size_t)a]);
        float level=juce::Decibels::decibelsToGain(juce::jlimit(-6.0f,6.0f,movement*variation));
        float real=gaussian(random),imag=gaussian(random),otherReal=gaussian(random),otherImag=gaussian(random),rho=correlation[(size_t)k],independent=std::sqrt(std::max(0.0f,1-rho*rho));
        for(int c=0;c<channels;++c){power[(size_t)c][(size_t)k]+=slew*(target[(size_t)c][(size_t)k]-power[(size_t)c][(size_t)k]);float amplitude=std::sqrt(std::max(0.0f,power[(size_t)c][(size_t)k])*(k==size/2?16.0f/9.0f:8.0f/9.0f))*level;
            data[(size_t)c][(size_t)k*2]=amplitude*(c==0?real:rho*real+independent*otherReal);
            data[(size_t)c][(size_t)k*2+1]=k==size/2?0:amplitude*(c==0?imag:rho*imag+independent*otherImag);
        }
    }
    for(int c=0;c<channels;++c){fft->performRealOnlyInverseTransform(data[(size_t)c].data());for(int i=0;i<size;++i)overlap[(size_t)c][(size_t)((position+i)%capacity)]+=data[(size_t)c][(size_t)i]*window[(size_t)i];}
}
void ProceduralNoise::next(float* samples){
    if(countdown==0){frame();countdown=hop;}
    for(int c=0;c<channels;++c){samples[c]=overlap[(size_t)c][(size_t)position];overlap[(size_t)c][(size_t)position]=0;}
    position=(position+1)%capacity;--countdown;
}
