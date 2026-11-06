#include "FactoryFPVEquipment.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Components/StaticMeshComponent.h"
#include "FGSchematicCategory.h"
#include "ModLoading/PluginModuleLoader.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVEquipmentContentTest,
    "FactoryFPV.Equipment.ContentAndDiscovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVEquipmentContentTest::RunTest(const FString&) {
    // These test the real native content graph. The SDK does not implement the
    // game's crafting/equip behavior, and SML world-module loading is disabled
    // in the editor. Actual HUB/workshop/equip acceptance remains a shipping test.
    const auto* Equipment=GetDefault<AFactoryFPVRadioEquipment>();
    const auto* Descriptor=GetDefault<UFactoryFPVRadioDescriptor>();
    const auto* Recipe=GetDefault<UFactoryFPVRadioRecipe>();
    const auto* Schematic=GetDefault<UFactoryFPVRadioSchematic>();
    const auto* Module=GetDefault<UFactoryFPVGameWorldModule>();
    TestEqual(TEXT("Radio equips in the ordinary hand slot"),Equipment->mEquipmentSlot,EEquipmentSlot::ES_ARMS);
    const auto* HeldPose=LoadObject<UAnimSequence>(nullptr,TEXT("/FactoryFPV/Pilot/A_RadioIdle1P.A_RadioIdle1P"));
    const auto* NativeSkeleton=LoadObject<USkeleton>(nullptr,TEXT("/Game/FactoryGame/Character/Player/Mesh/1PCharacter_Skeleton.1PCharacter_Skeleton"));
    TestNotNull(TEXT("Original radio first-person pose resolves"),HeldPose);
    TestNotNull(TEXT("Actual native first-person skeleton resolves"),NativeSkeleton);
    TestEqual(TEXT("Native radio uses the data-driven two-hand animation branch"),Equipment->GetArmsAnimation(),EArmEquipment::AE_Generic2Hand);
    TestTrue(TEXT("Radio supplies its original idle pose to the native equipment animation branch"),HeldPose && Equipment->GetIdlePoseAnimation()==HeldPose);
    if (HeldPose) {
        TestTrue(TEXT("Radio pose targets the actual first-person skeleton"),NativeSkeleton && HeldPose->GetSkeleton()==NativeSkeleton);
        TestTrue(TEXT("Radio idle has a nonzero playback duration"),HeldPose->GetPlayLength()>0.f);
        TestFalse(TEXT("Holding the radio cannot drive pioneer root motion"),HeldPose->bEnableRootMotion);
#if WITH_EDITOR
        // Evaluate source tracks over the skeleton reference hierarchy, without
        // using SDK placeholder geometry or claiming runtime retargeting proof.
        // The pose and equipment offset must agree geometrically at both keys.
        const auto* Model=HeldPose->GetDataModel();
        TestNotNull(TEXT("Original radio idle retains its source animation model"),Model);
        if (Model) {
            TestEqual(TEXT("Radio idle is a one-frame static loop"),Model->GetNumberOfFrames(),1);
            TestEqual(TEXT("Radio idle has two source keys"),Model->GetNumberOfKeys(),2);
        }
        if (Model && NativeSkeleton && Model->GetNumberOfKeys()==2 && Equipment->Transmitter && Equipment->GetRootComponent()) {
            const auto& Ref=NativeSkeleton->GetReferenceSkeleton();
            const int32 Marker=Ref.FindBoneIndex(TEXT("hand_rSocket"));
            const int32 Left=Ref.FindBoneIndex(TEXT("hand_l"));
            const int32 Right=Ref.FindBoneIndex(TEXT("hand_r"));
            TestTrue(TEXT("Native skeleton provides both wrists and the radio attachment marker"),Marker!=INDEX_NONE && Left!=INDEX_NONE && Right!=INDEX_NONE);
            TestTrue(TEXT("The radio attachment marker has an authored track"),Model->IsValidBoneTrackName(TEXT("hand_rSocket")));
            if (Marker!=INDEX_NONE && Left!=INDEX_NONE && Right!=INDEX_NONE && Model->IsValidBoneTrackName(TEXT("hand_rSocket"))) {
                TestEqual(TEXT("Radio marker remains a direct child of the right wrist"),Ref.GetParentIndex(Marker),Right);
                for (int32 Key=0;Key<2;++Key) {
                    const FTransform MarkerLocal=Model->GetBoneTrackTransform(TEXT("hand_rSocket"),FFrameNumber(Key));
                    TestTrue(*FString::Printf(TEXT("Radio attachment marker is finite and identity at key %d"),Key),
                        MarkerLocal.IsValid() && MarkerLocal.Equals(FTransform::Identity,1.e-6));
                    TArray<FTransform> Component;
                    Component.SetNum(Ref.GetNum());
                    bool bValidHierarchy=Ref.GetNum()>0;
                    for (int32 Index=0;Index<Ref.GetNum();++Index) {
                        const FName Name=Ref.GetBoneName(Index);
                        const int32 Parent=Ref.GetParentIndex(Index);
                        const FTransform Local=Model->IsValidBoneTrackName(Name) ? Model->GetBoneTrackTransform(Name,FFrameNumber(Key)) : Ref.GetRefBonePose()[Index];
                        if (!Local.IsValid() || Parent<INDEX_NONE || Parent>=Index) { bValidHierarchy=false;break; }
                        Component[Index]=Parent==INDEX_NONE ? Local : Local*Component[Parent];
                        if (!Component[Index].IsValid()) { bValidHierarchy=false;break; }
                    }
                    TestTrue(*FString::Printf(TEXT("Radio source pose produces finite native skeleton FK at key %d"),Key),bValidHierarchy);
                    if (!bValidHierarchy) continue;
                    // The native 1P layer solves the wrists from these helper
                    // bones after the idle pose; stale targets undo a valid grip.
                    for (const TCHAR* Helper:{TEXT("ik_hand_gun"),TEXT("ik_hand_r"),TEXT("ik_hand_l")}) {
                        const FName Name(Helper);
                        const int32 Index=Ref.FindBoneIndex(Name);
                        const bool bAuthored=Index!=INDEX_NONE && Model->IsValidBoneTrackName(Name);
                        TestTrue(*FString::Printf(TEXT("Native IK target %s is explicitly authored at key %d"),Helper,Key),bAuthored);
                        if (!bAuthored) continue;
                        const auto& Wrist=Component[Name==TEXT("ik_hand_l") ? Left : Right];
                        const double PositionError=FVector::Distance(Component[Index].GetTranslation(),Wrist.GetTranslation());
                        const double AngleError=FMath::RadiansToDegrees(Component[Index].GetRotation().AngularDistance(Wrist.GetRotation()));
                        TestTrue(*FString::Printf(TEXT("Native IK target %s matches its wrist position within 0.05 cm at key %d (%.6f cm)"),Helper,Key,PositionError),
                            FMath::IsFinite(PositionError) && PositionError<=.05);
                        TestTrue(*FString::Printf(TEXT("Native IK target %s matches its wrist orientation within 0.1 degree at key %d (%.6f degrees)"),Helper,Key,AngleError),
                            FMath::IsFinite(AngleError) && AngleError<=.1);
                    }
                    const FTransform Radio=Equipment->Transmitter->GetRelativeTransform()*Equipment->GetRootComponent()->GetRelativeTransform()*Component[Marker];
                    for (int32 Side=0;Side<2;++Side) {
                        const bool bLeft=Side==0;
                        const FVector Grip=Radio.TransformPosition(FVector(bLeft ? -11.8 : 11.8,-2.,-6.));
                        const double Error=FVector::Distance(Component[bLeft ? Left : Right].GetTranslation(),Grip);
                        TestTrue(*FString::Printf(TEXT("%s wrist meets the actual transmitter grip within 0.5 cm at key %d (%.6f cm)"),
                            bLeft ? TEXT("Left") : TEXT("Right"),Key,Error),FMath::IsFinite(Error) && Error<=.5);
                    }
                }
            }
        }
#endif
    }
    TestTrue(TEXT("Radio has its supplied transmitter mesh"),Equipment->Transmitter && Equipment->Transmitter->GetStaticMesh());
    const auto* FirstPersonMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/FactoryFPV/Pilot/M_Radio1P.M_Radio1P"));
    const auto* WorldMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/FactoryFPV/Pilot/M_Radio.M_Radio"));
    TestNotNull(TEXT("Native-policy first-person radio material resolves"),FirstPersonMaterial);
    TestNotNull(TEXT("Original world radio material resolves"),WorldMaterial);
    const auto* MappingProperty=FindFProperty<FMapProperty>(AFGEquipment::StaticClass(),TEXT("mComponentNameToFirstPersonMaterials"));
    TestNotNull(TEXT("Native equipment first-person material map resolves"),MappingProperty);
    if (MappingProperty && Equipment->Transmitter) {
        const auto* Mapping=MappingProperty->ContainerPtrToValuePtr<TMap<FName,FFirstPersonMaterialArray>>(Equipment);
        const auto* Entry=Mapping->Find(Equipment->Transmitter->GetFName());
        TestTrue(TEXT("The actual transmitter component has exactly its own first-person material mapping"),
            Entry && Entry->FirstPersonMaterials.Num()==1 && Entry->FirstPersonMaterials[0]==FirstPersonMaterial);
        TestTrue(TEXT("The unequipped CDO retains the original world material"),Equipment->Transmitter->GetMaterial(0)==WorldMaterial);
        TestTrue(TEXT("First-person mapping never aliases the unchanged world material"),FirstPersonMaterial && FirstPersonMaterial!=WorldMaterial);
    }
    TestEqual(TEXT("Held transmitter has no gameplay collision"),Equipment->Transmitter->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Stock equipment actions are enabled"),Equipment->mNeedsDefaultEquipmentMappingContext);
    TestTrue(TEXT("Primary action is bound"),(Equipment->mDefaultEquipmentActions&static_cast<uint8>(EDefaultEquipmentAction::PrimaryFire))!=0);
    TestTrue(TEXT("Secondary action is bound"),(Equipment->mDefaultEquipmentActions&static_cast<uint8>(EDefaultEquipmentAction::SecondaryFire))!=0);
    TestFalse(TEXT("Missing controller never counts as equipped"),AFactoryFPVRadioEquipment::IsEquippedBy(nullptr));
    TestTrue(TEXT("Descriptor spawns the radio equipment"),Descriptor->mEquipmentClass==AFactoryFPVRadioEquipment::StaticClass());
    TestEqual(TEXT("Equipment does not stack"),Descriptor->mStackSize,EStackSize::SS_ONE);
    TestEqual(TEXT("Native descriptor initializes usable inventory capacity before asset PostLoad"),Descriptor->mCachedStackSize,1);
    TestEqual(TEXT("Inventory stack-size API accepts one radio kit per empty slot"),
        UFGItemDescriptor::GetStackSize(UFactoryFPVRadioDescriptor::StaticClass()),1);
    TestFalse(TEXT("Crafted radio descriptor is a concrete item class"),
        UFactoryFPVRadioDescriptor::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
    TestFalse(TEXT("Crafted radio equipment is a concrete spawnable class"),
        AFactoryFPVRadioEquipment::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
    TestFalse(TEXT("Item has a player-readable name"),Descriptor->mDisplayName.IsEmpty());
    TestTrue(TEXT("Item has an equipment crafting category"),Descriptor->mCategory!=nullptr);
    TestTrue(TEXT("Item has a visible inventory icon"),Descriptor->mSmallIcon!=nullptr);
    TestEqual(TEXT("Recipe produces exactly one item kind"),Recipe->GetProducts().Num(),1);
    if (Recipe->GetProducts().Num()==1) {
        TestTrue(TEXT("Recipe's product is the actual equipment descriptor"),Recipe->GetProducts()[0].ItemClass==UFactoryFPVRadioDescriptor::StaticClass());
        TestEqual(TEXT("Recipe makes one kit"),Recipe->GetProducts()[0].Amount,1);
    }
    TestTrue(TEXT("Radio is crafted from ingredients rather than granted"),Recipe->GetIngredients().Num()>0);
    for (const auto& Ingredient:Recipe->GetIngredients()) {
        TestTrue(TEXT("Ingredient class resolves from the matching game SDK"),Ingredient.ItemClass!=nullptr);
        TestTrue(TEXT("Ingredient cost is positive"),Ingredient.Amount>0);
    }
    TestEqual(TEXT("Recipe has one production site"),Recipe->mProducedIn.Num(),1);
    if (!Recipe->mProducedIn.IsEmpty()) TestTrue(TEXT("Recipe uses the equipment workshop producer"),
        Recipe->mProducedIn[0].ToSoftObjectPath().ToString().Contains(TEXT("BP_WorkshopComponent")));
    TestTrue(TEXT("Manual crafting takes nonzero time"),Recipe->GetManualManufacturingDuration()>0);
    TestEqual(TEXT("Progression unlock is a HUB milestone"),Schematic->mType,ESchematicType::EST_Milestone);
    TestTrue(TEXT("Milestone is early progression, not tutorial injection"),Schematic->mTechTier>=1 && Schematic->mTechTier<=2);
    TestTrue(TEXT("Milestone requires payment"),!Schematic->mCost.IsEmpty());
    for (const auto& Cost:Schematic->mCost) TestTrue(TEXT("Milestone costs resolve and are positive"),Cost.ItemClass!=nullptr && Cost.Amount>0);
    TestTrue(TEXT("Milestone has a menu category and icon"),Schematic->mSchematicCategory!=nullptr && Schematic->mSmallSchematicIcon!=nullptr);
    TestEqual(TEXT("Milestone has exactly one unlock"),Schematic->mUnlocks.Num(),1);
    if (!Schematic->mUnlocks.IsEmpty()) {
        const auto* Unlock=Cast<UFGUnlockRecipe>(Schematic->mUnlocks[0]);
        TestNotNull(TEXT("Milestone uses an actual recipe unlock"),Unlock);
        if (Unlock) TestTrue(TEXT("Unlock contains the craftable kit recipe"),Unlock->GetRecipesToUnlock().Contains(UFactoryFPVRadioRecipe::StaticClass()));
        auto* Interface=LoadObject<UClass>(nullptr,TEXT("/Game/FactoryGame/Unlocks/BPI_UnlockableInterface.BPI_UnlockableInterface_C"));
        TestNotNull(TEXT("Game reward-display interface resolves"),Interface);
        if (Unlock && Interface) TestTrue(TEXT("HUB unlock implements real reward-display interface"),Unlock->GetClass()->ImplementsInterface(Interface));
    }
    TestTrue(TEXT("Native world module is a root module"),Module->bRootModule);
    TestTrue(TEXT("Root module registers the milestone"),Module->mSchematics.Contains(UFactoryFPVRadioSchematic::StaticClass()));
    const auto Discovered=FPluginModuleLoader::FindRootModulesOfType(UGameWorldModule::StaticClass());
    TestTrue(TEXT("SML discovers the native root under FactoryFPV ownership"),Discovered.ContainsByPredicate([](const FDiscoveredModule& Entry) {
        return Entry.ModuleClass==UFactoryFPVGameWorldModule::StaticClass() && Entry.OwnerPluginName==TEXT("FactoryFPV");
    }));
    return true;
}
#endif
