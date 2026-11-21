#pragma once
#include "ProceduralNoise.h"
#include "NoiseTracking.h"
namespace juce {class AudioProcessor;}

bool validNoiseFloor(const NoiseProfile&);
juce::ValueTree noiseFloorState(const NoiseProfile&);
bool readNoiseFloor(const juce::ValueTree&,NoiseProfile&);

// Per-instance statistics only. Audio uses try-locks and preallocated profiles;
// state encoding/decoding and all allocations run outside the audio callback.
class ProfileCache {
public:
    static constexpr int capacity=64;
    enum Recall {busy=-1,missing=0,unchanged=1,found=2};
    struct Result {uint64_t id=0;bool changed=false;};
    explicit ProfileCache(bool ambience);
    uint64_t epoch() const{return generation.load();}
    int count() const{return entries.load();}
    bool hasTimedRegions() const{return timedEntries.load()>0;}
    Recall recall(double position,uint64_t& current,NoiseProfile& output);
    Result remember(const NoiseProfile&,double position,double begin,uint64_t epoch);
    bool observe(uint64_t id,double position,uint64_t epoch);
    void clear();
    NoiseProfile latest() const;
    juce::ValueTree state() const;
    bool restore(const juce::ValueTree&);
private:
    struct Entry {NoiseProfile profile;NoiseBands bands{};double begin=0,end=0,notifiedEnd=0;float balance=0,correlation=0;uint64_t id=0,used=0;bool timed=false,valid=false;};
    static NoiseBands bandsFor(const NoiseProfile&);
    static std::pair<float,float> stereoFor(const NoiseProfile&);
    int at(double position) const;
    bool accepts(const NoiseProfile& p) const{return ambience?validAmbienceProfile(p):validNoiseFloor(p);}
    bool ambience;
    mutable juce::SpinLock lock;
    std::array<Entry,capacity> slots;
    std::atomic<int> entries{0},timedEntries{0};std::atomic<uint64_t> generation{1};
    uint64_t nextId=1,serial=0,latestId=0;
};

juce::var runCacheTests();
bool verifyHostedCache(juce::AudioProcessor&,juce::DynamicObject&,const juce::File& stateFile);
bool verifyRestoredCache(juce::AudioProcessor&,juce::DynamicObject&,const juce::File& stateFile);
