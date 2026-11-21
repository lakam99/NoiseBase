#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class NoiseBaseUpdater final : private juce::Thread {
public:
    NoiseBaseUpdater();
    ~NoiseBaseUpdater() override;
    bool request(bool prepare, std::function<void(juce::var)> completion);
    static bool installAndRestart(const juce::var& prepared, juce::String& error);
    std::atomic<double> progress{-1};
    std::atomic<int> phase{0};
    juce::String progressStage() const {static constexpr const char* stages[]{"CHECKING","DOWNLOADING","VERIFYING","EXTRACTING","CHECKING FILES","READY"};return stages[juce::jlimit(0,5,phase.load())];}
private:
    void run() override;
    bool preparing=false;
    std::function<void(juce::var)> callback;
};
