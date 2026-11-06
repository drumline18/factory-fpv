#include "FactoryFPVShippingVerticalFlight.h"

#include "FactoryFPVDrone.h"
#include "FactoryFPVFlightCueAudioComponent.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRaceGhost.h"
#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "FGCharacterBase.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Types/SlateAttributeMetaData.h"
#include "Widgets/Text/STextBlock.h"

namespace {
constexpr double TopClearanceCm=110.;
constexpr double BottomClearanceCm=120.;
constexpr double MaximumFlightSeconds=20.;

const TCHAR* Flag(bool bValue) { return bValue ? TEXT("true") : TEXT("false"); }

bool IsPresented(const UStaticMeshComponent* Mesh) {
    return Mesh && Mesh->IsRegistered() && Mesh->IsVisible() && !Mesh->bHiddenInGame;
}

UStaticMeshComponent* GhostMesh(const AFactoryFPVDrone& Drone) {
    return Drone.FindComponentByTag<UStaticMeshComponent>(TEXT("FactoryFPVRaceGhost"));
}

bool HasVisibleText(const TSharedRef<SWidget>& Widget,const FString& Text,bool bPrefix,const TCHAR* RequiredFragment) {
    FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(Widget.Get(),FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    if (!Widget->GetVisibility().IsVisible()) return false;
    if (Widget->GetTypeAsString()==TEXT("STextBlock")) {
        const FString Label=StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
        const bool bMatches=bPrefix ? Label.StartsWith(Text,ESearchCase::CaseSensitive) : Label==Text;
        if (bMatches && (!RequiredFragment || Label.Contains(RequiredFragment,ESearchCase::CaseSensitive))) return true;
    }
    auto* Children=Widget->GetChildren();
    for (int32 Index=0;Index<Children->Num();++Index)
        if (HasVisibleText(Children->GetChildAt(Index),Text,bPrefix,RequiredFragment)) return true;
    return false;
}

bool ScoringPathCrossed(TConstArrayView<FVector> Path,TConstArrayView<uint8> Scoring,const FTransform& Aperture,bool bForward) {
    const FVector Normal=Aperture.GetUnitAxis(EAxis::X)*(bForward ? 1. : -1.);
    for (int32 Index=1;Index<Path.Num();++Index) {
        if (Scoring.IsValidIndex(Index-1) && Scoring[Index-1] &&
            FVector::DotProduct(Path[Index-1]-Aperture.GetLocation(),Normal)<=0. &&
            FVector::DotProduct(Path[Index]-Aperture.GetLocation(),Normal)>0.) return true;
    }
    return false;
}
}

bool FFactoryFPVShippingVerticalFlight::Begin(AFactoryFPVDrone& Drone,AFactoryFPVRaceGate& Ascend,AFactoryFPVRaceGate& Descend,FString& Error) {
    const auto Fail=[&Error](const TCHAR* Reason) { Error=Reason;return false; };
    const TCHAR* CommandLine=FCommandLine::Get();
    bCheckGhost=FParse::Param(CommandLine,TEXT("FactoryFPVQARaceGhost"));
    bCheckRaceCues=FParse::Param(CommandLine,TEXT("FactoryFPVQARaceCues"));
    bCheckRaceEdge=FParse::Param(CommandLine,TEXT("FactoryFPVQARaceEdge"));
    const bool bGuarded=FParse::Param(CommandLine,TEXT("FactoryFPVAutomation")) &&
        (FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical")) || FParse::Param(CommandLine,TEXT("FactoryFPVQAVerticalPractice"))) &&
        FParse::Param(CommandLine,TEXT("RenderOffScreen")) && FParse::Param(CommandLine,TEXT("SavesUseProjectSavedDir")) &&
        FParse::Param(CommandLine,TEXT("NoAutoSave"));
    auto* World=Drone.GetWorld();
    const auto* PC=Drone.Pilot.Get();
    if (!bGuarded || Phase!=EPhase::Inactive || !World || World->GetNetMode()!=NM_Standalone || !Drone.HasAnyFlags(RF_Transient) ||
        !PC || PC->GetPawn()!=Drone.OriginalPawn.Get() || PC->GetViewTarget()!=&Drone ||
        !Drone.bSessionActive || Drone.InputMode!=TEXT("keyboard") || Drone.IsFlightArmed() ||
        Drone.IsNativeUIBlockingFlight() || !Drone.IsFlightOverlayVisible() || !Drone.Overlay || !Drone.Body || !Drone.Race ||
        !IsValid(Drone.PioneerAnchor) || !Drone.PioneerAnchor->IsHolding()) {
        return Fail(TEXT("Live vertical QA requires an ordinary disarmed disposable keyboard session with pioneer ownership and a visible OSD."));
    }
    if (bCheckGhost && (!FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical")) || !Drone.Camera)) {
        return Fail(TEXT("Best-lap ghost verification requires the ordinary live-vertical fixture and camera."));
    }
    if (bCheckRaceCues && (!FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical")) ||
        !FParse::Param(CommandLine,TEXT("DeterministicAudio")) || FParse::Param(CommandLine,TEXT("nosound")) || !Drone.CueAudio)) {
        return Fail(TEXT("Race cue verification requires the disposable live-vertical fixture and non-realtime audio."));
    }
    if (bCheckRaceEdge && (!FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical")) || !Drone.Camera)) {
        return Fail(TEXT("Race edge verification requires the disposable live-vertical fixture and camera."));
    }
    using EDirection=EFactoryFPVRaceGateDirection;
    const FTransform Lower=Ascend.GetApertureTransform(),Upper=Descend.GetApertureTransform();
    const FVector LowerCenter=Lower.GetLocation(),UpperCenter=Upper.GetLocation();
    const auto& State=Drone.Race->GetRaceState();
    if (Ascend.GetWorld()!=World || Descend.GetWorld()!=World || !Ascend.IsLiveRaceGate() || !Descend.IsLiveRaceGate() ||
        !Ascend.IsVerticalRaceGate() || !Descend.IsVerticalRaceGate() || !Ascend.IsGatePowered() || !Descend.IsGatePowered() ||
        Ascend.GetRaceDirection()!=EDirection::Ascend || Descend.GetRaceDirection()!=EDirection::Descend ||
        Ascend.GetCourseId()!=Drone.Race->GetCourseId() || Descend.GetCourseId()!=Ascend.GetCourseId() ||
        Ascend.GetGateOrder()!=1 || Descend.GetGateOrder()!=2 || !State.ready || !State.running ||
        State.inLap || State.lapCount!=0 || State.gateCount!=2 || State.nextGate!=1 ||
        !Ascend.IsNextCheckpointHighlighted() || Descend.IsNextCheckpointHighlighted() ||
        Lower.GetUnitAxis(EAxis::X).Z<.999 || Upper.GetUnitAxis(EAxis::X).Z>-.999 ||
        UpperCenter.Z-LowerCenter.Z<350. || UpperCenter.Z-LowerCenter.Z>700. ||
        FVector::Dist2D(LowerCenter,UpperCenter)>10.) {
        return Fail(TEXT("Live vertical QA requires two powered stacked gates with an Ascend start and a Descend checkpoint."));
    }
    Layout=UFactoryFPVRaceComponent::LayoutSignature(World,Ascend.GetCourseId());
    if (Layout.IsEmpty() || Ascend.GetBestLapSeconds(Layout)!=0.) return Fail(TEXT("Live vertical QA requires an untouched timing layout."));
    if (bCheckGhost) {
        FVector Position,Forward;
        bGhostAbsentFirstLap=!Ascend.GetBestLapGhost(Layout) && !Drone.Race->GetGhostPose(Position,Forward) && !IsPresented(GhostMesh(Drone));
        if (!bGhostAbsentFirstLap) return Fail(TEXT("The actual ghost appeared before a first best lap existed."));
    }

    LaunchPosition=Drone.LaunchPosition;
    if (!Drone.GetActorLocation().Equals(LaunchPosition,3.) || FVector::Dist2D(LaunchPosition,LowerCenter)>20. ||
        LowerCenter.Z-LaunchPosition.Z<BottomClearanceCm+50. || Drone.NominalThrustToWeight<1.5) {
        return Fail(TEXT("Live vertical QA requires a supported launch below the lower aperture in a common clear column."));
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingVerticalFlight),false,&Drone);
    Query.AddIgnoredActor(Drone.OriginalPawn.Get());
    const auto Shape=FCollisionShape::MakeSphere(Drone.Body->GetScaledSphereRadius());
    const FCollisionResponseParams Responses(Drone.Body->GetCollisionResponseToChannels());
    const FVector Ceiling(LaunchPosition.X,LaunchPosition.Y,UpperCenter.Z+TopClearanceCm+30.);
    FHitResult Hit;
    if (World->SweepSingleByChannel(Hit,LaunchPosition,Ceiling,FQuat::Identity,Drone.Body->GetCollisionObjectType(),Shape,Query,Responses))
        return Fail(TEXT("The actual drone body cannot sweep through the stacked column."));

    FlightDrone=&Drone;LowerGate=&Ascend;UpperGate=&Descend;
    bOriginalTickEnabled=Drone.IsActorTickEnabled();
    // This run is off-screen. Keep the game's real foreground guard intact and
    // feed the ordinary active-frame path after polling for this actor only.
    Drone.SetActorTickEnabled(false);
    bArmed=Drone.TryArmFlight(factoryfpv::Input{});
    if (!bArmed) { Cancel();return Fail(TEXT("Production arming refused zero throttle.")); }
    Phase=EPhase::AscendingStart;
    return true;
}

bool FFactoryFPVShippingVerticalFlight::VerifyRaceEdgeCue(AFactoryFPVDrone& Drone,int32 ExpectedGate,double VerticalDirection,FString& Error) const {
    auto& Camera=*Drone.Camera;
    auto& Overlay=*Drone.Overlay;
    FVector Aim;
    int32 Order=0;
    FString Instruction;
    bool bOnExitSide=false,bVertical=false;
    if (!Drone.Race->GetNextCheckpointApproach(Drone.GetActorLocation(),Drone.Body->GetScaledSphereRadius(),
        Aim,Order,Instruction,bOnExitSide,&bVertical) || Order!=ExpectedGate || bOnExitSide || !bVertical) {
        Error=TEXT("Race edge verification lost its active vertical entry checkpoint.");
        return false;
    }
    const FVector TowardAim=(Aim-Camera.GetComponentLocation()).GetSafeNormal();
    if (TowardAim.IsNearlyZero() || TowardAim.Z*VerticalDirection>=0.) {
        Error=TEXT("Race edge verification reached the wrong side of the vertical opening.");
        return false;
    }

    // Test the current camera view of the same powered gate without moving the drone or running a physics step.
    const FQuat OriginalRotation=Camera.GetRelativeRotation().Quaternion();
    const FQuat Centered=TowardAim.Rotation().Quaternion();
    const FQuat Away(Centered.GetAxisY(),FMath::DegreesToRadians(-85.*VerticalDirection));
    const factoryfpv::Input Neutral;
    Camera.SetWorldRotation(Away*Centered);
    Drone.UpdateOsd(Neutral);
    const FVector2D OffscreenDirection=Overlay.Data.RaceOffscreenDirection;
    const bool bOffscreenVisible=Overlay.HasRaceEdgeCue() && Overlay.Data.RaceGate==ExpectedGate &&
        OffscreenDirection.Y*VerticalDirection>.95;
    Camera.SetWorldRotation(Centered);
    Drone.UpdateOsd(Neutral);
    const bool bCenteredHidden=Overlay.HasRaceTarget() && Overlay.Data.RaceGate==ExpectedGate &&
        !Overlay.HasRaceEdgeCue() && Overlay.Data.RaceOffscreenDirection.IsNearlyZero();
    Camera.SetRelativeRotation(OriginalRotation);
    Drone.UpdateOsd(Neutral);

    if (!bOffscreenVisible || !bCenteredHidden) {
        Error=FString::Printf(TEXT("Race edge cue did not track the live checkpoint view. gate=%d offscreen=(%.3f,%.3f) edgeVisible=%d centeredHidden=%d"),
            ExpectedGate,OffscreenDirection.X,OffscreenDirection.Y,bOffscreenVisible,bCenteredHidden);
        return false;
    }
    return true;
}

FFactoryFPVShippingVerticalFlight::EResult FFactoryFPVShippingVerticalFlight::Tick(float DeltaSeconds,FString& Error) {
    const auto Fail=[&Error](FString Reason) { Error=MoveTemp(Reason);return EResult::Failed; };
    if (Phase==EPhase::Complete) return EResult::Passed;
    auto* Drone=FlightDrone.Get();
    auto* Lower=LowerGate.Get();
    auto* Upper=UpperGate.Get();
    auto* PC=Drone ? Drone->Pilot.Get() : nullptr;
    auto* Pioneer=Drone ? Cast<AFGCharacterBase>(Drone->OriginalPawn.Get()) : nullptr;
    if (!Drone || !Lower || !Upper || !PC || !Pioneer || !Pioneer->IsAliveAndWell() ||
        !Drone->bSessionActive || PC->GetPawn()!=Pioneer || PC->GetViewTarget()!=Drone ||
        !IsValid(Drone->PioneerAnchor) || !Drone->PioneerAnchor->IsHolding() || Drone->IsNativeUIBlockingFlight() ||
        !Drone->IsFlightArmed() || !Drone->IsFlightOverlayVisible() || !Lower->IsGatePowered() || !Upper->IsGatePowered()) {
        return Fail(TEXT("Live vertical QA lost the armed session, original pioneer, OSD or powered gates."));
    }
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds<=0.f) return EResult::Pending;
    if (bCheckRaceEdge && !bUpEdgeCueVerified) {
        bUpEdgeCueVerified=VerifyRaceEdgeCue(*Drone,1,-1.,Error);
        if (!bUpEdgeCueVerified) return EResult::Failed;
    }

    const FTransform LowerAperture=Lower->GetApertureTransform(),UpperAperture=Upper->GetApertureTransform();
    const double LowerZ=LowerAperture.GetLocation().Z,UpperZ=UpperAperture.GetLocation().Z;
    const bool bDescending=Phase==EPhase::DescendingUpper || Phase==EPhase::ReversingLower || Phase==EPhase::TurningBelow;
    const double TargetZ=Phase==EPhase::GhostReplay ? LowerZ+40. : bDescending ? LowerZ-BottomClearanceCm :
        Phase==EPhase::AscendingFinish ? LowerZ+TopClearanceCm : UpperZ+TopClearanceCm;
    const double Altitude=Drone->GetActorLocation().Z,Speed=Drone->Flight.velocity.z;
    const double DesiredSpeed=FMath::Clamp((TargetZ-Altitude)*.015,-3.2,3.2);
    const double DesiredAcceleration=FMath::Clamp((DesiredSpeed-Speed)*3.5,-6.,6.);
    const auto& Parameters=Drone->Flight.parameters;
    const double Lift=Parameters.gravity+DesiredAcceleration+Parameters.linearDrag.z*Speed+Parameters.quadraticDrag.z*Speed*FMath::Abs(Speed);
    const double Throttle=FMath::Clamp(Lift/(Parameters.gravity*Drone->NominalThrustToWeight),0.,1.);
    Drone->AdvanceActiveFlightFrame({0.,0.,0.,Throttle,true},DeltaSeconds);
    Elapsed+=DeltaSeconds;
    ++FrameCount;

    const FVector Position=Drone->GetActorLocation();
    const auto& State=Drone->Race->GetRaceState();
    MaxCrossTrackCm=FMath::Max(MaxCrossTrackCm,FVector::Dist2D(Position,LaunchPosition));
    if (Position.ContainsNaN() || MaxCrossTrackCm>25. || Position.Z<LaunchPosition.Z-3. || Position.Z>UpperZ+TopClearanceCm+30. ||
        !Drone->IsFlightArmed() || !State.ready || !State.running || State.problem!=factoryfpv::RaceProblem::None ||
        Elapsed>MaximumFlightSeconds) {
        return Fail(FString::Printf(TEXT("The live drone left its allowed flight state. phase=%d elapsed=%.3f z=%.3f vz=%.3f crossTrack=%.3f raceProblem=%d armed=%d"),
            int32(Phase),Elapsed,Position.Z,Drone->Flight.velocity.z,MaxCrossTrackCm,int32(State.problem),Drone->IsFlightArmed()));
    }
    if (bCheckGhost && State.lapCount==0) {
        FVector GhostPosition,GhostForward;
        bGhostAbsentFirstLap=!Drone->Race->GetGhostPose(GhostPosition,GhostForward) && !IsPresented(GhostMesh(*Drone));
        if (!bGhostAbsentFirstLap) return Fail(TEXT("The ghost was eligible or visible during the first timed lap."));
    }
    const auto Hud=[&](const TCHAR* Label,bool bPrefix=false,const TCHAR* RequiredFragment=nullptr) {
        return HasVisibleText(Drone->Overlay.ToSharedRef(),Label,bPrefix,RequiredFragment);
    };
    const auto ActiveGate=[&](int32 Order) {
        return State.inLap && State.nextGate==Order && Lower->IsNextCheckpointHighlighted()==(Order==1) &&
            Upper->IsNextCheckpointHighlighted()==(Order==2);
    };
    const auto Crossed=[&](const FTransform& Aperture,bool bForward) { return ScoringPathCrossed(Drone->RacePath,Drone->RaceScoring,Aperture,bForward); };
    switch (Phase) {
    case EPhase::AscendingStart:
        if (Crossed(LowerAperture,true)) {
            bAscendStart=ActiveGate(2) && State.lapCount==0 && Hud(TEXT("NEXT 2/2"),true,TEXT(" M ABOVE")) && Hud(TEXT("ENTER FROM ABOVE"));
            if (!bAscendStart) return Fail(TEXT("The live Ascend start did not show the upper checkpoint's height and required entry side."));
            Phase=EPhase::ReversingUpper;
        }
        break;
    case EPhase::ReversingUpper:
        if (Crossed(UpperAperture,false)) {
            bWrongWayAbove=ActiveGate(2) && State.lapCount==0 && Hud(TEXT("WRONG WAY - ENTER FROM ABOVE"));
            if (!bWrongWayAbove) return Fail(TEXT("An upward sweep through Descend did not leave that checkpoint active and show the warning."));
            Phase=EPhase::TurningAbove;
        }
        break;
    case EPhase::TurningAbove:
        if (Position.Z>=UpperZ+75. && Drone->Flight.velocity.z<1.6) {
            if (!Hud(TEXT("NEXT 2/2"),true,TEXT(" M BELOW")) || !Hud(TEXT("DESCEND")))
                return Fail(TEXT("Above the Descend checkpoint the HUD did not show the opening below and the legal maneuver."));
            if (bCheckRaceEdge) {
                bDownEdgeCueVerified=VerifyRaceEdgeCue(*Drone,2,1.,Error);
                if (!bDownEdgeCueVerified) return EResult::Failed;
            }
            Phase=EPhase::DescendingUpper;
        }
        break;
    case EPhase::DescendingUpper:
        if (Crossed(UpperAperture,true)) {
            bDescend=ActiveGate(1) && State.lapCount==0 && Hud(TEXT("NEXT 1/2"),true) && Hud(TEXT("GATE 2  /  "),true);
            if (!bDescend) return Fail(TEXT("The live Descend pass did not activate the start gate and its checkpoint split."));
            if (bCheckRaceCues) {
                bCheckpointCueVerified=Drone->CueAudio->GetActiveCue()==EFactoryFPVFlightCue::Checkpoint;
                if (!bCheckpointCueVerified) return Fail(TEXT("A valid live checkpoint did not start the checkpoint cue."));
            }
            Phase=EPhase::ReversingLower;
        }
        break;
    case EPhase::ReversingLower:
        if (Crossed(LowerAperture,false)) {
            bWrongWayBelow=ActiveGate(1) && State.lapCount==0 && Hud(TEXT("WRONG WAY - ENTER FROM BELOW"));
            if (!bWrongWayBelow) return Fail(TEXT("A downward sweep through Ascend did not keep the finish gate active and show the warning."));
            Phase=EPhase::TurningBelow;
        }
        break;
    case EPhase::TurningBelow:
        if (Position.Z<=LowerZ-85. && Drone->Flight.velocity.z>-1.6) Phase=EPhase::AscendingFinish;
        break;
    case EPhase::AscendingFinish:
        if (Crossed(LowerAperture,true)) {
            LapSeconds=State.lastLap;
            const auto Splits=Lower->GetBestLapSplits(Layout,1);
            SplitSeconds=Splits.Num()==1 ? Splits[0] : 0.;
            bLap=ActiveGate(2) && State.lapCount==1 && LapSeconds>0. && Lower->GetBestLapSeconds(Layout)==LapSeconds &&
                SplitSeconds>0. && SplitSeconds<LapSeconds;
            bHud=bLap && Hud(TEXT("NEW COURSE BEST  "),true) && Hud(TEXT("NEXT 2/2"),true);
            if (!bLap || !bHud) return Fail(TEXT("The live Ascend finish did not record the complete lap and split with a new-best HUD."));
            if (bCheckRaceCues) {
                bBestCueVerified=Drone->CueAudio->GetActiveCue()==EFactoryFPVFlightCue::PersonalBest;
                if (!bBestCueVerified) return Fail(TEXT("A scored personal best did not start the record cue."));
            }
            if (bCheckGhost) {
                const auto* Ghost=Lower->GetBestLapGhost(Layout);
                if (!Ghost || !Ghost->IsValid(LapSeconds)) return Fail(TEXT("The actual best lap did not retain valid timed ghost samples."));
                GhostSamples=Ghost->Positions.Num();
                const double OriginZ=Lower->GetActorLocation().Z;
                const auto ReachedUpper=[&](const FVector3f& Point) { return OriginZ+Point.Z>UpperZ; };
                bGhostRecorded=GhostSamples>2 && Ghost->Positions.ContainsByPredicate(ReachedUpper) &&
                    FMath::Abs(OriginZ+Ghost->Positions[0].Z-LowerZ)<25. && FMath::Abs(OriginZ+Ghost->Positions.Last().Z-LowerZ)<25.;
                if (!bGhostRecorded) return Fail(TEXT("The saved ghost did not trace the real climb and return between the start/finish and upper checkpoint."));
                Phase=EPhase::GhostReplay;
                break;
            }
            Phase=EPhase::Complete;
            return EResult::Passed;
        }
        break;
    case EPhase::GhostReplay: {
        FVector GhostPosition,GhostForward;
        if (!State.inLap || State.lapCount!=1 || !Drone->Race->GetGhostPose(GhostPosition,GhostForward) || GhostPosition.ContainsNaN() || GhostForward.ContainsNaN()) {
            return Fail(TEXT("The next timed lap did not obtain an eligible pose from its recorded best."));
        }
        // The ordinary presentation hides a ghost close to the pilot's lens.
        // Hold the live drone low while the recorded climb opens a real gap.
        const double CameraDistance=FVector::Dist(GhostPosition,Drone->Camera->GetComponentLocation());
        const auto* Mesh=GhostMesh(*Drone);
        if (CameraDistance<200. && IsPresented(Mesh)) return Fail(TEXT("The replay was visible inside its nearby hide radius."));
        if (CameraDistance>375.) {
            if (!IsPresented(Mesh) || !Mesh->GetStaticMesh() || !Mesh->GetComponentLocation().Equals(GhostPosition,2.) ||
                Mesh->GetCollisionEnabled()!=ECollisionEnabled::NoCollision || Mesh->GetGenerateOverlapEvents()) {
                return Fail(TEXT("The eligible best lap did not produce a visible noncolliding ghost at its actual replay pose."));
            }
            if (!FirstVisibleGhostPosition) FirstVisibleGhostPosition=GhostPosition;
            GhostAdvanceCm=FVector::Dist(FirstVisibleGhostPosition.GetValue(),GhostPosition);
            if (GhostAdvanceCm>=15.) {
                bGhostReplay=true;
                Drone->SuspendFlightInput();
                Drone->AdvanceActiveFlightFrame(factoryfpv::Input{},DeltaSeconds);
                bGhostCleared=!Drone->Race->GetGhostPose(GhostPosition,GhostForward) && !IsPresented(GhostMesh(*Drone));
                if (!bGhostCleared) return Fail(TEXT("The normal disarm path left the best-lap ghost eligible or visible."));
                Phase=EPhase::Complete;
                return EResult::Passed;
            }
        }
        if (State.elapsed>5.) return Fail(TEXT("The replay never visibly advanced beyond the live drone's nearby hide radius."));
        break;
    }
    default:
        return Fail(TEXT("The live vertical fixture received an invalid flight phase."));
    }
    return EResult::Pending;
}

void FFactoryFPVShippingVerticalFlight::Cancel() {
    if (auto* Drone=FlightDrone.Get();IsValid(Drone) && !Drone->IsActorBeingDestroyed()) {
        Drone->SuspendFlightInput();
        Drone->SetActorTickEnabled(bOriginalTickEnabled);
    }
    FlightDrone.Reset();
}

FString FFactoryFPVShippingVerticalFlight::Evidence() const {
    FString Fields=FString::Printf(TEXT(",\"liveVerticalArmed\":%s,\"liveVerticalAscendStart\":%s,\"liveVerticalWrongWayAbove\":%s,"
        "\"liveVerticalDescend\":%s,\"liveVerticalWrongWayBelow\":%s,\"liveVerticalLapVerified\":%s,\"liveVerticalHudVerified\":%s,"
        "\"liveVerticalFrameCount\":%d,\"liveVerticalLapSeconds\":%.6f,\"liveVerticalSplitSeconds\":%.6f,\"liveVerticalMaxCrossTrackCm\":%.6f"),
        Flag(bArmed),Flag(bAscendStart),Flag(bWrongWayAbove),Flag(bDescend),Flag(bWrongWayBelow),Flag(bLap),Flag(bHud),
        FrameCount,LapSeconds,SplitSeconds,MaxCrossTrackCm);
    if (bCheckGhost) Fields+=FString::Printf(TEXT(",\"liveVerticalGhostAbsentFirstLap\":%s,\"liveVerticalGhostRecorded\":%s,"
        "\"liveVerticalGhostReplay\":%s,\"liveVerticalGhostCleared\":%s,\"liveVerticalGhostSamples\":%d,\"liveVerticalGhostAdvanceCm\":%.6f"),
        Flag(bGhostAbsentFirstLap),Flag(bGhostRecorded),Flag(bGhostReplay),Flag(bGhostCleared),GhostSamples,GhostAdvanceCm);
    if (bCheckRaceCues) Fields+=FString::Printf(TEXT(",\"liveVerticalCheckpointCueVerified\":%s,\"liveVerticalBestCueVerified\":%s"),
        Flag(bCheckpointCueVerified),Flag(bBestCueVerified));
    if (bCheckRaceEdge) Fields+=FString::Printf(TEXT(",\"liveVerticalUpEdgeCueVerified\":%s,\"liveVerticalDownEdgeCueVerified\":%s"),
        Flag(bUpEdgeCueVerified),Flag(bDownEdgeCueVerified));
    return Fields;
}
