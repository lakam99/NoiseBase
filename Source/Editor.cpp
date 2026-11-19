#include "Editor.h"

namespace {
juce::Font labelFont(float size=12,bool bold=true){return juce::Font(juce::FontOptions("Arial",size,bold?juce::Font::bold:juce::Font::plain));}
void engraved(juce::Graphics& g,const juce::String& text,juce::Rectangle<float> r,juce::Colour ink,float size=12) {
    g.setFont(labelFont(size));g.setColour(ink);g.drawText(text,r,juce::Justification::centred,false);
}
}
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
    bool automatic=b.getButtonText().contains("AUTO");g.setFont(labelFont(10));
    auto text=r.reduced(4,1);auto top=text.removeFromTop(15);
    g.setColour(ink().withAlpha(b.getToggleState()?1.0f:.40f));g.drawText(automatic?"AUTO":"BYPASS",top,juce::Justification::centredLeft);
    g.setColour(ink().withAlpha(b.getToggleState()?.40f:1.0f));g.drawText(automatic?"HOLD":"IN",text,juce::Justification::centredLeft);
}
void HardwareLook::drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool,bool down) {
    auto r=b.getLocalBounds().toFloat().reduced(1);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setOpacity(b.isEnabled()?1.0f:.48f);
    g.drawImage(RasterAssets::get().buttons[down?1:0],r,juce::RectanglePlacement::stretchToFit);
    g.setOpacity(1);
}
void HardwareLook::drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) {
    g.setFont(labelFont(11));g.setColour(juce::Colours::black.withAlpha(.85f));g.drawText(b.getButtonText(),b.getLocalBounds().translated(0,1),juce::Justification::centred);
    g.setColour(juce::Colour(0xffeee8d7).withAlpha(b.isEnabled()?1.0f:.55f));g.drawText(b.getButtonText(),b.getLocalBounds(),juce::Justification::centred);
}

