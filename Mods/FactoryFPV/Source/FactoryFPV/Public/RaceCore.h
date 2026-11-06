#pragma once
// Original Factory FPV race logic. SI metres/seconds; no Unreal dependencies.
#include "FlightCore.h"
#include <cstdint>
#include <string>
#include <vector>

namespace factoryfpv {
enum class RaceGateKind : std::uint8_t { Gate,PoleLeft,PoleRight };
struct RaceGate {
    std::string id; // Full persistent gate identity, not an order-derived ID.
    int order=0; // A selected course contains exactly 1..N, with N >= 2.
    Vec center{},normal{1,0,0},right{0,1,0},up{0,0,1};
    double halfWidth=1,halfHeight=1; // Clear aperture, excluding the frame.
    bool powered=false;
    // Appended defaults preserve existing rectangular-gate aggregate callers.
    // A pole uses the same forward plane, with its solid axis at center.
    // halfWidth is its bounded open-side reach from that axis, not a symmetric
    // opening. Left is -right, right is +right; halfHeight bounds the clear
    // scoring height about center. Native collision supplies the actual path.
    RaceGateKind kind=RaceGateKind::Gate;
    double poleRadius=.15; // Solid pole lateral half-width in SI metres.
};
enum class RaceProblem { None,NoCourse,InvalidCourse,MissingGate,ChangedCourse,UnpoweredGate,InvalidMovement,Recovery };
struct RaceState {
    bool selected=false,running=false,ready=false,inLap=false;
    int gateCount=0,nextGate=0;
    std::uint64_t lapCount=0;
    double elapsed=0,lastLap=0,bestLap=0;
    RaceProblem problem=RaceProblem::NoCourse;
};
struct RaceUpdate {
    int crossings=0,accepted=0,wrongOrder=0,ambiguous=0;
    bool lapStarted=false,lapCompleted=false,lapInvalidated=false;
    int wrongWay=0;
    // The latest checkpoint after the most recent start/finish in this sweep.
    int splitOrder=0;
    double splitElapsed=0;
};

class RaceCore {
    static constexpr double PlaneEpsilon=1.e-6;
    static constexpr double ApertureEpsilon=1.e-9;
    static constexpr double GeometryEpsilon=1.e-6;
    static constexpr double SimultaneousFractionEpsilon=1.e-12;
    enum class Approach : std::uint8_t { Unknown,Behind,InFront };
    struct Crossing { double fraction=0;int order=0; };
    std::vector<RaceGate> course_;
    std::vector<Approach> approach_;
    std::vector<Crossing> crossings_,reverseCrossings_;
    std::vector<double> currentSplits_,lastSplits_;
    RaceState state_;
    double clock_=0,lapStart_=0;

