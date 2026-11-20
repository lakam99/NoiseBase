#include "Offline.h"

namespace {
NoiseProfile knownProfile(double rate) {
    DenoiseEngine engine;engine.prepare(rate,2);int size=engine.fftSize();
    juce::dsp::FFT fft((int)std::round(std::log2(size)));
    NoiseProfile profile;profile.sampleRate=rate;profile.fftSize=size;profile.quietSeconds=1;profile.power.assign((size_t)size/2+1,0);
    std::vector<float> data((size_t)size*2);juce::Random random(491);int frames=0;
    for(int start=0;start+size<(int)rate;start+=size/4){
        std::fill(data.begin(),data.end(),0);
        for(int i=0;i<size;++i){float noise=.02f*std::sin((float)(juce::MathConstants<double>::twoPi*300*(start+i)/rate))+.002f*(random.nextFloat()-.5f);float window=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/size);data[(size_t)i]=noise*window;}
        fft.performRealOnlyForwardTransform(data.data(),true);
        for(int k=0;k<=size/2;++k)profile.power[(size_t)k]+=data[(size_t)k*2]*data[(size_t)k*2]+data[(size_t)k*2+1]*data[(size_t)k*2+1];
        ++frames;
    }
    for(auto& power:profile.power)power/=std::max(1,frames);
    return profile;
}
juce::AudioBuffer<float> render(const std::vector<float>& input,double rate,const NoiseProfile& profile,bool noiseOnly,float cap=18,float mix=.65f,bool bypass=false,float trim=0) {
    DenoiseEngine engine;engine.prepare(rate,2);engine.setProfile(profile);
    int length=(int)input.size()+engine.latency();juce::AudioBuffer<float> result(2,length);
    for(int start=0;start<length;start+=137){
        int count=std::min(137,length-start);juce::AudioBuffer<float> block(2,count);
        for(int i=0;i<count;++i){float sample=start+i<(int)input.size()?input[(size_t)start+i]:0;block.setSample(0,i,sample);block.setSample(1,i,-sample);}
        engine.process(block,1,cap,.8f,mix,trim,false,bypass,noiseOnly);
        for(int channel=0;channel<2;++channel)result.copyFrom(channel,start,block,channel,0,count);
    }
    return result;
}
double amplitude(const juce::AudioBuffer<float>& result,double rate,int latency,double frequency){
    double real=0,imaginary=0;int start=(int)(rate*.6),count=(int)rate;
    for(int i=0;i<count;++i){double phase=juce::MathConstants<double>::twoPi*frequency*(start+i)/rate;float sample=result.getSample(0,latency+start+i);real+=sample*std::cos(phase);imaginary+=sample*std::sin(phase);}
    return 2*std::sqrt(real*real+imaginary*imaginary)/count;
}
}
juce::var runNoiseOutputTests(){
    juce::Array<juce::var> rows;bool passed=true;
    for(double rate:{44100.0,48000.0,96000.0}){
        auto profile=knownProfile(rate);int latency=profile.fftSize;
        std::vector<float> input((size_t)(rate*2));juce::Random random(492);
        for(int i=0;i<(int)input.size();++i)input[(size_t)i]=.02f*std::sin((float)(juce::MathConstants<double>::twoPi*300*i/rate))+.15f*std::sin((float)(juce::MathConstants<double>::twoPi*1500*i/rate))+.002f*(random.nextFloat()-.5f);
        auto clean=render(input,rate,profile,false),noise=render(input,rate,profile,true);
        auto zeroMix=render(input,rate,profile,true,18,0),bypassed=render(input,rate,profile,true,18,1,true,6),unity=render(input,rate,profile,true,0,1);
        float sumError=0,stereoError=0,zeroError=0,bypassError=0,unityError=0;bool finite=true;
        for(int i=0;i<(int)input.size();++i){int sample=i+latency;
            sumError=std::max(sumError,std::abs(clean.getSample(0,sample)+noise.getSample(0,sample)-input[(size_t)i]));
            stereoError=std::max(stereoError,std::abs(noise.getSample(0,sample)+noise.getSample(1,sample)));
            zeroError=std::max(zeroError,std::abs(zeroMix.getSample(0,sample)));bypassError=std::max(bypassError,std::abs(bypassed.getSample(0,sample)-input[(size_t)i]));unityError=std::max(unityError,std::abs(unity.getSample(0,sample)));
            finite &= std::isfinite(noise.getSample(0,sample));
        }
        double voiceDb=20*std::log10(std::max(amplitude(noise,rate,latency,1500)/.15,1e-12));
        double hum=amplitude(noise,rate,latency,300)/.02;
        bool ok=finite&&sumError<.00002f&&stereoError<.00002f&&zeroError<.00002f&&bypassError<.00002f&&unityError<.00002f&&voiceDb<-20&&hum>.3;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("latency_samples",latency);row->setProperty("clean_plus_noise_error",sumError);row->setProperty("stereo_anti_phase_error",stereoError);row->setProperty("zero_mix_noise_error",zeroError);row->setProperty("bypass_original_error",bypassError);row->setProperty("zero_reduction_noise_error",unityError);row->setProperty("synthetic_foreground_attenuation_db",voiceDb);row->setProperty("known_hum_retained_ratio",hum);row->setProperty("passed",ok);passed &=ok;rows.add(juce::var(row));
    }
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("runs",rows);return juce::var(report);
}
