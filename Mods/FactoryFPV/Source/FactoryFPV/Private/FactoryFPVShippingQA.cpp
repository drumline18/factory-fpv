#include "FactoryFPVShippingQA.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRacePanel.h"
#include "FactoryFPVRaceRoutePreview.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVRacingContent.h"
#include "FactoryFPVSettings.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVShippingVerticalFlight.h"
#include "FGBuildableSubsystem.h"
#include "FGBuildEffectActor.h"
#include "Buildables/FGBuildableWire.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGGameState.h"
#include "FGHUD.h"
#include "FGPowerCircuit.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGSchematicManager.h"
#include "FGPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Async/Async.h"
#include "Framework/Application/SlateApplication.h"
#include "Containers/Ticker.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Types/SlateAttributeMetaData.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

namespace FactoryFPVShippingQAInternal {
constexpr int32 Course=97;
constexpr double KnownLapSeconds=20.;
constexpr double KnownCheckpointSplit=.5;
constexpr double AlternateLapSeconds=13.;
constexpr double AlternateCheckpointSplit=1.25;
constexpr double LiveGateSpacingCm=500.;
constexpr double LiveColumnHeightCm=940.;
const TCHAR* CloneName=TEXT("FPV-QA-Clone");
const TCHAR* RoundtripName=TEXT("FPV-QA-Roundtrip");
const TCHAR* PracticeName=TEXT("FPV-Vertical-Practice");
const TCHAR* PracticeCourseName=TEXT("Vertical Practice");

FString NormalizedPath(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path.Replace(TEXT("\\"),TEXT("/"));
}
bool DisposablePath(const FString& Path) {
    return !Path.IsEmpty() && NormalizedPath(Path).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
FString Escape(FString Text) {
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
void Marker(bool Verify,const TCHAR* Result,const FString& Reason,const FString& Extra=FString()) {
    const auto* CommandLine=FCommandLine::Get();
    const bool bPractice=FParse::Param(CommandLine,TEXT("FactoryFPVQAVerticalPractice"));
    const bool bLiveVertical=bPractice || FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical"));
    const TCHAR* Stage=bPractice ? (Verify ? TEXT("practice-verify") : TEXT("practice-save")) : (Verify ? TEXT("verify") : TEXT("begin"));
    const TCHAR* Scope=bPractice ? TEXT("native staged practice save and separate live verification") :
        bLiveVertical ? TEXT("ordinary drone launch and return, scripted throttle through native physics, sweeps, racing and OSD") :
        TEXT("native API only; excludes hologram, cost, E UI and radio flight");
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"%s\"%s}"),
        Stage,Result,*Escape(Reason),Scope,*Extra);
    // Shipping config filters LogTemp at Warning. These machine-readable fixture
    // results must bypass category filtering while retaining normal log output.
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
bool GuardPaths(FString& Error) {
    if (!FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) {
        Error=TEXT("Missing explicit -FactoryFPVAutomation opt-in.");return false;
    }
    if (!DisposablePath(FPaths::ProjectSavedDir()) || !DisposablePath(UFGSaveSystem::GetSaveDirectoryPath())) {
        Error=TEXT("Project Saved and real Satisfactory save directories must both be under /build/shipping-world-qa-.");return false;
    }
    return true;
}
FString ManifestPath() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FactoryFPV-ShippingQA-Manifest.txt")); }
FString FiniteJson(double Value) { return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.9g"),Value) : FString(TEXT("null")); }
const TCHAR* DirectionName(EFactoryFPVRaceGateDirection Direction) {
    if (Direction==EFactoryFPVRaceGateDirection::Ascend) return TEXT("ASCEND");
    if (Direction==EFactoryFPVRaceGateDirection::Descend) return TEXT("DESCEND");
    return TEXT("FORWARD");
}
TSharedPtr<SWidget> FindTaggedSlateWidget(const TSharedRef<SWidget>& Root,FName Tag) {
    TSharedPtr<SWidget> Match;
    TArray<TSharedRef<SWidget>> Widgets{Root};
    while (!Widgets.IsEmpty()) {
        const auto Widget=Widgets.Pop(EAllowShrinking::No);
        if (Widget->GetTag()==Tag) {
            if (Match) return nullptr;
            Match=Widget;
        }
        auto* Children=Widget->GetChildren();
        for (int32 Index=0;Index<Children->Num();++Index) Widgets.Add(Children->GetChildAt(Index));
    }
    return Match;
}
bool SlateVisibleFrom(const TSharedRef<SWidget>& Root,TSharedPtr<SWidget> Widget) {
    for (;Widget;Widget=Widget->GetParentWidget()) {
        if (!Widget->GetVisibility().IsVisible()) return false;
        if (Widget.Get()==&Root.Get()) return true;
    }
    return false;
}
FKey SlateAcceptKey() {
    if (FSlateApplication::IsInitialized()) for (const FKey& Key:{EKeys::Enter,EKeys::SpaceBar,EKeys::Virtual_Accept,EKeys::Gamepad_FaceButton_Bottom}) {
        if (FSlateApplication::Get().GetNavigationActionFromKey(FKeyEvent(Key,FModifierKeysState(),0,false,0,0))==EUINavigationAction::Accept)
            return Key;
    }
    return {};
}
bool PressSlateButton(SButton& Button,const FKeyEvent& AcceptEvent) {
    const auto Geometry=Button.GetCachedGeometry();
    const bool bDown=Button.OnKeyDown(Geometry,AcceptEvent).IsEventHandled();
    const bool bUp=Button.OnKeyUp(Geometry,AcceptEvent).IsEventHandled();
    return bDown && bUp;
}
bool PressTaggedSlateButton(const TSharedRef<SWidget>& Root,const TCHAR* Tag,const FKeyEvent& AcceptEvent) {
    const auto Widget=FindTaggedSlateWidget(Root,FName(Tag));
    if (!Widget || Widget->GetTypeAsString()!=TEXT("SButton") || !SlateVisibleFrom(Root,Widget)) return false;
    FSlateAttributeMetaData::UpdateAllAttributes(*Widget,FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    return Widget->IsEnabled() && PressSlateButton(*StaticCastSharedPtr<SButton>(Widget),AcceptEvent);
}
struct FGateLighting {
    bool bActorVisible=false;
    int32 Points=0,VisiblePoints=0,Strips=0,VisibleStrips=0,ReadableStrengths=0;
    float Minimum=0,Maximum=0;
    FString Strengths=TEXT("[]");
    bool Matches(bool Powered) const {
        const float Expected=Powered ? 12.f : 0.f;
        return bActorVisible && Points==2 && VisiblePoints==(Powered ? 2 : 0) && Strips==8 && VisibleStrips==8 && ReadableStrengths==8 &&
            FMath::IsNearlyEqual(Minimum,Expected,.001f) && FMath::IsNearlyEqual(Maximum,Expected,.001f);
    }
    FString Json() const {
        return FString::Printf(TEXT("{\"actorVisible\":%s,\"pointLights\":%d,\"visiblePointLights\":%d,\"strips\":%d,\"visibleStrips\":%d,\"readableStrengths\":%d,\"minimumStrength\":%s,\"maximumStrength\":%s,\"strengths\":%s}"),
            bActorVisible ? TEXT("true") : TEXT("false"),Points,VisiblePoints,Strips,VisibleStrips,ReadableStrengths,
            *(ReadableStrengths ? FiniteJson(Minimum) : FString(TEXT("null"))),*(ReadableStrengths ? FiniteJson(Maximum) : FString(TEXT("null"))),*Strengths);
    }
};
FGateLighting ReadGateLighting(AFactoryFPVRaceGate* Gate) {
    FGateLighting Result;
    if (!IsValid(Gate)) return Result;
    Result.bActorVisible=!Gate->IsHidden();
    TInlineComponentArray<UPointLightComponent*> Points(Gate);
    for (auto* Light:Points) if (Light && Light->GetName().StartsWith(TEXT("GateLight"))) {
        ++Result.Points;if (Light->IsVisible() && !Light->bHiddenInGame) ++Result.VisiblePoints;
    }
    TInlineComponentArray<UStaticMeshComponent*> Meshes(Gate);
    Result.Strengths=TEXT("[");
    for (auto* Mesh:Meshes) if (Mesh && Mesh->GetName().StartsWith(TEXT("MainMesh_Light"))) {
        if (Result.Strips++) Result.Strengths+=TEXT(",");
        if (Mesh->IsVisible() && !Mesh->bHiddenInGame) ++Result.VisibleStrips;
        const auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));float Strength=0;
        const bool Read=Material && Material->GetScalarParameterValue(TEXT("LightStrength"),Strength) && FMath::IsFinite(Strength);
        Result.Strengths+=FString::Printf(TEXT("{\"component\":\"%s\",\"material\":\"%s\",\"strength\":%s}"),*Escape(Mesh->GetName()),*Escape(GetPathNameSafe(Mesh->GetMaterial(0))),*(Read ? FiniteJson(Strength) : FString(TEXT("null"))));
        if (Read) {
            if (!Result.ReadableStrengths) Result.Minimum=Result.Maximum=Strength;
            else { Result.Minimum=FMath::Min(Result.Minimum,Strength);Result.Maximum=FMath::Max(Result.Maximum,Strength); }
            ++Result.ReadableStrengths;
        }
    }
    Result.Strengths+=TEXT("]");return Result;
}

struct FRun : public TSharedFromThis<FRun> {
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<UFGPowerConnectionComponent> Source;
    TWeakObjectPtr<UFGSaveSession> SaveSession;
    TArray<TWeakObjectPtr<AFactoryFPVRaceGate>> Gates;
    TArray<TWeakObjectPtr<AFGBuildableWire>> Wires;
    TMap<FString,FString> Manifest;
    double Deadline=0;
    double ReadySince=0;
    bool bVerify=false;
    bool bPractice=false,bPracticeSaveCallback=false,bPracticeSaveVerified=false,bPracticeLoaded=false,bPracticeFreshRecord=false;
    bool bPracticeStagedPioneer=false,bPracticeGatesRetained=false;
    bool bPracticeRaceButton=false,bPracticeRaceButtonVerified=false;
    bool bPracticeModeButton=false,bPracticeModeButtonVerified=false;
    bool bPracticeCourseName=false,bPracticeNameLayoutUnchanged=false,bPracticeNameReloaded=false,bPracticeNamePresented=false;
    bool bPracticeNameApplyVerified=false;
    FString PracticePath,PracticeSessionName,PracticeMapName;
    FString PracticeObservedCourseName;
    bool bLiveVertical=false,bLivePoseStaged=false,bLiveReturning=false,bLivePoweredGates=false,bLiveClearColumn=false;
    bool bLivePioneerRestored=false,bLiveFixtureRemoved=false;
    bool bCheckRaceDebrief=false,bRaceDebriefLapVerified=false,bRaceDebriefReportVerified=false;
    bool bLiveMoveIgnored=false,bLiveLookIgnored=false,bLiveHudHidden=false,bLiveHudPartial=false;
    FTransform LiveOriginalPose;
    FRotator LiveOriginalControl;
    FVector LiveStand=FVector::ZeroVector;
    double LiveYaw=0.;
    double LiveNextSetupTick=0.;
    EMovementMode LiveOriginalMovement=MOVE_None;
    TWeakObjectPtr<AFactoryFPVDrone> LiveDrone;
    FFactoryFPVShippingVerticalFlight LiveFlight;
    bool bVerticalCourseEnabled=false,bVerticalNextTargetVerified=false,bVerticalEntryOnlyPinkVerified=false,bVerticalApproachVerified=false;
    bool bVerticalWrongWayRejected=false,bVerticalWrongWayFeedbackVerified=false,bVerticalLapScored=false,bVerticalFinishFeedbackVerified=false;
    bool bVerticalSelectedLayoutGuardVerified=false,bVerticalResetProtectionVerified=false,bVerticalRoutePreviewVerified=false;
    bool bVerticalClearanceAdvisoryVerified=false;
    bool bVerticalOrderSwapVerified=false;
    bool bVerticalNumberingCompactionVerified=false;
    bool bVerticalSplitTimingVerified=false,bVerticalSplitReloadVerified=false;
    bool bVerticalLayoutRecordsVerified=false;
    double VerifiedAlternateBest=0.,VerifiedAlternateSplit=0.;
    double VerifiedSplitDelta=0.;
    bool bMilestonePurchased=false,bGateRecipeAvailable=false,bFullGateRecipeAvailable=false,bPoleRecipeAvailable=false,bVerticalRecipeAvailable=false,bRecipeReadbackReady=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bNextTargetVerified=false,bCaptureViewRestored=false;
    bool bEntryOnlyPinkVerified=false,bOrdinaryStartPresentationVerified=false;
    TWeakObjectPtr<APlayerController> CaptureController;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<ACameraActor> CaptureCamera;
    TStrongObjectPtr<UFactoryFPVRaceComponent> CaptureRace;
    FDelegateHandle ScreenshotHandle;
    bool bOwnScreenshot=false,bScreenshotProcessed=false,bScreenMessagesBefore=false;
    int32 CaptureIndex=0;
    double CaptureSettleSince=0,CaptureStarted=0;
    FString PendingScreenshot;
    struct FCapture { FString Path;int32 Width=0,Height=0;double Variance=0;bool bDecoded=false; };
    TArray<FCapture> Captures;
    TArray<FString> ExistingGateRows;
    int32 ExistingCompactGateCount=0;
    bool bExistingGatesObserved=false,bExistingGatesRestored=false;
    bool bSpawned=false;
    bool bSaving=false;
    bool bDone=false;
    bool bSetupDiagnosticsPrinted=false;
    bool bConstructionEffectsEnabled=false;
    bool ConstructionEffectsObserved[2]={};
    bool bConstructionEffectsCompleted=false;
    bool bWired=false;
    TStrongObjectPtr<UClass> PendingWireClass;
    TStrongObjectPtr<UClass> PendingWireRecipe;
    bool bPowerCycleEnabled=false,bPowerCycleDisconnected=false,bPowerLossObserved=false,bPowerCycleReconnected=false,bPowerCycleRestored=false;
    bool bPowerCycleCourseFaultObserved=false,bPowerCycleCourseRecovered=false,bPowerCycleGate2StayedPowered=false;
    int32 PowerCyclePhase=0;
    TStrongObjectPtr<UFactoryFPVRaceComponent> PowerCycleRace;
    FString PowerCycleBaseline=TEXT("null"),PowerCycleLost=TEXT("null"),PowerCycleRestored=TEXT("null");
    FString LastReason=TEXT("Waiting for the loaded world and living pioneer.");

    bool RecipeReadback() {
        auto* Recipes=AFGRecipeManager::Get(World.Get());auto* Schematics=AFGSchematicManager::Get(World.Get());
        if (!Recipes || !Schematics) { LastReason=TEXT("Waiting for actual recipe/schematic managers.");return false; }
        bMilestonePurchased=Schematics->IsSchematicPurchased(UFactoryFPVRacingSchematic::StaticClass());
        const auto& Available=Recipes->GetAllAvailableRecipes();
        bGateRecipeAvailable=Available.Contains(UFactoryFPVRaceGateRecipe::StaticClass());
        bFullGateRecipeAvailable=Available.Contains(UFactoryFPVRaceGateFullRecipe::StaticClass());
        bPoleRecipeAvailable=Available.Contains(UFactoryFPVRacePoleRecipe::StaticClass());
        bVerticalRecipeAvailable=Available.Contains(UFactoryFPVRaceGateVerticalRecipe::StaticClass());
        // Read only: never purchase, register or add an available recipe here.
        if (bMilestonePurchased && (!bGateRecipeAvailable || !bFullGateRecipeAvailable || !bPoleRecipeAvailable || !bVerticalRecipeAvailable)) {
            LastReason=TEXT("Already-purchased racing milestone has not exposed the compact gate, full-size gate, pole and vertical gate recipes through normal startup migration.");return false;
        }
        bRecipeReadbackReady=true;return true;
    }
    UStaticMeshComponent* NamedMesh(AFactoryFPVRaceGate* Gate,const TCHAR* Name) const {
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Gate);
        for (auto* Mesh:Meshes) if (Mesh && Mesh->GetFName()==FName(Name)) return Mesh;
        return nullptr;
    }
    bool VerticalGeometryMatches(AFactoryFPVRaceGate* Gate) const {
        if (!Gate || Gate->GetClass()!=AFactoryFPVRaceGateVertical::StaticClass() || !Gate->IsVerticalRaceGate() || Gate->IsPoleCheckpoint() ||
            !Gate->GetApertureHalfExtents().Equals(FVector2D(180,180),.01) || !Gate->GetPowerConnection() ||
            !Gate->GetPowerConnection()->GetRelativeLocation().Equals(FVector(-207,-190,282),.01)) return false;
        const FTransform Aperture=Gate->GetLocalApertureTransform();
        const auto Direction=Gate->GetRaceDirection();
        if (Direction!=EFactoryFPVRaceGateDirection::Ascend && Direction!=EFactoryFPVRaceGateDirection::Descend) return false;
        const bool Descending=Direction==EFactoryFPVRaceGateDirection::Descend;
        if (!Aperture.GetLocation().Equals(FVector(0,0,260),.01) ||
            !Aperture.GetUnitAxis(EAxis::X).Equals(Descending ? FVector::DownVector : FVector::UpVector,.001) ||
            !Gate->GetPassageAimPoint().Equals(Gate->GetApertureTransform().GetLocation(),.01)) return false;
        const TCHAR* Names[]={TEXT("MainMesh_LeftPost"),TEXT("MainMesh_RightPost"),TEXT("MainMesh_TopBar"),TEXT("MainMesh_BottomBar"),
            TEXT("MainMesh_VerticalSupportLeft"),TEXT("MainMesh_VerticalSupportRight")};
        const FVector Centers[]={FVector(0,-190,260),FVector(0,190,260),FVector(-190,0,260),FVector(190,0,260),FVector(-190,-190,125),FVector(-190,190,125)};
        const FVector Scales[]={FVector(4,.2,.2),FVector(4,.2,.2),FVector(.2,3.6,.2),FVector(.2,3.6,.2),FVector(.2,.2,2.5),FVector(.2,.2,2.5)};
        for (int32 Index=0;Index<UE_ARRAY_COUNT(Names);++Index) {
            const auto* Solid=NamedMesh(Gate,Names[Index]);
            if (!Solid || !Solid->GetStaticMesh() || Solid->GetCollisionEnabled()==ECollisionEnabled::NoCollision ||
                !Solid->GetRelativeLocation().Equals(Centers[Index],.01) || !Solid->GetRelativeScale3D().Equals(Scales[Index],.001)) return false;
        }
        return true;
    }
    bool GeometryMatches() const {
        if (Gates.Num()!=2 || !Gates[0].IsValid() || !Gates[1].IsValid()) return false;
        auto* Gate=Gates[0].Get();auto* Second=Gates[1].Get();
        if (bLiveVertical) {
            const auto Recipe=UFactoryFPVRaceGateVerticalRecipe::StaticClass();
            return VerticalGeometryMatches(Gate) && VerticalGeometryMatches(Second) &&
                Gate->GetBuiltWithRecipe()==Recipe && Second->GetBuiltWithRecipe()==Recipe &&
                Gate->GetRaceDirection()==EFactoryFPVRaceGateDirection::Ascend &&
                Second->GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend;
        }
        if (Gate->GetClass()!=AFactoryFPVRaceGateFull::StaticClass() || Gate->GetBuiltWithRecipe()!=UFactoryFPVRaceGateFullRecipe::StaticClass() ||
            !Gate->IsFullSizeGate() || Gate->IsPoleCheckpoint() || !Gate->GetApertureHalfExtents().Equals(FVector2D(380,180),.01)) return false;
        auto* Left=NamedMesh(Gate,TEXT("MainMesh_LeftPost"));auto* Right=NamedMesh(Gate,TEXT("MainMesh_RightPost"));
        if (!Left || !Right || !Left->GetStaticMesh() || !Right->GetStaticMesh() ||
            !Left->GetRelativeLocation().Equals(FVector(0,-390,200),.01) || !Right->GetRelativeLocation().Equals(FVector(0,390,200),.01) ||
            !Left->GetRelativeScale3D().Equals(FVector(.25,.2,4),.0001) || !Right->GetRelativeScale3D().Equals(FVector(.25,.2,4),.0001)) return false;
        if (bVerticalCourseEnabled) {
            if (!VerticalGeometryMatches(Second) || Second->GetBuiltWithRecipe()!=UFactoryFPVRaceGateVerticalRecipe::StaticClass() ||
                Second->GetRaceDirection()!=EFactoryFPVRaceGateDirection::Descend) return false;
        } else {
            auto* Post=NamedMesh(Second,TEXT("MainMesh_LeftPost"));
            if (Second->GetClass()!=AFactoryFPVRacePole::StaticClass() || Second->GetBuiltWithRecipe()!=UFactoryFPVRacePoleRecipe::StaticClass() ||
                !Second->IsPoleCheckpoint() || Second->GetPolePassSide()!=-1 || !Second->GetApertureHalfExtents().Equals(FVector2D(800,180),.01) ||
                !Post || !Post->GetStaticMesh() || !Post->GetRelativeLocation().Equals(FVector(0,0,200),.01) ||
                !Post->GetRelativeScale3D().Equals(FVector(.3,.3,4),.0001)) return false;
            for (const TCHAR* Name:{TEXT("MainMesh_RightPost"),TEXT("MainMesh_TopBar"),TEXT("MainMesh_BottomBar")}) {
                auto* Removed=NamedMesh(Second,Name);
                if (!Removed || Removed->GetStaticMesh() || Removed->GetCollisionEnabled()!=ECollisionEnabled::NoCollision) return false;
            }
        }
        for (const auto& Weak:Gates) {
            auto* Board=NamedMesh(Weak.Get(),TEXT("MainMesh_LabelBoard"));
            TInlineComponentArray<UTextRenderComponent*> Labels(Weak.Get());
            if (!Board || !Board->GetStaticMesh() || !Board->GetMaterial(0) || Board->GetMaterial(0)->GetBlendMode()!=BLEND_Opaque || Labels.Num()!=2) return false;
            for (auto* Label:Labels) if (!Label || Label->Text.IsEmpty() || Label->WorldSize<30) return false;
        }
        return true;
    }
    bool CheckpointPresentationMatches(int32 Index,bool bHighlighted) const {
        auto* Gate=Gates.IsValidIndex(Index) ? Gates[Index].Get() : nullptr;
        if (!Gate || Gate->IsNextCheckpointHighlighted()!=bHighlighted || !ReadGateLighting(Gate).Matches(true)) return false;
        const bool Descending=Gate->GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend;
        const FLinearColor Ordinary=Index==0 ? FLinearColor(1.f,.22f,.025f) : FLinearColor(.025f,.65f,1.f);
        const FLinearColor Pink(1.f,.035f,.42f);
        int32 EntryStrips=0,ReverseStrips=0;
        TInlineComponentArray<UStaticMeshComponent*> Meshes(Gate);
        for (auto* Mesh:Meshes) {
            if (!Mesh) continue;
            const FString Name=Mesh->GetName();
            if (Name.StartsWith(TEXT("NextCheckpoint")) && Mesh->IsVisible() && !Mesh->bHiddenInGame) return false;
            const bool bEntry=Name.StartsWith(Descending ? TEXT("MainMesh_Light1_") : TEXT("MainMesh_Light0_"));
            const bool bReverse=Name.StartsWith(Descending ? TEXT("MainMesh_Light0_") : TEXT("MainMesh_Light1_"));
            if (!bEntry && !bReverse) continue;
            if (Descending && (bEntry ? Mesh->GetRelativeLocation().Z<=260 : Mesh->GetRelativeLocation().Z>=260)) return false;
            const auto* Material=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
            FLinearColor Actual;
            const auto& Expected=bHighlighted && bEntry ? Pink : Ordinary;
            if (!Material || !Material->GetVectorParameterValue(TEXT("LightColor"),Actual) || !Actual.Equals(Expected,.001f)) return false;
            if (bEntry) ++EntryStrips;
            else ++ReverseStrips;
        }
        if (EntryStrips!=4 || ReverseStrips!=4) return false;
        TInlineComponentArray<UTextRenderComponent*> Labels(Gate);
        if (Labels.Num()!=2) return false;
        bool bEntryLabel=false,bReverseLabel=false;
        const FString Prefix=FString::Printf(TEXT("GATE %d  /  C%d<br>"),Index+1,Course);
        for (auto* Label:Labels) {
            if (!Label || Label->TextRenderColor!=Ordinary.ToFColor(true)) return false;
            const FString LabelText=Label->Text.ToString();
            bEntryLabel|=Label->GetFName()==FName(Descending ? TEXT("GateLabel1") : TEXT("GateLabel0")) && LabelText==Prefix+(Descending ? TEXT("DESCEND") : TEXT("START / FINISH"));
            bReverseLabel|=Label->GetFName()==FName(Descending ? TEXT("GateLabel0") : TEXT("GateLabel1")) && LabelText==Prefix+TEXT("WRONG WAY");
        }
        return bEntryLabel && bReverseLabel;
    }
    bool ObserveExistingGates() {
        TArray<FString> Rows;
        int32 CompactCount=0;
        for (TActorIterator<AFactoryFPVRaceGate> It(World.Get());It;++It) {
            auto* Gate=*It;
            if (!Gate->IsLiveRaceGate() || Gate->GetCourseId()==Course) continue;
            // Class names/GUIDs/order/wire endpoints are read, never migrated by
            // the fixture. Normal product BeginPlay owns the geometry update.
            if (Gate->IsVerticalRaceGate()) {
                if (!VerticalGeometryMatches(Gate)) {
                    Finish(false,TEXT("A pre-existing vertical gate did not restore its opening, supports, direction or socket."));return false;
                }
            } else if (!Gate->IsPoleCheckpoint()) {
                const bool bCompact=Gate->GetClass()==AFactoryFPVRaceGate::StaticClass();
                const double PostY=bCompact ? 160. : 390.,CenterZ=bCompact ? 170. : 200.;
                const FVector2D OpeningHalf=bCompact ? FVector2D(150,150) : FVector2D(380,180);
                const FVector Socket=bCompact ? FVector(-25,-160,210) : FVector(-28,-390,250);
                auto* Left=NamedMesh(Gate,TEXT("MainMesh_LeftPost"));auto* Right=NamedMesh(Gate,TEXT("MainMesh_RightPost"));
                if ((!bCompact && Gate->GetClass()!=AFactoryFPVRaceGateFull::StaticClass()) || Gate->IsFullSizeGate()==bCompact ||
                    !Left || !Right || !Left->GetRelativeLocation().Equals(FVector(0,-PostY,CenterZ),.01) ||
                    !Right->GetRelativeLocation().Equals(FVector(0,PostY,CenterZ),.01) ||
                    !Gate->GetApertureHalfExtents().Equals(OpeningHalf,.01) || !Gate->GetPowerConnection() ||
                    !Gate->GetPowerConnection()->GetRelativeLocation().Equals(Socket,.01)) {
                    Finish(false,TEXT("A pre-existing ring did not restore its class-specific opening, post positions or power socket on world load."));return false;
                }
                if (bCompact) ++CompactCount;
            }
            TArray<AFGBuildableWire*> Links;Gate->GetPowerConnection()->GetWires(Links);TArray<FString> WireRows;
            for (auto* Wire:Links) if (IsValid(Wire)) {
                TArray<FString> Ends{GetPathNameSafe(Wire->GetConnection(0)),GetPathNameSafe(Wire->GetConnection(1))};Ends.Sort();
                WireRows.Add(Wire->GetPathName()+TEXT(":")+FString::Join(Ends,TEXT(",")));
            }
            WireRows.Sort();
            Rows.Add(FString::Printf(TEXT("%s|%s|%d|%d|%d|%s|%s"),*Gate->GetGateId().ToString(EGuidFormats::Digits),
                *Gate->GetClass()->GetPathName(),Gate->GetCourseId(),Gate->GetGateOrder(),Gate->GetPolePassSide(),DirectionName(Gate->GetRaceDirection()),*FString::Join(WireRows,TEXT(";"))));
        }
        Rows.Sort();
        ExistingCompactGateCount=CompactCount;
        if (!bVerify) { ExistingGateRows=Rows;bExistingGatesObserved=true;return true; }
        if (Manifest.FindRef(TEXT("existingCount"))!=FString::FromInt(Rows.Num())) { Finish(false,TEXT("Pre-existing checkpoint count changed across the native roundtrip."));return false; }
        for (int32 I=0;I<Rows.Num();++I) if (Rows[I]!=Manifest.FindRef(FString::Printf(TEXT("existing%d"),I))) {
            Finish(false,TEXT("A pre-existing checkpoint class/GUID/course/order/side/direction or physical wire endpoint changed across save/reload."));return false;
        }
        ExistingGateRows=Rows;bExistingGatesRestored=true;return true;
    }
    FString CheckpointFields() const {
        const bool Geometry=GeometryMatches();
        FString Extra=FString::Printf(TEXT(",\"mixedCheckpointTypesVerified\":%s,\"gateOuterWidthCm\":800,\"gateClearWidthCm\":760,\"gateClearHeightCm\":360,\"polePassSide\":%s,\"geometryAndLabelsVerified\":%s,\"racingMilestonePurchased\":%s,\"gateRecipeAvailable\":%s,\"fullGateRecipeAvailable\":%s,\"poleRecipeAvailable\":%s,\"verticalRecipeAvailable\":%s,\"recipeMigrationTested\":%s,\"recipeReadbackReady\":%s,\"nextTargetVerified\":%s,\"entryOnlyPinkVerified\":%s,\"ordinaryStartPresentationVerified\":%s,\"captureViewRestored\":%s"),
            Geometry ? TEXT("true") : TEXT("false"),bVerticalCourseEnabled ? TEXT("null") : TEXT("-1"),Geometry ? TEXT("true") : TEXT("false"),
            bMilestonePurchased ? TEXT("true") : TEXT("false"),bGateRecipeAvailable ? TEXT("true") : TEXT("false"),
            bFullGateRecipeAvailable ? TEXT("true") : TEXT("false"),bPoleRecipeAvailable ? TEXT("true") : TEXT("false"),bVerticalRecipeAvailable ? TEXT("true") : TEXT("false"),
            bMilestonePurchased && bGateRecipeAvailable && bFullGateRecipeAvailable && bPoleRecipeAvailable && bVerticalRecipeAvailable ? TEXT("true") : TEXT("false"),bRecipeReadbackReady ? TEXT("true") : TEXT("false"),
            bNextTargetVerified ? TEXT("true") : TEXT("false"),bEntryOnlyPinkVerified ? TEXT("true") : TEXT("false"),
            bOrdinaryStartPresentationVerified ? TEXT("true") : TEXT("false"),bCaptureViewRestored ? TEXT("true") : TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"verticalCourseEnabled\":%s,\"verticalDirectionVerified\":%s,\"verticalNextTargetVerified\":%s,\"verticalEntryOnlyPinkVerified\":%s,\"verticalApproachVerified\":%s,\"verticalWrongWayRejected\":%s,\"verticalWrongWayFeedbackVerified\":%s,\"verticalLapScored\":%s,\"verticalFinishFeedbackVerified\":%s,\"verticalSelectedLayoutGuardVerified\":%s,\"verticalResetProtectionVerified\":%s,\"verticalRoutePreviewVerified\":%s,\"verticalOrderSwapVerified\":%s,\"verticalNumberingCompactionVerified\":%s,\"captures\":["),
            bVerticalCourseEnabled ? TEXT("true") : TEXT("false"),bVerticalCourseEnabled && Geometry ? TEXT("true") : TEXT("false"),
            bVerticalNextTargetVerified ? TEXT("true") : TEXT("false"),bVerticalEntryOnlyPinkVerified ? TEXT("true") : TEXT("false"),
            bVerticalApproachVerified ? TEXT("true") : TEXT("false"),bVerticalWrongWayRejected ? TEXT("true") : TEXT("false"),
            bVerticalWrongWayFeedbackVerified ? TEXT("true") : TEXT("false"),
            bVerticalLapScored ? TEXT("true") : TEXT("false"),bVerticalFinishFeedbackVerified ? TEXT("true") : TEXT("false"),
            bVerticalSelectedLayoutGuardVerified ? TEXT("true") : TEXT("false"),bVerticalResetProtectionVerified ? TEXT("true") : TEXT("false"),
            bVerticalRoutePreviewVerified ? TEXT("true") : TEXT("false"),bVerticalOrderSwapVerified ? TEXT("true") : TEXT("false"),
            bVerticalNumberingCompactionVerified ? TEXT("true") : TEXT("false"));
        for (int32 I=0;I<Captures.Num();++I) Extra+=FString::Printf(TEXT("%s{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"lumaVariance\":%.12g,\"decoded\":%s}"),
            I ? TEXT(",") : TEXT(""),*Escape(Captures[I].Path),Captures[I].Width,Captures[I].Height,Captures[I].Variance,Captures[I].bDecoded ? TEXT("true") : TEXT("false"));
        return Extra+FString::Printf(TEXT("],\"existingCheckpointCount\":%d,\"existingCompactGateCount\":%d,\"existingCheckpointGeometryObserved\":%s,\"existingCheckpointPersistenceVerified\":%s,\"startupSplashFinished\":%s"),
            ExistingGateRows.Num(),ExistingCompactGateCount,bExistingGatesObserved && !ExistingGateRows.IsEmpty() ? TEXT("true") : TEXT("false"),
            bExistingGatesRestored && !ExistingGateRows.IsEmpty() ? TEXT("true") : TEXT("false"),bStartupFinished ? TEXT("true") : TEXT("false"));
    }
    void ReleaseCapture() {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshot && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot) {
            FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore;
        }
        bOwnScreenshot=false;
    }
    void RestoreCaptureView() {
        ReleaseCapture();
        if (CaptureRace.Get()) { CaptureRace->StopCourse();CaptureRace.Reset(); }
        auto* PC=CaptureController.Get();auto* Camera=CaptureCamera.Get();
        if (PC && Camera && PC->GetViewTarget()==Camera && OriginalView.IsValid()) PC->SetViewTarget(OriginalView.Get());
        bCaptureViewRestored=PC && OriginalView.IsValid() && PC->GetViewTarget()==OriginalView.Get();
        if (Camera) Camera->Destroy();CaptureCamera.Reset();
    }
    void SweepFixtureGate(UFactoryFPVRaceComponent& Race,int32 Index,bool bForward=true) const {
        const auto Aperture=Gates[Index]->GetApertureTransform();
        const FVector Center=Aperture.GetLocation();
        const FVector Offset=Aperture.GetUnitAxis(EAxis::X)*(bForward ? 150. : -150.);
        const FVector Path[]={Center-Offset,Center+Offset};
        Race.AdvanceFrame(MakeArrayView(Path),.25,10.);
    }
    bool ScoreVerticalLaps() {
        const auto Sweep=[this](int32 Index,bool bForward) { SweepFixtureGate(*CaptureRace.Get(),Index,bForward); };
        const auto WaitAtStart=[this](double Seconds) {
            const FVector Stand=Gates[0]->GetPassageAimPoint();
            CaptureRace->AdvanceFrame(MakeArrayView(&Stand,1),Seconds,10.);
        };
        // Capture 3 already crossed the start at .125 s. Only checkpoint 2
        // should be next after a wrong-way sweep, and 1 after a Descend pass.
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        const double PreviousBest=Gates[0]->GetBestLapSeconds(Layout);
        const double PreviousElapsed=CaptureRace->GetRaceState().elapsed;
        Sweep(1,false);
        const auto& Race=CaptureRace->GetRaceState();
        bVerticalWrongWayRejected=Race.inLap && Race.nextGate==2 && Race.lapCount==0;
        if (!bVerticalWrongWayRejected) return false;
        FFactoryFPVOSDTelemetry Reverse;
        CaptureRace->FillTelemetry(Reverse);
        FVector Aim;int32 Order=0;FString Instruction;
        const bool bWarningKeptTarget=Reverse.bRaceWrongWay && Reverse.RaceStatus==TEXT("WRONG WAY - ENTER FROM ABOVE") &&
            Reverse.RaceGate==2 && Reverse.RaceSplitGate==0 && Reverse.RaceLaps==0 && Reverse.RaceLast==0. && Reverse.RaceBest==PreviousBest &&
            FMath::IsNearlyEqual(Reverse.RaceElapsed,PreviousElapsed+.25) && Gates[0]->GetBestLapSeconds(Layout)==PreviousBest &&
            CaptureRace->GetNextCheckpointTarget(Aim,Order,Instruction) && Order==2 && Instruction==TEXT("DESCEND") &&
            Aim.Equals(Gates[1]->GetPassageAimPoint(),.01) && Gates[1]->IsNextCheckpointHighlighted() && !Gates[0]->IsNextCheckpointHighlighted();
        Sweep(1,true);
        if (!Race.inLap || Race.nextGate!=1 || Race.lapCount!=0) return false;
        FFactoryFPVOSDTelemetry Corrected;
        CaptureRace->FillTelemetry(Corrected);
        const bool bFirstSplit=Corrected.RaceSplitGate==2 && !Corrected.bRaceSplitHasDelta &&
            FMath::IsNearlyEqual(Corrected.RaceSplitSeconds,KnownCheckpointSplit);
        bVerticalWrongWayFeedbackVerified=bWarningKeptTarget && !Corrected.bRaceWrongWay && Corrected.RaceStatus.IsEmpty() &&
            Corrected.RaceGate==1 && Gates[0]->IsNextCheckpointHighlighted() && !Gates[1]->IsNextCheckpointHighlighted();
        if (!bVerticalWrongWayFeedbackVerified) return false;
        WaitAtStart(19.25);
        Sweep(0,true);
        FFactoryFPVOSDTelemetry First;
        CaptureRace->FillTelemetry(First);
        const bool bFirstScored=Race.lapCount==1 && Race.nextGate==2 && FMath::IsNearlyEqual(First.RaceLast,KnownLapSeconds) &&
            FMath::IsNearlyEqual(First.RaceBest,KnownLapSeconds) && FMath::IsNearlyEqual(Gates[0]->GetBestLapSeconds(Layout),KnownLapSeconds);
        // Let the finish notice expire, then cross the same checkpoint later
        // on lap two. Keep the total scripted lap length unchanged.
        WaitAtStart(3.25);
        Sweep(1,true);
        FFactoryFPVOSDTelemetry Comparison;
        CaptureRace->FillTelemetry(Comparison);
        VerifiedSplitDelta=Comparison.RaceSplitDelta;
        const bool bComparison=Comparison.RaceSplitGate==2 && Comparison.bRaceSplitHasDelta &&
            FMath::IsNearlyEqual(Comparison.RaceSplitSeconds,3.5) && FMath::IsNearlyEqual(VerifiedSplitDelta,3.) &&
            Comparison.RaceFinish==EFactoryFPVRaceFinish::None && Comparison.RaceStatus.IsEmpty();
        WaitAtStart(19.25);
        Sweep(0,true);
        FFactoryFPVOSDTelemetry Second;
        CaptureRace->FillTelemetry(Second);
        bVerticalLapScored=bFirstScored && Race.lapCount==2 && Race.nextGate==2 && FMath::IsNearlyEqual(Second.RaceLast,23.) &&
            FMath::IsNearlyEqual(Second.RaceBest,KnownLapSeconds) && FMath::IsNearlyEqual(Gates[0]->GetBestLapSeconds(Layout),KnownLapSeconds);
        bVerticalFinishFeedbackVerified=First.RaceFinish==EFactoryFPVRaceFinish::CourseBest &&
            Second.RaceFinish==EFactoryFPVRaceFinish::LapComplete && Gates[1]->IsNextCheckpointHighlighted();
        const auto Saved=Gates[0]->GetBestLapSplits(Layout,1);
        bVerticalSplitTimingVerified=bFirstSplit && bComparison && !Saved.IsEmpty() &&
            FMath::IsNearlyEqual(Saved[0],KnownCheckpointSplit) && Second.RaceSplitGate==0;
        return bVerticalLapScored && bVerticalFinishFeedbackVerified && bVerticalSplitTimingVerified;
    }
    bool VerifyReloadedSplitReference() {
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        const auto Saved=Gates[0]->GetBestLapSplits(Layout,1);
        if (Saved.IsEmpty() || !FMath::IsNearlyEqual(Saved[0],KnownCheckpointSplit)) return false;
        TStrongObjectPtr<UFactoryFPVRaceComponent> Probe(NewObject<UFactoryFPVRaceComponent>(GetTransientPackage(),NAME_None,RF_Transient));
        FString Error;
        if (!Probe->StartCourse(World.Get(),Course,Error)) return false;
        SweepFixtureGate(*Probe.Get(),0);
        SweepFixtureGate(*Probe.Get(),1);
        FFactoryFPVOSDTelemetry Comparison;
        Probe->FillTelemetry(Comparison);
        Probe->StopCourse();
        VerifiedSplitDelta=Comparison.RaceSplitDelta;
        bVerticalSplitReloadVerified=Comparison.RaceSplitGate==2 && Comparison.bRaceSplitHasDelta &&
            FMath::IsNearlyEqual(Comparison.RaceSplitSeconds,.25) && FMath::IsNearlyEqual(VerifiedSplitDelta,-.25) &&
            FMath::IsNearlyEqual(Comparison.RaceBest,KnownLapSeconds) && Comparison.RaceLaps==0;
        return bVerticalSplitReloadVerified;
    }
    bool VerifyVerticalLayoutRecords(bool bScoreAlternate) {
        auto* Start=Gates[0].Get();
        auto* Vertical=Gates[1].Get();
        using EDirection=EFactoryFPVRaceGateDirection;
        if (!Start || !Vertical || Vertical->GetRaceDirection()!=EDirection::Descend) return false;
        const FString Original=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        const auto HasOriginal=[&] {
            const auto Splits=Start->GetBestLapSplits(Original,1);
            return !Original.IsEmpty() && FMath::IsNearlyEqual(Start->GetBestLapSeconds(Original),KnownLapSeconds) &&
                Splits.Num()==1 && FMath::IsNearlyEqual(Splits[0],KnownCheckpointSplit);
        };
        FString Error;
        if (!HasOriginal() || !Vertical->Configure(Course,2,Vertical->GetPolePassSide(),EDirection::Ascend,Error)) return false;

        const FString Alternate=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        bool bVerified=!Alternate.IsEmpty() && Alternate!=Original;
        if (bVerified && bScoreAlternate) {
            TStrongObjectPtr<UFactoryFPVRaceComponent> Race(NewObject<UFactoryFPVRaceComponent>(GetTransientPackage(),NAME_None,RF_Transient));
            bVerified=Start->GetBestLapSeconds(Alternate)==0. && Race->StartCourse(World.Get(),Course,Error);
            if (bVerified) {
                const FVector Stand=Start->GetPassageAimPoint();
                SweepFixtureGate(*Race.Get(),0);
                Race->AdvanceFrame(MakeArrayView(&Stand,1),1.,10.);
                SweepFixtureGate(*Race.Get(),1);
                Race->AdvanceFrame(MakeArrayView(&Stand,1),11.5,10.);
                SweepFixtureGate(*Race.Get(),0);
                bVerified=Race->GetRaceState().lapCount==1;
            }
            Race->StopCourse();
        }
        const auto AlternateSplits=Start->GetBestLapSplits(Alternate,1);
        VerifiedAlternateBest=Start->GetBestLapSeconds(Alternate);
        VerifiedAlternateSplit=AlternateSplits.Num()==1 ? AlternateSplits[0] : 0.;
        const bool bRestored=Vertical->Configure(Course,2,Vertical->GetPolePassSide(),EDirection::Descend,Error);
        bVerticalLayoutRecordsVerified=bVerified && bRestored && UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course)==Original &&
            HasOriginal() && FMath::IsNearlyEqual(VerifiedAlternateBest,AlternateLapSeconds) &&
            FMath::IsNearlyEqual(VerifiedAlternateSplit,AlternateCheckpointSplit);
        return bVerticalLayoutRecordsVerified;
    }
    bool CheckSelectedVerticalLayout(APlayerController& PC,const FString& Layout) {
        auto* Local=PC.GetLocalPlayer();
        auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        auto* Current=World.Get();
        auto* Viewport=Current->GetGameViewport();
        if (!Hangar || !Viewport || Hangar->IsPanelOpen() || Hangar->GetSelectedRacingCourse()!=0 ||
            AFactoryFPVDrone::FindActive(Current) || AFactoryFPVPioneerAnchor::FindPending(Current,Pioneer.Get())) {
            LastReason=TEXT("Vertical layout guard requires normal pioneer ownership and no existing course selection.");return false;
        }
        auto* View=PC.GetViewTarget();
        const auto Pose=Pioneer->GetActorTransform();
        const auto Before=FactoryFPVInputLease::GetDiagnostics(&PC);
        const bool bMove=PC.IsMoveInputIgnored(),bLook=PC.IsLookInputIgnored();
        const bool bCursor=PC.bShowMouseCursor,bViewportIgnored=Viewport->IgnoreInput();
        const auto Preserved=[&] {
            const auto After=FactoryFPVInputLease::GetDiagnostics(&PC);
            return PC.GetPawn()==Pioneer.Get() && PC.GetViewTarget()==View && Pioneer->GetActorTransform().Equals(Pose,.01) &&
                PC.IsMoveInputIgnored()==bMove && PC.IsLookInputIgnored()==bLook && PC.bShowMouseCursor==bCursor &&
                Viewport->IgnoreInput()==bViewportIgnored && !Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(Current) &&
                After.ActiveLeases==Before.ActiveLeases && After.OwnedMoveContributions==Before.OwnedMoveContributions &&
                After.OwnedLookContributions==Before.OwnedLookContributions && After.ResetDepth==Before.ResetDepth;
        };
        FString Error;
        auto* Second=Gates[1].Get();
        Hangar->SetSelectedRacingCourse(Course);
        if (Hangar->GetSelectedRacingCourse()!=Course || Hangar->GetSelectedRacingLayout()!=Layout ||
            !Second->Configure(Course,2,Second->GetPolePassSide(),EFactoryFPVRaceGateDirection::Ascend,Error)) {
            Hangar->SetSelectedRacingCourse(0);
            LastReason=TEXT("Could not pin the live Descend layout and change the owned checkpoint: ")+Error;return false;
        }
        const bool bChangedReady=UFactoryFPVRaceComponent::ValidateCourse(Current,Course,Error,true) &&
            UFactoryFPVRaceComponent::LayoutSignature(Current,Course)!=Layout;
        auto* Refused=bChangedReady ? AFactoryFPVDrone::Launch(&PC,TEXT("keyboard"),Error) : nullptr;
        const bool bRefusalPreserved=bChangedReady && !Refused && Error.Contains(TEXT("checkpoint layout changed")) &&
            Hangar->GetSelectedRacingCourse()==Course && Hangar->GetSelectedRacingLayout()==Layout && Preserved();
        if (Refused) Refused->ExitDrone();
        const FString LaunchError=Error;
        const bool bRestored=Second->Configure(Course,2,Second->GetPolePassSide(),EFactoryFPVRaceGateDirection::Descend,Error) &&
            UFactoryFPVRaceComponent::LayoutSignature(Current,Course)==Layout;
        Hangar->SetSelectedRacingCourse(0);
        if (!bRefusalPreserved || !bRestored || Hangar->GetSelectedRacingCourse()!=0 || !Hangar->GetSelectedRacingLayout().IsEmpty()) {
            LastReason=TEXT("Native launch did not refuse only the changed, otherwise-ready layout or preserve pioneer ownership: ")+LaunchError;return false;
        }
        auto* FreeFlight=AFactoryFPVDrone::Launch(&PC,TEXT("keyboard"),Error);
        const bool bFreeFlight=FreeFlight && !FreeFlight->IsFlightArmed() && PC.GetViewTarget()==FreeFlight &&
            AFactoryFPVDrone::FindActive(Current)==FreeFlight && !Gates[0]->IsNextCheckpointHighlighted() && !Second->IsNextCheckpointHighlighted();
        if (FreeFlight) FreeFlight->ExitDrone();
        bVerticalSelectedLayoutGuardVerified=bFreeFlight && Preserved();
        if (!bVerticalSelectedLayoutGuardVerified) LastReason=TEXT("Could not start and return from an ordinary free flight after clearing the course selection: ")+Error;
        return bVerticalSelectedLayoutGuardVerified;
    }
    bool CheckVerticalResetProtection(const FString& Layout) {
        auto* First=Gates[0].Get();auto* Second=Gates[1].Get();
        const auto* PC=World->GetFirstPlayerController();
        const auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        if (!Hangar || Hangar->GetSelectedRacingCourse()!=0) {
            LastReason=TEXT("Vertical clearance QA requires the real local race-selection owner with no existing selection.");return false;
        }
        auto Panel=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(Second).Embedded(true);
        const auto FindWidget=[&Panel](FName Tag) { return FindTaggedSlateWidget(Panel,Tag); };
        const auto FindButton=[&](const TCHAR* Tag) -> TSharedPtr<SButton> {
            const auto Widget=FindWidget(FName(Tag));
            return Widget && Widget->GetTypeAsString()==TEXT("SButton") ? StaticCastSharedPtr<SButton>(Widget) : nullptr;
        };
        const auto Visible=[&Panel](const TSharedPtr<SWidget>& Widget) { return SlateVisibleFrom(Panel,Widget); };
        const FKey Accept=SlateAcceptKey();
        if (!Accept.IsValid()) {
            LastReason=TEXT("The vertical race probe requires a live navigation Accept key.");return false;
        }
        Panel->SlatePrepass();
        const FKeyEvent AcceptEvent(Accept,FModifierKeysState(),0,false,0,0);
        const auto Press=[&](const TSharedPtr<SButton>& Button) {
            if (!Visible(Button) || !Button->IsEnabled()) return false;
            const bool bPressed=PressSlateButton(*Button,AcceptEvent);
            Panel->SlatePrepass();
            return bPressed;
        };
        {
            const auto Refresh=FindButton(TEXT("FactoryFPVRaceRefresh")),Race=FindButton(TEXT("FactoryFPVRaceSelectCourse"));
            const auto CourseClearance=FindWidget(FName(TEXT("FactoryFPVRaceCourseClearance")));
            const auto EditorClearance=FindWidget(FName(TEXT("FactoryFPVRaceCheckpointClearance")));
            const double Radius=GetDefault<UFactoryFPVSettings>()->FrameSizeCm*.5;
            if (!Refresh || !Race || !CourseClearance || !EditorClearance ||
                CourseClearance->GetTypeAsString()!=TEXT("STextBlock") || EditorClearance->GetTypeAsString()!=TEXT("STextBlock") ||
                Second->ProbeVerticalClearance(Radius).HasObstruction() || Visible(CourseClearance) || Visible(EditorClearance) ||
                !Visible(Race) || !Race->IsEnabled()) {
                LastReason=TEXT("The unblocked vertical checkpoint did not start with both advisories hidden and the real race selection enabled.");return false;
            }
            FActorSpawnParameters Spawn;
            Spawn.OverrideLevel=World->PersistentLevel;
            Spawn.ObjectFlags|=RF_Transient;
            Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Roof=World->SpawnActor<AActor>(Spawn);
            if (!Roof) { LastReason=TEXT("Could not spawn the disposable vertical clearance roof.");return false; }
            ON_SCOPE_EXIT { if (IsValid(Roof) && !Roof->IsActorBeingDestroyed()) Roof->Destroy(); };
            auto* Box=NewObject<UBoxComponent>(Roof);
            Roof->SetRootComponent(Box);
            Roof->AddInstanceComponent(Box);
            Box->SetBoxExtent(FVector(70.,70.,10.));
            Box->SetWorldLocation(Second->GetApertureTransform().GetLocation()+FVector(0.,0.,110.));
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            Box->RegisterComponent();
            const auto Blocked=Second->ProbeVerticalClearance(Radius);
            const bool bAdvised=Blocked.bAboveBlocked && !Blocked.bOpeningBlocked && !Blocked.bBelowBlocked &&
                Press(Refresh) && Visible(CourseClearance) && Visible(EditorClearance) && Race->IsEnabled() &&
                StaticCastSharedPtr<STextBlock>(CourseClearance)->GetText().ToString()==
                    TEXT("Checkpoint 2 center line is blocked above the opening. An offset path may still work.") &&
                StaticCastSharedPtr<STextBlock>(EditorClearance)->GetText().ToString()==
                    TEXT("Center line blocked above the opening. An offset path may still work.");
            Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
            const bool bDestroyed=Roof->Destroy();
            bVerticalClearanceAdvisoryVerified=bAdvised && bDestroyed && !Second->ProbeVerticalClearance(Radius).HasObstruction() &&
                Press(Refresh) && !Visible(CourseClearance) && !Visible(EditorClearance) && Race->IsEnabled() &&
                UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course)==Layout;
            if (!bVerticalClearanceAdvisoryVerified) {
                LastReason=TEXT("The transient roof did not produce and clear the actual course and editor advisories while preserving race selection and layout.");return false;
            }
        }
        Panel=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(First).Embedded(true);
        const auto Reset=FindButton(TEXT("FactoryFPVRaceResetBest")),Confirm=FindButton(TEXT("FactoryFPVRaceConfirmBestReset")),
            Cancel=FindButton(TEXT("FactoryFPVRaceCancelBestReset"));
        if (!Reset || !Confirm || !Cancel) {
            LastReason=TEXT("The race reset probe requires all three real Slate buttons.");return false;
        }
        Panel->SlatePrepass();
        const auto BestKept=[&] { return FMath::IsNearlyEqual(First->GetBestLapSeconds(Layout),KnownLapSeconds); };
        const FName ChartTag(TEXT("FactoryFPVRaceRouteChart"));
        const bool bOpenedPreview=First->IsGatePowered() && Second->IsGatePowered() && BestKept() && !FindWidget(ChartTag) &&
            Press(FindButton(TEXT("FactoryFPVRaceRoute")));
        const auto Chart=bOpenedPreview ? FindWidget(ChartTag) : nullptr;
        const auto Preview=Chart && Chart->GetTypeAsString()==TEXT("SFactoryFPVRaceRoutePreview") ?
            StaticCastSharedPtr<SFactoryFPVRaceRoutePreview>(Chart) : nullptr;
        bVerticalRoutePreviewVerified=Preview && Visible(Preview) && !Preview->IsElevation() &&
            Press(FindButton(TEXT("FactoryFPVRaceRouteElevation"))) && Preview->IsElevation() && Visible(Preview) &&
            Press(FindButton(TEXT("FactoryFPVRaceRoutePlan"))) && !Preview->IsElevation() && Visible(Preview) &&
            Press(FindButton(TEXT("FactoryFPVRaceCheckpoints"))) && !FindWidget(ChartTag) && BestKept();
        if (!bVerticalRoutePreviewVerified) {
            LastReason=TEXT("Actual route controls did not show both projections and return to the checkpoint list while preserving the scored record.");
            return false;
        }
        const auto ApplyGate=FindButton(TEXT("FactoryFPVRaceApplyGate")),Swap=FindButton(TEXT("FactoryFPVRaceConfirmSwap")),
            CancelSwap=FindButton(TEXT("FactoryFPVRaceCancelSwap"));
        const auto OrderWidget=FindWidget(FName(TEXT("FactoryFPVRaceDraftOrder")));
        const auto BestWidget=FindWidget(FName(TEXT("FactoryFPVRaceBestLap")));
        if (!ApplyGate || !Swap || !CancelSwap || !OrderWidget || OrderWidget->GetTypeAsString()!=TEXT("SSpinBox<int32>") ||
            !BestWidget || BestWidget->GetTypeAsString()!=TEXT("STextBlock")) {
            LastReason=TEXT("The race order probe requires the actual order spin box, swap controls, and best time readout.");return false;
        }
        const auto Order=StaticCastSharedPtr<SSpinBox<int32>>(OrderWidget);
        const auto Best=StaticCastSharedPtr<STextBlock>(BestWidget);
        const FString RecordedBest=FString::Printf(TEXT("Course best: %.3f s"),KnownLapSeconds);
        const auto ChangeOrder=[&](const FKey& Key,int32 Expected) {
            const FKeyEvent Event(Key,FModifierKeysState(),0,false,0,0);
            const bool bHandled=Order->OnKeyDown(Order->GetCachedGeometry(),Event).IsEventHandled();
            Panel->SlatePrepass();
            return bHandled && Order->GetValue()==Expected;
        };
        const auto OrdersMatch=[&](int32 FirstOrder,int32 SecondOrder) {
            return First->GetCourseId()==Course && First->GetGateOrder()==FirstOrder &&
                Second->GetCourseId()==Course && Second->GetGateOrder()==SecondOrder;
        };
        const auto OriginalLayoutKept=[&] {
            return OrdersMatch(1,2) && UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course)==Layout && BestKept() &&
                Visible(Best) && Best->GetText().ToString()==RecordedBest;
        };
        if (!OriginalLayoutKept() || Visible(Swap) || !ChangeOrder(EKeys::Up,2) || !Press(ApplyGate) ||
            !Visible(Swap) || !Visible(CancelSwap) || !OriginalLayoutKept() || !Press(CancelSwap) ||
            Visible(Swap) || Visible(CancelSwap) || Order->GetValue()!=2 || !OriginalLayoutKept()) {
            LastReason=TEXT("Actual order controls failed to stage and cancel a swap while keeping both checkpoints and the scored best unchanged.");return false;
        }
        if (!Press(ApplyGate) || !Press(Swap) || Visible(Swap) || !OrdersMatch(2,1)) {
            LastReason=TEXT("The confirmed swap did not exchange both checkpoint numbers.");return false;
        }
        FString Error;
        const FString SwappedLayout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        if (SwappedLayout.IsEmpty() || SwappedLayout==Layout || !UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true) ||
            Second->GetBestLapSeconds(SwappedLayout)!=0. || !Visible(Best) ||
            Best->GetText().ToString()!=TEXT("Course best: no completed lap on this layout")) {
            LastReason=TEXT("The swapped checkpoints did not form a distinct powered course with a fresh best time: ")+Error;return false;
        }
        bVerticalOrderSwapVerified=ChangeOrder(EKeys::Down,1) && Press(ApplyGate) && Visible(Swap) && OrdersMatch(2,1) &&
            Press(Swap) && !Visible(Swap) && Order->GetValue()==1 && OriginalLayoutKept() &&
            UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true);
        if (!bVerticalOrderSwapVerified) {
            LastReason=TEXT("The inverse swap did not restore the exact powered layout and original scored best: ")+Error;return false;
        }
        {
            const FGuid FirstId=First->GetGateId(),SecondId=Second->GetGateId();
            const auto SecondDirection=Second->GetRaceDirection();
            const auto CheckpointsKept=[&] {
                const auto Splits=First->GetBestLapSplits(Layout,1);
                return First->GetGateId()==FirstId && Second->GetGateId()==SecondId && Second->GetRaceDirection()==SecondDirection &&
                    OwnedWireMatches() && OwnedWireMatches(1) && First->IsGatePowered() && Second->IsGatePowered() &&
                    Hangar->GetSelectedRacingCourse()==0 && Hangar->GetSelectedRacingLayout().IsEmpty() &&
                    BestKept() && Splits.Num()==1 && FMath::IsNearlyEqual(Splits[0],KnownCheckpointSplit);
            };
            const auto VisibleText=[&](const TCHAR* Tag) -> FString {
                const auto Widget=FindWidget(FName(Tag));
                return Visible(Widget) && Widget->GetTypeAsString()==TEXT("STextBlock") ? StaticCastSharedPtr<STextBlock>(Widget)->GetText().ToString() : FString();
            };
            const auto CompactionStaged=[&] {
                return Visible(FindButton(TEXT("FactoryFPVRaceConfirmCloseGaps"))) && Visible(FindButton(TEXT("FactoryFPVRaceCancelCloseGaps"))) &&
                    VisibleText(TEXT("FactoryFPVRaceCloseGapsPreview"))==TEXT("3 \u2192 2") &&
                    VisibleText(TEXT("FactoryFPVRaceCloseGapsWarning"))==TEXT("Loaded checkpoints only. Move near missing checkpoints first.");
            };
            const auto CompactionClosed=[&] {
                return !Visible(FindButton(TEXT("FactoryFPVRaceConfirmCloseGaps"))) && !Visible(FindButton(TEXT("FactoryFPVRaceCancelCloseGaps")));
            };
            const auto StageCompaction=[&] {
                return Press(FindButton(TEXT("FactoryFPVRaceCloseGaps"))) && CompactionStaged() && OrdersMatch(1,3) && CheckpointsKept();
            };
            if (!CheckpointsKept() || !Second->Configure(Course,3,Second->GetPolePassSide(),SecondDirection,Error) ||
                UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true) ||
                !Press(FindButton(TEXT("FactoryFPVRaceRefresh"))) || !CompactionClosed() || !StageCompaction() ||
                !Press(FindButton(TEXT("FactoryFPVRaceCancelCloseGaps"))) || !CompactionClosed() || !OrdersMatch(1,3) || !CheckpointsKept()) {
                LastReason=TEXT("Actual race gap controls did not preview and cancel 3 to 2 while preserving both wired checkpoints and the scored record: ")+Error;
                return false;
            }
            bVerticalNumberingCompactionVerified=StageCompaction() && Press(FindButton(TEXT("FactoryFPVRaceConfirmCloseGaps"))) &&
                CompactionClosed() && OriginalLayoutKept() && CheckpointsKept() &&
                UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true);
            if (!bVerticalNumberingCompactionVerified) {
                LastReason=TEXT("The confirmed gap repair did not restore the exact powered layout, checkpoint identities, record and split: ")+Error;
                return false;
            }
        }
        if (!BestKept() || Visible(Confirm) || Visible(Cancel) || !Press(Reset) || !Visible(Confirm) || !Visible(Cancel) ||
            !BestKept() || !Press(Cancel) || Visible(Confirm) || Visible(Cancel) || !BestKept() || !Press(Reset) || !Visible(Confirm)) {
            LastReason=TEXT("Actual race record controls did not stage and cancel confirmation while keeping the scored record.");return false;
        }
        if (!Second->Configure(Course,2,Second->GetPolePassSide(),EFactoryFPVRaceGateDirection::Ascend,Error)) {
            LastReason=TEXT("Could not change the owned checkpoint while reset confirmation was pending: ")+Error;return false;
        }
        const bool bChanged=UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,false) &&
            UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course)!=Layout;
        const bool bRejected=bChanged && Press(Confirm) && !Visible(Confirm) && Visible(Reset) && !Reset->IsEnabled() && BestKept();
        const bool bRestored=Second->Configure(Course,2,Second->GetPolePassSide(),EFactoryFPVRaceGateDirection::Descend,Error) &&
            UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course)==Layout;
        bVerticalResetProtectionVerified=bRejected && bRestored && BestKept();
        if (!bVerticalResetProtectionVerified) LastReason=TEXT("A stale confirmation did not preserve the scored record after restoring the original layout: ")+Error;
        return bVerticalResetProtectionVerified;
    }
    bool CaptureReady() {
        if (bVerify) return true;
        const TCHAR* Names[]={TEXT("Gate-Front.png"),TEXT("Gate-Back.png"),TEXT("Gate-Oblique.png"),
            bVerticalCourseEnabled ? TEXT("Vertical-Above.png") : TEXT("Pole-Left.png"),TEXT("Vertical-Below.png")};
        const int32 CaptureCount=bVerticalCourseEnabled ? 5 : 4;
        const double Now=FPlatformTime::Seconds();
        auto* PC=World.IsValid() ? World->GetFirstPlayerController() : nullptr;
        if (!PC || PC->GetPawn()!=Pioneer.Get()) { Finish(false,TEXT("Pioneer/controller changed during gate capture."));return false; }
        const auto CaptureComplete=[&] {
            if (bVerticalCourseEnabled && AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get())) {
                LastReason=TEXT("Waiting for the free-flight probe to release the pioneer on original support.");return false;
            }
            return bCaptureViewRestored;
        };
        if (CaptureIndex==CaptureCount) return CaptureComplete();
        if (!CaptureCamera.IsValid()) {
            if (!CheckpointPresentationMatches(0,false) || (bVerticalCourseEnabled && !CheckpointPresentationMatches(1,false))) {
                LastReason=TEXT("Waiting for ordinary powered orange strips and labels before course selection.");return false;
            }
            OriginalView=PC->GetViewTarget();CaptureController=PC;
            if (!OriginalView.IsValid() || FScreenshotRequest::IsScreenshotRequested()) { Finish(false,TEXT("Gate capture requires an existing native view and no unrelated screenshot request."));return false; }
            FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
            auto* Camera=World->SpawnActor<ACameraActor>(Params);
            if (!Camera || !Camera->GetCameraComponent()) { Finish(false,TEXT("Transient viewport camera failed to initialize."));return false; }
            CaptureCamera=Camera;Camera->GetCameraComponent()->SetFieldOfView(70.f);
            CaptureRace.Reset(NewObject<UFactoryFPVRaceComponent>(GetTransientPackage(),NAME_None,RF_Transient));
            FString Error;FVector Aim;int32 Order=0;FString Instruction;
            bNextTargetVerified=CaptureRace->StartCourse(World.Get(),Course,Error) && CaptureRace->GetNextCheckpointTarget(Aim,Order,Instruction) &&
                Order==1 && Aim.Equals(Gates[0]->GetPassageAimPoint(),.01) && Gates[0]->IsNextCheckpointHighlighted() && !Gates[1]->IsNextCheckpointHighlighted();
            if (!bNextTargetVerified) { Finish(false,TEXT("Real stationary course selection did not highlight and identify checkpoint1: ")+Error);return false; }
            bEntryOnlyPinkVerified=CheckpointPresentationMatches(0,true);
            if (!bEntryOnlyPinkVerified) { Finish(false,TEXT("Selected start gate did not keep pink on its four entry strips only, with ordinary reverse strips and unchanged labels."));return false; }
            Captures.SetNum(CaptureCount);PC->SetViewTarget(Camera);
        }
        if (PC->GetViewTarget()!=CaptureCamera.Get()) { Finish(false,TEXT("Another native owner replaced the gate capture camera."));return false; }
        auto& Capture=Captures[CaptureIndex];
        if (CaptureSettleSince==0) {
            if (bVerticalCourseEnabled && CaptureIndex==3) {
                // Feed a single analytic gate1 crossing to the transient race
                // component. It advances the cue without flying or recording a lap.
                const FTransform Start=Gates[0]->GetApertureTransform();
                const FVector Center=Start.GetLocation(),Normal=Start.GetUnitAxis(EAxis::X);
                const TArray<FVector> Path{Center-Normal*150.,Center+Normal*150.};
                CaptureRace->AdvanceFrame(MakeArrayView(Path),.25,10.);
                FVector Aim;int32 Order=0;FString Instruction;
                bVerticalNextTargetVerified=CaptureRace->GetNextCheckpointTarget(Aim,Order,Instruction) && Order==2 && Instruction==TEXT("DESCEND") &&
                    Aim.Equals(Gates[1]->GetPassageAimPoint(),.01) && Gates[1]->IsNextCheckpointHighlighted() && CheckpointPresentationMatches(0,false);
                bVerticalEntryOnlyPinkVerified=CheckpointPresentationMatches(1,true);
                if (!bVerticalNextTargetVerified || !bVerticalEntryOnlyPinkVerified) {
                    Finish(false,TEXT("API start crossing did not target the Descend gate or confine pink to its four upper entry strips."),ResultFields());return false;
                }
                const FVector Passage=Gates[1]->GetPassageAimPoint();
                const auto RaceBeforeApproach=CaptureRace->GetRaceState();
                bool bOnExitSide=false;
                const bool bEntryApproach=CaptureRace->GetNextCheckpointApproach(Passage+FVector(0,0,500.),10.,Aim,Order,Instruction,bOnExitSide) &&
                    !bOnExitSide && Order==2 && Instruction==TEXT("DESCEND") && Aim.Equals(Passage,.01);
                const bool bExitApproach=CaptureRace->GetNextCheckpointApproach(Passage-FVector(0,0,500.),10.,Aim,Order,Instruction,bOnExitSide) &&
                    bOnExitSide && Order==2 && Instruction==TEXT("ENTER FROM ABOVE") && Aim.Equals(Passage,.01);
                const auto& RaceAfterApproach=CaptureRace->GetRaceState();
                bVerticalApproachVerified=bEntryApproach && bExitApproach && RaceBeforeApproach.nextGate==RaceAfterApproach.nextGate &&
                    RaceBeforeApproach.inLap==RaceAfterApproach.inLap && RaceBeforeApproach.lapCount==RaceAfterApproach.lapCount &&
                    RaceBeforeApproach.elapsed==RaceAfterApproach.elapsed;
                if (!bVerticalApproachVerified) {
                    Finish(false,TEXT("Native Descend gate approach did not keep the passage target and warn a pilot below to enter from above without advancing the race."),ResultFields());return false;
                }
            }
            auto* Subject=Gates[CaptureIndex>=3 ? 1 : 0].Get();
            const auto Transform=Subject->GetActorTransform();
            // Offset the rear camera so checkpoint2 cannot occlude gate1's
            // reverse sign. Decoding a pole image cannot qualify a gate view.
            const FVector Local=CaptureIndex==0 ? FVector(-1250,0,420) : CaptureIndex==1 ? FVector(1250,-650,420) : CaptureIndex==2 ? FVector(-1150,700,550) :
                !bVerticalCourseEnabled ? FVector(-1000,-650,420) : CaptureIndex==3 ? FVector(520,-180,1030) : FVector(650,-180,65);
            const FVector Location=Transform.TransformPosition(Local),Target=Transform.TransformPosition(FVector(0,0,Subject->IsVerticalRaceGate() ? 260 : 300));
            CaptureCamera->SetActorLocationAndRotation(Location,(Target-Location).Rotation());
            CaptureSettleSince=Now;return false;
        }
        if (Now-CaptureSettleSince<.75) return false;
        if (Capture.Path.IsEmpty()) {
            auto* Viewport=World->GetGameViewport();
            if (FScreenshotRequest::IsScreenshotRequested() || !Viewport || !Viewport->Viewport || Viewport->Viewport->GetSizeXY().X<320 || Viewport->Viewport->GetSizeXY().Y<180) {
                Finish(false,TEXT("Gate viewport capture is unavailable or another screenshot is pending."));return false;
            }
            const FString Saved=NormalizedPath(FPaths::ProjectSavedDir()),Directory=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));
            const FString Path=FPaths::Combine(Directory,Names[CaptureIndex]);
            if (!DisposablePath(Saved) || !FPaths::IsUnderDirectory(Directory,Saved) || !FPaths::IsUnderDirectory(Path,Directory) ||
                IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Directory,true)) {
                Finish(false,TEXT("Gate captures require fresh guarded disposable Saved paths."));return false;
            }
            ReleaseCapture();Capture.Path=Path;PendingScreenshot=Path;CaptureStarted=Now;
            bScreenshotProcessed=false;bOwnScreenshot=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
            const TWeakPtr<FRun> Weak=AsShared();
            ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak] {
                if (const auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshot) { Run->bScreenshotProcessed=true;Run->bOwnScreenshot=false; }
            });
            FScreenshotRequest::RequestScreenshot(Path,false,false,false);
            LastReason=TEXT("Waiting for actual mixed-checkpoint viewport PNG: ")+Path;return false;
        }
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Gate viewport screenshot exceeded its five-second deadline."));return false; }
        if (!bScreenshotProcessed) return false;
        ReleaseCapture();FImage Image;TArray64<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) ||
            Image.SizeX<320 || Image.SizeY<180 || Image.NumSlices!=1 || Image.RawData.IsEmpty()) {
            Finish(false,TEXT("Gate viewport PNG did not decode to a sane image."));return false;
        }
        double Sum=0,Squared=0;int32 Count=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60)) {
            const auto Pixel=Image.GetOnePixelLinear(X,Y);
            if (!FMath::IsFinite(Pixel.R) || !FMath::IsFinite(Pixel.G) || !FMath::IsFinite(Pixel.B)) { Finish(false,TEXT("Gate PNG contains nonfinite pixels."));return false; }
            const double Luma=.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B;Sum+=Luma;Squared+=Luma*Luma;++Count;
        }
        Capture.Variance=Count ? FMath::Max(0.,Squared/Count-FMath::Square(Sum/Count)) : 0.;
        if (Capture.Variance<1e-8) { Finish(false,TEXT("Gate viewport PNG is blank/uniform."));return false; }
        Capture.Width=Image.SizeX;Capture.Height=Image.SizeY;Capture.bDecoded=true;++CaptureIndex;CaptureSettleSince=0;
        if (CaptureIndex==CaptureCount) {
            if (bVerticalCourseEnabled && !ScoreVerticalLaps()) {
                Finish(false,TEXT("Native vertical course did not verify reverse-pass recovery, exact checkpoint splits and both timed lap finishes."),ResultFields());return false;
            }
            RestoreCaptureView();
            if (bVerticalCourseEnabled && bCaptureViewRestored) {
                const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
                if (Layout.IsEmpty()) LastReason=TEXT("Scored vertical layout is unavailable after restoring the capture view.");
                if (Layout.IsEmpty() || !CheckSelectedVerticalLayout(*PC,Layout) || !CheckVerticalResetProtection(Layout)) {
                    Finish(false,LastReason,ResultFields());return false;
                }
                if (!VerifyVerticalLayoutRecords(true)) {
                    Finish(false,TEXT("A scored Ascend lap did not preserve the original Descend record and both split times."),ResultFields());return false;
                }
            }
            bOrdinaryStartPresentationVerified=CheckpointPresentationMatches(0,false);
            if (!bCaptureViewRestored || !bOrdinaryStartPresentationVerified || Gates[1]->IsNextCheckpointHighlighted() ||
                (bVerticalCourseEnabled && !CheckpointPresentationMatches(1,false))) {
                Finish(false,TEXT("Capture camera or stationary NEXT highlight was not released before native save."));return false;
            }
            return CaptureComplete();
        }
        return false;
    }

    FString EffectFields() const {
        auto* Buildables=World.IsValid() ? AFGBuildableSubsystem::Get(World.Get()) : nullptr;
        const auto* Sparse=GetDefault<AFactoryFPVRaceGate>()->GetBuildableSparseData();
        FString Extra=FString::Printf(TEXT(",\"constructionEffectsEnabled\":%s,\"constructionEffectsObserved\":%s,\"constructionEffectsCompleted\":%s,\"defaultBuildEffectTemplate\":\"%s\",\"gateBuildEffectTemplate\":\"%s\",\"constructionEffectStates\":["),
            bConstructionEffectsEnabled ? TEXT("true") : TEXT("false"),ConstructionEffectsObserved[0] && ConstructionEffectsObserved[1] ? TEXT("true") : TEXT("false"),
            bConstructionEffectsCompleted ? TEXT("true") : TEXT("false"),*Escape(GetPathNameSafe(Buildables ? Buildables->GetDefaultBuildEffectTemplate().Get() : nullptr)),
            *Escape(GetPathNameSafe(Sparse ? Sparse->mSparseBuildEffectActorTemplate.Get() : nullptr)));
        for (int32 Index=0;Index<Gates.Num();++Index) {
            const auto* Gate=Gates[Index].Get();
            Extra+=FString::Printf(TEXT("%s{\"gate\":%d,\"valid\":%s,\"observed\":%s,\"playing\":%s,\"hidden\":%s,\"effectActor\":\"%s\"}"),
                Index ? TEXT(",") : TEXT(""),Index+1,Gate ? TEXT("true") : TEXT("false"),Index<2 && ConstructionEffectsObserved[Index] ? TEXT("true") : TEXT("false"),
                Gate && Gate->IsPlayingBuildEffect() ? TEXT("true") : TEXT("false"),Gate && Gate->IsHidden() ? TEXT("true") : TEXT("false"),*Escape(GetPathNameSafe(Gate ? Gate->GetBuildEffectActor() : nullptr)));
        }
        return Extra+TEXT("]")+PowerCycleFields();
    }
    FString PowerCycleFields() const {
        const bool Verified=bVerify ? Manifest.FindRef(TEXT("powerCycleVerified"))==TEXT("1") : PowerCyclePhase==3;
        return FString::Printf(TEXT(",\"powerCycleEnabled\":%s,\"powerCycleDisconnected\":%s,\"powerLossObserved\":%s,\"powerCycleReconnected\":%s,\"powerCycleRestored\":%s,\"powerCycleCourseFaultObserved\":%s,\"powerCycleCourseRecovered\":%s,\"powerCycleGate2StayedPowered\":%s,\"powerCycleVerifiedBeforeSave\":%s,\"powerCycleRecheckOnly\":%s,\"powerCycleScope\":\"owned wire and stationary readiness/fault only; excludes physical in-lap cancellation and perceived lighting\",\"powerCycleEvidence\":{\"baseline\":%s,\"lost\":%s,\"restored\":%s,\"current\":%s}"),
            bPowerCycleEnabled ? TEXT("true") : TEXT("false"),bPowerCycleDisconnected ? TEXT("true") : TEXT("false"),bPowerLossObserved ? TEXT("true") : TEXT("false"),
            bPowerCycleReconnected ? TEXT("true") : TEXT("false"),bPowerCycleRestored ? TEXT("true") : TEXT("false"),bPowerCycleCourseFaultObserved ? TEXT("true") : TEXT("false"),
            bPowerCycleCourseRecovered ? TEXT("true") : TEXT("false"),bPowerCycleGate2StayedPowered ? TEXT("true") : TEXT("false"),Verified ? TEXT("true") : TEXT("false"),
            bVerify && bPowerCycleEnabled ? TEXT("true") : TEXT("false"),*PowerCycleBaseline,*PowerCycleLost,*PowerCycleRestored,*(bPowerCycleEnabled ? PowerSnapshot() : FString(TEXT("null"))));
    }
    FString PowerSnapshot() const {
        const AFGBuildableWire* Wire=Wires.Num()>0 ? Wires[0].Get() : nullptr;
        if (!Wire && bVerify && Gates.Num()>0 && Gates[0].IsValid()) {
            TArray<AFGBuildableWire*> LoadedLinks;Gates[0]->GetPowerConnection()->GetWires(LoadedLinks);
            if (LoadedLinks.Num()==1) Wire=LoadedLinks[0];
        }
        FString Json=FString::Printf(TEXT("{\"ownedWire\":\"%s\",\"endpoint0\":\"%s\",\"endpoint1\":\"%s\",\"gates\":["),
            *Escape(GetPathNameSafe(Wire)),*Escape(GetPathNameSafe(Wire ? Wire->GetConnection(0) : nullptr)),*Escape(GetPathNameSafe(Wire ? Wire->GetConnection(1) : nullptr)));
        for (int32 Index=0;Index<Gates.Num();++Index) {
            auto* Gate=Gates[Index].Get();const auto* Info=Gate ? Gate->GetPowerInfo() : nullptr;
            Json+=FString::Printf(TEXT("%s{\"order\":%d,\"powered\":%s,\"actualMW\":%s,\"targetMW\":%s,\"lighting\":%s}"),Index ? TEXT(",") : TEXT(""),
                Gate ? Gate->GetGateOrder() : 0,Gate && Gate->IsGatePowered() ? TEXT("true") : TEXT("false"),
                *(Info ? FiniteJson(Info->GetActualConsumption()) : FString(TEXT("null"))),*(Info ? FiniteJson(Info->GetTargetConsumption()) : FString(TEXT("null"))),*ReadGateLighting(Gate).Json());
        }
        return Json+TEXT("]}");
    }
    bool OwnedWireMatches(int32 Index=0) const {
        const auto* Wire=Wires.Num()==2 && Wires.IsValidIndex(Index) ? Wires[Index].Get() : nullptr;
        const auto* Gate=Gates.Num()==2 && Gates.IsValidIndex(Index) ? Gates[Index].Get() : nullptr;
        const auto* Connection=Gate ? Gate->GetPowerConnection() : nullptr;
        return Wire && Connection && Source.IsValid() &&
            ((Wire->GetConnection(0)==Source.Get() && Wire->GetConnection(1)==Connection) ||
             (Wire->GetConnection(1)==Source.Get() && Wire->GetConnection(0)==Connection));
    }
    void RefreshPowerRace() {
        // A stationary zero-duration observation refreshes actual live power
        // readiness through the same non-scoring and disarm calls as a parked
        // drone. No crossing, clock time or lap is used.
        const TArray<FVector> Still{Gates[0]->GetApertureTransform().GetLocation()};
        const uint8 NoScoring[]{0};
        PowerCycleRace->AdvanceFrame(MakeArrayView(Still),0.,0.,MakeArrayView(NoScoring));
        PowerCycleRace->InvalidateLap();
    }
    bool PowerCycleReady() {
        if (!bPowerCycleEnabled || PowerCyclePhase==3) return true;
        if (Gates.Num()!=2 || Wires.Num()!=2 || !Gates[0].IsValid() || !Gates[1].IsValid() || !Wires[0].IsValid() || !Source.IsValid()) {
            Finish(false,TEXT("Owned power-cycle gates, wire or source disappeared."));return false;
        }
        auto* First=Gates[0].Get();auto* Second=Gates[1].Get();auto* Wire=Wires[0].Get();
        const auto FirstLight=ReadGateLighting(First),SecondLight=ReadGateLighting(Second);
        if (PowerCyclePhase==0) {
            if (!Delivered() || !FirstLight.Matches(true) || !SecondLight.Matches(true)) { LastReason=TEXT("Waiting for initial real delivery and both gates' visible lights/emissive readbacks.");return false; }
            if (!OwnedWireMatches()) { Finish(false,TEXT("Power-cycle wire endpoints do not match this run's own gate1/source."));return false; }
            PowerCycleRace.Reset(NewObject<UFactoryFPVRaceComponent>(GetTransientPackage(),NAME_None,RF_Transient));
            FString Error;
            if (!PowerCycleRace->StartCourse(World.Get(),Course,Error)) { Finish(false,TEXT("Public powered race selection failed before own-wire disconnection: ")+Error);return false; }
            RefreshPowerRace();
            if (PowerCycleRace->GetRaceState().problem!=factoryfpv::RaceProblem::None) { Finish(false,TEXT("Idle disarm reported a race fault before any lap started."));return false; }
            PowerCycleBaseline=PowerSnapshot();bPowerCycleGate2StayedPowered=true;
            Wire->Disconnect();PowerCyclePhase=1;
            Marker(false,TEXT("PENDING"),TEXT("Disconnected only this run's gate1 wire; observing real grid, lights and stationary course readiness."),EffectFields());return false;
        }
        const auto* OtherInfo=Second->GetPowerInfo();
        bPowerCycleGate2StayedPowered=bPowerCycleGate2StayedPowered && Second->IsGatePowered() && OtherInfo &&
            FMath::IsNearlyEqual(OtherInfo->GetActualConsumption(),AFactoryFPVRaceGate::PowerConsumptionMW,.005f);
        if (!bPowerCycleGate2StayedPowered) { Finish(false,TEXT("Independent owned gate2 lost real power during the gate1 wire cycle."));return false; }
        RefreshPowerRace();
        if (PowerCyclePhase==1) {
            bPowerCycleDisconnected=!Wire->GetConnection(0) && !Wire->GetConnection(1);
            const auto* Info=First->GetPowerInfo();FString Error;
            const bool CourseRejected=!UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true);
            bPowerCycleCourseFaultObserved=CourseRejected && Error.StartsWith(TEXT("Checkpoint 1 has no power.")) &&
                !PowerCycleRace->GetRaceState().ready && PowerCycleRace->GetRaceState().problem==factoryfpv::RaceProblem::UnpoweredGate;
            if (!bPowerCycleDisconnected || First->IsGatePowered() || !Info || !FMath::IsNearlyZero(Info->GetActualConsumption(),.005f) ||
                !FirstLight.Matches(false) || !SecondLight.Matches(true) || !bPowerCycleCourseFaultObserved) {
                LastReason=TEXT("Waiting for native gate1 disconnection, zero consumption, lights/emission off and course power fault.");return false;
            }
            bPowerLossObserved=true;PowerCycleLost=PowerSnapshot();
            if (Source->GetNumFreeConnections()<1 || First->GetPowerConnection()->GetNumFreeConnections()<1 ||
                !Wire->Connect(Source.Get(),First->GetPowerConnection())) { Finish(false,TEXT("Native reconnect rejected this run's same disconnected wire/endpoints."));return false; }
            bPowerCycleReconnected=OwnedWireMatches();PowerCyclePhase=2;
            Marker(false,TEXT("PENDING"),TEXT("Native power loss extinguished gate1 and rejected course readiness while gate2 stayed powered; reconnecting the same owned wire."),EffectFields());return false;
        }
        bPowerCycleReconnected=OwnedWireMatches();
        if (!bPowerCycleReconnected || !Delivered() || !FirstLight.Matches(true) || !SecondLight.Matches(true)) {
            LastReason=TEXT("Waiting for the same native wire to restore both .1 MW consumers and all lights/emissive readbacks.");return false;
        }
        const auto& RaceState=PowerCycleRace->GetRaceState();
        bPowerCycleCourseRecovered=RaceState.ready && !RaceState.inLap && RaceState.nextGate==1 && RaceState.problem==factoryfpv::RaceProblem::None;
        if (!bPowerCycleCourseRecovered) { LastReason=TEXT("Waiting for public stationary course readiness to recover at gate1.");return false; }
        bPowerCycleRestored=true;PowerCycleRestored=PowerSnapshot();PowerCyclePhase=3;
        PowerCycleRace->StopCourse();
        Marker(false,TEXT("PENDING"),TEXT("Same owned wire restored native gate power, lights/emission and course readiness; proceeding to save/reload."),EffectFields());return true;
    }
    bool VerifyPowerCycleState() {
        if (!bPowerCycleEnabled) return true;
        if (Manifest.FindRef(TEXT("powerCycleEnabled"))!=TEXT("1") || Manifest.FindRef(TEXT("powerCycleVerified"))!=TEXT("1")) {
            Finish(false,TEXT("Reloaded fixture lacks verified before-save power-cycle provenance."));return false;
        }
        FString Error;
        if (Gates.Num()!=2 || !ReadGateLighting(Gates[0].Get()).Matches(true) || !ReadGateLighting(Gates[1].Get()).Matches(true) ||
            !UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true)) {
            LastReason=TEXT("Waiting for restored loaded gate lights/materials and public course readiness; no loaded wire is changed.");return false;
        }
        bPowerCycleRestored=true;bPowerCycleCourseRecovered=true;PowerCycleRestored=PowerSnapshot();return true;
    }
    bool EffectsReady() {
        if (!bConstructionEffectsEnabled) return true;
        bool Complete=Gates.Num()==2;
        for (int32 Index=0;Index<Gates.Num();++Index) {
            auto* Gate=Gates[Index].Get();
            if (!Gate || Index>=2) { Complete=false;continue; }
            if (!bVerify && (Gate->IsPlayingBuildEffect() || IsValid(Gate->GetBuildEffectActor()))) ConstructionEffectsObserved[Index]=true;
            if ((!bVerify && !ConstructionEffectsObserved[Index]) || Gate->IsPlayingBuildEffect() || Gate->IsHidden()) Complete=false;
        }
        bConstructionEffectsCompleted=Complete;
        if (!Complete) LastReason=TEXT("Waiting for both native construction effects to be observed and finish visibly; see constructionEffectStates/template diagnostics.");
        return Complete;
    }
    void Cleanup() {
        // Only actors created by this exact run are eligible for cleanup. Verify
        // never owns existing/loaded course actors and never saves the world.
        if (bLiveVertical) {
            EndLiveFlight();
            RestoreLivePioneer();
        }
        RestoreCaptureView();
        if (PowerCycleRace.Get()) { PowerCycleRace->StopCourse();PowerCycleRace.Reset(); }
        if (bVerify) return;
        for (const auto& Weak:Wires) if (auto* Wire=Weak.Get()) { Wire->Disconnect();Wire->Destroy(); }
        for (const auto& Weak:Gates) if (auto* Gate=Weak.Get()) Gate->Destroy();
    }
    void Finish(bool Success,const FString& Reason,const FString& Extra=FString()) {
        if (bDone) return;
        bDone=true;
        const FString Evidence=(Extra.IsEmpty() ? EffectFields() : Extra)+LiveFields();
        if (!Success) Cleanup();
        Marker(bVerify,Success ? TEXT("PASS") : TEXT("FAIL"),Reason,Evidence);
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
    }
    bool Ready() {
        auto* Current=World.Get();
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone) {
            LastReason=TEXT("A live standalone world is required.");return false;
        }
        auto* Mode=Current->GetAuthGameMode<AFGGameMode>();
        auto* Session=Mode ? Mode->GetSaveSession() : nullptr;
        auto* PC=Current->GetFirstPlayerController();
        auto* Character=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Mode || Mode->IsMainMenuGameMode() || !Current->HasBegunPlay() || !Session || !Character || !Character->IsAliveAndWell()) return false;
        const auto& Header=Session->GetLoadedSaveHeader();
        const FString Expected=bVerify ? (bPractice ? PracticeName : RoundtripName) : CloneName;
        const auto Load=Mode->GetLoadData();
        if (Header.SaveName.IsEmpty() || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (!Load.LoadName.Equals(Expected,ESearchCase::CaseSensitive) && !Header.SaveName.Equals(Expected,ESearchCase::CaseSensitive))) {
            LastReason=TEXT("Loaded save identity does not match the expected disposable clone/roundtrip.");return false;
        }
        if (bLiveVertical) {
            FString Clone;
            if (!Load.LoadName.Equals(Expected,ESearchCase::CaseSensitive) ||
                !UFGSaveSystem::GetAbsolutePathForSaveGame(Current,CloneName,Clone) ||
                !FPaths::IsUnderDirectory(NormalizedPath(Clone),NormalizedPath(FPaths::ProjectSavedDir())) ||
                FPaths::GetCleanFilename(Clone)!=TEXT("FPV-QA-Clone.sav") || !IFileManager::Get().FileExists(*Clone)) {
                LastReason=TEXT("The live flight requires the exact native disposable clone path.");return false;
            }
            if (bPractice) {
                PracticePath=FPaths::Combine(FPaths::GetPath(NormalizedPath(Clone)),FString(PracticeName)+TEXT(".sav"));
                if (bVerify) {
                    FString Actual;
                    if (!Header.SaveName.Equals(PracticeName,ESearchCase::CaseSensitive) ||
                        !Header.SessionName.Equals(PracticeName,ESearchCase::CaseSensitive) ||
                        !UFGSaveSystem::GetAbsolutePathForSaveGame(Current,PracticeName,Actual) ||
                        NormalizedPath(Actual)!=PracticePath || IFileManager::Get().FileSize(*PracticePath)<=0) {
                        LastReason=TEXT("The practice stage requires the exact native handoff filename, path and session.");return false;
                    }
                    PracticeSessionName=Header.SessionName;
                    bPracticeLoaded=true;
                }
            }
        }
        if (Session->HasSaveGameOperationPending()) { LastReason=TEXT("Another save is still pending.");return false; }
        Pioneer=Character;
        SaveSession=Session;
        if (auto* NativePC=Cast<AFGPlayerController>(PC);!NativePC || !FactoryFPVShippingStartup::Ready(NativePC->GetGameUI(),LastReason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
        if (!RecipeReadback()) return false;
        return true;
    }
    FString LiveFields() const {
        if (!bLiveVertical) return {};
        const auto Flag=[](bool Value) { return Value ? TEXT("true") : TEXT("false"); };
        FString Fields=FString::Printf(TEXT(",\"liveVerticalEnabled\":true,\"liveVerticalPoweredGates\":%s,\"liveVerticalClearColumn\":%s,"
            "\"liveVerticalPioneerRestored\":%s,\"liveVerticalFixtureRemoved\":%s,\"scriptedControls\":true,"
            "\"savedWorld\":%s,\"directRaceFeed\":false,\"forcedDronePose\":false"),
            Flag(bLivePoweredGates),Flag(bLiveClearColumn),Flag(bLivePioneerRestored),Flag(bLiveFixtureRemoved),Flag(bPracticeSaveVerified));
        if (bPractice) Fields+=FString::Printf(TEXT(",\"verticalPracticeEnabled\":true,\"verticalPracticeCourse\":%d,"
            "\"verticalPracticeFreshRecord\":%s,\"verticalPracticeStagedPioneer\":%s,\"verticalPracticeSaveCallback\":%s,"
            "\"verticalPracticeSaveVerified\":%s,\"verticalPracticeLoaded\":%s,\"verticalPracticeGatesRetained\":%s,"
            "\"verticalPracticeSaveName\":\"%s\",\"verticalPracticeSavePath\":\"%s\",\"verticalPracticeSessionName\":\"%s\""),
            Course,Flag(bPracticeFreshRecord),Flag(bPracticeStagedPioneer),Flag(bPracticeSaveCallback),
            Flag(bPracticeSaveVerified),Flag(bPracticeLoaded),Flag(bPracticeGatesRetained),
            PracticeName,*Escape(PracticePath),*Escape(PracticeSessionName));
        if (bPracticeRaceButton) Fields+=FString::Printf(TEXT(",\"verticalPracticeRaceButtonVerified\":%s"),Flag(bPracticeRaceButtonVerified));
        if (bPracticeModeButton) Fields+=FString::Printf(TEXT(",\"verticalPracticeModeButtonVerified\":%s"),Flag(bPracticeModeButtonVerified));
        if (bPracticeCourseName) Fields+=FString::Printf(TEXT(",\"verticalPracticeCourseName\":\"%s\",\"verticalPracticeNameLayoutUnchanged\":%s,"
            "\"verticalPracticeNameReloaded\":%s,\"verticalPracticeNamePresented\":%s,\"verticalPracticeNameApplyVerified\":%s"),
            *Escape(PracticeObservedCourseName),Flag(bPracticeNameLayoutUnchanged),Flag(bPracticeNameReloaded),
            Flag(bPracticeNamePresented),Flag(bPracticeNameApplyVerified));
        if (bCheckRaceDebrief) Fields+=FString::Printf(TEXT(",\"liveVerticalDebriefLapVerified\":%s,\"liveVerticalDebriefReportVerified\":%s"),
            Flag(bRaceDebriefLapVerified),Flag(bRaceDebriefReportVerified));
        return Fields+LiveFlight.Evidence();
    }
    void EndLiveFlight() {
        LiveFlight.Cancel();
        if (auto* Drone=LiveDrone.Get()) Drone->ExitDrone();
        LiveDrone.Reset();
    }
    bool RestoreLivePioneer() {
        if (!bLivePoseStaged) return true;
        auto* Character=Pioneer.Get();
        auto* PC=World.IsValid() ? World->GetFirstPlayerController() : nullptr;
        auto* Hud=PC ? Cast<AFGHUD>(PC->GetHUD()) : nullptr;
        auto* Move=Character ? Character->GetCharacterMovement() : nullptr;
        if (!Character || !PC || !Hud || !Move || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Character)) return false;
        const bool bNativeReturn=PC->GetPawn()==Character && PC->GetViewTarget()==Character && Character->IsAliveAndWell() &&
            !AFactoryFPVDrone::FindActive(World.Get()) && Move->MovementMode==LiveOriginalMovement &&
            PC->IsMoveInputIgnored()==bLiveMoveIgnored && PC->IsLookInputIgnored()==bLiveLookIgnored &&
            Hud->GetHiddenHUDMode()==bLiveHudHidden && Hud->GetPartialPumpiMode()==bLiveHudPartial;
        if (!bNativeReturn) return false;
        if (auto* Local=PC->GetLocalPlayer()) {
            if (auto* Hangar=Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()) Hangar->SetSelectedRacingCourse(0);
        }
        Move->StopMovementImmediately();
        PC->SetControlRotation(LiveOriginalControl);
        if (!Character->SetActorLocationAndRotation(LiveOriginalPose.GetLocation(),LiveOriginalPose.GetRotation(),
            false,nullptr,ETeleportType::TeleportPhysics) || !Character->GetActorTransform().Equals(LiveOriginalPose,3.)) return false;
        bLivePoseStaged=false;
        bLivePioneerRestored=true;
        return true;
    }
    bool LaunchCourseThroughHangar(UFactoryFPVLocalPlayerSubsystem& Hangar,FString& Error,bool bPracticeFlight=false) {
        auto* Viewport=World->GetGameViewport();
        const auto Window=Viewport ? Viewport->GetWindow() : nullptr;
        const auto Accept=SlateAcceptKey();
        if (!Window || !Accept.IsValid()) { Error=TEXT("The practice launch requires a native Slate window and navigation Accept key.");return false; }
        const auto Root=Window.ToSharedRef();
        const FKeyEvent AcceptEvent(Accept,FModifierKeysState(),0,false,0,0);
        const auto Find=[&Root](const TCHAR* Tag) { return FindTaggedSlateWidget(Root,FName(Tag)); };
        const auto Press=[&](const TCHAR* Tag) { return PressTaggedSlateButton(Root,Tag,AcceptEvent); };
        const int32 QueuedCourse=Hangar.GetSelectedRacingCourse();
        const FString QueuedLayout=Hangar.GetSelectedRacingLayout();
        // Use the normal retry-mode override to exercise Keyboard without changing any radio profile.
        Hangar.OpenPanel(FString(),TEXT("keyboard"));
        ON_SCOPE_EXIT { if (Hangar.IsPanelOpen()) Hangar.ClosePanel(); };
        Window->SlatePrepass();
        if (!Hangar.IsHangarOpen() || !Press(TEXT("FactoryFPVHangarRacingTab"))) {
            Error=TEXT("The mounted hangar did not open its Racing tab through the real Slate button.");return false;
        }
        Window->SlatePrepass();
        const auto Widget=Find(TEXT("FactoryFPVRaceCoursePicker"));
        if (!Widget || Widget->GetTypeAsString()!=TEXT("SComboBox<TSharedPtr<int32>>") || !SlateVisibleFrom(Root,Widget)) {
            Error=TEXT("The mounted Racing tab did not expose its course picker.");return false;
        }
        const auto Picker=StaticCastSharedPtr<SComboBox<TSharedPtr<int32>>>(Widget);
        FSlateAttributeMetaData::UpdateAllAttributes(*Widget,FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
        // Drive the picker delegate directly through Slate without OS focus or menu navigation.
        if (Widget->IsEnabled()) Picker->SetSelectedItem(MakeShared<int32>(Course));
        const auto Selected=Picker->GetSelectedItem();
        if (!Widget->IsEnabled() || !Selected || *Selected!=Course || Hangar.GetSelectedRacingCourse()!=QueuedCourse ||
            Hangar.GetSelectedRacingLayout()!=QueuedLayout ||
            AFactoryFPVDrone::FindActive(World.Get())) {
            Error=TEXT("The actual course picker did not preview course 97 without queuing or launching it.");return false;
        }
        if (bPracticeCourseName && !bPracticeFlight) {
            const auto Label=Find(TEXT("FactoryFPVRaceSelectedCourseLabel"));
            const auto LabelShowsName=[&](const FString& Name) {
                if (!Label || Label->GetTypeAsString()!=TEXT("STextBlock") || !SlateVisibleFrom(Root,Label)) return false;
                FSlateAttributeMetaData::UpdateAllAttributes(*Label,FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
                const FString Text=StaticCastSharedPtr<STextBlock>(Label)->GetText().ToString();
                return Text.StartsWith(Name+TEXT(" "),ESearchCase::CaseSensitive);
            };
            bPracticeNamePresented=LabelShowsName(PracticeObservedCourseName);
            if (!bPracticeNamePresented) { Error=TEXT("The mounted course picker did not display the native saved practice course name.");return false; }
            const auto NameWidget=Find(TEXT("FactoryFPVRaceCourseName"));
            const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World.Get());
            if (!Registry || !NameWidget || NameWidget->GetTypeAsString()!=TEXT("SEditableTextBox") || !SlateVisibleFrom(Root,NameWidget)) {
                Error=TEXT("The mounted Racing tab did not expose its actual track name editor.");return false;
            }
            const auto NameInput=StaticCastSharedPtr<SEditableTextBox>(NameWidget);
            FSlateAttributeMetaData::UpdateAllAttributes(*NameWidget,FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
            const FString Layout=Manifest.FindRef(TEXT("layout"));
            const auto ApplyName=[&](const FString& Name) {
                NameInput->SetText(FText::FromString(Name));
                return Press(TEXT("FactoryFPVRaceApplyCourseName")) && Registry->GetNameForCourse(Course).Equals(Name,ESearchCase::CaseSensitive) &&
                    UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course).Equals(Layout,ESearchCase::CaseSensitive) && LabelShowsName(Name);
            };
            const FString TemporaryName=TEXT("Practice QA ")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
            bPracticeNameApplyVerified=NameInput->IsEnabled() && NameInput->GetText().ToString().Equals(PracticeObservedCourseName,ESearchCase::CaseSensitive) &&
                ApplyName(TemporaryName) && ApplyName(PracticeObservedCourseName);
            if (!bPracticeNameApplyVerified) {
                Error=TEXT("The mounted Apply name action did not rename and restore the course without changing its layout.");return false;
            }
        }
        const bool bPressed=Press(bPracticeFlight ? TEXT("FactoryFPVRaceSaveAndPractice") : TEXT("FactoryFPVRaceSaveAndLaunch"));
        LiveDrone=AFactoryFPVDrone::FindActive(World.Get());
        const auto* Timing=LiveDrone.IsValid() ? LiveDrone->FindComponentByClass<UFactoryFPVRaceComponent>() : nullptr;
        const bool bCorrectQueue=bPracticeFlight ?
            Hangar.GetSelectedRacingCourse()==QueuedCourse && Hangar.GetSelectedRacingLayout()==QueuedLayout :
            Hangar.GetSelectedRacingCourse()==Course && Hangar.GetSelectedRacingLayout()==Manifest.FindRef(TEXT("layout"));
        const bool bLaunched=bPressed && !Hangar.IsPanelOpen() && Timing && Timing->GetCourseId()==Course &&
            Timing->IsPractice()==bPracticeFlight && bCorrectQueue;
        if (!bLaunched) Error=bPracticeFlight ? TEXT("Save & practice did not launch the viewed course in practice mode with the existing race queue preserved.") :
            TEXT("One Save & race press did not close the hangar and launch the viewed powered course.");
        return bLaunched;
    }
    bool VerifyLiveRaceDebrief(FString& Error) {
        auto* PC=World->GetFirstPlayerController();
        auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        const auto* Debrief=Hangar ? Hangar->GetLastRaceDebrief() : nullptr;
        const auto* Lower=Gates[0].Get();
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World.Get());
        const FString CourseName=Registry ? Registry->GetNameForCourse(Course) : FString();
        if (!Debrief || !Lower || Layout.IsEmpty() || Debrief->CourseId!=Course ||
            !Debrief->CourseName.Equals(CourseName,ESearchCase::CaseSensitive) || Debrief->LapCount!=1 ||
            Debrief->RecentLaps.Num()!=1 || Debrief->LastLapSplits.Num()!=1) {
            Error=TEXT("Normal return did not retain one completed lap and one checkpoint split for the live course.");return false;
        }
        const double LapSeconds=Lower->GetBestLapSeconds(Layout);
        const auto Splits=Lower->GetBestLapSplits(Layout,1);
        const auto& Lap=Debrief->RecentLaps[0];
        const auto& Split=Debrief->LastLapSplits[0];
        bRaceDebriefLapVerified=Lap.Number==1 && Lap.bCourseBest && LapSeconds>0. && Lap.Seconds==LapSeconds &&
            Debrief->FastestLap==LapSeconds && Lap.ReferenceSeconds==0. && Split.Checkpoint==2 &&
            Splits.Num()==1 && Split.Seconds==Splits[0] && Split.ReferenceSeconds==0.;
        if (!bRaceDebriefLapVerified) {
            Error=TEXT("The debrief did not match the actual fresh course-best lap and checkpoint split without a reference.");return false;
        }

        auto* Viewport=World->GetGameViewport();
        const auto Window=Viewport ? Viewport->GetWindow() : nullptr;
        const auto Accept=SlateAcceptKey();
        if (!Window || !Accept.IsValid() || Hangar->IsPanelOpen()) {
            Error=TEXT("The race debrief probe requires an idle hangar and native Slate window.");return false;
        }
        const auto Root=Window.ToSharedRef();
        const FKeyEvent AcceptEvent(Accept,FModifierKeysState(),0,false,0,0);
        Hangar->OpenPanel(FString(),TEXT("keyboard"));
        ON_SCOPE_EXIT { if (Hangar->IsPanelOpen()) Hangar->ClosePanel(); };
        Window->SlatePrepass();
        if (!Hangar->IsHangarOpen() || !PressTaggedSlateButton(Root,TEXT("FactoryFPVHangarRacingTab"),AcceptEvent)) {
            Error=TEXT("The actual hangar could not open the Racing tab after ordinary return.");return false;
        }
        Window->SlatePrepass();
        if (!PressTaggedSlateButton(Root,TEXT("FactoryFPVRaceDebriefTab"),AcceptEvent)) {
            Error=TEXT("The mounted Racing tab did not open its Last race report.");return false;
        }
        Window->SlatePrepass();
        const auto Report=FindTaggedSlateWidget(Root,TEXT("FactoryFPVRaceDebriefReport"));
        const auto Summary=FindTaggedSlateWidget(Root,TEXT("FactoryFPVRaceDebriefSummary"));
        const FString ExpectedSummary=FString::Printf(TEXT("1 completed lap  /  Session fastest %.3f s"),LapSeconds);
        bRaceDebriefReportVerified=SlateVisibleFrom(Root,Report) && SlateVisibleFrom(Root,Summary) &&
            Summary->GetTypeAsString()==TEXT("STextBlock") &&
            StaticCastSharedPtr<STextBlock>(Summary)->GetText().ToString().Equals(ExpectedSummary,ESearchCase::CaseSensitive) &&
            SlateVisibleFrom(Root,FindTaggedSlateWidget(Root,TEXT("FactoryFPVRaceDebriefRecentLaps"))) &&
            SlateVisibleFrom(Root,FindTaggedSlateWidget(Root,TEXT("FactoryFPVRaceDebriefSplits")));
        if (!bRaceDebriefReportVerified) Error=TEXT("Last race did not show the session summary, completed lap and checkpoint splits.");
        return bRaceDebriefReportVerified;
    }
    void BeginLiveFlight() {
        auto* Character=Pioneer.Get();
        auto* PC=World->GetFirstPlayerController();
        auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        auto* Hud=PC ? Cast<AFGHUD>(PC->GetHUD()) : nullptr;
        auto* Move=Character ? Character->GetCharacterMovement() : nullptr;
        if (!PC || !Hud || !Move || !Hangar || PC->GetViewTarget()!=Character || !Move->IsMovingOnGround() ||
            Hangar->GetSelectedRacingCourse()!=0 || Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(World.Get()) ||
            !GeometryMatches() || !bMilestonePurchased || !bVerticalRecipeAvailable) {
            Finish(false,TEXT("The live fixture requires an idle native pioneer and two unlocked vertical gates."));return;
        }
        if (bCheckRaceDebrief && Hangar->GetLastRaceDebrief()) {
            Finish(false,TEXT("The race debrief fixture requires a fresh session without prior results."));return;
        }
        const auto Lower=Gates[0]->GetActorTransform();
        FVector Stand;
        const bool bLoadedPractice=bPractice && bVerify;
        if (bLoadedPractice) LiveYaw=Lower.Rotator().Yaw;
        bLiveClearColumn=ClearLiveColumn(Lower.GetLocation()) && FindLiveStand(Lower,Stand) &&
            (bLoadedPractice || Stand.Equals(LiveStand,5.));
        if (!bLiveClearColumn) { Finish(false,TEXT("The owned gates do not leave the checked launch stand and clear flight column."));return; }
        if (bLoadedPractice) {
            LiveStand=Stand;
            const double HeadingError=FMath::Abs(FMath::FindDeltaAngleDegrees(PC->GetControlRotation().Yaw,LiveYaw));
            bPracticeStagedPioneer=Character->GetActorLocation().Equals(LiveStand,5.) && HeadingError<=3.;
            if (!bPracticeStagedPioneer) {
                Finish(false,FString::Printf(TEXT("Native reload did not restore the practice start and launch heading. Position delta %.2f cm, yaw delta %.2f degrees."),
                    FVector::Distance(Character->GetActorLocation(),LiveStand),HeadingError));return;
            }
        }
        bLivePoweredGates=true;
        LiveOriginalPose=Character->GetActorTransform();
        LiveOriginalControl=PC->GetControlRotation();
        LiveOriginalMovement=Move->MovementMode;
        bLiveMoveIgnored=PC->IsMoveInputIgnored();bLiveLookIgnored=PC->IsLookInputIgnored();
        bLiveHudHidden=Hud->GetHiddenHUDMode();bLiveHudPartial=Hud->GetPartialPumpiMode();
        bLivePoseStaged=true;
        if (!bLoadedPractice) {
            Move->StopMovementImmediately();
            const FRotator Heading(0,LiveYaw,0);
            const FRotator BodyRotation=bPractice ? Heading : Character->GetActorRotation();
            if (!Character->SetActorLocationAndRotation(LiveStand,BodyRotation,false,nullptr,ETeleportType::TeleportPhysics)) {
                Finish(false,TEXT("The disposable pioneer could not reach the verified flight stand."));return;
            }
            PC->SetControlRotation(Heading);
            if (bPractice) {
                bPracticeStagedPioneer=Character->GetActorLocation().Equals(LiveStand,3.) && Character->GetActorRotation().Equals(Heading,1.);
                if (!bPracticeStagedPioneer) { Finish(false,TEXT("The practice pioneer did not retain its staged pose."));return; }
                RequestPracticeSave();
                return;
            }
        }
        FString Error;
        if (bPracticeRaceButton) {
            bPracticeRaceButtonVerified=LaunchCourseThroughHangar(*Hangar,Error);
            if (!bPracticeRaceButtonVerified) { Finish(false,Error);return; }
        } else {
            Hangar->SetSelectedRacingCourse(Course);
            LiveDrone=AFactoryFPVDrone::Launch(PC,TEXT("keyboard"),Error);
        }
        if (!LiveDrone.IsValid() || !LiveFlight.Begin(*LiveDrone.Get(),*Gates[0].Get(),*Gates[1].Get(),Error)) {
            Finish(false,TEXT("The ordinary keyboard flight could not begin in the clear powered column: ")+Error);return;
        }
        LastReason=TEXT("Waiting for the live drone to climb, descend, and finish through both powered vertical gates.");
        Marker(false,TEXT("PENDING"),LastReason,LiveFields());
    }
    bool TickLiveFlight(float DeltaSeconds) {
        if (bLiveReturning) {
            if (!RestoreLivePioneer()) return true;
            FString Error;
            if (bCheckRaceDebrief && !VerifyLiveRaceDebrief(Error)) { Finish(false,Error);return false; }
            if (bPractice && bVerify) {
                if (bPracticeModeButton && !bPracticeModeButtonVerified) {
                    auto* PC=World->GetFirstPlayerController();
                    auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
                    auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
                    if (!Hangar) { Finish(false,TEXT("The practice mode probe could not reopen the native hangar."));return false; }
                    // The previous scored flight restored an empty queue. Give
                    // practice a different queued course to catch overwrites.
                    Hangar->SetSelectedRacingCourse(Course-1);
                    if (!LaunchCourseThroughHangar(*Hangar,Error,true)) { Finish(false,Error);return false; }
                    bPracticeModeButtonVerified=true;
                    bLivePoseStaged=true;
                    bLivePioneerRestored=false;
                    EndLiveFlight();
                    LastReason=TEXT("Waiting for normal pioneer return after Save & practice.");
                    return true;
                }
                bPracticeGatesRetained=Delivered() && GeometryMatches();
                Finish(bPracticeGatesRetained,bPracticeGatesRetained ?
                    TEXT("The exact practice save launched and flew a complete vertical lap from its loaded pioneer pose. The world was not saved again.") :
                    TEXT("The reloaded practice course did not retain both gates after ordinary return."));
                return false;
            }
            Cleanup();
            bLiveFixtureRemoved=!Gates.ContainsByPredicate([](const auto& Gate) { return Gate.IsValid(); }) &&
                !Wires.ContainsByPredicate([](const auto& Wire) { return Wire.IsValid(); });
            Finish(bLiveFixtureRemoved,bLiveFixtureRemoved ?
                TEXT("A live armed drone scored both powered vertical gates, rejected wrong-way crossings, displayed the lap, and returned to its pioneer.") :
                TEXT("The verified live flight did not remove every owned fixture actor."));
            return false;
        }
        FString Error;
        const auto Result=LiveFlight.Tick(DeltaSeconds,Error);
        using EResult=FFactoryFPVShippingVerticalFlight::EResult;
        if (Result==EResult::Failed) { Finish(false,Error);return false; }
        if (Result==EResult::Passed) {
            if (!Pioneer.IsValid() || !Pioneer->GetActorLocation().Equals(LiveStand,5.)) {
                Finish(false,TEXT("The anchored pioneer moved away from the launch stand during the flight."));return false;
            }
            EndLiveFlight();
            bLiveReturning=true;
            LastReason=TEXT("Waiting for normal pioneer return and fixture removal after the completed lap.");
        }
        return true;
    }
    bool PlaceOnClearFloor(const FVector& Requested,const FRotator& Rotation,FTransform& Out,int32* Failures,bool Vertical=false) const {
        auto* Current=World.Get();
        if (!Current) return false;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingQAFloor),false,Pioneer.Get());
        FHitResult Ground;
        const bool Hit=Current->LineTraceSingleByChannel(Ground,Requested+FVector(0,0,1000),Requested-FVector(0,0,5000),ECC_WorldStatic,Query);
        const int32 Failure=!Hit ? 0 : Ground.bStartPenetrating ? 1 : Ground.ImpactNormal.Z<.95f ? 2 : INDEX_NONE;
        if (Failure!=INDEX_NONE) { ++Failures[Failure];return false; }
        Out=FTransform(Rotation,Ground.ImpactPoint+FVector(0,0,2));
        // Conservative whole-buildable clearance, including text/arrow space.
        // This automation never dismantles or moves existing factory content.
        const bool Clear=!Current->OverlapBlockingTestByChannel(Out.TransformPosition(FVector(-50,0,305)),Rotation.Quaternion(),
            ECC_WorldDynamic,FCollisionShape::MakeBox(FVector(Vertical ? 275 : 180,480,300)),Query);
        if (!Clear) ++Failures[3];
        return Clear;
    }
    bool ClearLiveColumn(const FVector& Base) const {
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingLiveColumn),false,Pioneer.Get());
        FHitResult Hit;
        return !World->SweepSingleByChannel(Hit,Base+FVector(0,0,60),Base+FVector(0,0,LiveColumnHeightCm),
            FQuat::Identity,ECC_WorldDynamic,FCollisionShape::MakeSphere(55),Query);
    }
    bool FindLiveStand(const FTransform& Lower,FVector& Stand) const {
        auto* Character=Pioneer.Get();
        auto* Capsule=Character ? Character->GetCapsuleComponent() : nullptr;
        auto* Move=Character ? Character->GetCharacterMovement() : nullptr;
        if (!Capsule || !Move) return false;
        const FVector Point=Lower.GetLocation()-Lower.GetUnitAxis(EAxis::X)*150.;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingLiveStand),false,Character);
        FHitResult Floor;
        if (!World->LineTraceSingleByChannel(Floor,Point+FVector(0,0,60),Point-FVector(0,0,40),ECC_WorldStatic,Query) ||
            Floor.bStartPenetrating || Floor.ImpactNormal.Z<.95f) return false;
        const float HalfHeight=Capsule->GetScaledCapsuleHalfHeight();
        Stand=Floor.ImpactPoint+FVector(0,0,HalfHeight+2.2);
        const auto Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),HalfHeight);
        if (World->OverlapBlockingTestByChannel(Stand,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),
            Shape,Query,FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) return false;
        FFindFloorResult Walkable;
        Move->FindFloor(Stand,Walkable,false);
        return Walkable.IsWalkableFloor();
    }
    bool PlaceLiveGates(const FVector& Requested,const FRotator& Rotation,FTransform& Lower,FTransform& Upper,int32* Failures) {
        if (!PlaceOnClearFloor(Requested,Rotation,Lower,Failures,true) || !ClearLiveColumn(Lower.GetLocation()) ||
            !FindLiveStand(Lower,LiveStand)) return false;
        Upper=Lower;
        Upper.AddToTranslation(FVector(0,0,LiveGateSpacingCm));
        const FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingLiveUpper),false,Pioneer.Get());
        if (World->OverlapBlockingTestByChannel(Upper.TransformPosition(FVector(-50,0,180)),Rotation.Quaternion(),
            ECC_WorldDynamic,FCollisionShape::MakeBox(FVector(275,480,190)),Query)) return false;
        LiveYaw=Rotation.Yaw;
        return true;
    }
    bool FindSetup(UFGPowerConnectionComponent*& OutSource,FTransform& First,FTransform& Second,TSubclassOf<AFGBuildableWire>& WireClass,
        TSubclassOf<UFGRecipe>& WireRecipe) {
        auto* Current=World.Get();
        auto* Character=Pioneer.Get();
        if (!Current || !Character) return false;
        if (bPracticeCourseName) {
            const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(Current);
            if (!Registry || !Registry->GetNameForCourse(Course).IsEmpty()) {
                Finish(false,TEXT("Practice naming requires a live registry and an unused course 97 name in the source clone."));return false;
            }
        }
        WireClass=LoadClass<AFGBuildableWire>(nullptr,TEXT("/Game/FactoryGame/Buildable/Factory/PowerLine/Build_PowerLine.Build_PowerLine_C"));
        WireRecipe=LoadClass<UFGRecipe>(nullptr,TEXT("/Game/FactoryGame/Recipes/Buildings/Recipe_PowerLine.Recipe_PowerLine_C"));
        if (!WireClass || !WireRecipe || WireClass->HasAnyClassFlags(CLASS_Abstract)) {
            LastReason=TEXT("Real power-line buildable/recipe classes did not resolve.");return false;
        }
        for (TActorIterator<AFactoryFPVRaceGate> It(Current);It;++It)
            if (It->IsLiveRaceGate() && It->GetCourseId()==Course) {
                LastReason=TEXT("Reserved QA course 97 already exists; refusing to alter it.");return false;
            }
        TArray<UFGPowerConnectionComponent*> Candidates;
        TArray<UFGPowerConnectionComponent*> PoweredCandidates;
        // Progressive counts diagnose prerequisites without relaxing any rule.
        int32 Counts[13]={};
        int32 FloorFailures[4]={};
        const TCHAR* Names[]={TEXT("total"),TEXT("visibleDefault"),TEXT("near80m"),TEXT("spare1"),TEXT("spare2"),TEXT("circuit"),
            TEXT("powered"),TEXT("unfused"),TEXT("headroom"),TEXT("clearPairs"),TEXT("wireChecks"),TEXT("wireMatches"),TEXT("pairAttempts")};
        for (TActorIterator<AFGBuildable> It(Current);It;++It) {
            if (It->IsActorBeingDestroyed() || It->GetIsDismantled()) continue;
            TInlineComponentArray<UFGPowerConnectionComponent*> Connections(*It);
            for (auto* Connection:Connections) {
                ++Counts[0];
                if (!Connection || Connection->IsHidden() || Connection->GetPowerConnectionType()!=EPowerConnectionType::PCT_Default) continue;
                ++Counts[1];
                if (FVector::DistSquared(Connection->GetComponentLocation(),Character->GetActorLocation())>FMath::Square(8000.)) continue;
                ++Counts[2];
                const int32 Free=Connection->GetNumFreeConnections();
                if (Free<1) continue; ++Counts[3];
                if (Free<2) continue; ++Counts[4];
                auto* Circuit=Connection->GetPowerCircuit();
                if (!Circuit) continue; ++Counts[5];
                if (!Circuit->HasPower()) continue; ++Counts[6];
                if (Circuit->IsFuseTriggered()) continue; ++Counts[7];
                PoweredCandidates.Add(Connection);
                FPowerCircuitStats Stats;
                Circuit->GetStats(Stats);
                // MaximumPowerConsumption is hypothetical simultaneous demand.
                // This fixture adds 0.2 MW to actual demand and still requires
                // real native consumption, powered state and untripped fuses.
                const float Headroom=Circuit->GetPowerProductionCapacity()-Stats.PowerConsumed;
                if (!FMath::IsFinite(Headroom) || Headroom<.25f) continue;
                ++Counts[8];
                Candidates.Add(Connection);
            }
        }
        const auto ByDistance=[Character](const UFGPowerConnectionComponent& A,const UFGPowerConnectionComponent& B) {
            return FVector::DistSquared(A.GetComponentLocation(),Character->GetActorLocation())<FVector::DistSquared(B.GetComponentLocation(),Character->GetActorLocation());
        };
        Candidates.Sort(ByDistance);
        PoweredCandidates.Sort(ByDistance);
        const float MaxWireLength=WireClass.GetDefaultObject()->mMaxLength;
        const auto Report=[&] {
            FString Fields=TEXT(",\"setupCounts\":{");
            FString Summary;
            for (int32 Index=0;Index<UE_ARRAY_COUNT(Counts);++Index) {
                Fields+=FString::Printf(TEXT("%s\"%s\":%d"),Index ? TEXT(",") : TEXT(""),Names[Index],Counts[Index]);
                Summary+=FString::Printf(TEXT("%s%s=%d"),Index ? TEXT(" ") : TEXT(""),Names[Index],Counts[Index]);
            }
            Fields+=FString::Printf(TEXT(",\"floorMiss\":%d,\"floorPenetrating\":%d,\"floorSlope\":%d,\"floorBlocked\":%d}"),FloorFailures[0],FloorFailures[1],FloorFailures[2],FloorFailures[3]);
            const auto Number=[](float Value) { return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.6f"),Value) : FString(TEXT("null")); };
            for (int32 Group=0;Group<2;++Group) {
                const auto& Nearest=Group ? PoweredCandidates : Candidates;
                Fields+=Group ? TEXT(",\"nearestPowered\":[") : TEXT(",\"nearestEligible\":[");
                bool bAnyNearest=false;
                for (int32 Index=0;Index<FMath::Min(3,Nearest.Num());++Index) {
                    auto* Connection=Nearest[Index];auto* Circuit=Connection->GetPowerCircuit();
                    if (!Circuit) continue;
                    FPowerCircuitStats Stats;Circuit->GetStats(Stats);
                    Fields+=FString::Printf(TEXT("%s{\"path\":\"%s\",\"distanceM\":%.3f,\"free\":%d,\"circuit\":%d,\"group\":%d,\"capacityMW\":%s,\"statsCapacityMW\":%s,\"consumedMW\":%s,\"maximumMW\":%s,\"liveMaximumMW\":%s,\"graphPoints\":%d}"),
                        bAnyNearest ? TEXT(",") : TEXT(""),*Escape(Connection->GetPathName()),FVector::Distance(Connection->GetComponentLocation(),Character->GetActorLocation())/100.,
                        Connection->GetNumFreeConnections(),Connection->GetCircuitID(),Circuit->GetCircuitGroupID(),*Number(Circuit->GetPowerProductionCapacity()),
                        *Number(Stats.PowerProductionCapacity),*Number(Stats.PowerConsumed),*Number(Stats.MaximumPowerConsumption),*Number(Circuit->GetMaximumPowerConsumption()),Stats.GetNumGraphPoints());
                    bAnyNearest=true;
                }
                Fields+=TEXT("]");
            }
            LastReason=FString::Printf(TEXT("No setup accepted: %s floorMiss=%d floorPenetrating=%d floorSlope=%d floorBlocked=%d."),*Summary,FloorFailures[0],FloorFailures[1],FloorFailures[2],FloorFailures[3]);
            if (!bSetupDiagnosticsPrinted) { Marker(false,TEXT("PENDING"),TEXT("Setup prerequisite snapshot; progressive counts use actual grid demand for headroom."),Fields);bSetupDiagnosticsPrinted=true; }
        };
        // Larger checkpoints need more possible sites around dense factories.
        // Seven origins, three radii and sixteen headings give at most 336
        // pairs; every candidate keeps the same floor/clearance/wire checks.
        constexpr int32 Directions=16;
        const float Radii[]={1200.f,2200.f,3200.f};
        const int32 Origins=1+FMath::Min(6,Candidates.Num());
        const int32 PairsPerOrigin=Directions*UE_ARRAY_COUNT(Radii);
        for (int32 Pair=0;Pair<Origins*PairsPerOrigin;++Pair) {
            const int32 Origin=Pair/PairsPerOrigin;
            const int32 Direction=Pair%Directions;
            const int32 RadiusIndex=(Pair%PairsPerOrigin)/Directions;
            const FRotator Rotation(0,Direction*(360.f/Directions),0);
            const FVector Forward=Rotation.Vector();
            const FVector Center=Origin ? Candidates[Origin-1]->GetComponentLocation() : Character->GetActorLocation();
            const FVector Requested=Center+Forward*Radii[RadiusIndex];
            ++Counts[12];
            if (bLiveVertical ? !PlaceLiveGates(Requested,Rotation,First,Second,FloorFailures) :
                (!PlaceOnClearFloor(Requested,Rotation,First,FloorFailures) ||
                 !PlaceOnClearFloor(Requested+Forward*800.,Rotation,Second,FloorFailures,bVerticalCourseEnabled))) continue;
            ++Counts[9];
            for (auto* Candidate:Candidates) {
                ++Counts[10];
                const FVector EndpointA=First.TransformPosition(bLiveVertical ? FVector(-207,-190,282) : FVector(-28,-390,250));
                const FVector EndpointB=Second.TransformPosition(bVerticalCourseEnabled || bLiveVertical ? FVector(-207,-190,282) : FVector(-28,0,250));
                if (FVector::Distance(Candidate->GetComponentLocation(),EndpointA)<=MaxWireLength &&
                    FVector::Distance(Candidate->GetComponentLocation(),EndpointB)<=MaxWireLength) { ++Counts[11];OutSource=Candidate;Report();return true; }
            }
        }
        Report();
        return false;
    }
    bool ConnectGateWire(AFactoryFPVRaceGate* Gate,TSubclassOf<AFGBuildableWire> WireClass,TSubclassOf<UFGRecipe> WireRecipe) {
        auto* Buildables=World.IsValid() ? AFGBuildableSubsystem::Get(World.Get()) : nullptr;
        if (!Buildables || !Gate || !Source.IsValid() || !WireClass || !WireRecipe) { Finish(false,TEXT("Native wiring prerequisites disappeared."));return false; }
        auto* Wire=Cast<AFGBuildableWire>(Buildables->BeginSpawnBuildable(WireClass,FTransform::Identity));
        if (!Wire) { Finish(false,TEXT("Native deferred power-line spawn failed."));return false; }
        Wires.Add(Wire);
        Wire->SetBuiltWithRecipe(WireRecipe);
        if (!Wire->Connect(Source.Get(),Gate->GetPowerConnection())) {
            Wire->FinishSpawning(FTransform::Identity);
            Finish(false,TEXT("Real power-line Connect rejected the endpoint pair."));return false;
        }
        Wire->FinishSpawning(FTransform::Identity);
        if (!IsValid(Wire)) { Finish(false,TEXT("Power line did not survive native initialization."));return false; }
        return true;
    }
    bool SpawnSetup() {
        auto* Current=World.Get();
        auto* Buildables=Current ? AFGBuildableSubsystem::Get(Current) : nullptr;
        if (!Buildables) { LastReason=TEXT("Buildable subsystem is not available.");return false; }
        if (!bExistingGatesObserved && !ObserveExistingGates()) return false;
        UFGPowerConnectionComponent* Selected=nullptr;
        FTransform Positions[2];
        TSubclassOf<AFGBuildableWire> WireClass;
        TSubclassOf<UFGRecipe> WireRecipe;
        if (!FindSetup(Selected,Positions[0],Positions[1],WireClass,WireRecipe)) return false;
        if (bConstructionEffectsEnabled) {
            const auto* State=Current->GetGameState<AFGGameState>();
            if (!State || State->GetCheatTurboBuildMode() || GetMutableDefault<AFactoryFPVRaceGate>()->ShouldSkipBuildEffect()) {
                Finish(false,TEXT("Construction-effect qualification requires normal build effects: no turbo-build cheat and no skip-build-effect flag."));return false;
            }
            PendingWireClass.Reset(WireClass.Get());PendingWireRecipe.Reset(WireRecipe.Get());
        }
        // All guards/resources/clearance were checked before the first mutation.
        Source=Selected;
        const bool bVerticalSecond=bVerticalCourseEnabled || bLiveVertical;
        UClass* SecondClass=bVerticalSecond ? AFactoryFPVRaceGateVertical::StaticClass() : AFactoryFPVRacePole::StaticClass();
        UClass* SecondRecipe=bVerticalSecond ? UFactoryFPVRaceGateVerticalRecipe::StaticClass() : UFactoryFPVRacePoleRecipe::StaticClass();
        for (int32 Index=0;Index<2;++Index) {
            const bool bFull=Index==0 && !bLiveVertical;
            const TSubclassOf<AFGBuildable> CheckpointClass=bFull ? AFactoryFPVRaceGateFull::StaticClass() : SecondClass;
            auto* Gate=Cast<AFactoryFPVRaceGate>(Buildables->BeginSpawnBuildable(CheckpointClass,Positions[Index]));
            if (!Gate) { Finish(false,TEXT("Native deferred gate spawn failed."));return false; }
            Gates.Add(Gate);
            Gate->SetBuiltWithRecipe(bFull ? UFactoryFPVRaceGateFullRecipe::StaticClass() : SecondRecipe);
            if (bConstructionEffectsEnabled) {
                if (!Gate->GetRootComponent() || !Gate->GetRootComponent()->IsVisible()) { Finish(false,TEXT("Construction-effect gate root must be visible before initialization."));return false; }
                Gate->SetBuildEffectInstigator(Pioneer.Get());
            }
            Gate->FinishSpawning(Positions[Index]);
            EffectsReady(); // Observe the native effect immediately after BeginPlay.
            FString Error;
            auto Direction=EFactoryFPVRaceGateDirection::Forward;
            if (Index==1 && bVerticalSecond) Direction=EFactoryFPVRaceGateDirection::Descend;
            if (Index==0 && bLiveVertical) Direction=EFactoryFPVRaceGateDirection::Ascend;
            if (!IsValid(Gate) || !Gate->Configure(Course,Index+1,-1,Direction,Error) || !Gate->GetGateId().IsValid()) {
                Finish(false,TEXT("Native gate initialization/configuration failed: ")+Error);return false;
            }
            if (!bConstructionEffectsEnabled && !ConnectGateWire(Gate,WireClass,WireRecipe)) return false;
        }
        bSpawned=true;
        bWired=!bConstructionEffectsEnabled;
        LastReason=bWired ? TEXT("Waiting for real circuit delivery to both gates.") : TEXT("Both gates spawned with real construction instigators; waiting for native effect completion before wiring.");
        Marker(false,TEXT("PENDING"),LastReason,EffectFields());
        return true;
    }
    bool Delivered() {
        if (Gates.Num()!=2 || !Source.IsValid()) { LastReason=TEXT("The QA source or gates are unavailable.");return false; }
        for (int32 Index=0;Index<2;++Index) {
            auto* Gate=Gates[Index].Get();
            if (!Gate) return false;
            auto* Info=Gate->GetPowerInfo();
            auto* Connection=Gate->GetPowerConnection();
            if (!Info || !Connection) { LastReason=TEXT("Gate power components disappeared.");return false; }
            if (Connection->GetPowerInfo()!=Info || !Gate->IsGatePowered() ||
                !FMath::IsNearlyEqual(Info->GetTargetConsumption(),AFactoryFPVRaceGate::PowerConsumptionMW,.005f) ||
                !FMath::IsNearlyEqual(Info->GetActualConsumption(),AFactoryFPVRaceGate::PowerConsumptionMW,.005f) ||
                Connection->GetCircuitID()!=Source->GetCircuitID()) {
                LastReason=FString::Printf(TEXT("Gate %d power pending: powered=%d target=%.6f actual=%.6f connected=%d fuse=%d circuit=%d source-circuit=%d."),
                    Index+1,Gate->IsGatePowered(),Info->GetTargetConsumption(),Info->GetActualConsumption(),
                    Info->IsConnected(),Info->IsFuseTriggered(),Connection->GetCircuitID(),Source->GetCircuitID());
                return false;
            }
            TArray<AFGBuildableWire*> Links;
            Connection->GetWires(Links);
            if (Links.Num()!=1 || !IsValid(Links[0])) return false;
            auto* Wire=Links[0];
            if (!((Wire->GetConnection(0)==Connection && Wire->GetConnection(1)==Source.Get()) ||
                (Wire->GetConnection(1)==Connection && Wire->GetConnection(0)==Source.Get()))) return false;
            if (bVerify) {
                if (Manifest.FindRef(FString::Printf(TEXT("wire%d"),Index+1))!=Wire->GetPathName()) return false;
            } else if (!Wires.IsValidIndex(Index) || Wires[Index].Get()!=Wire) return false;
        }
        FString Error;
        if (!UFactoryFPVRaceComponent::ValidateCourse(World.Get(),Course,Error,true)) { LastReason=Error;return false; }
        return true;
    }
    FString ResultFields() const {
        FString Extra=EffectFields()+CheckpointFields()+FString::Printf(TEXT(",\"course\":97,\"source\":\"%s\",\"sourceCircuit\":%d,\"savedDir\":\"%s\""),
            *Escape(Source.IsValid() ? Source->GetPathName() : FString()),Source.IsValid() ? Source->GetCircuitID() : INDEX_NONE,*Escape(NormalizedPath(FPaths::ProjectSavedDir())));
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        Extra+=FString::Printf(TEXT(",\"layout\":\"%s\",\"pbSeconds\":%.6f"),*Escape(Layout),
            Gates.Num()>0 && Gates[0].IsValid() ? Gates[0]->GetBestLapSeconds(Layout) : 0.);
        const auto SavedSplits=Gates.Num()>0 && Gates[0].IsValid() ? Gates[0]->GetBestLapSplits(Layout,1) : TConstArrayView<double>();
        Extra+=FString::Printf(TEXT(",\"verticalSplitTimingVerified\":%s,\"verticalSplitReloadVerified\":%s,\"verticalSavedSplitSeconds\":%.6f,\"verticalComparedSplitDelta\":%.6f"),
            bVerticalSplitTimingVerified ? TEXT("true") : TEXT("false"),bVerticalSplitReloadVerified ? TEXT("true") : TEXT("false"),
            SavedSplits.IsEmpty() ? 0. : SavedSplits[0],VerifiedSplitDelta);
        Extra+=FString::Printf(TEXT(",\"verticalLayoutRecordsVerified\":%s,\"verticalAlternateBestSeconds\":%.6f,\"verticalAlternateSplitSeconds\":%.6f"),
            bVerticalLayoutRecordsVerified ? TEXT("true") : TEXT("false"),VerifiedAlternateBest,VerifiedAlternateSplit);
        Extra+=FString::Printf(TEXT(",\"verticalClearanceAdvisoryVerified\":%s"),bVerticalClearanceAdvisoryVerified ? TEXT("true") : TEXT("false"));
        for (int32 Index=0;Index<Gates.Num();++Index) if (const auto* Gate=Gates[Index].Get()) {
            const auto* Info=Gate->GetPowerInfo();
            TArray<AFGBuildableWire*> Links;
            Gate->GetPowerConnection()->GetWires(Links);
            const FString WirePath=Links.Num()>0 && IsValid(Links[0]) ? Links[0]->GetPathName() : FString();
            Extra+=FString::Printf(TEXT(",\"gate%d\":{\"id\":\"%s\",\"order\":%d,\"class\":\"%s\",\"recipe\":\"%s\",\"pole\":%s,\"passSide\":%d,\"vertical\":%s,\"direction\":\"%s\",\"instructions\":\"%s\",\"connection\":\"%s\",\"wire\":\"%s\",\"circuit\":%d,\"targetMW\":%.6f,\"actualMW\":%.6f}"),
                Index+1,*Gate->GetGateId().ToString(EGuidFormats::Digits),Gate->GetGateOrder(),*Escape(Gate->GetClass()->GetPathName()),*Escape(GetPathNameSafe(Gate->GetBuiltWithRecipe().Get())),
                Gate->IsPoleCheckpoint() ? TEXT("true") : TEXT("false"),Gate->GetPolePassSide(),Gate->IsVerticalRaceGate() ? TEXT("true") : TEXT("false"),
                DirectionName(Gate->GetRaceDirection()),*Escape(Gate->GetCheckpointInstructions()),*Escape(Gate->GetPowerConnection()->GetPathName()),
                *Escape(WirePath),Gate->GetPowerConnection()->GetCircuitID(),Info->GetTargetConsumption(),Info->GetActualConsumption());
        }
        return Extra;
    }
    FString GhostFingerprint(const FString& Layout) const {
        const auto* Ghost=Gates[0]->GetBestLapGhost(Layout);
        if (!Ghost) return FString();
        const auto& Samples=Ghost->Positions;
        return FString::Printf(TEXT("%d:%08x"),Samples.Num(),
            FCrc::MemCrc32(Samples.GetData(),Samples.Num()*sizeof(FVector3f)));
    }
    bool WriteGateManifest(const FString& Layout,const TCHAR* Variant) const {
        TArray<FString> Lines;
        Lines.Add(TEXT("version=4"));
        Lines.Add(FString(TEXT("variant="))+Variant);
        for (int32 Index=0;Index<2;++Index) {
            Lines.Add(FString::Printf(TEXT("class%d=%s"),Index+1,*Gates[Index]->GetClass()->GetPathName()));
            Lines.Add(FString::Printf(TEXT("recipe%d=%s"),Index+1,*GetPathNameSafe(Gates[Index]->GetBuiltWithRecipe().Get())));
        }
        Lines.Add(FString::Printf(TEXT("direction2=%s"),DirectionName(Gates[1]->GetRaceDirection())));
        Lines.Add(bMilestonePurchased ? TEXT("racingPurchased=1") : TEXT("racingPurchased=0"));
        Lines.Add(TEXT("existingCount=")+FString::FromInt(ExistingGateRows.Num()));
        for (int32 I=0;I<ExistingGateRows.Num();++I) Lines.Add(FString::Printf(TEXT("existing%d=%s"),I,*ExistingGateRows[I]));
        Lines.Add(TEXT("id1=")+Gates[0]->GetGateId().ToString(EGuidFormats::Digits));
        Lines.Add(TEXT("id2=")+Gates[1]->GetGateId().ToString(EGuidFormats::Digits));
        Lines.Add(TEXT("layout=")+Layout);
        if (bVerticalCourseEnabled) {
            const FString Fingerprint=GhostFingerprint(Layout);
            if (Fingerprint.IsEmpty()) return false;
            Lines.Add(TEXT("ghost=")+Fingerprint);
        }
        if (bPracticeCourseName) Lines.Add(TEXT("courseName=")+PracticeObservedCourseName);
        Lines.Add(TEXT("source=")+Source->GetPathName());
        Lines.Add(bPowerCycleEnabled ? TEXT("powerCycleEnabled=1") : TEXT("powerCycleEnabled=0"));
        Lines.Add(bPowerCycleEnabled && PowerCyclePhase==3 ? TEXT("powerCycleVerified=1") : TEXT("powerCycleVerified=0"));
        for (int32 Index=0;Index<2;++Index) Lines.Add(FString::Printf(TEXT("wire%d=%s"),Index+1,*Wires[Index]->GetPathName()));
        return FFileHelper::SaveStringArrayToFile(Lines,*ManifestPath());
    }
    void RequestPracticeSave() {
        auto* Current=World.Get();
        auto* Mode=Current ? Current->GetAuthGameMode<AFGGameMode>() : nullptr;
        auto* Session=SaveSession.Get();
        const auto* System=Current ? UFGSaveSystem::Get(Current) : nullptr;
        FString Existing;
        if (!Mode || !Session || !System || Session->HasSaveGameOperationPending() ||
            NormalizedPath(UFGSaveSystem::CreateAbsolutePath(Current,PracticeName,false))!=PracticePath ||
            IFileManager::Get().FileExists(*PracticePath) || IFileManager::Get().DirectoryExists(*PracticePath) ||
            UFGSaveSystem::GetAbsolutePathForSaveGame(Current,PracticeName,Existing) || System->SaveGameExistsSync(PracticeName) ||
            IFileManager::Get().FileExists(*ManifestPath())) {
            Finish(false,TEXT("Practice generation requires a fresh exact sibling slot and disposable manifest."));return;
        }
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(Current,Course);
        bPracticeFreshRecord=!Layout.IsEmpty() && Gates[0]->GetBestLapSeconds(Layout)==0.;
        if (bPracticeFreshRecord && bPracticeCourseName) {
            auto* Registry=AFactoryFPVRaceCourseRegistry::Get(Current);
            FString Error;
            if (!Registry || !Registry->GetNameForCourse(Course).IsEmpty() || !Registry->SetCourseName(Course,PracticeCourseName,Error)) {
                Finish(false,TEXT("Could not name the exact unused practice course: ")+Error);return;
            }
            PracticeObservedCourseName=Registry->GetNameForCourse(Course);
            bPracticeNameLayoutUnchanged=UFactoryFPVRaceComponent::LayoutSignature(Current,Course).Equals(Layout,ESearchCase::CaseSensitive);
            if (!PracticeObservedCourseName.Equals(PracticeCourseName,ESearchCase::CaseSensitive) || !bPracticeNameLayoutUnchanged) {
                Finish(false,TEXT("Renaming the practice course did not preserve its exact layout signature."));return;
            }
        }
        if (!bPracticeFreshRecord || !WriteGateManifest(Layout,TEXT("practice"))) {
            Finish(false,TEXT("The practice course requires an empty record and an exact gate/wire manifest."));return;
        }
        PracticeMapName=Session->GetLoadedSaveHeader().MapName;
        Mode->SetSaveSessionName(PracticeName);
        if (Mode->GetSaveSessionName()!=PracticeName) {
            Finish(false,TEXT("The native game mode refused the practice session name."));return;
        }
        bSaving=true;
        LastReason=TEXT("Waiting for the asynchronous native practice save.");
        Marker(false,TEXT("PENDING"),LastReason,LiveFields());
        const TWeakPtr<FRun> Weak=AsShared();
        Session->SaveGame(PracticeName,FOnSaveGameComplete::CreateLambda([Weak](bool Success,const FText& Error,void*) {
            const FString Message=Error.ToString();
            AsyncTask(ENamedThreads::GameThread,[Weak,Success,Message] {
                const auto Run=Weak.Pin();
                if (!Run || Run->bDone) return;
                if (!Success || Run->bPracticeSaveCallback) {
                    Run->Finish(false,TEXT("The native practice save callback failed or repeated: ")+Message);return;
                }
                Run->bPracticeSaveCallback=true;
            });
        }),nullptr);
    }
    bool VerifyPracticeSave() {
        const auto* Session=SaveSession.Get();
        const auto* System=World.IsValid() ? UFGSaveSystem::Get(World.Get()) : nullptr;
        if (!Session || !System) { Finish(false,TEXT("The practice save session disappeared."));return false; }
        if (!bPracticeSaveCallback || Session->HasSaveGameOperationPending()) return true;
        FSaveHeader Header;
        FString Actual;
        bPracticeSaveVerified=IFileManager::Get().FileSize(*PracticePath)>0 &&
            UFGSaveSystem::GetAbsolutePathForSaveGame(World.Get(),PracticeName,Actual) && NormalizedPath(Actual)==PracticePath &&
            System->LoadSaveGameHeaderSync(PracticeName,Header) && Header.SaveName.Equals(PracticeName,ESearchCase::CaseSensitive) &&
            Header.SessionName.Equals(PracticeName,ESearchCase::CaseSensitive) && Header.MapName==PracticeMapName;
        PracticeSessionName=Header.SessionName;
        Finish(bPracticeSaveVerified,bPracticeSaveVerified ?
            TEXT("Native practice save completed with its own slot and session, a staged pioneer, and no course best.") :
            TEXT("The native practice save did not resolve with the exact new name, path, map and session."));
        return false;
    }
    void RequestSave() {
        if (bPowerCycleEnabled && PowerCyclePhase!=3) { Finish(false,TEXT("Cannot save before owned-wire power-cycle qualification completes."));return; }
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        if (Layout.IsEmpty()) { Finish(false,TEXT("Course layout is unavailable before save."));return; }
        const auto SavedSplits=Gates[0]->GetBestLapSplits(Layout,1);
        const bool bKnownRecord=bVerticalCourseEnabled ? bVerticalLapScored && bVerticalSplitTimingVerified && !SavedSplits.IsEmpty() &&
            FMath::IsNearlyEqual(SavedSplits[0],KnownCheckpointSplit) && FMath::IsNearlyEqual(Gates[0]->GetBestLapSeconds(Layout),KnownLapSeconds) :
            Gates[0]->RecordLap(KnownLapSeconds,Layout);
        if (!bKnownRecord) { Finish(false,TEXT("Could not verify the known course PB before save."));return; }
        if (!WriteGateManifest(Layout,bVerticalCourseEnabled ? TEXT("vertical") : TEXT("pole"))) {
            Finish(false,TEXT("Could not write disposable QA manifest."));return;
        }
        auto* Session=SaveSession.Get();
        if (!Session) { Finish(false,TEXT("Save session disappeared."));return; }
        bSaving=true;
        Marker(false,TEXT("PENDING"),TEXT("Real native power delivery passed; saving FPV-QA-Roundtrip through SaveGame."),ResultFields());
        TWeakPtr<FRun> Weak=AsShared();
        Session->SaveGame(RoundtripName,FOnSaveGameComplete::CreateLambda([Weak](bool Success,const FText& Error,void*) {
            const FString Message=Error.ToString();
            AsyncTask(ENamedThreads::GameThread,[Weak,Success,Message] {
                if (const auto Run=Weak.Pin(); Run && !Run->bDone)
                    Run->Finish(Success,Success ? TEXT("Real grid delivery and asynchronous save completion verified. Reload roundtrip for persistence verification.") : TEXT("SaveGame callback failed: ")+Message,Run->ResultFields());
            });
        }),nullptr);
    }
    bool FindLoadedSetup() {
        if (Manifest.IsEmpty()) {
            TArray<FString> Lines;
            if (!FFileHelper::LoadFileToStringArray(Lines,*ManifestPath())) { Finish(false,TEXT("Missing disposable before-save manifest."));return false; }
            for (const auto& Line:Lines) { FString Key,Value;if (Line.Split(TEXT("="),&Key,&Value)) Manifest.Add(Key,Value); }
            const TCHAR* Variant=bPractice ? TEXT("practice") : bVerticalCourseEnabled ? TEXT("vertical") : TEXT("pole");
            const TCHAR* Direction=bPractice || bVerticalCourseEnabled ? TEXT("DESCEND") : TEXT("FORWARD");
            if (Manifest.FindRef(TEXT("version"))!=TEXT("4") || Manifest.FindRef(TEXT("layout")).IsEmpty() ||
                Manifest.FindRef(TEXT("variant"))!=Variant || Manifest.FindRef(TEXT("direction2"))!=Direction) {
                Finish(false,TEXT("Invalid or mismatched mixed-checkpoint before-save manifest."));return false;
            }
            if ((Manifest.FindRef(TEXT("racingPurchased"))==TEXT("1"))!=bMilestonePurchased) { Finish(false,TEXT("Racing purchase state changed across save/reload."));return false; }
            if (bPracticeCourseName) {
                const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World.Get());
                if (Registry) PracticeObservedCourseName=Registry->GetNameForCourse(Course);
                bPracticeNameReloaded=Registry && PracticeObservedCourseName.Equals(PracticeCourseName,ESearchCase::CaseSensitive) &&
                    PracticeObservedCourseName.Equals(Manifest.FindRef(TEXT("courseName")),ESearchCase::CaseSensitive);
                if (!bPracticeNameReloaded) {
                    Finish(false,TEXT("The native practice save did not reload its exact course name from the world registry."));return false;
                }
            }
            const bool SavedCycle=Manifest.FindRef(TEXT("powerCycleEnabled"))==TEXT("1");
            if (bPowerCycleEnabled && (!SavedCycle || Manifest.FindRef(TEXT("powerCycleVerified"))!=TEXT("1"))) {
                Finish(false,TEXT("Power-cycle verification requires a roundtrip from a verified power-cycle begin stage."));return false;
            }
            bPowerCycleEnabled=bPowerCycleEnabled || SavedCycle;
        }
        Gates.Reset();
        for (TActorIterator<AFactoryFPVRaceGate> It(World.Get());It;++It)
            if (It->IsLiveRaceGate() && It->GetCourseId()==Course) Gates.Add(*It);
        if (Gates.Num()!=2) { LastReason=TEXT("Roundtrip must restore exactly two QA course gates.");return false; }
        Gates.Sort([](const auto& A,const auto& B) { return A->GetGateOrder()<B->GetGateOrder(); });
        for (int32 Index=0;Index<2;++Index) if (Gates[Index]->GetGateOrder()!=Index+1 ||
            Gates[Index]->GetClass()->GetPathName()!=Manifest.FindRef(FString::Printf(TEXT("class%d"),Index+1)) ||
            GetPathNameSafe(Gates[Index]->GetBuiltWithRecipe().Get())!=Manifest.FindRef(FString::Printf(TEXT("recipe%d"),Index+1)) ||
            Gates[Index]->GetGateId().ToString(EGuidFormats::Digits)!=Manifest.FindRef(FString::Printf(TEXT("id%d"),Index+1))) {
            Finish(false,TEXT("Reloaded gate order, recipe or persistent GUID differs from the saved manifest."));return false;
        }
        if (!GeometryMatches()) { Finish(false,TEXT("Reloaded checkpoint classes, passing side or direction, physical geometry, or label board do not match the fixture."));return false; }
        if (!ObserveExistingGates()) return false;
        const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World.Get(),Course);
        const double ExpectedBest=bPractice ? 0. : KnownLapSeconds;
        if (Layout!=Manifest.FindRef(TEXT("layout")) || !FMath::IsNearlyEqual(Gates[0]->GetBestLapSeconds(Layout),ExpectedBest)) {
            const FString Expected=Manifest.FindRef(TEXT("layout"));
            FString Evidence=ResultFields()+FString::Printf(TEXT(",\"expectedLayout\":\"%s\",\"pbForExpectedLayout\":%.6f"),
                *Escape(Expected),Gates[0]->GetBestLapSeconds(Expected));
            for (int32 Index=0;Index<Gates.Num();++Index) Evidence+=FString::Printf(TEXT(",\"gate%dActorTransform\":\"%s\""),
                Index+1,*Escape(Gates[Index]->GetActorTransform().ToString()));
            Finish(false,TEXT("Reloaded layout signature or expected PB did not survive real save/load."),Evidence);return false;
        }
        if (bPractice) bPracticeFreshRecord=true;
        if (bVerticalCourseEnabled) {
            const FString Fingerprint=GhostFingerprint(Layout);
            if (Fingerprint.IsEmpty() || Fingerprint!=Manifest.FindRef(TEXT("ghost"))) {
                Finish(false,TEXT("The recorded ghost samples did not survive the native world save exactly."),ResultFields());return false;
            }
        }
        if (bPracticeCourseName) bPracticeNameLayoutUnchanged=true;
        Source.Reset();
        for (TActorIterator<AFGBuildable> It(World.Get());It;++It) {
            TInlineComponentArray<UFGPowerConnectionComponent*> Connections(*It);
            for (auto* Connection:Connections) if (Connection && Connection->GetPathName()==Manifest.FindRef(TEXT("source"))) { Source=Connection;break; }
            if (Source.IsValid()) break;
        }
        return Source.IsValid();
    }
    bool Tick(float DeltaSeconds) {
        if (bDone) return false;
        FString Error;
        if (!GuardPaths(Error)) { Finish(false,Error);return false; }
        if (!World.IsValid()) { Finish(false,TEXT("QA world was unloaded."));return false; }
        const double Now=FPlatformTime::Seconds();
        if (Now>Deadline) { Finish(false,TEXT("Shipping QA deadline: ")+LastReason);return false; }
        if (bSaving) return bPractice ? VerifyPracticeSave() : true;
        if (bLivePoseStaged) return TickLiveFlight(DeltaSeconds);
        if (bLiveVertical) {
            if (Now<LiveNextSetupTick) return true;
            LiveNextSetupTick=Now+.25;
        }
        if (!Ready()) return true;
        if (ReadySince==0) ReadySince=FPlatformTime::Seconds();
        if (FPlatformTime::Seconds()-ReadySince<2) return true; // Let initial grid simulation settle.
        if (bVerify) {
            if (FindLoadedSetup() && EffectsReady() && Delivered() && VerifyPowerCycleState()) {
                if (bPractice) {
                    BeginLiveFlight();
                    return !bDone;
                }
                if (bVerticalCourseEnabled && !VerifyReloadedSplitReference()) {
                    Finish(false,TEXT("A fresh lap could not compare against the checkpoint split from the reloaded best."),ResultFields());return false;
                }
                if (bVerticalCourseEnabled && !bVerticalLayoutRecordsVerified && !VerifyVerticalLayoutRecords(false)) {
                    Finish(false,TEXT("The saved Ascend and Descend records did not both survive native reload."),ResultFields());return false;
                }
                bOrdinaryStartPresentationVerified=CheckpointPresentationMatches(0,false);
                if (!bOrdinaryStartPresentationVerified || Gates[1]->IsNextCheckpointHighlighted() ||
                    (bVerticalCourseEnabled && !CheckpointPresentationMatches(1,false))) {
                    LastReason=TEXT("Waiting for ordinary powered start strips and labels without a stale highlight after reload.");return true;
                }
                Finish(true,TEXT("Real roundtrip restored checkpoint classes/recipes/GUIDs/order, physical wire endpoints, grid delivery, ordinary start lights, layout and known API-only PB."),ResultFields());
            }
        } else {
            if (!bSpawned) SpawnSetup();
            if (!bDone && bSpawned && !bWired && EffectsReady()) {
                for (const auto& Gate:Gates) if (!ConnectGateWire(Gate.Get(),PendingWireClass.Get(),PendingWireRecipe.Get())) return false;
                bWired=true;
                Marker(false,TEXT("PENDING"),TEXT("Both real construction effects were observed and completed visibly; native wires connected without a completion shortcut."),EffectFields());
            }
            if (!bDone && bWired && PowerCycleReady() && Delivered()) {
                if (bLiveVertical) BeginLiveFlight();
                else {
                    if (!GeometryMatches()) { Finish(false,TEXT("Native spawned checkpoint classes, passing side or direction, physical geometry, or labels do not match the fixture."));return false; }
                    if (CaptureReady()) RequestSave();
                }
            }
        }
        return !bDone;
    }
};
TWeakPtr<FRun> Active;

