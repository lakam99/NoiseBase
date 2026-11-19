#include "Processor.h"
#include "Editor.h"
#include <fstream>

class MainPanel final : public juce::AudioAppComponent {
public:
    MainPanel() {
        processor.standaloneMode.store(true);
        juce::PropertiesFile::Options options;options.applicationName="NoiseBase";options.filenameSuffix="settings";options.folderName="NoiseBase";options.osxLibrarySubFolder="Application Support";
        preferences=std::make_unique<juce::PropertiesFile>(options);
        juce::MemoryBlock state;if(state.fromBase64Encoding(preferences->getValue("controls")))processor.setStateInformation(state.getData(),(int)state.getSize());
        editor=std::make_unique<NoiseBaseEditor>(processor);
        editor->onDevices=[this]{
            auto selector=std::make_unique<juce::AudioDeviceSelectorComponent>(deviceManager,0,2,0,2,false,false,true,false);
            selector->setSize(500,420);
            juce::DialogWindow::LaunchOptions options;options.content.setOwned(selector.release());
            options.dialogTitle="NoiseBase patch bay";options.dialogBackgroundColour=juce::Colour(0xff30332e);
            options.escapeKeyTriggersCloseButton=true;options.useNativeTitleBar=true;options.resizable=false;options.launchAsync();
        };
        addAndMakeVisible(*editor);setSize(editor->getWidth(),editor->getHeight());
        setAudioChannels(0,2);
    }
    ~MainPanel() override {shutdownAudio();juce::MemoryBlock state;processor.getStateInformation(state);preferences->setValue("controls",state.toBase64Encoding());preferences->saveIfNeeded();editor.reset();}
    void prepareToPlay(int block,double rate) override {processor.prepareToPlay(rate,block);}
    void releaseResources() override {processor.releaseResources();}
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        juce::AudioBuffer<float> view(info.buffer->getArrayOfWritePointers(),std::min(2,info.buffer->getNumChannels()),info.startSample,info.numSamples);
        juce::MidiBuffer midi;processor.processBlock(view,midi);
    }
    void resized() override {if(editor)editor->setBounds(getLocalBounds());}
private:
    NoiseBaseProcessor processor;
    std::unique_ptr<NoiseBaseEditor> editor;
    std::unique_ptr<juce::PropertiesFile> preferences;
    juce::TooltipWindow tips{this,650};
};

class AppWindow final : public juce::DocumentWindow {
public:
    AppWindow():DocumentWindow("NoiseBase",juce::Colour(0xff252923),allButtons) {
        setUsingNativeTitleBar(true);setContentOwned(new MainPanel,true);setResizable(false,false);
        centreWithSize(getWidth(),getHeight());setVisible(true);
    }
    void closeButtonPressed() override {juce::JUCEApplication::getInstance()->systemRequestedQuit();}
};

juce::var validatePlugin(const juce::File& file,const juce::File& snapshot,const juce::File& input={},const juce::File& audioOutput={}) {
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descriptions;format.findAllTypesForFile(descriptions,file.getFullPathName());
    if(descriptions.isEmpty()) throw std::runtime_error("VST3 discovery returned no plug-in.");
    juce::String error;
    auto plugin=format.createInstanceFromDescription(*descriptions[0],48000,512,error);
    if(!plugin)throw std::runtime_error(error.toStdString());
    auto* report=new juce::DynamicObject;
    report->setProperty("name",plugin->getName());report->setProperty("discovered",true);
    juce::MemoryBlock saved;plugin->getStateInformation(saved);plugin->setStateInformation(saved.getData(),(int)saved.getSize());
    report->setProperty("state_roundtrip",saved.getSize()>0);
    for(auto* param:plugin->getParameters()) if(auto* identified=dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) if(identified->paramID=="reduction")param->setValueNotifyingHost(0);
    bool finite=true;juce::Array<juce::var> runs;
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
        auto* run=new juce::DynamicObject;run->setProperty("sample_rate",rate);run->setProperty("latency_samples",plugin->getLatencySamples());runs.add(juce::var(run));
        plugin->releaseResources();
    }
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
        juce::FileOutputStream stream(snapshot);juce::PNGImageFormat png;png.writeImageToStream(image,stream);
        report->setProperty("editor_created",true);
    }
    report->setProperty("finite_output",finite);report->setProperty("runs",runs);report->setProperty("passed",finite&&saved.getSize()>0);
    return juce::var(report);
}

