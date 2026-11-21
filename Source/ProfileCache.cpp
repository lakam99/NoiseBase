#include "ProfileCache.h"

bool validNoiseFloor(const NoiseProfile& p){
    if(!std::isfinite(p.sampleRate)||p.sampleRate<8000||p.sampleRate>384000||p.fftSize<256||p.fftSize>8192||(p.fftSize&(p.fftSize-1))||p.estimated||!std::isfinite(p.quietSeconds)||p.quietSeconds<.35||p.power.size()!=(size_t)p.fftSize/2+1)return false;
    double sum=0;for(float v:p.power){if(!std::isfinite(v)||v<0||v>1e9f)return false;sum+=v;}return sum>1e-12;
}
juce::ValueTree noiseFloorState(const NoiseProfile& p){
    if(!validNoiseFloor(p))return {};juce::MemoryOutputStream bytes;
    bytes.writeInt(1);bytes.writeInt(p.fftSize);bytes.writeDouble(p.sampleRate);bytes.writeDouble(p.quietSeconds);
    for(float v:p.power)bytes.writeFloat(v);juce::ValueTree result("NoiseFloor");result.setProperty("data",bytes.getMemoryBlock().toBase64Encoding(),nullptr);return result;
}
bool readNoiseFloor(const juce::ValueTree& state,NoiseProfile& result){
    if(!state.hasType("NoiseFloor"))return false;auto encoded=state["data"].toString();if(encoded.length()>50000)return false;
    juce::MemoryBlock bytes;if(!bytes.fromBase64Encoding(encoded))return false;juce::MemoryInputStream input(bytes,false);NoiseProfile p;
    if(input.readInt()!=1)return false;p.fftSize=input.readInt();p.sampleRate=input.readDouble();p.quietSeconds=input.readDouble();
    if(p.fftSize<256||p.fftSize>8192||bytes.getSize()!=24+((size_t)p.fftSize/2+1)*4)return false;
    p.power.resize((size_t)p.fftSize/2+1);for(auto& value:p.power)value=input.readFloat();if(!validNoiseFloor(p))return false;result=std::move(p);return true;
}
ProfileCache::ProfileCache(bool generator):ambience(generator){
    for(auto& e:slots){e.profile.power.reserve(4097);if(ambience){e.profile.ambiencePower[0].reserve(4097);e.profile.ambiencePower[1].reserve(4097);e.profile.ambienceCorrelation.reserve(4097);}}
}
NoiseBands ProfileCache::bandsFor(const NoiseProfile& p){auto result=noiseBands(p.power,p.sampleRate,p.fftSize);float normalization=(float)(10*std::log10(p.sampleRate*p.fftSize/(48000.0*2048)));for(auto& v:result)v-=normalization;return result;}
std::pair<float,float> ProfileCache::stereoFor(const NoiseProfile& p){double left=0,right=0,cross=0;for(size_t k=0;k<p.ambienceCorrelation.size();++k){double l=p.ambiencePower[0][k],r=p.ambiencePower[1][k];left+=l;right+=r;cross+=p.ambienceCorrelation[k]*std::sqrt(l*r);}return {(float)(10*std::log10(std::max(1e-20,right)/std::max(1e-20,left))),(float)(cross/std::max(1e-20,std::sqrt(left*right)))};}
int ProfileCache::at(double position) const {
    int best=-1;if(!std::isfinite(position))return best;
    for(int i=0;i<capacity;++i){auto& e=slots[(size_t)i];if(e.valid&&e.timed&&position>=e.begin-.025&&position<=e.end+.05&&(best<0||e.begin>slots[(size_t)best].begin||(e.begin==slots[(size_t)best].begin&&e.id>slots[(size_t)best].id)))best=i;}return best;
}
ProfileCache::Recall ProfileCache::recall(double position,uint64_t& current,NoiseProfile& output){
    const juce::SpinLock::ScopedTryLockType guard(lock);if(!guard.isLocked())return busy;int index=at(position);
    if(index<0){current=0;return missing;}auto& e=slots[(size_t)index];e.used=++serial;latestId=e.id;
    if(current==e.id)return unchanged;output=e.profile;current=e.id;return found;
}
ProfileCache::Result ProfileCache::remember(const NoiseProfile& p,double position,double begin,uint64_t expected){
    if(!accepts(p))return {};auto bands=bandsFor(p);auto stereo=ambience?stereoFor(p):std::pair<float,float>{0,0};bool timed=std::isfinite(position)&&std::abs(position)<1e10;
    const juce::SpinLock::ScopedTryLockType guard(lock);if(!guard.isLocked()||expected!=generation.load())return {};
    int index=timed?at(position):-1;
    auto same=[&](const Entry& e){return !differentNoise(bands,e.bands)&&std::abs(stereo.first-e.balance)<=3&&std::abs(stereo.second-e.correlation)<=.3f;};
    if(index>=0&&!same(slots[(size_t)index]))index=-1;
    if(index<0)for(int i=0;i<capacity;++i){auto& e=slots[(size_t)i];if(e.valid&&e.timed==timed&&(!timed||(position>=e.end&&position-e.end<=2))&&same(e)){index=i;break;}}
    bool created=index<0;
    if(created){
        for(int i=0;i<capacity;++i)if(!slots[(size_t)i].valid){index=i;break;}
        if(index<0){index=0;for(int i=1;i<capacity;++i)if(slots[(size_t)i].used<slots[(size_t)index].used)index=i;}
        auto& e=slots[(size_t)index];if(!e.valid)++entries;if(e.valid&&e.timed)--timedEntries;if(timed)++timedEntries;e.id=nextId++;e.valid=true;e.timed=timed;
        e.begin=timed?std::min(position,std::isfinite(begin)?begin:position):0;e.end=timed?position:0;e.notifiedEnd=e.end;
    }
    auto& e=slots[(size_t)index];float level=0;for(size_t b=0;b<bands.size();++b)level+=std::abs(bands[b]-e.bands[b]);
    bool changed=created||level/bands.size()>1||std::abs(stereo.first-e.balance)>1||std::abs(stereo.second-e.correlation)>.1f;
    e.profile=p;e.bands=bands;e.balance=stereo.first;e.correlation=stereo.second;e.used=++serial;latestId=e.id;if(timed)e.end=std::max(e.end,position);
    if(e.end-e.notifiedEnd>=5){changed=true;e.notifiedEnd=e.end;}return {e.id,changed};
}
bool ProfileCache::observe(uint64_t id,double position,uint64_t expected){
    if(!id||!std::isfinite(position))return false;const juce::SpinLock::ScopedTryLockType guard(lock);if(!guard.isLocked()||expected!=generation.load())return false;
    for(auto& e:slots)if(e.valid&&e.id==id&&e.timed&&position>=e.begin&&position<=e.end+2){e.end=std::max(e.end,position);if(e.end-e.notifiedEnd>=5){e.notifiedEnd=e.end;return true;}break;}return false;
}
void ProfileCache::clear(){const juce::SpinLock::ScopedLockType guard(lock);++generation;for(auto& e:slots)e.valid=false;entries.store(0);timedEntries.store(0);latestId=0;}
NoiseProfile ProfileCache::latest() const {const juce::SpinLock::ScopedLockType guard(lock);for(auto& e:slots)if(e.valid&&e.id==latestId)return e.profile;return {};}
juce::ValueTree ProfileCache::state() const {
    struct Saved {NoiseProfile profile;double begin,end;uint64_t id;bool timed;};std::vector<Saved> copy;uint64_t recent=0;
    {const juce::SpinLock::ScopedLockType guard(lock);copy.reserve((size_t)entries.load());for(auto& e:slots)if(e.valid)copy.push_back({e.profile,e.begin,e.end,e.id,e.timed});recent=latestId;}
    juce::ValueTree root("ProfileCache");root.setProperty("schema",1,nullptr);root.setProperty("kind",ambience?"reference":"main",nullptr);root.setProperty("latest",(juce::int64)recent,nullptr);
    for(auto& e:copy){juce::ValueTree entry("Region");entry.setProperty("begin",e.begin,nullptr);entry.setProperty("end",e.end,nullptr);entry.setProperty("timed",e.timed,nullptr);entry.setProperty("id",(juce::int64)e.id,nullptr);entry.appendChild(ambience?ambienceProfileState(e.profile):noiseFloorState(e.profile),nullptr);root.appendChild(entry,nullptr);}return root;
}
bool ProfileCache::restore(const juce::ValueTree& root){
    std::vector<Entry> decoded;bool valid=root.hasType("ProfileCache")&&(int)root["schema"]==1&&root["kind"].toString()==(ambience?"reference":"main")&&root.getNumChildren()<=capacity;
    uint64_t maximum=0,recent=(uint64_t)(juce::int64)root["latest"];
    if(valid)for(auto child:root){Entry e;e.id=(uint64_t)(juce::int64)child["id"];e.begin=(double)child["begin"];e.end=(double)child["end"];e.timed=(bool)child["timed"];
        bool profile=ambience?readAmbienceProfile(child.getChildWithName("AmbienceProfile"),e.profile):readNoiseFloor(child.getChildWithName("NoiseFloor"),e.profile);
        if(!child.hasType("Region")||!profile||!e.id||e.id>1000000000000ULL||!std::isfinite(e.begin)||!std::isfinite(e.end)||std::abs(e.begin)>1e10||std::abs(e.end)>1e10||e.end<e.begin||std::any_of(decoded.begin(),decoded.end(),[&](auto& old){return old.id==e.id;})){valid=false;break;}
        e.valid=true;e.bands=bandsFor(e.profile);auto stereo=ambience?stereoFor(e.profile):std::pair<float,float>{0,0};e.balance=stereo.first;e.correlation=stereo.second;e.notifiedEnd=e.end;e.used=e.id;maximum=std::max(maximum,e.id);decoded.push_back(std::move(e));
    }
    const juce::SpinLock::ScopedLockType guard(lock);++generation;for(auto& e:slots)e.valid=false;entries.store(0);timedEntries.store(0);latestId=0;
    if(!valid)return false;
    for(size_t i=0;i<decoded.size();++i){auto& from=decoded[i];auto& to=slots[i];to.profile=from.profile;to.bands=from.bands;to.balance=from.balance;to.correlation=from.correlation;to.begin=from.begin;to.end=from.end;to.notifiedEnd=from.end;to.id=from.id;to.used=from.used;to.timed=from.timed;to.valid=true;}
    entries.store((int)decoded.size());timedEntries.store((int)std::count_if(decoded.begin(),decoded.end(),[](auto& e){return e.timed;}));nextId=maximum+1;serial=maximum;latestId=std::any_of(decoded.begin(),decoded.end(),[&](auto& e){return e.id==recent;})?recent:maximum;return true;
}
