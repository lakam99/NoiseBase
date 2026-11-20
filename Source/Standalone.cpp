#include "Processor.h"
#include "Editor.h"
#include "Updater.h"
#include "OsTheme.h"
#include <fstream>
#include <cstdio>

namespace {
void writeSnapshot(const juce::File& file,const juce::Image& image) {
    juce::MemoryOutputStream bytes;juce::PNGImageFormat png;
    if(!png.writeImageToStream(image,bytes)||!file.replaceWithData(bytes.getData(),bytes.getDataSize()))throw std::runtime_error("Could not replace the preview PNG.");
}
}

class MainPanel final : public juce::AudioAppComponent, private juce::Timer {
public:
    MainPanel() {
        processor.standaloneMode.store(true);
        juce::PropertiesFile::Options options;options.applicationName="NoiseBase";options.filenameSuffix="settings";options.folderName="NoiseBase";options.osxLibrarySubFolder="Application Support";
        preferences=std::make_unique<juce::PropertiesFile>(options);
        juce::MemoryBlock state;if(state.fromBase64Encoding(preferences->getValue("controls")))processor.setStateInformation(state.getData(),(int)state.getSize());
        processor.setLooping(preferences->getBoolValue("tapeLoop",false));
        editor=std::make_unique<NoiseBaseEditor>(processor);
        editor->onUpdates=[this]{showUpdatesMenu();};
        editor->onDevices=[this]{
            auto selector=std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager,0,2,0,2,false,false,true,false);
            selector->setSize(500,420);
            juce::DialogWindow::LaunchOptions options;options.content.setOwned(selector.release());
            options.dialogTitle="NoiseBase patch bay";options.dialogBackgroundColour=OsTheme::background(juce::Desktop::getInstance().isDarkModeActive());
            options.escapeKeyTriggersCloseButton=true;options.useNativeTitleBar=true;options.resizable=false;options.launchAsync();
        };
        addAndMakeVisible(*editor);setSize(editor->getWidth(),editor->getHeight());
        setAudioChannels(0,2);
        startTimer(5000);
    }
    ~MainPanel() override {stopTimer();shutdownAudio();juce::MemoryBlock state;processor.getStateInformation(state);preferences->setValue("controls",state.toBase64Encoding());preferences->setValue("tapeLoop",processor.isLooping());preferences->saveIfNeeded();editor.reset();}
    void prepareToPlay(int block,double rate) override {processor.prepareToPlay(rate,block);}
    void releaseResources() override {processor.releaseResources();}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        juce::AudioBuffer<float> view(info.buffer->getArrayOfWritePointers(),std::min(2,info.buffer->getNumChannels()),info.startSample,info.numSamples);
        if(!processor.fileMode.load())view.clear();
        juce::MidiBuffer midi;processor.processBlock(view,midi);
    }
    void resized() override {if(editor)editor->setBounds(getLocalBounds());}
private:
    void timerCallback() override {
        startTimer(3600000);
#if JUCE_WINDOWS
        auto installReport=juce::File(juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{})).getChildFile("NoiseBase/last-update-result.json");
        if(installReport.existsAsFile()) {
            auto result=juce::JSON::parse(installReport);installReport.deleteFile();
            if(result.isObject()&&!(bool)result["passed"])juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"NoiseBase update was not installed",result["error"].toString());
        }
