#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVSettings.h"
#include "EngineUtils.h"
#include "Engine/World.h"

namespace {
factoryfpv::Vec Vector(const FVector& V) { return {V.X,V.Y,V.Z}; }
FString ProblemText(factoryfpv::RaceProblem Problem) {
    using P=factoryfpv::RaceProblem;
    switch (Problem) {
        case P::UnpoweredGate: return TEXT("COURSE POWER LOST - RESTORE POWER, RESTART AT GATE 1");
        case P::MissingGate: return TEXT("COURSE GATE UNAVAILABLE - RESELECT COURSE");
        case P::ChangedCourse: return TEXT("COURSE CHANGED - RESELECT COURSE");
        case P::Recovery: return TEXT("LAP CANCELLED - RESTART AT GATE 1");
        case P::InvalidCourse: return TEXT("COURSE NEEDS UNIQUE GATES NUMBERED 1 THROUGH N");
        case P::InvalidMovement: return TEXT("LAP CANCELLED - RESTART AT GATE 1");
        default: return FString();
    }
}

const TCHAR* CheckpointEntrySide(const AFactoryFPVRaceGate* Gate) {
    if (Gate) {
        switch (Gate->GetRaceDirection()) {
            case EFactoryFPVRaceGateDirection::Ascend: return TEXT("ENTER FROM BELOW");
            case EFactoryFPVRaceGateDirection::Descend: return TEXT("ENTER FROM ABOVE");
            default: break;
        }
    }
    return TEXT("FIND PINK ENTRY SIDE");
}

using FCourseActors=TArray<TWeakObjectPtr<AFactoryFPVRaceGate>>;

bool CollectCourseActors(UWorld* World,int32 CourseId,FCourseActors& Actors,FString& Error) {
    Actors.Reset();Error.Reset();
    if (!World || CourseId<1 || CourseId>AFactoryFPVRaceGate::MaximumCourseId) { Error=TEXT("Choose a racing course.");return false; }
    for (TActorIterator<AFactoryFPVRaceGate> It(World);It;++It) {
        if (!It->IsLiveRaceGate() || It->GetCourseId()!=CourseId) continue;
        if (!It->GetGateId().IsValid()) {
            Error=FString::Printf(TEXT("Checkpoint %d is still loading. Try again shortly."),It->GetGateOrder());
            return false;
        }
        Actors.Add(*It);
    }
    if (Actors.Num()<2) {
        Error=Actors.IsEmpty() ? TEXT("No checkpoints are loaded. Move near the course or build at least two checkpoints.") :
            TEXT("Only one checkpoint is loaded. Move near the rest or add another. Number them from 1.");
        return false;
    }
    Actors.Sort([](const auto& A,const auto& B) {
        if (A->GetGateOrder()!=B->GetGateOrder()) return A->GetGateOrder()<B->GetGateOrder();
        return A->GetGateId()<B->GetGateId();
    });
    return true;
}

factoryfpv::RaceGateKind GateKind(const AFactoryFPVRaceGate& Gate) {
    using Kind=factoryfpv::RaceGateKind;
    if (!Gate.IsPoleCheckpoint()) return Kind::Gate;
    return Gate.GetPolePassSide()<0 ? Kind::PoleLeft : Kind::PoleRight;
}

void SampleCourse(const FCourseActors& Actors,TArray<FGuid>& SampledIds,std::vector<factoryfpv::RaceGate>& Gates) {
    SampledIds.SetNum(Actors.Num());
    Gates.resize(Actors.Num());
    for (int32 Index=0;Index<Actors.Num();++Index) {
        const auto& Actor=*Actors[Index].Get();
        auto& Gate=Gates[Index];
        if (Gate.id.empty() || SampledIds[Index]!=Actor.GetGateId()) {
            SampledIds[Index]=Actor.GetGateId();
            const FString Id=SampledIds[Index].ToString(EGuidFormats::Digits);
            Gate.id=TCHAR_TO_UTF8(*Id);
        }
        const auto Transform=Actor.GetApertureTransform();
        const auto Extent=Actor.GetApertureHalfExtents();
        const auto Scale=Transform.GetScale3D().GetAbs();
        Gate.order=Actor.GetGateOrder();
        Gate.center=Vector(Transform.GetLocation()/100.);
        Gate.normal=Vector(Transform.GetScaledAxis(EAxis::X).GetSafeNormal());
        Gate.right=Vector(Transform.GetScaledAxis(EAxis::Y).GetSafeNormal());
        Gate.up=Vector(Transform.GetScaledAxis(EAxis::Z).GetSafeNormal());
        Gate.halfWidth=Extent.X*Scale.Y/100.;
        Gate.halfHeight=Extent.Y*Scale.Z/100.;
        Gate.powered=Actor.IsGatePowered();
        Gate.kind=GateKind(Actor);
        Gate.poleRadius=Actor.GetPoleRadiusCm()*Scale.Y/100.;
    }
}

FString BuildLayoutSignature(int32 CourseId,const FCourseActors& Actors) {
    FString Layout=FString::Printf(TEXT("FPV3:C%d;"),CourseId);
    for (const auto& WeakActor:Actors) {
        const auto* Actor=WeakActor.Get();
        // The persisted record follows the native float save pose. Live
        // crossing and fault detection use the full double pose separately.
        const FTransform SavedActor(FTransform3f(Actor->GetActorTransform()));
        const FTransform SavedAperture=Actor->GetApertureTransform(SavedActor);
        const auto Center=SavedAperture.GetLocation()/100.;
        const auto Normal=SavedAperture.GetScaledAxis(EAxis::X).GetSafeNormal();
        const auto Right=SavedAperture.GetScaledAxis(EAxis::Y).GetSafeNormal();
        const auto Up=SavedAperture.GetScaledAxis(EAxis::Z).GetSafeNormal();
        const auto Extent=Actor->GetApertureHalfExtents();
        const auto Scale=SavedAperture.GetScale3D().GetAbs();
        const FString Id=Actor->GetGateId().ToString(EGuidFormats::Digits);
        Layout+=FString::Printf(TEXT("%s:%d:%d:%.9f,%.9f,%.9f:%.9f,%.9f,%.9f:%.9f,%.9f,%.9f:%.9f,%.9f,%.9f:%.9f,%.9f:%.9f;"),
            *Id,Actor->GetGateOrder(),static_cast<int32>(GateKind(*Actor)),Center.X,Center.Y,Center.Z,Normal.X,Normal.Y,Normal.Z,
            Right.X,Right.Y,Right.Z,Up.X,Up.Y,Up.Z,Extent.X*Scale.Y/100.,Extent.Y*Scale.Z/100.,Actor->GetPoleRadiusCm()*Scale.Y/100.);
    }
    return Layout;
}

bool MatchesSelectedLayout(const FString& Layout,const FString* ExpectedLayout,FString& Error) {
    if (!ExpectedLayout || Layout==*ExpectedLayout) return true;
        Error=TEXT("The checkpoint layout changed. Review the course before launching again.");
    return false;
}

bool SelectCourse(factoryfpv::RaceCore& Core,const std::vector<factoryfpv::RaceGate>& Gates,FString& Error,bool RequirePower) {
    if (!Core.select(Gates)) {
        // The sampled gates are already sorted by checkpoint order.
        const auto Duplicate=std::adjacent_find(Gates.begin(),Gates.end(),[](const auto& A,const auto& B) { return A.order==B.order; });
        if (Duplicate!=Gates.end()) {
            Error=FString::Printf(TEXT("Checkpoint %d is assigned more than once. Give each checkpoint a unique number."),Duplicate->order);
            return false;
        }
        int32 Expected=1;
        for (const auto& Gate:Gates) {
            if (Gate.order!=Expected) {
                Error=FString::Printf(TEXT("Checkpoint %d is missing. Move near it or number the loaded checkpoints from 1."),Expected);
                return false;
            }
            ++Expected;
        }
        Error=TEXT("A checkpoint has an invalid identity or opening. Inspect the course checkpoints.");
        return false;
    }
    if (RequirePower && !Core.state().ready) {
        const auto Unpowered=std::find_if(Gates.begin(),Gates.end(),[](const auto& Gate) { return !Gate.powered; });
        if (Unpowered!=Gates.end())
            Error=FString::Printf(TEXT("Checkpoint %d has no power. Connect it and check the circuit fuse."),Unpowered->order);
        else Error=TEXT("The course is unavailable. Check its loaded checkpoints.");
        return false;
    }
    return true;
}
}

