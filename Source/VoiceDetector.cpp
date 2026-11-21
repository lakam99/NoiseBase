#include "VoiceDetector.h"
#include "AsyncCleanup.h"
#include <juce_dsp/juce_dsp.h>
#include "BinaryData.h"
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>
#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
#endif
#include <mutex>
#if JUCE_MAC
#include <dlfcn.h>
#endif

namespace {
std::once_flag initFlag;
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
       #elif JUCE_MAC
        Dl_info location{};
        if(!dladdr(reinterpret_cast<void*>(&initialiseRuntime),&location)||!location.dli_fname)throw std::runtime_error("Could not locate the NoiseBase bundle.");
        auto library=juce::File(juce::String::fromUTF8(location.dli_fname)).getParentDirectory().getSiblingFile("Frameworks").getChildFile("libonnxruntime.dylib");
        auto module=dlopen(library.getFullPathName().toRawUTF8(),RTLD_NOW|RTLD_LOCAL);
        if(!module)throw std::runtime_error("The bundled Apple Silicon speech runtime could not be loaded.");
        using GetBase=const OrtApiBase* (*)();auto getBase=reinterpret_cast<GetBase>(dlsym(module,"OrtGetApiBase"));
        if(!getBase)throw std::runtime_error("Incompatible speech runtime.");
        auto api=getBase()->GetApi(ORT_API_VERSION);if(!api)throw std::runtime_error("Speech runtime API version mismatch.");Ort::InitApi(api);
       #endif
    });
}
}

struct VoiceDetector::Impl {
    // Release sessions and the environment on their owning analysis worker,
    // never from a plugin module's global destructors under the loader lock.
    Ort::Env env{nullptr};
    Ort::Session session{nullptr};
    Ort::MemoryInfo memory{nullptr};
    std::array<float,576> input{};
    std::array<float,256> state{};
    std::array<float,64> context{};
    Impl() {
        initialiseRuntime();env=Ort::Env(ORT_LOGGING_LEVEL_ERROR,"NoiseBase");env.DisableTelemetryEvents();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(1); options.SetInterOpNumThreads(1);
        options.AddConfigEntry("session.intra_op.allow_spinning","0");
        options.AddConfigEntry("session.inter_op.allow_spinning","0");
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        session=Ort::Session(env,BinaryData::silero_vad_onnx,(size_t)BinaryData::silero_vad_onnxSize,options);
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


struct VoiceWorker::State {
    struct Frame {std::array<float,512> samples{};uint64_t generation=0,ticket=0;};
    juce::AbstractFifo fifo{8};
    std::array<Frame,8> frames;
    std::atomic<uint64_t> generation{0},produced{0},processed{0},processedGeneration{0};
    std::atomic<float> probability{1};
    std::atomic<bool> available{false},failed{false},stalled{false};
    std::atomic<int> modelLoads{0},testDelay{0},testCleanupDelay{0};
};
struct VoiceWorker::Worker final:juce::Thread {
    explicit Worker(std::shared_ptr<State> value):Thread("NoiseBase speech detector"),state(std::move(value)){}
    void run() override {
        try {
            int delay=state->testDelay.exchange(0);
            if(delay>0){state->stalled.store(true);sleep(delay);state->stalled.store(false);}
            if(threadShouldExit())return;
            VoiceDetector detector; ++state->modelLoads;state->available.store(true);
            uint64_t generation=0;
            while(!threadShouldExit()) {
                if(state->fifo.getNumReady()<1){wait(2);continue;}
                int a,n1,b,n2;state->fifo.prepareToRead(1,a,n1,b,n2);
                auto frame=state->frames[(size_t)a];state->fifo.finishedRead(1);
                if(frame.generation!=state->generation.load()||frame.ticket+2<state->produced.load()){generation=0;continue;}
                if(generation!=frame.generation){detector.reset();generation=frame.generation;}
                float probability=detector.analyse(frame.samples.data());
                if(frame.generation==state->generation.load()&&frame.ticket+2>=state->produced.load()){state->probability.store(probability);state->processed.store(frame.ticket);state->processedGeneration.store(frame.generation);}
            }
            // Detector/session/environment cleanup completes here, on this
            // worker, before the thread exits. The host never releases ONNX.
            int cleanupDelay=state->testCleanupDelay.exchange(0);
            if(cleanupDelay>0){state->stalled.store(true);sleep(cleanupDelay);state->stalled.store(false);}
        }catch(...){state->available.store(false);state->failed.store(true);state->probability.store(1);}
    }
    std::shared_ptr<State> state;
};
VoiceWorker::VoiceWorker():state(std::make_shared<State>()){}
VoiceWorker::~VoiceWorker(){
    if(!worker)return;
    worker->signalThreadShouldExit();worker->notify();
    if(!worker->waitForThreadToExit(100)){
        auto* remaining=worker.release();
        deferNoiseBaseCleanup([remaining]{remaining->waitForThreadToExit(-1);delete remaining;});
    }
}
void VoiceWorker::prepare(double sr){
    rate=sr;lowpass.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(sr,std::min(6500.0,sr*.40));resetHistory();
    if(!worker||!worker->isThreadRunning()){
        worker.reset();state->available.store(false);state->failed.store(false);
        worker=std::make_unique<Worker>(state);worker->startThread();
    }
}
void VoiceWorker::resetHistory(){phase=0;fill=0;lowpass.reset();++state->generation;state->probability.store(1);}
void VoiceWorker::push(const juce::AudioBuffer<float>& buffer){
    if(buffer.getNumChannels()==0)return;
    int channel=buffer.getNumChannels()>1&&buffer.getRMSLevel(1,0,buffer.getNumSamples())>buffer.getRMSLevel(0,0,buffer.getNumSamples())?1:0;
    auto* samples=buffer.getReadPointer(channel);
    for(int i=0;i<buffer.getNumSamples();++i){float filtered=lowpass.processSample(samples[i]);phase+=16000/rate;
        while(phase>=1){phase-=1;frame[(size_t)fill++]=filtered;
            if(fill==512){fill=0;auto ticket=++state->produced;int a,n1,b,n2;state->fifo.prepareToWrite(1,a,n1,b,n2);
                if(n1){auto& destination=state->frames[(size_t)a];destination.samples=frame;destination.generation=state->generation.load();destination.ticket=ticket;state->fifo.finishedWrite(1);}
                else state->probability.store(1); // Never wait or learn from an obsolete speech decision.
            }
        }
    }
}
float VoiceWorker::probability() const{return state->processedGeneration.load()==state->generation.load()&&state->processed.load()+2>=state->produced.load()?state->probability.load():1.0f;}
bool VoiceWorker::available() const{return state->available.load();}
bool VoiceWorker::failed() const{return state->failed.load();}
int VoiceWorker::modelLoads() const{return state->modelLoads.load();}
void VoiceWorker::delayStartupForTest(int milliseconds){state->testDelay.store(milliseconds);}
void VoiceWorker::delayCleanupForTest(int milliseconds){state->testCleanupDelay.store(milliseconds);}
bool VoiceWorker::stalledForTest() const{return state->stalled.load();}
