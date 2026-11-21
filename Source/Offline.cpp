#include "Offline.h"
#include "ReferenceLearner.h"

namespace {
std::unique_ptr<juce::AudioFormatReader> openReader(const juce::File& file) {
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if(!reader) throw std::runtime_error("Unable to open this audio file.");
    if(reader->numChannels<1 || reader->numChannels>2) throw std::runtime_error("Use a mono or stereo audio file.");
    if(reader->sampleRate<8000 || reader->lengthInSamples<1) throw std::runtime_error("The audio file is empty or has an unsupported sample rate.");
    return reader;
}
void checkCancel(std::atomic<bool>* cancel) { if(cancel && cancel->load()) throw std::runtime_error("Cancelled."); }
}

FileAnalysis analyseRecording(const juce::File& file,std::atomic<float>* progress,std::atomic<bool>* cancel) {
    checkCancel(cancel);
    auto reader=openReader(file);
    FileAnalysis result; result.source=file; result.frames=reader->lengthInSamples;
    result.sampleRate=reader->sampleRate; result.channels=(int)reader->numChannels;
    result.bits=(int)reader->bitsPerSample; result.floating=reader->usesFloatingPointData;
    checkCancel(cancel);VoiceDetector voice;
    juce::AudioBuffer<float> block(result.channels,8192);
    juce::dsp::IIR::Filter<float> lowpass;
    lowpass.coefficients=juce::dsp::IIR::Coefficients<float>::makeLowPass(result.sampleRate,std::min(6500.0,result.sampleRate*.4));
    std::array<float,512> frame{}; int fill=0; double phase=0;
    for(int64_t pos=0;pos<result.frames;pos+=8192) {
        checkCancel(cancel);
        int count=(int)std::min<int64_t>(8192,result.frames-pos);
        block.clear(); reader->read(&block,0,count,pos,true,true);
        int channel=result.channels>1 && block.getRMSLevel(1,0,count)>block.getRMSLevel(0,0,count)?1:0;
        for(int i=0;i<count;++i) {
            float value=lowpass.processSample(block.getSample(channel,i));
            if(!std::isfinite(value)) throw std::runtime_error("The input contains non-finite samples.");
            phase+=16000/result.sampleRate;
            while(phase>=1) {
                phase-=1; frame[(size_t)fill++]=value;
                if(fill==512) { result.voice.push_back(voice.analyse(frame.data())); fill=0; }
            }
        }
        if(progress) progress->store(.60f*(float)((double)pos/result.frames));
    }
    if(fill) { std::fill(frame.begin()+fill,frame.end(),0); result.voice.push_back(voice.analyse(frame.data())); }
    if(result.voice.empty()) result.voice.push_back(1);
    std::vector<bool> quiet(result.voice.size(),false);
    for(size_t i=0;i<quiet.size();++i) {
        bool ok=true;
        for(int offset=-7;offset<=7;++offset) {
            int k=(int)i+offset;
            if(k>=0 && k<(int)quiet.size() && result.voice[(size_t)k]>.25f) {ok=false; break;}
        }
        quiet[i]=ok;
    }
    int order=juce::jlimit(8,13,(int)std::round(std::log2(result.sampleRate*.046)));
    int size=1<<order, hop=size/4, bins=size/2+1;
    NoiseTimelineBuilder timeline(result.sampleRate,size,hop);
    juce::dsp::FFT fft(order);
    std::vector<float> window((size_t)size),data((size_t)size*2),power((size_t)bins),smooth((size_t)bins,0),minimum((size_t)bins,1e30f);
    std::vector<double> sum((size_t)bins,0);
    std::array<std::vector<float>,2> channelPower,channelSpectrum;
    std::array<std::vector<double>,2> channelSum;
    for(int c=0;c<2;++c){channelPower[(size_t)c].resize((size_t)bins);channelSpectrum[(size_t)c].resize((size_t)size*2);channelSum[(size_t)c].assign((size_t)bins,0);}
    std::vector<double> crossSum((size_t)bins,0);
    double levelSum=0,levelSquared=0;
    for(int i=0;i<size;++i) window[(size_t)i]=.5f-.5f*std::cos(juce::MathConstants<float>::twoPi*i/(float)size);
    juce::AudioBuffer<float> part(result.channels,size);
    int used=0, seen=0; double quietEnergy=0;
    for(int64_t pos=0;pos+size<=result.frames;pos+=hop) {
        checkCancel(cancel);
        part.clear(); reader->read(&part,0,size,pos,true,true);
        std::fill(power.begin(),power.end(),0);
        for(int c=0;c<result.channels;++c) {
            std::fill(data.begin(),data.end(),0);
            for(int i=0;i<size;++i) data[(size_t)i]=part.getSample(c,i)*window[(size_t)i];
            fft.performRealOnlyForwardTransform(data.data(),true);
            std::copy(data.begin(),data.end(),channelSpectrum[(size_t)c].begin());
            for(int k=0;k<bins;++k){float p=data[(size_t)k*2]*data[(size_t)k*2]+data[(size_t)k*2+1]*data[(size_t)k*2+1];channelPower[(size_t)c][(size_t)k]=p;power[(size_t)k]=std::max(power[(size_t)k],p);}
        }
        ++seen;
        double energy=0;
        for(int k=0;k<bins;++k) {
            smooth[(size_t)k]=.9f*smooth[(size_t)k]+.1f*power[(size_t)k];
            if(seen>12) minimum[(size_t)k]=std::min(minimum[(size_t)k],smooth[(size_t)k]);
            energy+=power[(size_t)k];
        }
        bool ok=pos>.10*result.sampleRate && pos+size<result.frames-.10*result.sampleRate;
        int a=(int)(pos/result.sampleRate/.032), b=(int)((pos+size)/result.sampleRate/.032)+1;
        for(int i=a;i<b && i<(int)quiet.size();++i) if(!quiet[(size_t)i]) ok=false;
        // The local timeline sees the whole track, including backgrounds that
        // are louder than the first scene. Keep the procedural reference's
        // existing transient rejection separate from dialogue processing.
        timeline.add(pos,power,ok);
        if(used>5 && energy>quietEnergy/used*4) ok=false;
        if(ok) { ++used; quietEnergy+=energy;double db=10*std::log10(std::max(energy,1e-20));levelSum+=db;levelSquared+=db*db;
            for(int k=0;k<bins;++k){sum[(size_t)k]+=power[(size_t)k];for(int c=0;c<2;++c)channelSum[(size_t)c][(size_t)k]+=channelPower[(size_t)std::min(c,result.channels-1)][(size_t)k];
                auto& left=channelSpectrum[0];auto& right=channelSpectrum[(size_t)(result.channels-1)];crossSum[(size_t)k]+=left[(size_t)k*2]*right[(size_t)k*2]+left[(size_t)k*2+1]*right[(size_t)k*2+1];}
        }
        if(progress) progress->store(.60f+.40f*(float)((double)pos/result.frames));
    }
    result.profile.fftSize=size; result.profile.sampleRate=result.sampleRate;
    result.profile.quietSeconds=used*hop/result.sampleRate;
    result.profile.estimated=result.profile.quietSeconds<.35;
    result.profile.power.resize((size_t)bins);
    for(int k=0;k<bins;++k) {
        float p=result.profile.estimated ? (minimum[(size_t)k]<1e29f?minimum[(size_t)k]*1.5f:1e-16f) : (float)(sum[(size_t)k]/std::max(used,1));
        result.profile.power[(size_t)k]=std::max(p,1e-16f);
    }
    auto old=result.profile.power;
    for(int k=1;k<bins-1;++k) result.profile.power[(size_t)k]=.25f*old[(size_t)k-1]+.5f*old[(size_t)k]+.25f*old[(size_t)k+1];
    result.profile.sourceChannels=result.channels;
    result.profile.variationDb=(float)juce::jlimit(0.0,3.0,std::sqrt(std::max(0.0,levelSquared/std::max(used,1)-std::pow(levelSum/std::max(used,1),2))));
    result.profile.ambienceCorrelation.resize((size_t)bins);
    for(int c=0;c<2;++c){result.profile.ambiencePower[(size_t)c].resize((size_t)bins);for(int k=0;k<bins;++k)result.profile.ambiencePower[(size_t)c][(size_t)k]=(float)(channelSum[(size_t)c][(size_t)k]/std::max(used,1));}
    for(int k=0;k<bins;++k)result.profile.ambienceCorrelation[(size_t)k]=(float)juce::jlimit(-1.0,1.0,crossSum[(size_t)k]/std::max(1e-30,std::sqrt(channelSum[0][(size_t)k]*channelSum[1][(size_t)k])));
    result.regions=std::make_shared<const NoiseRegions>(timeline.finish(result.frames,result.profile));
    if(progress) progress->store(1);
    return result;
}