UFactoryFPVRaceComponent::UFactoryFPVRaceComponent() { PrimaryComponentTick.bCanEverTick=false; }
void UFactoryFPVRaceComponent::EndPlay(const EEndPlayReason::Type Reason) {
    StopCourse();Super::EndPlay(Reason);
}

bool UFactoryFPVRaceComponent::ValidateCourse(UWorld* World,int32 CourseId,FString& Error,bool RequirePower,const FString* ExpectedLayout) {
    FCourseActors Actors;
    if (!CollectCourseActors(World,CourseId,Actors,Error)) return false;
    TArray<FGuid> Ids;std::vector<factoryfpv::RaceGate> Gates;
    SampleCourse(Actors,Ids,Gates);
    factoryfpv::RaceCore Check;
    return SelectCourse(Check,Gates,Error,RequirePower) &&
        (!ExpectedLayout || MatchesSelectedLayout(BuildLayoutSignature(CourseId,Actors),ExpectedLayout,Error));
}
FString UFactoryFPVRaceComponent::LayoutSignature(UWorld* World,int32 CourseId) {
    FCourseActors Actors;FString Error;
    return CollectCourseActors(World,CourseId,Actors,Error) ? BuildLayoutSignature(CourseId,Actors) : FString();
}
bool UFactoryFPVRaceComponent::StartCourse(UWorld* World,int32 CourseId,FString& Error,const FString* ExpectedLayout,bool bPractice) {
    Error.Reset();
    StopCourse();
    if (CourseId==0) return true;
    if (!CollectCourseActors(World,CourseId,LiveActors,Error)) { StopCourse();return false; }
    SampleCourse(LiveActors,LiveGateIds,LiveGates);
    if (!SelectCourse(Core,LiveGates,Error,true)) { StopCourse();return false; }
    Signature=BuildLayoutSignature(CourseId,LiveActors);
    if (!MatchesSelectedLayout(Signature,ExpectedLayout,Error) || !Core.start()) { StopCourse();return false; }
    StartGate=LiveActors[0];
    GhostOrigin=StartGate->GetActorLocation();
    bPracticeSession=bPractice;
    bShowGhost=GetDefault<UFactoryFPVSettings>()->bShowRaceGhost;
    RaceWorld=World;SelectedCourseId=CourseId;
    Debrief.CourseId=CourseId;
    RefreshStoredBest();
    UpdateCheckpointHighlight();
    return true;
}
void UFactoryFPVRaceComponent::StopCourse() {
    if (HighlightedGate.IsValid()) HighlightedGate->SetNextCheckpointHighlighted(false);
    HighlightedGate.Reset();
    Core=factoryfpv::RaceCore();LiveGates.clear();LiveGateIds.Reset();LiveActors.Reset();RaceWorld.Reset();StartGate.Reset();Signature.Reset();
    SelectedCourseId=0;StoredBest=0;CheckpointNoticeSeconds=0;LapFinishNoticeSeconds=0;bLastLapWasRecord=false;
    StoredBestSplits.Reset();SplitNoticeSeconds=0;
    Debrief={};
    ResetLapRecording();
    bPracticeSession=false;
    StoredGhost.Positions.Reset();bShowGhost=false;
}
void UFactoryFPVRaceComponent::InvalidateLap() {
    if (!SelectedCourseId) return;
    ClearScoringProgress();
    UpdateCheckpointHighlight();
}
void UFactoryFPVRaceComponent::ClearScoringProgress() {
    const auto& State=Core.state();
    // Clear approach tracking even when already idle. Only a real lap was
    // cancelled, and disarm must not hide an existing course fault.
    Core.invalidateLap(State.inLap ? factoryfpv::RaceProblem::Recovery : State.problem);
    ResetLapRecording();
    LapFinishNoticeSeconds=0;
    CheckpointNoticeSeconds=0;
    SplitNoticeSeconds=0;
}

