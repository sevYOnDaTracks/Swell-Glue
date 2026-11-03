#include "PluginEditor.h"
#include "BinaryData.h"
namespace {
const juce::Colour background{0xff1b1c20}, screen{0xff121316}, border{0xff34353b};
const juce::Colour text{0xffeeeef1}, dim{0xffa4a5af}, accent{0xfff4b765}, original{0xff8891a3};
void label(juce::Graphics& g,const juce::String& s,juce::Rectangle<float> r,float size,juce::Colour colour,juce::Justification align=juce::Justification::left,bool bold=false)
{g.setColour(colour);g.setFont(juce::FontOptions(size,bold?juce::Font::bold:juce::Font::plain));g.drawText(s,r,align);}
}
GlueLook::GlueLook()
{
    setColour(juce::TextButton::buttonColourId,juce::Colour(0xff25262b));
    setColour(juce::TextButton::buttonOnColourId,juce::Colour(0xff51402b));
    setColour(juce::TextButton::textColourOffId,text);setColour(juce::TextButton::textColourOnId,accent);
    setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff25262b));
    setColour(juce::ComboBox::outlineColourId,border);setColour(juce::ComboBox::textColourId,text);
    setColour(juce::PopupMenu::backgroundColourId,background);setColour(juce::PopupMenu::textColourId,text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId,juce::Colour(0xff51402b));
}
void GlueLook::drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,float pos,float start,float end,juce::Slider& slider)
{
    auto r=juce::Rectangle<float>((float)x,(float)y,(float)width,(float)height).reduced(8);
    float size=juce::jmin(r.getWidth(),r.getHeight());r=r.withSizeKeepingCentre(size,size);
    auto centre=r.getCentre();float radius=size*.5f;bool main=slider.getName()=="Glue";
    auto colour=main?accent:juce::Colour(0xffafb4c1);
    juce::Path track,arc;track.addCentredArc(centre.x,centre.y,radius-4,radius-4,0,start,end,true);
    arc.addCentredArc(centre.x,centre.y,radius-4,radius-4,0,start,start+pos*(end-start),true);
    g.setColour(border);g.strokePath(track,juce::PathStrokeType(5));g.setColour(colour);g.strokePath(arc,juce::PathStrokeType(5));
    auto face=r.reduced(main?23.f:18.f);g.setGradientFill(juce::ColourGradient(juce::Colour(0xff36373e),face.getTopLeft(),background,face.getBottomRight(),false));
    g.fillEllipse(face);g.setColour(juce::Colour(0xff494b53));g.drawEllipse(face,1);
    float angle=start+pos*(end-start);auto p=centre.getPointOnCircumference(radius-(main?32.f:26.f),angle);
    auto q=centre.getPointOnCircumference(radius-(main?51.f:40.f),angle);g.setColour(colour);g.drawLine({p,q},main?4.f:3.f);
    bool db=slider.getName()=="Output";auto value=db?juce::String(slider.getValue(),1):juce::String((int)std::round(slider.getValue()));
    if(db&&slider.getValue()>0)value="+"+value;
    label(g,value,{centre.x-55,centre.y-16,110,30},main?32.f:21.f,text,juce::Justification::centred,true);
    label(g,db?"dB":main?"%":"% traite",{centre.x-55,centre.y+15,110,16},10,dim,juce::Justification::centred);
}
GlueEditor::GlueEditor(GlueProcessor& p):AudioProcessorEditor(p),proc(p)
{
    setLookAndFeel(&look);setSize(1000,730);setResizable(true,true);setResizeLimits(800,584,1500,1095);
    if(auto* constrainer=getConstrainer())constrainer->setFixedAspectRatio(1000./730.);
    logo=juce::ImageCache::getFromMemory(BinaryData::swellmark_png,BinaryData::swellmark_pngSize);
    for(auto* slider:{&glue,&mix,&output})
    {
        slider->setSliderStyle(juce::Slider::RotaryVerticalDrag);slider->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
        slider->setRotaryParameters(juce::MathConstants<float>::pi*1.25f,juce::MathConstants<float>::pi*2.75f,true);addAndMakeVisible(*slider);
    }
    glue.setName("Glue");mix.setName("Mix");output.setName("Output");glue.setDoubleClickReturnValue(true,0);mix.setDoubleClickReturnValue(true,100);output.setDoubleClickReturnValue(true,0);
    ga=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"glue",glue);
    ma=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"mix",mix);
    oa=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.state,"output",output);
    for(int i=0;i<6;++i)presets.addItem(gluePresets[(size_t)i].name,i+1);
    presets.setSelectedItemIndex(proc.profile(),juce::dontSendNotification);presets.onChange=[this]{proc.loadPreset(presets.getSelectedItemIndex());};
    addAndMakeVisible(presets);
    for(auto* button:{&previous,&next,&slotA,&slotB,&copy,&reset,&bypass,&freeze})addAndMakeVisible(*button);
    previous.onClick=[this]{proc.loadPreset((proc.profile()+5)%6);};next.onClick=[this]{proc.loadPreset((proc.profile()+1)%6);};
    slotA.onClick=[this]{proc.selectSlot(0);};slotB.onClick=[this]{proc.selectSlot(1);};
    copy.onClick=[this]{proc.copySlot();message="Reglages copies vers "+juce::String(proc.activeSlot.load()==0?"B":"A");messageTicks=45;};
    reset.onClick=[this]{proc.loadPreset(proc.profile());};bypass.setClickingTogglesState(true);
    ba=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.state,"bypass",bypass);
    freeze.onClick=[this]{paused=!paused;freeze.setButtonText(paused?"Reprendre":"Pause");};
    ScopeFrame discard;while(proc.readScope(discard)){}proc.scopeActive=true;
    resized();startTimerHz(30);
}
GlueEditor::~GlueEditor(){proc.scopeActive=false;stopTimer();setLookAndFeel(nullptr);}
void GlueEditor::resized()
{
    scale=getWidth()/1000.f;
    auto place=[this](juce::Component& c,int x,int y,int w,int h){c.setTransform(juce::AffineTransform());c.setBounds(x,y,w,h);c.setTransform(juce::AffineTransform::scale(scale));};
    place(previous,385,24,30,35);place(presets,423,24,210,35);place(next,641,24,30,35);
    place(slotA,820,24,34,35);place(slotB,861,24,34,35);place(copy,905,24,67,35);
    place(glue,403,406,194,194);place(mix,235,453,140,140);place(output,625,453,140,140);
    place(freeze,653,342,90,28);place(reset,540,676,145,32);place(bypass,884,676,90,32);
}
void GlueEditor::timerCallback()
{
    ScopeFrame frame;while(proc.readScope(frame))if(!paused){history[(size_t)historyPosition]=frame;historyPosition=(historyPosition+1)%(int)history.size();}
    inLevel=std::max(proc.inputPeak.exchange(0),inLevel*.90f);outLevel=std::max(proc.outputPeak.exchange(0),outLevel*.90f);
    grLevel=std::max(proc.reductionPeak.exchange(0),grLevel-.35f);grLevel=std::max(0.f,grLevel);
    if(proc.state.getRawParameterValue("bypass")->load()>.5f)grLevel=0;
    if(outLevel>=1)clipHold=45;else if(clipHold>0)--clipHold;
    presets.setSelectedItemIndex(proc.profile(),juce::dontSendNotification);
    const int slot=proc.activeSlot.load();slotA.setToggleState(slot==0,juce::dontSendNotification);slotB.setToggleState(slot==1,juce::dontSendNotification);copy.setButtonText(slot==0?"A > B":"B > A");
    if(messageTicks>0)--messageTicks;repaint();
}
void GlueEditor::drawScope(juce::Graphics& g)
{
    juce::Rectangle<float> r{45,189,700,142};g.setColour(juce::Colour(0xff25262b));
    for(int i=1;i<8;++i)g.drawVerticalLine((int)(r.getX()+r.getWidth()*i/8),r.getY(),r.getBottom());
    for(int i=1;i<4;++i)g.drawHorizontalLine((int)(r.getY()+r.getHeight()*i/4),r.getX(),r.getRight());
    const auto trace=[&](bool processed,juce::Colour colour)
    {
        juce::Path shape;const float cy=r.getCentreY();
        for(int i=0;i<(int)history.size();++i){const auto& f=history[(size_t)((historyPosition+i)%history.size())];float value=processed?f.outMax:f.inMax;float x=r.getX()+i*(r.getWidth()/(history.size()-1));float y=cy-juce::jlimit(-1.1f,1.1f,value)*r.getHeight()*.43f;if(i==0)shape.startNewSubPath(x,y);else shape.lineTo(x,y);}
        for(int i=(int)history.size()-1;i>=0;--i){const auto& f=history[(size_t)((historyPosition+i)%history.size())];float value=processed?f.outMin:f.inMin;shape.lineTo(r.getX()+i*(r.getWidth()/(history.size()-1)),cy-juce::jlimit(-1.1f,1.1f,value)*r.getHeight()*.43f);}shape.closeSubPath();g.setColour(colour.withAlpha(.17f));g.fillPath(shape);g.setColour(colour);g.strokePath(shape,juce::PathStrokeType(1.1f));
    };
    trace(false,original);trace(true,bypass.getToggleState()?dim:accent);
}
void GlueEditor::drawMeters(juce::Graphics& g)
{
    label(g,"Reduction",{776,158,85,17},10,dim,juce::Justification::centred);
    label(g,"de gain",{776,175,85,17},10,dim,juce::Justification::centred);
    label(g,juce::String(grLevel,1),{778,200,65,35},25,accent,juce::Justification::centred,true);
    label(g,"dB",{840,214,20,15},10,dim);
    for(int i=0;i<15;++i){g.setColour(grLevel>=i*.8f+.05f?accent:juce::Colour(0xff2b2722));g.fillRect(810,246+i*7,15,4);}
    label(g,"0 - 12 dB",{775,356,88,15},9,dim,juce::Justification::centred);
    label(g,"Niveaux crete",{875,158,100,18},10,dim,juce::Justification::centred);
    for(int c=0;c<2;++c)
    {
        float level=c==0?inLevel:outLevel;float db=juce::Decibels::gainToDecibels(level,-60.f);int x=898+c*39;
        label(g,c==0?"IN":"OUT",{(float)x-12,185,35,17},9,dim,juce::Justification::centred);
        for(int i=0;i<20;++i){float segment=-48.f+i*2.4f;auto colour=segment>=-1?juce::Colour(0xfff18177):segment>=-6?juce::Colour(0xffe9c074):juce::Colour(0xff829f8a);g.setColour(db>=segment?colour:juce::Colour(0xff26272b));g.fillRect(x,331-i*6,9,4);}
        label(g,db<=-60?"-inf":juce::String(db,1),{(float)x-15,339,40,18},9,dim,juce::Justification::centred);
    }
    label(g,clipHold>0?"CLIP SORTIE":"Clip",{875,362,100,17},9,clipHold>0?juce::Colour(0xffff9385):dim,juce::Justification::centred);
}
void GlueEditor::paint(juce::Graphics& g)
{
    g.fillAll(background);g.addTransform(juce::AffineTransform::scale(scale));g.setColour(border);g.drawHorizontalLine(84,0,1000);g.drawHorizontalLine(659,0,1000);
    g.drawImageWithin(logo,28,26,32,29,juce::RectanglePlacement::centred);
    label(g,"swell",{73,18,90,35},25,text,juce::Justification::left,true);label(g,"A U D I O",{75,50,90,15},8,dim);
    g.setColour(border);g.drawVerticalLine(174,25,58);label(g,"GLUE",{194,22,100,35},24,text,juce::Justification::left,true);
    const auto snapshot=proc.capture();const auto& preset=gluePresets[(size_t)snapshot.profile];
    label(g,preset.description,{27,99,740,25},12,dim);
    bool modified=std::abs(snapshot.glue-preset.glue)>.05f||std::abs(snapshot.mix-preset.mix)>.05f||std::abs(snapshot.output-preset.output)>.05f;
    label(g,modified?"Preset modifie":"Preset usine",{815,99,157,25},11,modified?accent:dim,juce::Justification::right);
    g.setColour(screen);g.fillRoundedRectangle({27.f,140.f,946.f,245.f},7);g.setColour(border);g.drawRoundedRectangle({27.f,140.f,946.f,245.f},7,1);g.drawVerticalLine(766,140,385);g.drawVerticalLine(870,140,385);
    label(g,"Signal avant / apres",{45,153,260,22},12,text,juce::Justification::left,true);
    label(g,"Avant",{624,153,50,22},10,original);label(g,"Apres",{697,153,50,22},10,accent);g.setColour(original);g.drawLine(607,164,618,164,2);g.setColour(accent);g.drawLine(680,164,691,164,2);
    drawScope(g);drawMeters(g);
    auto status=paused?"Courbe figee":bypass.getToggleState()?"Bypass / signal original":"Signal audio / entree et sortie alignees";
    label(g,status,{45,344,590,24},10,dim);
    label(g,"Mix",{235,431,140,22},12,text,juce::Justification::centred,true);
    label(g,"Output",{625,431,140,22},12,text,juce::Justification::centred,true);
    label(g,"GLUE",{403,602,194,32},21,text,juce::Justification::centred,true);
    label(g,"De la cohesion au caractere",{350,635,300,18},10,dim,juce::Justification::centred);
    label(g,"Original",{246,599,65,18},9,dim);label(g,"Traite",{314,599,52,18},9,dim,juce::Justification::right);
    label(g,"-12 dB",{635,599,65,18},9,dim);label(g,"+12 dB",{698,599,60,18},9,dim,juce::Justification::right);
    label(g,messageTicks>0?message:preset.character,{27,676,500,33},11,accent);
}
