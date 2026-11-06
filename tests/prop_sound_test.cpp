#include "PropSoundCore.h"
#include <vector>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <limits>

using namespace factoryfpv;
static int checks=0;
static void check(bool result,const char* text) { ++checks; if (!result) throw std::runtime_error(text); }
static double rms(const std::vector<float>& values) {
    double sum=0; for (float value:values) sum+=value*value;
    return std::sqrt(sum/values.size());
}
static double rmsDifference(const std::vector<float>& first,const std::vector<float>& second) {
    double sum=0;
    for (std::size_t i=0;i<first.size();++i) {
        const double difference=first[i]-second[i];
        sum+=difference*difference;
    }
    return std::sqrt(sum/first.size());
}
static double dominant(const std::vector<float>& values) {
    double best=0,frequency=0;
    std::vector<double> window(values.size());
    for (std::size_t i=0;i<values.size();++i) window[i]=values[i]*(.5-.5*std::cos(6.283185307179586*i/(values.size()-1)));
    for (int hz=80;hz<=1200;++hz) {
        const double coefficient=2*std::cos(6.283185307179586*hz/48000.);
        double a=0,b=0;
        for (double value:window) { const double next=value+coefficient*a-b; b=a; a=next; }
        const double energy=a*a+b*b-coefficient*a*b;
        if (energy>best) { best=energy; frequency=hz; }
    }
    return frequency;
}
static void writeDemo(const char* path) {
    constexpr std::uint32_t Rate=48000,Count=Rate*9;
    PropSoundCore sound;
    std::vector<float> audio(Count);
    for (std::uint32_t begin=0;begin<Count;begin+=128) {
        const double t=begin/static_cast<double>(Rate);
        PropSoundParameters p;
        p.armed=t>=.4 && t<8.2;
        p.volume=.65;
        p.thrust=t<1.4 ? 0 : (t<3.4 ? (t-1.4)*.125 : (t<4.4 ? .25 :
            (t<6.2 ? .25+(t-4.4)*.36 : .2)));
        p.roll=t>6.2 && t<7.5 ? .8 : 0;
        sound.set(p);
        sound.render(audio.data()+begin,static_cast<int>(std::min<std::uint32_t>(128,Count-begin)));
    }
    std::ofstream out(path,std::ios::binary);
    auto u16=[&](std::uint16_t value) { out.put(static_cast<char>(value&255)); out.put(static_cast<char>(value>>8)); };
    auto u32=[&](std::uint32_t value) { u16(static_cast<std::uint16_t>(value&65535)); u16(static_cast<std::uint16_t>(value>>16)); };
    out.write("RIFF",4); u32(36+Count*2); out.write("WAVEfmt ",8); u32(16);
    u16(1); u16(1); u32(Rate); u32(Rate*2); u16(2); u16(16); out.write("data",4); u32(Count*2);
    for (float value:audio) u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(value*32767))));
    if (!out) throw std::runtime_error("Could not write prop sound preview");
}
int main(int argc,char** argv) {
    try {
        PropSoundCore sound;
        std::vector<float> buffer(24000);
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"disarmed starts silent");
        PropSoundParameters p; p.armed=true; p.thrust=.08; p.volume=.7;
        sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        const double lowRms=rms(buffer),lowFrequency=dominant(buffer);
        check(lowRms>.001,"armed motor is audible");
        p.thrust=.8; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)>lowRms*1.2,"thrust raises sound level");
        const double highFrequency=dominant(buffer);
        std::cout<<"Measured tonal peaks: "<<lowFrequency<<" -> "<<highFrequency<<" Hz\n";
        check(highFrequency>lowFrequency*1.6,"thrust raises measured blade-pass pitch");
        double peak=0; bool finite=true;
        for (float value:buffer) { finite &= std::isfinite(value); peak=std::max(peak,std::abs(static_cast<double>(value))); }
        check(finite && peak<.8,"output is finite and has headroom");
        p.armed=false; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"disarming fades completely to silence");
        p.armed=true; p.volume=0; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"zero volume is silent");
        p.volume=.7; p.thrust=std::numeric_limits<double>::quiet_NaN(); sound.set(p);
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"invalid thrust cannot produce sound");

        const auto renderAtSpeed=[&](double speedMps) {
            PropSoundCore fresh;
            PropSoundParameters parameters{true,.08,.7};
            parameters.speedMps=speedMps;
            fresh.set(parameters);
            std::vector<float> samples(buffer.size());
            fresh.render(samples.data(),static_cast<int>(samples.size()));
            return samples;
        };
        const auto stopped=renderAtSpeed(0), fast=renderAtSpeed(28);
        check(stopped==renderAtSpeed(4),"hover and slow travel keep the original motor sound");
        check(stopped==renderAtSpeed(std::numeric_limits<double>::quiet_NaN()),"invalid speed adds no air rush");
        const double fastRush=rmsDifference(stopped,fast);
        check(fastRush>.005 && fastRush<.025,"fast coasting adds restrained air rush at the same low thrust");
        const double moderateRush=rmsDifference(stopped,renderAtSpeed(16));
        check(moderateRush>0 && moderateRush<fastRush,"air rush grows with speed");
        check(fast==renderAtSpeed(280),"air rush caps at high speed");

        p={true,.08,.7}; p.speedMps=28;
        sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        p.armed=false; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"disarming fades out even while still moving fast");
        p.armed=true; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        p.volume=0; sound.set(p); sound.render(buffer.data(),static_cast<int>(buffer.size()));
        sound.render(buffer.data(),static_cast<int>(buffer.size()));
        check(rms(buffer)==0,"zero volume also silences active air rush");

        PropSoundCore a,b; p={true,.5,.7}; p.speedMps=18; a.set(p); b.set(p);
        std::vector<float> one(48000),chunks(48000);
        a.render(one.data(),48000);
        for (int begin=0;begin<48000;begin+=257) b.render(chunks.data()+begin,std::min(257,48000-begin));
        check(one==chunks,"audio is continuous and invariant to callback buffer boundaries");
        if (argc>1) writeDemo(argv[1]);
        std::cout<<checks<<" prop audio checks passed\n";
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
