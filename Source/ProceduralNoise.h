#pragma once
#include "DenoiseEngine.h"
#include <juce_data_structures/juce_data_structures.h>
#include <random>

bool validAmbienceProfile(const NoiseProfile&);
juce::ValueTree ambienceProfileState(const NoiseProfile&);
bool readAmbienceProfile(const juce::ValueTree&,NoiseProfile&);

// Independent Gaussian spectra, Hann overlap-add and slowly moving band levels.
// The only source material is a compact statistical profile. Memory is bounded.
class ProceduralNoise {
public:
    void prepare(double sampleRate,int channels,uint64_t seed=0);
    bool setProfile(const NoiseProfile&); // preallocated; safe on the audio thread
    void next(float* channels);
    bool ready() const {return hasProfile;}
    void setSlewSeconds(float seconds){slewSeconds=seconds;}
    void clearProfile(bool discardHistory=false){for(auto& values:target)std::fill(values.begin(),values.end(),0);hasProfile=false;
        if(discardHistory){for(auto& values:power)std::fill(values.begin(),values.end(),0);for(auto& values:overlap)std::fill(values.begin(),values.end(),0);countdown=0;position=0;modulation.fill(0);}}
private:
    void frame();
    double rate=48000;
    int size=2048,hop=512,channels=2,position=0,countdown=0,capacity=8192;
    bool hasProfile=false;
    float variation=0,slewSeconds=.15f;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window,correlation;
    std::array<std::vector<float>,2> target,power,data,overlap;
    std::array<float,8> modulation{};
    std::mt19937_64 random;
    std::normal_distribution<float> gaussian{0,1};
};