    static bool finite(Vec V) { return std::isfinite(V.x) && std::isfinite(V.y) && std::isfinite(V.z); }
    static bool geometryEqual(Vec A,Vec B) { return A.x==B.x && A.y==B.y && A.z==B.z; }
    static bool geometryNear(double A,double B) { return std::abs(A-B)<=GeometryEpsilon; }
    static bool geometryNear(Vec A,Vec B) { return (A-B).length()<=GeometryEpsilon; }
    static bool validGeometry(const RaceGate& Gate) {
        const bool IsPole=Gate.kind==RaceGateKind::PoleLeft || Gate.kind==RaceGateKind::PoleRight;
        return (Gate.kind==RaceGateKind::Gate || IsPole) &&
            std::isfinite(Gate.poleRadius) && Gate.poleRadius>=0 && (!IsPole || (Gate.poleRadius>0 && Gate.halfWidth>Gate.poleRadius)) &&
            finite(Gate.center) && finite(Gate.normal) && finite(Gate.right) && finite(Gate.up) &&
            std::isfinite(Gate.halfWidth) && Gate.halfWidth>0 && std::isfinite(Gate.halfHeight) && Gate.halfHeight>0 &&
            geometryNear(Gate.normal.length(),1) && geometryNear(Gate.right.length(),1) && geometryNear(Gate.up.length(),1) &&
            std::abs(Gate.normal.dot(Gate.right))<=GeometryEpsilon && std::abs(Gate.normal.dot(Gate.up))<=GeometryEpsilon &&
            std::abs(Gate.right.dot(Gate.up))<=GeometryEpsilon && geometryNear(Gate.normal.cross(Gate.right),Gate.up);
    }
    static bool validCourse(const std::vector<RaceGate>& Gates) {
        if (Gates.size()<2 || Gates.size()>static_cast<std::size_t>(2147483647)) return false;
        for (std::size_t Index=0;Index<Gates.size();++Index) {
            const auto& Gate=Gates[Index];
            if (Gate.id.empty() || Gate.order<1 || Gate.order>static_cast<int>(Gates.size()) || !validGeometry(Gate)) return false;
            for (std::size_t Previous=0;Previous<Index;++Previous)
                if (Gates[Previous].id==Gate.id || Gates[Previous].order==Gate.order) return false;
        }
        return true;
    }
    static bool clearsAperture(const RaceGate& Gate,Vec AtPlane,double Radius) {
        const double Lateral=AtPlane.dot(Gate.right);
        const double OpenSide=Gate.kind==RaceGateKind::PoleLeft ? -Lateral : Lateral;
        const bool Across=Gate.kind==RaceGateKind::Gate ?
            std::abs(Lateral)+Radius < Gate.halfWidth-ApertureEpsilon :
            OpenSide-Radius > Gate.poleRadius+ApertureEpsilon && OpenSide+Radius < Gate.halfWidth-ApertureEpsilon;
        return Across && std::abs(AtPlane.dot(Gate.up))+Radius < Gate.halfHeight-ApertureEpsilon;
    }
    void clearTracking() { std::fill(approach_.begin(),approach_.end(),Approach::Unknown); }
    void cancelLap(RaceProblem Problem) {
        state_.inLap=false;state_.elapsed=0;state_.nextGate=state_.selected ? 1 : 0;
        currentSplits_.clear();
        state_.problem=Problem;clearTracking();
    }
    RaceProblem checkLive(const std::vector<RaceGate>& Gates) const {
        if (Gates.size()!=course_.size()) return RaceProblem::MissingGate;
        // The adapter supplies gates in checkpoint order. Matching the already
        // validated course exactly also proves live geometry and uniqueness.
        const bool UnchangedInOrder=std::equal(Gates.begin(),Gates.end(),course_.begin(),[](const RaceGate& Live,const RaceGate& Expected) {
            return Live.id==Expected.id && Live.order==Expected.order && Live.kind==Expected.kind &&
                geometryEqual(Live.center,Expected.center) && geometryEqual(Live.normal,Expected.normal) &&
                geometryEqual(Live.right,Expected.right) && geometryEqual(Live.up,Expected.up) &&
                Live.halfWidth==Expected.halfWidth && Live.halfHeight==Expected.halfHeight && Live.poleRadius==Expected.poleRadius;
        });
        if (UnchangedInOrder)
            return std::all_of(Gates.begin(),Gates.end(),[](const RaceGate& Gate) { return Gate.powered; }) ?
                RaceProblem::None : RaceProblem::UnpoweredGate;
        if (!validCourse(Gates)) return RaceProblem::InvalidCourse;
        for (const auto& Expected:course_) {
            const auto Found=std::find_if(Gates.begin(),Gates.end(),[&](const auto& Gate) { return Gate.id==Expected.id; });
            if (Found==Gates.end()) return RaceProblem::MissingGate;
            if (Found->order!=Expected.order || !geometryNear(Found->center,Expected.center) || !geometryNear(Found->normal,Expected.normal) ||
                !geometryNear(Found->right,Expected.right) || !geometryNear(Found->up,Expected.up) ||
                !geometryNear(Found->halfWidth,Expected.halfWidth) || !geometryNear(Found->halfHeight,Expected.halfHeight) ||
                Found->kind!=Expected.kind || !geometryNear(Found->poleRadius,Expected.poleRadius)) return RaceProblem::ChangedCourse;
        }
        for (const auto& Gate:Gates) if (!Gate.powered) return RaceProblem::UnpoweredGate;
        return RaceProblem::None;
    }
public:
    const RaceState& state() const { return state_; }
    // Cumulative seconds since the start at checkpoints 2 through N. The
    // finish is reported separately by RaceState::lastLap.
    const std::vector<double>& currentLapSplits() const { return currentSplits_; }
    const std::vector<double>& lastLapSplits() const { return lastSplits_; }

