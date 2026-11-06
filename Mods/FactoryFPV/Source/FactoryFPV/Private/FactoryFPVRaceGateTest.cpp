#include "FactoryFPVRaceGate.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGateHologram.h"
#include "FGPowerCircuit.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"
#include <algorithm>
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRaceGateTest,
    "FactoryFPV.Racing.GateGeometryPowerAndPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRaceGateTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,
        ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Race gate test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    // A bare SDK world cannot BeginPlay an AFGBuildable: the native base needs
    // real FGWorldSettings, FGGameState and their buildable/proximity systems.
    // Test our instance initialization explicitly and do not spoof those systems.
    const auto* Defaults=GetDefault<AFactoryFPVRaceGate>();
    TestFalse(TEXT("Buildable race gate is concrete"),AFactoryFPVRaceGate::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
    TestFalse(TEXT("Race gates are persistent rather than transient actors"),AFactoryFPVRaceGate::StaticClass()->HasAnyClassFlags(CLASS_Transient));
    TestFalse(TEXT("Gate identity is not shared through the class default"),Defaults->GetGateId().IsValid());
    const_cast<AFactoryFPVRaceGate*>(Defaults)->EnsureIdentityAndOrder();
    TestFalse(TEXT("Instance initialization cannot assign a class-default identity"),Defaults->GetGateId().IsValid());

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto MakeGate=[&](FVector Position=FVector(0,0,0)) {
        return World->SpawnActor<AFactoryFPVRaceGate>(Position,FRotator::ZeroRotator,Spawn);
    };
    auto* Gate=MakeGate();
    if (!TestNotNull(TEXT("Native race gate"),Gate)) return false;
    TestFalse(TEXT("The original saved gate class remains compact"),Gate->IsFullSizeGate());
    TestTrue(TEXT("Gate participates in normal buildable saving"),Gate->ShouldSave_Implementation());
    TestTrue(TEXT("Gate transform is included in normal buildable saving"),Gate->NeedTransform_Implementation());
    TestEqual(TEXT("Gate uses the foundation-aware hologram with course-builder assignment"),
        Gate->mHologramClass.Get(),AFactoryFPVRaceGateHologram::StaticClass());
    TestTrue(TEXT("Gate factory tick is enabled for real power demand"),Gate->ShouldRegisterToFactoryTickGroup());
    TestNotNull(TEXT("Gate owns real power consumption information"),Gate->GetPowerInfo());
    TestNotNull(TEXT("Gate owns an ordinary wire connection"),Gate->GetPowerConnection());
    TestTrue(TEXT("Compact gate retains its original cable terminal location"),
        Gate->GetPowerConnection()->GetRelativeLocation().Equals(FVector(-25,-160,210),.001));
    TestFalse(TEXT("Power connection accepts visible cables"),Gate->GetPowerConnection()->IsHidden());
    TestEqual(TEXT("Power connector belongs to Satisfactory power circuits"),
        Gate->GetPowerConnection()->GetCircuitType().Get(),UFGPowerCircuit::StaticClass());
    TestFalse(TEXT("Unconnected gate does not count as powered"),Gate->IsGatePowered());
    TestTrue(TEXT("Gate supports native configure interaction without power"),Gate->IsUseable_Implementation());
    TestEqual(TEXT("Physical frame includes all four aperture sides and a terminal"),Gate->FrameParts.Num(),5);
    TestEqual(TEXT("Emissive strips appear on both faces"),Gate->LightStrips.Num(),8);
    TestEqual(TEXT("Power state controls both real light sources"),Gate->Lights.Num(),2);
    auto* Shaft=Cast<UStaticMeshComponent>(Gate->GetDefaultSubobjectByName(TEXT("MainMesh_DirectionShaft")));
    auto* HeadLeft=Cast<UStaticMeshComponent>(Gate->GetDefaultSubobjectByName(TEXT("MainMesh_DirectionHeadLeft")));
    auto* HeadRight=Cast<UStaticMeshComponent>(Gate->GetDefaultSubobjectByName(TEXT("MainMesh_DirectionHeadRight")));
    if (!TestNotNull(TEXT("Permanent direction arrow shaft"),Shaft) ||
        !TestNotNull(TEXT("Permanent direction arrow left head"),HeadLeft) ||
        !TestNotNull(TEXT("Permanent direction arrow right head"),HeadRight)) return false;
    const FVector LeftTip=HeadLeft->GetRelativeTransform().TransformPosition(FVector(50,0,0));
    const FVector RightTip=HeadRight->GetRelativeTransform().TransformPosition(FVector(50,0,0));
    TestTrue(TEXT("Visible arrowhead converges in the actual +X race direction"),
        LeftTip.Equals(RightTip,.1) && LeftTip.X>Shaft->GetRelativeLocation().X &&
        FMath::Abs(LeftTip.Y-Shaft->GetRelativeLocation().Y)<.1);
    for (auto* Marker:{Shaft,HeadLeft,HeadRight}) {
        TestEqual(TEXT("Direction marker adds no flight collision"),Marker->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
        TestTrue(TEXT("Direction marker stays outside the opening and visible without power"),
            Marker->GetRelativeLocation().Y<-170 && Marker->IsVisible() && !Marker->bHiddenInGame);
    }
    for (UStaticMeshComponent* Frame:Gate->FrameParts) {
        TestTrue(TEXT("Build/dismantle mesh discovery includes every frame member"),Frame->GetName().Contains(TEXT("MainMesh")));
        TestTrue(TEXT("Frame collision uses an actual mesh collision shape"),
            Frame->GetStaticMesh() && Frame->GetStaticMesh()->GetBodySetup());
        TestEqual(TEXT("Frame has blocking query and physics collision"),Frame->GetCollisionEnabled(),ECollisionEnabled::QueryAndPhysics);
    }
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVRaceGateTest),false);
    auto Sweep=[&](FVector From,FVector To) {
        FHitResult Hit;
        return World->SweepSingleByChannel(Hit,From,To,FQuat::Identity,ECC_WorldDynamic,
            FCollisionShape::MakeSphere(12),Query);
    };
    TestFalse(TEXT("A drone-sized sphere flies through the compact aperture"),Sweep(FVector(-200,0,170),FVector(200,0,170)));
    TestTrue(TEXT("The compact side post blocks a drone-sized sphere"),Sweep(FVector(-200,160,170),FVector(200,160,170)));
    TestTrue(TEXT("The compact top bar blocks a drone-sized sphere"),Sweep(FVector(-200,0,330),FVector(200,0,330)));
    TestTrue(TEXT("The physical bottom bar blocks a drone-sized sphere"),Sweep(FVector(-200,0,10),FVector(200,0,10)));
    TestFalse(TEXT("Safe compact aperture edge has no invisible collision"),Sweep(FVector(-200,130,170),FVector(200,130,170)));
    TestTrue(TEXT("Compact near-frame passage accounts for physical drone radius"),Sweep(FVector(-200,145,170),FVector(200,145,170)));
    TestEqual(TEXT("Compact aperture retains a three-metre clear opening"),Gate->GetApertureHalfExtents(),FVector2D(150,150));
    TestTrue(TEXT("Compact scoring plane retains its original center"),Gate->GetApertureTransform().GetLocation().Equals(FVector(0,0,170),.001));
    auto OuterWidth=[](const AFactoryFPVRaceGate* Frame) {
        const auto* Right=Frame->FrameParts[1].Get();
        const auto* Left=Frame->FrameParts[0].Get();
        return Right->GetRelativeLocation().Y+Right->GetRelativeScale3D().Y*50.-
            (Left->GetRelativeLocation().Y-Left->GetRelativeScale3D().Y*50.);
    };
    TestEqual(TEXT("Original physical frame retains its 3.4 metre outer width"),OuterWidth(Gate),340.);
    if (!TestNotNull(TEXT("Opposite text faces have a solid separating board"),Gate->LabelBoard.Get())) return false;
    TestEqual(TEXT("Label board cannot obstruct flight"),Gate->LabelBoard->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    if (!TestNotNull(TEXT("Separating board has a real material"),Gate->LabelBoard->GetMaterial(0))) return false;
    TestEqual(TEXT("Opaque board hides the mirrored back of the other label"),Gate->LabelBoard->GetMaterial(0)->GetBlendMode(),BLEND_Opaque);
    TestEqual(TEXT("Gate retains two opposing readable label faces"),Gate->Labels.Num(),2);
    const double BoardHalfDepth=Gate->LabelBoard->GetRelativeScale3D().X*50.;
    for (UTextRenderComponent* Label:Gate->Labels) {
        const FVector Offset=Label->GetRelativeLocation()-Gate->LabelBoard->GetRelativeLocation();
        TestTrue(TEXT("Each text face is outside the board and points away from the opposite face"),
            FMath::Abs(Offset.X)>BoardHalfDepth && FVector::DotProduct(Offset,Label->GetRelativeRotation().Vector())>0);
    }
    const FTransform Placement(FRotator(0,90,0),FVector(1000,2000,300),FVector(2,2,2));
    Gate->SetActorTransform(Placement);
    TestTrue(TEXT("Aperture center follows placed rotation and scale once"),
        Gate->GetApertureTransform().GetLocation().Equals(Placement.TransformPosition(FVector(0,0,170)),.001));
    TestTrue(TEXT("Aperture forward direction follows gate placement"),
        Gate->GetApertureTransform().GetRotation().GetAxisX().Equals(Placement.GetRotation().GetAxisX(),.001));
    TestTrue(TEXT("Aperture transform carries scale without inflating local half extents"),
        Gate->GetApertureTransform().GetScale3D().Equals(FVector(2)) && Gate->GetApertureHalfExtents()==FVector2D(150,150));
    TestTrue(TEXT("Compact cable terminal follows the original transformed attachment"),
        Gate->GetPowerConnection()->GetComponentLocation().Equals(Placement.TransformPosition(FVector(-25,-160,210)),.001));
    Gate->SetActorTransform(FTransform::Identity);

    auto* Full=World->SpawnActor<AFactoryFPVRaceGateFull>(FVector(9000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Separate full-size race gate"),Full)) return false;
    TestTrue(TEXT("Full-size gate has its own persistent concrete class"),Full->IsFullSizeGate() && !Full->IsPoleCheckpoint() &&
        !AFactoryFPVRaceGateFull::StaticClass()->HasAnyClassFlags(CLASS_Abstract|CLASS_Transient));
    TestEqual(TEXT("Full-size outer frame spans one eight-metre foundation"),OuterWidth(Full),800.);
    TestEqual(TEXT("Full-size gate retains the 7.6 by 3.6 metre opening"),Full->GetApertureHalfExtents(),FVector2D(380,180));
    TestTrue(TEXT("Full-size scoring plane is centered in its opening"),Full->GetApertureTransform().GetLocation().Equals(FVector(9000,0,200),.001));
    TestTrue(TEXT("Full-size cable terminal follows the wider frame"),
        Full->GetPowerConnection()->GetRelativeLocation().Equals(FVector(-28,-390,250),.001));
    TestFalse(TEXT("A sphere passes through the full-size opening beyond the compact post"),Sweep(FVector(8800,160,200),FVector(9200,160,200)));
    TestFalse(TEXT("A sphere clears the full-size opening just inside its edge"),Sweep(FVector(8800,365,200),FVector(9200,365,200)));
    TestTrue(TEXT("Full-size near-frame pass meets actual post collision"),Sweep(FVector(8800,370,200),FVector(9200,370,200)));
    TestTrue(TEXT("Full-size top bar remains collidable"),Sweep(FVector(8800,0,390),FVector(9200,0,390)));
    Full->Destroy();

    // The SDK power simulation/setters are generated stubs. Inject only the
    // component's reported result to test our consumer predicate and presentation;
    // this is not evidence that a wire or generator works in the shipping game.
    auto* Circuit=FindFProperty<FIntProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mCircuitID"));
    auto* HasPower=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mHasPower"));
    auto* Fuse=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mIsFuseTriggered"));
    if (!TestNotNull(TEXT("Reported circuit state property"),Circuit) ||
        !TestNotNull(TEXT("Reported has-power property"),HasPower) ||
        !TestNotNull(TEXT("Reported fuse state property"),Fuse)) return false;
    auto SetGridResult=[&](int32 CircuitId,bool bHasPower,bool bFuse) {
        Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),CircuitId);
        HasPower->SetPropertyValue_InContainer(Gate->GetPowerInfo(),bHasPower);
        Fuse->SetPropertyValue_InContainer(Gate->GetPowerInfo(),bFuse);
        Gate->UpdatePresentation();
    };
    Gate->CreateLightMaterials();
    Gate->UpdatePresentation();
    if (!TestEqual(TEXT("Every LED strip has its own controllable material instance"),Gate->LightMaterials.Num(),8)) return false;
    for (UMaterialInstanceDynamic* Material:Gate->LightMaterials) {
        if (!TestNotNull(TEXT("Race light material is included"),Material)) return false;
        TestEqual(TEXT("Unpowered material emits no light"),Material->K2_GetScalarParameterValue(TEXT("LightStrength")),0.f);
    }
    SetGridResult(INDEX_NONE,true,false);
    TestFalse(TEXT("A stale power flag without a circuit cannot enable a gate"),Gate->IsGatePowered());
    SetGridResult(17,false,false);
    TestFalse(TEXT("A connected but unpowered gate cannot score"),Gate->IsGatePowered());
    SetGridResult(17,true,false);
    TestTrue(TEXT("Connected satisfied consumer enables gate scoring"),Gate->IsGatePowered());
    for (UPointLightComponent* Light:Gate->Lights) TestTrue(TEXT("Powered gate enables actual lights"),Light->IsVisible());
    for (UMaterialInstanceDynamic* Material:Gate->LightMaterials) TestEqual(TEXT("Powered gate enables LED emission"),Material->K2_GetScalarParameterValue(TEXT("LightStrength")),12.f);

    TArray<FLinearColor> NormalStripColors;
    TArray<FLinearColor> NormalLightColors;
    TArray<FString> NormalSignTexts;
    TArray<FColor> NormalSignColors;
    for (UMaterialInstanceDynamic* Material:Gate->LightMaterials)
        NormalStripColors.Add(Material->K2_GetVectorParameterValue(TEXT("LightColor")));
    for (UPointLightComponent* Light:Gate->Lights) NormalLightColors.Add(Light->GetLightColor());
    for (UTextRenderComponent* Label:Gate->Labels) {
        NormalSignTexts.Add(Label->Text.ToString());
        NormalSignColors.Add(Label->TextRenderColor);
    }
    auto IsHotPink=[](const FLinearColor& Color) { return Color.R>Color.G*2 && Color.B>Color.G*2; };
    auto CheckPoweredPresentation=[&](bool Highlighted) {
        int32 EntryStripCount=0;
        int32 ReverseStripCount=0;
        for (int32 Index=0;Index<Gate->LightMaterials.Num();++Index) {
            auto* Material=Gate->LightMaterials[Index].Get();
            const double LocalX=Gate->LightStrips[Index]->GetRelativeLocation().X;
            const bool IsEntry=LocalX<0;
            EntryStripCount+=IsEntry;
            ReverseStripCount+=LocalX>0;
            const FLinearColor Color=Material->K2_GetVectorParameterValue(TEXT("LightColor"));
            const float Strength=Material->K2_GetScalarParameterValue(TEXT("LightStrength"));
            if (Highlighted && IsEntry) {
                TestTrue(TEXT("Physical approach LED emits hot pink at the normal strength"),IsHotPink(Color) && Strength==12.f);
            } else {
                TestTrue(TEXT("Ordinary and reverse LEDs retain their base color and strength"),Color.Equals(NormalStripColors[Index],.001f) && Strength==12.f);
            }
        }
        TestTrue(TEXT("Presentation covers all four actual LEDs on each face"),EntryStripCount==4 && ReverseStripCount==4);
        for (int32 Index=0;Index<Gate->Lights.Num();++Index) {
            const auto* Light=Gate->Lights[Index].Get();
            TestTrue(TEXT("Both point lights retain ordinary color and intensity without pink spill"),
                Light->IsVisible() && Light->GetLightColor().Equals(NormalLightColors[Index],.001f) && !IsHotPink(Light->GetLightColor()) && Light->Intensity==1200.f);
        }
        for (int32 Index=0;Index<Gate->Labels.Num();++Index) {
            const auto* Label=Gate->Labels[Index].Get();
            TestTrue(TEXT("Both signs retain their normal text and color when the target changes"),
                Label->Text.ToString()==NormalSignTexts[Index] && Label->TextRenderColor==NormalSignColors[Index]);
        }
    };
    Gate->SetNextCheckpointHighlighted(true);
    TestTrue(TEXT("Next checkpoint request is visible in gate state"),Gate->IsNextCheckpointHighlighted());
    CheckPoweredPresentation(true);
    SetGridResult(17,false,false);
    for (UMaterialInstanceDynamic* Material:Gate->LightMaterials)
        TestTrue(TEXT("Unpowered target removes pink and all LED emission"),
            !IsHotPink(Material->K2_GetVectorParameterValue(TEXT("LightColor"))) && Material->K2_GetScalarParameterValue(TEXT("LightStrength"))==0.f);
    SetGridResult(17,true,false);Gate->SetNextCheckpointHighlighted(false);
    CheckPoweredPresentation(false);
    // Exercise our re-entry state contract without pretending a bare SDK world
    // can run AFGBuildable::BeginPlay/EndPlay (those require real game systems).
    // Earlier EndPlay hid lights, while the cached powered/configuration values
    // remain identical to the component's next reported result.
    Gate->SetNextCheckpointHighlighted(true);
    TestTrue(TEXT("Re-entry fixture starts with active next-checkpoint ownership"),Gate->IsNextCheckpointHighlighted());
    Gate->bEndingPlay=true;
    for (UPointLightComponent* Light:Gate->Lights) Light->SetVisibility(false);
    TestFalse(TEXT("A stopped gate cannot count powered before world re-entry"),Gate->IsGatePowered());
    TestFalse(TEXT("A stopped gate cannot configure before world re-entry"),Gate->IsUseable_Implementation());
    Gate->PrepareForWorldEntry();
    TestFalse(TEXT("World re-entry clears transient checkpoint highlight ownership"),Gate->IsNextCheckpointHighlighted());
    TestTrue(TEXT("World re-entry preserves the reported circuit and available power"),
        Gate->GetPowerInfo()->IsConnected() && Gate->GetPowerInfo()->HasPower() && Gate->IsGatePowered());
    TestTrue(TEXT("World re-entry restores native configuration eligibility"),Gate->IsUseable_Implementation());
    Gate->UpdatePresentation();
    CheckPoweredPresentation(false);
    SetGridResult(17,true,true);
    TestFalse(TEXT("Tripped fuse disables gate scoring even with stale has-power state"),Gate->IsGatePowered());
    for (UPointLightComponent* Light:Gate->Lights) TestFalse(TEXT("Tripped fuse extinguishes the real lights"),Light->IsVisible());
    for (UMaterialInstanceDynamic* Material:Gate->LightMaterials) TestEqual(TEXT("Tripped fuse extinguishes LED emission"),Material->K2_GetScalarParameterValue(TEXT("LightStrength")),0.f);
    SetGridResult(INDEX_NONE,false,false);

    Gate->EnsureIdentityAndOrder();
    TestTrue(TEXT("First instance receives persistent identity"),Gate->GetGateId().IsValid());
    TestEqual(TEXT("First course gate is start/finish"),Gate->GetGateOrder(),1);
    const FGuid FirstIdentity=Gate->GetGateId();
    auto* Second=MakeGate(FVector(1000,0,0));
    Second->EnsureIdentityAndOrder();
    TestEqual(TEXT("Second newly placed gate takes the first free order"),Second->GetGateOrder(),2);
    TestTrue(TEXT("New gates have distinct identities"),Second->GetGateId().IsValid() && Second->GetGateId()!=FirstIdentity);
    Gate->EnsureIdentityAndOrder();
    TestTrue(TEXT("Repeated course snapshots preserve established gate identity"),Gate->GetGateId()==FirstIdentity);
    FString Error;
    TestTrue(TEXT("Users can configure another course and order"),Second->Configure(2,7,Error));
    TestEqual(TEXT("Course building appends after the highest existing number"),AFactoryFPVRaceGate::NextCourseBuildOrder(World,2),8);
    TestEqual(TEXT("An empty course begins at checkpoint one"),AFactoryFPVRaceGate::NextCourseBuildOrder(World,99),1);
    Second->EnsureIdentityAndOrder();
    TestTrue(TEXT("Configured or loaded numbering is never silently auto-renumbered"),Second->GetCourseId()==2 && Second->GetGateOrder()==7);
    auto* Third=MakeGate(FVector(2000,0,0));
    Third->EnsureIdentityAndOrder();
    TestEqual(TEXT("First free numbering respects course separation"),Third->GetGateOrder(),2);
    auto* DesignerFlag=FindFProperty<FBoolProperty>(AFGBuildable::StaticClass(),TEXT("mReplicatedBuiltInsideBlueprintDesigner"));
    if (!TestNotNull(TEXT("Native blueprint-designer state is reflected"),DesignerFlag)) return false;
    auto* Preview=MakeGate(FVector(5000,0,0));
    DesignerFlag->SetPropertyValue_InContainer(Preview,true);
    TestTrue(TEXT("Preview fixture uses the native designer-state getter"),Preview->IsBuildableInsideBlueprintDesigner());
    Preview->Configure(1,3,Error);Preview->EnsureIdentityAndOrder();
    TestFalse(TEXT("Designer preview cannot acquire a live race identity"),Preview->GetGateId().IsValid());
    Circuit->SetPropertyValue_InContainer(Preview->GetPowerInfo(),17);
    HasPower->SetPropertyValue_InContainer(Preview->GetPowerInfo(),true);
    TestFalse(TEXT("Even a reported powered preview is not a live powered gate"),Preview->IsLiveRaceGate() || Preview->IsGatePowered());
    auto* AfterPreview=MakeGate(FVector(6000,0,0));AfterPreview->EnsureIdentityAndOrder();
    TestEqual(TEXT("Designer gate numbering does not reserve a live-course checkpoint"),AfterPreview->GetGateOrder(),3);
    AfterPreview->Destroy();Preview->Destroy();
    auto* Copied=MakeGate(FVector(3000,0,0));
    Copied->GateGuid=FirstIdentity;
    Copied->bNumberAssigned=true;
    Copied->EnsureIdentityAndOrder();
    TestTrue(TEXT("A copied saved GUID is repaired to avoid ambiguous timing identity"),Copied->GetGateId().IsValid() && Copied->GetGateId()!=FirstIdentity);
    TestEqual(TEXT("Identity repair does not silently fix duplicate course/order configuration"),Copied->GetGateOrder(),1);

    TestTrue(TEXT("A complete first lap establishes a PB"),Gate->RecordLap(45,TEXT("layout-A")));
    TestEqual(TEXT("PB is available for its exact course signature"),Gate->GetBestLapSeconds(TEXT("layout-A")),45.);
    TestTrue(TEXT("A time-only PB is readable without checkpoint splits"),Gate->GetBestLapSplits(TEXT("layout-A"),3).IsEmpty());
    TestFalse(TEXT("Slower laps do not overwrite PB"),Gate->RecordLap(50,TEXT("layout-A")));
    TestFalse(TEXT("Equal laps do not count as PB improvement"),Gate->RecordLap(45,TEXT("layout-A")));
    const double Splits[]{8.,18.,27.};
    const double OtherSplits[]{7.,19.,31.};
    const auto SplitsMatch=[](TConstArrayView<double> Actual,TConstArrayView<double> Expected) {
        return Actual.Num()==Expected.Num() && std::equal(Actual.begin(),Actual.end(),Expected.begin());
    };
    TestTrue(TEXT("A faster lap upgrades a time-only PB with its own checkpoint splits"),Gate->RecordLap(40,TEXT("layout-A"),Splits));
    TestFalse(TEXT("A slower timed lap cannot replace the PB or its splits"),Gate->RecordLap(41,TEXT("layout-A"),OtherSplits));
    TestTrue(TEXT("Slower candidate leaves the best time and exact split sequence"),
        Gate->GetBestLapSeconds(TEXT("layout-A"))==40. && SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-A"),3),Splits));
    TestEqual(TEXT("A changed layout cannot reuse the old PB"),Gate->GetBestLapSeconds(TEXT("layout-B")),0.);
    TestTrue(TEXT("A changed layout or incomplete checkpoint count hides the stored splits"),
        Gate->GetBestLapSplits(TEXT("layout-B"),3).IsEmpty() && Gate->GetBestLapSplits(TEXT("layout-A"),2).IsEmpty());
    TestTrue(TEXT("A new time-only PB clears splits from the previous lap"),
        Gate->RecordLap(38,TEXT("layout-A")) && Gate->GetBestLapSplits(TEXT("layout-A"),3).IsEmpty());
    TestTrue(TEXT("First lap on a changed layout adds its own PB without replacing the earlier layout"),
        Gate->RecordLap(60,TEXT("layout-B"),OtherSplits) && Gate->GetBestLapSeconds(TEXT("layout-A"))==38.);
    TestTrue(TEXT("A later best on the first layout preserves the second layout"),
        Gate->RecordLap(36,TEXT("layout-A"),Splits) && Gate->GetBestLapSeconds(TEXT("layout-B"))==60. &&
        SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits));
    TestTrue(TEXT("Saving unchanged configuration preserves its PB"),Gate->Configure(1,1,Error));
    TestEqual(TEXT("Unchanged configuration PB remains"),Gate->GetBestLapSeconds(TEXT("layout-B")),60.);
    const double Nan=std::numeric_limits<double>::quiet_NaN(),Infinity=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Invalid lap times and empty signatures cannot become records"),
        Gate->RecordLap(Nan,TEXT("layout-B")) || Gate->RecordLap(Infinity,TEXT("layout-B")) ||
        Gate->RecordLap(0,TEXT("layout-B")) || Gate->RecordLap(-1,TEXT("layout-B")) || Gate->RecordLap(1,FString()));
    const double ZeroSplit[]{0.},NonFiniteSplits[]{3.,Nan},BackwardsSplits[]{3.,2.},RepeatedSplits[]{3.,3.},FinishSplit[]{59.};
    TestFalse(TEXT("Malformed explicit checkpoint timing cannot replace any part of the record"),
        Gate->RecordLap(59,TEXT("layout-B"),ZeroSplit) || Gate->RecordLap(59,TEXT("layout-B"),NonFiniteSplits) ||
        Gate->RecordLap(59,TEXT("layout-B"),BackwardsSplits) || Gate->RecordLap(59,TEXT("layout-B"),RepeatedSplits) ||
        Gate->RecordLap(59,TEXT("layout-B"),FinishSplit));
    TestTrue(TEXT("Rejected split data leaves the previous time and splits intact"),
        Gate->GetBestLapSeconds(TEXT("layout-B"))==60. && SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits));
    TestFalse(TEXT("Checkpoint gates cannot own course PBs"),Third->RecordLap(1,TEXT("layout-A")));

    for (const TCHAR* Name:{TEXT("CourseId"),TEXT("GateOrder"),TEXT("bNumberAssigned"),TEXT("PolePassSide"),TEXT("GateGuid"),
        TEXT("BestLapRecords"),TEXT("BestLapSeconds"),TEXT("BestLapLayout"),TEXT("BestLapSplits")}) {
        auto* Property=FindFProperty<FProperty>(AFactoryFPVRaceGate::StaticClass(),Name);
        TestTrue(TEXT("Gate identity, configuration and PB fields participate in save serialization"),Property && Property->HasAnyPropertyFlags(CPF_SaveGame));
    }
    // Round-trip the actual reflected save properties. This is not a Satisfactory
    // save-subsystem/circuit restoration test: the archive resolves existing
    // component references in this world, so preserve the target's own components.
    const auto RoundTripRecords=[&](AFactoryFPVRaceGate* Source,AFactoryFPVRaceGate* Target) {
        TArray<uint8> Bytes;
        FMemoryWriter MemoryWriter(Bytes);
        FObjectAndNameAsStringProxyArchive SaveArchive(MemoryWriter,false);
        SaveArchive.ArIsSaveGame=true;
        Source->Serialize(SaveArchive);
        auto* TargetPowerInfo=Target->PowerInfo.Get();
        auto* TargetPowerConnection=Target->PowerConnection.Get();
        FMemoryReader MemoryReader(Bytes);
        FObjectAndNameAsStringProxyArchive LoadArchive(MemoryReader,false);
        LoadArchive.ArIsSaveGame=true;
        Target->Serialize(LoadArchive);
        Target->PowerInfo=TargetPowerInfo;
        Target->PowerConnection=TargetPowerConnection;
        return !SaveArchive.IsError() && !LoadArchive.IsError();
    };
    auto* Loaded=MakeGate(FVector(4000,0,0));
    TestTrue(TEXT("Tagged SaveGame archive writes and reads the gate history"),RoundTripRecords(Gate,Loaded));
    TestTrue(TEXT("Save-property roundtrip retains course/order/assignment flag"),Loaded->GetCourseId()==1 && Loaded->GetGateOrder()==1 && Loaded->bNumberAssigned);
    TestTrue(TEXT("Save-property roundtrip retains persistent timing identity"),Loaded->GetGateId()==FirstIdentity);
    TestTrue(TEXT("Save-property roundtrip retains both exact-layout times and split sequences"),
        Loaded->GetBestLapSeconds(TEXT("layout-A"))==36. && SplitsMatch(Loaded->GetBestLapSplits(TEXT("layout-A"),3),Splits) &&
        Loaded->GetBestLapSeconds(TEXT("layout-B"))==60. && SplitsMatch(Loaded->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits));
    Loaded->ResetBestLap(TEXT("layout-A"));
    TestTrue(TEXT("Resetting one reloaded layout leaves the other layout's time and splits intact"),
        Loaded->GetBestLapSeconds(TEXT("layout-A"))==0. && Loaded->GetBestLapSplits(TEXT("layout-A"),3).IsEmpty() &&
        Loaded->GetBestLapSeconds(TEXT("layout-B"))==60. && SplitsMatch(Loaded->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits));
    Loaded->PostSerializedFromBlueprint(true);
    TestTrue(TEXT("Blueprint deserialization drops copied record history and retains gate configuration"),
        !Loaded->GetGateId().IsValid() && Loaded->GetCourseId()==1 && Loaded->GetGateOrder()==1 &&
        Loaded->GetBestLapSeconds(TEXT("layout-B"))==0. && Gate->GetBestLapSeconds(TEXT("layout-B"))==60.);
    Loaded->EnsureIdentityAndOrder();
    TestTrue(TEXT("A blueprint copy receives its own race identity"),
        Loaded->GetGateId().IsValid() && Loaded->GetGateId()!=FirstIdentity);
    Loaded->Destroy();

    auto* Legacy=MakeGate(FVector(4100,0,0));
    Legacy->BestLapLayout=TEXT("legacy-layout");
    Legacy->BestLapSeconds=50.;
    Legacy->BestLapSplits={8.,18.,27.};
    auto* LoadedLegacy=MakeGate(FVector(4200,0,0));
    TestTrue(TEXT("Tagged SaveGame archive accepts the legacy tuple"),RoundTripRecords(Legacy,LoadedLegacy));
    TestTrue(TEXT("A legacy time and its splits are available before a history write"),
        LoadedLegacy->GetBestLapSeconds(TEXT("legacy-layout"))==50. &&
        SplitsMatch(LoadedLegacy->GetBestLapSplits(TEXT("legacy-layout"),3),Splits));
    LoadedLegacy->BestLapSplits={8.,Infinity,27.};
    TestTrue(TEXT("Corrupted legacy splits leave the time readable"),
        LoadedLegacy->GetBestLapSeconds(TEXT("legacy-layout"))==50. && LoadedLegacy->GetBestLapSplits(TEXT("legacy-layout"),3).IsEmpty());
    LoadedLegacy->BestLapSplits=Legacy->BestLapSplits;
    TestTrue(TEXT("Recording a different layout carries forward the legacy time and splits"),
        LoadedLegacy->RecordLap(45.,TEXT("new-layout"),OtherSplits) &&
        LoadedLegacy->GetBestLapSeconds(TEXT("legacy-layout"))==50. &&
        SplitsMatch(LoadedLegacy->GetBestLapSplits(TEXT("legacy-layout"),3),Splits));
    LoadedLegacy->ResetBestLap(TEXT("legacy-layout"));
    TestTrue(TEXT("Resetting the migrated legacy layout cannot resurrect it or erase the new layout"),
        LoadedLegacy->GetBestLapSeconds(TEXT("legacy-layout"))==0. && LoadedLegacy->GetBestLapSeconds(TEXT("new-layout"))==45.);
    LoadedLegacy->Destroy();Legacy->Destroy();

    auto* History=MakeGate(FVector(4300,0,0));
    const auto HistoryKey=[](int32 Index) { return FString::Printf(TEXT("history-%d"),Index); };
    constexpr int32 HistoryLimit=AFactoryFPVRaceGate::MaximumBestLapLayouts;
    for (int32 Index=0;Index<HistoryLimit;++Index) {
        TestTrue(TEXT("A distinct layout fits while history is below capacity"),History->RecordLap(40.+Index,HistoryKey(Index)));
    }
    TestTrue(TEXT("An improved layout becomes recent and the next new layout evicts the oldest successful write"),
        History->RecordLap(39.,HistoryKey(0)) && History->RecordLap(60.,HistoryKey(HistoryLimit)) &&
        History->BestLapRecords.Num()==HistoryLimit && History->GetBestLapSeconds(HistoryKey(0))==39. &&
        History->GetBestLapSeconds(HistoryKey(1))==0. && History->GetBestLapSeconds(HistoryKey(2))==42. &&
        History->GetBestLapSeconds(HistoryKey(HistoryLimit))==60.);
    History->GateGuid=FirstIdentity;
    History->EnsureIdentityAndOrder();
    TestTrue(TEXT("Repairing a copied gate identity discards the copied layout history"),
        History->GetGateId().IsValid() && History->GetGateId()!=FirstIdentity && History->BestLapRecords.IsEmpty() &&
        History->GetBestLapSeconds(HistoryKey(0))==0. && Gate->GetBestLapSeconds(TEXT("layout-A"))==36.);
    History->Destroy();

    auto* DamagedHistory=MakeGate(FVector(4400,0,0));
    const auto AddDamagedRecord=[&](double Time,TArray<double> Times) {
        auto& Record=DamagedHistory->BestLapRecords.AddDefaulted_GetRef();
        Record.LayoutSignature=TEXT("duplicate-layout");
        Record.Seconds=Time;
        Record.Splits=MoveTemp(Times);
    };
    AddDamagedRecord(Nan,{8.,18.,27.});
    AddDamagedRecord(55.,{8.,18.,27.});
    AddDamagedRecord(54.,{8.,Infinity,27.});
    AddDamagedRecord(54.,{7.,19.,31.});
    TestTrue(TEXT("Duplicate saved rows prefer the fastest valid time and usable splits on a tie"),
        DamagedHistory->GetBestLapSeconds(TEXT("duplicate-layout"))==54. &&
        SplitsMatch(DamagedHistory->GetBestLapSplits(TEXT("duplicate-layout"),3),OtherSplits));
    TestTrue(TEXT("A later write normalizes duplicate saved rows"),
        DamagedHistory->RecordLap(60.,TEXT("another-layout")) && DamagedHistory->BestLapRecords.Num()==2 &&
        DamagedHistory->GetBestLapSeconds(TEXT("duplicate-layout"))==54.);
    DamagedHistory->ResetBestLap(TEXT("duplicate-layout"));
    TestTrue(TEXT("Removing the normalized duplicate layout leaves the independent record"),
        DamagedHistory->GetBestLapSeconds(TEXT("duplicate-layout"))==0. &&
        DamagedHistory->GetBestLapSeconds(TEXT("another-layout"))==60.);
    DamagedHistory->Destroy();

    TestFalse(TEXT("Out-of-range configuration is rejected"),Gate->Configure(0,1,Error) || Gate->Configure(1,129,Error));
    TestEqual(TEXT("Rejected configuration does not erase PB"),Gate->GetBestLapSeconds(TEXT("layout-B")),60.);
    TestTrue(TEXT("Changing course/order is supported"),Gate->Configure(3,1,Error));
    TestTrue(TEXT("Changing course preserves history under the old exact signature only"),
        Gate->GetBestLapSeconds(TEXT("layout-B"))==60. && SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits) &&
        Gate->GetBestLapSeconds(TEXT("layout-new-course"))==0.);
    const double SwapSplits[]{10.};
    if (!TestTrue(TEXT("Prepare split-bearing best and an unambiguous same-course swap partner"),
        Second->Configure(3,2,Error) && Gate->RecordLap(30,TEXT("layout-C"),SwapSplits))) return false;
    auto* Registry=World->SpawnActor<AFactoryFPVRaceCourseRegistry>();
    if (!TestNotNull(TEXT("Native course name registry"),Registry)) return false;
    TestTrue(TEXT("An unnamed world has no course name data to save"),
        !Registry->ShouldSave_Implementation() && Registry->GetNameForCourse(3).IsEmpty());
    TestTrue(TEXT("Saving a name trims the edges and leaves other course IDs unnamed"),
        Registry->SetCourseName(3,TEXT("  Refinery Sprint  "),Error) && Registry->ShouldSave_Implementation() &&
        Registry->GetNameForCourse(3)==TEXT("Refinery Sprint") && Registry->GetNameForCourse(1).IsEmpty());
    TestTrue(TEXT("Invalid course names leave the stored name in place"),
        !Registry->SetCourseName(0,TEXT("Invalid ID"),Error) &&
        !Registry->SetCourseName(3,TEXT("Multiple\nlines"),Error) &&
        !Registry->SetCourseName(3,FString::ChrN(AFactoryFPVRaceCourseRegistry::MaximumNameLength+1,TEXT('X')),Error) &&
        Registry->GetNameForCourse(3)==TEXT("Refinery Sprint"));
    TOptional<AFactoryFPVRaceGate::FEditorOrderSwap> Swap;
    const auto SwapTo=[&](int32 Order) {
        return !Gate->ConfigureFromEditor(3,Order,Gate->GetPolePassSide(),Gate->GetRaceDirection(),Error,Swap) &&
            Swap.IsSet() && Gate->ConfirmEditorOrderSwap(Swap.GetValue(),Error);
    };
    TestTrue(TEXT("Swapping start/finish hides the original physical gate's split record"),
        SwapTo(2) && Gate->GetBestLapSplits(TEXT("layout-C"),1).IsEmpty() && Second->GetBestLapSplits(TEXT("layout-C"),1).IsEmpty());
    TestEqual(TEXT("A course name stays with its ID when checkpoint 1 changes"),
        Registry->GetNameForCourse(3),FString(TEXT("Refinery Sprint")));
    TestTrue(TEXT("Reversing a swap restores both the original time and checkpoint split"),
        SwapTo(1) && Gate->GetBestLapSeconds(TEXT("layout-C"))==30. && SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-C"),1),SwapSplits));
    TestTrue(TEXT("Clearing the only name leaves no registry data to save"),
        Registry->SetCourseName(3,FString(),Error) && Registry->GetNameForCourse(3).IsEmpty() && !Registry->ShouldSave_Implementation());
    Gate->ResetBestLap(TEXT("layout-C"));
    TestTrue(TEXT("Explicit reset clears only the selected PB and its splits"),
        Gate->GetBestLapSeconds(TEXT("layout-C"))==0. && Gate->GetBestLapSplits(TEXT("layout-C"),1).IsEmpty() &&
        Gate->GetBestLapSeconds(TEXT("layout-B"))==60. && SplitsMatch(Gate->GetBestLapSplits(TEXT("layout-B"),3),OtherSplits));
    auto* Pole=World->SpawnActor<AFactoryFPVRacePole>(FVector(10000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Concrete native slalom pole"),Pole)) return false;
    TestFalse(TEXT("Slalom pole is a persistent concrete buildable"),AFactoryFPVRacePole::StaticClass()->HasAnyClassFlags(CLASS_Abstract|CLASS_Transient));
    Pole->EnsureIdentityAndOrder();TestTrue(TEXT("Slalom pole uses normal saved course configuration"),Pole->Configure(5,1,-1,Error));
    TestTrue(TEXT("Pole class selects open checkpoint geometry"),Pole->IsPoleCheckpoint());
    TestEqual(TEXT("Open pole reach and clear height use centimetres"),Pole->GetApertureHalfExtents(),FVector2D(800,180));
    TestEqual(TEXT("Solid pole scoring radius matches thirty-centimetre post"),Pole->GetPoleRadiusCm(),15.f);
    TestTrue(TEXT("Physical pole is thirty by thirty by four hundred centimetres"),
        (Pole->FrameParts[0]->GetRelativeScale3D()*100.).Equals(FVector(30,30,400),.001));
    for (int32 Index=1;Index<=3;++Index) {
        TestNull(TEXT("Pole has no ghost frame mesh"),Pole->FrameParts[Index]->GetStaticMesh());
        TestEqual(TEXT("Pole has no invisible frame collision"),Pole->FrameParts[Index]->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    }
    auto PolePoint=[](double X,double Y,double Z) { return FVector(10000+X,Y,Z); };
    TestTrue(TEXT("Slalom solid post blocks a real drone-sized sphere"),Sweep(PolePoint(-200,0,200),PolePoint(200,0,200)));
    TestFalse(TEXT("Open slalom side has no gate frame"),Sweep(PolePoint(-200,-100,200),PolePoint(200,-100,200)));
    TestFalse(TEXT("Opposite side remains physically open even though it does not score"),Sweep(PolePoint(-200,100,200),PolePoint(200,100,200)));
    TestFalse(TEXT("Former gate-post location has no invisible pole frame"),Sweep(PolePoint(-200,390,200),PolePoint(200,390,200)));
    TestTrue(TEXT("Left-passing pole points navigation toward its selected open side"),Pole->GetPassageAimPoint().Equals(PolePoint(0,-407.5,200),.001));
    TestTrue(TEXT("Pole label gives an explicit left-side instruction"),Pole->GetCheckpointInstructions().Contains(TEXT("LEFT")));
    TestFalse(TEXT("Invalid pole side is refused"),Pole->Configure(5,1,0,Error));
    Pole->RecordLap(12,TEXT("pole-layout"));
    TestTrue(TEXT("Changing pole passing side is supported"),Pole->Configure(5,1,1,Error));
    TestTrue(TEXT("Changed pole passing side leaves the old exact record available on its own signature"),
        Pole->GetBestLapSeconds(TEXT("pole-layout"))==12. && Pole->GetBestLapSeconds(TEXT("right-pole-layout"))==0.);
    TestTrue(TEXT("Right-passing pole moves its navigation aim to the other side"),Pole->GetPassageAimPoint().Equals(PolePoint(0,407.5,200),.001));
    Pole->Configure(5,1,-1,Error);Pole->RecordLap(14,TEXT("saved-pole-layout"));
    TArray<uint8> PoleBytes;FMemoryWriter PoleWriter(PoleBytes);FObjectAndNameAsStringProxyArchive PoleSave(PoleWriter,false);PoleSave.ArIsSaveGame=true;Pole->Serialize(PoleSave);
    auto* LoadedPole=World->SpawnActor<AFactoryFPVRacePole>(FVector(12000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Save-property pole target"),LoadedPole)) return false;
    auto* PolePower=LoadedPole->PowerInfo.Get();auto* PoleConnection=LoadedPole->PowerConnection.Get();
    FMemoryReader PoleReader(PoleBytes);FObjectAndNameAsStringProxyArchive PoleLoad(PoleReader,false);PoleLoad.ArIsSaveGame=true;LoadedPole->Serialize(PoleLoad);
    LoadedPole->PowerInfo=PolePower;LoadedPole->PowerConnection=PoleConnection;LoadedPole->ApplyCheckpointGeometry();
    TestTrue(TEXT("Actual SaveGame property archive preserves pole class, side, course and identity"),LoadedPole->IsPoleCheckpoint() && LoadedPole->GetPolePassSide()==-1 && LoadedPole->GetCourseId()==5 && LoadedPole->GetGateOrder()==1 && LoadedPole->GetGateId()==Pole->GetGateId());
    TestEqual(TEXT("Pole save-property archive preserves layout-specific PB"),LoadedPole->GetBestLapSeconds(TEXT("saved-pole-layout")),14.);
    TestFalse(TEXT("Loaded pole starts without stale next-checkpoint ownership"),LoadedPole->IsNextCheckpointHighlighted());
    LoadedPole->Destroy();Pole->Destroy();

    using Direction=EFactoryFPVRaceGateDirection;
    const FVector LiftLocation(16000,0,0);
    auto* Lift=World->SpawnActor<AFactoryFPVRaceGateVertical>(LiftLocation,FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Concrete native vertical gate"),Lift)) return false;
    TestTrue(TEXT("Vertical gate is a persistent buildable and defaults to ascent"),Lift->IsVerticalRaceGate() &&
        Lift->GetRaceDirection()==Direction::Ascend && !Lift->GetClass()->HasAnyClassFlags(CLASS_Abstract|CLASS_Transient));
    TestTrue(TEXT("Ordinary gate keeps its forward direction when vertical configuration is rejected"),
        !Gate->Configure(3,1,1,Direction::Ascend,Error) && Gate->GetRaceDirection()==Direction::Forward);
    Lift->EnsureIdentityAndOrder();
    TestTrue(TEXT("Vertical gate accepts ascent configuration"),Lift->Configure(6,1,1,Direction::Ascend,Error));
    TestTrue(TEXT("Vertical gate refuses forward direction without changing its configuration"),
        !Lift->Configure(6,1,1,Direction::Forward,Error) && Lift->GetRaceDirection()==Direction::Ascend);
    TestEqual(TEXT("Horizontal aperture is 3.6 metres clear in each direction"),Lift->GetApertureHalfExtents(),FVector2D(180,180));
    TestTrue(TEXT("Vertical gate cable terminal remains outside the clear passage"),
        Lift->GetPowerConnection()->GetRelativeLocation().Equals(FVector(-207,-190,282),.001));
    const FVector LiftCenter=LiftLocation+FVector(0,0,260);
    const FVector LiftBelow=LiftCenter-FVector(0,0,200);
    const FVector LiftAbove=LiftCenter+FVector(0,0,200);
    const FTransform Ascending=Lift->GetApertureTransform();
    TestTrue(TEXT("Ascend plane faces up through the horizontal frame center"),Ascending.GetLocation().Equals(LiftCenter,.001) &&
        Ascending.GetScaledAxis(EAxis::X).Equals(FVector(0,0,1),.001));
    TestFalse(TEXT("A drone sphere passes upward through the actual horizontal opening"),Sweep(LiftBelow,LiftAbove));
    TestFalse(TEXT("A drone sphere clears the opening close to a rail"),Sweep(LiftBelow+FVector(0,165,0),LiftAbove+FVector(0,165,0)));
    TestTrue(TEXT("The side rail blocks a vertical pass at the frame"),Sweep(LiftBelow+FVector(0,190,0),LiftAbove+FVector(0,190,0)));
    TestTrue(TEXT("The far rail blocks a vertical pass at the frame"),Sweep(LiftBelow+FVector(-190,0,0),LiftAbove+FVector(-190,0,0)));
    TestTrue(TEXT("Far-corner support has physical collision beneath the gate"),
        Sweep(LiftLocation+FVector(-250,190,100),LiftLocation+FVector(-120,190,100)));

    TestFalse(TEXT("The native vertical opening and both approaches are clear"),Lift->ProbeVerticalClearance(12.).HasObstruction());
    auto* Obstacle=World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Vertical clearance blocker"),Obstacle)) return false;
    auto* ObstacleBox=NewObject<UBoxComponent>(Obstacle);
    Obstacle->SetRootComponent(ObstacleBox);
    Obstacle->AddInstanceComponent(ObstacleBox);
    ObstacleBox->SetBoxExtent(FVector(5.));
    ObstacleBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    ObstacleBox->SetCollisionObjectType(ECC_WorldStatic);
    ObstacleBox->SetCollisionResponseToAllChannels(ECR_Block);
    ObstacleBox->RegisterComponent();
    const auto CheckClearance=[&](const TCHAR* Label,const FVector& Offset,double Radius,bool Opening,bool Above,bool Below) {
        Obstacle->SetActorLocation(LiftCenter+Offset);
        const auto Clearance=Lift->ProbeVerticalClearance(Radius);
        TestTrue(Label,Clearance.bOpeningBlocked==Opening && Clearance.bAboveBlocked==Above && Clearance.bBelowBlocked==Below);
    };
    CheckClearance(TEXT("A solid at the opening is reported once"),FVector::ZeroVector,12.,true,false,false);
    CheckClearance(TEXT("A solid above the opening is reported only above"),FVector(0,0,150),12.,false,true,false);
    CheckClearance(TEXT("A solid below the opening is reported only below"),FVector(0,0,-150),12.,false,false,true);
    CheckClearance(TEXT("Geometry beyond the local approach is outside the advisory"),FVector(0,0,270),12.,false,false,false);
    CheckClearance(TEXT("The smaller drone clears an offset obstruction"),FVector(25,0,100),12.,false,false,false);
    CheckClearance(TEXT("The larger drone detects the same offset obstruction"),FVector(25,0,100),30.,false,true,false);
    DesignerFlag->SetPropertyValue_InContainer(Lift,true);
    CheckClearance(TEXT("A Blueprint Designer preview has no live approach advisory"),FVector::ZeroVector,12.,false,false,false);
    DesignerFlag->SetPropertyValue_InContainer(Lift,false);
    Obstacle->SetActorLocation(Gate->GetPassageAimPoint());
    TestFalse(TEXT("An upright gate does not report a vertical approach advisory"),Gate->ProbeVerticalClearance(12.).HasObstruction());

    Circuit->SetPropertyValue_InContainer(Lift->GetPowerInfo(),17);
    HasPower->SetPropertyValue_InContainer(Lift->GetPowerInfo(),true);
    Lift->CreateLightMaterials();
    Lift->SetNextCheckpointHighlighted(true);
    auto CheckVerticalLeds=[&](bool Ascend) {
        if (!TestEqual(TEXT("Vertical gate owns eight controllable LEDs"),Lift->LightMaterials.Num(),8)) return;
        int32 BottomCount=0;
        for (int32 Index=0;Index<Lift->LightMaterials.Num();++Index) {
            auto* Material=Lift->LightMaterials[Index].Get();
            if (!TestNotNull(TEXT("Vertical LED material"),Material)) continue;
            const bool Bottom=Lift->LightStrips[Index]->GetRelativeLocation().Z<260;
            BottomCount+=Bottom;
            TestTrue(TEXT("Only the vertical approach face turns pink at normal strength"),
                IsHotPink(Material->K2_GetVectorParameterValue(TEXT("LightColor")))==(Bottom==Ascend) &&
                Material->K2_GetScalarParameterValue(TEXT("LightStrength"))==12.f);
        }
        TestEqual(TEXT("The underside owns four distinct approach LEDs"),BottomCount,4);
    };
    CheckVerticalLeds(true);
    TestTrue(TEXT("Vertical gate can switch to descent"),Lift->Configure(6,1,1,Direction::Descend,Error));
    CheckVerticalLeds(false);
    const FTransform Descending=Lift->GetApertureTransform();
    TestTrue(TEXT("Direction reversal preserves center and reverses the scoring normal"),Descending.GetLocation().Equals(LiftCenter,.001) &&
        Descending.GetScaledAxis(EAxis::X).Equals(FVector(0,0,-1),.001));
    CheckClearance(TEXT("Changing the scoring direction still reports physical above"),FVector(0,0,150),12.,false,true,false);
    CheckClearance(TEXT("Changing the scoring direction still reports physical below"),FVector(0,0,-150),12.,false,false,true);
    Obstacle->Destroy();
    TestFalse(TEXT("A drone sphere passes downward through the same opening"),Sweep(LiftAbove,LiftBelow));
    TestTrue(TEXT("Direction reversal preserves actual frame collision"),Sweep(LiftAbove+FVector(0,190,0),LiftBelow+FVector(0,190,0)));
    TestTrue(TEXT("Legacy configure overload retains the selected vertical direction"),
        Lift->Configure(6,1,Error) && Lift->GetRaceDirection()==Direction::Descend);

    TArray<uint8> LiftBytes;
    FMemoryWriter LiftWriter(LiftBytes);
    FObjectAndNameAsStringProxyArchive LiftSave(LiftWriter,false);
    LiftSave.ArIsSaveGame=true;
    Lift->Serialize(LiftSave);
    auto* LoadedLift=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(18000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Save-property vertical target"),LoadedLift)) return false;
    TestTrue(TEXT("Fresh vertical target starts with its constructor default"),LoadedLift->GetRaceDirection()==Direction::Ascend);
    auto* LiftPower=LoadedLift->PowerInfo.Get();
    auto* LiftConnection=LoadedLift->PowerConnection.Get();
    FMemoryReader LiftReader(LiftBytes);
    FObjectAndNameAsStringProxyArchive LiftLoad(LiftReader,false);
    LiftLoad.ArIsSaveGame=true;
    LoadedLift->Serialize(LiftLoad);
    LoadedLift->PowerInfo=LiftPower;
    LoadedLift->PowerConnection=LiftConnection;
    TestTrue(TEXT("Actual SaveGame archive restores descent and the saved checkpoint identity"),LoadedLift->IsVerticalRaceGate() &&
        LoadedLift->GetRaceDirection()==Direction::Descend && LoadedLift->GetCourseId()==6 && LoadedLift->GetGateOrder()==1 &&
        LoadedLift->GetGateId()==Lift->GetGateId() && LoadedLift->GetApertureTransform().GetScaledAxis(EAxis::X).Equals(FVector(0,0,-1),.001));
    LoadedLift->Destroy();
    Lift->Destroy();

    const FVector FloorTop(24000,0,1000);
    auto* Floor=World->SpawnActor<AFactoryFPVRaceGateFloor>(FloorTop,FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Concrete native floor gate"),Floor)) return false;
    TestTrue(TEXT("Floor gate uses vertical scoring and has no leg components"),
        Floor->IsFloorRaceGate() && Floor->IsVerticalRaceGate() && Floor->VerticalSupports.IsEmpty());
    TestEqual(TEXT("Floor gate has a 7.2 m square opening"),Floor->GetApertureHalfExtents(),FVector2D(360,360));
    TestTrue(TEXT("Floor passage is centered within the 1 m tile depth"),
        Floor->GetPassageAimPoint().Equals(FloorTop-FVector(0,0,50),.001));
    const FVector FloorBelow=FloorTop-FVector(0,0,240);
    const FVector FloorAbove=FloorTop+FVector(0,0,180);
    TestFalse(TEXT("A swept drone clears the floor opening from either side"),
        Sweep(FloorBelow,FloorAbove) || Sweep(FloorAbove,FloorBelow));
    TestTrue(TEXT("The floor rim blocks passage through its solid edge"),
        Sweep(FloorBelow+FVector(0,380,0),FloorAbove+FVector(0,380,0)));
    TestFalse(TEXT("The rim is confined to one foundation tile"),
        Sweep(FloorBelow+FVector(0,430,0),FloorAbove+FVector(0,430,0)));
    TestFalse(TEXT("The floor gate has no supports beneath the tile"),
        Sweep(FloorTop+FVector(-450,-380,-175),FloorTop+FVector(-300,-380,-175)));
    TestTrue(TEXT("Floor descent uses the same ordered vertical crossing direction"),
        Floor->Configure(7,1,1,Direction::Descend,Error) &&
        Floor->GetApertureTransform().GetUnitAxis(EAxis::X).Equals(FVector::DownVector,.001));
    Floor->Destroy();
    AddInfo(TEXT("Physical aperture/collision, persistence fields, numbering and reported-power consumer behavior tested. Actual build placement, cable/grid consumption and Satisfactory save/reload require shipping acceptance."));
    return true;
}
#endif
