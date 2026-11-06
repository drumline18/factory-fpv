#pragma once
#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Buildables/FGBuildable.h"
#include "FactoryFPVRaceGhost.h"
#include "FactoryFPVRaceGate.generated.h"

class UFGPowerConnectionComponent;
class UFGPowerInfoComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UTextRenderComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class EFactoryFPVRaceGateDirection : uint8 { Forward, Ascend, Descend };

USTRUCT()
struct FFactoryFPVRaceLapRecord {
    GENERATED_BODY()

    UPROPERTY(SaveGame) FString LayoutSignature;
    UPROPERTY(SaveGame) double Seconds=0.;
    UPROPERTY(SaveGame) TArray<double> Splits;
    UPROPERTY(SaveGame) FFactoryFPVRaceGhost Ghost;
};

// A normal saved buildable and real power-grid consumer. The race detector uses
// a plane through a clear physical aperture, never an overlap trigger that can
// be skipped at flight speed. This actor does not own the race timing state.
UCLASS()
class FACTORYFPV_API AFactoryFPVRaceGate : public AFGBuildable {
    GENERATED_BODY()
public:
    AFactoryFPVRaceGate();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void Factory_Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void PostSerializedFromBlueprint(bool bIsBlueprintWorld) override;
    virtual void OnUse_Implementation(AFGCharacterPlayer* Character,const FUseState& State) override;
    virtual bool IsUseable_Implementation() const override;
    virtual FText GetLookAtDecription_Implementation(AFGCharacterPlayer* Character,const FUseState& State) const override;

    static constexpr float PowerConsumptionMW=.1f;
    static constexpr int32 MaximumCourseId=99;
    static constexpr int32 MaximumGateOrder=128;
    static constexpr int32 MaximumBestLapLayouts=16;
    int32 GetCourseId() const { return CourseId; }
    int32 GetGateOrder() const { return GateOrder; }
    static int32 NextCourseBuildOrder(UWorld* World,int32 CourseId);
    FGuid GetGateId() const { return GateGuid; }
    // Idempotent instance initialization. Also useful for deterministic course
    // snapshots after a build/blueprint copy; never modifies a class default.
    void EnsureIdentityAndOrder();
    bool Configure(int32 NewCourseId,int32 NewGateOrder,FString& Error);
    bool Configure(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,FString& Error);
    bool Configure(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,EFactoryFPVRaceGateDirection NewDirection,FString& Error);
    struct FEditorOrderSwap {
        TWeakObjectPtr<AFactoryFPVRaceGate> OtherGate;
        int32 CourseId=0;
        int32 FromOrder=0;
        int32 ToOrder=0;
        int32 RequestedPassSide=1;
        EFactoryFPVRaceGateDirection RequestedDirection=EFactoryFPVRaceGateDirection::Forward;
    private:
        friend class AFactoryFPVRaceGate;
        FGuid SourceId,OtherId;
        int32 PreviousPassSide=1;
        EFactoryFPVRaceGateDirection PreviousDirection=EFactoryFPVRaceGateDirection::Forward;
    };
    // A conflicting editor move leaves both gates untouched. Only a unique
    // occupied slot on the same course can be exchanged after confirmation.
    bool ConfigureFromEditor(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,EFactoryFPVRaceGateDirection NewDirection,
        FString& Error,TOptional<FEditorOrderSwap>& ProposedSwap);
    bool ConfirmEditorOrderSwap(const FEditorOrderSwap& Proposal,FString& Error);
    struct FEditorNumberingCompaction {
        struct FEntry {
            TWeakObjectPtr<AFactoryFPVRaceGate> Gate;
            FGuid GateId;
            int32 OldOrder=0;
        };
        int32 CourseId=0;
        // Sorted by current checkpoint number. The destination is the 1-based index.
        TArray<FEntry> Entries;
    };
    // Only loaded checkpoints can be proposed. The complete snapshot is
    // checked again before applying any changes.
    static bool ProposeEditorNumberingCompaction(UWorld* World,int32 CourseId,FEditorNumberingCompaction& Plan,FString& Error);
    static bool ConfirmEditorNumberingCompaction(UWorld* World,const FEditorNumberingCompaction& Plan,FString& Error);
    bool IsPoleCheckpoint() const { return bPoleCheckpoint; }
    bool IsFullSizeGate() const { return bFullSizeGate; }
    bool IsVerticalRaceGate() const { return bVerticalRaceGate; }
    bool IsFloorRaceGate() const { return bFloorRaceGate; }
    EFactoryFPVRaceGateDirection GetRaceDirection() const;
    int32 GetPolePassSide() const { return PolePassSide<0 ? -1 : 1; }
    FString GetCheckpointInstructions() const;
    void SetNextCheckpointHighlighted(bool Highlighted);
    bool IsNextCheckpointHighlighted() const { return bNextCheckpoint; }
    bool IsLiveRaceGate() const;
    bool IsGatePowered() const;
    UFGPowerInfoComponent* GetPowerInfo() const { return PowerInfo; }
    UFGPowerConnectionComponent* GetPowerConnection() const { return PowerConnection; }

