#include "Offline.h"
#include <random>

juce::var runBackgroundArtifactTests(){
    juce::Array<juce::var> runs;bool passed=true;
    for(double rate:{44100.,48000.,96000.})for(float cap:{14.f,24.f}){
        DenoiseEngine engine;engine.prepare(rate,1);
        int size=engine.fftSize(),latency=engine.latency(),count=(int)(rate*3.5);
        NoiseProfile profile;profile.sampleRate=rate;profile.fftSize=size;profile.quietSeconds=5;profile.power.resize((size_t)size/2+1);
        constexpr double colour=.18,sigma=.02,pole=1-colour;
        // Analytic spectrum of stationary coloured Gaussian room noise. This
        // is independent of the denoiser's gain formula and contains no tones.
        for(size_t k=0;k<profile.power.size();++k){double w=juce::MathConstants<double>::twoPi*k/size;
            profile.power[k]=(float)(sigma*sigma*(size*3./8)*colour*colour/(1+pole*pole-2*pole*std::cos(w)));}
        if(!engine.setProfile(profile,true))throw std::runtime_error("Background fixture rejected");
        std::mt19937 random(67332);std::normal_distribution<float> normal(0,1);double low=0;
        std::vector<float> source((size_t)count+latency,0),rendered(source.size(),0);
        for(int i=0;i<count;++i){low=pole*low+colour*sigma*normal(random);double t=i/rate;
            source[(size_t)i]=(float)(low+(t<.4?.08*std::sin(t*juce::MathConstants<double>::twoPi*190)+.03*std::sin(t*juce::MathConstants<double>::twoPi*570):0));}
        juce::AudioBuffer<float> block(1,127);bool finite=true;
        for(int at=0;at<(int)source.size();at+=127){int n=std::min(127,(int)source.size()-at);block.setSize(1,n,false,false,true);block.copyFrom(0,0,source.data()+at,n);
            engine.process(block,at<rate*.4?1.f:0.f,cap,1,1,0,false,false);
            for(int j=0;j<n;++j){float value=block.getSample(0,j);finite&=std::isfinite(value);rendered[(size_t)(at+j)]=value;}}
        double in=0,out=0,cross=0,voiceIn=0,voiceOut=0;
        for(int i=(int)(rate*2.2);i<(int)(rate*3.4);++i){double x=source[(size_t)i],y=rendered[(size_t)(i+latency)];in+=x*x;out+=y*y;cross+=x*y;}
        for(int i=(int)(rate*.12);i<(int)(rate*.32);++i){double x=source[(size_t)i],y=rendered[(size_t)(i+latency)];voiceIn+=x*x;voiceOut+=y*y;}
        double correlation=cross/std::sqrt(std::max(1e-30,in*out)),attenuation=10*std::log10(std::max(1e-30,in)/std::max(1e-30,out));
        double voiceChange=10*std::log10(std::max(1e-30,voiceOut)/std::max(1e-30,voiceIn));
        // A confirmed noise pause should approach a quieter version of the
        // same background, rather than amplify random spectral islands.
        bool ok=finite&&correlation>.98&&attenuation>cap-2&&attenuation<cap+.4&&voiceChange>-2&&voiceChange<.2;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("reduction_db",cap);row->setProperty("background_identity_correlation",correlation);row->setProperty("background_attenuation_db",attenuation);row->setProperty("speech_level_change_db",voiceChange);row->setProperty("finite",finite);row->setProperty("passed",ok);runs.add(juce::var(row));passed&=ok;
    }
    auto* result=new juce::DynamicObject;result->setProperty("passed",passed);result->setProperty("runs",runs);return juce::var(result);
}
