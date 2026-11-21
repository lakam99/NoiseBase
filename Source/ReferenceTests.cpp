#include "Processor.h"
#include "AsyncCleanup.h"
#include "BinaryData.h"
#include <random>
#include <numeric>

namespace {
struct ReferenceClock final:juce::AudioPlayHead {
    int64_t cursor=0;bool playing=true;
    juce::Optional<PositionInfo> getPosition() const override {PositionInfo info;info.setTimeInSamples(cursor);info.setTimeInSeconds(cursor/48000.0);info.setIsPlaying(playing);return info;}
};
struct ClockBinding {
    explicit ClockBinding(juce::AudioProcessor& p):processor(p),previous(p.getPlayHead()),offline(p.isNonRealtime()){processor.setPlayHead(&clock);processor.setNonRealtime(false);}
    ~ClockBinding(){processor.setPlayHead(previous);processor.setNonRealtime(offline);}
    juce::AudioProcessor& processor;juce::AudioPlayHead* previous;bool offline;ReferenceClock clock;
};
juce::AudioProcessorParameter* parameter(juce::AudioProcessor& p,const char* name){for(auto* value:p.getParameters())if(value->getName(128)==name)return value;throw std::runtime_error(std::string("Missing parameter: ")+name);}
NoiseProfile savedProfile(juce::AudioProcessor& processor){
    juce::MemoryBlock state;processor.getStateInformation(state);auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
    if(xml&&xml->hasTagName("VST3PluginState")){auto* component=xml->getChildByName("IComponent");juce::MemoryBlock inner;if(component&&inner.fromBase64Encoding(component->getAllSubText()))xml=juce::AudioProcessor::getXmlFromBinary(inner.getData(),(int)inner.getSize());}
    NoiseProfile p;if(xml)readAmbienceProfile(juce::ValueTree::fromXml(*xml).getChildWithName("AmbienceProfile"),p);return p;
}
double generatedRms(const NoiseProfile& profile){ProceduralNoise generator;generator.prepare(profile.sampleRate,profile.sourceChannels,91877);generator.setProfile(profile);double sum=0;int count=(int)profile.sampleRate;for(int i=0;i<count*3;++i){float sample[2]{};generator.next(sample);if(i>=count)sum+=sample[0]*sample[0];}return std::sqrt(sum/(count*2));}
}

