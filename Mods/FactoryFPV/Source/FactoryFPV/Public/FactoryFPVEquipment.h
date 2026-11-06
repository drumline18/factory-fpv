#pragma once
#include "CoreMinimal.h"
#include "Equipment/FGEquipment.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "Module/GameWorldModule.h"
#include "FactoryFPVEquipment.generated.h"

class UStaticMeshComponent;
class APlayerController;

// An ordinary craftable arm-slot item. Flight remains local/standalone only.
UCLASS()
class FACTORYFPV_API AFactoryFPVRadioEquipment : public AFGEquipment {
    GENERATED_BODY()
public:
    AFactoryFPVRadioEquipment();
    static bool IsEquippedBy(const APlayerController* Controller);
    virtual void UnEquip() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
    virtual void HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action,EDefaultEquipmentActionEvent Event) override;
private:
    friend class FFactoryFPVEquipmentContentTest;
    void ReturnFromFlight();
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Transmitter;
};

UCLASS()
class FACTORYFPV_API UFactoryFPVRadioDescriptor : public UFGEquipmentDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRadioDescriptor();
private:
    friend class FFactoryFPVEquipmentContentTest;
};

UCLASS()
class FACTORYFPV_API UFactoryFPVRadioRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRadioRecipe();
private:
    friend class FFactoryFPVEquipmentContentTest;
};

UCLASS()
class FACTORYFPV_API UFactoryFPVUnlockRadioRecipe : public UFGUnlockRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVUnlockRadioRecipe();
};

UCLASS()
class FACTORYFPV_API UFactoryFPVRadioSchematic : public UFGSchematic {
    GENERATED_BODY()
public:
    UFactoryFPVRadioSchematic();
private:
    friend class FFactoryFPVEquipmentContentTest;
};

// SML discovers native root modules, as well as Blueprint root assets.
UCLASS()
class FACTORYFPV_API UFactoryFPVGameWorldModule : public UGameWorldModule {
    GENERATED_BODY()
public:
    UFactoryFPVGameWorldModule();
};
