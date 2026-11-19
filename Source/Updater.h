#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class NoiseBaseUpdater final : private juce::Thread {
public:
    NoiseBaseUpdater();
    ~NoiseBaseUpdater() override;
    bool request(bool prepare, std::function<void(juce::var)> completion);
    static bool installAndRestart(const juce::var& prepared, juce::String& error);
private:
    void run() override;
    bool preparing=false;
    std::function<void(juce::var)> callback;
};
