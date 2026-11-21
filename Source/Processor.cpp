#include "Processor.h"
#include "Editor.h"
#include "AsyncCleanup.h"

namespace {
class FunctionJob:public juce::ThreadPoolJob {
public:
    explicit FunctionJob(std::function<void()> f):ThreadPoolJob("NoiseBase file job"),function(std::move(f)){}
    JobStatus runJob() override {function();return jobHasFinished;}
private:std::function<void()> function;
};
}
struct NoiseBaseProcessor::FileJobState {
    std::atomic<bool> cancel{false},finished{false};
    std::atomic<float> progress{0};
    std::unique_ptr<FileAnalysis> result;
    juce::String status;
};

juce::AudioProcessorValueTreeState::ParameterLayout NoiseBaseProcessor::layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"reduction",1},"Reduction",juce::NormalisableRange<float>(0,24,.1f),14));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"preserve",1},"Voice preserve",juce::NormalisableRange<float>(0,100,.1f),80));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1},"Mix",juce::NormalisableRange<float>(0,100,.1f),100));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output",juce::NormalisableRange<float>(-12,12,.1f),0));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"auto",1},"Automatic noise learning",true));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"skin",1},"Faceplate",juce::StringArray{"Ivory","Graphite","Walnut"},0));
    // Append to retain the existing parameter order and IDs in saved projects.
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"noiseOnly",1},"Noise only",false));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"procedural",1},"Procedural noise",true));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"externalReference",1},"External reference",false));
    return result;
}
NoiseBaseProcessor::NoiseBaseProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true).withInput("Reference",juce::AudioChannelSet::stereo(),false)),params(*this,nullptr,"NoiseBaseState",layout()) {
    formats.registerBasicFormats();
}
NoiseBaseProcessor::~NoiseBaseProcessor(){
    if(activeJob)activeJob->cancel.store(true);
    if(jobs){auto* pool=jobs.release();deferNoiseBaseCleanup([pool]{pool->removeAllJobs(true,-1);delete pool;});}
    transport.stop();transport.setSource(nullptr);if(readAhead.isThreadRunning())readAhead.stopThread(5000);
}
void NoiseBaseProcessor::prepareToPlay(double rate,int block) {
    engine.prepare(rate,getTotalNumOutputChannels());setLatencySamples(engine.latency());
    generator.prepare(rate,getTotalNumOutputChannels());playbackRate=rate;
    proceduralFade.reset(rate,.035);proceduralFade.setCurrentAndTargetValue(0);ambienceGain.reset(rate,.035);ambienceGain.setCurrentAndTargetValue(0);wasProcedural=false;
    voice.prepare(rate);voiceActive=false;transport.prepareToPlay(block,rate);
    reference.prepare(rate);externalVersion=0;wasExternal=false;
    for(auto* profile:{&fileGeneratorProfile,&externalProfile,&activeReferenceProfile})for(auto* values:{&profile->power,&profile->ambiencePower[0],&profile->ambiencePower[1],&profile->ambienceCorrelation})values->reserve(4097);
    NoiseProfile saved;bool external=false;{const juce::ScopedLock lock(profileStateLock);saved=storedProfile;external=storedProfileExternal;}
    auto active=externalReferenceSelected()&&!referenceResetRequested.load()?snapshotReference():NoiseProfile{};
    if(validAmbienceProfile(active)){
        const juce::SpinLock::ScopedLockType lock(pendingLock);pendingProfile=std::move(active);pendingExternal=true;pending.store(true,std::memory_order_release);
    }else if(validAmbienceProfile(saved))queueProfile(saved,external);
}
void NoiseBaseProcessor::releaseResources(){transport.releaseResources();}
bool NoiseBaseProcessor::isBusesLayoutSupported(const BusesLayout& buses) const {
    auto in=buses.getMainInputChannelSet(),out=buses.getMainOutputChannelSet();
    auto reference=buses.getChannelSet(true,1);
    return in==out && (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo())
        && (reference.isDisabled()||reference==juce::AudioChannelSet::mono()||reference==juce::AudioChannelSet::stereo());
}
void NoiseBaseProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    processAudio(buffer,false);
}
void NoiseBaseProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    processAudio(buffer,true);
}
void NoiseBaseProcessor::processAudio(juce::AudioBuffer<float>& hostBuffer,bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    auto buffer=getBusBuffer(hostBuffer,false,0);
    bool external=externalReferenceSelected();
    if(external!=wasExternal){
        if(external){
            bool held=value("auto")<=.5f&&referenceSnapshotValid.load()&&validAmbienceProfile(externalProfile);
            if(held){generator.setProfile(externalProfile);ambienceReady.store(true);}
            else{generator.clearProfile();ambienceReady.store(false);referenceSnapshotValid.store(false);}
            externalVersion=0;
        }
        else{if(!generator.setProfile(fileGeneratorProfile))generator.clearProfile();ambienceReady.store(validAmbienceProfile(fileGeneratorProfile));}
        wasExternal=external;
    }
    if(referenceResetRequested.exchange(false)){reference.reset();externalVersion=0;generator.clearProfile();ambienceReady.store(false);referenceSnapshotValid.store(false);referenceSnapshotDirty=false;}
    bool connected=!standaloneMode.load()&&getBusCount(true)>1&&getBus(true,1)->isEnabled();referenceConnected.store(connected);
    if(external&&connected&&value("auto")>.5f){auto source=getBusBuffer(hostBuffer,true,1);reference.push(source);}
    referenceFailed.store(reference.failed());
    bool standaloneFile=standaloneMode.load()&&fileMode.load();
    double filePosition=standaloneFile?transport.getCurrentPosition():0;
    if(standaloneMode.load() && fileMode.load()) {juce::AudioSourceChannelInfo info(&buffer,0,buffer.getNumSamples());transport.getNextAudioBlock(info);}
    if(resetRequested.exchange(false)){engine.resetNoise();voice.resetHistory();voiceActive=true;}
    if(pending.load(std::memory_order_acquire)) {
        const juce::SpinLock::ScopedTryLockType lock(pendingLock);
        if(lock.isLocked()) {
            if(!pendingExternal){fileGeneratorProfile=pendingProfile;if(!engine.setProfile(pendingProfile))engine.resetNoise();}
            if(external==pendingExternal){if(!generator.setProfile(pendingProfile))generator.clearProfile();ambienceReady.store(validAmbienceProfile(pendingProfile));
                if(external){externalProfile=pendingProfile;referenceSnapshotDirty=validAmbienceProfile(pendingProfile);}}
            pending.store(false,std::memory_order_release);
        }
    }
    if(external&&(value("auto")>.5f||!ambienceReady.load())&&reference.readProfile(externalProfile,externalVersion)){generator.setProfile(externalProfile);ambienceReady.store(true);referenceSnapshotDirty=true;}
    if(referenceSnapshotDirty){const juce::SpinLock::ScopedTryLockType lock(activeReferenceLock);if(lock.isLocked()){activeReferenceProfile=externalProfile;referenceSnapshotValid.store(true);referenceSnapshotDirty=false;}}
    bool usingTimeline=false,knownSpeech=false;float speech=1;
    if(standaloneFile){
        const juce::SpinLock::ScopedTryLockType lock(regionLock);
        if(lock.isLocked()&&noiseRegions&&!noiseRegions->empty()){
            double rate=noiseRegions->front().profile.sampleRate;
            int64_t frame=(int64_t)std::llround(std::max(0.0,filePosition-engine.fftSize()/(2*playbackRate))*rate);
            int next=noiseRegionAt(*noiseRegions,frame);
            if(value("auto")>.5f){if(next!=selectedRegion){engine.setProfile((*noiseRegions)[(size_t)next].profile,true);selectedRegion=next;activeNoiseRegion.store(next+1);}usingTimeline=true;}
            if(fileVoice&&!fileVoice->empty()){int index=juce::jlimit(0,(int)fileVoice->size()-1,(int)(filePosition/.032));speech=(*fileVoice)[(size_t)index];knownSpeech=true;}
        }
    }
    bool bypassed=forceBypass||value("bypass")>.5f,procedural=proceduralSelected();
    if(!knownSpeech&&!procedural&&!bypassed){if(!voiceActive)voice.resetHistory();voiceActive=true;voice.push(buffer);speech=voice.probability();}else voiceActive=false;
    engine.process(buffer,speech,value("reduction"),value("preserve")*.01f,
        value("mix")*.01f,value("output"),value("auto")>.5f&&!usingTimeline,forceBypass||value("bypass")>.5f,value("noiseOnly")>.5f);
    if(standaloneFile&&procedural&&!wasProcedural&&transport.isPlaying())ambiencePlaying.store(true);wasProcedural=procedural;
    bool running=procedural&&!bypassed&&(!standaloneMode.load()||ambiencePlaying.load());
    proceduralFade.setTargetValue(procedural&&!bypassed?1.0f:0.0f);
    ambienceGain.setTargetValue(running?value("mix")*.01f*juce::Decibels::decibelsToGain(value("output")):0);
    float generatedPeak=engine.outputMeter();
    if(proceduralFade.isSmoothing()||proceduralFade.getCurrentValue()>0){
        generatedPeak=0;int channels=std::min(buffer.getNumChannels(),getTotalNumOutputChannels());
        for(int i=0;i<buffer.getNumSamples();++i){float sample[2]{};float fade=proceduralFade.getNextValue(),level=ambienceGain.getNextValue();if(running||level>1e-8f)generator.next(sample);
            for(int c=0;c<channels;++c){float result=buffer.getSample(c,i)*(1-fade)+sample[c]*level*fade;buffer.setSample(c,i,result);generatedPeak=std::max(generatedPeak,std::abs(result));}}
    }else{proceduralFade.skip(buffer.getNumSamples());ambienceGain.skip(buffer.getNumSamples());}
    if(running&&standaloneMode.load())ambienceSamples.fetch_add(buffer.getNumSamples());
    inputLevel.store(engine.inputMeter());outputLevel.store(engine.outputMeter());reductionLevel.store(engine.reductionMeter());
    quietSeconds.store((float)engine.quietTime());detectorAvailable.store(knownSpeech||voice.available());detectorFailed.store(!knownSpeech&&voice.failed());
    if(procedural){outputLevel.store(generatedPeak);reductionLevel.store(0);}
}
juce::AudioProcessorEditor* NoiseBaseProcessor::createEditor(){return new NoiseBaseEditor(*this);}
void NoiseBaseProcessor::getStateInformation(juce::MemoryBlock& dest) {auto state=params.copyState();state.removeChild(state.getChildWithName("AmbienceProfile"),nullptr);
    bool external=externalReferenceSelected();NoiseProfile saved=external?snapshotReference():NoiseProfile{};
    {const juce::ScopedLock lock(profileStateLock);if(!validAmbienceProfile(saved)&&storedProfileExternal==external)saved=storedProfile;}
    auto profile=ambienceProfileState(saved);if(profile.isValid()){profile.setProperty("label",external?"SIDECHAIN REFERENCE":getFileName(),nullptr);state.appendChild(profile,nullptr);}
    auto xml=state.createXml();copyXmlToBinary(*xml,dest);}
void NoiseBaseProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(params.state.getType())){
        auto state=juce::ValueTree::fromXml(*xml);
        if(!state.getChildWithProperty("id","noiseOnly").isValid()){
            juce::ValueTree mode("PARAM");mode.setProperty("id","noiseOnly",nullptr);mode.setProperty("value",0.0f,nullptr);state.appendChild(mode,nullptr);
        }
        // Existing 1.4 sessions keep their removed-noise route until selected.
        if(!state.getChildWithProperty("id","procedural").isValid()){juce::ValueTree mode("PARAM");mode.setProperty("id","procedural",nullptr);mode.setProperty("value",0.0f,nullptr);state.appendChild(mode,nullptr);}
        if(!state.getChildWithProperty("id","externalReference").isValid()){juce::ValueTree source("PARAM");source.setProperty("id","externalReference",nullptr);source.setProperty("value",0.0f,nullptr);state.appendChild(source,nullptr);}
        bool external=(float)state.getChildWithProperty("id","externalReference")["value"]>.5f;
        if(external)referenceResetRequested.store(true);
        NoiseProfile restored;auto saved=state.getChildWithName("AmbienceProfile");
        if(readAmbienceProfile(saved,restored)){queueProfile(restored,external);const juce::ScopedLock lock(statusLock);fileName=saved["label"].toString();status="PROFILE RESTORED  /  source clip not required";}
        else {queueProfile(NoiseProfile{},external);}
        params.replaceState(state);
    }
}
void NoiseBaseProcessor::setStatus(const juce::String& text){const juce::ScopedLock lock(statusLock);status=text;}
NoiseProfile NoiseBaseProcessor::snapshotReference() const {const juce::SpinLock::ScopedLockType lock(activeReferenceLock);return referenceSnapshotValid.load()?activeReferenceProfile:NoiseProfile{};}
juce::String NoiseBaseProcessor::getStatus() const {const juce::ScopedLock lock(statusLock);return status;}
juce::String NoiseBaseProcessor::getFileName() const {const juce::ScopedLock lock(statusLock);return fileName;}
void NoiseBaseProcessor::queueProfile(const NoiseProfile& profile,bool external) {
    {const juce::ScopedLock lock(profileStateLock);storedProfile=validAmbienceProfile(profile)?profile:NoiseProfile{};storedProfileExternal=external;}
    if(externalReferenceSelected()==external)ambienceReady.store(validAmbienceProfile(profile));
    const juce::SpinLock::ScopedLockType lock(pendingLock);pendingProfile=profile;pendingExternal=external;pending.store(true,std::memory_order_release);
}
void NoiseBaseProcessor::loadRecording(const juce::File& file) {
    if(busy.load())return;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->numChannels>2 || reader->sampleRate<8000 || reader->lengthInSamples<=0) {setStatus("Use a nonempty mono/stereo WAV, AIFF, FLAC or MP3");return;}
    params.getParameter("externalReference")->setValueNotifyingHost(0);
    stopPlayback();transport.setSource(nullptr);readerSource.reset();queueProfile(NoiseProfile{});
    resetRequested.store(true);
    {const juce::SpinLock::ScopedLockType lock(regionLock);noiseRegions.reset();fileVoice.reset();selectedRegion=-1;}noiseRegionCount.store(0);activeNoiseRegion.store(0);
    params.getParameter("auto")->setValueNotifyingHost(1);
    double rate=reader->sampleRate;
    if(standaloneMode.load()){
        if(!readAhead.isThreadRunning())readAhead.startThread();
        readerSource=std::make_unique<TapeReaderSource>(std::move(reader));readerSource->setLooping(loopPlayback.load());
        transport.setSource(readerSource.get(),32768,&readAhead,rate);
    }
    loadedFile=file;fileMode.store(true);analysed.store(false);
    {const juce::ScopedLock lock(statusLock);fileName=file.getFileName();}
    scanRecording();
}

