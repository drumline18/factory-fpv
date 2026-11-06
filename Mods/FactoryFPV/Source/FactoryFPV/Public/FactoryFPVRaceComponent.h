#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FactoryFPVRaceDebrief.h"
#include "FactoryFPVRaceGhost.h"
#include "RaceCore.h"
#include "FactoryFPVRaceComponent.generated.h"

class AFactoryFPVRaceGate;
struct FFactoryFPVOSDTelemetry;

UCLASS()
class FACTORYFPV_API UFactoryFPVRaceComponent : public UActorComponent {
    GENERATED_BODY()
public:
    UFactoryFPVRaceComponent();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    static bool ValidateCourse(UWorld* World,int32 CourseId,FString& Error,bool RequirePower=true,const FString* ExpectedLayout=nullptr);
    static FString LayoutSignature(UWorld* World,int32 CourseId);
    bool StartCourse(UWorld* World,int32 CourseId,FString& Error,const FString* ExpectedLayout=nullptr,bool bPractice=false);
    void StopCourse();
    void InvalidateLap();
    void AdvanceFrame(TConstArrayView<FVector> Path,double Seconds,double RadiusCm,TConstArrayView<uint8> ScoringFlags={});
    void FillTelemetry(FFactoryFPVOSDTelemetry& Data) const;
    bool GetNextCheckpointTarget(FVector& Aim,int32& Order,FString& Instruction) const;
    bool GetGhostPose(FVector& Position,FVector& Forward) const;
    bool GetNextCheckpointApproach(const FVector& DronePosition,double ClearanceCm,FVector& Aim,int32& Order,
        FString& Instruction,bool& bOnExitSide,bool* bVerticalGate=nullptr) const;
    int32 GetCourseId() const { return SelectedCourseId; }
    bool IsPractice() const { return bPracticeSession; }
    const factoryfpv::RaceState& GetRaceState() const { return Core.state(); }
    const FFactoryFPVRaceDebrief& GetDebrief() const { return Debrief; }
private:
    enum class ECheckpointNotice : uint8 { WrongOrder, WrongWay };
    void ClearScoringProgress();
    void ResetLapRecording();
    void CaptureDebriefReference();
    void RecordDebriefLap();
    void RefreshStoredBest();
    bool IsGhostActive() const;
    void UpdateCheckpointHighlight();
    TWeakObjectPtr<AFactoryFPVRaceGate> HighlightedGate;
    factoryfpv::RaceCore Core;
    std::vector<factoryfpv::RaceGate> LiveGates;
    TArray<TWeakObjectPtr<AFactoryFPVRaceGate>> LiveActors;
    TArray<FGuid> LiveGateIds;
    TWeakObjectPtr<UWorld> RaceWorld;
    TWeakObjectPtr<AFactoryFPVRaceGate> StartGate;
    FString Signature;
    int32 SelectedCourseId=0;
    double StoredBest=0;
    TArray<double> StoredBestSplits;
    FFactoryFPVRaceDebrief Debrief;
    double LapReferenceSeconds=0;
    TArray<double> LapReferenceSplits;
    FFactoryFPVRaceGhost RecordingGhost;
    FFactoryFPVRaceGhost StoredGhost;
    FVector GhostOrigin=FVector::ZeroVector;
    bool bPracticeSession=false;
    bool bShowGhost=false;
    ECheckpointNotice CheckpointNotice=ECheckpointNotice::WrongOrder;
    double CheckpointNoticeSeconds=0;
    double LapFinishNoticeSeconds=0;
    bool bLastLapWasRecord=false;
    double SplitNoticeSeconds=0;
    int32 SplitGate=0;
    double SplitSeconds=0;
    double SplitDelta=0;
    bool bSplitHasDelta=false;
};