void UFactoryFPVRaceComponent::ResetLapRecording() {
    RecordingGhost.Positions.Reset();
    LapReferenceSeconds=0;
    LapReferenceSplits.Reset();
}

void UFactoryFPVRaceComponent::CaptureDebriefReference() {
    // Freeze the pace available at the start crossing. Refreshing a streamed
    // gate or recording a new best later must not rewrite that reference.
    LapReferenceSeconds=0;
    LapReferenceSplits.Reset();
    if (const auto* Gate=StartGate.Get()) {
        LapReferenceSeconds=Gate->GetBestLapSeconds(Signature);
        const auto Splits=Gate->GetBestLapSplits(Signature,Core.state().gateCount-1);
        if (!Splits.IsEmpty()) LapReferenceSplits.Append(Splits.GetData(),Splits.Num());
    }
}

void UFactoryFPVRaceComponent::RecordDebriefLap() {
    const auto& State=Core.state();
    Debrief.LapCount=State.lapCount;
    Debrief.FastestLap=State.bestLap;
    if (Debrief.RecentLaps.Num()==FFactoryFPVRaceDebrief::MaxRecentLaps)
        Debrief.RecentLaps.RemoveAt(0,1,EAllowShrinking::No);
    Debrief.RecentLaps.Add({State.lapCount,State.lastLap,LapReferenceSeconds,bLastLapWasRecord});

    const auto& Splits=Core.lastLapSplits();
    const int32 SplitCount=static_cast<int32>(Splits.size());
    Debrief.LastLapSplits.Reset(SplitCount);
    for (int32 Index=0;Index<SplitCount;++Index) {
        const double Reference=LapReferenceSplits.IsValidIndex(Index) ? LapReferenceSplits[Index] : 0.;
        Debrief.LastLapSplits.Add({Index+2,Splits[Index],Reference});
    }
}

