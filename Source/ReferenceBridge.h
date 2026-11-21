#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Fixed, bounded lookahead. The adjustable gap horizon never changes latency.
// A gap is latched at its start, so a distant clip cannot cause premature re-entry.
class ReferenceBridge {
public:
    static constexpr double maximumSeconds=2.0;
    void prepare(double rate,int baseLatency){
        capacity=juce::nextPowerOfTwo((int)std::ceil(rate*maximumSeconds)+baseLatency+2);
        signal.assign((size_t)capacity,0);prefix.assign((size_t)capacity,0);
        for(auto& channel:audio)channel.assign((size_t)capacity,0);reset();
    }
    void reset(){position=0;rendered=-1;count=0;previous=false;bridging=false;for(auto& c:audio)std::fill(c.begin(),c.end(),0);std::fill(signal.begin(),signal.end(),0);std::fill(prefix.begin(),prefix.end(),0);}
    bool step(bool input,float* main,int channels,int delay,int totalLatency,int horizon){
        auto index=(size_t)(position&(capacity-1));signal[index]=(uint8_t)input;count+=(uint64_t)input;prefix[(size_t)((position+1)&(capacity-1))]=count;
        for(int c=0;c<channels;++c){audio[(size_t)c][index]=main[c];main[c]=position>=delay?audio[(size_t)c][(size_t)((position-delay)&(capacity-1))]:0;}
        rendered=position-totalLatency;++position;if(rendered<0){previous=false;bridging=false;return false;}
        bool current=signal[(size_t)(rendered&(capacity-1))]!=0;
        auto future=prefix[(size_t)((rendered+horizon+1)&(capacity-1))]-prefix[(size_t)((rendered+1)&(capacity-1))];
        if(current)bridging=false;else if(previous)bridging=future>0;
        previous=current;return current||bridging;
    }
    int64_t inputPosition() const{return position;}
    int64_t outputPosition() const{return rendered;}
    bool isBridging() const{return bridging;}
private:
    int capacity=1;int64_t position=0,rendered=-1;uint64_t count=0;bool previous=false,bridging=false;
    std::vector<uint8_t> signal;std::vector<uint64_t> prefix;std::array<std::vector<float>,2> audio;
};