NoiseBaseEditor::NoiseBaseEditor(NoiseBaseProcessor& p):AudioProcessorEditor(p),processor(p) {
    setLookAndFeel(&look);setOpaque(true);
    for(auto* control:{&reduction,&preserve,&mix,&output,&skin}) {control->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);control->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);control->setRotaryParameters(juce::MathConstants<float>::pi*1.23f,juce::MathConstants<float>::pi*2.77f,true);addAndMakeVisible(control);}
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"reduction",reduction));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"preserve",preserve));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"mix",mix));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"output",output));
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.params,"skin",skin));
    reduction.setComponentID("reduction");preserve.setComponentID("preserve");mix.setComponentID("mix");output.setComponentID("output");
    for(auto* control:{&reduction,&preserve,&mix,&output})control->setPopupDisplayEnabled(true,false,this);
    reduction.textFromValueFunction=[](double v){return juce::String(v,1)+" dB";};output.textFromValueFunction=[](double v){return juce::String(v,1)+" dB";};
    preserve.textFromValueFunction=[](double v){return juce::String(v,0)+" %";};mix.textFromValueFunction=[](double v){return juce::String(v,0)+" % clean";};
    for(auto* c:{(juce::Component*)&automatic,(juce::Component*)&bypass,(juce::Component*)&reset})addAndMakeVisible(c);
    autoAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"auto",automatic);
    bypassAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.params,"bypass",bypass);
    reset.onClick=[this]{processor.params.getParameter("auto")->setValueNotifyingHost(1);processor.resetProfile();};
    if(processor.standaloneMode.load()) {
        for(auto* c:{&load,&scan,&play,&stop,&save,&devices})addAndMakeVisible(c);
        addAndMakeVisible(updates);updates.onClick=[this]{if(onUpdates)onUpdates();};
        load.onClick=[this]{openFile();};scan.onClick=[this]{processor.scanRecording();};
        play.onClick=[this]{processor.togglePlayback();};stop.onClick=[this]{processor.stopPlayback();};
        save.onClick=[this]{exportFile();};devices.onClick=[this]{if(onDevices)onDevices();};
    }
    reduction.setTooltip("Maximum noise reduction, 0 to 24 dB");preserve.setTooltip("Protect quieter speech detail");mix.setTooltip("Dry / denoised blend");output.setTooltip("Output trim, -12 to +12 dB");skin.setTooltip("Ivory / Graphite / Walnut faceplates");
    setSize(960,processor.standaloneMode.load()?668:540);startTimerHz(30);
}
NoiseBaseEditor::~NoiseBaseEditor(){stopTimer();setLookAndFeel(nullptr);}
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
    engraved(g,"REDWEB  /  DIALOGUE RESTORATION",{270,69,420,17},look.ink().withAlpha(.75f),9);
    engraved(g,"FACEPLATE",{65,22,110,16},look.ink(),9);
    engraved(g,juce::StringArray{"IVORY","GRAPHITE","WALNUT"}[look.skin],{65,94,110,11},look.ink(),10);
    drawMeter(g,{64,112,260,150},"INPUT  /  dBFS",inputNeedle,false);
    drawMeter(g,{350,112,260,150},"REDUCTION  /  dB",reductionNeedle,true);
    drawMeter(g,{636,112,260,150},"OUTPUT  /  dBFS",outputNeedle,false);
    const juce::StringArray names{"REDUCTION","VOICE DETAIL","DRY / CLEAN","OUTPUT TRIM"};
    const juce::StringArray units{"dB","PRESERVE %","BLEND","dB"};
    for(int i=0;i<4;++i) {
        float x=87.0f+i*222;
        engraved(g,names[i],{x,305,120,20},look.ink(),11);
        // Scale lettering belongs to the faceplate, outside the knob's child
        // bounds, so the upper midpoint is not clipped by JUCE's child paint.
        std::vector<float> positions;
        juce::StringArray labels;
        if(i==0){positions={0,.25f,.5f,.75f,1};labels={"0","6","12","18","24"};}
        else if(i==3){positions={0,.25f,.5f,.75f,1};labels={"-12","-6","0","+6","+12"};}
        else if(i==2){positions={0,.5f,1};labels={"DRY","50","CLEAN"};}
        else {positions={0,.5f,1};labels={"0","50","100"};}
        float cx=147+i*222,cy=398;
        for(size_t mark=0;mark<positions.size();++mark){float a=juce::MathConstants<float>::pi*(1.23f+1.54f*positions[mark]);g.setColour(look.ink());g.setFont(labelFont(10));g.drawText(labels[(int)mark],juce::Rectangle<float>{cx+std::sin(a)*66-19,cy-std::cos(a)*66-6,38,12},juce::Justification::centred);}
        engraved(g,units[i],{x-8,459,136,16},look.ink().withAlpha(.85f),9);
    }
    g.setColour(juce::Colours::black.withAlpha(.2f));g.drawLine(64,478,896,478,1);
    bool learned=processor.quietSeconds.load()>=.35f;
    int lamp=processor.detectorFailed.load()?2:(learned?1:0);
    g.drawImage(art.lamps[(size_t)lamp],juce::Rectangle<float>{67,494,18,18},juce::RectanglePlacement::stretchToFit);
    juce::String live=learned?"FLOOR LEARNED":"AUTO LEARNING  /  waiting for a pause";
    if(!processor.detectorAvailable.load())live="SPEECH DETECTOR  /  warming up";
    if(processor.detectorFailed.load())live="DETECTOR UNAVAILABLE  /  restore the runtime files";
    if(processor.standaloneMode.load())live=processor.getStatus();
    g.setFont(labelFont(10,false));g.setColour(look.ink());g.drawFittedText(live,{95,491,700,31},juce::Justification::centredLeft,2);
    if(processor.standaloneMode.load()) {
        g.setColour(look.ink().withAlpha(.20f));g.drawLine(72,540,888,540,1);
        g.setFont(labelFont(11,false));g.setColour(look.ink());g.drawFittedText(processor.fileMode.load()?processor.getFileName():"LOAD A RECORDING  /  automatic noise scan",{80,550,640,24},juce::Justification::centredLeft,1);
        if(processor.busy.load()){int state=((juce::Time::getMillisecondCounter()/350)%2)==0?0:3;g.drawImage(art.lamps[(size_t)state],juce::Rectangle<float>{853,553,13,13},juce::RectanglePlacement::stretchToFit);}
    }
}
void NoiseBaseEditor::resized() {
    skin.setBounds(85,37,74,63);
    for(int i=0;i<4;++i){juce::Slider* controls[]{&reduction,&preserve,&mix,&output};controls[i]->setBounds(78+i*222,337,138,122);}
    automatic.setBounds(68,270,220,32);bypass.setBounds(578,270,230,32);reset.setBounds(432,274,96,25);
    if(processor.standaloneMode.load()) {
        updates.setBounds(806,493,90,25);
        load.setBounds(80,594,132,27);scan.setBounds(224,594,100,27);play.setBounds(336,594,100,27);stop.setBounds(448,594,100,27);save.setBounds(560,594,150,27);devices.setBounds(724,594,155,27);
    }
}
void NoiseBaseEditor::timerCallback() {
    int next=juce::jlimit(0,2,(int)std::round(processor.params.getRawParameterValue("skin")->load()));
    if(next!=look.skin){look.skin=next;sendLookAndFeelChange();}
    inputNeedle=.78f*inputNeedle+.22f*processor.inputLevel.load();outputNeedle=.78f*outputNeedle+.22f*processor.outputLevel.load();reductionNeedle=.78f*reductionNeedle+.22f*processor.reductionLevel.load();
    bool busy=processor.busy.load();load.setEnabled(!busy);scan.setEnabled(!busy&&processor.fileMode.load());save.setEnabled(!busy&&processor.analysed.load());play.setEnabled(processor.fileMode.load());stop.setEnabled(processor.fileMode.load());
    play.setButtonText(processor.isPlaying()?"PAUSE":"PLAY");repaint();
}
void NoiseBaseEditor::openFile() {
    chooser=std::make_unique<juce::FileChooser>("Load a recording",juce::File{},"*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode|juce::FileBrowserComponent::canSelectFiles,[safe=juce::Component::SafePointer<NoiseBaseEditor>(this)](const juce::FileChooser& c){if(safe && c.getResult().existsAsFile())safe->processor.loadRecording(c.getResult());});
}
void NoiseBaseEditor::exportFile() {
    chooser=std::make_unique<juce::FileChooser>("Print denoised WAV",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("NoiseBase-denoised.wav"),"*.wav");
    chooser->launchAsync(juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::canSelectFiles|juce::FileBrowserComponent::warnAboutOverwriting,[safe=juce::Component::SafePointer<NoiseBaseEditor>(this)](const juce::FileChooser& c){if(safe && c.getResult()!=juce::File{})safe->processor.saveRecording(c.getResult().withFileExtension("wav"));});
}
