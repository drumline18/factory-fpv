#include "DroneDamage.h"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace factoryfpv;
int main() {
    int checks=0;
    auto check=[&](bool value,const char* name) { if(!value) throw std::runtime_error(name);++checks; };
    auto near=[&](double a,double b,double tolerance,const char* name) { check(std::abs(a-b)<=tolerance,name); };
    auto enabled=[] { DroneDamage model;model.parameters.enabled=true;model.reset();return model; };
    auto finite=[](const DroneDamage& model) { return std::isfinite(model.integrityPercent()) && std::isfinite(model.powerFraction()) &&
        model.integrityPercent()>=0 && model.integrityPercent()<=100 && model.powerFraction()>=0 && model.powerFraction()<=1; };

    DroneDamage disabled;
    near(disabled.reportImpact(100,.65),0,0,"disabled collisions never damage");
    near(disabled.integrityPercent(),100,0,"disabled health is intact");
    near(disabled.powerFraction(),1,0,"disabled model never reduces baseline thrust");
    check(!disabled.destroyed(),"disabled model never blocks arming");

    auto gentle=enabled();
    near(gentle.reportImpact(2.99,.65),0,0,"gentle contact does not damage");
    near(gentle.reportImpact(3,.65),0,0,"threshold itself is harmless");
    near(gentle.reportImpact(-100,.65),0,0,"departing surface is not an inward crash");
    near(gentle.integrityPercent(),100,0,"harmless contacts retain structure");

    auto impact=enabled();
    const double energy=.5*.65*(25-9);
    const double expectedLoss=energy/30*100;
    near(impact.reportImpact(5,.65),expectedLoss,1e-12,"impact consumes excess normal kinetic energy once");
    near(impact.integrityPercent(),100-expectedLoss,1e-12,"reported loss matches remaining structure");
    near(impact.powerFraction(),std::sqrt((100-expectedLoss)/100),1e-12,"damaged frame has reduced usable thrust");
    check(!impact.destroyed(),"moderate hit leaves recoverable frame");
    near(impact.reportImpact(5,.65),0,0,"duplicate contact report does not stack damage");
    for(int index=0;index<100;++index) { impact.step(.001);impact.reportImpact(5,.65); }
    near(impact.integrityPercent(),100-expectedLoss,1e-12,"continuous repeated reports remain one contact");
    impact.step(.151);
    near(impact.reportImpact(5,.65),expectedLoss,1e-12,"separate later collision damages again");

    auto light=enabled(),heavy=enabled(),faster=enabled();
    const double lossLight=light.reportImpact(5,.65);
    near(heavy.reportImpact(5,1.3),lossLight*2,1e-12,"mass contributes once to crash energy");
    near(faster.reportImpact(7,.65)/lossLight,(49.-9)/(25.-9),1e-12,"normal-speed severity follows energy rather than linear speed");

    auto peak=enabled(),single=enabled();
    peak.reportImpact(5,.65);
    check(peak.reportImpact(9,.65)>0,"a more severe contact is not suppressed by cooldown");
    single.reportImpact(9,.65);
    near(peak.integrityPercent(),single.integrityPercent(),1e-12,"one contact charges its peak energy without double counting");
    near(peak.reportImpact(4,.65),0,0,"less severe contact tail does not add damage");

    auto shortSteps=enabled(),longStep=enabled();
    shortSteps.reportImpact(5,.65);longStep.reportImpact(5,.65);
    for(int index=0;index<16;++index) shortSteps.step(.01);
    longStep.step(.16);
    shortSteps.reportImpact(5,.65);longStep.reportImpact(5,.65);
    near(shortSteps.integrityPercent(),longStep.integrityPercent(),1e-12,"contact-window expiration does not depend on tick partition");

    auto wreck=enabled();
    near(wreck.reportImpact(11,.65),100,0,"severe single collision exhausts structural budget");
    check(wreck.destroyed(),"destroyed flag is latched until reset");
    near(wreck.integrityPercent(),0,0,"destroyed integrity never goes negative");
    near(wreck.powerFraction(),0,0,"destroyed frame produces no usable thrust");
    wreck.step(100);
    near(wreck.reportImpact(50,5),0,0,"subsequent crashes cannot create extra negative health");
    check(wreck.destroyed(),"elapsed time does not repair structure");
    wreck.reset();
    check(!wreck.destroyed() && wreck.integrityPercent()==100 && wreck.powerFraction()==1,"recovery reset restores frame and thrust");
    near(wreck.reportImpact(5,.65),expectedLoss,1e-12,"reset also clears old contact-window peak");

    auto repeated=enabled();
    for(int index=0;index<20;++index) { repeated.reportImpact(5,.65);repeated.step(.2); }
    check(repeated.destroyed() && finite(repeated),"repeated distinct collisions eventually destroy frame without overflow");

    const double nan=std::numeric_limits<double>::quiet_NaN(),infinity=std::numeric_limits<double>::infinity();
    auto invalid=enabled();
    near(invalid.reportImpact(nan,.65)+invalid.reportImpact(infinity,.65)+invalid.reportImpact(5,nan)+invalid.reportImpact(5,infinity)+
        invalid.reportImpact(5,0)+invalid.reportImpact(5,-1),0,0,"invalid speed/mass cannot invent damage");
    near(invalid.integrityPercent(),100,0,"invalid input preserves finite intact structure");
    invalid.reportImpact(5,.65);invalid.step(nan);invalid.step(infinity);invalid.step(-1);
    near(invalid.reportImpact(5,.65),0,0,"invalid time cannot fabricate a fresh collision window");
    invalid.step(1e300);
    check(invalid.reportImpact(5,.65)>0 && finite(invalid),"large finite time expires window safely");
    auto extreme=enabled();
    extreme.reportImpact(std::numeric_limits<double>::max(),std::numeric_limits<double>::max());
    check(extreme.destroyed() && finite(extreme),"finite pathological collisions cannot overflow state");
    auto malformed=enabled();
    malformed.parameters.damageThresholdMps=nan;malformed.parameters.destructionEnergyJoules=nan;malformed.parameters.contactWindowSeconds=nan;
    near(malformed.reportImpact(5,.65),expectedLoss,1e-12,"nonfinite parameters use stable defaults");
    malformed.parameters.enabled=false;malformed.step(.1);malformed.parameters.enabled=true;
    near(malformed.integrityPercent(),100,0,"disabled advance clears obsolete damage state");
    auto noWindow=enabled();noWindow.parameters.contactWindowSeconds=0;
    noWindow.reportImpact(5,.65);noWindow.reportImpact(5,.65);
    near(noWindow.integrityPercent(),100-2*expectedLoss,1e-12,"zero contact window explicitly counts separate reports");
    std::cout<<checks<<" simulated damage checks passed\n";
}
