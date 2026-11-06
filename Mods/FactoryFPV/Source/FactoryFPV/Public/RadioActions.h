#pragma once
#include "RadioArm.h"
#include <array>

namespace factoryfpv {
enum class RadioAction { Return=0, Recover=1, ToggleMode=2 };
constexpr int RadioActionCount=3;

// Optional actions are events, never an absolute switch position. Every flight,
// focus loss, pause or disconnect must reset the gate; a held switch cannot
// immediately return/recover the drone or change its flight mode on resuming.
class RadioActionGate {
    bool sawOff=false;
public:
    void reset() { sawOff=false; }
    bool update(ArmPosition position) {
        if (position==ArmPosition::Lost) { reset(); return false; }
        if (position==ArmPosition::Off) { sawOff=true; return false; }
        if (position==ArmPosition::Middle) return false;
        const bool request=sawOff;
        sawOff=false;
        return request;
    }
};

// Captured direction and endpoint ranges do not distinguish physical controls.
// An inverted copy of the arm switch must not also become a return control.
inline bool samePhysicalControl(const ArmBinding& first,const ArmBinding& second) {
    return (first.kind==1 || first.kind==2) && first.kind==second.kind && first.index==second.index;
}

enum class RadioBindingIssue { None, Invalid, FlightAxis, Conflict };
struct RadioBindingValidation {
    RadioBindingIssue issue=RadioBindingIssue::None;
    // Slots: 0 arm, 1 return, 2 recover, 3 flight-mode toggle.
    int first=-1,second=-1;
    bool valid() const { return issue==RadioBindingIssue::None; }
};

// Validate loaded settings as well as newly learned controls. The learner
// already ignores flight axes; checking again catches edited/stale INI files
// and flight-axis recalibration that reuses a formerly auxiliary channel.
inline RadioBindingValidation validateRadioBindings(const ArmBinding& arm,
    const std::array<ArmBinding,RadioActionCount>& actions,const std::array<int,4>& flightAxes) {
    const std::array<ArmBinding,RadioActionCount+1> controls{arm,actions[0],actions[1],actions[2]};
    for (int slot=0;slot<RadioActionCount+1;++slot) {
        const auto& binding=controls[slot];
        if (binding.kind==0) continue;
        if (!binding.valid()) return {RadioBindingIssue::Invalid,slot,-1};
        if (binding.kind==2 && std::find(flightAxes.begin(),flightAxes.end(),binding.index)!=flightAxes.end())
            return {RadioBindingIssue::FlightAxis,slot,-1};
        for (int previous=0;previous<slot;++previous) {
            if (samePhysicalControl(binding,controls[previous]))
                return {RadioBindingIssue::Conflict,previous,slot};
        }
    }
    return {};
}
}
