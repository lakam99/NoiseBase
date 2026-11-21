#include "ReferenceLearner.h"
#include "NoiseTracking.h"
#include "AsyncCleanup.h"
#include <numeric>

namespace {
constexpr int packetSize=1024,queueSize=32;
struct Packet {
    std::array<std::array<float,packetSize>,2> audio{};
    int count=0,channels=1;
    double rate=48000;
    uint64_t generation=0,sequence=0;
};

// Delay spectral measurement until the voice detector has seen both sides of
// the window. This prevents the beginning/end of words entering the model.
class Analysis {
public:
    Analysis(double sr,int count,VoiceDetector& detector):rate(sr),channels(count),voice(detector){
        int order=juce::jlimit(8,13,(int)std::round(std::log2(rate*.046)));
        size=1<<order;hop=size/4;bins=size/2+1;
        capacity=juce::nextPowerOfTwo((int)(rate*.65)+size+packetSize);
        fft=std::make_unique<juce::dsp::FFT>(order);
        window.resize((size_t)size);cross.assign((size_t)bins,0);
        for(int i=0;i<size;++i)window[(size_t)i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/size);
        for(int c=0;c<2;++c){ring[(size_t)c].assign((size_t)capacity,0);spectrum[(size_t)c].resize((size_t)size*2);sum[(size_t)c].assign((size_t)bins,0);}
        lowpass.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(rate,std::min(6500.0,rate*.4));
        voice.reset();
    }
    template<class Publish> void add(const Packet& packet,Publish publish){
        double energy[2]{};
        for(int c=0;c<channels;++c)for(int i=0;i<packet.count;++i){float v=packet.audio[(size_t)c][(size_t)i];if(!std::isfinite(v))return;energy[c]+=v*v;}
        int voiceChannel=channels==2&&energy[1]>energy[0]?1:0;
        for(int i=0;i<packet.count;++i){
            for(int c=0;c<2;++c)ring[(size_t)c][(size_t)(samples&(capacity-1))]=packet.audio[(size_t)std::min(c,channels-1)][(size_t)i];
            ++samples;
            float filtered=lowpass.processSample(packet.audio[(size_t)voiceChannel][(size_t)i]);phase+=16000/rate;
            while(phase>=1){phase-=1;voiceFrame[(size_t)fill++]=filtered;if(fill==512){probabilities[(size_t)(voiceFrames%probabilities.size())]=voice.analyse(voiceFrame.data());++voiceFrames;fill=0;}}
        }
        while(nextStart+size+rate*.256<=samples){
            int64_t first=std::max<int64_t>(0,(int64_t)std::floor((nextStart/rate-.224)/.032));
            int64_t end=(int64_t)std::ceil(((nextStart+size)/rate+.224)/.032);
            if(end>voiceFrames)break;
            bool quiet=first>=voiceFrames-(int64_t)probabilities.size();
            for(auto i=first;quiet&&i<end;++i)quiet=probabilities[(size_t)(i%probabilities.size())]<.25f;
            if(lastQuiet>=0&&nextStart-lastQuiet>rate*.12)clearBlock();
            if(quiet){
                double frameEnergy=0;
                for(int c=0;c<channels;++c){auto& data=spectrum[(size_t)c];std::fill(data.begin(),data.end(),0);
                    for(int i=0;i<size;++i)data[(size_t)i]=ring[(size_t)c][(size_t)((nextStart+i)&(capacity-1))]*window[(size_t)i];
                    fft->performRealOnlyForwardTransform(data.data(),true);
                    for(int k=0;k<bins;++k)frameEnergy+=(double)data[(size_t)k*2]*data[(size_t)k*2]+(double)data[(size_t)k*2+1]*data[(size_t)k*2+1];}
                // Digital silence does not erase the last usable ambience.
                bool usable=frameEnergy>size*(double)size*1e-12&&!(count>6&&frameEnergy>energySum/count*10);
                if(usable){
                    ++count;lastQuiet=nextStart;energySum+=frameEnergy;
                    double db=10*std::log10(frameEnergy);levelSum+=db;levelSquared+=db*db;
                    for(int k=0;k<bins;++k){
                        for(int c=0;c<2;++c){auto& data=spectrum[(size_t)std::min(c,channels-1)];sum[(size_t)c][(size_t)k]+=(double)data[(size_t)k*2]*data[(size_t)k*2]+(double)data[(size_t)k*2+1]*data[(size_t)k*2+1];}
                        auto& left=spectrum[0];auto& right=spectrum[(size_t)(channels-1)];cross[(size_t)k]+=(double)left[(size_t)k*2]*right[(size_t)k*2]+(double)left[(size_t)k*2+1]*right[(size_t)k*2+1];}
                    if(count*hop>=rate*.4){publish(makeProfile());clearBlock();}
                }
            }
            nextStart+=hop;
        }
    }
private:
    NoiseProfile makeProfile(){
        NoiseProfile p;p.sampleRate=rate;p.fftSize=size;p.sourceChannels=channels;p.quietSeconds=count*hop/rate;
        p.power.resize((size_t)bins);p.ambienceCorrelation.resize((size_t)bins);
        for(int c=0;c<2;++c){p.ambiencePower[(size_t)c].resize((size_t)bins);for(int k=0;k<bins;++k)p.ambiencePower[(size_t)c][(size_t)k]=(float)(sum[(size_t)c][(size_t)k]/count);}
        for(int k=0;k<bins;++k){p.power[(size_t)k]=std::max(p.ambiencePower[0][(size_t)k],p.ambiencePower[1][(size_t)k]);p.ambienceCorrelation[(size_t)k]=(float)juce::jlimit(-1.0,1.0,cross[(size_t)k]/std::max(1e-30,std::sqrt(sum[0][(size_t)k]*sum[1][(size_t)k])));}
        p.variationDb=(float)juce::jlimit(0.0,3.0,std::sqrt(std::max(0.0,levelSquared/count-std::pow(levelSum/count,2))));
        return p;
    }
    void clearBlock(){count=0;energySum=levelSum=levelSquared=0;lastQuiet=-1;for(auto& values:sum)std::fill(values.begin(),values.end(),0);std::fill(cross.begin(),cross.end(),0);}
    double rate,phase=0,energySum=0,levelSum=0,levelSquared=0;
    int channels,size=0,hop=0,bins=0,capacity=0,fill=0,count=0;
    int64_t samples=0,nextStart=0,voiceFrames=0,lastQuiet=-1;
    VoiceDetector& voice;
    juce::dsp::IIR::Filter<float> lowpass;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::array<float,512> voiceFrame{},probabilities{};
    std::array<std::vector<float>,2> ring,spectrum;
    std::array<std::vector<double>,2> sum;
    std::vector<double> cross;
    std::vector<float> window;
};
}

