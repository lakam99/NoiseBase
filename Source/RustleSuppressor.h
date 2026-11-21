#pragma once
#include <vector>
#include <cstdint>

// Detect short, broadband friction bursts, independently of the stationary
// floor. All storage is prepared up front; no extra delay or worker is used.
class RustleSuppressor {
public:
    void prepare(double sampleRate,int fftSize,int hopSize);
    void reset();
    void analyse(const std::vector<float>& power);
    float gainForBin(int bin,float maximumDb,float speech,float preserve) const;
    bool excludesLearning() const {return confidence>.15f;}
    float activity() const {return confidence;}
    uint64_t activeFrames() const {return detectedFrames;}
private:
    double rate=48000,seconds=0,background=0,age=0,quietAge=0,peak=0,valley=0;
    int size=2048,hop=512,first=0,last=0,low=0,width=1;
    bool initialized=false,event=false,repeated=false;
    float confidence=0,textureConfidence=0;
    uint64_t detectedFrames=0;
    std::vector<double> prefix;
    std::vector<float> spectrum,tonalHistory;
};
