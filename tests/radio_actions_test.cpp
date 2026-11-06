#include "RadioActions.h"
#include <iostream>
#include <stdexcept>
using namespace factoryfpv;

int main() {
    int count=0;
    auto check=[&](bool value,const char* name) { if(!value) throw std::runtime_error(name);++count; };
    RadioActionGate gate;
    check(!gate.update(ArmPosition::On),"startup-held switch does not trigger an action");
    check(!gate.update(ArmPosition::Middle),"middle position is not a release");
    check(!gate.update(ArmPosition::On),"middle-on cannot prime action");
    check(!gate.update(ArmPosition::Off),"released control only primes action");
    check(gate.update(ArmPosition::On),"fresh off-on triggers action");
    check(!gate.update(ArmPosition::On),"held control does not repeat action");
    gate.update(ArmPosition::Off);
    check(!gate.update(ArmPosition::Middle),"middle does not trigger action");
    check(gate.update(ArmPosition::On),"three-position off-middle-on triggers once");
    check(!gate.update(ArmPosition::Middle) && !gate.update(ArmPosition::On),"middle does not retrigger consumed action");
    gate.update(ArmPosition::Off);gate.reset();
    check(!gate.update(ArmPosition::On),"focus or pause reset requires a new release");
    gate.update(ArmPosition::Off);gate.update(ArmPosition::Lost);
    check(!gate.update(ArmPosition::On),"disconnect clears release observed before loss");
    gate.update(ArmPosition::Off);
    check(gate.update(ArmPosition::On),"release after reconnect permits fresh action");
    gate.reset();
    check(!gate.update(ArmPosition::On),"new flight cannot repeat prior return or recovery switch");
    gate.update(ArmPosition::Off);gate.update(ArmPosition::Off);
    check(gate.update(ArmPosition::On) && !gate.update(ArmPosition::On),"repeated off samples still produce one action");

    std::array<long,8> axes{};
    std::array<bool,128> buttons{};
    ArmBinding button{1,7,0,1},reverseButton{1,7,1,0};
    gate.reset();
    gate.update(button.position(axes,buttons,true));buttons[7]=true;
    check(gate.update(button.position(axes,buttons,true)),"normal HID button drives event");
    check(!gate.update(button.position(axes,buttons,true)),"held HID button cannot repeat");
    gate.reset();gate.update(reverseButton.position(axes,buttons,true));buttons[7]=false;
    check(gate.update(reverseButton.position(axes,buttons,true)),"inverted button drives fresh event");
    ArmBinding axis{2,4,65535,0};
    axes[4]=65535;gate.reset();gate.update(axis.position(axes,buttons,true));axes[4]=32767;
    check(!gate.update(axis.position(axes,buttons,true)),"auxiliary switch middle is inactive");
    axes[4]=0;
    check(gate.update(axis.position(axes,buttons,true)),"reversed auxiliary switch drives event");
    axes[4]=65535;gate.update(axis.position(axes,buttons,true));axes[4]=-30000;
    check(!gate.update(axis.position(axes,buttons,true)),"out-of-range input is invalid");
    axes[4]=0;
    check(!gate.update(axis.position(axes,buttons,true)),"invalid input clears earlier release");
    axes[4]=65535;gate.update(axis.position(axes,buttons,true));
    gate.update(axis.position(axes,buttons,false));axes[4]=0;
    check(!gate.update(axis.position(axes,buttons,true)),"real HID disconnect invalidates gate");
    ArmBinding unbound;
    gate.update(ArmPosition::Off);
    check(!gate.update(unbound.position(axes,buttons,true)) && !gate.update(ArmPosition::On),"cleared binding cannot retain primed event");

    const std::array<int,4> sticks{0,1,3,2};
    std::array<ArmBinding,RadioActionCount> actions{};
    check(validateRadioBindings({},actions,sticks).valid(),"all optional controls default unbound");
    check(validateRadioBindings(button,actions,sticks).valid(),"existing arm-only profile remains valid");
    actions={ArmBinding{1,8,0,1},ArmBinding{2,4,65535,0},ArmBinding{1,9,1,0}};
    check(validateRadioBindings(button,actions,sticks).valid(),"three distinct actions coexist with arm");
    check(samePhysicalControl(button,reverseButton),"inversion does not distinguish physical control");
    check(!samePhysicalControl(ArmBinding{1,4,0,1},ArmBinding{2,4,0,65535}),"button and axis namespaces are distinct");
    check(!samePhysicalControl({},{}),"unbound controls never conflict");
    actions[0]=reverseButton;
    auto result=validateRadioBindings(button,actions,sticks);
    check(result.issue==RadioBindingIssue::Conflict && result.first==0 && result.second==1,"return cannot share reversed arm control");
    actions[0]={1,8,0,1};actions[2]={2,4,0,65535};
    result=validateRadioBindings(button,actions,sticks);
    check(result.issue==RadioBindingIssue::Conflict && result.first==2 && result.second==3,"mode cannot share recover auxiliary channel");
    actions[2]={1,8,1,0};
    result=validateRadioBindings(button,actions,sticks);
    check(result.issue==RadioBindingIssue::Conflict && result.first==1 && result.second==3,"two momentary actions cannot share button");
    actions[2]={1,9,0,1};
    for (int channel:sticks) {
        actions[0]={2,channel,0,65535};result=validateRadioBindings(button,actions,sticks);
        check(result.issue==RadioBindingIssue::FlightAxis && result.first==1,"each flight axis excluded from actions");
    }
    actions[0]={1,8,0,1};
    result=validateRadioBindings(ArmBinding{2,0,0,65535},actions,sticks);
    check(result.issue==RadioBindingIssue::FlightAxis && result.first==0,"edited arm binding cannot reuse flight axis");
    actions[0]={3,8,0,1};
    result=validateRadioBindings(button,actions,sticks);
    check(result.issue==RadioBindingIssue::Invalid && result.first==1,"unknown action binding kind rejected");
    actions[0]={1,128,0,1};
    check(validateRadioBindings(button,actions,sticks).issue==RadioBindingIssue::Invalid,"out-of-range button rejected");
    actions[0]={1,8,1,1};
    check(validateRadioBindings(button,actions,sticks).issue==RadioBindingIssue::Invalid,"identical button endpoints rejected");
    actions[0]={2,8,0,65535};
    check(validateRadioBindings(button,actions,sticks).issue==RadioBindingIssue::Invalid,"out-of-range auxiliary axis rejected");
    actions[0]={2,4,1000,1010};
    check(validateRadioBindings(button,actions,sticks).issue==RadioBindingIssue::Invalid,"insufficient auxiliary range rejected");
    actions[0]={0,7,1,1};
    check(validateRadioBindings(button,actions,sticks).valid(),"disabled legacy fields remain harmless");

    // Distinct gates must not leak OFF priming or ON events between actions.
    std::array<RadioActionGate,RadioActionCount> gates;
    gates[static_cast<int>(RadioAction::Recover)].update(ArmPosition::Off);
    check(!gates[static_cast<int>(RadioAction::Return)].update(ArmPosition::On),"recover release cannot prime return");
    check(gates[static_cast<int>(RadioAction::Recover)].update(ArmPosition::On),"recovery switch fires own action");
    check(!gates[static_cast<int>(RadioAction::Recover)].update(ArmPosition::On),"held recovery cannot repeat after collision recovery");
    gates[static_cast<int>(RadioAction::ToggleMode)].update(ArmPosition::Off);
    for (auto& item:gates) item.reset();
    check(!gates[static_cast<int>(RadioAction::ToggleMode)].update(ArmPosition::On),"suspending all actions clears pending flight-mode event");

    // The shared endpoint learner excludes sticks for optional actions too.
    std::array<long,8> off{},on{};
    std::array<bool,128> offButtons{},onButtons{};
    std::array<RadioAxisInfo,8> info{};
    for(auto& item:info) item={true,0,65535};
    ArmBinding learned;
    on[0]=65535;
    check(!learnArmBinding(off,offButtons,on,onButtons,info,sticks,learned),"shared learner ignores flight-axis movement");
    onButtons[7]=true;
    check(learnArmBinding(off,offButtons,on,onButtons,info,sticks,learned),"shared learner identifies only changed nonflight control");
    actions={learned,ArmBinding{},ArmBinding{}};
    check(validateRadioBindings(button,actions,sticks).issue==RadioBindingIssue::Conflict,"learned control still checked against arm");
    std::cout<<count<<" radio action checks passed\n";
}
