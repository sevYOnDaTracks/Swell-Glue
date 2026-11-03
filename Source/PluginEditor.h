#pragma once
#include "PluginProcessor.h"

class GlueLook : public juce::LookAndFeel_V4
{
public:
    GlueLook();
    void drawRotarySlider(juce::Graphics&,int,int,int,int,float,float,float,juce::Slider&) override;
};

class GlueEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit GlueEditor(GlueProcessor&);
    ~GlueEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    GlueProcessor& proc;
    GlueLook look;
    juce::Image logo;
    juce::Slider glue, mix, output;
    juce::ComboBox presets;
    juce::TextButton previous{"<"},next{">"},slotA{"A"},slotB{"B"},copy{"A > B"};
    juce::TextButton reset{"Retablir le preset"},bypass{"Bypass"},freeze{"Pause"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ga,ma,oa;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> ba;
    std::array<ScopeFrame,700> history{};
    int historyPosition=0;
    float scale=1, inLevel=0,outLevel=0,grLevel=0;
    int clipHold=0;
    bool paused=false;
    juce::String message;
    int messageTicks=0;
    void timerCallback() override;
    void drawScope(juce::Graphics&);
    void drawMeters(juce::Graphics&);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlueEditor)
};