struct ReferenceLearner::State {
    juce::AbstractFifo fifo{queueSize};std::array<Packet,queueSize> packets;
    std::atomic<uint64_t> generation{1},sequence{0},submitted{0},completed{0},publishedGeneration{0},minimumGeneration{1},version{0};
    std::atomic<bool> failed{false};std::atomic<int> cleanupDelay{0};
    juce::SpinLock profileLock;NoiseProfile profile;
};
struct ReferenceLearner::Worker final:juce::Thread {
    explicit Worker(std::shared_ptr<State> s):Thread("NoiseBase reference analysis"),state(std::move(s)){}
    void run() override {
        try{
            std::unique_ptr<VoiceDetector> voice;std::unique_ptr<Analysis> analysis;
            uint64_t generation=0,previous=0;double rate=0;int channels=0;NoiseProfile current;
            while(!threadShouldExit()){
                if(state->fifo.getNumReady()==0){wait(1000);continue;}
                int a,n1,b,n2;state->fifo.prepareToRead(1,a,n1,b,n2);auto packet=state->packets[(size_t)a];state->fifo.finishedRead(1);
                if(packet.generation!=state->generation.load()||packet.sequence+(uint64_t)std::ceil(packet.rate*.25)<state->sequence.load()){analysis.reset();state->completed.store(packet.sequence);continue;}
                if(!voice)voice=std::make_unique<VoiceDetector>();
                if(!analysis||generation!=packet.generation||previous+(uint64_t)packet.count!=packet.sequence||rate!=packet.rate||channels!=packet.channels){
                    analysis=std::make_unique<Analysis>(packet.rate,packet.channels,*voice);current={};generation=packet.generation;rate=packet.rate;channels=packet.channels;
                }
                previous=packet.sequence;
                analysis->add(packet,[&](NoiseProfile p){
                    if(!validAmbienceProfile(p)||generation!=state->generation.load())return;
                    if(validAmbienceProfile(current)&&!differentNoise(noiseBands(p.power,p.sampleRate,p.fftSize),noiseBands(current.power,current.sampleRate,current.fftSize))){
                        for(int c=0;c<2;++c)for(size_t k=0;k<p.power.size();++k)p.ambiencePower[(size_t)c][k]=.65f*current.ambiencePower[(size_t)c][k]+.35f*p.ambiencePower[(size_t)c][k];
                        for(size_t k=0;k<p.power.size();++k){p.power[k]=.65f*current.power[k]+.35f*p.power[k];p.ambienceCorrelation[k]=.65f*current.ambienceCorrelation[k]+.35f*p.ambienceCorrelation[k];}
                        p.quietSeconds+=current.quietSeconds;p.variationDb=.65f*current.variationDb+.35f*p.variationDb;
                    }
                    current=p;
                    const juce::SpinLock::ScopedLockType lock(state->profileLock);
                    state->profile=std::move(p);state->publishedGeneration.store(generation);++state->version;
                });
                state->completed.store(packet.sequence);
            }
            int delay=state->cleanupDelay.exchange(0);if(delay>0)sleep(delay);
        }catch(...){state->failed.store(true);}
    }
    std::shared_ptr<State> state;
};
ReferenceLearner::ReferenceLearner():state(std::make_shared<State>()){}
ReferenceLearner::~ReferenceLearner(){
    if(!worker)return;worker->signalThreadShouldExit();worker->notify();
    if(!worker->waitForThreadToExit(100)){auto* remaining=worker.release();deferNoiseBaseCleanup([remaining]{remaining->waitForThreadToExit(-1);delete remaining;});}
}
void ReferenceLearner::prepare(double sr){rate=sr;++state->generation;if(!worker||!worker->isThreadRunning()){worker.reset();state->failed.store(false);worker=std::make_unique<Worker>(state);worker->startThread();}}
void ReferenceLearner::reset(){auto generation=++state->generation;state->minimumGeneration.store(generation);}
void ReferenceLearner::push(const juce::AudioBuffer<float>& input){
    int channels=std::min(2,input.getNumChannels());if(!channels)return;bool written=false;
    for(int offset=0;offset<input.getNumSamples();offset+=packetSize){int count=std::min(packetSize,input.getNumSamples()-offset);auto sequence=state->sequence.fetch_add((uint64_t)count)+(uint64_t)count;int a,n1,b,n2;state->fifo.prepareToWrite(1,a,n1,b,n2);
        if(n1){auto& packet=state->packets[(size_t)a];packet.count=count;packet.channels=channels;packet.rate=rate;packet.generation=state->generation.load();packet.sequence=sequence;
            for(int c=0;c<channels;++c)std::copy_n(input.getReadPointer(c)+offset,count,packet.audio[(size_t)c].begin());state->fifo.finishedWrite(1);state->submitted.store(sequence);written=true;}}
    if(written&&worker)worker->notify();
}
bool ReferenceLearner::readProfile(NoiseProfile& dest,uint64_t& version){
    if(!ready()||version==state->version.load())return false;
    const juce::SpinLock::ScopedTryLockType lock(state->profileLock);if(!lock.isLocked()||!ready())return false;
    dest=state->profile;version=state->version.load();return true;
}
NoiseProfile ReferenceLearner::snapshot() const {const juce::SpinLock::ScopedLockType lock(state->profileLock);return ready()?state->profile:NoiseProfile{};}
bool ReferenceLearner::ready() const{return state->publishedGeneration.load()>=state->minimumGeneration.load();}
bool ReferenceLearner::failed() const{return state->failed.load();}
uint64_t ReferenceLearner::updates() const{return state->version.load();}
bool ReferenceLearner::waitForIdleForTest(int milliseconds) const {auto end=juce::Time::getMillisecondCounterHiRes()+milliseconds;while(state->completed.load()<state->submitted.load()&&!failed()&&juce::Time::getMillisecondCounterHiRes()<end)juce::Thread::sleep(1);return !failed()&&state->completed.load()>=state->submitted.load();}
void ReferenceLearner::delayCleanupForTest(int milliseconds){state->cleanupDelay.store(milliseconds);}
