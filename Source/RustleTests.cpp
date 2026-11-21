#include "Offline.h"
#include "BinaryData.h"

namespace {
struct Render {std::vector<float> samples;uint64_t detected=0;double milliseconds=0;};
Render renderRustle(const std::vector<float>& input,double rate,bool enabled){
    DenoiseEngine engine;engine.prepare(rate,2);if(!enabled)engine.disableRustleForTest();
    NoiseProfile floor;floor.sampleRate=rate;floor.fftSize=engine.fftSize();floor.quietSeconds=1;floor.power.assign((size_t)floor.fftSize/2+1,1e-10f);engine.setProfile(floor);
    Render result;result.samples.resize(input.size());int latency=engine.latency(),length=(int)input.size()+latency;
    auto begin=juce::Time::getMillisecondCounterHiRes();
    for(int start=0;start<length;start+=137){int count=std::min(137,length-start);juce::AudioBuffer<float> audio(2,count);for(int i=0;i<count;++i){float value=start+i<(int)input.size()?input[(size_t)(start+i)]:0;audio.setSample(0,i,value);audio.setSample(1,i,-value);}engine.process(audio,1,14,.8f,1,0,false,false);
        for(int i=0;i<count;++i){int destination=start+i-latency;if(destination>=0&&destination<(int)input.size())result.samples[(size_t)destination]=audio.getSample(0,i);if(std::abs(audio.getSample(0,i)+audio.getSample(1,i))>1e-6f)throw std::runtime_error("Rustle stereo mask changed polarity.");}}
    result.milliseconds=juce::Time::getMillisecondCounterHiRes()-begin;result.detected=engine.rustleFrames();return result;
}
double bandLevel(const std::vector<float>& input,double rate,double begin,double end,bool high){
    juce::dsp::IIR::Filter<float> filter;filter.coefficients=high?juce::dsp::IIR::Coefficients<float>::makeHighPass(rate,2500):juce::dsp::IIR::Coefficients<float>::makeLowPass(rate,1500);
    double sum=0;int count=0;for(int i=0;i<std::min((int)input.size(),(int)(end*rate));++i){double value=filter.processSample(input[(size_t)i]);if(i>=begin*rate){sum+=value*value;++count;}}return std::sqrt(sum/std::max(1,count));
}
double differenceDb(double current,double previous){return 20*std::log10(std::max(1e-20,current)/std::max(1e-20,previous));}
std::vector<float> speechFixture(){int bytes=0;auto* data=BinaryData::getNamedResource("referencespeech_wav",bytes);juce::WavAudioFormat wav;auto stream=std::make_unique<juce::MemoryInputStream>(data,(size_t)bytes,false);std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(),true));if(!reader)throw std::runtime_error("Missing clean speech fixture.");
    juce::AudioBuffer<float> original(1,(int)reader->lengthInSamples+16);original.clear();reader->read(&original,0,(int)reader->lengthInSamples,0,true,false);std::vector<float> result((size_t)std::llround(reader->lengthInSamples*44100/reader->sampleRate));juce::LagrangeInterpolator converter;converter.process(reader->sampleRate/44100,original.getReadPointer(0),result.data(),(int)result.size());return result;
}
}
juce::var runRustleTests(){
    bool passed=true;juce::Array<juce::var> rows;
    for(double rate:{44100.0,48000.0,96000.0}){
        std::vector<float> signal((size_t)(rate*3));juce::Random random(201101);juce::dsp::IIR::Filter<float> high,low;high.coefficients=juce::dsp::IIR::Coefficients<float>::makeHighPass(rate,3300);low.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(rate,11000);
        for(size_t i=0;i<signal.size();++i){double t=i/rate;float friction=low.processSample(high.processSample((random.nextFloat()-.5f)*std::sqrt(12.0f)));double envelope=std::exp(-std::pow((t-1.10)/.023,2))+.95*std::exp(-std::pow((t-1.235)/.026,2));signal[i]=.015f*std::sin((float)(2*juce::MathConstants<double>::pi*500*t))+.08f*(float)envelope*friction;}
        auto before=renderRustle(signal,rate,false),after=renderRustle(signal,rate,true);double reduction=differenceDb(bandLevel(after.samples,rate,1,1.4,true),bandLevel(before.samples,rate,1,1.4,true));double voice=differenceDb(bandLevel(after.samples,rate,1,1.4,false),bandLevel(before.samples,rate,1,1.4,false));
        RustleSuppressor steady;int size=rate>60000?4096:2048;steady.prepare(rate,size,size/4);std::vector<float> power((size_t)size/2+1,1);for(int i=0;i<(int)(rate*3/(size/4));++i){std::fill(power.begin(),power.end(),i*(size/4)<rate*.5?1.0f:100.0f);steady.analyse(power);}bool sustained=steady.activity()<.001f&&!steady.excludesLearning();
        bool ok=after.detected>0&&reduction<-2.5&&std::abs(voice)<.15&&sustained;auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("friction_high_band_reduction_db",reduction);row->setProperty("low_voice_band_change_db",voice);row->setProperty("detected_frames",(juce::int64)after.detected);row->setProperty("sustained_floor_not_classified_as_rustle",sustained);row->setProperty("render_milliseconds",after.milliseconds);row->setProperty("passed",ok);passed&=ok;rows.add(juce::var(row));
    }
    auto speech=speechFixture();auto baseline=renderRustle(speech,44100,false),clean=renderRustle(speech,44100,true);double energy=0,error=0;for(size_t i=0;i<speech.size();++i){double delta=clean.samples[i]-baseline.samples[i];error+=delta*delta;energy+=baseline.samples[i]*baseline.samples[i];}
    double relativeError=std::sqrt(error/std::max(1e-20,energy)),speechChange=differenceDb(bandLevel(clean.samples,44100,0,speech.size()/44100.0,true),bandLevel(baseline.samples,44100,0,speech.size()/44100.0,true));bool protectedSpeech=relativeError<.02&&std::abs(speechChange)<.5;passed&=protectedSpeech;
    // A single wide-band /s/-like onset receives only the conservative branch,
    // even if its spectrum is indistinguishable from an initial friction stroke.
    std::vector<float> fricative(44100*2);juce::Random random(201102);juce::dsp::IIR::Filter<float> hp;hp.coefficients=juce::dsp::IIR::Coefficients<float>::makeHighPass(44100,3000);
    for(size_t i=0;i<fricative.size();++i){double t=i/44100.0;float white=(random.nextFloat()-.5f)*std::sqrt(12.0f);float value=hp.processSample(white);fricative[i]=.00001f*white+(t>.7&&t<.88?.015f*value*(float)std::sin(juce::MathConstants<double>::pi*(t-.7)/.18):0);}
    auto sBefore=renderRustle(fricative,44100,false),sAfter=renderRustle(fricative,44100,true);double sChange=differenceDb(bandLevel(sAfter.samples,44100,.7,.88,true),bandLevel(sBefore.samples,44100,.7,.88,true));bool sProtected=sChange>-2.0;passed&=sProtected;
    // A shaped sibilant can share the friction ridge and falling upper tail.
    // A confident speech decision must still protect this single onset.
    juce::dsp::IIR::Filter<float> shaping;shaping.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(44100,6500);for(auto& sample:fricative)sample=shaping.processSample(sample);
    auto shapedBefore=renderRustle(fricative,44100,false),shapedAfter=renderRustle(fricative,44100,true);double shapedChange=differenceDb(bandLevel(shapedAfter.samples,44100,.7,.88,true),bandLevel(shapedBefore.samples,44100,.7,.88,true));bool shapedProtected=shapedChange>-2.0;passed&=shapedProtected;
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("runs",rows);report->setProperty("clean_speech_relative_error",relativeError);report->setProperty("clean_speech_high_band_change_db",speechChange);report->setProperty("clean_speech_protected",protectedSpeech);report->setProperty("wideband_sibilant_change_db",sChange);report->setProperty("wideband_sibilant_protected",sProtected);report->setProperty("shaped_sibilant_change_db",shapedChange);report->setProperty("shaped_sibilant_protected",shapedProtected);return juce::var(report);
}
