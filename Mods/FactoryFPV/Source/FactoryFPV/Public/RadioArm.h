#pragma once
#include "RadioCalibration.h"
#include <array>
#include <cmath>

namespace factoryfpv {
enum class ArmPosition { Lost, Off, Middle, On };
struct ArmBinding {
    int kind=0; // 0 disabled, 1 HID button, 2 unused HID axis
    int index=-1;
    long off=0,on=1;
    bool valid() const {
        if (kind==1) return index>=0 && index<128 && (off==0 || off==1) && (on==0 || on==1) && off!=on;
        return kind==2 && index>=0 && index<8 && std::abs(double(on)-off)>=100;
    }
    bool available(const std::array<RadioAxisInfo,8>& axes,const std::array<bool,128>& buttons) const {
        return valid() && (kind==1 ? buttons[index] : axes[index].present);
    }
    ArmPosition position(const std::array<long,8>& axes,const std::array<bool,128>& buttons,bool connected) const {
        if (!connected || !valid()) return ArmPosition::Lost;
        const double value=kind==1 ? (buttons[index] ? 1. : 0.) : double(axes[index]);
        const double normalized=(value-off)/(double(on)-off);
        // Values beyond the captured endpoints are invalid rather than armed.
        if (!std::isfinite(normalized) || normalized<-.15 || normalized>1.15) return ArmPosition::Lost;
        if (normalized<=.25) return ArmPosition::Off;
        return normalized>=.75 ? ArmPosition::On : ArmPosition::Middle;
    }
};
struct ArmDecision { bool arm=false,disarm=false; };
class RadioArmGate {
    bool sawOff=false;
public:
    void reset() { sawOff=false; }
    ArmDecision update(ArmPosition position) {
        if (position==ArmPosition::Lost) { reset(); return {false,true}; }
        if (position==ArmPosition::Off) { sawOff=true; return {false,true}; }
        if (position==ArmPosition::Middle) return {false,true};
        const bool request=sawOff;
        sawOff=false; // A rejected high-throttle request cannot arm later while held on.
        return {request,false};
    }
};

// Capture two stationary endpoints; ignore the four calibrated flight axes.
inline bool learnArmBinding(const std::array<long,8>& offAxes,const std::array<bool,128>& offButtons,
    const std::array<long,8>& onAxes,const std::array<bool,128>& onButtons,
    const std::array<RadioAxisInfo,8>& info,const std::array<int,4>& flightAxes,ArmBinding& result) {
    ArmBinding candidate;
    int matches=0;
    for (int i=0;i<128;++i) if (offButtons[i]!=onButtons[i]) {
        candidate={1,i,offButtons[i] ? 1L : 0L,onButtons[i] ? 1L : 0L};++matches;
    }
    for (int i=0;i<8;++i) {
        if (!info[i].present || std::find(flightAxes.begin(),flightAxes.end(),i)!=flightAxes.end()) continue;
        const double span=double(info[i].maximum)-info[i].minimum;
        if (span>0 && std::abs(double(onAxes[i])-offAxes[i])>=span*.3) {
            candidate={2,i,offAxes[i],onAxes[i]};++matches;
        }
    }
    if (matches!=1 || !candidate.valid()) return false;
    result=candidate;
    return true;
}
}