void NoiseBaseProcessor::scanRecording(){
    if(!fileMode.load()||busy.exchange(true))return;
    jobProgress.store(0);setStatus("SCANNING WHOLE TRACK / LOCATING LOCAL NOISE FLOORS");
    auto state=std::make_shared<FileJobState>();activeJob=state;auto source=loadedFile;
    if(!jobs)jobs=std::make_unique<juce::ThreadPool>(1);
    jobs->addJob(new FunctionJob([state,source]{
        try{state->result=std::make_unique<FileAnalysis>(analyseRecording(source,&state->progress,&state->cancel));}
        catch(const std::exception& e){state->status=juce::String("SCAN: ")+e.what();}
        state->finished.store(true,std::memory_order_release);
    }),true);
}
void NoiseBaseProcessor::serviceJobs(){
    auto state=activeJob;if(!state)return;jobProgress.store(state->progress.load());
    if(!state->finished.load(std::memory_order_acquire))return;
    if(state->result){
        auto found=std::move(state->result);queueProfile(found->profile);
        auto probabilities=std::make_shared<const std::vector<float>>(found->voice);
        {const juce::SpinLock::ScopedLockType lock(regionLock);noiseRegions=found->regions;fileVoice=std::move(probabilities);selectedRegion=-1;}
        noiseRegionCount.store(found->regions?(int)found->regions->size():0);activeNoiseRegion.store(0);
        {const juce::ScopedLock lock(analysisLock);analysis=std::move(found);}
        analysed.store(true);setStatus("TRACK READY / "+juce::String(noiseRegionCount.load())+" LOCAL NOISE REGIONS");
    }else setStatus(state->status);
    activeJob.reset();busy.store(false);
}
void NoiseBaseProcessor::learnTrack(){
    params.getParameter("auto")->setValueNotifyingHost(1);
    if(externalReferenceSelected()){queueProfile(NoiseProfile{},true);referenceResetRequested.store(true);setStatus("SIDECHAIN / RESTARTING REFERENCE ANALYSIS");}
    else if(fileMode.load())scanRecording();
    else{resetProfile();setStatus("AUTO RESTARTED / PLAY THE TRACK TO LEARN ITS BACKGROUNDS");}
}
void NoiseBaseProcessor::saveRecording(const juce::File& output,double ambienceSeconds){
    bool bypassed=value("bypass")>.5f,generated=proceduralSelected()&&!bypassed;
    if((generated?!ambienceReady.load():!analysed.load())||busy.exchange(true))return;
    if(output==loadedFile){setStatus("PRINT: Choose a new file; source cannot be overwritten");busy.store(false);return;}
    auto state=std::make_shared<FileJobState>();activeJob=state;jobProgress.store(0);
    float cap=bypassed?0:value("reduction"),pres=value("preserve")*.01f,blend=value("mix")*.01f,level=bypassed?0:value("output");
    bool noiseOutput=value("noiseOnly")>.5f&&!bypassed,followTrack=value("auto")>.5f;
    if(!jobs)jobs=std::make_unique<juce::ThreadPool>(1);
    if(generated){
        bool external=externalReferenceSelected();NoiseProfile profile=external?snapshotReference():NoiseProfile{};
        {const juce::ScopedLock lock(profileStateLock);if(!validAmbienceProfile(profile)&&storedProfileExternal==external)profile=storedProfile;}
        setStatus("GENERATING / SEAMLESS PROCEDURAL NOISE");
        jobs->addJob(new FunctionJob([state,profile,output,ambienceSeconds,blend,level]{
            try{exportAmbience(profile,output,ambienceSeconds,blend,level,&state->progress,&state->cancel);state->status="PROCEDURAL LOOP PRINTED / "+output.getFileName();}
            catch(const std::exception& e){state->status=juce::String("PRINT: ")+e.what();}state->finished.store(true,std::memory_order_release);
        }),true);return;
    }
    auto snapshot=std::make_shared<FileAnalysis>();{const juce::ScopedLock lock(analysisLock);*snapshot=*analysis;}
    if(!followTrack){const juce::SpinLock::ScopedLockType lock(regionLock);if(noiseRegions&&selectedRegion>=0&&selectedRegion<(int)noiseRegions->size())snapshot->profile=(*noiseRegions)[(size_t)selectedRegion].profile;}
    setStatus(noiseOutput?"PRINTING NOISE BASE":bypassed?"PRINTING ORIGINAL / BYPASS":"PRINTING CLEAN / FOLLOWING TRACK FLOORS");
    jobs->addJob(new FunctionJob([state,snapshot,output,cap,pres,blend,level,noiseOutput,followTrack]{
        try{exportRecording(*snapshot,output,cap,pres,blend,level,&state->progress,&state->cancel,noiseOutput,followTrack);state->status=(noiseOutput?"NOISE BASE PRINTED / ":"PRINT COMPLETE / ")+output.getFileName();}
        catch(const std::exception& e){state->status=juce::String("PRINT: ")+e.what();}state->finished.store(true,std::memory_order_release);
    }),true);
}
void NoiseBaseProcessor::togglePlayback(){if(proceduralSelected()){if(ambienceReady.load())ambiencePlaying.store(!ambiencePlaying.load());return;}if(fileMode.load()){if(transport.isPlaying())transport.stop();else{if(transport.getCurrentPosition()>=transport.getLengthInSeconds()-.01)transport.setPosition(0);transport.start();}}}
void NoiseBaseProcessor::stopPlayback(){ambiencePlaying.store(false);ambienceSamples.store(0);transport.stop();transport.setPosition(0);}
void NoiseBaseProcessor::setLooping(bool enabled){auto position=transport.getCurrentPosition();loopPlayback.store(enabled);if(readerSource)readerSource->setLooping(enabled);if(fileMode.load())transport.setPosition(position);}