#endif
        auto now=juce::Time::getCurrentTime().toMilliseconds();
        auto previous=preferences->getValue("lastUpdateCheck","0").getLargeIntValue();
        if(preferences->getBoolValue("automaticUpdates",true)&&now-previous>=86400000)checkUpdates(false,false);
    }
    void showUpdatesMenu() {
        juce::PopupMenu menu;menu.addItem(1,"Check for updates...");menu.addItem(2,"Automatically check for updates",true,preferences->getBoolValue("automaticUpdates",true));menu.addItem(3,"Open GitHub releases");
        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(editor.get()),[safe=juce::Component::SafePointer<MainPanel>(this)](int item){
            if(!safe)return;
            if(item==1)safe->checkUpdates(true,false);
            if(item==2){safe->preferences->setValue("automaticUpdates",!safe->preferences->getBoolValue("automaticUpdates",true));safe->preferences->saveIfNeeded();}
            if(item==3)juce::URL("https://github.com/lakam99/NoiseBase/releases").launchInDefaultBrowser();
        });
    }
    void checkUpdates(bool manual,bool prepare) {
        if(updateBusy)return;
        updateBusy=true;editor->setUpdateBusy(true,prepare);
        updater.request(prepare,[safe=juce::Component::SafePointer<MainPanel>(this),manual,prepare](juce::var result){
            if(!safe)return;
            safe->updateBusy=false;safe->editor->setUpdateBusy(false);
            if(!(bool)result["passed"]){
                if(manual)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"NoiseBase updates",result["error"].toString());
                return;
            }
            safe->preferences->setValue("lastUpdateCheck",juce::String(juce::Time::getCurrentTime().toMilliseconds()));safe->preferences->saveIfNeeded();
            if(!(bool)result["available"]){if(manual)juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"NoiseBase updates","You are running NoiseBase " NOISEBASE_VERSION ". No newer stable release is available.");return;}
#if JUCE_MAC
            auto options=juce::MessageBoxOptions{}.withIconType(juce::MessageBoxIconType::InfoIcon).withTitle("NoiseBase "+result["version"].toString()).withMessage("A newer Apple Silicon release is available. Open the release page to download and install it?").withButton("Open release",1).withButton("Later",0);
            juce::AlertWindow::showAsync(options,[url=result["releaseUrl"].toString()](int choice){if(choice==1)juce::URL(url).launchInDefaultBrowser();});
#else
            auto options=juce::MessageBoxOptions{}.withIconType(juce::MessageBoxIconType::InfoIcon).withTitle("NoiseBase "+result["version"].toString());
            options=options.withMessage(prepare?"The update is downloaded and verified. Close other NoiseBase windows and any DAW using its VST3, then install and restart.":"A newer stable release is available on GitHub. Download and verify it now?").withButton(prepare?"Install and restart":"Download update",1).withButton("Later",0);
            juce::AlertWindow::showAsync(options,[safe,prepare,result](int choice){
                if(!safe||choice!=1)return;
                if(!prepare){safe->checkUpdates(true,true);return;}
                if(safe->processor.busy.load()||safe->processor.isPlaying()){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"NoiseBase updates","Stop playback and wait for scanning or export to finish before installing.");return;}
                juce::String error;
                if(NoiseBaseUpdater::installAndRestart(result,error))juce::JUCEApplication::getInstance()->systemRequestedQuit();
                else juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"NoiseBase updates",error);
            });
#endif
        });
    }
    NoiseBaseUpdater updater;
    bool updateBusy=false;
    NoiseBaseProcessor processor;
    std::unique_ptr<NoiseBaseEditor> editor;
    std::unique_ptr<juce::PropertiesFile> preferences;
    juce::TooltipWindow tips{this,650};
};

class AppWindow final : public juce::DocumentWindow {
public:
    AppWindow():DocumentWindow("NoiseBase",juce::Colour(0xff252923),allButtons) {
        setUsingNativeTitleBar(true);setContentOwned(new MainPanel,true);setResizable(false,false);
        setIcon(RasterAssets::get().icon);centreWithSize(getWidth(),getHeight());OsTheme::setNativeFrame(*this,juce::Desktop::getInstance().isDarkModeActive());setVisible(true);
    }
    void closeButtonPressed() override {juce::JUCEApplication::getInstance()->systemRequestedQuit();}
};

