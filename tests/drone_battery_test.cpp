#include "DroneBattery.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace factoryfpv;
int main() {
    int checks=0;
    auto check=[&](bool value,const char* name) { if (!value) throw std::runtime_error(name); ++checks; };
    auto near=[&](double a,double b,double tolerance,const char* name) { check(std::abs(a-b)<=tolerance,name); };
    auto finite=[&](const DroneBattery& battery) {
        return std::isfinite(battery.packVoltage()) && std::isfinite(battery.currentAmps()) && std::isfinite(battery.powerWatts()) &&
            std::isfinite(battery.remainingPercent()) && std::isfinite(battery.consumedMah());
    };
    auto enabled=[] {
        DroneBattery battery;
        battery.parameters.enabled=true;
        battery.reset();
        return battery;
    };
    DroneBattery off;
    off.reset();off.step(60,true,1);
    near(off.consumedMah(),0,0,"disabled simulation never drains");
    check(!off.low() && !off.critical() && !off.empty(),"disabled simulation never raises battery alerts");
    near(off.packVoltage(),0,0,"disabled simulation has no invented pack reading");

    auto fresh=enabled();
    near(fresh.packVoltage(),16.8,1e-10,"full 4S open-circuit voltage");
    near(fresh.remainingPercent(),100,0,"fresh pack full");
    fresh.step(60,false,1);
    near(fresh.consumedMah(),0,0,"disarmed default does not drain despite stale thrust");
    fresh.step(1,true,0);
    check(fresh.currentAmps()>0 && fresh.powerWatts()==fresh.parameters.armedIdleWatts,"armed motors have estimated idle draw");
    fresh.step(1,false,0);
    near(fresh.currentAmps(),0,0,"disarm removes motor draw");

    auto light=enabled(),heavy=enabled();
    light.step(.5,true,.25);heavy.step(.5,true,1);
    check(heavy.currentAmps()>light.currentAmps() && heavy.consumedMah()>light.consumedMah(),"higher thrust draws more current/charge");
    check(heavy.packVoltage()<light.packVoltage(),"higher load sags pack voltage");
    near(heavy.currentAmps()*heavy.packVoltage(),heavy.powerWatts(),1e-9,"terminal voltage/current conserve electrical power");
    const double terminalBeforeDisarm=heavy.packVoltage();
    const double usedBeforeDisarm=heavy.consumedMah();
    heavy.step(.5,false,0);
    check(heavy.packVoltage()>terminalBeforeDisarm,"voltage rebounds at rest");
    near(heavy.consumedMah(),usedBeforeDisarm,0,"voltage rebound does not restore spent charge");

    auto noResistance=enabled();
    noResistance.parameters.cellResistanceOhms=0;
    noResistance.reset();noResistance.step(1,true,.5);
    auto resistance=enabled();resistance.step(1,true,.5);
    check(resistance.packVoltage()<noResistance.packVoltage() && resistance.currentAmps()>noResistance.currentAmps(),"series resistance increases current needed for equal power");

    auto coarse=enabled(),fine=enabled();
    for (int i=0;i<120;++i) coarse.step(.5,true,.35);
    for (int i=0;i<14400;++i) fine.step(1./240,true,.35);
    near(coarse.consumedMah(),fine.consumedMah(),.002,"charge integration stable across frame rates");
    near(coarse.packVoltage(),fine.packVoltage(),.0001,"voltage stable across frame rates");
    auto longTick=enabled();longTick.step(60,true,.35);
    near(longTick.consumedMah(),fine.consumedMah(),.002,"bounded long tick agrees with fixed-rate integration");
    double integratedMah=0;
    auto conservation=enabled();
    for (int i=0;i<7200;++i) {
        conservation.step(1./240,true,.4);
        integratedMah+=conservation.currentAmps()*(1000./3600.)/240.;
    }
    near(conservation.consumedMah(),integratedMah,.01,"reported current integrates to consumed mAh");

    auto hovering=enabled();
    double hoverSeconds=0;
    while (!hovering.empty() && hoverSeconds<1800) { hovering.step(.25,true,.25);hoverSeconds+=.25; }
    check(hovering.empty() && hoverSeconds>300 && hoverSeconds<1200,"default hover endurance is sensible minutes");
    check(hovering.low() && hovering.critical(),"depleted pack has low/critical alerts");
    near(hovering.consumedMah(),hovering.parameters.capacityMah,0,"empty cutoff is actual capacity");
    near(hovering.remainingPercent(),0,0,"depleted percentage is bounded at zero");
    near(hovering.currentAmps(),0,0,"empty pack cannot supply further current");
    const double usedAtEmpty=hovering.consumedMah();
    hovering.step(60,true,1);
    near(hovering.consumedMah(),usedAtEmpty,0,"spent charge cannot grow past capacity");
    hovering.reset();
    check(!hovering.empty() && !hovering.low() && !hovering.critical(),"reset replaces pack and clears alerts");
    near(hovering.remainingPercent(),100,0,"reset is full");

    auto threshold=enabled();
    bool sawLowOnly=false,sawCritical=false;
    for (int i=0;i<1000 && !threshold.empty();++i) {
        threshold.step(.5,true,1);
        sawLowOnly |= threshold.low() && !threshold.critical();
        sawCritical |= threshold.critical() && !threshold.empty();
    }
    check(sawLowOnly && sawCritical,"warnings precede depletion at separate thresholds");

    auto idle=enabled();idle.parameters.disarmedIdleWatts=4;idle.reset();idle.step(10,false,0);
    check(idle.consumedMah()>0 && idle.currentAmps()>0,"optional disarmed electronics load works");
    auto four=enabled(),six=enabled();six.parameters.cells=6;six.reset();
    four.step(10,true,.5);six.step(10,true,.5);
    check(six.packVoltage()>four.packVoltage() && six.consumedMah()<four.consumedMah(),"more series cells supply equal watts at lower current");
    auto small=enabled(),large=enabled();large.parameters.capacityMah=3000;large.reset();
    small.step(10,true,.5);large.step(10,true,.5);
    check(large.remainingPercent()>small.remainingPercent(),"larger capacity retains more charge percentage");

    auto invalid=enabled();
    for (double dt:{0.,-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),1e20}) invalid.step(dt,true,1);
    near(invalid.consumedMah(),0,0,"invalid/unbounded timesteps do not fabricate elapsed load");
    invalid.step(1,true,std::numeric_limits<double>::quiet_NaN());
    near(invalid.consumedMah(),0,0,"invalid thrust cannot drain or poison state");
    invalid.parameters.capacityMah=std::numeric_limits<double>::quiet_NaN();
    invalid.parameters.maxPowerWatts=std::numeric_limits<double>::infinity();
    invalid.parameters.fullCellVoltage=-100;invalid.parameters.emptyCellVoltage=100;
    invalid.parameters.cellResistanceOhms=-1;invalid.parameters.cells=999;
    invalid.parameters.lowPercent=std::numeric_limits<double>::quiet_NaN();
    invalid.reset();invalid.step(10,true,1);
    check(finite(invalid) && invalid.currentAmps()>=0 && invalid.packVoltage()>0,"invalid parameters bounded with finite fallback");
    auto extreme=enabled();
    extreme.parameters.cells=1;extreme.parameters.cellResistanceOhms=.1;extreme.parameters.maxPowerWatts=20000;
    extreme.reset();extreme.step(1,true,1);
    check(extreme.powerLimited() && finite(extreme) && extreme.packVoltage()>0,"impossible power demand saturates without imaginary voltage/current");
    near(extreme.currentAmps()*extreme.packVoltage(),extreme.powerWatts(),1e-9,"saturated output still conserves terminal power");
    auto drainStress=enabled();
    drainStress.parameters.cells=1;drainStress.parameters.capacityMah=50;
    drainStress.parameters.cellResistanceOhms=0;drainStress.parameters.maxPowerWatts=20000;
    drainStress.reset();drainStress.step(60,true,1);
    check(drainStress.empty() && finite(drainStress),"maximum load/minimum capacity cannot overflow on a long tick");
    near(drainStress.consumedMah(),50,0,"large consumption increment is clipped exactly at charge capacity");
    auto highInput=enabled(),clamped=enabled();highInput.step(1,true,100);clamped.step(1,true,1);
    near(highInput.consumedMah(),clamped.consumedMah(),0,"excess normalized thrust clamps safely");

    std::cout<<checks<<" simulated battery checks passed; default hover endurance "<<hoverSeconds/60<<" min\n";
}
