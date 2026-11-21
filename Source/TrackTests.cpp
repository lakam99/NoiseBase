#include "Offline.h"

namespace {
NoiseProfile whiteProfile(double rate,float sigma){DenoiseEngine sizing;sizing.prepare(rate,1);NoiseProfile p;p.sampleRate=rate;p.fftSize=sizing.fftSize();p.quietSeconds=1;p.power.assign((size_t)p.fftSize/2+1,.375f*p.fftSize*sigma*sigma);return p;}
double level(const std::vector<float>& samples,double rate,double begin,double end){double sum=0;int a=(int)(begin*rate),b=std::min((int)samples.size(),(int)(end*rate));for(int i=a;i<b;++i)sum+=samples[(size_t)i]*samples[(size_t)i];return std::sqrt(sum/std::max(1,b-a));}
std::vector<float> readAudio(const juce::File& file){juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));if(!reader)throw std::runtime_error("Track test WAV could not be opened.");juce::AudioBuffer<float> data(1,(int)reader->lengthInSamples);reader->read(&data,0,data.getNumSamples(),0,true,false);return {data.getReadPointer(0),data.getReadPointer(0)+data.getNumSamples()};}
}
juce::var runTrackTests(){
    bool passed=true;juce::Array<juce::var> rows;
    for(double rate:{44100.0,48000.0,96000.0}){
        auto a=whiteProfile(rate,.002f),b=whiteProfile(rate,.020f),c=whiteProfile(rate,.001f);int hop=a.fftSize/4;
        NoiseTimelineBuilder builder(rate,a.fftSize,hop);
        for(int64_t position=0;position<(int64_t)(rate*12);position+=hop){double t=position/rate;auto power=t<4?a.power:t<8?b.power:c.power;bool quiet=!(t>1&&t<2)&&!(t>5&&t<7)&&!(t>9&&t<10);if(!quiet)for(int k=1;k<10;++k)power[(size_t)k]+=20;builder.add(position,power,quiet);}
        auto regions=builder.finish((int64_t)(rate*12),a);
        bool boundaries=regions.size()==3&&std::abs(regions[1].start/rate-4)<.08&&std::abs(regions[2].start/rate-8)<.08;
        DenoiseEngine automatic,hold,speechOnly;automatic.prepare(rate,1);hold.prepare(rate,1);speechOnly.prepare(rate,1);automatic.setProfile(a);hold.setProfile(a);speechOnly.setProfile(a);
        juce::Random random(473);std::vector<float> autoOutput,holdOutput;int total=(int)(rate*6)+automatic.latency();autoOutput.reserve((size_t)total);holdOutput.reserve((size_t)total);
        for(int start=0;start<total;start+=137){int count=std::min(137,total-start);juce::AudioBuffer<float> input(1,count);double t=start/rate;float sigma=t<2?.002f:t<4?.020f:.001f;bool speaking=t>=2&&t<2.6;
            for(int i=0;i<count;++i){float sample=sigma*(random.nextFloat()-.5f)*std::sqrt(12.0f);if(speaking)sample+=.08f*std::sin((float)(juce::MathConstants<double>::twoPi*1500*(start+i)/rate));input.setSample(0,i,sample);}
            juce::AudioBuffer<float> fixed(input);automatic.process(input,speaking?1.0f:0.0f,18,.8f,1,0,true,false);hold.process(fixed,speaking?1.0f:0.0f,18,.8f,1,0,false,false);
            for(int i=0;i<count;++i){autoOutput.push_back(input.getSample(0,i));holdOutput.push_back(fixed.getSample(0,i));}
            juce::AudioBuffer<float> voiced(1,count);for(int i=0;i<count;++i)voiced.setSample(0,i,.2f*std::sin((float)(juce::MathConstants<double>::twoPi*500*(start+i)/rate)));speechOnly.process(voiced,1,18,.8f,1,0,true,false);
        }
        double delay=automatic.latency()/rate;
        double improvement=20*std::log10(std::max(1e-12,level(autoOutput,rate,3.4+delay,3.9+delay)/level(holdOutput,rate,3.4+delay,3.9+delay)));
        bool ok=boundaries&&improvement<-3&&automatic.automaticChanges()>0&&hold.automaticChanges()==0&&speechOnly.automaticChanges()==0;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("regions",(int)regions.size());row->setProperty("edit_boundaries_within_80ms",boundaries);row->setProperty("live_louder_background_improvement_db",improvement);row->setProperty("automatic_floor_changes",automatic.automaticChanges());row->setProperty("hold_keeps_floor",hold.automaticChanges()==0);row->setProperty("speech_does_not_trigger_relearning",speechOnly.automaticChanges()==0);row->setProperty("passed",ok);rows.add(juce::var(row));passed&=ok;
    }
    // Exercise the actual whole-file VAD scan and WAV renderer with a later
    // background 20 dB louder than the beginning, then a quieter third clip.
    auto source=juce::File::createTempFile(".wav"),local=juce::File::createTempFile(".wav"),fixed=juce::File::createTempFile(".wav"),residual=juce::File::createTempFile(".wav");
    constexpr double rate=48000;juce::AudioBuffer<float> input(1,48000*12);juce::Random random(474);
    for(int i=0;i<input.getNumSamples();++i){float sigma=i<48000*4?.002f:i<48000*8?.020f:.001f;input.setSample(0,i,sigma*(random.nextFloat()-.5f)*std::sqrt(12.0f));}
    {juce::WavAudioFormat format;std::unique_ptr<juce::OutputStream> stream(source.createOutputStream());auto writer=format.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(24));if(!writer||!writer->writeFromAudioSampleBuffer(input,0,input.getNumSamples()))throw std::runtime_error("Could not create joined-track fixture.");}
    auto analysis=analyseRecording(source);exportRecording(analysis,local,18,.8f,1,0);exportRecording(analysis,fixed,18,.8f,1,0,nullptr,nullptr,false,false);exportRecording(analysis,residual,18,.8f,1,0,nullptr,nullptr,true);
    auto clean=readAudio(local),old=readAudio(fixed),removed=readAudio(residual),original=readAudio(source);
    double improvement=20*std::log10(std::max(1e-12,level(clean,rate,5,7)/level(old,rate,5,7)));float error=0;
    bool timing=clean.size()==original.size()&&removed.size()==original.size();if(timing)for(size_t i=0;i<clean.size();++i)error=std::max(error,std::abs(clean[i]+removed[i]-original[i]));
    bool filePassed=analysis.regions&&analysis.regions->size()>=3&&improvement<-3&&timing&&error<.000001f;
    auto* file=new juce::DynamicObject;file->setProperty("passed",filePassed);file->setProperty("regions",analysis.regions?(int)analysis.regions->size():0);file->setProperty("later_background_improvement_db",improvement);file->setProperty("sample_count_preserved",timing);file->setProperty("clean_plus_noise_error",error);
    source.deleteFile();local.deleteFile();fixed.deleteFile();residual.deleteFile();passed&=filePassed;
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("runs",rows);report->setProperty("whole_file_scan_and_export",juce::var(file));return juce::var(report);
}
