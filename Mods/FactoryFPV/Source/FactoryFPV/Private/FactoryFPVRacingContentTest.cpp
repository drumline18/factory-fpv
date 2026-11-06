#include "FactoryFPVRacingContent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVEquipment.h"
#include "Unlocks/FGUnlockRecipe.h"
#include "Engine/Texture2D.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRacingContentTest,"FactoryFPV.Racing.BuildableRegistration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFactoryFPVRacingContentTest::RunTest(const FString&) {
    const auto* Descriptor=GetDefault<UFactoryFPVRaceGateDescriptor>();
    const auto* Recipe=GetDefault<UFactoryFPVRaceGateRecipe>();
    const auto* FullDescriptor=GetDefault<UFactoryFPVRaceGateFullDescriptor>();
    const auto* FullRecipe=GetDefault<UFactoryFPVRaceGateFullRecipe>();
    const auto* VerticalDescriptor=GetDefault<UFactoryFPVRaceGateVerticalDescriptor>();
    const auto* VerticalRecipe=GetDefault<UFactoryFPVRaceGateVerticalRecipe>();
    const auto* FloorDescriptor=GetDefault<UFactoryFPVRaceGateFloorDescriptor>();
    const auto* FloorRecipe=GetDefault<UFactoryFPVRaceGateFloorRecipe>();
    const auto* PoleDescriptor=GetDefault<UFactoryFPVRacePoleDescriptor>();
    const auto* PoleRecipe=GetDefault<UFactoryFPVRacePoleRecipe>();
    const auto* Schematic=GetDefault<UFactoryFPVRacingSchematic>();
    TestTrue(TEXT("Build gun descriptor targets the actual native gate"),Descriptor->mBuildableClass==AFactoryFPVRaceGate::StaticClass());
    TestEqual(TEXT("Existing saved gate descriptor keeps its native class path"),Descriptor->GetClass()->GetPathName(),FString(TEXT("/Script/FactoryFPV.FactoryFPVRaceGateDescriptor")));
    TestEqual(TEXT("Existing saved gate recipe keeps its native class path"),Recipe->GetClass()->GetPathName(),FString(TEXT("/Script/FactoryFPV.FactoryFPVRaceGateRecipe")));
    TestTrue(TEXT("Full-size descriptor targets the separate native full-size buildable"),FullDescriptor->mBuildableClass==AFactoryFPVRaceGateFull::StaticClass());
    TestTrue(TEXT("Both sizes appear in the same Build Gun menu category"),FullDescriptor->mCategory==Descriptor->mCategory && FullDescriptor->mSubCategories==Descriptor->mSubCategories);
    TestEqual(TEXT("Compact menu label identifies its size"),Descriptor->mDisplayName.ToString(),FString(TEXT("FPV Compact Race Gate")));
    TestEqual(TEXT("Full-size menu label identifies its size"),FullDescriptor->mDisplayName.ToString(),FString(TEXT("FPV Full-Size Race Gate")));
    TestTrue(TEXT("Full-size gate has usable capacity and reuses the owned gate icon"),FullDescriptor->mCachedStackSize==1 && FullDescriptor->mSmallIcon && FullDescriptor->mSmallIcon==Descriptor->mSmallIcon);
    TestTrue(TEXT("Vertical gate uses its own native buildable in the racing Build Gun category"),
        VerticalDescriptor->mBuildableClass==AFactoryFPVRaceGateVertical::StaticClass() && VerticalDescriptor->mCategory==Descriptor->mCategory &&
        VerticalDescriptor->mSubCategories==Descriptor->mSubCategories && VerticalDescriptor->mCachedStackSize==1 && VerticalDescriptor->mSmallIcon);
    TestEqual(TEXT("Vertical gate has a distinct Build Gun label"),VerticalDescriptor->mDisplayName.ToString(),FString(TEXT("FPV Vertical Race Gate")));
    TestTrue(TEXT("Floor gate uses a separate buildable in the racing Build Gun category"),
        FloorDescriptor->mBuildableClass==AFactoryFPVRaceGateFloor::StaticClass() && FloorDescriptor->mCategory==Descriptor->mCategory &&
        FloorDescriptor->mSubCategories==Descriptor->mSubCategories && FloorDescriptor->mCachedStackSize==1 && FloorDescriptor->mSmallIcon);
    TestEqual(TEXT("Floor gate has a distinct Build Gun label"),FloorDescriptor->mDisplayName.ToString(),FString(TEXT("FPV Floor Race Gate")));
    TestTrue(TEXT("Pole descriptor uses the separate native pole buildable"),PoleDescriptor->mBuildableClass==AFactoryFPVRacePole::StaticClass());
    TestTrue(TEXT("New pole keeps the gate's real build-menu category and subcategory"),PoleDescriptor->mCategory==Descriptor->mCategory && PoleDescriptor->mSubCategories==Descriptor->mSubCategories);
    TestEqual(TEXT("Native pole product has usable unit capacity"),PoleDescriptor->mCachedStackSize,1);
    TestNotNull(TEXT("Pole always has an owned icon or owned gate fallback"),PoleDescriptor->mSmallIcon.Get());
    TestTrue(TEXT("Compact gate describes the 3 m opening"),Descriptor->mDescription.ToString().Contains(TEXT("3 m by 3 m")));
    TestTrue(TEXT("Full-size gate describes the foundation width and 7.6 m opening"),FullDescriptor->mDescription.ToString().Contains(TEXT("8 m foundation")) && FullDescriptor->mDescription.ToString().Contains(TEXT("7.6 m")));
    TestTrue(TEXT("Pole explains its directional side configuration"),PoleDescriptor->mDescription.ToString().Contains(TEXT("left or right")));
    TestTrue(TEXT("Gate has a build-menu category"),Descriptor->mCategory!=nullptr);
    TestTrue(TEXT("Gate has a build-menu subcategory"),!Descriptor->mSubCategories.IsEmpty() && Descriptor->mSubCategories[0]!=nullptr);
    TestNotNull(TEXT("Gate has its original custom icon"),Descriptor->mSmallIcon.Get());
    TestEqual(TEXT("Native build product has explicit unit capacity"),Descriptor->mCachedStackSize,1);
    TestEqual(TEXT("One gate recipe product"),Recipe->GetProducts().Num(),1);
    if (Recipe->GetProducts().Num()==1) {
        TestTrue(TEXT("Recipe product uses a build descriptor"),Recipe->GetProducts()[0].ItemClass==UFactoryFPVRaceGateDescriptor::StaticClass());
        TestEqual(TEXT("Recipe builds one gate"),Recipe->GetProducts()[0].Amount,1);
    }
    TestTrue(TEXT("Gate has material costs"),Recipe->GetIngredients().Num()>=3);
    for (const auto& Item:Recipe->GetIngredients()) TestTrue(TEXT("Gate ingredient resolves with positive amount"),Item.ItemClass && Item.Amount>0);
    TestEqual(TEXT("Build gun is sole producer"),Recipe->mProducedIn.Num(),1);
    if (!Recipe->mProducedIn.IsEmpty()) TestTrue(TEXT("Build gun class resolves"),Recipe->mProducedIn[0].LoadSynchronous()!=nullptr);
    TestEqual(TEXT("Full-size recipe produces one item kind"),FullRecipe->GetProducts().Num(),1);
    if (FullRecipe->GetProducts().Num()==1) {
        TestTrue(TEXT("Full-size recipe produces one full-size gate descriptor"),FullRecipe->GetProducts()[0].ItemClass==UFactoryFPVRaceGateFullDescriptor::StaticClass() && FullRecipe->GetProducts()[0].Amount==1);
    }
    TestTrue(TEXT("Full-size gate uses the same sole native Build Gun producer"),FullRecipe->mProducedIn.Num()==1 && FullRecipe->mProducedIn==Recipe->mProducedIn);
    const auto VerticalProducts=VerticalRecipe->GetProducts();
    TestTrue(TEXT("Vertical recipe produces one vertical gate through the native Build Gun"),
        VerticalProducts.Num()==1 && VerticalProducts[0].ItemClass==UFactoryFPVRaceGateVerticalDescriptor::StaticClass() &&
        VerticalProducts[0].Amount==1 && VerticalRecipe->mProducedIn.Num()==1 && VerticalRecipe->mProducedIn==Recipe->mProducedIn);
    const auto FloorProducts=FloorRecipe->GetProducts();
    TestTrue(TEXT("Floor recipe produces one floor gate through the native Build Gun"),
        FloorProducts.Num()==1 && FloorProducts[0].ItemClass==UFactoryFPVRaceGateFloorDescriptor::StaticClass() &&
        FloorProducts[0].Amount==1 && FloorRecipe->mProducedIn==Recipe->mProducedIn);
    const auto SmallCost=Recipe->GetIngredients();
    const auto FullCost=FullRecipe->GetIngredients();
    TestEqual(TEXT("Compact gate uses four original ingredients"),SmallCost.Num(),4);
    TestEqual(TEXT("Full-size gate uses four ingredients"),FullCost.Num(),4);
    if (SmallCost.Num()==4 && FullCost.Num()==4) {
        const int32 SmallAmounts[]={8,6,4,10};
        const int32 FullAmounts[]={12,10,6,15};
        const TCHAR* IngredientNames[]={TEXT("Desc_IronPlate_C"),TEXT("Desc_IronRod_C"),TEXT("Desc_Cable_C"),TEXT("Desc_Wire_C")};
        for (int32 Index=0;Index<4;++Index) {
            const FString Ingredient=IngredientNames[Index];
            TestTrue(Ingredient+TEXT(" resolves to the same item for both gate sizes"),SmallCost[Index].ItemClass &&
                SmallCost[Index].ItemClass.Get()->GetName()==Ingredient && FullCost[Index].ItemClass==SmallCost[Index].ItemClass);
            TestEqual(Ingredient+TEXT(" retains its compact gate cost"),SmallCost[Index].Amount,SmallAmounts[Index]);
            TestEqual(Ingredient+TEXT(" has its full-size gate cost"),FullCost[Index].Amount,FullAmounts[Index]);
        }
    }
    TestEqual(TEXT("Pole recipe produces one item kind"),PoleRecipe->GetProducts().Num(),1);
    if (PoleRecipe->GetProducts().Num()==1) TestTrue(TEXT("Pole recipe creates one separate pole descriptor"),PoleRecipe->GetProducts()[0].ItemClass==UFactoryFPVRacePoleDescriptor::StaticClass() && PoleRecipe->GetProducts()[0].Amount==1);
    TestTrue(TEXT("Pole requires actual materials"),PoleRecipe->GetIngredients().Num()>=3);
    for (const auto& Item:PoleRecipe->GetIngredients()) TestTrue(TEXT("Pole ingredient resolves with positive amount"),Item.ItemClass && Item.Amount>0);
    TestTrue(TEXT("Pole uses the same sole native Build Gun producer"),PoleRecipe->mProducedIn.Num()==1 && PoleRecipe->mProducedIn==Recipe->mProducedIn);
    TestEqual(TEXT("Racing unlock remains early progression"),Schematic->mTechTier,1);
    TestTrue(TEXT("Racing has a separate unlock for existing completed radio milestones"),Schematic->GetClass()!=UFactoryFPVRadioSchematic::StaticClass());
    TestTrue(TEXT("Native root registers racing milestone"),GetDefault<UFactoryFPVGameWorldModule>()->mSchematics.Contains(UFactoryFPVRacingSchematic::StaticClass()));
    TestTrue(TEXT("Native root registers the world course name registry"),
        GetDefault<UFactoryFPVGameWorldModule>()->ModSubsystems.Contains(AFactoryFPVRaceCourseRegistry::StaticClass()));
    TestEqual(TEXT("Racing uses a standard recipe unlock"),Schematic->mUnlocks.Num(),1);
    if (Schematic->mUnlocks.Num()==1) {
        const auto* Unlock=Cast<UFGUnlockRecipe>(Schematic->mUnlocks[0]);
        TestNotNull(TEXT("Unlock has native recipe contract"),Unlock);
        const auto* Interface=LoadObject<UClass>(nullptr,TEXT("/Game/FactoryGame/Unlocks/BPI_UnlockableInterface.BPI_UnlockableInterface_C"));
        TestTrue(TEXT("Racing reward uses the standard game interface"),Interface && Unlock && Unlock->GetClass()->ImplementsInterface(Interface));
        if (Unlock) {
            TestEqual(TEXT("Racing reward retains its original unlock subobject name"),Unlock->GetFName(),FName(TEXT("UnlockRaceGateRecipe")));
            const auto Recipes=Unlock->GetRecipesToUnlock();
            TestTrue(TEXT("Racing milestone unlocks both upright sizes, both vertical gates and the pole"),Recipes.Num()==5 &&
                Recipes.Contains(UFactoryFPVRaceGateRecipe::StaticClass()) && Recipes.Contains(UFactoryFPVRaceGateFullRecipe::StaticClass()) &&
                Recipes.Contains(UFactoryFPVRaceGateVerticalRecipe::StaticClass()) && Recipes.Contains(UFactoryFPVRaceGateFloorRecipe::StaticClass()) &&
                Recipes.Contains(UFactoryFPVRacePoleRecipe::StaticClass()));
        }
    }
    TestFalse(TEXT("Recipe compatibility helper refuses absent world without mutation"),FactoryFPVRacingContent::EnsureUnlockedRacingRecipes(nullptr));
    // Generated SDK build-gun methods are stubs: actual hologram placement,
    // material consumption, purchased-world recipe migration and grid wiring
    // require the Shipping game; native purchase state is never fabricated here.
    return true;
}
#endif
