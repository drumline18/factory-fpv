#include "FactoryFPVEquipment.h"
#include "FactoryFPVRacingContent.h"
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGCategory.h"
#include "FGSchematicCategory.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace {
template<class T> TSubclassOf<T> GameClass(const TCHAR* Path) {
    return TSoftClassPtr<T>(FSoftObjectPath(Path)).LoadSynchronous();
}
TSubclassOf<UFGItemDescriptor> IronPlate() {
    return GameClass<UFGItemDescriptor>(TEXT("/Game/FactoryGame/Resource/Parts/IronPlate/Desc_IronPlate.Desc_IronPlate_C"));
}
TSubclassOf<UFGItemDescriptor> Cable() {
    return GameClass<UFGItemDescriptor>(TEXT("/Game/FactoryGame/Resource/Parts/Cable/Desc_Cable.Desc_Cable_C"));
}
TSubclassOf<UFGItemDescriptor> Wire() {
    return GameClass<UFGItemDescriptor>(TEXT("/Game/FactoryGame/Resource/Parts/Wire/Desc_Wire.Desc_Wire_C"));
}
UTexture2D* RadioIcon() {
    // Prefer the dedicated icon when present; keep the content usable without
    // inventing an asset or presenting the radio's UV atlas as an item icon.
    auto* Icon=LoadObject<UTexture2D>(nullptr,TEXT("/FactoryFPV/Pilot/T_Radio_Icon.T_Radio_Icon"),nullptr,LOAD_NoWarn|LOAD_Quiet);
    if (!Icon) Icon=LoadObject<UTexture2D>(nullptr,TEXT("/Game/FactoryGame/Interface/UI/Assets/HUD_Elements/Keybord_Shortcuts/IconDesc_ResourceScanner.IconDesc_ResourceScanner"));
    return Icon;
}
}

AFactoryFPVRadioEquipment::AFactoryFPVRadioEquipment() {
    mEquipmentSlot=EEquipmentSlot::ES_ARMS;
    mArmAnimation=EArmEquipment::AE_Generic2Hand;
    mAttachSocket=TEXT("hand_rSocket");
    // The native Generic2Hand branch requires its explicit idle pose. This
    // original compact grip uses the live 1P rig's anatomical proportions;
    // tools/create-radio-idle.py records the authored attachment transform.
    // It is independent of the remote pilot's third-person body/grip.
    static ConstructorHelpers::FObjectFinder<UAnimSequence> HeldPose(TEXT("/FactoryFPV/Pilot/A_RadioIdle1P.A_RadioIdle1P"));
    auto* IdlePoseProperty=FindFProperty<FObjectPropertyBase>(AFGEquipment::StaticClass(),TEXT("mIdlePoseAnimation"));
    if (HeldPose.Succeeded() && IdlePoseProperty) IdlePoseProperty->SetObjectPropertyValue_InContainer(this,HeldPose.Object);
    else UE_LOG(LogTemp,Error,TEXT("FactoryFPV: native first-person radio idle pose could not be initialized."));
    mNeedsDefaultEquipmentMappingContext=true;
    mDefaultEquipmentActions=static_cast<uint8>(EDefaultEquipmentAction::PrimaryFire|EDefaultEquipmentAction::SecondaryFire);
    if (!GetRootComponent()) SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RadioRoot")));
    Transmitter=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RadioTransmitter"));
    Transmitter->SetupAttachment(GetRootComponent());
    Transmitter->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Transmitter->SetGenerateOverlapEvents(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/FactoryFPV/Pilot/SM_Radio_CM.SM_Radio_CM"));
    Transmitter->SetStaticMesh(Mesh.Object);
    // Use the game's ordinary first-person material swap, matching the gloves'
    // Panini/FOV policy. The mesh's world material remains the third-person one.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> FirstPersonMaterial(TEXT("/FactoryFPV/Pilot/M_Radio1P.M_Radio1P"));
    if (FirstPersonMaterial.Succeeded()) {
        FFirstPersonMaterialArray Entry;
        Entry.FirstPersonMaterials.Add(FirstPersonMaterial.Object);
        mComponentNameToFirstPersonMaterials.Add(Transmitter->GetFName(),MoveTemp(Entry));
    } else UE_LOG(LogTemp,Error,TEXT("FactoryFPV: first-person radio material could not be initialized."));
    // Keep paired with A_RadioIdle1P's authored hand-local attachment marker.
    Transmitter->SetRelativeTransform(FTransform(
        FQuat(.7579310412465046,.09643346661506766,-.6415226384349959,-.0684823160165576),
        FVector(-7.665302319839871,-4.82529540399333,9.858989025739834)));
}

bool AFactoryFPVRadioEquipment::IsEquippedBy(const APlayerController* Controller) {
    const auto* Character=Controller ? Cast<AFGCharacterPlayer>(Controller->GetPawn()) : nullptr;
    return Character && Cast<AFactoryFPVRadioEquipment>(Character->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS))!=nullptr;
}

