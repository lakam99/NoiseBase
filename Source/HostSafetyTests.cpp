#include "Processor.h"
#include "AsyncCleanup.h"

namespace {
bool drain(){auto end=juce::Time::getMillisecondCounterHiRes()+5000;while(pendingNoiseBaseCleanups()>0&&juce::Time::getMillisecondCounterHiRes()<end)juce::Thread::sleep(2);return pendingNoiseBaseCleanups()==0;}
}
juce::var runHostSafetyTests(){
    drain();bool passed=true;
    auto worker=std::make_unique<VoiceWorker>();worker->delayStartupForTest(600);worker->prepare(48000);
    auto deadline=juce::Time::getMillisecondCounterHiRes()+1000;while(!worker->stalledForTest()&&juce::Time::getMillisecondCounterHiRes()<deadline)juce::Thread::sleep(1);
    bool stalled=worker->stalledForTest();auto begin=juce::Time::getMillisecondCounterHiRes();worker.reset();double retireMs=juce::Time::getMillisecondCounterHiRes()-begin;bool retired=pendingNoiseBaseCleanups()>0;passed&=stalled&&retired&&retireMs<350&&drain();
    auto cached=std::make_unique<VoiceWorker>();cached->prepare(48000);deadline=juce::Time::getMillisecondCounterHiRes()+5000;while(!cached->available()&&!cached->failed()&&juce::Time::getMillisecondCounterHiRes()<deadline)juce::Thread::sleep(2);
    cached->prepare(44100);cached->prepare(96000);bool reused=cached->available()&&cached->modelLoads()==1;
    cached->delayCleanupForTest(600);begin=juce::Time::getMillisecondCounterHiRes();cached.reset();double cleanupMs=juce::Time::getMillisecondCounterHiRes()-begin;
    bool cleanupDeferred=pendingNoiseBaseCleanups()>0;passed&=reused&&cleanupDeferred&&cleanupMs<350&&drain();
    auto processor=std::make_unique<NoiseBaseProcessor>();processor->delaySpeechStartupForTest(600);processor->setNonRealtime(true);processor->prepareToPlay(48000,64);
    double peakCallback=0;juce::AudioBuffer<float> block(2,64);juce::MidiBuffer midi;
    for(int i=0;i<400;++i){block.clear();if(i%13==0)processor->learnTrack();begin=juce::Time::getMillisecondCounterHiRes();processor->processBlock(block,midi);peakCallback=std::max(peakCallback,juce::Time::getMillisecondCounterHiRes()-begin);}
    begin=juce::Time::getMillisecondCounterHiRes();processor.reset();double undoMs=juce::Time::getMillisecondCounterHiRes()-begin;passed&=peakCallback<100&&undoMs<350&&drain();
    auto source=juce::File::createTempFile(".wav");
    {juce::AudioBuffer<float> noise(1,48000*4);juce::Random random(1901);for(int i=0;i<noise.getNumSamples();++i)noise.setSample(0,i,.005f*(random.nextFloat()-.5f));juce::WavAudioFormat wav;std::unique_ptr<juce::OutputStream> stream(source.createOutputStream());auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(48000).withNumChannels(1).withBitsPerSample(16));if(!writer||!writer->writeFromAudioSampleBuffer(noise,0,noise.getNumSamples()))throw std::runtime_error("Could not create lifecycle fixture.");}
    double peakScanUndo=0;
    for(int i=0;i<8;++i){auto instance=std::make_unique<NoiseBaseProcessor>();instance->loadRecording(source);instance->learnTrack();begin=juce::Time::getMillisecondCounterHiRes();instance.reset();peakScanUndo=std::max(peakScanUndo,juce::Time::getMillisecondCounterHiRes()-begin);}
    bool cleaned=drain();source.deleteFile();passed&=peakScanUndo<350&&cleaned;
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("simulated_slow_worker_was_deferred",stalled&&retired);report->setProperty("slow_worker_retirement_ms",retireMs);report->setProperty("model_reused_across_rate_changes",reused);report->setProperty("slow_learned_model_cleanup_deferred",cleanupDeferred);report->setProperty("slow_learned_model_cleanup_ms",cleanupMs);report->setProperty("peak_offline_flagged_callback_ms",peakCallback);report->setProperty("undo_during_slow_startup_ms",undoMs);report->setProperty("cancelled_scan_undo_cycles",8);report->setProperty("peak_scan_undo_ms",peakScanUndo);report->setProperty("background_cleanup_drained",cleaned);return juce::var(report);
}
