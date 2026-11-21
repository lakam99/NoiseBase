#include "DenoiseEngine.h"
#include "NoiseTracking.h"

void DenoiseEngine::prepare(double sr, int channels) {
    rate = sr; channelCount = juce::jlimit(1, 2, channels);
    int order = juce::jlimit(8, 13, (int)std::round(std::log2(sr * .046)));
    size = 1 << order; hop = size / 4; outputCapacity = size * 4;
    fft = std::make_unique<juce::dsp::FFT>(order);
    window.resize((size_t)size);
    for (int i=0;i<size;++i) window[(size_t)i] = .5f - .5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)size);
    input.assign((size_t)channelCount, std::vector<float>((size_t)size, 0));
    output.assign((size_t)channelCount, std::vector<float>((size_t)outputCapacity, 0));
    fftData.assign((size_t)channelCount, std::vector<float>((size_t)size * 2, 0));
    removableData.assign((size_t)channelCount,std::vector<float>((size_t)size*2,0));
    removableOutput.assign((size_t)channelCount,std::vector<float>((size_t)outputCapacity,0));
    int bins = size / 2 + 1;
    noise.assign((size_t)bins, 1e-16f); minimum.assign((size_t)bins, 1e30f);
    smoothedPower.assign((size_t)bins, 0); power.assign((size_t)bins, 0);
    gain.assign((size_t)bins, 1); priorClean.assign((size_t)bins, 0);
    priorGain.assign((size_t)bins, 1); frequencyScratch.assign((size_t)bins, 0);
    candidate.assign((size_t)bins,0);candidateSeconds=speechHangover=0;changes=0;
    rustle.prepare(rate,size,hop);
    inputPos=outputPos=0; untilFrame=hop; learnedSeconds=0; minimumAge=0; profileReady=false;
    mixSmooth.reset(rate,.035); mixSmooth.setCurrentAndTargetValue(1);
    outputSmooth.reset(rate,.035); outputSmooth.setCurrentAndTargetValue(1);
    bypassSmooth.reset(rate,.025); bypassSmooth.setCurrentAndTargetValue(1);
    noiseOnlySmooth.reset(rate,.025); noiseOnlySmooth.setCurrentAndTargetValue(0);
    rustleGain.reset(rate,.001);rustleGain.setCurrentAndTargetValue(1);
    meterInput=meterOutput=meterReduction=0;
}

void DenoiseEngine::resetNoise() {
    std::fill(noise.begin(),noise.end(),1e-16f);
    std::fill(minimum.begin(),minimum.end(),1e30f);
    std::fill(priorClean.begin(),priorClean.end(),0);
    learnedSeconds=0; profileReady=false; minimumAge=0;
    candidateSeconds=speechHangover=0;changes=0;
    rustle.reset();
    rustleGain.setCurrentAndTargetValue(1);
}

bool DenoiseEngine::setProfile(const NoiseProfile& profile,bool clearHistory) {
    if(profile.fftSize<1 || profile.sampleRate<1 || profile.power.empty()) return false;
    double scale=(rate*size)/(profile.sampleRate*profile.fftSize);
    for(size_t k=0;k<noise.size();++k) {
        double index=(k*rate/size)*profile.fftSize/profile.sampleRate;
        int a=juce::jlimit(0,(int)profile.power.size()-1,(int)index);
        int b=std::min(a+1,(int)profile.power.size()-1);
        float fraction=(float)juce::jlimit(0.0,1.0,index-a);
        noise[k]=std::max(1e-16f,(profile.power[(size_t)a]+fraction*(profile.power[(size_t)b]-profile.power[(size_t)a]))*(float)scale);
    }
    learnedSeconds=profile.quietSeconds; profileReady=!profile.estimated;candidateSeconds=0;
    if(clearHistory){std::fill(priorClean.begin(),priorClean.end(),0);std::fill(priorGain.begin(),priorGain.end(),1);rustle.reset();rustleGain.setCurrentAndTargetValue(1);}
    return true;
}
bool DenoiseEngine::copyLearnedProfile(NoiseProfile& p) const {
    if(!profileReady||learnedSeconds<.35)return false;p.sampleRate=rate;p.fftSize=size;p.quietSeconds=learnedSeconds;p.estimated=false;p.sourceChannels=channelCount;p.power=noise;return true;
}

