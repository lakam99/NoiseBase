#include "Editor.h"

namespace {
juce::Font labelFont(float size=12,bool bold=true){return juce::Font(juce::FontOptions("Arial",size,bold?juce::Font::bold:juce::Font::plain));}
void engraved(juce::Graphics& g,const juce::String& text,juce::Rectangle<float> r,juce::Colour ink,float size=12) {
    g.setFont(labelFont(size));g.setColour(ink);g.drawText(text,r,juce::Justification::centred,false);
}
}
HardwareLook::HardwareLook(){setColourScheme(juce::Desktop::getInstance().isDarkModeActive()?getDarkColourScheme():getLightColourScheme());}
juce::Colour HardwareLook::panel() const {return skin==0?juce::Colour(0xffd4cbb6):skin==1?juce::Colour(0xff282b2c):juce::Colour(0xffb9a17b);}
juce::Colour HardwareLook::ink() const {return skin==1?juce::Colour(0xffd6d2bd):juce::Colour(0xff292a25);}
juce::Colour HardwareLook::accent() const {return skin==1?juce::Colour(0xffc84e3b):juce::Colour(0xff7b382a);}
void HardwareLook::drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float position,float start,float end,juce::Slider& slider) {
    auto area=juce::Rectangle<float>((float)x,(float)y,(float)w,(float)h).reduced(14);
    float radius=std::min(area.getWidth(),area.getHeight())*.5f,cx=area.getCentreX(),cy=area.getCentreY();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    if(w>90)for(int i=0;i<=12;++i){float a=start+(end-start)*i/12;g.setColour(ink().withAlpha(.65f));g.drawLine(cx+std::sin(a)*(radius+5),cy-std::cos(a)*(radius+5),cx+std::sin(a)*(radius+(i%3==0?11:8)),cy-std::cos(a)*(radius+(i%3==0?11:8)),i%3==0?1.3f:.7f);}
    g.drawImage(RasterAssets::get().knob,juce::Rectangle<float>{cx-radius,cy-radius,radius*2,radius*2},juce::RectanglePlacement::stretchToFit);
    // Keep the photographed highlights fixed; only the live index moves.
    float angle=start+position*(end-start),inner=radius*.81f;
    g.setColour(juce::Colours::black.withAlpha(.60f));g.drawLine(cx+std::sin(angle)*inner*.42f+1,cy-std::cos(angle)*inner*.42f+1,cx+std::sin(angle)*inner*.88f+1,cy-std::cos(angle)*inner*.88f+1,3.3f);
    g.setColour(juce::Colour(0xffeee7cb));g.drawLine(cx+std::sin(angle)*inner*.42f,cy-std::cos(angle)*inner*.42f,cx+std::sin(angle)*inner*.88f,cy-std::cos(angle)*inner*.88f,2.8f);
    if(w<=90)for(int i=0;i<3;++i){float a=start+(end-start)*i*.5f;g.setColour(ink().withAlpha(i==(int)std::round(slider.getValue())?.9f:.45f));g.drawLine(cx+std::sin(a)*(radius+3),cy-std::cos(a)*(radius+3),cx+std::sin(a)*(radius+7),cy-std::cos(a)*(radius+7),1.4f);}
}
void HardwareLook::drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool,bool) {
    auto r=b.getLocalBounds().toFloat();auto art=r.removeFromLeft(43).reduced(5,0);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(RasterAssets::get().switches[b.getToggleState()?0:1],art,juce::RectanglePlacement::stretchToFit);
    bool automatic=b.getButtonText().contains("AUTO"),noise=b.getButtonText()=="NOISE BASE",procedural=b.getButtonText()=="PROCEDURAL",reference=b.getButtonText()=="REFERENCE";g.setFont(labelFont(10));
    auto text=r.reduced(4,1);auto top=text.removeFromTop(15);
    g.setColour(ink().withAlpha(b.getToggleState()?1.0f:.40f));g.drawText(automatic?"AUTO":noise?"NOISE BASE":procedural?"PROCEDURAL":reference?"SIDECHAIN":"BYPASS",top,juce::Justification::centredLeft);
    g.setColour(ink().withAlpha(b.getToggleState()?.40f:1.0f));g.drawText(automatic?"HOLD":noise?"CLEAN":procedural?"REMOVED":reference?"CLIP":"IN",text,juce::Justification::centredLeft);
}
void HardwareLook::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool,bool down) {
    auto r=b.getLocalBounds().toFloat().reduced(1);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(b.isEnabled()?1.0f:.48f);
    if(b.getComponentID().startsWith("transport-")) {
        int index=b.getComponentID()=="transport-play"?0:b.getComponentID()=="transport-pause"?1:b.getComponentID()=="transport-stop"?2:b.getComponentID()=="transport-loop"?3:4;
        g.drawImage(RasterAssets::get().transport[(size_t)index],r.translated(0,down?1.0f:0.0f),juce::RectanglePlacement::centred);
        if(index==3&&b.getToggleState())g.drawImage(RasterAssets::get().lamps[1],juce::Rectangle<float>{r.getRight()-9,r.getBottom()-9,7,7},juce::RectanglePlacement::stretchToFit);
        g.setOpacity(1);return;
    }
    g.drawImage(RasterAssets::get().buttons[down?1:0],r,juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1);
}
void HardwareLook::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {
    if(b.getComponentID().startsWith("transport-"))return;
    g.setFont(labelFont(11));g.setColour(juce::Colours::black.withAlpha(.85f));g.drawText(b.getButtonText(),b.getLocalBounds().translated(0,1),juce::Justification::centred);
    g.setColour(juce::Colour(0xffeee8d7).withAlpha(b.isEnabled()?1.0f:.55f));g.drawText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred);
}

