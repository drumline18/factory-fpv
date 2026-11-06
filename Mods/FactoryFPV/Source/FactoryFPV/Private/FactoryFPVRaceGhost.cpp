#include "FactoryFPVRaceGhost.h"

namespace {
constexpr double TimeTolerance=1.e-8;
// The world is much smaller. Reject corrupt poses before they reach rendering.
constexpr float MaxOffsetCm=2'000'000.f;
bool IsValidPosition(const FVector3f& Position) {
    return !Position.ContainsNaN() && Position.GetAbsMax()<=MaxOffsetCm;
}
}

void FFactoryFPVRaceGhost::Start(const FVector& Position) {
    Positions.Reset();
    const FVector3f Point(Position);
    if (IsValidPosition(Point)) Positions.Add(Point);
}

void FFactoryFPVRaceGhost::Append(const FVector& From,const FVector& To,double FromTime,double ToTime) {
    if (Positions.IsEmpty()) return;
    if (!FMath::IsFinite(FromTime) || !FMath::IsFinite(ToTime) || ToTime>MaxDuration ||
        !IsValidPosition(FVector3f(From)) || !IsValidPosition(FVector3f(To))) {
        Positions.Reset();
        return;
    }
    if (ToTime<=FromTime) return;
    for (double Time=Positions.Num()*SamplePeriod; Time<ToTime-TimeTolerance; Time=Positions.Num()*SamplePeriod) {
        const double Fraction=FMath::Clamp((Time-FromTime)/(ToTime-FromTime),0.,1.);
        Positions.Add(FVector3f(FMath::Lerp(From,To,Fraction)));
    }
}

void FFactoryFPVRaceGhost::Finish(const FVector& Position,double LapSeconds) {
    if (Positions.IsEmpty()) return;
    Positions.Add(FVector3f(Position));
    if (!IsValid(LapSeconds)) Positions.Reset();
}

bool FFactoryFPVRaceGhost::IsValid(double LapSeconds) const {
    if (!FMath::IsFinite(LapSeconds) || LapSeconds<=TimeTolerance || LapSeconds>MaxDuration ||
        Positions.Num()!=FMath::CeilToInt((LapSeconds-TimeTolerance)/SamplePeriod)+1) return false;
    return !Positions.ContainsByPredicate([](const FVector3f& Point) { return !IsValidPosition(Point); });
}

FVector FFactoryFPVRaceGhost::Sample(double Seconds,double LapSeconds) const {
    // The saved trace has been validated on load. The final interval can be shorter.
    const double Time=FMath::Clamp(Seconds,0.,LapSeconds);
    const int32 Index=FMath::Min(FMath::FloorToInt(Time/SamplePeriod),Positions.Num()-2);
    const double FromTime=Index*SamplePeriod;
    const double ToTime=Index+1==Positions.Num()-1 ? LapSeconds : (Index+1)*SamplePeriod;
    return FMath::Lerp(FVector(Positions[Index]),FVector(Positions[Index+1]),(Time-FromTime)/(ToTime-FromTime));
}
