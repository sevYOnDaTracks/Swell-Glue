#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "GlueDSP.h"

struct ScopeFrame { float inMin=0, inMax=0, outMin=0, outMax=0; };
struct GlueSnapshot { float glue=40, mix=85, output=0; int profile=5; };

class GlueProcessor : public juce::AudioProcessor
{
public:
    GlueProcessor();
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> inputPeak{0}, outputPeak{0}, reductionPeak{0};
    std::atomic<bool> scopeActive{false};
    std::atomic<int> activeSlot{0};
    const juce::String getName() const override { return "Swell Glue"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter("bypass"); }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override { return 6; }
    int getCurrentProgram() override { return profile(); }
    void setCurrentProgram(int index) override { loadPreset(index); }
    const juce::String getProgramName(int index) override { return gluePresets[(size_t)juce::jlimit(0,5,index)].name; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    int profile() const { return juce::jlimit(0,5,(int)state.getRawParameterValue("profile")->load()); }
    void loadPreset(int);
    void selectSlot(int);
    void copySlot();
    GlueSnapshot capture() const;
    bool readScope(ScopeFrame&);
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void apply(const GlueSnapshot&);
    void render(juce::AudioBuffer<float>&,bool);
    void pushScope(const ScopeFrame&);
    static void publishPeak(std::atomic<float>&,float);
    GlueDSP dsp;
    juce::SmoothedValue<float> glue, gain, wetMix;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::AudioBuffer<float> inputDelay, inputScratch;
    int maximumBlock=1;
    int delayPosition=0, latency=0, scopeSamples=0, scopeStride=128, lastProfile=-1;
    ScopeFrame accumulating;
    juce::dsp::DryWetMixer<float> parallel{512};
    juce::dsp::DryWetMixer<float> bypassMix{512};
    juce::AbstractFifo scopeFifo{2048};
    std::array<ScopeFrame,2048> scopeData{};
    juce::CriticalSection slotLock;
    std::array<GlueSnapshot,2> slots{{{40,85,0,5},{55,90,1,1}}};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlueProcessor)
};
