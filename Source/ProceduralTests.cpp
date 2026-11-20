#include "Offline.h"

juce::var runProceduralTests(){
    bool passed=true;juce::Array<juce::var> rows;
    for(double rate:{44100.0,48000.0,96000.0}){
        DenoiseEngine sizing;sizing.prepare(rate,2);NoiseProfile p;p.fftSize=sizing.fftSize();p.sampleRate=rate;p.sourceChannels=2;p.quietSeconds=2;
        int bins=p.fftSize/2+1;p.power.assign((size_t)bins,0);p.ambienceCorrelation.assign((size_t)bins,.65f);
        float sigma=.012f;
        for(int c=0;c<2;++c){p.ambiencePower[(size_t)c].resize((size_t)bins);for(int k=0;k<bins;++k)p.ambiencePower[(size_t)c][(size_t)k]=.375f*p.fftSize*sigma*sigma*(c==0?1:.25f);}
        p.power=p.ambiencePower[0];auto state=ambienceProfileState(p);NoiseProfile restored;
        bool roundtrip=readAmbienceProfile(state,restored)&&restored.ambiencePower==p.ambiencePower&&restored.ambienceCorrelation==p.ambienceCorrelation;
        auto malformed=state.createCopy();malformed.setProperty("data","broken",nullptr);NoiseProfile ignored;bool reject=!readAmbienceProfile(malformed,ignored);
        auto uncertain=p;uncertain.estimated=true;bool refusesEstimated=!validAmbienceProfile(uncertain);
        ProceduralNoise generator,repeat;generator.prepare(rate,2,98311);repeat.prepare(rate,2,98311);generator.setProfile(restored);repeat.setProfile(p);
        double left=0,right=0,cross=0;float repeatError=0,peak=0;bool finite=true;int samples=(int)(rate*12),warm=(int)rate;
        std::vector<float> firstSecond((size_t)(int)rate);double secondDifference=0;
        for(int i=0;i<samples;++i){float a[2]{},b[2]{};generator.next(a);repeat.next(b);for(int c=0;c<2;++c){finite&=std::isfinite(a[c]);repeatError=std::max(repeatError,std::abs(a[c]-b[c]));peak=std::max(peak,std::abs(a[c]));}
            if(i>=warm){left+=a[0]*a[0];right+=a[1]*a[1];cross+=a[0]*a[1];}
            if(i>=warm&&i<warm+(int)rate)firstSecond[(size_t)(i-warm)]=a[0];
            if(i>=warm+(int)rate&&i<warm+2*(int)rate){double d=a[0]-firstSecond[(size_t)(i-warm-(int)rate)];secondDifference+=d*d;}
        }
        double rms=std::sqrt(left/(samples-warm)),ratio=std::sqrt(right/left),coherence=cross/std::sqrt(left*right);
        bool loopVerified=true;
        if(rate==48000){
            auto file=juce::File::createTempFile(".wav");exportAmbience(p,file,3,1,0,nullptr,nullptr,7719);
            ProceduralNoise reference;reference.prepare(rate,2,7719);reference.setProfile(p);float expected[2]{};int fade=(int)(rate*.75);
            for(int i=0;i<(int)(rate*2)+fade;++i)reference.next(expected);float last[2]{expected[0],expected[1]};reference.next(expected);
            juce::AudioFormatManager formats;formats.registerBasicFormats();std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
            loopVerified=reader&&reader->lengthInSamples==144000&&reader->numChannels==2;
            if(reader){juce::AudioBuffer<float> edge(2,1);reader->read(&edge,0,1,0,true,true);for(int c=0;c<2;++c)loopVerified&=std::abs(edge.getSample(c,0)-expected[c])<.000001f;reader->read(&edge,0,1,143999,true,true);for(int c=0;c<2;++c)loopVerified&=std::abs(edge.getSample(c,0)-last[c])<.000001f;}
            reader.reset();file.deleteFile();
        }
        // Continuity is independent of caller block sizes: next() has no block reset.
        bool ok=roundtrip&&reject&&refusesEstimated&&finite&&repeatError==0&&std::abs(rms/sigma-1)<.08&&std::abs(ratio-.5)<.04&&std::abs(coherence-.65)<.05&&secondDifference/rate>sigma*sigma&&loopVerified;
        auto* row=new juce::DynamicObject;row->setProperty("sample_rate",rate);row->setProperty("profile_roundtrip",roundtrip);row->setProperty("rejects_malformed_profile",reject);row->setProperty("refuses_unreliable_floor",refusesEstimated);row->setProperty("generated_seconds",12);row->setProperty("reference_rms",sigma);row->setProperty("generated_rms",rms);row->setProperty("right_left_rms_ratio",ratio);row->setProperty("stereo_correlation",coherence);row->setProperty("repeat_seed_error",repeatError);row->setProperty("successive_seconds_differ",secondDifference/rate>sigma*sigma);if(rate==48000)row->setProperty("export_loop_boundary_matches_continuous_sequence",loopVerified);row->setProperty("peak",peak);row->setProperty("passed",ok);rows.add(juce::var(row));passed&=ok;
    }
    auto* report=new juce::DynamicObject;report->setProperty("passed",passed);report->setProperty("runs",rows);return juce::var(report);
}
