#pragma once
#include "CoreMinimal.h"
#include "FactoryFPVRaceGhost.generated.h"

// Samples are centimetres relative to gate 1. The last sample is at the exact
// finish time. All earlier samples are uniformly spaced from the start crossing.
USTRUCT()
struct FFactoryFPVRaceGhost {
    GENERATED_BODY()

    static constexpr double SamplePeriod=.05;
    static constexpr double MaxDuration=300.;
    UPROPERTY(SaveGame) TArray<FVector3f> Positions;

    void Start(const FVector& Position);
    void Append(const FVector& From,const FVector& To,double FromTime,double ToTime);
    void Finish(const FVector& Position,double LapSeconds);
    bool IsValid(double LapSeconds) const;
    FVector Sample(double Seconds,double LapSeconds) const;
};