void exportRecording(const FileAnalysis& analysis,const juce::File& destination,float reduction,float preserve,float mix,float outputDb,std::atomic<float>* progress,std::atomic<bool>* cancel,bool noiseOnly,bool followTrack) {
    if(destination==analysis.source) throw std::runtime_error("Choose a new file; the source cannot be overwritten.");
    auto reader=openReader(analysis.source);
    juce::TemporaryFile temp(destination);
    std::unique_ptr<juce::OutputStream> stream(temp.getFile().createOutputStream());
    if(!stream) throw std::runtime_error("Cannot create the output file.");
    juce::WavAudioFormat wav;
    auto options=juce::AudioFormatWriterOptions{}.withSampleRate(analysis.sampleRate).withNumChannels(analysis.channels)
        .withBitsPerSample(analysis.floating?32:juce::jlimit(16,32,analysis.bits))
        .withSampleFormat(analysis.floating?juce::AudioFormatWriterOptions::SampleFormat::floatingPoint:juce::AudioFormatWriterOptions::SampleFormat::integral);
    for(int i=0;i<reader->metadataValues.size();++i)options=options.withMetadata(reader->metadataValues.getAllKeys()[i],reader->metadataValues.getAllValues()[i]);
    auto writer=wav.createWriterFor(stream,options);
    if(!writer) throw std::runtime_error("Cannot create a WAV writer.");
    DenoiseEngine engine; engine.prepare(analysis.sampleRate,analysis.channels);
    const auto* regions=followTrack&&analysis.regions&&!analysis.regions->empty()?analysis.regions.get():nullptr;
    int region=0;engine.setProfile(regions?regions->front().profile:analysis.profile);
    int latency=engine.latency();
    juce::AudioBuffer<float> block(analysis.channels,512);
    // Prime parameter ramps with silence before the file starts.
    for(int p=0;p<latency;p+=512) { block.clear(); engine.process(block,0,reduction,preserve,mix,outputDb,false,false,noiseOnly); }
    int64_t rendered=0, written=0;
    while(rendered<analysis.frames+latency) {
        checkCancel(cancel);
        int count=(int)std::min<int64_t>(512,analysis.frames+latency-rendered);
        if(regions){int next=noiseRegionAt(*regions,std::max<int64_t>(0,rendered-engine.fftSize()/2));if(next!=region){region=next;engine.setProfile((*regions)[(size_t)region].profile,true);}
            if(region+1<(int)regions->size())count=(int)std::min<int64_t>(count,(*regions)[(size_t)(region+1)].start+engine.fftSize()/2-rendered);}
        block.setSize(analysis.channels,count,false,false,true); block.clear();
        if(rendered<analysis.frames) reader->read(&block,0,(int)std::min<int64_t>(count,analysis.frames-rendered),rendered,true,true);
        int idx=juce::jlimit(0,(int)analysis.voice.size()-1,(int)(rendered/analysis.sampleRate/.032));
        engine.process(block,analysis.voice[(size_t)idx],reduction,preserve,mix,outputDb,false,false,noiseOnly);
        int skip=(int)std::max<int64_t>(0,latency-rendered);
        if(skip<count) {
            int available=(int)std::min<int64_t>(count-skip,analysis.frames-written);
            if(available>0 && !writer->writeFromAudioSampleBuffer(block,skip,available)) throw std::runtime_error("Failed while writing the output.");
            written+=available;
        }
        rendered+=count;
        if(progress) progress->store((float)((double)written/analysis.frames));
    }
    writer.reset();
    if(written!=analysis.frames) throw std::runtime_error("Timing verification failed; output was not committed.");
    if(!temp.overwriteTargetFileWithTemporary()) throw std::runtime_error("Cannot move the rendered file into place.");
    if(progress) progress->store(1);
}