void AFactoryFPVRadioEquipment::HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action,EDefaultEquipmentActionEvent Event) {
    Super::HandleDefaultEquipmentActionEvent(Action,Event);
    if (Event!=EDefaultEquipmentActionEvent::Pressed || GetNetMode()!=NM_Standalone || !IsLocalInstigator()) return;
    auto* Character=GetInstigatorCharacter();
    auto* PC=Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
    if (!PC || !PC->GetLocalPlayer()) return;
    if (Action==EDefaultEquipmentAction::SecondaryFire && AFactoryFPVDrone::FindActive(GetWorld())) {
        ReturnFromFlight();
        return;
    }
    if (Action==EDefaultEquipmentAction::PrimaryFire || Action==EDefaultEquipmentAction::SecondaryFire) {
        PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()->OpenPanel();
    }
}

void AFactoryFPVRadioEquipment::ReturnFromFlight() {
    if (GetNetMode()!=NM_Standalone || !IsLocalInstigator()) return;
    if (auto* Drone=AFactoryFPVDrone::FindActive(GetWorld())) Drone->ExitDrone();
}
void AFactoryFPVRadioEquipment::UnEquip() {
    ReturnFromFlight();
    Super::UnEquip();
}
void AFactoryFPVRadioEquipment::EndPlay(const EEndPlayReason::Type Reason) {
    ReturnFromFlight();
    Super::EndPlay(Reason);
}

UFactoryFPVRadioDescriptor::UFactoryFPVRadioDescriptor() {
    mEquipmentClass=AFactoryFPVRadioEquipment::StaticClass();
    mUseDisplayNameAndDescription=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","RadioName","FPV Radio Kit");
    mDescription=NSLOCTEXT("FactoryFPV","RadioDescription","A radio controller, FPV goggles and reusable scout quadcopter. Equip in your hands and use Primary Fire to open the hangar. Stand on solid ground before launch. Single-player only.");
    mStackSize=EStackSize::SS_ONE;
    // Native descriptor CDOs do not take the Blueprint asset PostLoad path.
    // Inventory capacity reads this cache, not the SS_ONE enum's numeric value
    // (which is zero). Initialize both for this deliberately one-per-slot kit.
    UE_LOG(LogTemp,Display,TEXT("FactoryFPV kit descriptor %s: inherited cached stack size %d; configured capacity 1"),
        *GetClass()->GetPathName(),mCachedStackSize);
    mCachedStackSize=1;
    mForm=EResourceForm::RF_SOLID;
    mCanBeDiscarded=true;
    mCategory=GameClass<UFGCategory>(TEXT("/Game/FactoryGame/Resource/ItemCategories/Cat_Equipment.Cat_Equipment_C"));
    mSmallIcon=RadioIcon();
    mPersistentBigIcon=mSmallIcon;
    mConveyorMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/FactoryFPV/Pilot/SM_Radio_CM.SM_Radio_CM"));
}

