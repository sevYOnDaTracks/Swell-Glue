#include "PluginProcessor.h"
#include "PluginEditor.h"

GlueProcessor::GlueProcessor()
 : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true)
                                  .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
   state(*this,nullptr,"SwellGlue",layout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout GlueProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout result;
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"glue",1},"Glue",juce::NormalisableRange<float>(0,100,.1f),40.f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix",1},"Mix",juce::NormalisableRange<float>(0,100,.1f),85.f));
    result.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"output",1},"Output",juce::NormalisableRange<float>(-12,12,.1f),0.f));
    juce::StringArray names; for(const auto& p:gluePresets) names.add(p.name);
    result.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"profile",1},"Style",names,5));
    result.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass",1},"Bypass",false));
    return result;
}
bool GlueProcessor::isBusesLayoutSupported(const BusesLayout& b) const
{
    return b.getMainInputChannelSet()==b.getMainOutputChannelSet()
        && (b.getMainOutputChannelSet()==juce::AudioChannelSet::mono() || b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());
}
void GlueProcessor::prepareToPlay(double sr,int blockSize)
{
    const auto channels=(size_t)getTotalNumInputChannels();
    maximumBlock=std::max(1,blockSize);inputScratch.setSize((int)channels,maximumBlock);
    // Linear-phase FIR keeps the dry and wet paths aligned for parallel mixing.
    oversampling=std::make_unique<juce::dsp::Oversampling<float>>(channels,2,juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,true,true);
    oversampling->initProcessing((size_t)std::max(1,blockSize));
    latency=(int)oversampling->getLatencyInSamples();setLatencySamples(latency);
    juce::dsp::ProcessSpec spec{sr,(juce::uint32)std::max(1,blockSize),(juce::uint32)channels};
    for(auto* mixer:{&parallel,&bypassMix}) { mixer->prepare(spec);mixer->setMixingRule(juce::dsp::DryWetMixingRule::linear);mixer->setWetLatency((float)latency);mixer->reset(); }
    glue.reset(sr*4,.035);glue.setCurrentAndTargetValue(state.getRawParameterValue("glue")->load()/100.f);
    gain.reset(sr,.035);gain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(state.getRawParameterValue("output")->load()));
    lastProfile=profile();dsp.prepare(sr*4,lastProfile);
    inputDelay.setSize((int)channels,std::max(1,latency));inputDelay.clear();delayPosition=0;
    accumulating={};scopeSamples=0;scopeStride=std::max(1,(int)(sr/350.));
    inputPeak=0;outputPeak=0;reductionPeak=0;
}
void GlueProcessor::publishPeak(std::atomic<float>& target,float value)
{
    auto previous=target.load(std::memory_order_relaxed);
    while(previous<value && !target.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
}
void GlueProcessor::render(juce::AudioBuffer<float>& b,bool forceBypass)
{
    juce::ScopedNoDenormals noDenormals;
    if(b.getNumSamples()==0 || oversampling==nullptr)return;
    const bool bypassed=forceBypass || state.getRawParameterValue("bypass")->load()>.5f;
    const int style=profile();if(style!=lastProfile){dsp.setProfile(style);lastProfile=style;}
    glue.setTargetValue(state.getRawParameterValue("glue")->load()/100.f);
    gain.setTargetValue(juce::Decibels::decibelsToGain(state.getRawParameterValue("output")->load()));
    auto fullBlock=juce::dsp::AudioBlock<float>(b);
    // Hosts may deliver more than the advertised block size: process in bounded chunks.
    const int safeChunk=maximumBlock;
    float gr=0,ip=0,op=0;
    for(int offset=0;offset<b.getNumSamples();offset+=safeChunk)
    {
        const int count=std::min(safeChunk,b.getNumSamples()-offset);
        auto block=fullBlock.getSubBlock((size_t)offset,(size_t)count);
        parallel.pushDrySamples(block);bypassMix.pushDrySamples(block);
        // Keep an aligned, decimated input reference for the display, without audio-thread allocation.
        for(int i=0;i<count;++i)for(int c=0;c<b.getNumChannels();++c)ip=std::max(ip,std::abs(b.getSample(c,offset+i)));
        auto up=oversampling->processSamplesUp(block);
        for(size_t i=0;i<up.getNumSamples();++i)
        {
            float sample[2]{};for(size_t c=0;c<up.getNumChannels();++c)sample[c]=up.getSample((int)c,(int)i);
            dsp.tick(sample,(int)up.getNumChannels(),glue.getNextValue());gr=std::max(gr,dsp.reductionDb);
            for(size_t c=0;c<up.getNumChannels();++c)up.setSample((int)c,(int)i,sample[c]);
        }
        // Collect the raw reference in a preallocated scratch buffer before it is overwritten below.
        // inputScratch is prepared once per stream, see header.
        for(int c=0;c<b.getNumChannels();++c)
            inputScratch.copyFrom(c,0,b,c,offset,count);
        oversampling->processSamplesDown(block);
        parallel.setWetMixProportion(state.getRawParameterValue("glue")->load()==0.f?0.f:state.getRawParameterValue("mix")->load()/100.f);
        parallel.mixWetSamples(block);
        for(int i=0;i<count;++i)
        {
            const float g=gain.getNextValue();for(int c=0;c<b.getNumChannels();++c)b.setSample(c,offset+i,b.getSample(c,offset+i)*g);
        }
        bypassMix.setWetMixProportion(bypassed?0.f:1.f);bypassMix.mixWetSamples(block);
        for(int i=0;i<count;++i)
        {
            for(int c=0;c<b.getNumChannels();++c)
            {
                const float input=inputScratch.getSample(c,i);
                const float aligned=latency>0?inputDelay.getSample(c,delayPosition):input;
                if(latency>0)inputDelay.setSample(c,delayPosition,input);
                const float output=b.getSample(c,offset+i);op=std::max(op,std::abs(output));
                accumulating.inMin=std::min(accumulating.inMin,aligned);accumulating.inMax=std::max(accumulating.inMax,aligned);
                accumulating.outMin=std::min(accumulating.outMin,output);accumulating.outMax=std::max(accumulating.outMax,output);
            }
            if(latency>0)delayPosition=(delayPosition+1)%latency;
            if(++scopeSamples>=scopeStride){if(scopeActive.load(std::memory_order_relaxed))pushScope(accumulating);accumulating={};scopeSamples=0;}
        }
    }
    publishPeak(inputPeak,ip);publishPeak(outputPeak,op);publishPeak(reductionPeak,bypassed?0.f:gr);
}
void GlueProcessor::processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&){ if (! licence->isUnlocked()) { b.clear(); return; } render(b,false);}
void GlueProcessor::processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&){render(b,true);}
void GlueProcessor::pushScope(const ScopeFrame& frame)
{
    int s1,n1,s2,n2;scopeFifo.prepareToWrite(1,s1,n1,s2,n2);if(n1)scopeData[(size_t)s1]=frame;scopeFifo.finishedWrite(n1);
}
bool GlueProcessor::readScope(ScopeFrame& frame)
{
    int s1,n1,s2,n2;scopeFifo.prepareToRead(1,s1,n1,s2,n2);if(n1)frame=scopeData[(size_t)s1];scopeFifo.finishedRead(n1);return n1!=0;
}
GlueSnapshot GlueProcessor::capture() const
{return {state.getRawParameterValue("glue")->load(),state.getRawParameterValue("mix")->load(),state.getRawParameterValue("output")->load(),profile()};}
void GlueProcessor::apply(const GlueSnapshot& s)
{
    const char* ids[]{"glue","mix","output","profile"};const float values[]{s.glue,s.mix,s.output,(float)s.profile};
    for(int i=0;i<4;++i){auto* p=state.getParameter(ids[i]);p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(values[i]));p->endChangeGesture();}
}
void GlueProcessor::loadPreset(int index)
{index=juce::jlimit(0,5,index);const auto& p=gluePresets[(size_t)index];apply({p.glue,p.mix,p.output,index});}
void GlueProcessor::selectSlot(int index)
{
    index=juce::jlimit(0,1,index);GlueSnapshot target;
    {const juce::ScopedLock lock(slotLock);slots[(size_t)activeSlot.load()]=capture();activeSlot=index;target=slots[(size_t)index];}
    apply(target);
}
void GlueProcessor::copySlot()
{const juce::ScopedLock lock(slotLock);slots[0]=slots[1]=capture();}
void GlueProcessor::getStateInformation(juce::MemoryBlock& out)
{
    auto tree=state.copyState();const juce::ScopedLock lock(slotLock);slots[(size_t)activeSlot.load()]=capture();tree.setProperty("activeSlot",activeSlot.load(),nullptr);
    for(int i=0;i<2;++i){auto prefix=juce::String(i==0?"slotA":"slotB");const auto& s=slots[(size_t)i];tree.setProperty(prefix+"Glue",s.glue,nullptr);tree.setProperty(prefix+"Mix",s.mix,nullptr);tree.setProperty(prefix+"Output",s.output,nullptr);tree.setProperty(prefix+"Profile",s.profile,nullptr);}
    if(auto xml=tree.createXml())copyXmlToBinary(*xml,out);
}
void GlueProcessor::setStateInformation(const void* data,int size)
{
    if(auto xml=getXmlFromBinary(data,size);xml && xml->hasTagName("SwellGlue"))
    {
        auto tree=juce::ValueTree::fromXml(*xml);state.replaceState(tree);
        const juce::ScopedLock lock(slotLock);activeSlot=juce::jlimit(0,1,(int)tree.getProperty("activeSlot",0));
        for(int i=0;i<2;++i){auto prefix=juce::String(i==0?"slotA":"slotB");auto& s=slots[(size_t)i];s.glue=juce::jlimit(0.f,100.f,(float)tree.getProperty(prefix+"Glue",40.f));s.mix=juce::jlimit(0.f,100.f,(float)tree.getProperty(prefix+"Mix",85.f));s.output=juce::jlimit(-12.f,12.f,(float)tree.getProperty(prefix+"Output",0.f));s.profile=juce::jlimit(0,5,(int)tree.getProperty(prefix+"Profile",5));}
    }
}
juce::AudioProcessorEditor* GlueProcessor::createEditor(){return swell::LicenceGuard::attach (new GlueEditor(*this));}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new GlueProcessor();}