NoiseBaseEditor::NoiseBaseEditor(NoiseBaseProcessor& p):AudioProcessorEditor(p),processor(p) {
    setLookAndFeel(&look);setOpaque(true);juce::Desktop::getInstance().addDarkModeSettingListener(this);
    for(auto* control:{&reduction,&preserve,&mix,&output,&skin}) {control->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);control->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);control->setRotaryParameters(juce::MathConstants<float>::pi*1.23f,juce::MathConstants<float>::pi*2.77f,true);addAndMakeVisible(control);}
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"reduction",reduction));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"preserve",preserve));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"mix",mix));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"output",output));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"skin",skin));
    reduction.setComponentID("reduction");preserve.setComponentID("preserve");mix.setComponentID("mix");output.setComponentID("output");
    // All changing control values live in the fluorescent display.
    reduction.textFromValueFunction=[](double v){return juce::String(v,1)+" dB";};output.textFromValueFunction=[](double v){return juce::String(v,1)+" dB";};
    preserve.textFromValueFunction=[](double v){return juce::String(v,0)+" %";};mix.textFromValueFunction=[](double v){return juce::String(v,0)+" % clean";};
    for(auto* c:{(juce::Component*)&automatic,(juce::Component*)&bypass,(juce::Component*)&noiseOnly,(juce::Component*)&procedural,(juce::Component*)&reset})addAndMakeVisible(c);
    autoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"auto",automatic);
    bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"bypass",bypass);
    noiseAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"noiseOnly",noiseOnly);
    proceduralAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"procedural",procedural);
    if(!processor.standaloneMode.load()){addAndMakeVisible(reference);referenceAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"externalReference",reference);}
    reference.setTooltip("SIDECHAIN learns ambience from another DAW track. Select NOISE BASE + PROCEDURAL to generate it here; HOLD freezes the reference. CLIP uses LOAD CLIP instead.");
    noiseOnly.setTooltip("Select noise-base output or cleaned recording. Bypass restores the original.");
    procedural.setTooltip("PROCEDURAL generates continuous fresh ambience from the selected clip or sidechain reference. REMOVED monitors the denoiser residual, which can include speech.");
    reset.onClick=[this]{processor.learnTrack();};
    reset.setTooltip("Re-analyze the whole loaded clip, or restart incoming analysis for a live sidechain/DAW track.");
    automatic.setTooltip("Automatically follow background changes throughout the track. HOLD freezes the current noise floor.");
    for(auto* c:{&load,&scan,&save})addAndMakeVisible(c);
    load.setButtonText("LOAD CLIP");load.setTooltip("Learn a procedural profile from a mono/stereo reference clip; saved profiles work without the original file");
    load.onClick=[this]{openFile();};scan.onClick=[this]{processor.scanRecording();};save.onClick=[this]{exportFile();};
    if(processor.standaloneMode.load()) {
        for(auto* c:{&play,&stop,&loop,&beginning,&devices})addAndMakeVisible(c);
        play.setComponentID("transport-play");stop.setComponentID("transport-stop");loop.setComponentID("transport-loop");beginning.setComponentID("transport-start");
        play.setTitle("Play");stop.setTitle("Stop and rewind");loop.setTitle("Loop playback");beginning.setTitle("Return to beginning");
        play.setTooltip("Play / pause");stop.setTooltip("Stop and return to the beginning");loop.setTooltip("Repeat the whole recording");beginning.setTooltip("Return to the beginning, preserving play or pause");
        loop.setClickingTogglesState(true);loop.setToggleState(processor.isLooping(),juce::dontSendNotification);
        addAndMakeVisible(updates);updates.onClick=[this]{if(onUpdates)onUpdates();};
        play.onClick=[this]{processor.togglePlayback();};stop.onClick=[this]{processor.stopPlayback();};
        loop.onClick=[this]{processor.setLooping(loop.getToggleState());};beginning.onClick=[this]{processor.returnToStart();};
        devices.onClick=[this]{if(onDevices)onDevices();};
    }
    reduction.setTooltip("Maximum denoiser reduction; procedural ambience is independent of reduction");preserve.setTooltip("Protect quieter speech detail in the denoiser");mix.setTooltip("Dry / clean blend; noise-base output level in procedural mode");output.setTooltip("Output trim, -12 to +12 dB");skin.setTooltip("Ivory / Graphite / Walnut faceplates");
    setSize(960,668);timerCallback();startTimerHz(30);
}
NoiseBaseEditor::~NoiseBaseEditor(){stopTimer();if(auto* desktop=juce::Desktop::getInstanceWithoutCreating())desktop->removeDarkModeSettingListener(this);setLookAndFeel(nullptr);}
void NoiseBaseEditor::darkModeSettingChanged(){look.setColourScheme(juce::Desktop::getInstance().isDarkModeActive()?HardwareLook::getDarkColourScheme():HardwareLook::getLightColourScheme());sendLookAndFeelChange();repaint();}
void NoiseBaseEditor::drawMeter(juce::Graphics& g,juce::Rectangle<float> r,const juce::String& label,float value,bool reductionMeter) {
    const auto& art=RasterAssets::get();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(art.meter,r,juce::RectanglePlacement::stretchToFit);
    auto p=art.meterPaper;
    juce::Rectangle<float> face{r.getX()+p.getX()*r.getWidth(),r.getY()+p.getY()*r.getHeight(),p.getWidth()*r.getWidth(),p.getHeight()*r.getHeight()};
    float cx=face.getCentreX(),cy=face.getBottom()-7;
    float radius=juce::jlimit(42.0f,84.0f,cy-face.getY()-21);
    for(int i=0;i<=24;++i){
        float a=-1.02f+2.04f*i/24;bool major=i%4==0;float inner=radius-(major?10.0f:5.0f);
        g.setColour(i>20?juce::Colour(0xff943c26):juce::Colour(0xff39352b));
        g.drawLine(cx+std::sin(a)*inner,cy-std::cos(a)*inner,cx+std::sin(a)*radius,cy-std::cos(a)*radius,major?1.5f:.7f);
        if(major){juce::String number=reductionMeter?juce::String(i):juce::String(-48+i*2);g.setFont(labelFont(10));g.drawText(number,juce::Rectangle<float>{cx+std::sin(a)*(radius+13)-14,cy-std::cos(a)*(radius+13)-6,28,12},juce::Justification::centred);}
    }
    float normal=reductionMeter?juce::jlimit(0.0f,1.0f,value/24):juce::jlimit(0.0f,1.0f,(juce::Decibels::gainToDecibels(value,-60.0f)+48)/48);
    float a=-1.02f+normal*2.04f;
    g.setColour(juce::Colours::black.withAlpha(.20f));g.drawLine(cx+1,cy+1,cx+std::sin(a)*(radius-4)+1,cy-std::cos(a)*(radius-4)+1,2);
    g.setColour(juce::Colour(0xffa63822));g.drawLine(cx,cy,cx+std::sin(a)*(radius-4),cy-std::cos(a)*(radius-4),1.7f);
    g.drawImage(art.knob,juce::Rectangle<float>{cx-5,cy-5,10,10},juce::RectanglePlacement::stretchToFit);
    engraved(g,label,{r.getX()+12,r.getBottom()-17,r.getWidth()-24,14},juce::Colour(0xffe9dfc4),11);
}
void NoiseBaseEditor::paint(juce::Graphics& g) {
    const auto& art=RasterAssets::get();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(art.panels[(size_t)look.skin],getLocalBounds().toFloat(),juce::RectanglePlacement::stretchToFit);
    engraved(g,"N O I S E B A S E",{276,32,410,36},look.ink(),25);
    engraved(g,"DIALOGUE RESTORATION",{270,69,420,17},look.ink().withAlpha(.75f),9);
    engraved(g,"FACEPLATE",{65,22,110,16},look.ink(),9);
    engraved(g,"I  /  G  /  W",{65,94,110,11},look.ink(),10);
    if(!processor.standaloneMode.load())engraved(g,"REFERENCE",{756,26,140,16},look.ink(),10);
    drawMeter(g,{64,112,260,150},"INPUT  /  dBFS",inputNeedle,false);
    drawMeter(g,{350,112,260,150},"REDUCTION  /  dB",reductionNeedle,true);
    drawMeter(g,{636,112,260,150},"OUTPUT  /  dBFS",outputNeedle,false);
    const juce::StringArray names{"REDUCTION","VOICE DETAIL","MIX / LEVEL","OUTPUT TRIM"};
    const juce::StringArray units{"dB","PRESERVE %","%","dB"};
    for(int i=0;i<4;++i) {
        float x=87.0f+i*222;
        engraved(g,names[i],{x,305,120,20},look.ink(),11);
        // Scale lettering belongs to the faceplate, outside the knob's child
        // bounds, so the upper midpoint is not clipped by JUCE's child paint.
        std::vector<float> positions;
        juce::StringArray labels;
        if(i==0){positions={0,.25f,.5f,.75f,1};labels={"0","6","12","18","24"};}
        else if(i==3){positions={0,.25f,.5f,.75f,1};labels={"-12","-6","0","+6","+12"};}
        else if(i==2){positions={0,.5f,1};labels={"0","50","100"};}
        else {positions={0,.5f,1};labels={"0","50","100"};}
        float cx=147+i*222,cy=398;
        for(size_t mark=0;mark<positions.size();++mark){float a=juce::MathConstants<float>::pi*(1.23f+1.54f*positions[mark]);g.setColour(look.ink());g.setFont(labelFont(10));g.drawText(labels[(int)mark],juce::Rectangle<float>{cx+std::sin(a)*66-19,cy-std::cos(a)*66-6,38,12},juce::Justification::centred);}
        engraved(g,units[i],{x-8,459,136,16},look.ink().withAlpha(.85f),9);
    }
    bool learned=processor.quietSeconds.load()>=.35f;
    juce::String live=automatic.getToggleState()?(learned?"AUTO TRACK / FOLLOWING NOISE CHANGES":"AUTO TRACK / WAITING FOR QUIET BACKGROUND"):"HOLD / FIXED NOISE FLOOR";
    if(!processor.detectorAvailable.load())live="SPEECH DETECTOR  /  warming up";
    if(processor.detectorFailed.load())live="DETECTOR UNAVAILABLE  /  restore the runtime files";
    bool standalone=processor.standaloneMode.load();
    bool generated=processor.proceduralSelected();
    juce::String routing=bypass.getToggleState()?"ORIGINAL / BYPASS":noiseOnly.getToggleState()?(generated?"INFINITE NOISE":"REMOVED NOISE"):"CLEAN";
    if(generated)live=processor.ambienceReady.load()?"PROFILE READY / GENERATING FRESH AMBIENCE":"LOAD CLIP / NEED AT LEAST 0.35S OF QUIET BACKGROUND";
    drawVfdHousing(g,{64,485,832,92});
    juce::String controls=juce::StringArray{"IVORY","GRAPHITE","WALNUT"}[look.skin]+"  RED "+juce::String(reduction.getValue(),1)+" DB  VOICE "+juce::String(preserve.getValue(),1)+"%  MIX "+juce::String(mix.getValue(),1)+"%  OUT "+(output.getValue()>=0?"+":"")+juce::String(output.getValue(),1)+" DB";
    {
        if((standalone&&(!generated||processor.fileMode.load()))||processor.busy.load()||processor.ambienceReady.load())live=processor.getStatus();
        if(processor.busy.load())live+="  "+juce::String((int)(processor.jobProgress.load()*100))+"%";
        else if(updateBusy)live="CHECKING GITHUB RELEASES";
        else if(standalone&&(processor.fileMode.load()||processor.ambienceReady.load()))live=(processor.isPlaying()?"PLAY  | ":!generated&&processor.playbackPosition()>=processor.playbackDuration()-.001?"END   | ":processor.playbackPosition()>0?"PAUSE | ":"STOP  | ")+live+(!generated&&processor.isLooping()?" | LOOP ON":"");
        if(processor.externalReferenceSelected()){
            if(processor.referenceFailed.load())live="REFERENCE DETECTOR UNAVAILABLE";
            else if(!processor.ambienceReady.load())live=processor.referenceConnected.load()?"LEARNING REFERENCE / PLAY A QUIET PASSAGE":"ENABLE SIDECHAIN / SEND A REFERENCE TRACK";
            else live=automatic.getToggleState()?(processor.referenceConnected.load()?"FOLLOWING REFERENCE BACKGROUND":"REFERENCE READY / CONTINUING AMBIENCE"):"REFERENCE HELD / CONTINUING AMBIENCE";
        }
        displayLines[0].draw(g,controls,{82,495,796,17},1.65f,false);
        if(updateBusy&&downloadingUpdate){
            auto progress=onUpdateProgress?onUpdateProgress():std::pair<double,juce::String>{-1,"PREPARING"};
            displayLines[1].draw(g,"UPDATE / "+progress.second,{82,518,203,17},1.35f,false);
            constexpr int segments=48;int filled=(int)(juce::jlimit(0.0,1.0,progress.first)*segments);int cursor=(int)(juce::Time::getMillisecondCounter()/55)%segments;
            for(int i=0;i<segments;++i){bool lit=progress.first>=0?i<filled:(i-cursor+segments)%segments<6;g.setColour(lit?juce::Colour(0xffa4ffe9):juce::Colour(0xff245849));g.fillRect(295.0f+i*10.1f,522.0f,7.7f,8.0f);}
            displayLines[4].draw(g,progress.first>=0?juce::String((int)(juce::jlimit(0.0,1.0,progress.first)*100))+"%":"...",{803,518,75,17},1.65f,false);
        }else displayLines[1].draw(g,routing+" | "+live,{82,518,796,17});
        displayLines[2].draw(g,processor.externalReferenceSelected()?"SIDECHAIN REFERENCE / FRESH STATISTICAL AMBIENCE":processor.fileMode.load()||processor.ambienceReady.load()?processor.getFileName():"NO CLIP PROFILE LOADED",{82,543,558,17});
        auto time=[](double value){int seconds=(int)std::max(0.0,value);return juce::String(seconds/60).paddedLeft('0',2)+":"+juce::String(seconds%60).paddedLeft('0',2);};
        displayLines[3].draw(g,standalone?(generated?time(processor.playbackPosition())+" / INF":time(processor.playbackPosition())+" / "+time(processor.playbackDuration())):"HOST CLOCK",{683,543,195,17},1.65f,false);
    }
    if(!standalone)engraved(g,"HOST PLAYBACK  /  INFINITE PROFILE GENERATOR",{292,598,260,28},look.ink(),10);
}
void NoiseBaseEditor::resized() {
    skin.setBounds(85,37,74,63);
    reference.setBounds(760,51,136,34);
    for(int i=0;i<4;++i){juce::Slider* controls[]{&reduction,&preserve,&mix,&output};controls[i]->setBounds(78+i*222,337,138,122);}
    automatic.setBounds(68,270,145,32);reset.setBounds(224,274,82,25);noiseOnly.setBounds(330,270,165,32);procedural.setBounds(520,270,180,32);bypass.setBounds(750,270,140,32);
    load.setBounds(72,596,118,32);scan.setBounds(198,596,80,32);save.setBounds(566,596,118,32);
    if(processor.standaloneMode.load()) {
        load.setBounds(72,596,118,32);scan.setBounds(198,596,80,32);
        beginning.setBounds(306,592,52,40);play.setBounds(366,592,52,40);stop.setBounds(426,592,52,40);loop.setBounds(486,592,52,40);
        save.setBounds(566,596,118,32);devices.setBounds(692,596,108,32);updates.setBounds(808,596,88,32);
    }
}
void NoiseBaseEditor::timerCallback() {
    processor.serviceJobs();
    int next=juce::jlimit(0,2,(int)std::round(processor.params.getRawParameterValue("skin")->load()));
    if(next!=look.skin){look.skin=next;sendLookAndFeelChange();}
    inputNeedle=.78f*inputNeedle+.22f*processor.inputLevel.load();outputNeedle=.78f*outputNeedle+.22f*processor.outputLevel.load();reductionNeedle=.78f*reductionNeedle+.22f*processor.reductionLevel.load();
    bool busy=processor.busy.load(),generated=processor.proceduralSelected()&&!bypass.getToggleState(),external=processor.externalReferenceSelected();load.setEnabled(!busy);scan.setEnabled(!busy&&!external&&processor.fileMode.load());save.setEnabled(!busy&&(generated?processor.ambienceReady.load():processor.analysed.load()));
    reduction.setEnabled(!generated);preserve.setEnabled(!generated);automatic.setEnabled(!generated||external);reset.setEnabled(!busy&&(external||!generated||processor.fileMode.load()));reference.setEnabled(!busy);
    for(auto* button:{&play,&stop,&beginning})button->setEnabled(generated?processor.ambienceReady.load():processor.fileMode.load());
    loop.setEnabled(!generated&&processor.fileMode.load());
    auto transportId=processor.isPlaying()?"transport-pause":"transport-play";
    if(play.getComponentID()!=transportId){play.setComponentID(transportId);play.setTitle(processor.isPlaying()?"Pause":"Play");}
    loop.setToggleState(processor.isLooping(),juce::dontSendNotification);repaint();
}
void NoiseBaseEditor::openFile() {
    chooser=std::make_unique<juce::FileChooser>("Load a recording",juce::File{},"*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<NoiseBaseEditor>(this)](const juce::FileChooser& c){if(safe && c.getResult().existsAsFile())safe->processor.loadRecording(c.getResult());});
}
void NoiseBaseEditor::exportFile() {
    if(processor.proceduralSelected()&&!bypass.getToggleState()){
        auto* dialog=new juce::AlertWindow("Print procedural ambience","Duration in seconds (1 to 3600). A seamless loop is generated from the learned profile.",juce::MessageBoxIconType::NoIcon);
        dialog->addTextEditor("seconds","60","Seconds",false);dialog->addButton("Continue",1);dialog->addButton("Cancel",0);
        dialog->enterModalState(true,juce::ModalCallbackFunction::create([safe=juce::Component::SafePointer<NoiseBaseEditor>(this),dialog](int choice){if(!safe||choice!=1)return;double seconds=dialog->getTextEditorContents("seconds").getDoubleValue();if(seconds<1||seconds>3600){juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Procedural duration","Enter a duration from 1 to 3600 seconds.");return;}safe->chooseExportFile(seconds);}),true);return;
    }
    chooseExportFile(60);
}
void NoiseBaseEditor::chooseExportFile(double seconds) {
    bool original=bypass.getToggleState(),noise=noiseOnly.getToggleState()&&!original;
    bool generated=noise&&procedural.getToggleState();
    chooser=std::make_unique<juce::FileChooser>(original?"Print original WAV":generated?"Print seamless procedural ambience":noise?"Print estimated noise WAV":"Print denoised WAV",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(original?"NoiseBase-original.wav":generated?"NoiseBase-procedural-loop.wav":noise?"NoiseBase-noise-base.wav":"NoiseBase-denoised.wav"),"*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<NoiseBaseEditor>(this),seconds](const juce::FileChooser& c){if(safe && c.getResult()!=juce::File{})safe->processor.saveRecording(c.getResult().withFileExtension("wav"),seconds);});
}
