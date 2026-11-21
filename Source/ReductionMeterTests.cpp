#include "Offline.h"
#include "DenoiseEngine.h"

juce::var runReductionMeterTests(){
    juce::Array<juce::var> rows;bool passed=true;
    for(double rate:{44100.,48000.,96000.}){
        DenoiseEngine a,b;a.prepare(rate,2);b.prepare(rate,2);
        for(auto* engine:{&a,&b}){NoiseProfile p;p.sampleRate=rate;p.fftSize=engine->fftSize();p.quietSeconds=1;p.power.assign((size_t)p.fftSize/2+1,1e6f);if(!engine->setProfile(p))throw std::runtime_error("Meter fixture profile rejected");}
        juce::AudioBuffer<float> block(2,256),trimmed(2,256);juce::Random random(924);
        float silentPeak=0,trimDifference=0;double sourceEnergy=0,resultEnergy=0;
        for(int n=0;n<(int)rate;n+=256){block.clear();a.process(block,0,24,0,1,0,false,false);silentPeak=std::max(silentPeak,a.reductionMeter());}
        a.prepare(rate,2);NoiseProfile p;p.sampleRate=rate;p.fftSize=a.fftSize();p.quietSeconds=1;p.power.assign((size_t)p.fftSize/2+1,1e6f);a.setProfile(p);
        for(int n=0;n<(int)(rate*2);n+=256){
            for(int i=0;i<256;++i){float x=(random.nextFloat()-.5f)*.08f;block.setSample(0,i,x);block.setSample(1,i,-x);}
            trimmed.makeCopyOf(block);double before=0;for(int i=0;i<256;++i)before+=(double)block.getSample(0,i)*block.getSample(0,i);
            a.process(block,0,12,0,1,0,false,false);b.process(trimmed,0,12,0,1,6,false,false);
            if(n>(int)rate){sourceEnergy+=before;for(int i=0;i<256;++i)resultEnergy+=(double)block.getSample(0,i)*block.getSample(0,i);trimDifference=std::max(trimDifference,std::abs(a.reductionMeter()-b.reductionMeter()));}
        }
        float reduced=a.reductionMeter();double measured=10*std::log10(sourceEnergy/resultEnergy);
        for(int n=0;n<(int)rate;n+=256){for(int i=0;i<256;++i){float x=(random.nextFloat()-.5f)*.08f;for(int c=0;c<2;++c)block.setSample(c,i,x);}a.process(block,0,24,0,1,0,false,true);}
        float bypass=a.reductionMeter();
        for(int n=0;n<(int)rate;n+=256){for(int i=0;i<256;++i)for(int c=0;c<2;++c)block.setSample(c,i,(random.nextFloat()-.5f)*.08f);a.process(block,0,24,0,0,0,false,false);}
        float dryMix=a.reductionMeter();
        for(int n=0;n<(int)rate;n+=256){block.clear();a.process(block,0,24,0,1,0,false,false);}
        float stopped=a.reductionMeter();
        bool ok=silentPeak==0&&stopped==0&&reduced>9&&reduced<15&&std::abs(reduced-measured)<1&&trimDifference<.001f&&bypass<.05f&&dryMix<.05f;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("silence_meter_db",silentPeak);row->setProperty("settled_meter_db",reduced);row->setProperty("measured_attenuation_db",measured);row->setProperty("output_trim_meter_difference_db",trimDifference);row->setProperty("bypass_meter_db",bypass);row->setProperty("dry_mix_meter_db",dryMix);row->setProperty("silence_after_signal_db",stopped);row->setProperty("passed",ok);rows.add(juce::var(row));passed&=ok;
    }
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("runs",rows);return juce::var(report);
}
