#include "Processor.h"
#include "Editor.h"

namespace {
class FunctionJob:public juce::ThreadPoolJob {
public:
    explicit FunctionJob(std::function<void()> f):ThreadPoolJob("NoiseBase file job"),function(std::move(f)){}
    JobStatus runJob() override {function();return jobHasFinished;}
private:std::function<void()> function;
};
}

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
    return result;
}
NoiseBaseProcessor::NoiseBaseProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),params(*this,nullptr,"NoiseBaseState",layout()) {
    formats.registerBasicFormats();readAhead.startThread();
}
NoiseBaseProcessor::~NoiseBaseProcessor(){cancel.store(true);jobs.removeAllJobs(true,15000);transport.stop();transport.setSource(nullptr);readAhead.stopThread(5000);}
void NoiseBaseProcessor::prepareToPlay(double rate,int block) {
    engine.prepare(rate,getTotalNumOutputChannels());setLatencySamples(engine.latency());
    generator.prepare(rate,getTotalNumOutputChannels());playbackRate=rate;
    proceduralFade.reset(rate,.035);proceduralFade.setCurrentAndTargetValue(0);ambienceGain.reset(rate,.035);ambienceGain.setCurrentAndTargetValue(0);wasProcedural=false;
    voice.prepare(rate);transport.prepareToPlay(block,rate);
    NoiseProfile saved;{const juce::ScopedLock lock(profileStateLock);saved=storedProfile;}
    if(validAmbienceProfile(saved))queueProfile(saved);
}
void NoiseBaseProcessor::releaseResources(){transport.releaseResources();}
bool NoiseBaseProcessor::isBusesLayoutSupported(const BusesLayout& buses) const {
    auto in=buses.getMainInputChannelSet(),out=buses.getMainOutputChannelSet();
    return in==out && (out==juce::AudioChannelSet::mono() || out==juce::AudioChannelSet::stereo());
}
void NoiseBaseProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    processAudio(buffer,false);
}
void NoiseBaseProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    processAudio(buffer,true);
}
void NoiseBaseProcessor::processAudio(juce::AudioBuffer<float>& buffer,bool forceBypass) {
    juce::ScopedNoDenormals noDenormals;
    if(standaloneMode.load() && fileMode.load()) {juce::AudioSourceChannelInfo info(&buffer,0,buffer.getNumSamples());transport.getNextAudioBlock(info);}
    if(resetRequested.exchange(false)) engine.resetNoise();
    if(pending.load(std::memory_order_acquire)) {
        const juce::SpinLock::ScopedTryLockType lock(pendingLock);
        if(lock.isLocked()) {if(!engine.setProfile(pendingProfile))engine.resetNoise();if(!generator.setProfile(pendingProfile))generator.clearProfile();pending.store(false,std::memory_order_release);}
    }
    voice.push(buffer);
    if(isNonRealtime())voice.waitForPending();
    engine.process(buffer,voice.probability.load(),value("reduction"),value("preserve")*.01f,
        value("mix")*.01f,value("output"),value("auto")>.5f,forceBypass||value("bypass")>.5f,value("noiseOnly")>.5f);
    bool procedural=proceduralSelected(),bypassed=forceBypass||value("bypass")>.5f;
    if(procedural&&!wasProcedural&&transport.isPlaying())ambiencePlaying.store(true);wasProcedural=procedural;
    bool running=procedural&&!bypassed&&(!standaloneMode.load()||ambiencePlaying.load());
    proceduralFade.setTargetValue(procedural&&!bypassed?1.0f:0.0f);
    ambienceGain.setTargetValue(running?value("mix")*.01f*juce::Decibels::decibelsToGain(value("output")):0);
    float generatedPeak=0;
    for(int i=0;i<buffer.getNumSamples();++i){float sample[2]{};float fade=proceduralFade.getNextValue(),level=ambienceGain.getNextValue();if(running||level>1e-8f)generator.next(sample);
        for(int c=0;c<std::min(buffer.getNumChannels(),getTotalNumOutputChannels());++c){float result=buffer.getSample(c,i)*(1-fade)+sample[c]*level*fade;buffer.setSample(c,i,result);generatedPeak=std::max(generatedPeak,std::abs(result));}}
    if(running&&standaloneMode.load())ambienceSamples.fetch_add(buffer.getNumSamples());
    inputLevel.store(engine.inputMeter());outputLevel.store(engine.outputMeter());reductionLevel.store(engine.reductionMeter());
    quietSeconds.store((float)engine.quietTime());detectorAvailable.store(voice.available.load());detectorFailed.store(voice.failed.load());
    if(procedural){outputLevel.store(generatedPeak);reductionLevel.store(0);}
}
juce::AudioProcessorEditor* NoiseBaseProcessor::createEditor(){return new NoiseBaseEditor(*this);}
void NoiseBaseProcessor::getStateInformation(juce::MemoryBlock& dest) {auto state=params.copyState();state.removeChild(state.getChildWithName("AmbienceProfile"),nullptr);
    {const juce::ScopedLock lock(profileStateLock);auto profile=ambienceProfileState(storedProfile);if(profile.isValid()){profile.setProperty("label",getFileName(),nullptr);state.appendChild(profile,nullptr);}}
    auto xml=state.createXml();copyXmlToBinary(*xml,dest);}
void NoiseBaseProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size))if(xml->hasTagName(params.state.getType())){
        auto state=juce::ValueTree::fromXml(*xml);
        if(!state.getChildWithProperty("id","noiseOnly").isValid()){
            juce::ValueTree mode("PARAM");mode.setProperty("id","noiseOnly",nullptr);mode.setProperty("value",0.0f,nullptr);state.appendChild(mode,nullptr);
        }
        // Existing 1.4 sessions keep their removed-noise route until selected.
        if(!state.getChildWithProperty("id","procedural").isValid()){juce::ValueTree mode("PARAM");mode.setProperty("id","procedural",nullptr);mode.setProperty("value",0.0f,nullptr);state.appendChild(mode,nullptr);}
        NoiseProfile restored;auto saved=state.getChildWithName("AmbienceProfile");
        if(readAmbienceProfile(saved,restored)){queueProfile(restored);const juce::ScopedLock lock(statusLock);fileName=saved["label"].toString();status="PROFILE RESTORED  /  source clip not required";}
        else {queueProfile(NoiseProfile{});}
        params.replaceState(state);
    }
}
void NoiseBaseProcessor::setStatus(const juce::String& text){const juce::ScopedLock lock(statusLock);status=text;}
juce::String NoiseBaseProcessor::getStatus() const {const juce::ScopedLock lock(statusLock);return status;}
juce::String NoiseBaseProcessor::getFileName() const {const juce::ScopedLock lock(statusLock);return fileName;}
void NoiseBaseProcessor::queueProfile(const NoiseProfile& profile) {
    {const juce::ScopedLock lock(profileStateLock);storedProfile=validAmbienceProfile(profile)?profile:NoiseProfile{};}
    ambienceReady.store(validAmbienceProfile(profile));
    const juce::SpinLock::ScopedLockType lock(pendingLock);pendingProfile=profile;pending.store(true,std::memory_order_release);
}
void NoiseBaseProcessor::loadRecording(const juce::File& file) {
    if(busy.load())return;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->numChannels>2 || reader->sampleRate<8000 || reader->lengthInSamples<=0) {setStatus("Use a nonempty mono/stereo WAV, AIFF, FLAC or MP3");return;}
    stopPlayback();transport.setSource(nullptr);readerSource.reset();queueProfile(NoiseProfile{});
    double rate=reader->sampleRate;
    readerSource=std::make_unique<TapeReaderSource>(std::move(reader));readerSource->setLooping(loopPlayback.load());
    transport.setSource(readerSource.get(),32768,&readAhead,rate);
    loadedFile=file;fileMode.store(true);analysed.store(false);
    {const juce::ScopedLock lock(statusLock);fileName=file.getFileName();}
    scanRecording();
}
void NoiseBaseProcessor::scanRecording() {
    if(!fileMode.load() || busy.exchange(true))return;
    cancel.store(false);jobProgress.store(0);setStatus("SCANNING  /  locating the noise floor");
    juce::File source=loadedFile;
    jobs.addJob(new FunctionJob([this,source]{
        try {
            auto found=std::make_unique<FileAnalysis>(analyseRecording(source,&jobProgress,&cancel));
            queueProfile(found->profile);
            juce::String text=found->profile.estimated ? "ESTIMATED FLOOR  /  need a quieter clip for procedural noise" : "PROFILE READY  /  procedural ambience available";
            {const juce::ScopedLock lock(analysisLock);analysis=std::move(found);}
            analysed.store(true);setStatus(text);
        }catch(const std::exception& e){setStatus(juce::String("SCAN: ")+e.what());}
        busy.store(false);
    }),true);
}
void NoiseBaseProcessor::saveRecording(const juce::File& output,double ambienceSeconds) {
    if(proceduralSelected()&&value("bypass")<.5f){
        if(!ambienceReady.load()||busy.exchange(true))return;
        if(output==loadedFile){setStatus("PRINT: Choose a new file; source cannot be overwritten");busy.store(false);return;}
        NoiseProfile profile;{const juce::ScopedLock lock(profileStateLock);profile=storedProfile;}
        float blend=value("mix")*.01f,level=value("output");cancel.store(false);jobProgress.store(0);setStatus("GENERATING  /  seamless procedural noise");
        jobs.addJob(new FunctionJob([this,profile,output,ambienceSeconds,blend,level]{try{exportAmbience(profile,output,ambienceSeconds,blend,level,&jobProgress,&cancel);setStatus("PROCEDURAL LOOP PRINTED  /  "+output.getFileName());}catch(const std::exception& e){setStatus(juce::String("PRINT: ")+e.what());}busy.store(false);}),true);return;
    }
    if(!analysed.load() || busy.exchange(true))return;
    bool bypassed=value("bypass")>.5f,noiseOutput=value("noiseOnly")>.5f&&!bypassed;
    cancel.store(false);jobProgress.store(0);setStatus(noiseOutput?"PRINTING NOISE BASE  /  original remains untouched":bypassed?"PRINTING ORIGINAL  /  BYPASS":"PRINTING CLEAN  /  original remains untouched");
    auto fileAnalysis=std::make_shared<FileAnalysis>();
    {const juce::ScopedLock lock(analysisLock);*fileAnalysis=*analysis;}
    float cap=bypassed?0:value("reduction"),pres=value("preserve")*.01f,blend=value("mix")*.01f,level=bypassed?0:value("output");
    jobs.addJob(new FunctionJob([this,fileAnalysis,output,cap,pres,blend,level,noiseOutput]{
        try{exportRecording(*fileAnalysis,output,cap,pres,blend,level,&jobProgress,&cancel,noiseOutput);setStatus((noiseOutput?"NOISE BASE PRINTED  /  ":"PRINT COMPLETE  /  ")+output.getFileName());}
        catch(const std::exception& e){setStatus(juce::String("PRINT: ")+e.what());}
        busy.store(false);
    }),true);
}
void NoiseBaseProcessor::togglePlayback(){if(proceduralSelected()){if(ambienceReady.load())ambiencePlaying.store(!ambiencePlaying.load());return;}if(fileMode.load()){if(transport.isPlaying())transport.stop();else{if(transport.getCurrentPosition()>=transport.getLengthInSeconds()-.01)transport.setPosition(0);transport.start();}}}
void NoiseBaseProcessor::stopPlayback(){ambiencePlaying.store(false);ambienceSamples.store(0);transport.stop();transport.setPosition(0);}
void NoiseBaseProcessor::setLooping(bool enabled){auto position=transport.getCurrentPosition();loopPlayback.store(enabled);if(readerSource)readerSource->setLooping(enabled);if(fileMode.load())transport.setPosition(position);}