juce::var runEngineTests() {
    auto* report=new juce::DynamicObject;
    juce::Array<juce::var> results;
    bool passed=true;
    for(double rate:{44100.0,48000.0,96000.0}) {
        DenoiseEngine engine; engine.prepare(rate,2);
        int latency=engine.latency(), length=(int)rate;
        std::vector<float> input((size_t)length),rendered((size_t)length+latency+1024,0);
        juce::Random random(934);
        for(auto& x:input) x=(random.nextFloat()-.5f)*.2f;
        int total=length+latency;
        for(int p=0;p<total;p+=137) {
            int count=std::min(137,total-p); juce::AudioBuffer<float> block(2,count);
            for(int i=0;i<count;++i) { float v=p+i<length?input[(size_t)p+i]:0; block.setSample(0,i,v); block.setSample(1,i,-v); }
            engine.process(block,1,0,1,1,0,false,false);
            for(int i=0;i<count;++i) rendered[(size_t)p+i]=block.getSample(0,i);
        }
        float error=0;
        for(int i=0;i<length;++i) error=std::max(error,std::abs(rendered[(size_t)i+latency]-input[(size_t)i]));
        engine.prepare(rate,2);
        std::fill(rendered.begin(),rendered.end(),0);
        float stereoError=0;
        for(int p=0;p<total;p+=137) {
            int count=std::min(137,total-p);juce::AudioBuffer<float> block(2,count);
            for(int i=0;i<count;++i){float v=p+i<length?input[(size_t)p+i]:0;block.setSample(0,i,v);block.setSample(1,i,-v);}
            engine.process(block,0,18,1,1,6,true,true);
            for(int i=0;i<count;++i){rendered[(size_t)p+i]=block.getSample(0,i);stereoError=std::max(stereoError,std::abs(block.getSample(0,i)+block.getSample(1,i)));}
        }
        float bypassError=0;for(int i=0;i<length;++i)bypassError=std::max(bypassError,std::abs(rendered[(size_t)i+latency]-input[(size_t)i]));
        auto* row=new juce::DynamicObject; row->setProperty("sample_rate",rate);row->setProperty("latency_samples",latency);row->setProperty("unity_max_error",error);
        row->setProperty("bypass_max_error_with_output_trim",bypassError);row->setProperty("stereo_anti_phase_error",stereoError);
        bool ok=error<.00002f&&bypassError<.00002f&&stereoError<.00002f;
        row->setProperty("passed",ok); passed &= ok; results.add(juce::var(row));
    }
    auto noiseTests=runNoiseOutputTests();passed &= (bool)noiseTests["passed"];
    auto proceduralTests=runProceduralTests();passed &= (bool)proceduralTests["passed"];
    auto trackTests=runTrackTests();passed &= (bool)trackTests["passed"];report->setProperty("whole_track",trackTests);
    auto hostTests=runHostSafetyTests();passed &= (bool)hostTests["passed"];report->setProperty("host_safety",hostTests);
    auto referenceTests=runReferenceTests();passed &= (bool)referenceTests["passed"];report->setProperty("external_reference",referenceTests);
    report->setProperty("procedural",proceduralTests);
    report->setProperty("passed",passed); report->setProperty("unity_latency_tests",results);report->setProperty("noise_only",noiseTests); return juce::var(report);
}

