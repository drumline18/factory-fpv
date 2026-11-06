#include "FactoryFPVRacingContent.h"
#include "FactoryFPVRaceGate.h"
#include "FGBuildCategory.h"
#include "FGRecipeManager.h"
#include "FGSchematicManager.h"
#include "FGSchematicCategory.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

namespace {
template<class T> TSubclassOf<T> RaceGameClass(const TCHAR* Path) {
    return TSoftClassPtr<T>(FSoftObjectPath(Path)).LoadSynchronous();
}
TSubclassOf<UFGItemDescriptor> Part(const TCHAR* Name) {
    return RaceGameClass<UFGItemDescriptor>(*FString::Printf(TEXT("/Game/FactoryGame/Resource/Parts/%s/Desc_%s.Desc_%s_C"),Name,Name,Name));
}
UTexture2D* GateIcon() {
    return LoadObject<UTexture2D>(nullptr,TEXT("/FactoryFPV/Racing/T_RaceGate_Icon.T_RaceGate_Icon"),nullptr,LOAD_NoWarn|LOAD_Quiet);
}
UTexture2D* PoleIcon() {
    auto* Icon=LoadObject<UTexture2D>(nullptr,TEXT("/FactoryFPV/Racing/T_RacePole_Icon.T_RacePole_Icon"),nullptr,LOAD_NoWarn|LOAD_Quiet);
    return Icon ? Icon : GateIcon();
}
TArray<TSubclassOf<UFGRecipe>> RacingRecipes() {
    return {UFactoryFPVRaceGateRecipe::StaticClass(),UFactoryFPVRacePoleRecipe::StaticClass(),
        UFactoryFPVRaceGateFullRecipe::StaticClass(),UFactoryFPVRaceGateVerticalRecipe::StaticClass(),
        UFactoryFPVRaceGateFloorRecipe::StaticClass()};
}
}
UFactoryFPVRaceGateDescriptor::UFactoryFPVRaceGateDescriptor() {
    mBuildableClass=AFactoryFPVRaceGate::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","GateName","FPV Compact Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","GateDescription","A compact powered timing gate, 3.4 m wide with a 3 m by 3 m opening. Connect power, then interact to set its course and checkpoint number. Number 1 is start/finish. Fly through following the arrow.");
    mStackSize=EStackSize::SS_ONE;mCachedStackSize=1;
    mCategory=RaceGameClass<UFGBuildCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"));
    mSubCategories.Add(RaceGameClass<UFGCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Lights.SC_Lights_C")));
    mMenuPriority=490;
    mSmallIcon=GateIcon();mPersistentBigIcon=mSmallIcon;
}
UFactoryFPVRaceGateRecipe::UFactoryFPVRaceGateRecipe() {
    mIngredients={FItemAmount(Part(TEXT("IronPlate")),8),FItemAmount(Part(TEXT("IronRod")),6),FItemAmount(Part(TEXT("Cable")),4),FItemAmount(Part(TEXT("Wire")),10)};
    mProduct={FItemAmount(UFactoryFPVRaceGateDescriptor::StaticClass(),1)};
    mManufactoringDuration=2.f;mManualManufacturingMultiplier=1.f;mManufacturingMenuPriority=490;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"))));
}
UFactoryFPVRaceGateFullDescriptor::UFactoryFPVRaceGateFullDescriptor() {
    mBuildableClass=AFactoryFPVRaceGateFull::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","FullGateName","FPV Full-Size Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","FullGateDescription","A powered timing gate spanning one 8 m foundation, with a 7.6 m wide, 3.6 m high opening. Connect power, then interact to set its course and checkpoint number. Number 1 is start/finish. Fly through following the arrow.");
    mStackSize=EStackSize::SS_ONE;
    mCachedStackSize=1;
    mCategory=RaceGameClass<UFGBuildCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"));
    mSubCategories.Add(RaceGameClass<UFGCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Lights.SC_Lights_C")));
    mMenuPriority=492;
    mSmallIcon=GateIcon();
    mPersistentBigIcon=mSmallIcon;
}
UFactoryFPVRaceGateFullRecipe::UFactoryFPVRaceGateFullRecipe() {
    mIngredients={FItemAmount(Part(TEXT("IronPlate")),12),FItemAmount(Part(TEXT("IronRod")),10),FItemAmount(Part(TEXT("Cable")),6),FItemAmount(Part(TEXT("Wire")),15)};
    mProduct={FItemAmount(UFactoryFPVRaceGateFullDescriptor::StaticClass(),1)};
    mManufactoringDuration=2.f;
    mManualManufacturingMultiplier=1.f;
    mManufacturingMenuPriority=492;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"))));
}
UFactoryFPVRaceGateVerticalDescriptor::UFactoryFPVRaceGateVerticalDescriptor() {
    mBuildableClass=AFactoryFPVRaceGateVertical::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","VerticalGateName","FPV Vertical Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","VerticalGateDescription","A powered horizontal race gate with a 3.6 m square opening raised 2.6 m above the build surface. Leave space above and below it. Interact to choose Ascend or Descend, course and checkpoint number.");
    mStackSize=EStackSize::SS_ONE;
    mCachedStackSize=1;
    mCategory=RaceGameClass<UFGBuildCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"));
    mSubCategories.Add(RaceGameClass<UFGCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Lights.SC_Lights_C")));
    mMenuPriority=493;
    mSmallIcon=GateIcon();
    mPersistentBigIcon=mSmallIcon;
}
UFactoryFPVRaceGateVerticalRecipe::UFactoryFPVRaceGateVerticalRecipe() {
    mIngredients={FItemAmount(Part(TEXT("IronPlate")),10),FItemAmount(Part(TEXT("IronRod")),8),FItemAmount(Part(TEXT("Cable")),4),FItemAmount(Part(TEXT("Wire")),10)};
    mProduct={FItemAmount(UFactoryFPVRaceGateVerticalDescriptor::StaticClass(),1)};
    mManufactoringDuration=2.f;mManualManufacturingMultiplier=1.f;mManufacturingMenuPriority=493;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"))));
}
UFactoryFPVRaceGateFloorDescriptor::UFactoryFPVRaceGateFloorDescriptor() {
    mBuildableClass=AFactoryFPVRaceGateFloor::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","FloorGateName","FPV Floor Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","FloorGateDescription","An 8 m by 8 m floor tile with a 7.2 m square race opening. Leave a tile empty, then aim at the edge of an adjacent flat foundation. Connect power and interact to choose Ascend or Descend, course and checkpoint number.");
    mStackSize=EStackSize::SS_ONE;
    mCachedStackSize=1;
    mCategory=RaceGameClass<UFGBuildCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"));
    mSubCategories.Add(RaceGameClass<UFGCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Lights.SC_Lights_C")));
    mMenuPriority=494;
    mSmallIcon=GateIcon();mPersistentBigIcon=mSmallIcon;
}
UFactoryFPVRaceGateFloorRecipe::UFactoryFPVRaceGateFloorRecipe() {
    mIngredients={FItemAmount(Part(TEXT("IronPlate")),10),FItemAmount(Part(TEXT("IronRod")),8),FItemAmount(Part(TEXT("Cable")),4),FItemAmount(Part(TEXT("Wire")),10)};
    mProduct={FItemAmount(UFactoryFPVRaceGateFloorDescriptor::StaticClass(),1)};
    mManufactoringDuration=2.f;mManualManufacturingMultiplier=1.f;mManufacturingMenuPriority=494;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"))));
}
UFactoryFPVRacePoleDescriptor::UFactoryFPVRacePoleDescriptor() {
    mBuildableClass=AFactoryFPVRacePole::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","PoleName","FPV Racing Pole");
    mDescription=NSLOCTEXT("FactoryFPV","PoleDescription","An open powered racing checkpoint. Interact to choose a course, number and required side. Following the forward arrow, pass on the pole's selected left or right side within 8 m and between 0.2 m and 3.8 m high. No overhead frame or closed orbit is required.");
    mStackSize=EStackSize::SS_ONE;mCachedStackSize=1;
    mCategory=RaceGameClass<UFGBuildCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/BC_Organization.BC_Organization_C"));
    mSubCategories.Add(RaceGameClass<UFGCategory>(TEXT("/Game/FactoryGame/Interface/UI/InGame/BuildMenu/BuildCategories/Sub_Organisation/SC_Lights.SC_Lights_C")));
    mMenuPriority=491;mSmallIcon=PoleIcon();mPersistentBigIcon=mSmallIcon;
}
UFactoryFPVRacePoleRecipe::UFactoryFPVRacePoleRecipe() {
    mIngredients={FItemAmount(Part(TEXT("IronPlate")),4),FItemAmount(Part(TEXT("IronRod")),6),FItemAmount(Part(TEXT("Cable")),2),FItemAmount(Part(TEXT("Wire")),5)};
    mProduct={FItemAmount(UFactoryFPVRacePoleDescriptor::StaticClass(),1)};
    mManufactoringDuration=2.f;mManualManufacturingMultiplier=1.f;mManufacturingMenuPriority=491;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Equipment/BuildGun/BP_BuildGun.BP_BuildGun_C"))));
}
UFactoryFPVRacingSchematic::UFactoryFPVRacingSchematic() {
    mType=ESchematicType::EST_Milestone;mTechTier=1;mMenuPriority=451;
    mDisplayName=NSLOCTEXT("FactoryFPV","RaceMilestone","Factory FPV Racing");
    mDescription=NSLOCTEXT("FactoryFPV","RaceMilestoneDescription","Build powered upright gates, vertical race gates and slalom poles. Time FPV laps through your factory.");
    mCost={FItemAmount(Part(TEXT("IronPlate")),25),FItemAmount(Part(TEXT("IronRod")),25),FItemAmount(Part(TEXT("Cable")),25)};
    mTimeToComplete=60.f;
    mSchematicCategory=RaceGameClass<UFGSchematicCategory>(TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Equipment.SC_Equipment_C"));
    auto UnlockClass=RaceGameClass<UFGUnlockRecipe>(TEXT("/Game/FactoryGame/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"));
    if (UnlockClass) {
        auto* Unlock=Cast<UFGUnlockRecipe>(CreateDefaultSubobject(TEXT("UnlockRaceGateRecipe"),UFGUnlockRecipe::StaticClass(),UnlockClass.Get(),true,false));
        if (Unlock) {
            if (auto* Recipes=FindFProperty<FArrayProperty>(UFGUnlockRecipe::StaticClass(),TEXT("mRecipes")))
                *Recipes->ContainerPtrToValuePtr<TArray<TSubclassOf<UFGRecipe>>>(Unlock)=RacingRecipes();
            mUnlocks.Add(Unlock);
        }
    }
    mSmallSchematicIcon=GateIcon();mSchematicIcon.SetResourceObject(mSmallSchematicIcon);
    mSchematicIcon.ImageSize=FVector2D(256,256);mSchematicIcon.DrawAs=ESlateBrushDrawType::Image;
#if WITH_EDITORONLY_DATA
    if (auto* Property=FindFProperty<FEnumProperty>(UFGSchematic::StaticClass(),TEXT("mIncludeInBuilds")))
        Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(this),static_cast<int64>(EIncludeInBuilds::IIB_PublicBuilds));
#endif
}

bool FactoryFPVRacingContent::EnsureUnlockedRacingRecipes(UWorld* World) {
    if (!World || World->bIsTearingDown || !World->HasBegunPlay() || World->GetNetMode()!=NM_Standalone) return false;
    auto* Schematics=AFGSchematicManager::Get(World);
    auto* Recipes=AFGRecipeManager::Get(World);
    if (!IsValid(Schematics) || !IsValid(Recipes) || !Schematics->HasAuthority() || !Recipes->HasAuthority()) return false;
    if (!Schematics->IsSchematicPurchased(UFactoryFPVRacingSchematic::StaticClass())) return true;
    bool bAllAvailable=true;
    for (const TSubclassOf<UFGRecipe>& Recipe : RacingRecipes()) {
        if (!Recipes->GetAllAvailableRecipes().Contains(Recipe)) Recipes->AddAvailableRecipe(Recipe);
        if (!Recipes->GetAllAvailableRecipes().Contains(Recipe)) bAllAvailable=false;
    }
    return bAllAvailable;
}
