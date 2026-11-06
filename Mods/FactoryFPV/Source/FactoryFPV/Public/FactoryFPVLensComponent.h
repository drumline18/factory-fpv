#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FactoryFPVLensComponent.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

// One optional camera-local post-process pass. Never changes a volume, player
// camera manager or another camera's post-process settings.
UCLASS()
class FACTORYFPV_API UFactoryFPVLensComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UFactoryFPVLensComponent();
    bool ApplyToCamera(UCameraComponent* Camera,float Strength);
    void Clear();
    static float SanitizeStrength(float Strength);
protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    friend class FFactoryFPVLensIsolationTest;
    UPROPERTY() TObjectPtr<UMaterialInterface> LensBase;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LensMaterial;
    TWeakObjectPtr<UCameraComponent> TargetCamera;
};
