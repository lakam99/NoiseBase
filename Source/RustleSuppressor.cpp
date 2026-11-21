#include "RustleSuppressor.h"
#include <algorithm>
#include <cmath>

namespace {float unit(double value){return (float)std::clamp(value,0.0,1.0);}}
void RustleSuppressor::prepare(double sr,int fftSize,int hopSize){
    rate=sr;size=fftSize;hop=hopSize;seconds=hop/rate;
    int bins=size/2+1;first=std::clamp((int)std::ceil(2500*size/rate),1,bins-1);last=std::clamp((int)(std::min(12000.0,rate*.45)*size/rate),first,bins-1);
    low=std::clamp((int)std::ceil(80*size/rate),1,first);width=std::max(2,(int)std::round(250*size/rate));
    prefix.resize((size_t)bins+1);spectrum.resize((size_t)bins);tonalHistory.resize((size_t)bins);detectedFrames=0;reset();
}
void RustleSuppressor::reset(){initialized=event=repeated=false;confidence=textureConfidence=eventTexture=0;background=age=quietAge=peak=valley=0;std::fill(prefix.begin(),prefix.end(),0);std::fill(spectrum.begin(),spectrum.end(),0);std::fill(tonalHistory.begin(),tonalHistory.end(),0);}
void RustleSuppressor::analyse(const std::vector<float>& power){
    if(power.size()!=spectrum.size()||last-first<4){confidence=0;return;}
    double high=0,body=0,logs=0;prefix[0]=0;
    for(size_t k=0;k<power.size();++k){double value=std::max(1e-30f,power[k]);spectrum[k]=(float)value;prefix[k+1]=prefix[k]+value;if((int)k>=first&&(int)k<=last){high+=value;logs+=std::log(value);}else if((int)k>=low&&(int)k<first)body+=value;}
    float toneTracking=(float)(1-std::exp(-seconds/.065));
    for(int k=0;k<(int)power.size();++k){int a=std::max(0,k-width),b=std::min((int)power.size()-1,k+width);double local=(prefix[(size_t)b+1]-prefix[(size_t)a])/(b-a+1);float tone=unit((spectrum[(size_t)k]/std::max(1e-30,local)-2)/3);tonalHistory[(size_t)k]+=toneTracking*(tone-tonalHistory[(size_t)k]);}
    double flat=std::exp(logs/(last-first+1))/(high/(last-first+1)+1e-30);
    float shape=std::sqrt(unit((flat-.075)/.18)*unit((high/(high+body+1e-30)-.22)/.48));
    auto bandEnergy=[&](double begin,double end){int a=std::clamp((int)std::ceil(begin*size/rate),0,(int)power.size()),b=std::clamp((int)std::ceil(end*size/rate),a,(int)power.size());return prefix[(size_t)b]-prefix[(size_t)a];};
    // Friction often has a broad 4-8 kHz ridge with a present, falling upper
    // tail. Require several bands: a narrow /s/, lower /sh/ and full-band hiss
    // must not get stronger treatment merely for having high-frequency energy.
    double ridge=bandEnergy(4000,8000),lower=bandEnergy(1800,4000),upper=bandEnergy(8000,12000),tail=bandEnergy(12000,20000);
    double u=upper/std::max(1e-30,ridge),l=lower/std::max(1e-30,ridge),v=tail/std::max(1e-30,ridge);
    float texture=rate>=32000?std::sqrt(unit((.65-l)/.35)*unit((u-.06)/.09)*unit((.70-u)/.25)*unit((.30-v)/.15)*unit((v-.01)/.02))*unit((flat-.075)/.10):0;
    textureConfidence+=(texture-textureConfidence)*(float)(1-std::exp(-seconds/.012));shape=std::max(shape,texture);
    if(!initialized){background=high;initialized=true;}
    double ratio=high/std::max(background,1e-25);bool audible=high/(size*(double)size)>1e-12;
    if(!event&&audible&&ratio>(texture>.25f?2.0:4.0)&&shape>.1f){event=true;repeated=false;age=quietAge=0;peak=valley=high;eventTexture=textureConfidence;}
    if(event){
        age+=seconds;
        eventTexture=std::max(eventTexture,textureConfidence);
        if(high>peak){peak=high;valley=high;}else valley=std::min(valley,high);
        if(valley<peak*.35&&high>valley*2&&high>peak*.12){repeated=true;peak=valley=high;}
        quietAge=ratio<1.5&&shape<.15f?quietAge+seconds:0;
        if(quietAge>.12||age>.65){event=false;repeated=false;}
    }
    float target=event&&audible?shape*unit((ratio-1.25)/2.5)*unit((.65-age)/.20):0;
    double time=target>confidence?.008:.045;confidence+=(target-confidence)*(float)(1-std::exp(-seconds/time));
    if(confidence>.15f)++detectedFrames;
    // A sustained new noise floor must eventually be learned normally.
    double tracking=high<background?.060:(event?.65:.20);background+=(high-background)*(1-std::exp(-seconds/tracking));
}
float RustleSuppressor::removableWeight(int k,float preserve) const {
    if(k<0||k>=(int)spectrum.size())return 0;
    double frequency=k*rate/size;float band=unit((frequency-1800)/1200);if(band<=0)return 0;
    int a=std::max(0,k-width),b=std::min((int)spectrum.size()-1,k+width);double neighbours=(prefix[(size_t)b+1]-prefix[(size_t)a])/(b-a+1);
    float tonal=unit((unit((spectrum[(size_t)k]/std::max(1e-30,neighbours)-2)/3)*tonalHistory[(size_t)k]-.35)/.50);
    return band*(1-(.8f+.19f*unit(preserve))*tonal);
}
float RustleSuppressor::outputGain(float amount,float speech,float preserve) const {
    if(confidence<.001f||amount<=0)return 1;
    // Keep isolated speech onsets conservative. Irregular repeated strokes
    // permit more attenuation; narrow harmonic peaks receive extra protection.
    float speechGuard=unit(speech)*unit(preserve),certainSpeech=unit((speech-.85f)/.10f);
    float limit=repeated?14.0f-3*speechGuard:std::max(6.0f-5.5f*speechGuard,(14.0f-3*speechGuard)*eventTexture*(1-certainSpeech));
    bool certainRustle=repeated||(eventTexture>.45f&&certainSpeech<.6f);
    float depth=certainRustle?std::min(36.0f,2.5f*std::min(amount,limit)):std::min(amount,limit);
    float certainty=certainRustle?unit((confidence-.08f)/.50f):confidence;
    float db=depth*certainty;
    return std::pow(10.0f,-db/20);
}
