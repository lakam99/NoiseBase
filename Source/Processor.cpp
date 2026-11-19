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
    return result;
}
NoiseBaseProcessor::NoiseBaseProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),params(*this,nullptr,"NoiseBaseState",layout()) {
    formats.registerBasicFormats();readAhead.startThread();
}
NoiseBaseProcessor::~NoiseBaseProcessor(){cancel.store(true);jobs.removeAllJobs(true,15000);transport.stop();transport.setSource(nullptr);readAhead.stopThread(5000);}
void NoiseBaseProcessor::prepareToPlay(double rate,int block) {
    engine.prepare(rate,getTotalNumOutputChannels());setLatencySamples(engine.latency());
    voice.prepare(rate);transport.prepareToPlay(block,rate);
    const juce::ScopedLock lock(analysisLock);
    if(analysis && fileMode.load()) queueProfile(analysis->profile);
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
        if(lock.isLocked()) {engine.setProfile(pendingProfile);pending.store(false,std::memory_order_release);}
    }
    voice.push(buffer);
    if(isNonRealtime())voice.waitForPending();
    engine.process(buffer,voice.probability.load(),value("reduction"),value("preserve")*.01f,
        value("mix")*.01f,value("output"),value("auto")>.5f,forceBypass||value("bypass")>.5f);
    inputLevel.store(engine.inputMeter());outputLevel.store(engine.outputMeter());reductionLevel.store(engine.reductionMeter());
    quietSeconds.store((float)engine.quietTime());detectorAvailable.store(voice.available.load());detectorFailed.store(voice.failed.load());
}
juce::AudioProcessorEditor* NoiseBaseProcessor::createEditor(){return new NoiseBaseEditor(*this);}
void NoiseBaseProcessor::getStateInformation(juce::MemoryBlock& dest) {auto xml=params.copyState().createXml();copyXmlToBinary(*xml,dest);}
void NoiseBaseProcessor::setStateInformation(const void* data,int size) {if(auto xml=getXmlFromBinary(data,size)) if(xml->hasTagName(params.state.getType())) params.replaceState(juce::ValueTree::fromXml(*xml));}
void NoiseBaseProcessor::setStatus(const juce::String& text){const juce::ScopedLock lock(statusLock);status=text;}
juce::String NoiseBaseProcessor::getStatus() const {const juce::ScopedLock lock(statusLock);return status;}
juce::String NoiseBaseProcessor::getFileName() const {const juce::ScopedLock lock(statusLock);return fileName;}
void NoiseBaseProcessor::queueProfile(const NoiseProfile& profile) {const juce::SpinLock::ScopedLockType lock(pendingLock);pendingProfile=profile;pending.store(true,std::memory_order_release);}
void NoiseBaseProcessor::loadRecording(const juce::File& file) {
    if(busy.load())return;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader || reader->numChannels>2 || reader->sampleRate<8000 || reader->lengthInSamples<=0) {setStatus("Use a nonempty mono/stereo WAV, AIFF, FLAC or MP3");return;}
    transport.stop();transport.setSource(nullptr);readerSource.reset();
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
            juce::String text=found->profile.estimated ? "ESTIMATED FLOOR  /  reduction limited to 6 dB" : "TAPE READY  /  noise profile learned";
            {const juce::ScopedLock lock(analysisLock);analysis=std::move(found);}
            analysed.store(true);setStatus(text);
        }catch(const std::exception& e){setStatus(juce::String("SCAN: ")+e.what());}
        busy.store(false);
    }),true);
}
void NoiseBaseProcessor::saveRecording(const juce::File& output) {
    if(!analysed.load() || busy.exchange(true))return;
    cancel.store(false);jobProgress.store(0);setStatus("PRINTING  /  original remains untouched");
    auto fileAnalysis=std::make_shared<FileAnalysis>();
    {const juce::ScopedLock lock(analysisLock);*fileAnalysis=*analysis;}
    float cap=value("bypass")>.5f?0:value("reduction"),pres=value("preserve")*.01f,blend=value("mix")*.01f,level=value("output");
    jobs.addJob(new FunctionJob([this,fileAnalysis,output,cap,pres,blend,level]{
        try{exportRecording(*fileAnalysis,output,cap,pres,blend,level,&jobProgress,&cancel);setStatus("PRINT COMPLETE  /  "+output.getFileName());}
        catch(const std::exception& e){setStatus(juce::String("PRINT: ")+e.what());}
        busy.store(false);
    }),true);
}
void NoiseBaseProcessor::togglePlayback(){if(fileMode.load()){if(transport.isPlaying())transport.stop();else{if(transport.getCurrentPosition()>=transport.getLengthInSeconds()-.01)transport.setPosition(0);transport.start();}}}
void NoiseBaseProcessor::stopPlayback(){transport.stop();transport.setPosition(0);}
void NoiseBaseProcessor::setLooping(bool enabled){auto position=transport.getCurrentPosition();loopPlayback.store(enabled);if(readerSource)readerSource->setLooping(enabled);if(fileMode.load())transport.setPosition(position);}
