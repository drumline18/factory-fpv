#pragma once
#include <algorithm>
#include <cmath>

namespace factoryfpv {
struct DroneDamageParameters {
    bool enabled=false;
    double damageThresholdMps=3;
    double destructionEnergyJoules=30;
    double contactWindowSeconds=.15;
};

// Original, optional crash-consequence approximation. Only normal approach
// speed contributes: kinetic energy above a harmless-contact threshold consumes
// a structural budget. This is not a fracture, propeller or materials solver.
// It never damages the pioneer/world or changes FlightCore's collision policy.
// Configure/reset at launch. One owner/thread advances and reports collisions.
class DroneDamage {
    double lossPercent=0;
    double contactRemaining=0;
    double contactPeakJoules=0;

    static double bounded(double value,double low,double high,double fallback) {
        return std::isfinite(value) ? std::clamp(value,low,high) : fallback;
    }
    DroneDamageParameters sanitized() const {
        auto p=parameters;
        p.damageThresholdMps=bounded(p.damageThresholdMps,.5,20,3);
        p.destructionEnergyJoules=bounded(p.destructionEnergyJoules,1,10000,30);
        p.contactWindowSeconds=bounded(p.contactWindowSeconds,0,1,.15);
        return p;
    }
public:
    DroneDamageParameters parameters;

    void reset() { lossPercent=0;contactRemaining=0;contactPeakJoules=0; }
    bool enabled() const { return parameters.enabled; }
    double integrityPercent() const { return enabled() ? std::clamp(100-lossPercent,0.,100.) : 100.; }
    bool destroyed() const { return enabled() && lossPercent>=100; }
    double powerFraction() const {
        // Progressive loss of usable thrust; terminal destruction is exactly 0.
        return enabled() ? std::sqrt(integrityPercent()*.01) : 1.;
    }
    void step(double dt) {
        if (!enabled()) { reset();return; }
        if (!std::isfinite(dt) || dt<=0) return;
        contactRemaining=std::max(0.,contactRemaining-dt);
        if (contactRemaining<=0) contactPeakJoules=0;
    }
    // Pass positive inward normal speed, max(0, -velocity.dot(outwardNormal)),
    // captured BEFORE FlightCore.contact/impact clears its pre-contact velocity.
    // Returns newly applied loss in percentage points, bounded to remaining health.
    double reportImpact(double inwardSpeedMps,double massKg) {
        if (!enabled()) { reset();return 0; }
        if (destroyed() || !std::isfinite(inwardSpeedMps) || inwardSpeedMps<=0 ||
            !std::isfinite(massKg) || massKg<=0) return 0;
        const auto p=sanitized();
        // Bound finite pathological inputs before multiplication can overflow.
        const double speed=std::min(inwardSpeedMps,500.);
        const double mass=std::clamp(massKg,.01,1000.);
        if (speed<=p.damageThresholdMps) return 0;
        const double energy=.5*mass*(speed*speed-p.damageThresholdMps*p.damageThresholdMps);
        if (contactRemaining<=0 || p.contactWindowSeconds==0) contactPeakJoules=0;
        // Repeated sweep reports during one contact do not multiply the same
        // impact. A stronger report still charges its extra energy immediately.
        // A new contact after a quiet window can damage the frame again.
        const double additionalEnergy=std::max(0.,energy-contactPeakJoules);
        contactPeakJoules=std::max(contactPeakJoules,energy);
        contactRemaining=p.contactWindowSeconds;
        const double before=lossPercent;
        lossPercent=std::clamp(lossPercent+additionalEnergy/p.destructionEnergyJoules*100.,0.,100.);
        return lossPercent-before;
    }
};
}
