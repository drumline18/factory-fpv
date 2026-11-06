#pragma once
// Flight-model portions adapted from invilso/gta-fpv-drone (MIT),
// commit f58bac799ce05f35f138f1bffbfb4a718b24107b, drone/physics.lua and vecmath.lua.
// Copyright (c) 2026 invilso. See licenses/invilso-MIT.txt in the source package.
// Engine-independent prototype: SI units, X forward, Y right, Z up.
#include <algorithm>
#include <cmath>

namespace factoryfpv {
constexpr double Pi = 3.14159265358979323846;
constexpr double StepSeconds = 1.0 / 240.0;
constexpr double MaxArmThrottle = 0.05;
inline double clamp(double v, double lo, double hi) { return std::clamp(v,lo,hi); }
struct Vec {
    double x=0, y=0, z=0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec operator*(double s) const { return {x*s,y*s,z*s}; }
    double dot(Vec b) const { return x*b.x+y*b.y+z*b.z; }
    Vec cross(Vec b) const { return {y*b.z-z*b.y,z*b.x-x*b.z,x*b.y-y*b.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    Vec normalized() const { auto n=length(); return n>1e-12 ? *this*(1/n) : Vec{}; }
};
inline Vec rotate(Vec v, Vec axis, double angle) {
    const double c=std::cos(angle), s=std::sin(angle);
    return v*c+axis.cross(v)*s+axis*(axis.dot(v)*(1-c));
}
inline double shape(double v, double expo, double deadzone) {
    if (!std::isfinite(v)) return 0;
    v=clamp(v,-1,1); expo=clamp(expo,0,1); deadzone=clamp(deadzone,0,0.95);
    double a=std::abs(v);
    if(a<=deadzone) return 0;
    a=(a-deadzone)/(1-deadzone);
    return std::copysign(expo*a*a*a+(1-expo)*a,v);
}
inline double rateOrLinked(double rate, double linkedRate) {
    return std::isfinite(rate) && rate>0 ? rate : linkedRate;
}
inline double rateCommand(double stick,double maximumDegrees,double expo,double deadzone,double centerDegrees=0.) {
    if (rateOrLinked(centerDegrees,0.)==0.) return shape(stick,expo,deadzone)*maximumDegrees;
    if (!std::isfinite(stick) || !std::isfinite(maximumDegrees) || maximumDegrees<=0.) return 0.;
    const double zone=std::isfinite(deadzone) ? clamp(deadzone,0.,.95) : 0.;
    const double magnitude=std::min(1.,std::abs(stick));
    if (magnitude<=zone) return 0.;
    if (magnitude==1.) return std::copysign(maximumDegrees,stick);

    const double x=(magnitude-zone)/(1.-zone);
    const double center=clamp(centerDegrees,std::min(10.,maximumDegrees),maximumDegrees);
    const double exponent=2.+3.*(std::isfinite(expo) ? clamp(expo,0.,1.) : 0.);
    // Center controls the initial slope. Expo changes how quickly the outer stick adds rate.
    return std::copysign(center*x+(maximumDegrees-center)*std::pow(x,exponent),stick);
}
inline double throttleCommand(double stick,double nominalHoverThrottle,double expo) {
    if (!std::isfinite(stick)) return 0.;
    const double x=clamp(stick,0.,1.);
    if (x<=MaxArmThrottle || x==1. || !std::isfinite(expo) || expo<=0. ||
        !std::isfinite(nominalHoverThrottle) || nominalHoverThrottle<=MaxArmThrottle || nominalHoverThrottle>=1.) return x;

    // Bend each interval toward hover with matching slope 1-expo there,
    // and the original unit slope at the arming threshold and full stick.
    const double strength=std::min(.8,expo);
    if (x<nominalHoverThrottle) {
        const double span=nominalHoverThrottle-MaxArmThrottle;
        const double t=(x-MaxArmThrottle)/span;
        return x+span*strength*t*t*(1.-t);
    }
    const double span=1.-nominalHoverThrottle;
    const double t=(x-nominalHoverThrottle)/span;
    const double remaining=1.-t;
    return x-span*strength*t*remaining*remaining;
}
struct Calibration {
    double minimum=-1, center=0, maximum=1;
    bool reversed=false;
    bool valid() const { return std::isfinite(minimum)&&std::isfinite(center)&&std::isfinite(maximum)
        && minimum<center && center<maximum; }
    double axis(double raw, bool throttle=false) const {
        if(!valid() || !std::isfinite(raw)) return 0;
        if(throttle) {
            auto t=clamp((raw-minimum)/(maximum-minimum),0,1);
            return reversed ? 1-t : t;
        }
        auto v=clamp((raw-center)/(raw>=center ? maximum-center : center-minimum),-1,1);
        return reversed ? -v : v;
    }
};
struct Input {
    double roll=0, pitch=0, yaw=0, throttle=0;
    bool connected=true;
    bool valid() const { return connected && std::isfinite(roll) && std::isfinite(pitch)
        && std::isfinite(yaw) && std::isfinite(throttle); }
};
struct Parameters {
    double mass=0.65, maxThrust=26.0, gravity=9.81;
    double motorTau=0.035, angularTau=0.025, rateDegrees=600, expo=0.3, deadzone=0.03;
    Vec linearDrag{0.08,0.12,0.20}, quadraticDrag{0.012,0.020,0.035};
    // Angle mode commands a tilt cone, with yaw remaining rate controlled.
    // It does not hold altitude or position. Defaults preserve the acro model.
    bool angleMode=false;
    double angleLimitDegrees=45, levelingGain=6, angleRateDegrees=300;
    double pitchRateDegrees=0, yawRateDegrees=0; // Zero uses rateDegrees.
    double rollCenterRateDegrees=0, pitchCenterRateDegrees=0, yawCenterRateDegrees=0; // Zero retains legacy Expo.
    double throttleExpo=0., nominalHoverThrottle=0.; // Zero Expo retains linear throttle.
};
class FlightCore {
public:
    Parameters parameters;
    Vec position{}, velocity{}, forward{1,0,0}, right{0,1,0}, up{0,0,1}, bodyRates{};
    double thrust=0;
    bool armed=false;
    void reset(Vec p={}, double heading=0) {
        position=p; velocity={}; bodyRates={}; thrust=0; armed=false;
        forward={std::cos(heading),std::sin(heading),0};
        right={-std::sin(heading),std::cos(heading),0}; up={0,0,1};
    }
    bool arm(const Input& in) {
        if(!in.valid() || in.throttle<0 || in.throttle>MaxArmThrottle) return false;
        armed=true; return true;
    }
    void disarm() { armed=false; thrust=0; }
    void impact(Vec contactPosition) {
        position=contactPosition; velocity={}; bodyRates={}; disarm();
    }
    void contact(Vec correctedPosition, Vec outwardNormal) {
        const auto n=outwardNormal.normalized();
        const double inward=velocity.dot(n);
        if(inward < -3.0) { impact(correctedPosition); return; }
        position=correctedPosition;
        if(inward<0) velocity=velocity-n*inward;
        // Gentle ground contact must allow motors to spool up for takeoff.
    }
    void step(const Input& in, double dt=StepSeconds) {
        if(!std::isfinite(dt)||dt<=0||dt>0.02) return;
        if(!in.valid()) disarm();
        const auto& p=parameters;
        if(!(p.mass>0) || !(p.maxThrust>=0)) { disarm(); return; }
        if(armed) {
            const auto acroRate=[&p](double stick,double maximum,double center) {
                // Keep the original multiplication order for existing tunes.
                if (rateOrLinked(center,0.)==0.) return shape(stick,p.expo,p.deadzone)*(maximum*Pi/180.);
                return rateCommand(stick,maximum,p.expo,p.deadzone,center)*Pi/180.;
            };
            // Right roll lowers right wing; positive pitch raises nose; positive yaw turns right.
            const Vec target=p.angleMode ? angleRates(in) : Vec{
                -acroRate(in.roll,p.rateDegrees,p.rollCenterRateDegrees),
                -acroRate(in.pitch,rateOrLinked(p.pitchRateDegrees,p.rateDegrees),p.pitchCenterRateDegrees),
                acroRate(in.yaw,rateOrLinked(p.yawRateDegrees,p.rateDegrees),p.yawCenterRateDegrees)};
            double k=1-std::exp(-dt/std::max(p.angularTau,1e-4));
            bodyRates=bodyRates+(target-bodyRates)*k;
            k=1-std::exp(-dt/std::max(p.motorTau,1e-4));
            thrust+=(throttleCommand(in.throttle,p.nominalHoverThrottle,p.throttleExpo)-thrust)*k;
        } else {
            // No attitude stabilization with motors off: retain spin, apply passive damping.
            bodyRates=bodyRates*std::exp(-0.2*dt);
        }
        Vec omega=forward*bodyRates.x+right*bodyRates.y+up*bodyRates.z;
        double speed=omega.length();
        if(speed>1e-9) {
            auto axis=omega*(1/speed);
            forward=rotate(forward,axis,speed*dt).normalized();
            up=rotate(up,axis,speed*dt);
            right=up.cross(forward).normalized(); up=forward.cross(right).normalized();
        }
        auto drag=[](double v,double a,double b) { return -(a*v+b*v*std::abs(v)); };
        Vec acceleration=up*(thrust*p.maxThrust/p.mass)+Vec{0,0,-p.gravity};
        acceleration=acceleration
            +forward*drag(velocity.dot(forward),p.linearDrag.x,p.quadraticDrag.x)
            +right*drag(velocity.dot(right),p.linearDrag.y,p.quadraticDrag.y)
            +up*drag(velocity.dot(up),p.linearDrag.z,p.quadraticDrag.z);
        velocity=velocity+acceleration*dt;
        position=position+velocity*dt;
    }
private:
    Vec angleRates(const Input& in) const {
        const auto finiteClamp=[](double value,double minimum,double maximum,double fallback) {
            return std::isfinite(value) ? clamp(value,minimum,maximum) : fallback;
        };
        const auto& p=parameters;
        const Vec worldUp{0,0,1};
        Vec heading{forward.x,forward.y,0};
        // Heading projection is undefined at vertical. The horizontal right
        // vector gives a continuous local reference for recovery near that pose.
        if (heading.length()<1e-6) heading=Vec{right.y,-right.x,0};
        if (heading.length()<1e-6) heading={1,0,0};
        heading=heading.normalized();
        const Vec horizontalRight=worldUp.cross(heading);
        const double roll=shape(in.roll,p.expo,p.deadzone),pitch=shape(in.pitch,p.expo,p.deadzone);
        const Vec stickTilt=horizontalRight*roll-heading*pitch;
        const double magnitude=stickTilt.length();
        const double angle=std::min(1.,magnitude)*finiteClamp(p.angleLimitDegrees,5,80,45)*Pi/180.;
        const Vec desiredUp=worldUp*std::cos(angle)+stickTilt.normalized()*std::sin(angle);
        Vec axis=up.cross(desiredUp);
        const double sine=axis.length(),cosine=clamp(up.dot(desiredUp),-1,1);
        const double error=std::atan2(sine,cosine);
        if (sine>1e-8) axis=axis*(1/sine);
        else if (cosine<0) {
            // Exact upside-down alignment has no unique shortest rotation.
            // Choose the body-forward axis deterministically so recovery starts.
            axis=(forward-up*forward.dot(up)).normalized();
            if (axis.length()<1e-6) axis=right.normalized();
        } else axis={};
        const double tiltRate=std::min(error*finiteClamp(p.levelingGain,.5,20,6),
            finiteClamp(p.angleRateDegrees,30,720,300)*Pi/180.);
        // World-up yaw preserves the tilt cone while turning; project the
        // resulting angular velocity into the body's rate-controller frame.
        const double yawMaximum=finiteClamp(rateOrLinked(p.yawRateDegrees,p.rateDegrees),0,2400,600);
        const double yawRate=rateCommand(in.yaw,yawMaximum,p.expo,p.deadzone,p.yawCenterRateDegrees)*Pi/180.;
        const Vec angularVelocity=axis*tiltRate+worldUp*yawRate;
        return {angularVelocity.dot(forward),angularVelocity.dot(right),angularVelocity.dot(up)};
    }
};
// Fixed simulation step, independent of render frequency. Overload time is dropped,
// never integrated as one enormous unstable step. Caller decides pause/focus policy.
class Stepper {
    double accumulator=0;
public:
    void clear() { accumulator=0; }
    template<class F> int advance(double elapsed, F tick) {
        if(!std::isfinite(elapsed)||elapsed<=0) return 0;
        accumulator+=std::min(elapsed,0.1);
        int count=0;
        while(accumulator+1e-12>=StepSeconds && count<24) {
            tick(StepSeconds); accumulator-=StepSeconds; ++count;
        }
        if(accumulator<0) accumulator=0;
        return count;
    }
};
} // namespace factoryfpv
