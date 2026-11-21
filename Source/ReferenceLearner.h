#pragma once
#include "ProceduralNoise.h"
#include "VoiceDetector.h"

// A bounded audio-to-worker queue. Neural analysis and FFT/profile construction
// stay off the host callback; only compact statistics return to the generator.
class ReferenceLearner {
public:
    ReferenceLearner();
    ~ReferenceLearner();
    void prepare(double sampleRate);
    void push(const juce::AudioBuffer<float>&);
    void reset(); // audio-thread request; clears the learned reference
    bool readProfile(NoiseProfile&,uint64_t& version); // nonblocking, preallocate destination
    NoiseProfile snapshot() const; // state saving/export, never called by audio
    bool ready() const;
    bool failed() const;
    uint64_t updates() const;
    bool waitForIdleForTest(int milliseconds) const;
    void delayCleanupForTest(int milliseconds);
private:
    struct State;
    struct Worker;
    std::shared_ptr<State> state;
    std::unique_ptr<Worker> worker;
    double rate=48000;
};

juce::var runReferenceTests();
bool verifyHostedReference(juce::AudioProcessor&,juce::DynamicObject& report);
