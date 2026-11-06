#pragma once
#include "CoreMinimal.h"

struct FFactoryFPVRaceLapResult {
    uint64 Number=0;
    double Seconds=0.;
    double ReferenceSeconds=0.;
    bool bCourseBest=false;
};

struct FFactoryFPVRaceSplitResult {
    int32 Checkpoint=0;
    double Seconds=0.;
    double ReferenceSeconds=0.;
};

// Transient summary of scored laps in the most recent completed race flight.
struct FFactoryFPVRaceDebrief {
    static constexpr int32 MaxRecentLaps=20;

    int32 CourseId=0;
    FString CourseName;
    uint64 LapCount=0;
    double FastestLap=0.;
    TArray<FFactoryFPVRaceLapResult> RecentLaps;
    TArray<FFactoryFPVRaceSplitResult> LastLapSplits;
};