juce::var testFileDeck(const juce::File& source,const juce::File& output,const juce::File& snapshot,int skin) {
    NoiseBaseProcessor processor;processor.standaloneMode.store(true);
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
    bool paused=!processor.isPlaying();processor.stopPlayback();
    if(energy<1e-9)throw std::runtime_error("File preview returned silence.");
    if(output.existsAsFile())throw std::runtime_error("Smoke test output already exists.");
    processor.saveRecording(output);waitForJob();
    if(!output.existsAsFile())throw std::runtime_error(processor.getStatus().toStdString());
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
    juce::FileOutputStream stream(snapshot);juce::PNGImageFormat png;png.writeImageToStream(image,stream);
    processor.releaseResources();
    auto* report=new juce::DynamicObject;report->setProperty("passed",paused&&energy>0);report->setProperty("load_and_auto_scan",true);
    report->setProperty("preview_non_silent",energy>0);report->setProperty("pause_and_stop",paused);report->setProperty("export_created",output.existsAsFile());
    report->setProperty("status",processor.getStatus());return juce::var(report);
}

class NoiseBaseApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {return "NoiseBase";}
    const juce::String getApplicationVersion() override {return "1.1.0";}
    bool moreThanOneInstanceAllowed() override {return true;}
    void initialise(const juce::String& commandLine) override {
        auto args=juce::StringArray::fromTokens(commandLine,true);
        for(auto& item:args)item=item.unquoted();
        auto option=[&](const juce::String& name)->juce::String {int i=args.indexOf(name);return i>=0&&i+1<args.size()?args[i+1]:juce::String{};};
        if(args.contains("--self-test")||args.contains("--process")||args.contains("--render-ui")||args.contains("--validate-plugin")||args.contains("--ui-smoke")) {
            try {
                juce::var report;
                if(args.contains("--self-test"))report=runEngineTests();
                else if(args.contains("--ui-smoke")) {
                    int i=args.indexOf("--ui-smoke");if(i+2>=args.size())throw std::runtime_error("Use --ui-smoke input output --snapshot image.png");
                    report=testFileDeck(juce::File(args[i+1]),juce::File(args[i+2]),juce::File(option("--snapshot")),juce::jlimit(0,2,option("--skin").getIntValue()));
                }
                else if(args.contains("--process")) {
                    int i=args.indexOf("--process");if(i+2>=args.size())throw std::runtime_error("Use --process input.wav output.wav");
                    auto analysis=analyseRecording(juce::File(args[i+1]));
                    juce::File output(args[i+2]);
                    if(output.existsAsFile())throw std::runtime_error("Output exists; choose a new copy.");
                    exportRecording(analysis,output,14,.8f,1,0);
                    auto* result=new juce::DynamicObject;result->setProperty("passed",true);result->setProperty("samples",(double)analysis.frames);
                    result->setProperty("sample_rate",analysis.sampleRate);result->setProperty("channels",analysis.channels);
                    result->setProperty("noise_reference_seconds",analysis.profile.quietSeconds);result->setProperty("estimated",analysis.profile.estimated);
                    result->setProperty("output",output.getFullPathName());report=juce::var(result);
                } else if(args.contains("--render-ui")) {
                    NoiseBaseProcessor processor;processor.standaloneMode.store(true);
                    int skin=juce::jlimit(0,2,option("--skin").getIntValue());
                    processor.params.getParameter("skin")->setValueNotifyingHost(skin*.5f);
                    NoiseBaseEditor editor(processor);
                    // Parameter attachments are updated before the snapshot is painted.
                    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
                    auto image=editor.createComponentSnapshot(editor.getLocalBounds(),true,1.0f);
                    juce::FileOutputStream stream(juce::File(option("--render-ui")));juce::PNGImageFormat png;
                    if(!png.writeImageToStream(image,stream))throw std::runtime_error("UI render failed.");
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
        window=std::make_unique<AppWindow>();
    }
    void shutdown() override {window.reset();}
    void systemRequestedQuit() override {quit();}
private:std::unique_ptr<AppWindow> window;
};
START_JUCE_APPLICATION(NoiseBaseApplication)
