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

class VoiceWorker : private juce::Thread {
public:
    VoiceWorker();
    ~VoiceWorker() override;
    void prepare(double sr);
    void push(const juce::AudioBuffer<float>&);
    void waitForPending();
    std::atomic<float> probability{1};
    std::atomic<bool> available{false};
    std::atomic<bool> failed{false};
private:
    void run() override;
    juce::AbstractFifo fifo{32768};
    std::array<float,32768> samples{};
    double rate=48000, phase=0;
    juce::dsp::IIR::Filter<float> lowpass;
    std::atomic<bool> inFlight{false};
};
