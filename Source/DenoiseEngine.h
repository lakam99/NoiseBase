#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>

struct NoiseProfile {
    double sampleRate = 0;
    int fftSize = 0;
    std::vector<float> power;
    double quietSeconds = 0;
    bool estimated = false;
    // Aggregate quiet-frame statistics only; no recorded waveform is retained.
    std::array<std::vector<float>,2> ambiencePower;
    std::vector<float> ambienceCorrelation;
    int sourceChannels = 1;
    float variationDb = 0;
};

class DenoiseEngine {
public:
    void prepare(double sampleRate, int channels);
    void resetNoise();
    bool setProfile(const NoiseProfile&,bool clearHistory=false);
    bool copyLearnedProfile(NoiseProfile&) const; // caller preallocates power
    void process(juce::AudioBuffer<float>&, float speech, float reductionDb,
                 float preserve, float mix, float outputDb, bool autoLearn, bool bypass, bool noiseOnly=false);
    int latency() const { return size; }
    int fftSize() const { return size; }
    int hopSize() const { return hop; }
    double quietTime() const { return learnedSeconds; }
    float reductionMeter() const { return meterReduction; }
    float inputMeter() const { return meterInput; }
    float outputMeter() const { return meterOutput; }
    int automaticChanges() const {return changes;}
private:
    void frame(float speech, float cap, float preserve, bool autoLearn);
    double rate = 48000, learnedSeconds = 0,candidateSeconds=0,speechHangover=0;
    int changes=0;
    int size = 2048, hop = 512, channelCount = 2, inputPos = 0, outputPos = 0, untilFrame = 0;
    int outputCapacity = 8192, minimumAge = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, noise, minimum, smoothedPower, power, gain, priorClean, priorGain, frequencyScratch;
    std::vector<float> candidate;
    std::vector<std::vector<float>> input, output, fftData;
    bool profileReady = false;
    float meterInput = 0, meterOutput = 0, meterReduction = 0;
    juce::SmoothedValue<float> mixSmooth, outputSmooth, bypassSmooth, noiseOnlySmooth;
};
