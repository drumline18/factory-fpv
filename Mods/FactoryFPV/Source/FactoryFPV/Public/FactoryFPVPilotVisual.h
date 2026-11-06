#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FactoryFPVPilotVisual.generated.h"

class ACharacter;
class UPrimitiveComponent;
class UPoseableMeshComponent;
class UStaticMeshComponent;

// A transient presentation only: never possess, equip, or modify character animation.
UCLASS(NotPlaceable, Transient)
class FACTORYFPV_API AFactoryFPVPilotVisual : public AActor {
    GENERATED_BODY()
public:
    AFactoryFPVPilotVisual();
    bool Begin(ACharacter* Character);
    void Release();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void Destroyed() override;
private:
    friend class FFactoryFPVPilotVisualTest;
    bool PoseArm(bool bLeft);
    void HideOriginal(UPrimitiveComponent* Component);
    struct FHiddenPart {
        TWeakObjectPtr<UPrimitiveComponent> Component;
        bool bHidden=false;
        bool bHiddenShadow=false;
    };
    TArray<FHiddenPart> HiddenParts;
    UPROPERTY() TObjectPtr<UPoseableMeshComponent> Body;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Radio;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Goggles;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Accessories;
    bool bReleased=false;
};
