#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "FactoryFPVPioneerAnchor.generated.h"

class ACharacter;
class UWorldPartitionStreamingSourceComponent;

enum class EFactoryFPVReturnStatus : uint8
{
    None,
    LoadingGround,
    MissingFloor
};

// Independent of the drone/view target. Lives in the persistent level and can
// outlive the drone briefly while the pioneer floor finishes loading on return.
UCLASS(NotPlaceable, Transient)
class FACTORYFPV_API AFactoryFPVPioneerAnchor : public AActor
{
    GENERATED_BODY()
public:
    AFactoryFPVPioneerAnchor();
    static AFactoryFPVPioneerAnchor* FindPending(UWorld* World, ACharacter* Character);
    bool Hold(ACharacter* Character, FString& Error);
    bool IsHolding() const;
    bool IsReleasePending() const;
    // Presentation-only readback of the same real readiness used by release.
    EFactoryFPVReturnStatus GetReturnStatus() const;
    // Explicit recovery only. Never drops the pioneer when no safe loaded floor exists.
    bool RecoverToNearbyFloor(FString& Error);
    void RequestRelease();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FFactoryFPVPioneerSafetyTest;
    UPROPERTY() TObjectPtr<UWorldPartitionStreamingSourceComponent> Streaming;
    TWeakObjectPtr<ACharacter> Pioneer;
    TWeakObjectPtr<UCharacterMovementComponent> Movement;
    EMovementMode PreviousMode = MOVE_Walking;
    uint8 PreviousCustomMode = 0;
    bool bHolding = false;
    bool bReleaseRequested = false;
    bool IsPioneerAlive() const;
    bool HasFloor() const;
    bool IsDestinationLoaded(const FVector& Location) const;
    void RestoreMovement();
    void TryCompleteRelease();
};
