#include "FactoryFPVLensComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

UFactoryFPVLensComponent::UFactoryFPVLensComponent() {
    PrimaryComponentTick.bCanEverTick=false;
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/FactoryFPV/Materials/M_FPVLens.M_FPVLens"));
    LensBase=Material.Object;
}

float UFactoryFPVLensComponent::SanitizeStrength(float Strength) {
    return FMath::IsFinite(Strength) ? FMath::Clamp(Strength,0.f,1.f) : 0.f;
}

void UFactoryFPVLensComponent::Clear() {
    if (auto* Camera=TargetCamera.Get()) {
        if (LensMaterial) Camera->PostProcessSettings.RemoveBlendable(LensMaterial.Get());
    }
    TargetCamera.Reset();
}

bool UFactoryFPVLensComponent::ApplyToCamera(UCameraComponent* Camera,float Strength) {
    Strength=SanitizeStrength(Strength);
    if (TargetCamera.Get()!=Camera || Strength==0.f) Clear();
    // Off is an actual bypass: no full-screen pass and the original view is exact.
    if (!Camera || Strength==0.f) return true;
    if (!LensBase) return false;
    if (!LensMaterial) LensMaterial=UMaterialInstanceDynamic::Create(LensBase,this);
    if (!LensMaterial) return false;
    LensMaterial->SetScalarParameterValue(TEXT("LensStrength"),Strength);
    TargetCamera=Camera;
    Camera->PostProcessSettings.AddBlendable(LensMaterial.Get(),1.f);
    return true;
}

void UFactoryFPVLensComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
    Clear();
    Super::EndPlay(EndPlayReason);
}