juce::var validatePlugin(const juce::File& file,const juce::File& snapshot,const juce::File& input={},const juce::File& audioOutput={}) {
    auto stage=[](const char* text){std::fprintf(stderr,"VST3 verification: %s\n",text);std::fflush(stderr);};stage("discover");
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;format.findAllTypesForFile(descriptions,file.getFullPathName());
    if(descriptions.isEmpty()) throw std::runtime_error("VST3 discovery returned no plug-in.");
    juce::String error;
    auto plugin=format.createInstanceFromDescription(*descriptions[0],48000,512,error);
    if(!plugin)throw std::runtime_error(error.toStdString());
    stage("instance created");
    auto* report=new juce::DynamicObject;
    report->setProperty("name",plugin->getName());report->setProperty("discovered",true);
    juce::MemoryBlock saved;plugin->getStateInformation(saved);plugin->setStateInformation(saved.getData(),(int)saved.getSize());
    report->setProperty("state_roundtrip",saved.getSize()>0);
    juce::AudioProcessorParameter* noiseParameter=nullptr;
    for(auto* parameter:plugin->getParameters())if(parameter->getName(128)=="Noise only")noiseParameter=parameter;
    if(!noiseParameter)throw std::runtime_error("VST3 noise-only automation parameter is missing.");
    noiseParameter->setValueNotifyingHost(1);juce::MemoryBlock noiseState;plugin->getStateInformation(noiseState);
    noiseParameter->setValueNotifyingHost(0);plugin->setStateInformation(noiseState.getData(),(int)noiseState.getSize());
    if(noiseParameter->getValue()<.5f)throw std::runtime_error("VST3 noise-only mode was not restored.");
    report->setProperty("noise_only_automatable_and_saved",true);noiseParameter->setValueNotifyingHost(0);
    juce::AudioProcessorParameter* proceduralParameter=nullptr;
    for(auto* parameter:plugin->getParameters())if(parameter->getName(128)=="Procedural noise")proceduralParameter=parameter;
    if(!proceduralParameter)throw std::runtime_error("VST3 procedural automation parameter is missing.");
    // Check residual cancellation explicitly; procedural output without a model
    // is silent regardless of the reduction setting and would hide a failure.
    proceduralParameter->setValueNotifyingHost(0);
    juce::AudioProcessorParameter* reductionParameter=nullptr;
    for(auto* parameter:plugin->getParameters())if(parameter->getName(128)=="Reduction")reductionParameter=parameter;
    if(!reductionParameter)throw std::runtime_error("VST3 reduction parameter is missing.");
    reductionParameter->setValueNotifyingHost(0);
    for(auto* param:plugin->getParameters()) if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) if(identified->paramID=="reduction")param->setValueNotifyingHost(0);
    bool finite=true;float liveNoiseSilence=0;juce::Array<juce::var> runs;
    for(double rate:{44100.0,48000.0,96000.0}) {
        plugin->setRateAndBufferSizeDetails(rate,512);plugin->prepareToPlay(rate,512);
        for(int block:{64,137,512}) {
            juce::AudioBuffer<float> audio(2,block);juce::MidiBuffer midi;
            for(int repeat=0;repeat<24;++repeat) {
                for(int i=0;i<block;++i){float value=.03f*std::sin((float)(repeat*block+i)*.11f);audio.setSample(0,i,value);audio.setSample(1,i,-value);}
                plugin->processBlock(audio,midi);
                for(int c=0;c<2;++c)for(int i=0;i<block;++i)finite &= std::isfinite(audio.getSample(c,i));
            }
        }
        noiseParameter->setValueNotifyingHost(1);
        // Actual live-host audio must cancel at zero reduction in noise mode.
        for(int position=0;position<(int)(rate*.25)+plugin->getLatencySamples();position+=137){juce::AudioBuffer<float> audio(2,137);juce::MidiBuffer midi;for(int i=0;i<137;++i){float sample=.03f*std::sin((float)(position+i)*.11f);audio.setSample(0,i,sample);audio.setSample(1,i,-sample);}plugin->processBlock(audio,midi);for(int channel=0;channel<2;++channel)for(int i=0;i<137;++i){finite &= std::isfinite(audio.getSample(channel,i));if(position>(int)(rate*.1)+plugin->getLatencySamples())liveNoiseSilence=std::max(liveNoiseSilence,std::abs(audio.getSample(channel,i)));}}
        noiseParameter->setValueNotifyingHost(0);
        auto* run=new juce::DynamicObject;run->setProperty("sample_rate",rate);run->setProperty("latency_samples",plugin->getLatencySamples());runs.add(juce::var(run));
        plugin->releaseResources();
    }
    stage("live input rates passed");
    // Load a self-contained learned model through the real VST3 component state.
    // This exercises the JUCE host wrapper as well as the processor callback.
    NoiseBaseProcessor stateBuilder;auto modelState=stateBuilder.params.copyState();
    modelState.getChildWithProperty("id","noiseOnly").setProperty("value",1.0f,nullptr);
    modelState.getChildWithProperty("id","procedural").setProperty("value",1.0f,nullptr);
    NoiseProfile model;model.sampleRate=48000;model.fftSize=2048;model.quietSeconds=1;model.sourceChannels=2;
    model.power.assign(1025,.375f*2048*.01f*.01f);model.ambiencePower[0]=model.power;model.ambiencePower[1]=model.power;model.ambienceCorrelation.assign(1025,.3f);
    auto modelTree=ambienceProfileState(model);modelTree.setProperty("label","HOST TEST PROFILE",nullptr);modelState.appendChild(modelTree,nullptr);
    juce::MemoryBlock componentState;juce::AudioProcessor::copyXmlToBinary(*modelState.createXml(),componentState);
    juce::XmlElement wrapper("VST3PluginState");wrapper.createNewChildElement("IComponent")->addTextElement(componentState.toBase64Encoding());
    juce::MemoryBlock wrapperState;juce::AudioProcessor::copyXmlToBinary(wrapper,wrapperState);
    plugin->setStateInformation(wrapperState.getData(),(int)wrapperState.getSize());
    juce::MemoryBlock hostSaved;plugin->getStateInformation(hostSaved);plugin->setStateInformation(saved.getData(),(int)saved.getSize());plugin->setStateInformation(hostSaved.getData(),(int)hostSaved.getSize());
    bool generatedInHost=true;juce::Array<juce::var> generatedRuns;
    for(double rate:{44100.0,48000.0,96000.0}){
        plugin->setRateAndBufferSizeDetails(rate,137);plugin->prepareToPlay(rate,137);double energy=0;int measured=0;
        for(int start=0;start<(int)(rate*2);start+=137){juce::AudioBuffer<float> audio(2,137);audio.clear();juce::MidiBuffer midi;plugin->processBlock(audio,midi);if(start>(int)rate)for(int i=0;i<137;++i){float sample=audio.getSample(0,i);finite&=std::isfinite(sample);energy+=sample*sample;++measured;}}
        double rms=std::sqrt(energy/std::max(1,measured));bool ok=std::isfinite(rms)&&std::abs(rms/.01-1)<.12;generatedInHost&=ok;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("silent_input_generated_rms",rms);row->setProperty("passed",ok);generatedRuns.add(juce::var(row));plugin->releaseResources();
    }
    if(!generatedInHost)throw std::runtime_error("Saved procedural profile did not generate on silent VST3 host input.");
    stage("saved procedural profile passed");
    report->setProperty("procedural_profile_saved_and_restored_in_vst3",true);report->setProperty("procedural_silent_host_runs",generatedRuns);
    plugin->setStateInformation(saved.getData(),(int)saved.getSize());
    if(input.existsAsFile()) {
        juce::AudioFormatManager formats;formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(input));
        if(!reader || reader->numChannels>2 || reader->lengthInSamples>reader->sampleRate*120)throw std::runtime_error("Use a short mono/stereo validation recording.");
        for(auto* param:plugin->getParameters())if(auto* named=dynamic_cast<juce::AudioProcessorParameterWithID*>(param)){
            if(named->paramID=="reduction")param->setValueNotifyingHost(14.0f/24);
            if(named->paramID=="auto")param->setValueNotifyingHost(1);
            if(named->paramID=="bypass")param->setValueNotifyingHost(0);
        }
        plugin->setNonRealtime(true);plugin->setRateAndBufferSizeDetails(reader->sampleRate,512);plugin->prepareToPlay(reader->sampleRate,512);
        int latency=plugin->getLatencySamples();int length=(int)reader->lengthInSamples;
        juce::AudioBuffer<float> rendered((int)reader->numChannels,length);rendered.clear();
        for(int pos=0;pos<length+latency;pos+=512) {
            int count=std::min(512,length+latency-pos);juce::AudioBuffer<float> audio(2,count);audio.clear();
            if(pos<length)reader->read(&audio,0,std::min(count,length-pos),pos,true,true);
            juce::MidiBuffer midi;plugin->processBlock(audio,midi);
            int skip=std::max(0,latency-pos);int destination=std::max(0,pos-latency);
            int keep=std::min(count-skip,length-destination);
            if(keep>0)for(int c=0;c<rendered.getNumChannels();++c)rendered.copyFrom(c,destination,audio,c,skip,keep);
        }
        if(audioOutput!=juce::File{}) {
            juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(audioOutput.createOutputStream());
            auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(reader->sampleRate).withNumChannels(rendered.getNumChannels()).withBitsPerSample(24));
            if(!writer || !writer->writeFromAudioSampleBuffer(rendered,0,length))throw std::runtime_error("Could not write the host validation audio.");
        }
        report->setProperty("auto_learning_audio_rendered",true);report->setProperty("audio_samples",length);
        report->setProperty("audio_sample_rate",reader->sampleRate);report->setProperty("offline_host_processing",true);
        plugin->releaseResources();plugin->setNonRealtime(false);
    }
    if(snapshot!=juce::File{}) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
        if(!editor)throw std::runtime_error("VST3 editor creation failed.");
        auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
        writeSnapshot(snapshot,image);
        report->setProperty("editor_created",true);
    }
    report->setProperty("finite_output",finite);report->setProperty("live_noise_only_zero_reduction_error",liveNoiseSilence);report->setProperty("runs",runs);report->setProperty("passed",finite&&saved.getSize()>0&&liveNoiseSilence<.00002f&&generatedInHost);
    stage("editor and output passed; releasing host instance");
    return juce::var(report);
}

