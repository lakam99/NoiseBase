#pragma once
#include "DenoiseEngine.h"
#include "VoiceDetector.h"

struct FileAnalysis {
    NoiseProfile profile;
    juce::File source;
    int64_t frames=0;
    double sampleRate=0;
    int channels=0, bits=24;
    bool floating=false;
    std::vector<float> voice;
};

FileAnalysis analyseRecording(const juce::File&,std::atomic<float>* progress=nullptr,std::atomic<bool>* cancel=nullptr);
void exportRecording(const FileAnalysis&,const juce::File& output,float reduction,float preserve,
                     float mix,float outputDb,std::atomic<float>* progress=nullptr,std::atomic<bool>* cancel=nullptr);
juce::var runEngineTests();
