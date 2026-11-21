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
    void learnTrack();
    void serviceJobs(); // message-thread handoff; never called by processBlock
    void delaySpeechStartupForTest(int milliseconds){voice.delayStartupForTest(milliseconds);}
    void loadRecording(const juce::File&);
    void scanRecording();
    void saveRecording(const juce::File&,double ambienceSeconds=60);
    void togglePlayback();
    void stopPlayback();
    void returnToStart() {if(fileMode.load())transport.setPosition(0);ambienceSamples.store(0);}
    void setLooping(bool);
    bool isLooping() const {return loopPlayback.load();}
    bool proceduralSelected() const {return value("noiseOnly")>.5f&&value("procedural")>.5f;}
    double playbackPosition() const {return proceduralSelected()?ambienceSamples.load()/playbackRate:transport.getCurrentPosition();}
    double playbackDuration() const {return proceduralSelected()?0:transport.getLengthInSeconds();}
    bool isPlaying() const {return proceduralSelected()?ambiencePlaying.load():transport.isPlaying();}
    juce::String getStatus() const;
    juce::String getFileName() const;
    std::atomic<float> inputLevel{0},outputLevel{0},reductionLevel{0},jobProgress{0},quietSeconds{0};
    std::atomic<bool> standaloneMode{false},fileMode{false},busy{false},analysed{false},detectorAvailable{false};
    std::atomic<bool> detectorFailed{false};
    std::atomic<bool> ambienceReady{false};
    std::atomic<int> noiseRegionCount{0},activeNoiseRegion{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float value(const char* id) const {return params.getRawParameterValue(id)->load();}
    void setStatus(const juce::String&);
    void queueProfile(const NoiseProfile&);
    void processAudio(juce::AudioBuffer<float>&,bool forceBypass);
    DenoiseEngine engine;
    ProceduralNoise generator;
    juce::SmoothedValue<float> proceduralFade,ambienceGain;
    NoiseProfile storedProfile;
    mutable juce::CriticalSection profileStateLock;
    std::atomic<bool> ambiencePlaying{false};
    std::atomic<int64_t> ambienceSamples{0};
    double playbackRate=48000;
    bool wasProcedural=false,voiceActive=false;
    VoiceWorker voice;
    juce::AudioFormatManager formats;
    juce::AudioTransportSource transport;
    juce::TimeSliceThread readAhead{"NoiseBase file playback"};
    std::unique_ptr<TapeReaderSource> readerSource;
    std::atomic<bool> loopPlayback{false};
    struct FileJobState;
    std::unique_ptr<juce::ThreadPool> jobs;
    std::shared_ptr<FileJobState> activeJob;
    mutable juce::CriticalSection statusLock,analysisLock;
    juce::String status="AUTO listens for quieter sections",fileName="NO TAPE LOADED";
    juce::File loadedFile;
    std::unique_ptr<FileAnalysis> analysis;
    juce::SpinLock regionLock;
    std::shared_ptr<const NoiseRegions> noiseRegions;
    std::shared_ptr<const std::vector<float>> fileVoice;
    int selectedRegion=-1;
    juce::SpinLock pendingLock;
    NoiseProfile pendingProfile;
    std::atomic<bool> pending{false},resetRequested{false};
};
