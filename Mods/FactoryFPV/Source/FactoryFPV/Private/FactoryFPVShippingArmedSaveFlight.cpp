#include "FactoryFPVShippingArmedSaveFlight.h"

#include "FactoryFPVDrone.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVRaceComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "FGCharacterBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace {
constexpr double HoverAltitudeCm=120.;
constexpr double ColumnHeightCm=185.;
constexpr double MaxCrossTrackDistanceCm=25.;
constexpr double MinimumStableSeconds=.35;

const TCHAR* Flag(bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }
}

FFactoryFPVShippingArmedSaveFlight::~FFactoryFPVShippingArmedSaveFlight() {
    Cancel();
}

bool FFactoryFPVShippingArmedSaveFlight::Begin(AFactoryFPVDrone& Drone,FString& Error) {
    const auto Fail=[&Error](const TCHAR* Reason) { Error=Reason;return false; };
    const TCHAR* CommandLine=FCommandLine::Get();
    FString Stage;
    const bool bGuarded=FParse::Param(CommandLine,TEXT("FactoryFPVAutomation")) &&
        FParse::Param(CommandLine,TEXT("FactoryFPVQAArmedHover")) && FParse::Param(CommandLine,TEXT("FactoryFPVQAAutosave")) &&
        FParse::Param(CommandLine,TEXT("RenderOffScreen")) && FParse::Param(CommandLine,TEXT("SavesUseProjectSavedDir")) &&
        !FParse::Param(CommandLine,TEXT("NoAutoSave")) &&
        FParse::Value(CommandLine,TEXT("FactoryFPVQASave="),Stage) && Stage.Equals(TEXT("save"),ESearchCase::IgnoreCase);
    const auto* World=Drone.GetWorld();
    const auto* PC=Drone.Pilot.Get();
    const auto* Pioneer=Cast<AFGCharacterBase>(Drone.OriginalPawn.Get());
    if (!bGuarded || FlightDrone.IsValid() || !World || World->GetNetMode()!=NM_Standalone || !Drone.HasAnyFlags(RF_Transient) ||
        !PC || !Pioneer || !Pioneer->IsAliveAndWell() || PC->GetPawn()!=Pioneer || PC->GetViewTarget()!=&Drone ||
        !Drone.bSessionActive || Drone.InputMode!=TEXT("keyboard") || Drone.IsFlightArmed() || !Drone.IsActorTickEnabled() ||
        Drone.IsNativeUIBlockingFlight() || !Drone.IsFlightOverlayVisible() || !Drone.Body || !Drone.Race || Drone.Race->GetCourseId()!=0 ||
        !IsValid(Drone.PioneerAnchor) || !Drone.PioneerAnchor->IsHolding()) {
        return Fail(TEXT("Armed autosave QA requires an ordinary disarmed disposable keyboard flight with the original pioneer and visible OSD."));
    }

    LaunchPosition=Drone.LaunchPosition;
    if (!Drone.GetActorLocation().Equals(LaunchPosition,3.) || Drone.Flight.up.z<.999 ||
        !FMath::IsFinite(Drone.NominalThrustToWeight) || Drone.NominalThrustToWeight<1.5) {
        return Fail(TEXT("Armed autosave QA requires an upright launch with enough thrust to hover."));
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingArmedSaveFlight),false,&Drone);
    Query.AddIgnoredActor(Pioneer);
    const auto Shape=FCollisionShape::MakeSphere(Drone.Body->GetScaledSphereRadius());
    const auto Channel=Drone.Body->GetCollisionObjectType();
    const FCollisionResponseParams Responses(Drone.Body->GetCollisionResponseToChannels());
    FHitResult Ground,Ceiling;
    if (!World->SweepSingleByChannel(Ground,LaunchPosition,LaunchPosition-FVector(0,0,12.),FQuat::Identity,Channel,Shape,Query,Responses) ||
        Ground.bStartPenetrating || Ground.ImpactNormal.Z<.5 ||
        World->SweepSingleByChannel(Ceiling,LaunchPosition,LaunchPosition+FVector(0,0,ColumnHeightCm),FQuat::Identity,Channel,Shape,Query,Responses)) {
        return Fail(TEXT("Armed autosave QA requires a supported launch and a clear swept hover column."));
    }

    FrameCount=0;
    StableFrames=0;
    ContinuousStableSeconds=0.;
    CurrentAltitudeCm=0.;
    Thrust=0.;
    MaxCrossTrackCm=0.;
    FlightDrone=&Drone;
    // Keep the real foreground guard intact. Only this disposable actor uses
    // the ordinary post-poll flight path from the QA ticker.
    Drone.SetActorTickEnabled(false);
    bArmedAtZero=Drone.TryArmFlight(factoryfpv::Input{});
    if (!bArmedAtZero) { Cancel();return Fail(TEXT("Production arming refused zero throttle.")); }
    return true;
}

