#include "../Source/PluginProcessor.h"
#include <iostream>
#include <stdexcept>

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void set(GlueProcessor& p,const char* id,float v){auto* parameter=p.state.getParameter(id);parameter->setValueNotifyingHost(parameter->convertTo0to1(v));}
bool equal(const GlueSnapshot& a,const GlueSnapshot& b)
{return std::abs(a.glue-b.glue)<.01f&&std::abs(a.mix-b.mix)<.01f&&std::abs(a.output-b.output)<.01f&&a.profile==b.profile;}

void audioCheck(double rate,int channels,float mix,float output,bool forcedBypass,bool parameterBypass,bool zeroGlue)
{
    GlueProcessor p;p.setPlayConfigDetails(channels,channels,rate,64);set(p,"glue",zeroGlue?0:80);set(p,"mix",mix);set(p,"output",output);set(p,"bypass",parameterBypass?1:0);
    p.prepareToPlay(rate,64);juce::MidiBuffer midi;
    auto process=[&](juce::AudioBuffer<float>& block){if(forcedBypass)p.processBlockBypassed(block,midi);else p.processBlock(block,midi);};
    juce::AudioBuffer<float> warm(channels,64);for(int i=0;i<300;++i){warm.clear();process(warm);}
    const int length=12000,latency=p.getLatencySamples();juce::AudioBuffer<float> input(channels,length),result(channels,length+latency+1024);result.clear();
    for(int c=0;c<channels;++c)for(int i=0;i<length;++i)input.setSample(c,i,(float)(.35*std::sin(i*.11)*(c==0?1:-1)));
    int sizes[]{1,7,64,257,1024};int offset=0,iteration=0;
    p.scopeActive=true;
    while(offset<length+latency)
    {
        int n=sizes[iteration++%5];juce::AudioBuffer<float> block(channels,n);block.clear();int count=std::min(n,length-offset);
        if(count>0)for(int c=0;c<channels;++c)block.copyFrom(c,0,input,c,offset,count);
        process(block);for(int c=0;c<channels;++c)result.copyFrom(c,offset,block,c,0,n);offset+=n;
    }
    const bool dry=forcedBypass||parameterBypass||mix==0||zeroGlue;
    const float expectedGain=forcedBypass||parameterBypass?1.f:juce::Decibels::decibelsToGain(output);
    double difference=0;for(int c=0;c<channels;++c)for(int i=0;i<length;++i)
    {
        float value=result.getSample(c,i+latency);require(std::isfinite(value),"Non finite audio");
        if(dry)require(std::abs(value-input.getSample(c,i)*expectedGain)<2.e-5f,"Dry/bypass alignment or gain error");
        difference+=std::abs(value-input.getSample(c,i));
        if(channels==2)require(std::abs(result.getSample(0,i+latency)+result.getSample(1,i+latency))<1.e-5f,"Stereo linkage error");
    }
    if(!dry)require(difference>1,"No processing at high Glue");
    require(p.inputPeak.load()>.2f&&p.outputPeak.load()>.01f,"Meters missing");
    ScopeFrame frame;bool scopeSeen=false;while(p.readScope(frame))scopeSeen=scopeSeen||frame.inMax>.01f||frame.inMin<-.01f;require(scopeSeen,"Scope missing real signal");
    p.releaseResources();
}

int main()
{
    swell::Licence::unlockForTesting(); // rendu hors ligne, sans fichier de licence
    juce::ScopedJuceInitialiser_GUI init;
    try {
        GlueProcessor p;
        for(int i=0;i<6;++i){p.loadPreset(i);auto s=p.capture();const auto& preset=gluePresets[(size_t)i];require(s.profile==i&&std::abs(s.glue-preset.glue)<.01f&&std::abs(s.mix-preset.mix)<.01f,"Factory preset recall failed");}
        set(p,"glue",63.4f);set(p,"output",2.3f);const auto a=p.capture();p.selectSlot(1);p.loadPreset(2);set(p,"mix",38.7f);const auto b=p.capture();p.selectSlot(0);require(equal(p.capture(),a),"A recall failed");p.selectSlot(1);require(equal(p.capture(),b),"B recall failed");
        juce::MemoryBlock state;p.getStateInformation(state);GlueProcessor restored;restored.setStateInformation(state.getData(),(int)state.getSize());require(restored.activeSlot==1&&equal(restored.capture(),b),"State restore failed");restored.selectSlot(0);require(equal(restored.capture(),a),"Saved A snapshot failed");restored.copySlot();restored.selectSlot(1);require(equal(restored.capture(),a),"Copy snapshot failed");
        for(double rate:{44100.,48000.,96000.})for(int channels:{1,2})
        {
            audioCheck(rate,channels,0,0,false,false,false);
            audioCheck(rate,channels,0,6,false,false,false);
            audioCheck(rate,channels,100,6,true,false,false);
            audioCheck(rate,channels,100,6,false,true,false);
            audioCheck(rate,channels,100,0,false,false,true);
            audioCheck(rate,channels,45,0,false,false,false);
        }
        std::cout<<"PASS: presets, A/B, copy, session restore, real meters/scope, mono/stereo, oversized/variable blocks, dry gain, zero Glue and both bypass paths.\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