bool verifyHostedReference(juce::AudioProcessor& plugin,juce::DynamicObject& report){
    ClockBinding binding(plugin);auto& clock=binding.clock;
    auto original=plugin.getBusesLayout();bool layouts=plugin.getBusCount(true)==2;juce::Array<juce::var> rows;
    if(!layouts)return false;
    for(int main:{1,2})for(int auxiliary:{1,2}){auto buses=original;buses.inputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(main));buses.outputBuses.set(0,juce::AudioChannelSet::canonicalChannelSet(main));buses.inputBuses.set(1,juce::AudioChannelSet::canonicalChannelSet(auxiliary));layouts&=plugin.setBusesLayout(buses);}
    auto buses=original;buses.inputBuses.set(0,juce::AudioChannelSet::stereo());buses.outputBuses.set(0,juce::AudioChannelSet::stereo());buses.inputBuses.set(1,juce::AudioChannelSet::stereo());
    if(!plugin.setBusesLayout(buses))return false;
    parameter(plugin,"External reference")->setValueNotifyingHost(1);parameter(plugin,"Noise only")->setValueNotifyingHost(1);parameter(plugin,"Procedural noise")->setValueNotifyingHost(1);
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(1);parameter(plugin,"Bypass")->setValueNotifyingHost(0);parameter(plugin,"Mix")->setValueNotifyingHost(1);parameter(plugin,"Output")->setValueNotifyingHost(.5f);
    plugin.setRateAndBufferSizeDetails(48000,512);plugin.prepareToPlay(48000,512);
    std::mt19937 random(16171);std::normal_distribution<float> normal(0,1);juce::MidiBuffer midi;double peakCallback=0;
    auto feed=[&](double seconds,float reference,bool mainVoice,bool paced){double energy=0;int measured=0;int length=(int)(seconds*48000);
        for(int start=0;start<length;start+=512){int count=std::min(512,length-start);juce::AudioBuffer<float> audio(plugin.getTotalNumInputChannels(),count);audio.clear();
            for(int i=0;i<count;++i){if(mainVoice){float value=.2f*std::sin((float)(start+i)*juce::MathConstants<float>::twoPi*523/48000);audio.setSample(0,i,value);audio.setSample(1,i,value);}if(audio.getNumChannels()>2){float left=reference*normal(random),independent=reference*normal(random);audio.setSample(2,i,left);audio.setSample(3,i,.5f*(.6f*left+.8f*independent));}}
            auto begin=juce::Time::getMillisecondCounterHiRes();plugin.processBlock(audio,midi);peakCallback=std::max(peakCallback,juce::Time::getMillisecondCounterHiRes()-begin);
            if(clock.playing||plugin.isNonRealtime())clock.cursor+=count;
            if(start>length-48000)for(int i=0;i<count;++i){float value=audio.getSample(0,i);if(!std::isfinite(value))throw std::runtime_error("Non-finite sidechain output.");energy+=value*value;++measured;}
            if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin)){if(!native->waitForReferenceForTest(5000))throw std::runtime_error("Reference worker did not drain.");}
            else if(paced)juce::Thread::sleep(3);
        }return std::sqrt(energy/std::max(1,measured));};
    double learned=feed(4,.01f,true,true);auto first=savedProfile(plugin);bool learnedOk=validAmbienceProfile(first)&&std::abs(learned/.01-1)<.3;
    bool meter=true,hostClock=true;if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin)){meter=native->inputLevel.load()>.015f&&native->inputLevel.load()<.1f;hostClock=native->referenceTransportKnown.load()&&native->referenceTransportRunning.load()&&native->hostPositionSeconds.load()>3.98&&native->hostPositionSeconds.load()<4;}
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(0);feed(.1f,0,false,false);juce::MemoryBlock held;plugin.getStateInformation(held);auto frozen=savedProfile(plugin);
    double holding=feed(2,.08f,true,true);auto afterHold=savedProfile(plugin);bool hold=validAmbienceProfile(frozen)&&frozen.ambiencePower==afterHold.ambiencePower&&std::abs(holding/std::max(learned,1e-12)-1)<.25;
    plugin.releaseResources();plugin.prepareToPlay(48000,512);feed(.1,0,false,false);bool reprepareHold=frozen.ambiencePower==savedProfile(plugin).ambiencePower;
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(1);double changed=feed(4,.04f,true,true);bool follows=changed>learned*2.5&&validAmbienceProfile(savedProfile(plugin));
    // Seeking to an empty timeline region in AUTO must invalidate both the
    // model and queued work, rather than using the previous clip's background.
    clock.cursor+=480000;plugin.releaseResources();plugin.prepareToPlay(48000,512);double seekSilence=feed(.25,0,false,false);bool seekReset=seekSilence<1e-7&&!validAmbienceProfile(savedProfile(plugin));
    double relearned=feed(4,.02f,false,true);bool relearns=std::abs(relearned/.02-1)<.3;
    clock.cursor=0;double loopFirst=feed(512.0/48000,.01f,false,true);bool loopReset=loopFirst<1e-7&&!validAmbienceProfile(savedProfile(plugin));
    double loopLevel=feed(3,.01f,false,true);loopReset&=std::abs(loopLevel/.01-1)<.3;
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(0);
    double gap=feed(2,0,false,false);bool waitsForInput=gap<1e-7&&validAmbienceProfile(savedProfile(plugin));
    if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin))meter&=native->inputLevel.load()==0&&!native->referenceSignal.load();
    double resumed=feed(2,.01f,false,false);bool resumes=std::abs(resumed/.01-1)<.3;
    auto beforeStop=savedProfile(plugin);clock.playing=false;
    double stopped=feed(2,.04f,false,false);bool stopsWithHost=stopped<1e-7&&savedProfile(plugin).ambiencePower==beforeStop.ambiencePower;
    if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin)){meter&=native->inputLevel.load()>.06f&&native->referenceSignal.load();hostClock&=!native->referenceTransportRunning.load();}
    clock.playing=true;double restarted=feed(2,.01f,false,false);bool restarts=std::abs(restarted/.01-1)<.3;
    parameter(plugin,"Bypass")->setValueNotifyingHost(1);feed(.25,.2f,false,false);double bypass=feed(1,.2f,false,false);bool isolated=bypass<1e-7;
    plugin.releaseResources();buses.inputBuses.set(1,juce::AudioChannelSet::disabled());bool disabled=plugin.setBusesLayout(buses);
    plugin.setStateInformation(held.getData(),(int)held.getSize());plugin.prepareToPlay(48000,512);double restored=feed(2,0,false,false);bool restoredWaits=restored<1e-7&&validAmbienceProfile(savedProfile(plugin));
    plugin.releaseResources();buses.inputBuses.set(1,juce::AudioChannelSet::stereo());bool reconnected=plugin.setBusesLayout(buses);parameter(plugin,"Bypass")->setValueNotifyingHost(0);plugin.prepareToPlay(48000,512);
    double restoredActive=feed(2,.005f,false,false);bool restoredOk=std::abs(restoredActive/.01-1)<.3;
    clock.playing=false;plugin.releaseResources();plugin.setNonRealtime(true);plugin.prepareToPlay(48000,512);double offline=feed(2,.005f,false,false);bool offlineWorks=std::abs(offline/.01-1)<.3;
    bool finiteTail=plugin.getTailLengthSeconds()<1;
    plugin.releaseResources();bool layoutRestored=plugin.setBusesLayout(original);
    bool passed=layouts&&disabled&&reconnected&&layoutRestored&&learnedOk&&hold&&reprepareHold&&follows&&seekReset&&relearns&&loopReset&&waitsForInput&&resumes&&stopsWithHost&&restarts&&isolated&&restoredWaits&&restoredOk&&offlineWorks&&finiteTail&&meter&&hostClock&&peakCallback<100;
    auto* result=new juce::DynamicObject;result->setProperty("passed",passed);result->setProperty("mono_stereo_bus_layouts",layouts);result->setProperty("learned_reference_rms",learned);result->setProperty("held_output_rms",holding);result->setProperty("changed_reference_rms",changed);result->setProperty("host_stopped_rms",stopped);result->setProperty("reference_gap_rms",gap);result->setProperty("restored_without_sidechain_rms",restored);result->setProperty("restored_with_sidechain_rms",restoredActive);result->setProperty("hold_profile_exact",hold);result->setProperty("hold_survives_host_reprepare",reprepareHold);result->setProperty("follows_background_changes",follows);result->setProperty("auto_seek_discards_old_profile",seekReset);result->setProperty("relearns_after_seek",relearns);result->setProperty("auto_loop_wrap_discards_old_profile",loopReset);result->setProperty("waits_for_reference_input",waitsForInput);result->setProperty("resumes_when_reference_returns",resumes);result->setProperty("stops_with_host_even_with_live_sidechain",stopsWithHost);result->setProperty("restarts_with_host",restarts);result->setProperty("reference_never_passed_to_bypass_output",isolated);result->setProperty("restored_profile_waits_for_input",restoredWaits);result->setProperty("saved_profile_with_reference",restoredOk);result->setProperty("offline_render_with_stopped_transport",offlineWorks);result->setProperty("finite_sidechain_tail",finiteTail);result->setProperty("native_reference_meter",meter);result->setProperty("native_host_clock",hostClock);result->setProperty("peak_callback_ms",peakCallback);report.setProperty("sidechain_reference",juce::var(result));return passed;
}