    // Aperture +X is the scoring direction and Y/Z span the opening.
    // Horizontal gates map these axes to the buildable's vertical and floor axes.
    FTransform GetApertureTransform() const;
    FTransform GetApertureTransform(const FTransform& Placement) const;
    FTransform GetLocalApertureTransform() const;
    FVector2D GetApertureHalfExtents() const;
    float GetPoleRadiusCm() const { return 15.f; }
    FVector GetPassageAimPoint() const;
    struct FVerticalClearance {
        bool bOpeningBlocked=false;
        bool bAboveBlocked=false;
        bool bBelowBlocked=false;
        bool HasObstruction() const { return bOpeningBlocked || bAboveBlocked || bBelowBlocked; }
    };
    // A local centerline advisory. Side sweeps are only classified once the
    // opening is clear, so the same central blocker cannot count on both sides.
    FVerticalClearance ProbeVerticalClearance(double DroneRadiusCm) const;

    // Gate 1 exposes the course PB for the exact layout signature. A swap can
    // leave an older record hidden on its original physical start gate.
    double GetBestLapSeconds(const FString& LayoutSignature) const;
    // Cumulative elapsed seconds for checkpoints 2 through N. The start and
    // finish are implicit. Copy this view if retaining it across record edits.
    TConstArrayView<double> GetBestLapSplits(const FString& LayoutSignature,int32 ExpectedSplitCount) const;
    const FFactoryFPVRaceGhost* GetBestLapGhost(const FString& LayoutSignature) const;
    bool RecordLap(double Seconds,const FString& LayoutSignature);
    bool RecordLap(double Seconds,const FString& LayoutSignature,TConstArrayView<double> Splits,const FFactoryFPVRaceGhost* Ghost=nullptr);
    void ResetBestLap(const FString& LayoutSignature);
protected:
    bool bPoleCheckpoint=false;
    bool bFullSizeGate=false;
    bool bVerticalRaceGate=false;
    bool bFloorRaceGate=false;
    void ApplyCheckpointGeometry();
    void CreateVerticalSupports();
private:
    friend class AFactoryFPVRaceGateHologram;
    friend class FFactoryFPVRaceGateTest;
    struct FEditorSlotOccupants {
        AFactoryFPVRaceGate* Destination=nullptr;
        int32 SourceCount=0;
        int32 DestinationCount=0;
    };
    FEditorSlotOccupants FindEditorSlotOccupants(int32 DestinationCourse,int32 DestinationOrder) const;
    bool ValidateConfiguration(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,EFactoryFPVRaceGateDirection NewDirection,FString& Error) const;
    void ApplyConfiguration(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,EFactoryFPVRaceGateDirection NewDirection);
    struct FLapRecordView {
        double Seconds=0.;
        TConstArrayView<double> Splits;
        const FFactoryFPVRaceGhost* Ghost=nullptr;
    };
    FLapRecordView FindBestLap(const FString& LayoutSignature) const;
    void NormalizeBestLapRecords();
    void ClearLegacyBestLap();
    void ClearBestLapRecords();
    void PrepareForWorldEntry();
    void AssignDefaultOrder();
    void CreateLightMaterials();
    void UpdatePresentation();
    UStaticMeshComponent* CreateMeshPart(const TCHAR* Name,bool Collision);
    void ApplyVerticalCheckpointGeometry();
    void ApplyFloorCheckpointGeometry();
    void RefreshClearance();

