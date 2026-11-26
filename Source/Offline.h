#pragma once
#include "DenoiseEngine.h"
#include "VoiceDetector.h"
#include "ProceduralNoise.h"
#include "NoiseTimeline.h"

struct FileAnalysis {
    NoiseProfile profile;
    juce::File source;
    int64_t frames=0;
    double sampleRate=0;
    int channels=0, bits=24;
    bool floating=false;
    std::vector<float> voice;
    std::shared_ptr<const NoiseRegions> regions;
};

FileAnalysis analyseRecording(const juce::File&,std::atomic<float>* progress=nullptr,std::atomic<bool>* cancel=nullptr);
void exportRecording(const FileAnalysis&,const juce::File& output,float reduction,float preserve,
                     float mix,float outputDb,std::atomic<float>* progress=nullptr,std::atomic<bool>* cancel=nullptr,bool noiseOnly=false,bool followTrack=true,uint64_t* rustleFrames=nullptr);
juce::var runEngineTests();
juce::var runReductionMeterTests();
juce::var runBackgroundArtifactTests();
juce::var runNoiseOutputTests();
void exportAmbience(const NoiseProfile&,const juce::File&,double seconds,float mix,float outputDb,std::atomic<float>* progress=nullptr,std::atomic<bool>* cancel=nullptr,uint64_t seed=0);
juce::var runProceduralTests();
juce::var runTrackTests();
juce::var runHostSafetyTests();
juce::var runRustleTests();