    // Selecting replaces the run and its records. A valid but unpowered course
    // can be selected; start/plane scoring remain blocked until all gates work.
    bool select(const std::vector<RaceGate>& Gates) {
        course_.clear();approach_.clear();crossings_.clear();reverseCrossings_.clear();state_={};clock_=0;lapStart_=0;
        currentSplits_.clear();lastSplits_.clear();
        if (!validCourse(Gates)) { state_.problem=RaceProblem::InvalidCourse;return false; }
        course_=Gates;
        std::sort(course_.begin(),course_.end(),[](const auto& A,const auto& B) { return A.order<B.order; });
        approach_.resize(course_.size(),Approach::Unknown);
        crossings_.reserve(course_.size());reverseCrossings_.reserve(course_.size());
        currentSplits_.reserve(course_.size()-1);lastSplits_.reserve(course_.size()-1);
        state_.selected=true;state_.gateCount=static_cast<int>(course_.size());state_.nextGate=1;
        state_.problem=checkLive(Gates);state_.ready=state_.problem==RaceProblem::None;
        return true;
    }
    bool start() {
        if (!state_.selected || !state_.ready) return false;
        state_.running=true;cancelLap(RaceProblem::None);clock_=0;lapStart_=0;
        return true;
    }
    void stop() { state_.running=false;cancelLap(state_.selected ? RaceProblem::None : RaceProblem::NoCourse); }
    void resetRun() {
        stop();state_.lapCount=0;state_.lastLap=0;state_.bestLap=0;clock_=0;lapStart_=0;
        lastSplits_.clear();
    }
    // F9 recovery/teleport must call this before feeding the next movement.
    // Completed records survive; the interrupted lap always restarts at gate1.
    void invalidateLap(RaceProblem Reason=RaceProblem::Recovery) { cancelLap(Reason); }

    // Validate once per sampled world state. Returns whether a live fault
    // invalidated an active lap, including when the simulation is paused.
    bool refreshLive(const std::vector<RaceGate>& Live) {
        if (!state_.selected) return false;
        const RaceProblem LiveProblem=checkLive(Live);
        state_.ready=LiveProblem==RaceProblem::None;
        if (!state_.ready) {
            const bool Invalidated=state_.inLap;
            cancelLap(LiveProblem);
            return Invalidated;
        }
        if (state_.problem==RaceProblem::MissingGate || state_.problem==RaceProblem::InvalidCourse ||
            state_.problem==RaceProblem::ChangedCourse || state_.problem==RaceProblem::UnpoweredGate) state_.problem=RaceProblem::None;
        return false;
    }