void DenoiseEngine::frame(float speech, float cap, float preserve, bool learn) {
    std::fill(power.begin(),power.end(),0);
    for(int c=0;c<channelCount;++c) {
        auto& data=fftData[(size_t)c];
        std::fill(data.begin(),data.end(),0);
        for(int i=0;i<size;++i) data[(size_t)i]=input[(size_t)c][(size_t)((inputPos+i)&(size-1))]*window[(size_t)i];
        fft->performRealOnlyForwardTransform(data.data(),true);
        for(int k=0;k<=size/2;++k) {
            float p=data[(size_t)k*2]*data[(size_t)k*2]+data[(size_t)k*2+1]*data[(size_t)k*2+1];
            power[(size_t)k]=juce::jmax(power[(size_t)k],p);
        }
    }
    rustle.analyse(power);
    float target=rustleEnabled?rustle.outputGain(cap,speech,preserve):1.0f;
    if(target!=rustleGain.getTargetValue()){float current=rustleGain.getCurrentValue();rustleGain.reset(rate,target<current?.001:.035);rustleGain.setCurrentAndTargetValue(current);rustleGain.setTargetValue(target);}
    double arithmetic=0;
    int first=juce::jmax(1,(int)(120*size/rate)), last=juce::jmin(size/2,(int)(4500*size/rate));
    for(int k=first;k<=last;++k) arithmetic+=power[(size_t)k];
    // The neural detector handles tonal room noise; rejecting it by spectral
    // flatness would prevent many real lavalier recordings from ever learning.
    if(speech>.35f)speechHangover=.1;else speechHangover=std::max(0.0,speechHangover-hop/rate);
    bool quiet=speech<.20f && speechHangover<=0 && arithmetic>1e-12 && !rustle.excludesLearning();
    bool changed=learn&&quiet&&profileReady&&differentNoise(noiseBands(power,rate,size),noiseBands(noise,rate,size));
    if(changed){
        auto currentBands=noiseBands(power,rate,size);
        if(candidateSeconds>.05&&differentNoise(currentBands,noiseBands(candidate,rate,size)))candidateSeconds=0;
        float alpha=candidateSeconds==0?1.0f:(float)(1-std::exp(-hop/(rate*.12)));
        for(size_t k=0;k<candidate.size();++k)candidate[k]+=alpha*(power[k]-candidate[k]);
        candidateSeconds+=hop/rate;
        if(candidateSeconds>=.30){std::copy(candidate.begin(),candidate.end(),noise.begin());std::fill(priorClean.begin(),priorClean.end(),0);candidateSeconds=0;++changes;}
    }else candidateSeconds=0;
    float learningRate=(float)(hop/(rate*1.5));
    for(size_t k=0;k<noise.size();++k) {
        smoothedPower[k]=.80f*smoothedPower[k]+.20f*power[k];
        minimum[k]=std::min(minimum[k],smoothedPower[k]);
        if(learn && quiet && !changed) {
            float alpha=learnedSeconds<.15 ? .35f : learningRate;
            noise[k]=(1-alpha)*noise[k]+alpha*power[k];
        }
    }
    if(learn && quiet) { learnedSeconds+=hop/rate; if(learnedSeconds>=.35) profileReady=true; }
    if(++minimumAge>(int)(rate*2/hop)) {
        if(learn && !profileReady) for(size_t k=0;k<noise.size();++k) noise[k]=juce::jmax(minimum[k]*1.4f,1e-16f);
        std::copy(smoothedPower.begin(),smoothedPower.end(),minimum.begin()); minimumAge=0;
    }
    float actualCap=profileReady ? cap : std::min(cap,6.0f);
    float floor=juce::Decibels::decibelsToGain(-actualCap+std::min(5.0f,actualCap*.4f)*speech*preserve);
    for(size_t k=0;k<noise.size();++k) {
        float posterior=power[k]/juce::jmax(noise[k],1e-16f);
        float immediate=juce::jmax(posterior-1.0f,0.0f);
        float prior=juce::jmax(.90f*priorClean[k]+.10f*immediate,.65f*immediate);
        float g=juce::jlimit(floor,1.0f,prior/(1+prior));
        priorClean[k]=posterior*g*g;
        frequencyScratch[k]=g;
    }
    double averageGain=0;
    for(size_t k=0;k<gain.size();++k) {
        size_t a=k==0?0:k-1,b=std::min(k+1,gain.size()-1);
        float smooth=.25f*frequencyScratch[a]+.5f*frequencyScratch[k]+.25f*frequencyScratch[b];
        gain[k]=.28f*priorGain[k]+.72f*smooth; priorGain[k]=gain[k];averageGain+=gain[k]*(1-(1-target)*rustle.removableWeight((int)k,preserve));
    }
    meterReduction=.9f*meterReduction+.1f*juce::jmax(0.0f,-juce::Decibels::gainToDecibels((float)(averageGain/gain.size()),-60.0f));
    for(int c=0;c<channelCount;++c) {
        auto& data=fftData[(size_t)c];
        for(int k=0;k<=size/2;++k) { data[(size_t)k*2]*=gain[(size_t)k]; data[(size_t)k*2+1]*=gain[(size_t)k]; }
        auto& removable=removableData[(size_t)c];std::copy(data.begin(),data.end(),removable.begin());
        for(int k=0;k<=size/2;++k){float weight=rustle.removableWeight(k,preserve);removable[(size_t)k*2]*=weight;removable[(size_t)k*2+1]*=weight;}
        fft->performRealOnlyInverseTransform(data.data());
        fft->performRealOnlyInverseTransform(removable.data());
        for(int i=0;i<size;++i){auto index=(size_t)((outputPos+1+i)&(outputCapacity-1));float weight=window[(size_t)i]*(2.0f/3.0f);output[(size_t)c][index]+=data[(size_t)i]*weight;removableOutput[(size_t)c][index]+=removable[(size_t)i]*weight;}
    }
}