void UFactoryFPVRaceComponent::RefreshStoredBest() {
    StoredBest=0;
    StoredBestSplits.Reset();
    StoredGhost.Positions.Reset();
    if (const auto* Gate=StartGate.Get()) {
        StoredBest=Gate->GetBestLapSeconds(Signature);
        const auto Splits=Gate->GetBestLapSplits(Signature,Core.state().gateCount-1);
        if (!Splits.IsEmpty()) StoredBestSplits.Append(Splits.GetData(),Splits.Num());
        if (const auto* Ghost=Gate->GetBestLapGhost(Signature)) StoredGhost=*Ghost;
    }
}
void UFactoryFPVRaceComponent::AdvanceFrame(TConstArrayView<FVector> Path,double Seconds,double RadiusCm,TConstArrayView<uint8> ScoringFlags) {
    if (!SelectedCourseId || Path.IsEmpty() || !FMath::IsFinite(Seconds) || Seconds<0) return;
    CheckpointNoticeSeconds=FMath::Max(0.,CheckpointNoticeSeconds-Seconds);
    LapFinishNoticeSeconds=FMath::Max(0.,LapFinishNoticeSeconds-Seconds);
    SplitNoticeSeconds=FMath::Max(0.,SplitNoticeSeconds-Seconds);
    FString Error;
    if (!CollectCourseActors(RaceWorld.Get(),SelectedCourseId,LiveActors,Error)) {
        // An incomplete or uninitialized sample must cancel the existing run.
        LiveActors.Reset();LiveGateIds.Reset();LiveGates.clear();
    } else {
        SampleCourse(LiveActors,LiveGateIds,LiveGates);
        // A replacement start can own future records only if the entire saved
        // layout matches. Stable frames need no save-key formatting.
        const auto& LiveStart=LiveActors[0];
        if (StartGate!=LiveStart && LiveStart->GetGateOrder()==1 && BuildLayoutSignature(SelectedCourseId,LiveActors)==Signature) {
            StartGate=LiveStart;GhostOrigin=LiveStart->GetActorLocation();RefreshStoredBest();
        }
    }
    Core.refreshLive(LiveGates);
    if (!Core.state().ready) {
        ResetLapRecording();
        LapFinishNoticeSeconds=0;
        CheckpointNoticeSeconds=0;
        SplitNoticeSeconds=0;
        UpdateCheckpointHighlight();
        return;
    }
    const int32 Segments=FMath::Max(1,Path.Num()-1);
    const double Duration=Seconds/Segments;
    for (int32 Index=0;Index<Segments;++Index) {
        const FVector& From=Path[FMath::Min(Index,Path.Num()-1)];
        const FVector& To=Path[FMath::Min(Index+1,Path.Num()-1)];
        const bool ScoringAllowed=ScoringFlags.IsEmpty() || (ScoringFlags.IsValidIndex(Index) && ScoringFlags[Index]!=0);
        if (!ScoringAllowed) ClearScoringProgress();
        const double PriorElapsed=Core.state().elapsed;
        const auto Update=Core.advanceSegment(Vector(From/100.),Vector(To/100.),Duration,RadiusCm/100.,ScoringAllowed);
        const auto& State=Core.state();
        FVector StartCrossing;
        if (Update.lapStarted && !bPracticeSession) {
            // The timer already retained the remainder of this segment after the
            // interpolated gate-1 crossing. Use the same instant for both traces.
            const double Fraction=FMath::Clamp(1.-State.elapsed/Duration,0.,1.);
            StartCrossing=FMath::Lerp(From,To,Fraction)-GhostOrigin;
        }
        if (Update.wrongWay || Update.wrongOrder) {
            CheckpointNotice=Update.wrongWay ? ECheckpointNotice::WrongWay : ECheckpointNotice::WrongOrder;
            CheckpointNoticeSeconds=2.;
        } else if (Update.accepted || Update.lapInvalidated) CheckpointNoticeSeconds=0;
        if (Update.lapInvalidated) {
            LapFinishNoticeSeconds=0;
            ResetLapRecording();
        }
        if (Update.lapStarted || Update.lapInvalidated) SplitNoticeSeconds=0;
        if (Update.lapCompleted) {
            LapFinishNoticeSeconds=3.;
            bLastLapWasRecord=false;
            // Practice uses the live timing core, but the start gate owns the
            // official saved time, splits and replay for this layout.
            if (!bPracticeSession) {
                RecordingGhost.Append(From-GhostOrigin,StartCrossing,PriorElapsed,State.lastLap);
                RecordingGhost.Finish(StartCrossing,State.lastLap);
                if (auto* Gate=StartGate.Get()) {
                    const auto& Splits=Core.lastLapSplits();
                    bLastLapWasRecord=Gate->RecordLap(State.lastLap,Signature,
                        MakeArrayView(Splits.data(),static_cast<int32>(Splits.size())),&RecordingGhost);
                    RefreshStoredBest();
                }
            }
            RecordDebriefLap();
        }
        if (Update.lapStarted) CaptureDebriefReference();
        if (!bPracticeSession) {
            if (Update.lapStarted) {
                RecordingGhost.Start(StartCrossing);
                RecordingGhost.Append(StartCrossing,To-GhostOrigin,0.,State.elapsed);
            } else if (ScoringAllowed && State.inLap) {
                RecordingGhost.Append(From-GhostOrigin,To-GhostOrigin,PriorElapsed,State.elapsed);
            }
        }
        if (Update.splitOrder>=2) {
            SplitGate=Update.splitOrder;
            SplitSeconds=Update.splitElapsed;
            const int32 SplitIndex=SplitGate-2;
            bSplitHasDelta=StoredBestSplits.IsValidIndex(SplitIndex);
            SplitDelta=bSplitHasDelta ? SplitSeconds-StoredBestSplits[SplitIndex] : 0.;
            SplitNoticeSeconds=2.5;
        }
    }
    UpdateCheckpointHighlight();
}

