#include "FlightCore.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace factoryfpv;
int checks=0;
void check(bool ok,const char* name) { ++checks; if(!ok) throw std::runtime_error(name); }
void near(double a,double b,double eps,const char* name) { check(std::abs(a-b)<eps,name); }
void run(FlightCore& d,Input i,int n) { for(int k=0;k<n;++k) d.step(i); }
FlightCore flightAtFps(int fps,bool angleMode=false) {
    FlightCore d; d.parameters.angleMode=angleMode; d.arm({}); Stepper s;
    for(int f=0;f<fps*4;++f) s.advance(1.0/fps,[&](double dt){d.step({.2,-.1,.15,.4,true},dt);});
    return d;
}
void tilt(FlightCore& d,Vec axis,double degrees) {
    axis=axis.normalized();
    d.forward=rotate(d.forward,axis,degrees*Pi/180.);
    d.right=rotate(d.right,axis,degrees*Pi/180.);
    d.up=rotate(d.up,axis,degrees*Pi/180.);
}
double tiltDegrees(const FlightCore& d) { return std::acos(clamp(d.up.z,-1,1))*180./Pi; }
int main() {
    try {
        Calibration c{-100,10,200,false};
        near(c.axis(-100),-1,1e-12,"calibration min");
        near(c.axis(10),0,1e-12,"calibration center");
        near(c.axis(200),1,1e-12,"calibration max");
        c.reversed=true; near(c.axis(-100,true),1,1e-12,"reverse throttle");
        c.center=c.maximum; check(!c.valid(),"invalid calibration");
        near(shape(.01,.3,.03),0,1e-12,"deadzone");
        near(shape(.5,.3,.03),-shape(-.5,.3,.03),1e-12,"expo symmetry");
        near(shape(1,.3,.03),1,1e-12,"full rate");

        const double InvalidCenter=std::numeric_limits<double>::quiet_NaN();
        bool LegacyRateExact=true;
        const double LegacySticks[]{-1.,-.52,-.03,0.,.03,.27,1.};
        const double LegacyExpos[]{0.,.3,.85,1.};
        for (double Expo:LegacyExpos) for (double Stick:LegacySticks) {
            const double Expected=shape(Stick,Expo,.03)*623.;
            LegacyRateExact &= rateCommand(Stick,623.,Expo,.03)==Expected &&
                rateCommand(Stick,623.,Expo,.03,InvalidCenter)==Expected && rateCommand(Stick,623.,Expo,.03,-10.)==Expected;
        }
        check(LegacyRateExact,"disabled and invalid centers retain the exact legacy curve through full Expo");
        near(rateCommand(.03,600.,1.,.03,100.),0.,1e-12,"custom center preserves deadzone");
        check(rateCommand(1.2,600.,1.,.03,100.)==600. && rateCommand(-1.,600.,1.,.03,100.)==-600.,
            "custom center preserves signed full stick maximum");
        near(rateCommand(.001,600.,.85,0.,100.)/.001,100.,1e-6,"custom center sets the near-center slope");
        near(rateCommand(.55,600.,1.,.1,200.),112.5,1e-12,"custom Expo curves the deadzone-remapped outer stick");
        near(rateCommand(.55,600.,0.,.1,200.),200.,1e-12,"less Expo adds more outer rate");
        near(rateCommand(.5,600.,0.,0.,1.),152.5,1e-12,"positive center sensitivity has a minimum");
        near(rateCommand(.5,600.,.85,0.,1200.),300.,1e-12,"center capped to the maximum produces a linear curve");
        double PreviousRate=0.;
        bool MonotonicSymmetric=true;
        for (int Step=1;Step<=100;++Step) {
            const double Stick=Step*.01;
            const double Rate=rateCommand(Stick,600.,.85,.03,125.);
            MonotonicSymmetric &= Rate>=PreviousRate && Rate==-rateCommand(-Stick,600.,.85,.03,125.);
            PreviousRate=Rate;
        }
        check(MonotonicSymmetric,"custom rate increases monotonically and mirrors about center");

        constexpr double Hover=.25, ThrottleExpo=.8;
        const double InvalidThrottleValue=std::numeric_limits<double>::quiet_NaN();
        const auto Curve=[=](double Stick) { return throttleCommand(Stick,Hover,ThrottleExpo); };
        bool LegacyThrottleExact=true;
        const double ThrottleSticks[]{-.2,0.,.025,MaxArmThrottle,.24,Hover,.26,.87,1.,1.2};
        for (double Stick:ThrottleSticks) {
            const double Expected=clamp(Stick,0.,1.);
            LegacyThrottleExact &= throttleCommand(Stick,Hover,0.)==Expected &&
                throttleCommand(Stick,Hover,InvalidThrottleValue)==Expected &&
                throttleCommand(Stick,InvalidThrottleValue,ThrottleExpo)==Expected &&
                throttleCommand(Stick,MaxArmThrottle,ThrottleExpo)==Expected &&
                throttleCommand(Stick,1.,ThrottleExpo)==Expected;
        }
        check(LegacyThrottleExact,"zero or invalid throttle Expo or hover preserves exact linear throttle");
        check(Curve(-1.)==0. && Curve(0.)==0. && Curve(.025)==.025 && Curve(MaxArmThrottle)==MaxArmThrottle &&
            Curve(Hover)==Hover && Curve(1.)==1. && Curve(2.)==1.,"Expo preserves arming region, hover, and full stick exactly");
        check(Curve(InvalidThrottleValue)==0. && Curve(std::numeric_limits<double>::infinity())==0.,"nonfinite throttle fails closed");
        check(throttleCommand(.31,Hover,9.)==Curve(.31),"throttle Expo is capped at .8");
        check(.24<Curve(.24) && Curve(.24)<Hover && Hover<Curve(.26) && Curve(.26)<.26,
            "throttle Expo pulls both sides of hover toward the hover command");
        bool MonotonicThrottle=true;
        const double HoverSettings[]{.1,Hover,1./1.2};
        for (double HoverPoint:HoverSettings) {
            double Previous=0.;
            for (int Step=1;Step<=1000;++Step) {
                const double Command=throttleCommand(Step*.001,HoverPoint,ThrottleExpo);
                MonotonicThrottle &= Command>Previous && Command<=1.;
                Previous=Command;
            }
        }
        check(MonotonicThrottle,"maximum Expo remains monotonic across the supported hover range");
        constexpr double SlopeStep=1.e-6;
        auto Slope=[&](double From,double To) { return (Curve(To)-Curve(From))/(To-From); };
        near(Slope(MaxArmThrottle,MaxArmThrottle+SlopeStep),1.,2.e-5,"Expo matches the original slope at the arming threshold");
        near(Slope(Hover-SlopeStep,Hover),1.-ThrottleExpo,2.e-5,"Expo softens the slope just below hover");
        near(Slope(Hover,Hover+SlopeStep),1.-ThrottleExpo,2.e-5,"Expo matches the softened slope just above hover");
        near(Slope(1.-SlopeStep,1.),1.,2.e-5,"Expo matches the original one-sided slope at full throttle");

        FlightCore ShapedThrottle,LinearThrottle;
        const double NominalHover=ShapedThrottle.parameters.mass*ShapedThrottle.parameters.gravity/ShapedThrottle.parameters.maxThrust;
        ShapedThrottle.parameters.throttleExpo=ThrottleExpo;
        ShapedThrottle.parameters.nominalHoverThrottle=NominalHover;
        check(!ShapedThrottle.arm({0,0,0,std::nextafter(MaxArmThrottle,1.),true}) &&
            ShapedThrottle.arm({0,0,0,MaxArmThrottle,true}),"arming still uses the raw stick threshold with Expo enabled");
        ShapedThrottle.thrust=NominalHover;
        run(ShapedThrottle,{0,0,0,NominalHover,true},240);
        near(ShapedThrottle.position.z,0.,1.e-10,"Expo preserves the physical hover balance point");
        LinearThrottle.arm({});
        LinearThrottle.thrust=NominalHover;
        const Input JustAboveHover{0,0,0,NominalHover+.02,true};
        run(LinearThrottle,JustAboveHover,120);
        run(ShapedThrottle,JustAboveHover,120);
        check(ShapedThrottle.thrust>NominalHover && ShapedThrottle.thrust-NominalHover<(LinearThrottle.thrust-NominalHover)*.4 &&
            ShapedThrottle.velocity.z>0. && ShapedThrottle.velocity.z<LinearThrottle.velocity.z,
            "throttle Expo reduces actual near-hover thrust and climb sensitivity");

        FlightCore d;
        check(!d.arm({0,0,0,.8,true}),"high throttle arm rejected");
        check(!d.arm({0,0,0,0,false}),"disconnected arm rejected");
        check(d.arm({}),"low throttle arm accepted");
        d.parameters.linearDrag={}; d.parameters.quadraticDrag={};
        d.thrust=d.parameters.mass*d.parameters.gravity/d.parameters.maxThrust;
        run(d,{0,0,0,d.thrust,true},240);
        near(d.position.z,0,1e-10,"hover force balances weight");
        d.reset(); run(d,{},240);
        near(d.velocity.z,-9.81,1e-9,"free fall velocity");
        near(d.position.z,-4.9254375,1e-8,"semi-implicit free fall");
        d.reset(); d.arm({}); run(d,{0,0,0,1,true},120);
        check(d.velocity.z>0,"full throttle climb");
        d.step({0,0,0,1,false}); check(!d.armed && d.thrust==0,"disconnect failsafe");
        d.reset(); d.arm({}); run(d,{.2,0,0,.3,true},20);
        check(d.right.z<0,"positive roll lowers right wing");
        d.reset(); d.arm({}); run(d,{0,.2,0,.3,true},20);
        check(d.forward.z>0,"positive pitch raises nose");
        d.reset(); d.arm({}); run(d,{0,0,.2,.3,true},20);
        check(d.forward.y>0,"positive yaw turns right");
        FlightCore axisRates;
        axisRates.parameters.rateDegrees=480;
        axisRates.arm({});
        const Input FullRotation{1,1,1,0,true};
        run(axisRates,FullRotation,240);
        near(axisRates.bodyRates.x*180/Pi,-480,1e-8,"linked roll retains the old full stick rate");
        near(axisRates.bodyRates.y*180/Pi,-480,1e-8,"linked pitch inherits the roll rate");
        near(axisRates.bodyRates.z*180/Pi,480,1e-8,"linked yaw inherits the roll rate");
        axisRates.reset();
        axisRates.parameters.pitchRateDegrees=310;
        axisRates.parameters.yawRateDegrees=240;
        axisRates.arm({});
        run(axisRates,FullRotation,240);
        near(axisRates.bodyRates.x*180/Pi,-480,1e-8,"pitch and yaw overrides preserve roll");
        near(axisRates.bodyRates.y*180/Pi,-310,1e-8,"independent acro pitch rate");
        near(axisRates.bodyRates.z*180/Pi,240,1e-8,"independent acro yaw rate");
        axisRates.reset();
        axisRates.parameters.expo=.85;
        axisRates.arm({});
        axisRates.step({.37,0,0,0,true});
        const double LegacyRoll=-shape(.37,.85,.03)*(480.*Pi/180.);
        const double AngularStep=1-std::exp(-StepSeconds/axisRates.parameters.angularTau);
        check(axisRates.bodyRates.x==LegacyRoll*AngularStep,"disabled center leaves legacy acro integration exact");
        axisRates.reset();
        axisRates.parameters.expo=0.;
        axisRates.parameters.deadzone=0.;
        axisRates.parameters.rollCenterRateDegrees=100.;
        axisRates.parameters.pitchCenterRateDegrees=160.;
        axisRates.parameters.yawCenterRateDegrees=90.;
        axisRates.arm({});
        run(axisRates,{.5,-.5,.5,0,true},240);
        near(axisRates.bodyRates.x*180/Pi,-145.,1e-8,"acro uses the custom roll center");
        near(axisRates.bodyRates.y*180/Pi,117.5,1e-8,"acro uses the custom pitch center and retains the pitch sign");
        near(axisRates.bodyRates.z*180/Pi,82.5,1e-8,"acro uses the custom yaw center");
        axisRates.reset();
        axisRates.parameters.angleMode=true;
        axisRates.arm({});
        run(axisRates,{0,0,1,0,true},240);
        near(axisRates.bodyRates.z*180/Pi,240,1e-8,"angle mode uses the independent yaw rate");
        near(tiltDegrees(axisRates),0,1e-8,"angle yaw stays level on centered roll and pitch");
        axisRates.reset();
        axisRates.arm({});
        run(axisRates,{0,0,.5,0,true},240);
        near(axisRates.bodyRates.z*180/Pi,82.5,1e-8,"angle yaw uses the custom center");
        axisRates.reset();
        axisRates.arm({});
        run(axisRates,{0,.5,0,0,true},960);
        near(tiltDegrees(axisRates),22.5,1e-5,"angle pitch tilt ignores the acro center override");
        d.reset(); d.arm({}); run(d,{.9,.7,.8,.5,true},24000);
        near(d.forward.dot(d.up),0,1e-10,"orthogonal after tumbling");
        near(d.right.length(),1,1e-10,"unit basis after tumbling");
        d.impact({1,2,3}); check(!d.armed && d.velocity.length()==0,"collision disarms and stops");
        near(d.position.z,3,1e-12,"collision position");
        d.reset(); d.arm({});
        for(int k=0;k<240;++k) {
            d.step({0,0,0,.5,true});
            if(d.position.z<0) d.contact({0,0,0},{0,0,1});
        }
        check(d.armed && d.position.z>0.5,"gentle ground contact permits takeoff");
        d.velocity={0,0,-4}; d.contact({0,0,0},{0,0,1});
        check(!d.armed,"hard contact disarms");
        auto a=flightAtFps(30),b=flightAtFps(60),e=flightAtFps(144);
        near((a.position-b.position).length(),0,1e-9,"30/60 fps match");
        near((a.position-e.position).length(),0,1e-9,"30/144 fps match");
        d.reset(); d.arm({}); d.step({0,0,0,std::numeric_limits<double>::quiet_NaN(),true});
        check(!d.armed && std::isfinite(d.position.z),"NaN input fails closed");
        Stepper s; int n=s.advance(20,[](double){}); check(n==24,"stall work bounded");
        d.reset(); d.arm({}); d.thrust=.5; d.up={0,0,-1}; d.right={0,-1,0};
        d.step({0,0,0,.5,true}); check(d.velocity.z < -9.81*StepSeconds,"inverted thrust points down");
        // Angle mode is an independent optional attitude controller. Preserve
        // the acro checks above as well as neutral attitude behavior explicitly.
        d=FlightCore{}; tilt(d,{1,0,0},35);d.arm({});run(d,{},720);
        near(tiltDegrees(d),35,1e-8,"default acro retains neutral-stick bank");
        d.parameters.angleMode=true;run(d,{},720);
        check(tiltDegrees(d)<.01,"angle mode levels a bank when sticks center");
        check(d.armed,"changing flight mode does not silently disarm");
        d=FlightCore{};d.parameters.angleMode=true;d.parameters.angleLimitDegrees=40;
        d.parameters.expo=0;d.parameters.deadzone=0;d.arm({});run(d,{1,0,0,.3,true},960);
        near(tiltDegrees(d),40,1e-5,"angle full roll respects configured tilt");
        check(d.right.z<-.6,"angle roll direction matches acro");
        d.reset();d.arm({});run(d,{0,1,0,.3,true},960);
        near(tiltDegrees(d),40,1e-5,"angle full pitch respects configured tilt");
        check(d.forward.z>.6,"angle pitch-back raises nose");
        d.parameters.rollCenterRateDegrees=240.;
        d.reset();d.arm({});run(d,{.5,0,0,.3,true},960);
        near(tiltDegrees(d),20,1e-5,"angle roll tilt ignores the acro center override");
        d.reset();d.arm({});run(d,{1,1,0,.3,true},1440);
        near(tiltDegrees(d),40,1e-4,"diagonal sticks share one tilt cone");
        const double beforeYaw=std::atan2(d.forward.y,d.forward.x);
        run(d,{1,1,.15,.3,true},120);
        check(std::abs(std::atan2(d.forward.y,d.forward.x)-beforeYaw)>.1,"yaw remains rate controlled in angle mode");
        check(tiltDegrees(d)<40.5,"yaw while banked preserves tilt limit");
        d.reset();tilt(d,{1,0,0},180);d.arm({});run(d,{},960);
        check(d.up.z>.99999,"angle recovers from exact inverted orientation");
        d.reset();tilt(d,{0,1,0},90);d.arm({});run(d,{},960);
        check(d.up.z>.99999,"angle recovers from vertical orientation");
        d.reset();tilt(d,{1,1,0},179.99);d.arm({});run(d,{},960);
        check(d.up.z>.99999,"angle recovers a nearly inverted compound attitude");
        d.reset();tilt(d,{1,0,0},45);run(d,{},960);
        near(tiltDegrees(d),45,1e-8,"disarmed angle mode has no active stabilization");
        d.reset();d.arm({});run(d,{},240);
        check(d.velocity.z < -8,"self-level does not invent altitude hold or thrust");
        d.reset();d.arm({});d.parameters.angleLimitDegrees=std::numeric_limits<double>::quiet_NaN();
        d.parameters.levelingGain=std::numeric_limits<double>::infinity();
        d.parameters.angleRateDegrees=-5;run(d,{1,0,0,0,true},1920);
        near(tiltDegrees(d),45,1e-4,"invalid angle tuning falls back or clamps safely");
        a=flightAtFps(30,true);b=flightAtFps(60,true);e=flightAtFps(144,true);
        near((a.position-b.position).length(),0,1e-9,"angle 30/60 fps match");
        near((a.position-e.position).length(),0,1e-9,"angle 30/144 fps match");
        near((a.up-e.up).length(),0,1e-10,"angle attitude independent of render fps");
        d=FlightCore{};d.parameters.angleMode=true;d.arm({});
        double maximumTilt=0;
        for(int step=0;step<14400;++step) {
            d.step({1,1,1,.5,true});
            maximumTilt=std::max(maximumTilt,tiltDegrees(d));
        }
        check(maximumTilt<47,"rapid sustained yaw does not overturn angle-limited craft");
        check(std::isfinite(d.position.x)&&std::isfinite(d.position.y)&&std::isfinite(d.position.z),"long angle flight remains finite");
        near(d.forward.dot(d.up),0,1e-10,"angle basis remains orthogonal under sustained yaw");
        near(d.up.length(),1,1e-10,"angle basis remains normalized");
        d.step({0,0,0,.5,false});
        check(!d.armed && d.thrust==0,"angle disconnect still cuts motors");
        std::cout<<checks<<" checks passed\n";
    } catch(const std::exception& ex) { std::cerr<<"FAIL: "<<ex.what()<<'\n'; return 1; }
}
