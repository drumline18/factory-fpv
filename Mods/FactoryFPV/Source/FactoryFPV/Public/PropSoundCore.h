#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace factoryfpv {
struct PropSoundParameters {
    bool armed=false;
    double thrust=0;
    double volume=.5;
    double maxRpm=24000;
    double roll=0, pitch=0, yaw=0;
    double speedMps=0;
};

// Original procedural audio: four detuned blade-pass/electrical tones and
// filtered air noise. No recordings or third-party audio assets are used.
// One instance is owned exclusively by the audio-render thread.
class PropSoundCore {
    static constexpr double Tau=6.2831853071795864769;
    static constexpr double AirRushStartMps=4., AirRushFullMps=28.;
    double sampleRate=48000;
    PropSoundParameters target;
    std::array<double,4> bladePhase{0,.9,2.4,4.7}, electricalPhase{.4,2.1,4.4,5.2}, rpm{};
    double gain=0, airRushGain=0, noiseLow=0;
    std::uint32_t random=0x46505631;
    static double bounded(double value,double low,double high,double fallback=0) {
        return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
    }
public:
    explicit PropSoundCore(double rate=48000) : sampleRate(bounded(rate,16000,192000,48000)) {}
    void set(PropSoundParameters value) {
        if (!std::isfinite(value.thrust) || !std::isfinite(value.volume) || !std::isfinite(value.maxRpm)) value.armed=false;
        value.thrust=bounded(value.thrust,0,1);
        value.volume=bounded(value.volume,0,1);
        value.maxRpm=bounded(value.maxRpm,8000,42000,24000);
        value.roll=bounded(value.roll,-1,1);
        value.pitch=bounded(value.pitch,-1,1);
        value.yaw=bounded(value.yaw,-1,1);
        value.speedMps=bounded(value.speedMps,0,AirRushFullMps);
        target=value;
    }
    void render(float* output,int count) {
        if (!output || count<=0) return;
        const bool audible=target.armed && target.volume>0;
        if (!audible && gain<1e-6 && airRushGain<1e-6) {
            std::fill(output,output+count,0.f);
            gain=0; airRushGain=0; rpm.fill(0);
            return;
        }
        const double load=std::sqrt(target.thrust);
        const double targetGain=target.armed ? target.volume*(.22+.78*load) : 0;
        const double gainStep=1-std::exp(-1/(sampleRate*(targetGain>gain ? .025 : .035)));
        const double rpmStep=1-std::exp(-1/(sampleRate*(target.armed ? .045 : .12)));
        // Blend in wind noise from drone speed independently of motor load.
        const double speedFraction=std::max(0.,(target.speedMps-AirRushStartMps)/(AirRushFullMps-AirRushStartMps));
        const double airRushTarget=audible ? target.volume*.13*speedFraction*speedFraction*(3-2*speedFraction) : 0;
        const double airRushStep=1-std::exp(-1/(sampleRate*(audible ? .10 : .035)));
        constexpr double detune[]{-.012,.009,.016,-.007};
        for (int frame=0;frame<count;++frame) {
            gain+=(targetGain-gain)*gainStep;
            airRushGain+=(airRushTarget-airRushGain)*airRushStep;
            double motors=0;
            for (int motor=0;motor<4;++motor) {
                const double difference=detune[motor]+(motor<2 ? 1 : -1)*target.pitch*.025+
                    (motor%2 ? 1 : -1)*target.roll*.025+(motor==0 || motor==3 ? 1 : -1)*target.yaw*.02;
                const double wanted=target.armed ? target.maxRpm*(.18+.82*load)*(1+difference) : 0;
                rpm[motor]+=(wanted-rpm[motor])*rpmStep;
                bladePhase[motor]+=Tau*rpm[motor]*2/(60*sampleRate);
                electricalPhase[motor]+=Tau*rpm[motor]*7/(60*sampleRate);
                if (bladePhase[motor]>=Tau) bladePhase[motor]-=Tau;
                if (electricalPhase[motor]>=Tau) electricalPhase[motor]-=Tau;
                const double p=bladePhase[motor];
                motors+=.58*std::sin(p)+.18*std::sin(2*p)+.08*std::sin(3*p)+
                    .03*std::sin(5*p)+.13*std::sin(electricalPhase[motor]);
            }
            random^=random<<13; random^=random>>17; random^=random<<5;
            const double white=static_cast<double>(random)/4294967295.*2-1;
            noiseLow+=.13*(white-noiseLow);
            const double air=(white-noiseLow)*(.025+.075*load)+noiseLow*.03;
            const double signal=gain*(motors*.085+air)+airRushGain*noiseLow;
            output[frame]=static_cast<float>(std::clamp(signal,-.8,.8));
        }
    }
};
}