bool UFactoryFPVRaceComponent::IsGhostActive() const {
    const auto& State=Core.state();
    return bShowGhost && State.ready && State.inLap && StoredGhost.Positions.Num()>=2 && State.elapsed<StoredBest;
}

bool UFactoryFPVRaceComponent::GetGhostPose(FVector& Position,FVector& Forward) const {
    if (!IsGhostActive()) return false;
    const double Time=Core.state().elapsed;
    Position=GhostOrigin+StoredGhost.Sample(Time,StoredBest);
    Forward=(StoredGhost.Sample(Time+.1,StoredBest)-StoredGhost.Sample(Time-.1,StoredBest)).GetSafeNormal();
    return true;
}
void UFactoryFPVRaceComponent::UpdateCheckpointHighlight() {
    AFactoryFPVRaceGate* Next=nullptr;
    const auto& State=Core.state();
    const int32 NextIndex=State.nextGate-1;
    if (SelectedCourseId && State.running && State.ready && LiveActors.IsValidIndex(NextIndex)) {
        auto* Candidate=LiveActors[NextIndex].Get();
        if (Candidate && Candidate->IsGatePowered() && Candidate->GetCourseId()==SelectedCourseId && Candidate->GetGateOrder()==State.nextGate)
            Next=Candidate;
    }
    if (Next==HighlightedGate.Get()) {
        // Streamed re-entry can retain this actor object but reset its local
        // presentation state. The cached pointer alone is not ownership proof.
        if (Next && !Next->IsNextCheckpointHighlighted()) Next->SetNextCheckpointHighlighted(true);
        return;
    }
    if (HighlightedGate.IsValid()) HighlightedGate->SetNextCheckpointHighlighted(false);
    HighlightedGate=Next;
    if (Next) Next->SetNextCheckpointHighlighted(true);
}
bool UFactoryFPVRaceComponent::GetNextCheckpointTarget(FVector& Aim,int32& Order,FString& Instruction) const {
    Aim=FVector::ZeroVector;Order=0;Instruction.Reset();
    const auto* Next=HighlightedGate.Get();
    if (!Next || !Core.state().ready || !Next->IsLiveRaceGate() || !Next->IsGatePowered()) return false;
    Aim=Next->GetPassageAimPoint();Order=Next->GetGateOrder();
    if (Next->IsPoleCheckpoint()) Instruction=Next->GetPolePassSide()>0 ? TEXT("PASS RIGHT") : TEXT("PASS LEFT");
    else if (Next->IsVerticalRaceGate()) Instruction=Next->GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend ? TEXT("DESCEND") : TEXT("ASCEND");
    return !Aim.ContainsNaN();
}
bool UFactoryFPVRaceComponent::GetNextCheckpointApproach(const FVector& DronePosition,double ClearanceCm,FVector& Aim,int32& Order,
    FString& Instruction,bool& bOnExitSide,bool* bVerticalGate) const {
    bOnExitSide=false;
    if (bVerticalGate) *bVerticalGate=false;
    if (DronePosition.ContainsNaN()) { Aim=FVector::ZeroVector;Order=0;Instruction.Reset();return false; }
    if (!GetNextCheckpointTarget(Aim,Order,Instruction)) return false;
    const auto* Next=HighlightedGate.Get();
    if (bVerticalGate) *bVerticalGate=Next->IsVerticalRaceGate();
    const auto Aperture=Next->GetApertureTransform();
    const FVector Normal=Aperture.GetScaledAxis(EAxis::X).GetSafeNormal();
    const double SignedDistance=FVector::DotProduct(DronePosition-Aperture.GetLocation(),Normal);
    // Use the scoring body's side and leave a small margin at the plane. The
    // target remains the passage itself, so the cue does not suggest a route.
    const double Margin=FMath::IsFinite(ClearanceCm) ? FMath::Max(20.,ClearanceCm) : 20.;
    bOnExitSide=FMath::IsFinite(SignedDistance) && SignedDistance>Margin;
    if (bOnExitSide) Instruction=CheckpointEntrySide(Next);
    return true;
}
void UFactoryFPVRaceComponent::FillTelemetry(FFactoryFPVOSDTelemetry& Data) const {
    Data.bRacing=false;Data.bRaceWrongWay=false;Data.RaceCourse=Data.RaceGate=Data.RaceGateCount=0;Data.RaceLaps=0;
    Data.bRacePractice=bPracticeSession;
    Data.RaceElapsed=Data.RaceLast=Data.RaceBest=0;Data.RaceStatus.Reset();Data.RaceFinish=EFactoryFPVRaceFinish::None;
    Data.RaceSplitGate=0;Data.RaceSplitSeconds=Data.RaceSplitDelta=0.;Data.bRaceSplitHasDelta=false;
    Data.bRaceGhostActive=IsGhostActive();
    if (!SelectedCourseId) return;
    const auto& State=Core.state();
    Data.bRacing=true;Data.RaceCourse=SelectedCourseId;Data.RaceGate=State.nextGate;
    Data.RaceGateCount=State.gateCount;Data.RaceLaps=State.lapCount;Data.RaceElapsed=State.elapsed;
    Data.RaceLast=State.lastLap;
    Data.RaceBest=bPracticeSession || StoredBest>0 ? StoredBest : State.bestLap;
    Data.RaceStatus=ProblemText(State.problem);
    if (CheckpointNoticeSeconds>0 && CheckpointNotice==ECheckpointNotice::WrongWay &&
        (Data.RaceStatus.IsEmpty() || State.problem==factoryfpv::RaceProblem::Recovery)) {
        Data.bRaceWrongWay=true;
        Data.RaceStatus=FString::Printf(TEXT("WRONG WAY - %s"),CheckpointEntrySide(HighlightedGate.Get()));
    }
    if (Data.RaceStatus.IsEmpty()) Data.RaceStatus=CheckpointNoticeSeconds>0 ? TEXT("WRONG GATE - FOLLOW CHECKPOINT ORDER") :
        (State.inLap ? FString() : TEXT("CROSS GATE 1 FROM THE PINK SIDE TO START"));
    if (Data.RaceStatus.IsEmpty() && LapFinishNoticeSeconds>0)
        Data.RaceFinish=bLastLapWasRecord ? EFactoryFPVRaceFinish::CourseBest : EFactoryFPVRaceFinish::LapComplete;
    if (State.inLap && SplitNoticeSeconds>0) {
        Data.RaceSplitGate=SplitGate;
        Data.RaceSplitSeconds=SplitSeconds;
        Data.RaceSplitDelta=SplitDelta;
        Data.bRaceSplitHasDelta=bSplitHasDelta;
    }
}
