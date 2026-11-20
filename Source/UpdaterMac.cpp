#include "Updater.h"

namespace {
std::array<int,3> stableVersion(juce::String text){
    if(text.startsWithChar('v'))text=text.substring(1);auto parts=juce::StringArray::fromTokens(text,".",{});
    if(parts.size()!=3)throw std::runtime_error("Invalid stable release version.");std::array<int,3> version{};
    for(int i=0;i<3;++i){if(parts[i].isEmpty()||parts[i].length()>6||!parts[i].containsOnly("0123456789")||(parts[i].length()>1&&parts[i].startsWithChar('0')))throw std::runtime_error("Invalid stable release version.");version[(size_t)i]=parts[i].getIntValue();}return version;
}
}
NoiseBaseUpdater::NoiseBaseUpdater():Thread("NoiseBase release checker"){}
NoiseBaseUpdater::~NoiseBaseUpdater(){signalThreadShouldExit();stopThread(3000);}
bool NoiseBaseUpdater::request(bool prepare,std::function<void(juce::var)> completion){if(isThreadRunning())return false;preparing=prepare;callback=std::move(completion);startThread();return true;}
void NoiseBaseUpdater::run(){
    auto* object=new juce::DynamicObject;juce::var result(object);
    try{
        auto stream=juce::URL("https://api.github.com/repos/lakam99/NoiseBase/releases/latest").createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(20000).withExtraHeaders("User-Agent: NoiseBase/" NOISEBASE_VERSION "\r\nAccept: application/vnd.github+json\r\n"));
        if(!stream)throw std::runtime_error("Could not reach GitHub Releases.");juce::MemoryBlock bytes;stream->readIntoMemoryBlock(bytes,2097153);if(bytes.getSize()>2097152)throw std::runtime_error("Release metadata is too large.");
        auto release=juce::JSON::parse(bytes.toString());auto tag=release["tag_name"].toString();auto version=stableVersion(tag);auto current=stableVersion(NOISEBASE_VERSION);
        if(!release.isObject()||(bool)release["draft"]||(bool)release["prerelease"])throw std::runtime_error("Expected a stable published release.");
        juce::String name="NoiseBase-"+tag.substring(1)+"-macOS-arm64.zip";bool found=false;
        if(auto* assets=release["assets"].getArray())for(auto& asset:*assets)if(asset["name"].toString()==name&&asset["state"].toString()=="uploaded"){
            auto expected="https://github.com/lakam99/NoiseBase/releases/download/"+tag+"/"+name;
            if(asset["browser_download_url"].toString()!=expected)throw std::runtime_error("Unexpected release download URL.");found=true;
        }
        if(version>current&&!found)throw std::runtime_error("The newer release has no Apple Silicon package yet.");
        object->setProperty("passed",true);object->setProperty("available",version>current&&found);object->setProperty("version",tag.substring(1));object->setProperty("releaseUrl","https://github.com/lakam99/NoiseBase/releases/tag/"+tag);object->setProperty("manual_install",true);
    }catch(const std::exception& error){object->setProperty("passed",false);object->setProperty("error",juce::String(error.what()));}
    if(!threadShouldExit())juce::MessageManager::callAsync([completion=callback,result]{if(completion)completion(result);});
}
bool NoiseBaseUpdater::installAndRestart(const juce::var&,juce::String& error){error="Install the Apple Silicon package from GitHub Releases after closing NoiseBase and its host.";return false;}