bool verifyHostedBridge(juce::AudioProcessor& plugin,juce::DynamicObject& report){
    ClockBinding binding(plugin);auto original=plugin.getBusesLayout(),buses=original;
    buses.inputBuses.set(0,juce::AudioChannelSet::stereo());buses.outputBuses.set(0,juce::AudioChannelSet::stereo());buses.inputBuses.set(1,juce::AudioChannelSet::stereo());
    if(!plugin.setBusesLayout(buses))return false;bool passed=true;juce::Array<juce::var> rows;
    for(float gapLimit:{.75f,.20f}){
        NoiseBaseProcessor builder;auto tree=builder.params.copyState();
        for(auto id:{"externalReference","noiseOnly","procedural","auto","bridgeGaps"})tree.getChildWithProperty("id",id).setProperty("value",1.0f,nullptr);
        tree.getChildWithProperty("id","bridgeTime").setProperty("value",gapLimit,nullptr);
        juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(*tree.createXml(),state);
        if(dynamic_cast<NoiseBaseProcessor*>(&plugin)==nullptr){juce::XmlElement wrapper("VST3PluginState");wrapper.createNewChildElement("IComponent")->addTextElement(state.toBase64Encoding());juce::MemoryBlock outer;juce::AudioProcessor::copyXmlToBinary(wrapper,outer);state=outer;}
        plugin.setStateInformation(state.getData(),(int)state.getSize());binding.clock.cursor=0;plugin.setRateAndBufferSizeDetails(48000,512);plugin.prepareToPlay(48000,512);
        int latency=plugin.getLatencySamples();bool delay=latency==98048;bool tail=plugin.getTailLengthSeconds()>=latency/48000.0+.15;
        std::mt19937 random(54119);std::normal_distribution<float> normal(0,1);juce::MidiBuffer midi;
        std::array<double,6> energy{};std::array<int,6> measured{};double peakCallback=0;bool metBridge=false;
        for(int start=0;start<48000*12;start+=512){int count=std::min(512,48000*12-start);juce::AudioBuffer<float> audio(4,count);audio.clear();
            for(int i=0;i<count;++i){double t=(start+i)/48000.0;float level=t<4?.01f:t>=4.5&&t<8.5?.03f:0;audio.setSample(2,i,level*normal(random));audio.setSample(3,i,level*normal(random));}
            auto begin=juce::Time::getMillisecondCounterHiRes();plugin.processBlock(audio,midi);peakCallback=std::max(peakCallback,juce::Time::getMillisecondCounterHiRes()-begin);binding.clock.cursor+=count;
            for(int i=0;i<count;++i){double t=(start+i-latency)/48000.0;int window=t>=2&&t<3?0:t>=4.2&&t<4.35?1:t>=4.5&&t<4.6?2:t>=6&&t<7.5?3:t>=8.8&&t<9.5?4:t>=-1.5&&t<-.25?5:-1;
                if(window>=0){float value=audio.getSample(0,i);energy[(size_t)window]+=value*value;++measured[(size_t)window];}}
            if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin)){metBridge|=native->referenceBridging.load();if(!native->waitForReferenceForTest(5000))throw std::runtime_error("Bridge analysis timeout.");}
            else juce::Thread::sleep(3);
        }
        std::array<double,6> rms{};for(size_t i=0;i<rms.size();++i)rms[i]=std::sqrt(energy[i]/std::max(1,measured[i]));
        bool gap=gapLimit>.5f?std::abs(rms[1]/.01-1)<.25:rms[1]<1e-7;
        bool transition=gapLimit>.5f?rms[2]>.005&&rms[2]<.026:true;
        bool cleanEnd=rms[4]<1e-7,noPreroll=rms[5]<1e-7,levels=std::abs(rms[0]/.01-1)<.25&&std::abs(rms[3]/.03-1)<.25;
        bool ok=delay&&tail&&gap&&transition&&cleanEnd&&noPreroll&&levels&&peakCallback<100;passed&=ok;
        auto* row=new juce::DynamicObject;row->setProperty("maximum_gap_seconds",gapLimit);row->setProperty("reported_latency_samples",latency);row->setProperty("first_clip_rms",rms[0]);row->setProperty("gap_rms",rms[1]);row->setProperty("transition_first_100ms_rms",rms[2]);row->setProperty("next_clip_rms",rms[3]);row->setProperty("after_last_clip_rms",rms[4]);row->setProperty("before_first_clip_rms",rms[5]);row->setProperty("gap_behavior_correct",gap);row->setProperty("transition_uses_next_clip_at_its_time",transition);row->setProperty("end_fades_to_silence",cleanEnd);row->setProperty("no_early_noise_before_first_clip",noPreroll);row->setProperty("peak_callback_ms",peakCallback);if(dynamic_cast<NoiseBaseProcessor*>(&plugin))row->setProperty("bridge_indicator_seen",metBridge);row->setProperty("passed",ok);rows.add(juce::var(row));
        plugin.releaseResources();
    }
    // Check actual sample timing, not just the reported number. Bypass keeps
    // the main signal aligned through both the denoiser and lookahead delay.
    parameter(plugin,"Bypass")->setValueNotifyingHost(1);plugin.prepareToPlay(48000,512);int reported=plugin.getLatencySamples();float timingError=0;binding.clock.cursor=0;
    for(int start=0;start<reported+1024;start+=137){int count=std::min(137,reported+1024-start);juce::AudioBuffer<float> audio(4,count);audio.clear();if(start==0){audio.setSample(0,0,.125f);audio.setSample(1,0,-.125f);}juce::MidiBuffer midi;plugin.processBlock(audio,midi);binding.clock.cursor+=count;
        for(int i=0;i<count;++i)for(int c=0;c<2;++c){float expected=start+i==reported?(c==0?.125f:-.125f):0;timingError=std::max(timingError,std::abs(audio.getSample(c,i)-expected));}}
    bool timing=reported==98048&&timingError<1e-7f;passed&=timing;
    // VST3 host parameter changes are delivered in process(), and latency
    // restart notifications run on the message thread. Exercise that contract.
    parameter(plugin,"Bypass")->setValueNotifyingHost(0);parameter(plugin,"Bridge gaps")->setValueNotifyingHost(0);
    {juce::AudioBuffer<float> audio(4,512);audio.clear();juce::MidiBuffer midi;plugin.processBlock(audio,midi);binding.clock.cursor+=512;}
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    plugin.releaseResources();plugin.prepareToPlay(48000,512);bool latencyOff=plugin.getLatencySamples()==2048;passed&=latencyOff;plugin.releaseResources();passed&=plugin.setBusesLayout(original);
    auto* result=new juce::DynamicObject;result->setProperty("passed",passed);result->setProperty("disabled_restores_normal_latency",latencyOff);result->setProperty("bypass_impulse_matches_reported_latency",timing);result->setProperty("bypass_impulse_max_error",timingError);result->setProperty("runs",rows);report.setProperty("gap_lookahead",juce::var(result));return passed;
}

