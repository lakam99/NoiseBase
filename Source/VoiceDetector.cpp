#include "VoiceDetector.h"
#include <juce_dsp/juce_dsp.h>
#include "BinaryData.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
#endif
#include <mutex>

namespace {
std::once_flag initFlag;
std::unique_ptr<Ort::Env> sharedEnv;
void initialiseRuntime() {
    std::call_once(initFlag,[]{
       #if JUCE_WINDOWS
        HMODULE own=nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&initialiseRuntime),&own);
        wchar_t path[32768]{}; GetModuleFileNameW(own,path,32768);
        juce::File dll=juce::File(juce::String(path)).getSiblingFile("onnxruntime.dll");
        auto module=LoadLibraryExW(dll.getFullPathName().toWideCharPointer(),nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module) throw std::runtime_error("onnxruntime.dll must stay beside NoiseBase.");
        using GetBase=const OrtApiBase* (ORT_API_CALL*)();
        auto getBase=reinterpret_cast<GetBase>(GetProcAddress(module,"OrtGetApiBase"));
        if(!getBase) throw std::runtime_error("Incompatible ONNX Runtime.");
        auto api=getBase()->GetApi(ORT_API_VERSION);
        if(!api) throw std::runtime_error("ONNX Runtime API version mismatch.");
        Ort::InitApi(api);
       #endif
        sharedEnv=std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR,"NoiseBase");
        sharedEnv->DisableTelemetryEvents();
    });
}
}

struct VoiceDetector::Impl {
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory{nullptr};
    std::array<float,576> input{};
    std::array<float,256> state{};
    std::array<float,64> context{};
    Impl() {
        initialiseRuntime();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session=Ort::Session(*sharedEnv,BinaryData::silero_vad_onnx,(size_t)BinaryData::silero_vad_onnxSize,options);
        memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
    }
};
VoiceDetector::VoiceDetector():impl(std::make_unique<Impl>()) {}
VoiceDetector::~VoiceDetector()=default;
void VoiceDetector::reset() { impl->state.fill(0); impl->context.fill(0); }
float VoiceDetector::analyse(const float* frame) {
    auto& p=*impl;
    std::copy(p.context.begin(),p.context.end(),p.input.begin());
    for(int i=0;i<512;++i) p.input[(size_t)i+64]=juce::jlimit(-1.0f,1.0f,frame[i]*4);
    std::array<int64_t,2> inputShape{1,576};
    std::array<int64_t,3> stateShape{2,1,128};
    int64_t rate=16000;
    std::array<Ort::Value,3> values{
        Ort::Value::CreateTensor<float>(p.memory,p.input.data(),p.input.size(),inputShape.data(),inputShape.size()),
        Ort::Value::CreateTensor<float>(p.memory,p.state.data(),p.state.size(),stateShape.data(),stateShape.size()),
        Ort::Value::CreateTensor<int64_t>(p.memory,&rate,1,nullptr,0)};
    const char* names[]{"input","state","sr"}; const char* outputs[]{"output","stateN"};
    auto result=p.session.Run(Ort::RunOptions{nullptr},names,values.data(),3,outputs,2);
    std::copy_n(result[1].GetTensorData<float>(),256,p.state.begin());
    std::copy(p.input.end()-64,p.input.end(),p.context.begin());
    return juce::jlimit(0.0f,1.0f,result[0].GetTensorData<float>()[0]);
}

VoiceWorker::VoiceWorker():Thread("NoiseBase speech detector") {}
VoiceWorker::~VoiceWorker(){ stopThread(5000); }
void VoiceWorker::prepare(double sr) {
    stopThread(5000); rate=sr; phase=0; fifo.reset(); probability.store(1); available.store(false);failed.store(false);
    lowpass.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(sr,std::min(6500.0,sr*.40));
    lowpass.reset(); startThread();
}
void VoiceWorker::push(const juce::AudioBuffer<float>& buffer) {
    int channel=0;
    if(buffer.getNumChannels()>1 && buffer.getRMSLevel(1,0,buffer.getNumSamples())>buffer.getRMSLevel(0,0,buffer.getNumSamples())) channel=1;
    auto* data=buffer.getReadPointer(channel);
    for(int i=0;i<buffer.getNumSamples();++i) {
        float filtered=lowpass.processSample(data[i]);
        phase+=16000/rate;
        while(phase>=1) {
            phase-=1;
            int a,n1,b,n2; fifo.prepareToWrite(1,a,n1,b,n2);
            if(n1) { samples[(size_t)a]=filtered; fifo.finishedWrite(1); }
        }
    }
}
void VoiceWorker::run() {
    try {
        VoiceDetector detector; std::array<float,512> frame{}; available.store(true);
        while(!threadShouldExit()) {
            if(fifo.getNumReady()<512) { wait(2); continue; }
            inFlight.store(true);
            int a,n1,b,n2; fifo.prepareToRead(512,a,n1,b,n2);
            std::copy_n(samples.begin()+a,n1,frame.begin());
            if(n2) std::copy_n(samples.begin()+b,n2,frame.begin()+n1);
            fifo.finishedRead(n1+n2);
            probability.store(detector.analyse(frame.data()),std::memory_order_relaxed);
            inFlight.store(false);
        }
    } catch(...) { available.store(false); failed.store(true);probability.store(1); }
    inFlight.store(false);
}
void VoiceWorker::waitForPending() {
    auto deadline=juce::Time::getMillisecondCounter()+5000;
    while(isThreadRunning() && (!available.load() || fifo.getNumReady()>=512 || inFlight.load())) {
        if(juce::Time::getMillisecondCounter()>deadline)break;
        juce::Thread::sleep(1);
    }
}