    // Feed an actual post-collision swept segment from the sampled world
    // state. Callers must refresh when the live gates can change and invalidate
    // before teleports/recovery. Paused calls consume no movement or time.
    RaceUpdate advanceSegment(Vec From,Vec To,double Seconds,double Radius,bool ClockRunning=true) {
        RaceUpdate Result;
        crossings_.clear();
        reverseCrossings_.clear();
        if (!state_.selected || !state_.ready || !state_.running || !ClockRunning) return Result;
        if (finite(From) && finite(To) && Seconds==0 && (To-From).length()==0 && std::isfinite(Radius) && Radius>=0) return Result;
        if (!finite(From) || !finite(To) || !finite(To-From) || !std::isfinite(Seconds) || Seconds<=0 || !std::isfinite(Radius) || Radius<0 ||
            !std::isfinite(clock_+Seconds)) {
            Result.lapInvalidated=state_.inLap;cancelLap(RaceProblem::InvalidMovement);return Result;
        }
        const double SegmentStart=clock_;
        clock_+=Seconds;
        for (std::size_t Index=0;Index<course_.size();++Index) {
            const auto& Gate=course_[Index];
            const double Before=(From-Gate.center).dot(Gate.normal),After=(To-Gate.center).dot(Gate.normal);
            if (!std::isfinite(Before) || !std::isfinite(After) || !std::isfinite(After-Before)) {
                Result.lapInvalidated=state_.inLap;cancelLap(RaceProblem::InvalidMovement);return Result;
            }
            // A definite approach survives arrival exactly on the plane, so
            // split sweeps register once, at the departure instant.
            auto& Side=approach_[Index];
            if (Before < -PlaneEpsilon) Side=Approach::Behind;
            else if (Before > PlaneEpsilon) Side=Approach::InFront;
            const bool Forward=Side==Approach::Behind && Before<=0 && After>0;
            const bool Reverse=Side==Approach::InFront && Before>=0 && After<0;
            if (Forward || Reverse) {
                Side=Approach::Unknown;
                const double Fraction=-Before/(After-Before);
                const Vec AtPlane=From+(To-From)*Fraction-Gate.center;
                // Inspect clearance at the physical plane crossing. The caller
                // supplies a collision-truncated path and scoring permission.
                if (clearsAperture(Gate,AtPlane,Radius))
                    (Forward ? crossings_ : reverseCrossings_).push_back({Fraction,Gate.order});
            }
            if (After < -PlaneEpsilon) Side=Approach::Behind;
            else if (After > PlaneEpsilon) Side=Approach::InFront;
        }
        const auto ByFraction=[](const Crossing& A,const Crossing& B) {
            return A.fraction<B.fraction || (A.fraction==B.fraction && A.order<B.order);
        };
        std::sort(crossings_.begin(),crossings_.end(),ByFraction);
        std::sort(reverseCrossings_.begin(),reverseCrossings_.end(),ByFraction);
        auto Reverse=reverseCrossings_.cbegin();
        // Reverse crossings are advisory. Evaluate them in sweep order against
        // the expected checkpoint, before any simultaneous progression.
        const auto reportReverseThrough=[&](double Fraction) {
            while (Reverse!=reverseCrossings_.cend() && Reverse->fraction<=Fraction) {
                if (Reverse->order==state_.nextGate) ++Result.wrongWay;
                ++Reverse;
            }
        };
        Result.crossings=static_cast<int>(crossings_.size());
        for (std::size_t Index=0;Index<crossings_.size();++Index) {
            const auto& Crossing=crossings_[Index];
            reportReverseThrough(Crossing.fraction+SimultaneousFractionEpsilon);
            // Coincident crossings have no defensible order. Do not invent one
            // from actor enumeration/order numbers for overlapping gate planes.
            if ((Index>0 && std::abs(Crossing.fraction-crossings_[Index-1].fraction)<SimultaneousFractionEpsilon) ||
                (Index+1<crossings_.size() && std::abs(Crossing.fraction-crossings_[Index+1].fraction)<SimultaneousFractionEpsilon)) {
                ++Result.ambiguous;continue;
            }
            if (Crossing.order!=state_.nextGate) { ++Result.wrongOrder;continue; }
            ++Result.accepted;
            const double CrossingTime=SegmentStart+Crossing.fraction*Seconds;
            if (Crossing.order==1) {
                if (state_.inLap) {
                    const double Lap=CrossingTime-lapStart_;
                    if (Lap>0 && std::isfinite(Lap)) {
                        state_.lastLap=Lap;
                        lastSplits_.swap(currentSplits_);
                        if (state_.bestLap==0 || Lap<state_.bestLap) state_.bestLap=Lap;
                        ++state_.lapCount;Result.lapCompleted=true;
                    }
                }
                currentSplits_.clear();
                Result.splitOrder=0;Result.splitElapsed=0;
                state_.inLap=true;state_.problem=RaceProblem::None;lapStart_=CrossingTime;
                state_.nextGate=2;Result.lapStarted=true;
            } else {
                Result.splitOrder=Crossing.order;
                Result.splitElapsed=CrossingTime-lapStart_;
                currentSplits_.push_back(Result.splitElapsed);
                state_.nextGate=Crossing.order==state_.gateCount ? 1 : Crossing.order+1;
            }
        }
        reportReverseThrough(1.);
        state_.elapsed=state_.inLap ? clock_-lapStart_ : 0;
        return Result;
    }

    // Compatibility entry point for callers that sample live gates every step.
    RaceUpdate update(const std::vector<RaceGate>& Live,Vec From,Vec To,double Seconds,double Radius,bool ClockRunning=true) {
        const bool Invalidated=refreshLive(Live);
        auto Result=advanceSegment(From,To,Seconds,Radius,ClockRunning);
        Result.lapInvalidated=Result.lapInvalidated || Invalidated;
        return Result;
    }
};
}
