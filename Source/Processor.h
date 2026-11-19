#pragma once
#include "Offline.h"
#include "TapeReaderSource.h"

class NoiseBaseProcessor final : public juce::AudioProcessor {
public:
    NoiseBaseProcessor();
    ~NoiseBaseProcessor() override;
    juce::AudioProcessorValueTreeState params;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override {return params.getParameter("bypass");}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override {return true;}
    const juce::String getName() const override {return "NoiseBase";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override {return .15;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return {};}
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    void resetProfile() {resetRequested.store(true);}
    void loadRecording(const juce::File&);
    void scanRecording();
    void saveRecording(const juce::File&);
    void togglePlayback();
    void stopPlayback();
    void returnToStart() {if(fileMode.load())transport.setPosition(0);}
    void setLooping(bool);
    bool isLooping() const {return loopPlayback.load();}
    double playbackPosition() const {return transport.getCurrentPosition();}
    double playbackDuration() const {return transport.getLengthInSeconds();}
    bool isPlaying() const {return transport.isPlaying();}
    juce::String getStatus() const;
    juce::String getFileName() const;
    std::atomic<float> inputLevel{0},outputLevel{0},reductionLevel{0},jobProgress{0},quietSeconds{0};
    std::atomic<bool> standaloneMode{false},fileMode{false},busy{false},analysed{false},detectorAvailable{false};
    std::atomic<bool> detectorFailed{false};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float value(const char* id) const {return params.getRawParameterValue(id)->load();}
    void setStatus(const juce::String&);
    void queueProfile(const NoiseProfile&);
    void processAudio(juce::AudioBuffer<float>&,bool forceBypass);
    DenoiseEngine engine;
    VoiceWorker voice;
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    juce::TimeSliceThread readAhead{"NoiseBase file playback"};
    std::unique_ptr<TapeReaderSource> readerSource;
    std::atomic<bool> loopPlayback{false};
    juce::ThreadPool jobs{1};
    mutable juce::CriticalSection statusLock,analysisLock;
    juce::String status="AUTO listens for quieter sections",fileName="NO TAPE LOADED";
    juce::File loadedFile;
    std::unique_ptr<FileAnalysis> analysis;
    juce::SpinLock pendingLock;
    NoiseProfile pendingProfile;
    std::atomic<bool> pending{false},resetRequested{false},cancel{false};
};
