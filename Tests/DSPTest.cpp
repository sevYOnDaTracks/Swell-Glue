#include "../Source/GlueDSP.h"
#include <iostream>

double measure(int preset,float amount,double rate)
{
    GlueDSP dsp;dsp.prepare(rate,preset);double energy=0;
    for(int i=0;i<(int)(rate*.45);++i)
    {
        float source=(float)(.65*std::sin(2*3.141592653589793*440*i/rate));float frame[2]{source,source};dsp.tick(frame,2,amount);
        if(!std::isfinite(frame[0])||std::abs(frame[0])>1||frame[0]!=frame[1])throw 1;
        if(amount==0&&frame[0]!=source)throw 2;
        if(i>rate*.25)energy+=frame[0]*frame[0];
    }
    return energy;
}
int main()
{
    try {
        for(double rate:{44100.,48000.,176400.,192000.,384000.})for(int preset=0;preset<6;++preset)
        {
            double dry=measure(preset,0,rate),gentle=measure(preset,.35f,rate),strong=measure(preset,1,rate);
            if(!(strong<gentle&&gentle<dry))throw 3;
            GlueDSP dsp;dsp.prepare(rate,preset);
            for(int i=0;i<1000;++i){float silence[2]{};dsp.tick(silence,2,1);if(silence[0]!=0||silence[1]!=0)throw 4;}
            for(int i=0;i<10000;++i){float frame[2]{float(.4*std::sin(i*.2)),0};dsp.tick(frame,2,.7f);if(frame[1]!=0)throw 5;}
            std::cout<<rate<<" Hz / "<<gluePresets[(size_t)preset].name<<": compression "<<10*std::log10(strong/dry)<<" dB\n";
        }
        GlueDSP dsp;dsp.prepare(192000,0);
        for(int i=0;i<50000;++i){if(i%2000==0)dsp.setProfile((i/2000)%6);float v=(float)std::sin(i*.1);float frame[2]{v,-v};dsp.tick(frame,2,(i%10000)/10000.f);if(!std::isfinite(frame[0])||std::abs(frame[0])>1.001f||std::abs(frame[0]+frame[1])>1.e-6f)throw 6;}
        std::cout<<"PASS: six presets, five sample rates, unity at zero, silence, linked stereo, crosstalk and transitions.\n";
    }catch(int code){std::cerr<<"Failure "<<code<<'\n';return code;}return 0;
}
