#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>

class VoiceDetector {
public:
    VoiceDetector();
    ~VoiceDetector();
    void reset();
    float analyse(const float* frame512);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

class VoiceWorker {
public:
    VoiceWorker();
    ~VoiceWorker();
    void prepare(double sr);
    void resetHistory();
    void push(const juce::AudioBuffer<float>&);
    float probability() const;
    bool available() const;
    bool failed() const;
    int modelLoads() const;
    // Deterministic native lifecycle verification; not a host parameter.
    void delayStartupForTest(int milliseconds);
    void delayCleanupForTest(int milliseconds);
    bool stalledForTest() const;
private:
    struct State;
    struct Worker;
    std::shared_ptr<State> state;
    std::unique_ptr<Worker> worker;
    std::array<float,512> frame{};
    int fill=0;
    double rate=48000, phase=0;
    juce::dsp::IIR::Filter<float> lowpass;
};
