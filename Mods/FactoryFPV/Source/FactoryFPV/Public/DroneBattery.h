#pragma once
#include <algorithm>
#include <cmath>

namespace factoryfpv {
struct DroneBatteryParameters {
    bool enabled=false;
    int cells=4;
    double capacityMah=1500;
    double maxPowerWatts=600;
    double armedIdleWatts=8;
    double disarmedIdleWatts=0;
    double powerExponent=1.1;
    double fullCellVoltage=4.2;
    double emptyCellVoltage=3.3;
    double cellResistanceOhms=.008;
    double lowPercent=20;
    double criticalPercent=10;
};

// Original, deliberately approximate battery simulation, NOT receiver telemetry.
// Load is estimated electrical power from normalized spooled thrust. A monotonic
// state-of-charge curve and series resistance model determine voltage/current;
// integrated current consumes charge. It does not command thrust or model heat,
// chemistry/aging, individual cell imbalance, or an actual radio's battery.
// Configure parameters and reset at launch. One owner/thread advances an instance.
class DroneBattery {
    struct Load { double volts=0,amps=0,watts=0; bool limited=false; };
    double usedMah=0;
    Load output;

    static double bounded(double value,double low,double high,double fallback) {
        return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
    }
    DroneBatteryParameters sanitized() const {
        auto p=parameters;
        p.cells=std::clamp(p.cells,1,12);
        p.capacityMah=bounded(p.capacityMah,50,50000,1500);
        p.maxPowerWatts=bounded(p.maxPowerWatts,1,20000,600);
        p.armedIdleWatts=bounded(p.armedIdleWatts,0,p.maxPowerWatts,std::min(8.,p.maxPowerWatts));
        p.disarmedIdleWatts=bounded(p.disarmedIdleWatts,0,p.maxPowerWatts,0);
        p.powerExponent=bounded(p.powerExponent,.5,2,1.1);
        p.emptyCellVoltage=bounded(p.emptyCellVoltage,2.5,4,3.3);
        p.fullCellVoltage=bounded(p.fullCellVoltage,p.emptyCellVoltage+.2,4.4,std::max(4.2,p.emptyCellVoltage+.2));
        p.cellResistanceOhms=bounded(p.cellResistanceOhms,0,.1,.008);
        p.lowPercent=bounded(p.lowPercent,1,50,20);
        p.criticalPercent=bounded(p.criticalPercent,0,p.lowPercent,std::min(10.,p.lowPercent));
        return p;
    }
    static double openCircuitVoltage(const DroneBatteryParameters& p,double chargeUsed) {
        const double soc=std::clamp(1-chargeUsed/p.capacityMah,0.,1.);
        // Approximate typical pack curve, normalized to the configured endpoints.
        constexpr double charge[]{0,.05,.2,.5,.9,1};
        constexpr double voltage[]{0,.2,.45,.6,.8,1};
        double fraction=0;
        for (int i=1;i<6;++i) if (soc<=charge[i]) {
            const double t=(soc-charge[i-1])/(charge[i]-charge[i-1]);
            fraction=voltage[i-1]+t*(voltage[i]-voltage[i-1]);
            break;
        }
        return p.cells*(p.emptyCellVoltage+(p.fullCellVoltage-p.emptyCellVoltage)*fraction);
    }
    static Load loadAt(const DroneBatteryParameters& p,double chargeUsed,double requestedWatts) {
        const double ocv=openCircuitVoltage(p,chargeUsed);
        if (requestedWatts<=0) return {ocv,0,0,false};
        const double resistance=p.cells*p.cellResistanceOhms;
        if (resistance<1e-12) return {ocv,requestedWatts/ocv,requestedWatts,false};
        // Solve P=I*(OCV-I*R) on the stable, higher-voltage branch. An extreme
        // requested load cannot draw imaginary current or a negative voltage.
        const double maximumPower=ocv*ocv/(4*resistance);
        const double watts=std::min(requestedWatts,maximumPower);
        const double voltage=(ocv+std::sqrt(std::max(0.,ocv*ocv-4*resistance*watts)))*.5;
        return {voltage,watts/voltage,watts,requestedWatts>maximumPower};
    }
public:
    DroneBatteryParameters parameters;

    void reset() {
        usedMah=0;
        const auto p=sanitized();
        output=p.enabled ? loadAt(p,0,0) : Load{};
    }
    void step(double dt,bool armed,double spooledThrust) {
        const auto p=sanitized();
        usedMah=std::clamp(usedMah,0.,p.capacityMah);
        if (!p.enabled) { output={}; return; }
        // An invalid/suspended tick is not fabricated elapsed time or motor load.
        // The flight wrapper normally calls at its 240 Hz simulation interval.
        if (!std::isfinite(dt) || dt<=0 || dt>60 || !std::isfinite(spooledThrust)) {
            output=loadAt(p,usedMah,0); return;
        }
        if (usedMah>=p.capacityMah) { output=loadAt(p,usedMah,0); return; }
        const double thrust=std::clamp(spooledThrust,0.,1.);
        const double requested=armed ? p.armedIdleWatts+(p.maxPowerWatts-p.armedIdleWatts)*std::pow(thrust,p.powerExponent) : p.disarmedIdleWatts;
        if (requested<=0) { output=loadAt(p,usedMah,0); return; }
        // RK2 charge integration limits dependence on the caller's frame rate.
        // At most 3000 bounded substeps even if a caller supplies a 60 s tick.
        const int steps=static_cast<int>(std::ceil(dt/.02));
        const double h=dt/steps;
        for (int i=0;i<steps && usedMah<p.capacityMah;++i) {
            const auto start=loadAt(p,usedMah,requested);
            const double midpoint=std::min(p.capacityMah,usedMah+start.amps*h*(1000./7200.));
            const auto middle=loadAt(p,midpoint,requested);
            usedMah=std::min(p.capacityMah,usedMah+middle.amps*h*(1000./3600.));
        }
        output=loadAt(p,usedMah,usedMah>=p.capacityMah ? 0 : requested);
    }
    bool enabled() const { return parameters.enabled; }
    double remainingPercent() const {
        return enabled() ? std::clamp(100*(1-usedMah/sanitized().capacityMah),0.,100.) : 100.;
    }
    double packVoltage() const { return enabled() ? output.volts : 0.; }
    double currentAmps() const { return enabled() ? output.amps : 0.; }
    double powerWatts() const { return enabled() ? output.watts : 0.; }
    double consumedMah() const { return std::clamp(usedMah,0.,sanitized().capacityMah); }
    bool powerLimited() const { return enabled() && output.limited; }
    bool empty() const { return enabled() && usedMah>=sanitized().capacityMah; }
    bool low() const { return enabled() && remainingPercent()<=sanitized().lowPercent; }
    bool critical() const { return enabled() && remainingPercent()<=sanitized().criticalPercent; }
};
}