void Start(UWorld* World,FOutputDevice& Output,bool Verify) {
    FString Error;
    if (!GuardPaths(Error)) {
        Marker(Verify,TEXT("FAIL"),Error);
        Output.Log(*Error);
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    if (!World || World->GetNetMode()!=NM_Standalone) {
        Marker(Verify,TEXT("FAIL"),TEXT("A standalone game world is required."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin(); Existing && !Existing->bDone) { Output.Log(TEXT("Shipping QA is already running."));return; }
    auto Run=MakeShared<FRun>();
    Run->World=World;
    Run->bVerify=Verify;
    Run->bConstructionEffectsEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAConstructionEffects"));
    Run->bPowerCycleEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAPowerCycle"));
    Run->bVerticalCourseEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAVerticalCourse"));
    const auto* CommandLine=FCommandLine::Get();
    Run->bPractice=FParse::Param(CommandLine,TEXT("FactoryFPVQAVerticalPractice"));
    Run->bPracticeRaceButton=FParse::Param(CommandLine,TEXT("FactoryFPVQAPracticeRaceButton"));
    Run->bPracticeModeButton=FParse::Param(CommandLine,TEXT("FactoryFPVQAPracticeModeButton"));
    Run->bPracticeCourseName=FParse::Param(CommandLine,TEXT("FactoryFPVQAPracticeCourseName"));
    if (Run->bPracticeRaceButton && (!Run->bPractice || !Verify)) {
        Marker(Verify,TEXT("FAIL"),TEXT("Save & race button verification requires the isolated practice reload stage."));
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Run->bPracticeModeButton && !Run->bPracticeRaceButton) {
        Marker(Verify,TEXT("FAIL"),TEXT("Save & practice verification requires the isolated practice reload and the mounted race launch check."));
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Run->bPracticeCourseName && (!Run->bPractice || (Verify && !Run->bPracticeRaceButton))) {
        Marker(Verify,TEXT("FAIL"),TEXT("Course name verification requires isolated practice save and mounted race-button reload."));
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    const bool bLiveOnly=FParse::Param(CommandLine,TEXT("FactoryFPVQALiveVertical"));
    Run->bCheckRaceDebrief=FParse::Param(CommandLine,TEXT("FactoryFPVQARaceDebrief"));
    if (Run->bCheckRaceDebrief && !bLiveOnly) {
        Marker(Verify,TEXT("FAIL"),TEXT("Race debrief verification requires the disposable LiveVertical fixture."));
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    Run->bLiveVertical=Run->bPractice || bLiveOnly;
    if (Run->bLiveVertical && ((bLiveOnly && (Verify || Run->bPractice)) || Run->bVerticalCourseEnabled || Run->bPowerCycleEnabled || Run->bConstructionEffectsEnabled ||
        !FParse::Param(CommandLine,TEXT("NoAutoSave")) || !FParse::Param(CommandLine,TEXT("RenderOffScreen")) ||
        !FParse::Param(CommandLine,TEXT("SavesUseProjectSavedDir")))) {
        Marker(Verify,TEXT("FAIL"),TEXT("Live vertical flight requires an offscreen, autosave-disabled isolated clone and cannot run with other gate fixture options."));
        FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    Run->Deadline=FPlatformTime::Seconds()+(Run->bLiveVertical ? 65. : 45.);
    Active=Run;
    Marker(Verify,TEXT("PENDING"),TEXT("Guarded disposable shipping API QA queued; waiting for the exact loaded save and living pioneer."),Run->EffectFields());
    Output.Log(TEXT("Guarded shipping QA queued. Detailed structured results are in the game log."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float DeltaSeconds) { return Run->Tick(DeltaSeconds); }),
        Run->bLiveVertical ? 0.f : .25f);
}
}

void FFactoryFPVShippingQA::Begin(UWorld* World,FOutputDevice& Output) { FactoryFPVShippingQAInternal::Start(World,Output,false); }
void FFactoryFPVShippingQA::Verify(UWorld* World,FOutputDevice& Output) { FactoryFPVShippingQAInternal::Start(World,Output,true); }