UFactoryFPVRadioRecipe::UFactoryFPVRadioRecipe() {
    mIngredients={FItemAmount(IronPlate(),5),FItemAmount(Cable(),10),FItemAmount(Wire(),20)};
    mProduct={FItemAmount(UFactoryFPVRadioDescriptor::StaticClass(),1)};
    mManufactoringDuration=12.f;
    mManualManufacturingMultiplier=1.f;
    mManufacturingMenuPriority=400.f;
    mProducedIn.Add(TSoftClassPtr<UObject>(FSoftObjectPath(TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"))));
}
UFactoryFPVUnlockRadioRecipe::UFactoryFPVUnlockRadioRecipe() {
    mRecipes.Add(UFactoryFPVRadioRecipe::StaticClass());
}
UFactoryFPVRadioSchematic::UFactoryFPVRadioSchematic() {
    mType=ESchematicType::EST_Milestone;
    mDisplayName=NSLOCTEXT("FactoryFPV","RadioMilestone","Remote Factory Reconnaissance");
    mDescription=NSLOCTEXT("FactoryFPV","RadioMilestoneDescription","Unlock the FPV Radio Kit for remote flights through your factory.");
    mTechTier=1;
    mMenuPriority=450.f;
    mCost={FItemAmount(IronPlate(),25),FItemAmount(Cable(),25),FItemAmount(Wire(),50)};
    mTimeToComplete=60.f;
    mSchematicCategory=GameClass<UFGSchematicCategory>(TEXT("/Game/FactoryGame/Schematics/SchematicCategories/SC_Equipment.SC_Equipment_C"));
    // The standard Blueprint also implements BPI_UnlockableInterface, which
    // the HUB uses to turn unlocks into reward entries. A native-only recipe
    // unlock can register correctly yet produce an empty Rewards panel.
    auto UnlockClass=GameClass<UFGUnlockRecipe>(TEXT("/Game/FactoryGame/Unlocks/BP_UnlockRecipe.BP_UnlockRecipe_C"));
    if (UnlockClass) {
        auto* Unlock=Cast<UFGUnlockRecipe>(CreateDefaultSubobject(TEXT("UnlockRadioKitRecipe"),UFGUnlockRecipe::StaticClass(),UnlockClass.Get(),true,false));
        if (Unlock) {
            if (auto* Recipes=FindFProperty<FArrayProperty>(UFGUnlockRecipe::StaticClass(),TEXT("mRecipes"))) {
                auto* Values=Recipes->ContainerPtrToValuePtr<TArray<TSubclassOf<UFGRecipe>>>(Unlock);
                *Values={UFactoryFPVRadioRecipe::StaticClass()};
            }
            mUnlocks.Add(Unlock);
        }
    }
    mSmallSchematicIcon=RadioIcon();
    mSchematicIcon.SetResourceObject(mSmallSchematicIcon);
    mSchematicIcon.ImageSize=FVector2D(256,256);
    mSchematicIcon.DrawAs=ESlateBrushDrawType::Image;
#if WITH_EDITORONLY_DATA
    // The matching SDK exposes this editor setting to reflection, but keeps
    // the native member private. Set its supported reflected enum value.
    if (auto* Property=FindFProperty<FEnumProperty>(UFGSchematic::StaticClass(),TEXT("mIncludeInBuilds")))
        Property->GetUnderlyingProperty()->SetIntPropertyValue(Property->ContainerPtrToValuePtr<void>(this),static_cast<int64>(EIncludeInBuilds::IIB_PublicBuilds));
#endif
}
UFactoryFPVGameWorldModule::UFactoryFPVGameWorldModule() {
    bRootModule=true;
    ModSubsystems.Add(AFactoryFPVRaceCourseRegistry::StaticClass());
    mSchematics.Add(UFactoryFPVRadioSchematic::StaticClass());
    mSchematics.Add(UFactoryFPVRacingSchematic::StaticClass());
}
