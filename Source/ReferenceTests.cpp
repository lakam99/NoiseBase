#include "Processor.h"
#include "AsyncCleanup.h"
#include "BinaryData.h"
#include <random>
#include <numeric>

namespace {
juce::AudioProcessorParameter* parameter(juce::AudioProcessor& p,const char* name){for(auto* value:p.getParameters())if(value->getName(128)==name)return value;throw std::runtime_error(std::string("Missing parameter: ")+name);}
NoiseProfile savedProfile(juce::AudioProcessor& processor){
    juce::MemoryBlock state;processor.getStateInformation(state);auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
    if(xml&&xml->hasTagName("VST3PluginState")){auto* component=xml->getChildByName("IComponent");juce::MemoryBlock inner;if(component&&inner.fromBase64Encoding(component->getAllSubText()))xml=juce::AudioProcessor::getXmlFromBinary(inner.getData(),(int)inner.getSize());}
    NoiseProfile p;if(xml)readAmbienceProfile(juce::ValueTree::fromXml(*xml).getChildWithName("AmbienceProfile"),p);return p;
}
double generatedRms(const NoiseProfile& profile){ProceduralNoise generator;generator.prepare(profile.sampleRate,profile.sourceChannels,91877);generator.setProfile(profile);double sum=0;int count=(int)profile.sampleRate;for(int i=0;i<count*3;++i){float sample[2]{};generator.next(sample);if(i>=count)sum+=sample[0]*sample[0];}return std::sqrt(sum/(count*2));}
}

bool verifyHostedReference(juce::AudioProcessor& plugin,juce::DynamicObject& report){
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
            if(start>length-48000)for(int i=0;i<count;++i){float value=audio.getSample(0,i);if(!std::isfinite(value))throw std::runtime_error("Non-finite sidechain output.");energy+=value*value;++measured;}
            if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin)){if(!native->waitForReferenceForTest(5000))throw std::runtime_error("Reference worker did not drain.");}
            else if(paced)juce::Thread::sleep(3);
        }return std::sqrt(energy/std::max(1,measured));};
    double learned=feed(4,.01f,true,true);auto first=savedProfile(plugin);bool learnedOk=validAmbienceProfile(first)&&std::abs(learned/.01-1)<.3;
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(0);feed(.1f,0,false,false);juce::MemoryBlock held;plugin.getStateInformation(held);auto frozen=savedProfile(plugin);
    double holding=feed(2,.08f,true,true);auto afterHold=savedProfile(plugin);bool hold=validAmbienceProfile(frozen)&&frozen.ambiencePower==afterHold.ambiencePower&&std::abs(holding/std::max(learned,1e-12)-1)<.25;
    plugin.releaseResources();plugin.prepareToPlay(48000,512);feed(.1,0,false,false);bool reprepareHold=frozen.ambiencePower==savedProfile(plugin).ambiencePower;
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(1);double changed=feed(4,.04f,true,true);bool follows=changed>learned*2.5&&validAmbienceProfile(savedProfile(plugin));
    parameter(plugin,"Automatic noise learning")->setValueNotifyingHost(0);double stopped=feed(2,0,false,false);bool continues=std::abs(stopped/std::max(changed,1e-12)-1)<.3;
    parameter(plugin,"Bypass")->setValueNotifyingHost(1);feed(.25,.2f,false,false);double bypass=feed(1,.2f,false,false);bool isolated=bypass<1e-7;
    plugin.releaseResources();buses.inputBuses.set(1,juce::AudioChannelSet::disabled());bool disabled=plugin.setBusesLayout(buses);
    plugin.setStateInformation(held.getData(),(int)held.getSize());plugin.prepareToPlay(48000,512);double restored=feed(2,0,false,false);bool restoredOk=std::abs(restored/.01-1)<.3;
    plugin.releaseResources();bool layoutRestored=plugin.setBusesLayout(original);
    bool passed=layouts&&disabled&&layoutRestored&&learnedOk&&hold&&reprepareHold&&follows&&continues&&isolated&&restoredOk&&peakCallback<100;
    auto* result=new juce::DynamicObject;result->setProperty("passed",passed);result->setProperty("mono_stereo_bus_layouts",layouts);result->setProperty("learned_reference_rms",learned);result->setProperty("held_output_rms",holding);result->setProperty("changed_reference_rms",changed);result->setProperty("source_stopped_rms",stopped);result->setProperty("restored_without_sidechain_rms",restored);result->setProperty("hold_profile_exact",hold);result->setProperty("hold_survives_host_reprepare",reprepareHold);result->setProperty("follows_background_changes",follows);result->setProperty("continues_without_reference",continues);result->setProperty("reference_never_passed_to_bypass_output",isolated);result->setProperty("saved_profile_without_source",restoredOk);result->setProperty("peak_callback_ms",peakCallback);report.setProperty("sidechain_reference",juce::var(result));return passed;
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
    NoiseBaseProcessor processor;auto* report=new juce::DynamicObject;passed&=verifyHostedReference(processor,*report);
    auto legacy=processor.params.copyState();legacy.removeChild(legacy.getChildWithProperty("id","externalReference"),nullptr);juce::MemoryBlock oldState;juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(),oldState);processor.params.getParameter("externalReference")->setValueNotifyingHost(1);processor.setStateInformation(oldState.getData(),(int)oldState.getSize());bool compatible=processor.params.getRawParameterValue("externalReference")->load()<.5f;passed&=compatible;report->setProperty("old_sessions_default_to_clip_reference",compatible);
    auto delayed=std::make_unique<ReferenceLearner>();delayed->prepare(48000);juce::AudioBuffer<float> silence(1,512);silence.clear();delayed->push(silence);bool drained=delayed->waitForIdleForTest(5000);delayed->delayCleanupForTest(600);auto begin=juce::Time::getMillisecondCounterHiRes();delayed.reset();double retirement=juce::Time::getMillisecondCounterHiRes()-begin;
    auto deadline=juce::Time::getMillisecondCounterHiRes()+5000;while(pendingNoiseBaseCleanups()>0&&juce::Time::getMillisecondCounterHiRes()<deadline)juce::Thread::sleep(2);
    bool cleanup=drained&&retirement<350&&pendingNoiseBaseCleanups()==0;passed&=cleanup;
    report->setProperty("runs",runs);report->setProperty("slow_reference_cleanup_ms",retirement);report->setProperty("slow_reference_cleanup_safe",cleanup);report->setProperty("passed",passed);return juce::var(report);
}