    UPROPERTY(SaveGame) int32 CourseId=1;
    UPROPERTY(SaveGame) int32 GateOrder=1;
    UPROPERTY(SaveGame) bool bNumberAssigned=false;
    int32 PendingBuildCourse=0; // Set by the construction hologram for this placement only.
    UPROPERTY(SaveGame) int32 PolePassSide=1;
    UPROPERTY(SaveGame) EFactoryFPVRaceGateDirection RaceDirection=EFactoryFPVRaceGateDirection::Forward;
    UPROPERTY(SaveGame,DuplicateTransient) FGuid GateGuid;
    // Oldest successful PB write first. The previous single-record fields
    // remain readable before the first write to an older save's history.
    UPROPERTY(SaveGame,DuplicateTransient) TArray<FFactoryFPVRaceLapRecord> BestLapRecords;
    UPROPERTY(SaveGame,DuplicateTransient) double BestLapSeconds=0;
    UPROPERTY(SaveGame,DuplicateTransient) FString BestLapLayout;
    UPROPERTY(SaveGame,DuplicateTransient) TArray<double> BestLapSplits;
    UPROPERTY(SaveGame) TObjectPtr<UFGPowerInfoComponent> PowerInfo;
    UPROPERTY(SaveGame) TObjectPtr<UFGPowerConnectionComponent> PowerConnection;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> FrameParts;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> VerticalSupports;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> LightStrips;
    UPROPERTY() TArray<TObjectPtr<UPointLightComponent>> Lights;
    UPROPERTY() TArray<TObjectPtr<UTextRenderComponent>> Labels;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> LabelBoard;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> DirectionParts;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> LightMaterials;
    bool bNextCheckpoint=false;
    bool bLastNextCheckpoint=false;
    int32 LastPresentedSide=0;
    EFactoryFPVRaceGateDirection LastPresentedDirection=EFactoryFPVRaceGateDirection::Forward;
    bool bEndingPlay=false;
    bool bPresentationInitialized=false;
    bool bLastPowered=false;
    int32 LastPresentedCourse=0;
    int32 LastPresentedOrder=0;
};

// A distinct saved buildable; the original class remains the compact gate.
UCLASS()
class FACTORYFPV_API AFactoryFPVRaceGateFull : public AFactoryFPVRaceGate {
    GENERATED_BODY()
public:
    AFactoryFPVRaceGateFull();
};

UCLASS()
class FACTORYFPV_API AFactoryFPVRaceGateVertical : public AFactoryFPVRaceGate {
    GENERATED_BODY()
public:
    AFactoryFPVRaceGateVertical();
};

// A foundation-sized alternative. Keeping it a separate saved class retains
// the geometry and timing signatures of raised gates in existing courses.
UCLASS()
class FACTORYFPV_API AFactoryFPVRaceGateFloor : public AFactoryFPVRaceGate {
    GENERATED_BODY()
public:
    AFactoryFPVRaceGateFloor();
    static constexpr double TileSizeCm=800.;
    static constexpr double TileDepthCm=100.;
};

// A persistent powered open checkpoint. The class determines geometry; the
// saved side controls which half of the forward crossing plane can score.
UCLASS()
class FACTORYFPV_API AFactoryFPVRacePole : public AFactoryFPVRaceGate {
    GENERATED_BODY()
public:
    AFactoryFPVRacePole();
};
