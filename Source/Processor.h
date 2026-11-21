#pragma once
#include "Offline.h"
#include "TapeReaderSource.h"
#include "ReferenceLearner.h"
#include "ReferenceBridge.h"

class NoiseBaseProcessor final : public juce::AudioProcessor,private juce::AsyncUpdater,private juce::AudioProcessorValueTreeState::Listener {
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
    double getTailLengthSeconds() const override {
        if(proceduralSelected()&&!externalReferenceSelected())return std::numeric_limits<double>::infinity();
        int delay=bridgeLatencySamples.load();
        // Include buffered activity, its fade and a guard for block-sized edges.
        return delay>0?.25+(delay+bridgeBaseLatency.load())/bridgeSampleRate.load():.15;
    }
    int bridgeDelaySamples() const{return bridgeLatencySamples.load();}
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
    bool externalReferenceSelected() const {return !standaloneMode.load()&&value("externalReference")>.5f;}
    bool waitForReferenceForTest(int ms){return reference.waitForIdleForTest(ms);}
    double playbackPosition() const {return proceduralSelected()?ambienceSamples.load()/playbackRate:transport.getCurrentPosition();}
    double playbackDuration() const {return proceduralSelected()?0:transport.getLengthInSeconds();}
    bool isPlaying() const {return proceduralSelected()?ambiencePlaying.load():transport.isPlaying();}
    juce::String getStatus() const;
    juce::String getFileName() const;
    std::atomic<float> inputLevel{0},outputLevel{0},reductionLevel{0},jobProgress{0},quietSeconds{0};
    std::atomic<bool> standaloneMode{false},fileMode{false},busy{false},analysed{false},detectorAvailable{false};
    std::atomic<bool> detectorFailed{false};
    std::atomic<bool> ambienceReady{false};
    std::atomic<bool> referenceConnected{false},referenceFailed{false};
    std::atomic<bool> referenceSignal{false},referenceTransportKnown{false},referenceTransportRunning{true};
    std::atomic<double> hostPositionSeconds{-1};
    std::atomic<bool> referenceBridging{false},referenceOutputActive{false};
    std::atomic<int> noiseRegionCount{0},activeNoiseRegion{0};
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    float value(const char* id) const {return params.getRawParameterValue(id)->load();}
    void setStatus(const juce::String&);
    void queueProfile(const NoiseProfile&,bool external=false);
    NoiseProfile snapshotReference() const;
    void processAudio(juce::AudioBuffer<float>&,bool forceBypass);
    void parameterChanged(const juce::String&,float) override {triggerAsyncUpdate();}
    void handleAsyncUpdate() override {refreshBridgeLatency();}
    void refreshBridgeLatency();
    void resetBridgeTiming();
    void applyBridgeProfiles(int64_t outputPosition);
    DenoiseEngine engine;
    ProceduralNoise generator;
    juce::SmoothedValue<float> proceduralFade,ambienceGain;
    NoiseProfile storedProfile;
    bool storedProfileExternal=false,pendingExternal=false,wasExternal=false;
    NoiseProfile fileGeneratorProfile,externalProfile;
    NoiseProfile activeReferenceProfile;
    mutable juce::SpinLock activeReferenceLock;
    std::atomic<bool> referenceSnapshotValid{false};
    std::atomic<bool> referenceFallbackAllowed{false};
    bool referenceSnapshotDirty=false;
    bool wasReferenceActive=false,referenceHaveExpectedPosition=false;
    int64_t referenceExpectedPosition=0;
    uint64_t externalVersion=0;
    struct ProfileEvent {NoiseProfile profile;int64_t position=0;};
    std::array<ProfileEvent,16> bridgeProfiles;
    int bridgeProfileHead=0,bridgeProfileCount=0,processedBridgeDelay=0;
    NoiseProfile incomingReferenceProfile;
    ReferenceBridge bridge;
    std::atomic<int> bridgeLatencySamples{0};
    std::atomic<double> bridgeSampleRate{48000};
    std::atomic<int> bridgeBaseLatency{2048};
    std::atomic<bool> preparingAudio{false};
    bool wasTransportRunning=true;
    ReferenceLearner reference;
    std::atomic<bool> referenceResetRequested{false};
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
