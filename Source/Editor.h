#pragma once
#include "Processor.h"
#include "RasterAssets.h"
#include "VfdDisplay.h"

struct HardwareLook final : juce::LookAndFeel_V4 {
    HardwareLook();
    int skin=0;
    juce::Colour panel() const;
    juce::Colour ink() const;
    juce::Colour accent() const;
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
    void drawToggleButton(juce::Graphics&,juce::ToggleButton&,bool,bool) override;
    void drawButtonBackground(juce::Graphics&,juce::Button&,const juce::Colour&,bool,bool) override;
    void drawButtonText(juce::Graphics&,juce::TextButton&,bool,bool) override;
};

class NoiseBaseEditor final : public juce::AudioProcessorEditor,public juce::FileDragAndDropTarget,private juce::Timer,private juce::DarkModeSettingListener {
public:
    explicit NoiseBaseEditor(NoiseBaseProcessor&);
    ~NoiseBaseEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override {return !processor.busy.load()&&files.size()==1;}
    void filesDropped(const juce::StringArray& files,int,int) override {if(isInterestedInFileDrag(files))processor.loadRecording(juce::File(files[0]));}
    std::function<void()> onDevices;
    std::function<void()> onUpdates;
    void setUpdateBusy(bool busy,bool downloading=false) {updateBusy=busy;downloadingUpdate=downloading;updates.setEnabled(!busy);repaint();}
private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void drawMeter(juce::Graphics&,juce::Rectangle<float>,const juce::String&,float,bool);
    void openFile();
    void exportFile();
    void chooseExportFile(double seconds);
    NoiseBaseProcessor& processor;
    HardwareLook look;
    juce::Slider reduction,preserve,mix,output,skin;
    juce::ToggleButton automatic{"AUTO LEARN"},bypass{"BYPASS"},noiseOnly{"NOISE BASE"},procedural{"PROCEDURAL"};
    juce::TextButton reset{"LEARN"},load{"LOAD TAPE"},scan{"SCAN"},play,stop,loop,beginning,save{"PRINT WAV"},devices{"PATCH BAY"};
    juce::TextButton updates{"UPDATES"};
    std::array<VfdLine,4> displayLines;
    bool updateBusy=false,downloadingUpdate=false;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment,bypassAttachment,noiseAttachment,proceduralAttachment;
    std::unique_ptr<juce::FileChooser> chooser;
    float inputNeedle=0,outputNeedle=0,reductionNeedle=0;
};