bool FFactoryFPVShippingArmedSaveFlight::Tick(float DeltaSeconds,FString& Error) {
    const auto Fail=[this,&Error](const TCHAR* Reason) {
        StableFrames=0;
        ContinuousStableSeconds=0.;
        Error=Reason;
        return false;
    };
    auto* Drone=FlightDrone.Get();
    const auto* PC=Drone ? Drone->Pilot.Get() : nullptr;
    const auto* Pioneer=Drone ? Cast<AFGCharacterBase>(Drone->OriginalPawn.Get()) : nullptr;
    if (!Drone || !PC || !Pioneer || !Pioneer->IsAliveAndWell() || PC->GetPawn()!=Pioneer || PC->GetViewTarget()!=Drone ||
        !Drone->bSessionActive || Drone->IsActorTickEnabled() || !Drone->IsFlightArmed() || !Drone->IsFlightOverlayVisible() ||
        Drone->IsNativeUIBlockingFlight() || !IsValid(Drone->PioneerAnchor) || !Drone->PioneerAnchor->IsHolding() ||
        !Drone->InputLease.Renew()) {
        return Fail(TEXT("Armed autosave QA lost the armed session, original pioneer, controls or OSD."));
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<0.f) return Fail(TEXT("Armed autosave QA received an invalid frame delta."));
    if (DeltaSeconds==0.f) return true;

    const double Height=Drone->GetActorLocation().Z-LaunchPosition.Z;
    const double Speed=Drone->Flight.velocity.z;
    if (!FMath::IsFinite(Height) || !FMath::IsFinite(Speed)) return Fail(TEXT("Armed autosave QA read a non-finite flight state."));
    const double DesiredSpeed=FMath::Clamp((HoverAltitudeCm-Height)*.015,-1.8,1.8);
    const double DesiredAcceleration=FMath::Clamp((DesiredSpeed-Speed)*3.5,-4.,4.);
    const auto& Parameters=Drone->Flight.parameters;
    const double Lift=Parameters.gravity+DesiredAcceleration+Parameters.linearDrag.z*Speed+
        Parameters.quadraticDrag.z*Speed*FMath::Abs(Speed);
    const double Throttle=FMath::Clamp(Lift/(Parameters.gravity*Drone->NominalThrustToWeight),0.,1.);
    Drone->AdvanceActiveFlightFrame({0.,0.,0.,Throttle,true},DeltaSeconds);

    const FVector Position=Drone->GetActorLocation();
    const double NewAltitudeCm=Position.Z-LaunchPosition.Z;
    const double NewThrust=Drone->Flight.thrust;
    const double CrossTrackCm=FVector::Dist2D(Position,LaunchPosition);
    if (!FMath::IsFinite(NewAltitudeCm) || !FMath::IsFinite(NewThrust) || !FMath::IsFinite(CrossTrackCm) ||
        !FMath::IsFinite(Drone->Flight.velocity.z)) return Fail(TEXT("Armed autosave QA produced a non-finite flight state."));
    CurrentAltitudeCm=NewAltitudeCm;
    Thrust=NewThrust;
    MaxCrossTrackCm=FMath::Max(MaxCrossTrackCm,CrossTrackCm);
    ++FrameCount;
    if (!Drone->IsFlightArmed() || CurrentAltitudeCm<-3. || CurrentAltitudeCm>ColumnHeightCm || CrossTrackCm>MaxCrossTrackDistanceCm) {
        return Fail(TEXT("Armed autosave QA disarmed or left the verified hover column."));
    }
    if (Thrust>.05 && FMath::Abs(CurrentAltitudeCm-HoverAltitudeCm)<=25. && FMath::Abs(Drone->Flight.velocity.z)<=.35) {
        ++StableFrames;
        ContinuousStableSeconds+=FMath::Min(static_cast<double>(DeltaSeconds),.1);
    } else {
        StableFrames=0;
        ContinuousStableSeconds=0.;
    }
    return true;
}

bool FFactoryFPVShippingArmedSaveFlight::IsPoweredHovering() const {
    return FlightDrone.IsValid() && ContinuousStableSeconds>=MinimumStableSeconds;
}

void FFactoryFPVShippingArmedSaveFlight::Cancel() {
    if (auto* Drone=FlightDrone.Get(); Drone && !Drone->IsActorBeingDestroyed()) {
        Drone->SuspendFlightInput();
        Drone->SetActorTickEnabled(true);
    }
    FlightDrone.Reset();
}

FString FFactoryFPVShippingArmedSaveFlight::Evidence() const {
    return FString::Printf(TEXT(",\"armedHoverArmedAtZero\":%s,\"armedHoverPowered\":%s,\"armedHoverFrames\":%d,"
        "\"armedHoverStableFrames\":%d,\"armedHoverStableSeconds\":%.3f,\"armedHoverAltitudeCm\":%.3f,"
        "\"armedHoverThrust\":%.4f,\"armedHoverMaxCrossTrackCm\":%.3f"),
        Flag(bArmedAtZero),Flag(ContinuousStableSeconds>=MinimumStableSeconds),FrameCount,StableFrames,ContinuousStableSeconds,CurrentAltitudeCm,Thrust,MaxCrossTrackCm);
}
