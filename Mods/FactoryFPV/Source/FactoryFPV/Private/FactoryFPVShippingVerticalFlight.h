#pragma once

#include "CoreMinimal.h"

class AFactoryFPVDrone;
class AFactoryFPVRaceGate;

// A disposable off-screen pilot. The caller owns the launched drone and gates.
class FFactoryFPVShippingVerticalFlight {
public:
    enum class EResult { Pending, Passed, Failed };
    bool Begin(AFactoryFPVDrone& Drone,AFactoryFPVRaceGate& Ascend,AFactoryFPVRaceGate& Descend,FString& Error);
    EResult Tick(float DeltaSeconds,FString& Error);
    void Cancel();
    FString Evidence() const;

private:
    bool VerifyRaceEdgeCue(AFactoryFPVDrone& Drone,int32 ExpectedGate,double VerticalDirection,FString& Error) const;
    enum class EPhase { Inactive, AscendingStart, ReversingUpper, TurningAbove, DescendingUpper, ReversingLower, TurningBelow, AscendingFinish, GhostReplay, Complete };
    TWeakObjectPtr<AFactoryFPVDrone> FlightDrone;
    TWeakObjectPtr<AFactoryFPVRaceGate> LowerGate,UpperGate;
    FVector LaunchPosition=FVector::ZeroVector;
    FString Layout;
    EPhase Phase=EPhase::Inactive;
    bool bOriginalTickEnabled=false;
    bool bArmed=false,bAscendStart=false,bWrongWayAbove=false,bDescend=false,bWrongWayBelow=false,bLap=false,bHud=false;
    bool bCheckGhost=false,bGhostAbsentFirstLap=false,bGhostRecorded=false,bGhostReplay=false,bGhostCleared=false;
    bool bCheckRaceCues=false,bCheckpointCueVerified=false,bBestCueVerified=false;
    bool bCheckRaceEdge=false,bUpEdgeCueVerified=false,bDownEdgeCueVerified=false;
    int32 GhostSamples=0;
    TOptional<FVector> FirstVisibleGhostPosition;
    double GhostAdvanceCm=0.;
    int32 FrameCount=0;
    double Elapsed=0.,LapSeconds=0.,SplitSeconds=0.,MaxCrossTrackCm=0.;
};