juce::var runReferenceTests(){
    bool passed=true;juce::Array<juce::var> runs;
    for(double rate:{44100.0,48000.0,96000.0}){
        ReferenceLearner learner;learner.prepare(rate);std::mt19937 random(18121);std::normal_distribution<float> normal(0,1);double sourceEnergy=0;int measured=0;int channels=rate==44100?1:2;
        for(int start=0;start<(int)(rate*3);start+=512){juce::AudioBuffer<float> audio(channels,512);for(int i=0;i<512;++i){float left=.012f*normal(random);audio.setSample(0,i,left);if(channels==2)audio.setSample(1,i,.5f*(.6f*left+.8f*.012f*normal(random)));if(start>(int)(rate*2)){sourceEnergy+=left*left;++measured;}}learner.push(audio);if(!learner.waitForIdleForTest(5000))throw std::runtime_error("Reference analyzer timeout.");}
        auto profile=learner.snapshot();bool valid=validAmbienceProfile(profile);double rms=valid?generatedRms(profile):0,expected=std::sqrt(sourceEnergy/std::max(1,measured));
        double stereo=valid&&channels==2?std::sqrt(std::accumulate(profile.ambiencePower[1].begin(),profile.ambiencePower[1].end(),0.0)/std::accumulate(profile.ambiencePower[0].begin(),profile.ambiencePower[0].end(),0.0)):1;
        bool speechGuard=true,shapeChanged=true;double dialogueRatio=1,coloredRatio=0;uint64_t duringDialogue=0;
        if(rate==48000&&valid){
            int bytes=0;auto* data=BinaryData::getNamedResource("referencespeech_wav",bytes);if(!data)throw std::runtime_error("Missing synthesized speech test fixture.");
            juce::WavAudioFormat wav;std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(new juce::MemoryInputStream(data,(size_t)bytes,false),true));if(!reader)throw std::runtime_error("Invalid speech test fixture.");
            juce::AudioBuffer<float> phrase(1,(int)reader->lengthInSamples);reader->read(&phrase,0,phrase.getNumSamples(),0,true,false);
            auto before=learner.updates();int length=(int)(phrase.getNumSamples()*rate/reader->sampleRate);
            for(int start=0;start<length+(int)(rate*.4);start+=512){juce::AudioBuffer<float> audio(2,512);audio.clear();for(int i=0;i<512;++i){double index=(start+i)*reader->sampleRate/rate;int a=(int)index;float value=0;if(a+1<phrase.getNumSamples())value=phrase.getSample(0,a)+(float)(index-a)*(phrase.getSample(0,a+1)-phrase.getSample(0,a));float left=.012f*normal(random),right=.5f*(.6f*left+.8f*.012f*normal(random));audio.setSample(0,i,value+left);audio.setSample(1,i,value+right);}learner.push(audio);if(!learner.waitForIdleForTest(5000))throw std::runtime_error("Speech guard timeout.");}
            auto after=learner.snapshot();duringDialogue=learner.updates()-before;
            dialogueRatio=std::sqrt(std::accumulate(after.power.begin(),after.power.end(),0.0)/std::accumulate(profile.power.begin(),profile.power.end(),0.0));
            speechGuard=dialogueRatio>.8&&dialogueRatio<1.15;
            float filtered[2]{};
            for(int start=0;start<(int)(rate*3);start+=512){juce::AudioBuffer<float> audio(2,512);for(int i=0;i<512;++i)for(int c=0;c<2;++c){filtered[c]=.9f*filtered[c]+.1f*.012f*normal(random);audio.setSample(c,i,filtered[c]*std::sqrt(19.0f));}learner.push(audio);if(!learner.waitForIdleForTest(5000))throw std::runtime_error("Colored-reference timeout.");}
            auto colored=learner.snapshot();double low=0,high=0;int lowCount=0,highCount=0;
            for(size_t k=1;k<colored.power.size();++k){double hz=k*colored.sampleRate/colored.fftSize;if(hz>=100&&hz<1000){low+=colored.power[k];++lowCount;}if(hz>=8000&&hz<16000){high+=colored.power[k];++highCount;}}
            coloredRatio=(low/std::max(1,lowCount))/std::max(1e-20,high/std::max(1,highCount));shapeChanged=coloredRatio>4;
        }
        learner.reset();bool reset=!learner.ready()&&!validAmbienceProfile(learner.snapshot());
        bool ok=valid&&std::abs(rms/std::max(expected,1e-12)-1)<.15&&std::abs(stereo-(channels==2?.5:1))<.05&&speechGuard&&shapeChanged&&reset;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("channels",channels);row->setProperty("source_rms",expected);row->setProperty("generated_rms",rms);row->setProperty("stereo_level_ratio",stereo);row->setProperty("synthesized_dialogue_rejected",speechGuard);if(rate==48000){row->setProperty("floor_level_ratio_after_dialogue",dialogueRatio);row->setProperty("updates_during_phrase_including_leading_quiet",(int)duringDialogue);row->setProperty("follows_colored_spectrum",shapeChanged);row->setProperty("colored_low_high_mean_power_ratio",coloredRatio);}row->setProperty("manual_reset_clears_reference",reset);row->setProperty("passed",ok);runs.add(juce::var(row));passed&=ok;
    }
    NoiseBaseProcessor processor;auto* report=new juce::DynamicObject;passed&=verifyHostedReference(processor,*report);passed&=verifyHostedBridge(processor,*report);
    auto legacy=processor.params.copyState();legacy.removeChild(legacy.getChildWithProperty("id","externalReference"),nullptr);juce::MemoryBlock oldState;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),oldState);processor.params.getParameter("externalReference")->setValueNotifyingHost(1);processor.setStateInformation(oldState.getData(),(int)oldState.getSize());bool compatible=processor.params.getRawParameterValue("externalReference")->load()<.5f;passed&=compatible;report->setProperty("old_sessions_default_to_clip_reference",compatible);
    auto delayed=std::make_unique<ReferenceLearner>();delayed->prepare(48000);juce::AudioBuffer<float> silence(1,512);silence.clear();delayed->push(silence);bool drained=delayed->waitForIdleForTest(5000);delayed->delayCleanupForTest(600);auto begin=juce::Time::getMillisecondCounterHiRes();delayed.reset();double retirement=juce::Time::getMillisecondCounterHiRes()-begin;
    auto deadline=juce::Time::getMillisecondCounterHiRes()+5000;while(pendingNoiseBaseCleanups()>0&&juce::Time::getMillisecondCounterHiRes()<deadline)juce::Thread::sleep(2);
    bool cleanup=drained&&retirement<350&&pendingNoiseBaseCleanups()==0;passed&=cleanup;
    report->setProperty("runs",runs);report->setProperty("slow_reference_cleanup_ms",retirement);report->setProperty("slow_reference_cleanup_safe",cleanup);report->setProperty("passed",passed);return juce::var(report);
}
