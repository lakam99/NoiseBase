#include "Updater.h"
#include <cmath>
#if JUCE_WINDOWS
#include <windows.h>
#include <shellapi.h>
#endif

namespace {
juce::File powershell() {
    return juce::File::getSpecialLocation(juce::File::windowsSystemDirectory).getChildFile("WindowsPowerShell/v1.0/powershell.exe");
}
juce::File applicationDirectory() {return juce::File::getSpecialLocation(juce::File::currentExecutableFile).getParentDirectory();}
juce::var failure(const juce::String& message) {auto* result=new juce::DynamicObject;result->setProperty("passed",false);result->setProperty("error",message);return juce::var(result);}
}
NoiseBaseUpdater::NoiseBaseUpdater():Thread("NoiseBase release checker"){}
NoiseBaseUpdater::~NoiseBaseUpdater(){signalThreadShouldExit();stopThread(3000);}
bool NoiseBaseUpdater::request(bool prepare,std::function<void(juce::var)> completion) {
    if(isThreadRunning())return false;
    preparing=prepare;progress.store(-1);phase.store(0);callback=std::move(completion);startThread();return true;
}
void NoiseBaseUpdater::run() {
    juce::var result;
    auto helper=applicationDirectory().getChildFile("NoiseBase-Update.ps1");
    auto report=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("NoiseBase-update-"+juce::Uuid{}.toString(),".json",false);
    auto progressFile=juce::File(report.getFullPathName()+".progress.json");
    if(!helper.existsAsFile())result=failure("The update helper is missing. Reinstall the complete NoiseBase package.");
    else {
        juce::StringArray arguments{powershell().getFullPathName(),"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",helper.getFullPathName(),"-Mode",preparing?"Prepare":"Check","-CurrentVersion",NOISEBASE_VERSION,"-Report",report.getFullPathName()};
        juce::ChildProcess child;
        if(!child.start(arguments,0))result=failure("Could not start the update helper.");
        else {
            auto deadline=juce::Time::getMillisecondCounterHiRes()+(preparing?360000.0:35000.0);
            while(child.isRunning()&&!threadShouldExit()&&juce::Time::getMillisecondCounterHiRes()<deadline){
                if(progressFile.existsAsFile()&&progressFile.getSize()<=4096){auto update=juce::JSON::parse(progressFile);if(update.isObject()){double value=(double)update["fraction"];if(std::isfinite(value)&&value>=-1&&value<=1){progress.store(value);phase.store(juce::jlimit(0,5,(int)update["phase"]));}}}
                wait(100);
            }
            if(child.isRunning()){child.kill();result=failure("The update check was cancelled or timed out.");}
            else if(report.existsAsFile())result=juce::JSON::parse(report);
            else result=failure("The update service did not return a report. Try again when connected to the internet.");
        }
    }
    report.deleteFile();progressFile.deleteFile();juce::File(progressFile.getFullPathName()+".tmp").deleteFile();
    juce::File(progressFile.getFullPathName()+".previous").deleteFile();
    if(!result.isObject())result=failure("The update response could not be read.");
    if(!threadShouldExit())juce::MessageManager::callAsync([completion=callback,result]{if(completion)completion(result);});
}
bool NoiseBaseUpdater::installAndRestart(const juce::var& prepared,juce::String& error) {
    auto installed=applicationDirectory();
    auto marker=juce::JSON::parse(installed.getChildFile(".noisebase-install.json"));
    if(marker["product"].toString()!="studio.redweb.noisebase"||marker["installRoot"].toString()!=installed.getFullPathName()) {error="Install NoiseBase with Install-NoiseBase.ps1 before using in-app installation.";return false;}
    juce::File installer(prepared["installer"].toString()),package(prepared["packageRoot"].toString());
    auto cache=juce::File(juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA",{})).getChildFile("NoiseBase/Updates");
    if(!installer.existsAsFile()||!package.isDirectory()||!installer.isAChildOf(cache)||!package.isAChildOf(cache)){error="The verified update package is missing or outside the update cache.";return false;}
    juce::StringArray options{"-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass","-File",installer.getFullPathName(),"-PackageRoot",package.getFullPathName(),"-InstallRoot",installed.getFullPathName(),"-Vst3Root",marker["vst3Root"].toString(),"-WaitForProcess",juce::String((int)::GetCurrentProcessId()),"-Relaunch","-Report",cache.getParentDirectory().getChildFile("last-update-result.json").getFullPathName()};
    // ShellExecute starts a detached, hidden helper that can outlive the app.
    auto params=options.joinIntoString("\" \"");params="\""+params+"\"";
#if JUCE_WINDOWS
    auto status=(INT_PTR)ShellExecuteW(nullptr,L"open",powershell().getFullPathName().toWideCharPointer(),params.toWideCharPointer(),installer.getParentDirectory().getFullPathName().toWideCharPointer(),SW_HIDE);
    if(status>32)return true;
#endif
    error="The installer could not be started. The current installation was left in place.";return false;
}