void DenoiseEngine::process(juce::AudioBuffer<float>& buffer,float speech,float cap,float preserve,float mix,float outputDb,bool learn,bool bypass,bool noiseOnly) {
    juce::ScopedNoDenormals noDenormals;
    mixSmooth.setTargetValue(juce::jlimit(0.0f,1.0f,mix));
    outputSmooth.setTargetValue(bypass?1.0f:juce::Decibels::decibelsToGain(outputDb));
    bypassSmooth.setTargetValue(bypass?0.0f:1.0f);
    noiseOnlySmooth.setTargetValue(noiseOnly?1.0f:0.0f);
    float peakIn=0,peakOut=0;
    for(int s=0;s<buffer.getNumSamples();++s) {
        float mixValue=mixSmooth.getNextValue(),active=bypassSmooth.getNextValue();
        float blend=mixValue*active,listen=noiseOnlySmooth.getNextValue();
        float level=outputSmooth.getNextValue();
        // Apply the transient decision to the completed overlap-add layer.
        // Earlier FFT contributions are still buffered here, so their onset
        // can be cleaned using the existing latency rather than leaking past
        // independently attenuated frames. No additional audio delay is added.
        float artifactGain=rustleGain.getNextValue();
        for(int c=0;c<channelCount;++c) {
            float v=buffer.getSample(c,s); peakIn=std::max(peakIn,std::abs(v));
            float dry=input[(size_t)c][(size_t)inputPos];
            input[(size_t)c][(size_t)inputPos]=std::isfinite(v)?v:0;
            float wet=output[(size_t)c][(size_t)outputPos]; output[(size_t)c][(size_t)outputPos]=0;
            wet-=(1-artifactGain)*removableOutput[(size_t)c][(size_t)outputPos];removableOutput[(size_t)c][(size_t)outputPos]=0;
            float clean=dry+(wet-dry)*blend;
            // Subtract before output trim, using the same delayed dry reference.
            // Bypass restores the original even when noise-only remains selected.
            float removed=(dry-wet)*mixValue;
            float noise=dry+(removed-dry)*active;
            float result=(clean+(noise-clean)*listen)*level;
            buffer.setSample(c,s,result); peakOut=std::max(peakOut,std::abs(result));
        }
        inputPos=(inputPos+1)&(size-1);
        if(--untilFrame==0) { frame(speech,cap,preserve,learn); untilFrame=hop; }
        outputPos=(outputPos+1)&(outputCapacity-1);
    }
    float decay=(float)std::exp(-buffer.getNumSamples()/(rate*.25));
    meterInput=std::max(peakIn,meterInput*decay); meterOutput=std::max(peakOut,meterOutput*decay);
}