void exportAmbience(const NoiseProfile& profile,const juce::File& destination,double seconds,float mix,float outputDb,std::atomic<float>* progress,std::atomic<bool>* cancel,uint64_t seed){
    if(!validAmbienceProfile(profile))throw std::runtime_error("Load a clip containing at least 0.35 seconds of confidently quiet background before generating ambience.");
    if(!std::isfinite(seconds)||seconds<1||seconds>3600)throw std::runtime_error("Choose a duration from 1 to 3600 seconds.");
    int channels=profile.sourceChannels;double rate=profile.sampleRate;int64_t length=(int64_t)std::llround(seconds*rate);int fade=(int)std::min<int64_t>((int64_t)(rate*.75),length/4);
    ProceduralNoise generator;generator.prepare(rate,channels,seed);generator.setProfile(profile);float sample[2]{};
    for(int i=0;i<(int)(rate*2);++i){if(i%8192==0)checkCancel(cancel);generator.next(sample);}
    juce::AudioBuffer<float> preceding(channels,fade),block(channels,512);
    for(int i=0;i<fade;++i){generator.next(sample);for(int c=0;c<channels;++c)preceding.setSample(c,i,sample[c]);}
    juce::TemporaryFile temp(destination);std::unique_ptr<juce::OutputStream> stream(temp.getFile().createOutputStream());juce::WavAudioFormat wav;
    auto writer=wav.createWriterFor(stream,juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(channels).withBitsPerSample(24));
    if(!writer)throw std::runtime_error("Cannot create the procedural WAV.");
    float gain=juce::jlimit(0.0f,1.0f,mix)*juce::Decibels::decibelsToGain(outputDb);
    for(int64_t start=0;start<length;start+=512){checkCancel(cancel);int count=(int)std::min<int64_t>(512,length-start);
        for(int i=0;i<count;++i){generator.next(sample);int64_t tail=start+i-(length-fade);float angle=tail>=0?(float)(juce::MathConstants<double>::halfPi*tail/std::max(1,fade-1)):0;
            for(int c=0;c<channels;++c){float value=sample[c];if(tail>=0)value=value*std::cos(angle)+preceding.getSample(c,(int)tail)*std::sin(angle);block.setSample(c,i,value*gain);}}
        if(!writer->writeFromAudioSampleBuffer(block,0,count))throw std::runtime_error("Could not write the procedural WAV.");
        if(progress)progress->store((float)((double)(start+count)/length));
    }
    writer.reset();if(!temp.overwriteTargetFileWithTemporary())throw std::runtime_error("Could not commit the procedural WAV.");
}
