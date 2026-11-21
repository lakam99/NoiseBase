#include "Processor.h"

namespace {
NoiseProfile cacheFloor(float sigma,double rate=48000){DenoiseEngine engine;engine.prepare(rate,1);NoiseProfile p;p.sampleRate=rate;p.fftSize=engine.fftSize();p.quietSeconds=1;p.power.assign((size_t)p.fftSize/2+1,.375f*p.fftSize*sigma*sigma);return p;}
NoiseProfile ambience(float sigma){auto p=cacheFloor(sigma);p.sourceChannels=2;p.ambiencePower[0]=p.power;p.ambiencePower[1]=p.power;p.ambienceCorrelation.assign(p.power.size(),.3f);return p;}
bool equalPower(const NoiseProfile& a,const NoiseProfile& b){return a.sampleRate==b.sampleRate&&a.fftSize==b.fftSize&&a.power==b.power;}
juce::AudioProcessorParameter* control(juce::AudioProcessor& p,const char* name){for(auto* v:p.getParameters())if(v->getName(128)==name)return v;throw std::runtime_error(std::string("Missing cache-test control: ")+name);}
juce::ValueTree componentTree(const juce::MemoryBlock& state){
    auto xml=juce::AudioProcessor::getXmlFromBinary(state.getData(),(int)state.getSize());
    if(xml&&xml->hasTagName("VST3PluginState")){auto* node=xml->getChildByName("IComponent");juce::MemoryBlock inner;if(node&&inner.fromBase64Encoding(node->getAllSubText()))xml=juce::AudioProcessor::getXmlFromBinary(inner.getData(),(int)inner.getSize());}
    return xml?juce::ValueTree::fromXml(*xml):juce::ValueTree{};
}
juce::ValueTree currentTree(juce::AudioProcessor& p){juce::MemoryBlock state;p.getStateInformation(state);return componentTree(state);}
void freshState(juce::AudioProcessor& p){NoiseBaseProcessor builder;juce::MemoryBlock state;juce::AudioProcessor::copyXmlToBinary(*builder.params.copyState().createXml(),state);
    if(dynamic_cast<NoiseBaseProcessor*>(&p)==nullptr){juce::XmlElement wrapper("VST3PluginState");wrapper.createNewChildElement("IComponent")->addTextElement(state.toBase64Encoding());juce::MemoryBlock outer;juce::AudioProcessor::copyXmlToBinary(wrapper,outer);state=outer;}p.setStateInformation(state.getData(),(int)state.getSize());}
struct CacheClock final:juce::AudioPlayHead {int64_t cursor=0;bool playing=true;juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setTimeInSamples(cursor);p.setTimeInSeconds(cursor/48000.0);p.setIsPlaying(playing);return p;}};
struct CacheBinding {explicit CacheBinding(juce::AudioProcessor& p):processor(p),previous(p.getPlayHead()),offline(p.isNonRealtime()){p.setPlayHead(&clock);p.setNonRealtime(false);}~CacheBinding(){processor.setPlayHead(previous);processor.setNonRealtime(offline);}juce::AudioProcessor& processor;juce::AudioPlayHead* previous;bool offline;CacheClock clock;};
struct DirtyListener final:juce::AudioProcessorListener {explicit DirtyListener(juce::AudioProcessor& p):owner(p){owner.addListener(this);}~DirtyListener(){owner.removeListener(this);}void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}void audioProcessorChanged(juce::AudioProcessor*,const ChangeDetails& details) override {if(details.nonParameterStateChanged)++notifications;}juce::AudioProcessor& owner;int notifications=0;};
double totalPower(const NoiseProfile& p){double sum=0;for(float v:p.power)sum+=v;return sum;}
}
bool verifyHostedCache(juce::AudioProcessor& plugin,juce::DynamicObject& report,const juce::File& stateFile){
    CacheBinding binding(plugin);DirtyListener listener(plugin);freshState(plugin);auto original=plugin.getBusesLayout(),buses=original;
    buses.inputBuses.set(0,juce::AudioChannelSet::stereo());buses.outputBuses.set(0,juce::AudioChannelSet::stereo());buses.inputBuses.set(1,juce::AudioChannelSet::stereo());if(!plugin.setBusesLayout(buses))return false;
    plugin.setRateAndBufferSizeDetails(48000,512);plugin.prepareToPlay(48000,512);std::mt19937 random(81019);std::normal_distribution<float> normal(0,1);juce::MidiBuffer midi;
    auto learn=[&](bool reference){binding.clock.cursor=0;for(int start=0;start<48000*6;start+=512){juce::AudioBuffer<float> audio(4,512);audio.clear();float level=(start<48000*3?1.0f:4.0f)*(reference?.01f:.006f);for(int i=0;i<512;++i){audio.setSample(reference?2:0,i,level*normal(random));audio.setSample(reference?3:1,i,level*normal(random));}plugin.processBlock(audio,midi);binding.clock.cursor+=512;juce::Thread::sleep(3);}};
    learn(false);control(plugin,"External reference")->setValueNotifyingHost(1);control(plugin,"Noise only")->setValueNotifyingHost(1);control(plugin,"Procedural noise")->setValueNotifyingHost(1);learn(true);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);juce::MemoryBlock saved;plugin.getStateInformation(saved);auto tree=componentTree(saved);
    ProfileCache main(false),reference(true);main.restore(tree.getChildWithProperty("kind","main"));reference.restore(tree.getChildWithProperty("kind","reference"));
    NoiseProfile mainA,mainB,refA,refB;uint64_t id=0;bool mainRegions=main.recall(1,id,mainA)==ProfileCache::found&&main.recall(5,id,mainB)==ProfileCache::found&&totalPower(mainB)>totalPower(mainA)*6;
    id=0;bool referenceRegions=reference.recall(1,id,refA)==ProfileCache::found&&reference.recall(5,id,refB)==ProfileCache::found&&totalPower(refB)>totalPower(refA)*6;
    bool dirty=dynamic_cast<NoiseBaseProcessor*>(&plugin)==nullptr||listener.notifications>0;
    bool written=stateFile==juce::File{}||stateFile.replaceWithData(saved.getData(),saved.getSize());
    if(written&&stateFile!=juce::File{}){
        auto bridged=tree.createCopy();bridged.getChildWithProperty("id","bridgeGaps").setProperty("value",1.0f,nullptr);juce::MemoryBlock component;juce::AudioProcessor::copyXmlToBinary(*bridged.createXml(),component);
        if(dynamic_cast<NoiseBaseProcessor*>(&plugin)==nullptr){juce::XmlElement wrapper("VST3PluginState");wrapper.createNewChildElement("IComponent")->addTextElement(component.toBase64Encoding());juce::MemoryBlock outer;juce::AudioProcessor::copyXmlToBinary(wrapper,outer);component=outer;}
        written=stateFile.getSiblingFile(stateFile.getFileNameWithoutExtension()+"-bridge.bin").replaceWithData(component.getData(),component.getSize());
    }
    auto* result=new juce::DynamicObject;result->setProperty("passed",mainRegions&&referenceRegions&&dirty&&written);result->setProperty("main_regions",main.count());result->setProperty("reference_regions",reference.count());result->setProperty("main_backgrounds_distinct",mainRegions);result->setProperty("reference_backgrounds_distinct",referenceRegions);result->setProperty("saved_state_bytes",(juce::int64)saved.getSize());if(dynamic_cast<NoiseBaseProcessor*>(&plugin))result->setProperty("song_dirty_notifications",listener.notifications);report.setProperty("learned_session_cache",juce::var(result));plugin.releaseResources();plugin.setBusesLayout(original);return mainRegions&&referenceRegions&&dirty&&written;
}
bool verifyRestoredCache(juce::AudioProcessor& plugin,juce::DynamicObject& report,const juce::File& stateFile){
    juce::MemoryBlock saved;if(!stateFile.loadFileAsData(saved)||saved.getSize()>16*1024*1024)return false;auto tree=componentTree(saved);ProfileCache main(false),reference(true);main.restore(tree.getChildWithProperty("kind","main"));reference.restore(tree.getChildWithProperty("kind","reference"));
    NoiseProfile mainA,refA;uint64_t id=0;bool known=main.recall(1,id,mainA)==ProfileCache::found;id=0;known&=reference.recall(1,id,refA)==ProfileCache::found;
    CacheBinding binding(plugin);plugin.setStateInformation(saved.getData(),(int)saved.getSize());auto buses=plugin.getBusesLayout();buses.inputBuses.set(0,juce::AudioChannelSet::stereo());buses.outputBuses.set(0,juce::AudioChannelSet::stereo());buses.inputBuses.set(1,juce::AudioChannelSet::stereo());bool layout=plugin.setBusesLayout(buses);plugin.setRateAndBufferSizeDetails(48000,512);plugin.prepareToPlay(48000,512);
    binding.clock.cursor=48000;binding.clock.playing=false;juce::AudioBuffer<float> audio(4,512);audio.clear();juce::MidiBuffer midi;plugin.processBlock(audio,midi);bool waits=audio.getMagnitude(0,512)==0;
    binding.clock.playing=true;std::mt19937 random(81020);std::normal_distribution<float> normal(0,1);for(int i=0;i<512;++i){audio.setSample(2,i,.01f*normal(random));audio.setSample(3,i,.01f*normal(random));}plugin.processBlock(audio,midi);
    auto afterReference=currentTree(plugin);NoiseProfile activeReference;bool bridged=(float)tree.getChildWithProperty("id","bridgeGaps")["value"]>.5f;
    bool ref=readAmbienceProfile(afterReference.getChildWithName("AmbienceProfile"),activeReference)&&equalPower(activeReference,refA);
    if(bridged){ProfileCache selected(true);selected.restore(afterReference.getChildWithProperty("kind","reference"));ref=equalPower(selected.latest(),refA);}
    control(plugin,"External reference")->setValueNotifyingHost(0);control(plugin,"Noise only")->setValueNotifyingHost(0);audio.clear();for(int i=0;i<512;++i){audio.setSample(0,i,.006f*normal(random));audio.setSample(1,i,.006f*normal(random));}plugin.processBlock(audio,midi);
    auto afterMain=currentTree(plugin);NoiseProfile activeMain;bool floor=readNoiseFloor(afterMain.getChildWithName("NoiseFloor"),activeMain)&&equalPower(activeMain,mainA);bool confidence=true;if(auto* native=dynamic_cast<NoiseBaseProcessor*>(&plugin))confidence=native->quietSeconds.load()>=.35f&&native->mainCacheHits.load()>0&&native->referenceCacheHits.load()>0;
    bool automatic=control(plugin,"Automatic noise learning")->getValue()>.5f;bool ok=known&&layout&&waits&&ref&&floor&&confidence&&automatic;
    auto* result=new juce::DynamicObject;result->setProperty("passed",ok);result->setProperty("main_regions_restored",main.count());result->setProperty("reference_regions_restored",reference.count());result->setProperty("stopped_sidechain_stays_silent",waits);result->setProperty("reference_cache_selected_before_new_learning",ref);result->setProperty("main_floor_recalled_before_new_learning",floor);result->setProperty("auto_stays_enabled",automatic);result->setProperty("bridge_mode",bridged);result->setProperty("only_512_reference_samples_processed",true);report.setProperty("restored_session_cache",juce::var(result));return ok;
}
juce::var runCacheTests(){
    bool passed=true;auto a=cacheFloor(.01f),b=cacheFloor(.04f);NoiseProfile decoded;
    bool floorRoundtrip=readNoiseFloor(noiseFloorState(a),decoded)&&equalPower(a,decoded);
    ProfileCache cache(false);auto first=cache.remember(a,2,0,cache.epoch());cache.observe(first.id,3,cache.epoch());auto second=cache.remember(b,12,10,cache.epoch());
    auto state=cache.state();ProfileCache restored(false);bool loaded=restored.restore(state);uint64_t id=0;NoiseProfile recalled;recalled.power.reserve(4097);
    bool regions=loaded&&restored.count()==2&&restored.recall(1,id,recalled)==ProfileCache::found&&equalPower(a,recalled)&&restored.recall(11,id,recalled)==ProfileCache::found&&equalPower(b,recalled)&&restored.recall(30,id,recalled)==ProfileCache::missing;
    auto malformed=state.createCopy();malformed.getChild(0).setProperty("begin",std::numeric_limits<double>::quiet_NaN(),nullptr);ProfileCache bad(false);bool rejects=!bad.restore(malformed)&&bad.count()==0;
    auto oldEpoch=restored.epoch();restored.clear();bool clear=restored.remember(a,1,0,oldEpoch).id==0&&restored.count()==0;
    ProfileCache separate(false);bool isolated=separate.count()==0&&cache.count()==2;
    ProfileCache bounded(true);for(int i=0;i<80;++i)bounded.remember(ambience(.01f),i*10+2,i*10,bounded.epoch());id=0;
    bool capped=bounded.count()==ProfileCache::capacity&&bounded.recall(1,id,recalled)==ProfileCache::missing&&bounded.recall(791,id,recalled)==ProfileCache::found;
    auto corrupt=bounded.state();corrupt.appendChild(corrupt.getChild(0).createCopy(),nullptr);bool rejectOverflow=!bounded.restore(corrupt)&&bounded.count()==0;
    DenoiseEngine converted;converted.prepare(96000,1);converted.setProfile(a);NoiseProfile at96;bool rates=converted.copyLearnedProfile(at96);
    ProfileCache resampled(false);resampled.remember(a,1,0,resampled.epoch());resampled.remember(at96,2,0,resampled.epoch());rates&=resampled.count()==1;
    ProfileCache replaced(false);replaced.remember(a,2,0,replaced.epoch());replaced.remember(b,2,0,replaced.epoch());id=0;bool edits=replaced.recall(1,id,recalled)==ProfileCache::found&&equalPower(b,recalled);
    ProfileCache stereo(true);auto left=ambience(.01f),changed=left;std::fill(changed.ambienceCorrelation.begin(),changed.ambienceCorrelation.end(),-.7f);stereo.remember(left,2,0,stereo.epoch());stereo.remember(changed,3,2,stereo.epoch());id=0;bool spatial=stereo.count()==2&&stereo.recall(2.5,id,recalled)==ProfileCache::found&&recalled.ambienceCorrelation==changed.ambienceCorrelation;
    auto* report=new juce::DynamicObject;auto stateFile=juce::File::createTempFile(".nbstate");NoiseBaseProcessor producer;bool learned=verifyHostedCache(producer,*report,stateFile);NoiseBaseProcessor reader;bool restart=verifyRestoredCache(reader,*report,stateFile);reader.learnTrack();bool manual=reader.cachedMainRegions()==0&&reader.cachedReferenceRegions()>0;stateFile.deleteFile();
    passed=floorRoundtrip&&regions&&rejects&&clear&&isolated&&capped&&rejectOverflow&&rates&&edits&&spatial&&learned&&restart&&manual;
    report->setProperty("passed",passed);report->setProperty("noise_floor_roundtrip",floorRoundtrip);report->setProperty("multiple_regions_restored",regions);report->setProperty("rejects_malformed_cache",rejects);report->setProperty("manual_clear_blocks_old_audio_writes",clear);report->setProperty("instance_isolation",isolated);report->setProperty("bounded_lru_regions",capped);report->setProperty("rejects_oversized_cache",rejectOverflow);report->setProperty("sample_rate_compatible_profiles",rates);report->setProperty("changed_source_replaces_cached_profile",edits);report->setProperty("stereo_changes_keep_separate_profiles",spatial);report->setProperty("learn_clears_only_selected_cache",manual);return juce::var(report);
}
