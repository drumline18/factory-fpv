#pragma once

#include "CoreMinimal.h"

class AFactoryFPVDrone;

// Keeps a disposable off-screen autosave flight under throttle control.
class FFactoryFPVShippingArmedSaveFlight {
public:
    FFactoryFPVShippingArmedSaveFlight()=default;
    ~FFactoryFPVShippingArmedSaveFlight();
    FFactoryFPVShippingArmedSaveFlight(const FFactoryFPVShippingArmedSaveFlight&)=delete;
    FFactoryFPVShippingArmedSaveFlight& operator=(const FFactoryFPVShippingArmedSaveFlight&)=delete;

    bool Begin(AFactoryFPVDrone& Drone,FString& Error);
    bool Tick(float DeltaSeconds,FString& Error);
    bool IsPoweredHovering() const;
    double AltitudeCm() const { return CurrentAltitudeCm; }
    double StableSeconds() const { return ContinuousStableSeconds; }
    void Cancel();
    FString Evidence() const;

private:
    TWeakObjectPtr<AFactoryFPVDrone> FlightDrone;
    FVector LaunchPosition=FVector::ZeroVector;
    bool bArmedAtZero=false;
    int32 FrameCount=0;
    int32 StableFrames=0;
    double ContinuousStableSeconds=0.;
    double CurrentAltitudeCm=0.;
    double Thrust=0.;
    double MaxCrossTrackCm=0.;
};