juce::var testFileDeck(const juce::File& source,const juce::File& output,const juce::File& snapshot,int skin) {
    NoiseBaseProcessor processor;processor.standaloneMode.store(true);
    auto legacy=processor.params.copyState();legacy.removeChild(legacy.getChildWithProperty("id","noiseOnly"),nullptr);
    legacy.removeChild(legacy.getChildWithProperty("id","procedural"),nullptr);
    juce::MemoryBlock legacyState;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),legacyState);
    processor.params.getParameter("noiseOnly")->setValueNotifyingHost(1);processor.setStateInformation(legacyState.getData(),(int)legacyState.getSize());
    bool legacyClean=processor.params.getRawParameterValue("noiseOnly")->load()<.5f&&processor.params.getRawParameterValue("procedural")->load()<.5f;
    if(!legacyClean)throw std::runtime_error("An older preset did not restore clean output.");
    processor.params.getParameter("procedural")->setValueNotifyingHost(0);
    processor.params.getParameter("skin")->setValueNotifyingHost(skin*.5f);
    processor.prepareToPlay(48000,512);
    NoiseBaseEditor editor(processor);
    processor.loadRecording(source);
    auto waitForJob=[&]{
        auto deadline=juce::Time::getMillisecondCounter()+30000;
        while(processor.busy.load()) {
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            if(juce::Time::getMillisecondCounter()>deadline)throw std::runtime_error("File deck timed out.");
        }
    };
    waitForJob();
    if(!processor.analysed.load())throw std::runtime_error(processor.getStatus().toStdString());
    // Verify the loop junction against independent reads of the original file.
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reference(formats.createReaderFor(source));
    TapeReaderSource repeat(std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(source)));
    int seam=(int)std::min<juce::int64>(8,repeat.getTotalLength());
    juce::AudioBuffer<float> actual(2,seam*2),expected(2,seam*2);expected.clear();
    reference->read(&expected,0,seam,repeat.getTotalLength()-seam,true,true);reference->read(&expected,seam,seam,0,true,true);
    repeat.setLooping(true);repeat.setNextReadPosition(repeat.getTotalLength()-seam);
    repeat.getNextAudioBlock({&actual,0,seam*2});
    bool exactLoop=true;for(int c=0;c<2;++c)for(int i=0;i<seam*2;++i)exactLoop &= std::abs(actual.getSample(c,i)-expected.getSample(c,i))<1e-7f;
    if(!exactLoop)throw std::runtime_error("Loop junction did not match the source samples.");
    processor.togglePlayback();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    double energy=0;
    for(int repeat=0;repeat<160;++repeat) {
        juce::AudioBuffer<float> audio(2,512);audio.clear();juce::MidiBuffer midi;
        processor.processBlock(audio,midi);
        for(int i=0;i<512;++i)energy+=audio.getSample(0,i)*audio.getSample(0,i);
        if(repeat%12==0)juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
    }
    processor.togglePlayback();
    bool paused=!processor.isPlaying();processor.returnToStart();
    bool rewindPaused=!processor.isPlaying()&&processor.playbackPosition()<.001;
    processor.togglePlayback();processor.returnToStart();
    bool rewindPlaying=processor.isPlaying()&&processor.playbackPosition()<.001;
    int observedWraps=0;
    auto runTape=[&](double seconds) {
        int blocks=(int)std::ceil(seconds*48000/512)+4096;
        double tailEnergy=0;
        double elapsed=0,previous=processor.playbackPosition();
        for(int block=0;block<blocks&&elapsed<seconds&&processor.isPlaying();++block){
            juce::AudioBuffer<float> audio(2,512);audio.clear();juce::MidiBuffer midi;processor.processBlock(audio,midi);
            double position=processor.playbackPosition();
            if(position<previous-.1){++observedWraps;elapsed+=processor.playbackDuration()-previous+position;}else elapsed+=std::max(0.0,position-previous);
            previous=position;
            if(elapsed>seconds-.3)for(int i=0;i<512;++i)tailEnergy+=audio.getSample(0,i)*audio.getSample(0,i);
            if(block%12==0)juce::MessageManager::getInstance()->runDispatchLoopUntil(2);
        }
        return tailEnergy;
    };
    processor.setLooping(true);
    auto duration=processor.playbackDuration();
    auto loopEnergy=runTape(duration+.5);
    bool repeats=observedWraps>0&&processor.isPlaying()&&processor.playbackPosition()<duration&&loopEnergy>1e-12;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(70);
    {auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);writeSnapshot(snapshot.getSiblingFile(snapshot.getFileNameWithoutExtension()+"-loop.png"),image);}
    processor.setLooping(false);runTape(duration+.5);
    bool eofStops=!processor.isPlaying();processor.stopPlayback();
    if(!rewindPaused||!rewindPlaying||!repeats||!eofStops)throw std::runtime_error("Transport rewind/loop/end-of-file verification failed.");
    if(energy<1e-9)throw std::runtime_error("File preview returned silence.");
    if(output.existsAsFile())throw std::runtime_error("Smoke test output already exists.");
    processor.saveRecording(output);waitForJob();
    if(!output.existsAsFile())throw std::runtime_error(processor.getStatus().toStdString());
    auto noiseFile=output.getSiblingFile(output.getFileNameWithoutExtension()+"-noise-base.wav");
    if(noiseFile.existsAsFile())throw std::runtime_error("Noise export test output already exists.");
    processor.params.getParameter("noiseOnly")->setValueNotifyingHost(1);processor.saveRecording(noiseFile);waitForJob();
    std::unique_ptr<juce::AudioFormatReader> cleanReader(formats.createReaderFor(output)),noiseReader(formats.createReaderFor(noiseFile));
    if(!cleanReader||!noiseReader||noiseReader->lengthInSamples!=reference->lengthInSamples||noiseReader->sampleRate!=reference->sampleRate||noiseReader->numChannels!=reference->numChannels)throw std::runtime_error("Noise export changed file timing or layout.");
    int channels=(int)reference->numChannels;juce::AudioBuffer<float> original(channels,8192),clean(channels,8192),noise(channels,8192);float reconstruction=0;
    for(juce::int64 start=0;start<reference->lengthInSamples;start+=8192){int count=(int)std::min<juce::int64>(8192,reference->lengthInSamples-start);reference->read(&original,0,count,start,true,true);cleanReader->read(&clean,0,count,start,true,true);noiseReader->read(&noise,0,count,start,true,true);for(int channel=0;channel<channels;++channel)for(int i=0;i<count;++i)reconstruction=std::max(reconstruction,std::abs(clean.getSample(channel,i)+noise.getSample(channel,i)-original.getSample(channel,i)));}
    float tolerance=(float)(2.0/std::pow(2.0,std::max(16,(int)reference->bitsPerSample)-1)+.000001);
    if(reconstruction>tolerance)throw std::runtime_error("Noise and clean exports did not reconstruct the original.");
    bool noiseExportSaved=true;
    processor.params.getParameter("procedural")->setValueNotifyingHost(1);
    if(!processor.ambienceReady.load())throw std::runtime_error("Quiet reference did not produce a procedural profile.");
    auto ambienceFile=output.getSiblingFile(output.getFileNameWithoutExtension()+"-procedural.wav");
    processor.saveRecording(ambienceFile,3);waitForJob();
    std::unique_ptr<juce::AudioFormatReader> ambienceReader(formats.createReaderFor(ambienceFile));
    if(!ambienceReader||ambienceReader->lengthInSamples!=(int64_t)std::llround(reference->sampleRate*3)||ambienceReader->numChannels!=reference->numChannels)throw std::runtime_error("Procedural export duration or layout failed.");
    juce::MemoryBlock proceduralState;processor.getStateInformation(proceduralState);
    NoiseBaseProcessor restored;restored.setStateInformation(proceduralState.getData(),(int)proceduralState.getSize());restored.prepareToPlay(48000,137);
    if(!restored.ambienceReady.load()||restored.fileMode.load())throw std::runtime_error("A profile was not restored independently of its source file.");
    restored.params.getParameter("reduction")->setValueNotifyingHost(0);
    double generatedEnergy=0;
    int continuousSamples=(int)(48000*(2*reference->lengthInSamples/reference->sampleRate+1));
    for(int i=0;i<continuousSamples;i+=137){juce::AudioBuffer<float> silence(2,137);silence.clear();juce::MidiBuffer midi;restored.processBlock(silence,midi);if(i>continuousSamples-48000)for(int k=0;k<137;++k){float sample=silence.getSample(0,k);if(!std::isfinite(sample))throw std::runtime_error("Procedural host output was non-finite.");generatedEnergy+=sample*sample;}}
    if(generatedEnergy<1e-10)throw std::runtime_error("Restored profile stopped generating on silent host input.");
    restored.standaloneMode.store(true);restored.togglePlayback();
    if(!restored.isPlaying())throw std::runtime_error("Restored standalone profile cannot start without a source file.");
    restored.stopPlayback();
    restored.releaseResources();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
    writeSnapshot(snapshot,image);
    processor.releaseResources();
    auto* report=new juce::DynamicObject;report->setProperty("passed",paused&&energy>0);report->setProperty("load_and_auto_scan",true);
    report->setProperty("preview_non_silent",energy>0);report->setProperty("pause_and_stop",paused);report->setProperty("export_created",output.existsAsFile());
    report->setProperty("sample_exact_loop_junction",exactLoop);report->setProperty("return_to_start_keeps_pause",rewindPaused);report->setProperty("return_to_start_keeps_play",rewindPlaying);
    report->setProperty("loop_repeats_past_eof",repeats);report->setProperty("loop_off_stops_at_eof",eofStops);
    report->setProperty("legacy_preset_restores_clean",legacyClean);report->setProperty("noise_export_preserves_timing_layout",noiseExportSaved);report->setProperty("clean_plus_noise_export_error",reconstruction);
    report->setProperty("procedural_profile_restores_without_source",true);report->setProperty("procedural_output_on_silent_host",generatedEnergy>0);report->setProperty("procedural_continues_beyond_source_duration",true);report->setProperty("procedural_export_duration",3);
    report->setProperty("status",processor.getStatus());return juce::var(report);
}

class NoiseBaseApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "NoiseBase";}
    const juce::String getApplicationVersion() override {return NOISEBASE_VERSION;}
    bool moreThanOneInstanceAllowed() override {return true;}
    void initialise(const juce::String& commandLine) override {
        auto args=juce::StringArray::fromTokens(commandLine,true);
        for(auto& item:args)item=item.unquoted();
        auto option=[&](const juce::String& name)->juce::String {int i=args.indexOf(name);return i>=0&&i+1<args.size()?args[i+1]:juce::String{};};
        if(args.contains("--self-test")||args.contains("--process")||args.contains("--render-ui")||args.contains("--validate-plugin")||args.contains("--ui-smoke")||args.contains("--check-updates")||args.contains("--theme-test")) {
            try {
                juce::var report;
                if(args.contains("--theme-test")){
                    report=runWindowThemeTests();
                    if(!option("--icon-preview").isEmpty()){
                        juce::Image sheet(juce::Image::ARGB,440,180,true);
                        {juce::Graphics graphics(sheet);for(int row=0;row<2;++row){graphics.setColour(OsTheme::background(row==1));graphics.fillRect(0,row*90,440,90);int x=16;for(int size:{16,32,48,64}){graphics.drawImage(RasterAssets::get().icon,juce::Rectangle<float>{(float)x,(float)(row*90+10),(float)size,(float)size},juce::RectanglePlacement::centred);x+=size+38;}}}
                        writeSnapshot(juce::File(option("--icon-preview")),sheet);
                    }
                }
                else if(args.contains("--check-updates")) {
                    NoiseBaseUpdater updater;bool finished=false;
                    updater.request(false,[&](juce::var result){report=result;finished=true;});
                    auto deadline=juce::Time::getMillisecondCounterHiRes()+40000;
                    while(!finished&&juce::Time::getMillisecondCounterHiRes()<deadline)juce::MessageManager::getInstance()->runDispatchLoopUntil(25);
                    if(!finished)throw std::runtime_error("Update check timed out.");
                }
                else if(args.contains("--self-test"))report=runEngineTests();
                else if(args.contains("--ui-smoke")) {
                    int i=args.indexOf("--ui-smoke");if(i+2>=args.size())throw std::runtime_error("Use --ui-smoke input output --snapshot image.png");
                    report=testFileDeck(juce::File(args[i+1]),juce::File(args[i+2]),juce::File(option("--snapshot")),juce::jlimit(0,2,option("--skin").getIntValue()));
                }
                else if(args.contains("--process")) {
                    int i=args.indexOf("--process");if(i+2>=args.size())throw std::runtime_error("Use --process input.wav output.wav");
                    auto analysis=analyseRecording(juce::File(args[i+1]));
                    juce::File output(args[i+2]);
                    if(output.existsAsFile())throw std::runtime_error("Output exists; choose a new copy.");
                    double seconds=option("--seconds").isEmpty()?60:option("--seconds").getDoubleValue();
                    if(args.contains("--procedural"))exportAmbience(analysis.profile,output,seconds,1,0);else exportRecording(analysis,output,14,.8f,1,0,nullptr,nullptr,args.contains("--noise-only"));
                    auto* result=new juce::DynamicObject;result->setProperty("passed",true);result->setProperty("samples",args.contains("--procedural")?(double)std::llround(seconds*analysis.sampleRate):(double)analysis.frames);
                    result->setProperty("sample_rate",analysis.sampleRate);result->setProperty("channels",analysis.channels);
                    result->setProperty("noise_reference_seconds",analysis.profile.quietSeconds);result->setProperty("estimated",analysis.profile.estimated);
                    result->setProperty("noise_only",args.contains("--noise-only"));
                    result->setProperty("procedural",args.contains("--procedural"));result->setProperty("seamless_loop",args.contains("--procedural"));
                    result->setProperty("output",output.getFullPathName());report=juce::var(result);
                } else if(args.contains("--render-ui")) {
                    NoiseBaseProcessor processor;processor.standaloneMode.store(!args.contains("--plugin-layout"));
                    int skin=juce::jlimit(0,2,option("--skin").getIntValue());
                    processor.params.getParameter("skin")->setValueNotifyingHost(skin*.5f);
                    processor.params.getParameter("noiseOnly")->setValueNotifyingHost(args.contains("--noise-only")||args.contains("--procedural")?1:0);
                    processor.params.getParameter("procedural")->setValueNotifyingHost(args.contains("--procedural")?1:0);
                    NoiseBaseEditor editor(processor);
                    // Parameter attachments are updated before the snapshot is painted.
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
                    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
                    writeSnapshot(juce::File(option("--render-ui")),image);
                    auto* result=new juce::DynamicObject;result->setProperty("passed",true);result->setProperty("skin",skin);report=juce::var(result);
                } else report=validatePlugin(juce::File(option("--validate-plugin")),option("--snapshot").isEmpty()?juce::File{}:juce::File(option("--snapshot")),option("--input").isEmpty()?juce::File{}:juce::File(option("--input")),option("--render-audio").isEmpty()?juce::File{}:juce::File(option("--render-audio")));
                if(!option("--report").isEmpty())juce::File(option("--report")).replaceWithText(juce::JSON::toString(report,true));
                if(report.hasProperty("passed") && !(bool)report["passed"])setApplicationReturnValue(2);
            } catch(const std::exception& e) {
                auto* error=new juce::DynamicObject;error->setProperty("passed",false);error->setProperty("error",juce::String(e.what()));
                if(!option("--report").isEmpty())juce::File(option("--report")).replaceWithText(juce::JSON::toString(juce::var(error),true));
                setApplicationReturnValue(1);
            }
            quit();return;
        }
        theme=std::make_unique<OsTheme>();window=std::make_unique<AppWindow>();
    }
    void shutdown() override {window.reset();theme.reset();}
    void systemRequestedQuit() override {quit();}
private:std::unique_ptr<OsTheme> theme;std::unique_ptr<AppWindow> window;
};
START_JUCE_APPLICATION(NoiseBaseApplication)
