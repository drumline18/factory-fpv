#include "FactoryFPVShippingBuildGunQA.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRacingContent.h"
#include "FactoryFPVShippingStartup.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGConstructDisqualifier.h"
#include "FGGameMode.h"
#include "FGGameState.h"
#include "FGInventoryComponent.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGPlayerController.h"
#include "FGRecipe.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Buildables/FGBuildableFoundation.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "Hologram/FGHologram.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

namespace FactoryFPVShippingBuildGunQAInternal {
constexpr double TimeLimit=65.;
constexpr double PreviewSettleTime=.25;
constexpr int32 MaxCandidates=64;
constexpr double FloorTileSizeCm=800.;
constexpr double FloorTileDepthCm=100.;
constexpr double FloorTileOpeningHalfCm=360.;
const TCHAR* CloneName=TEXT("FPV-QA-Clone");

FString NormalizedPath(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}
bool Guard(FString& Reason) {
    const TCHAR* CommandLine=FCommandLine::Get();
    if (!FParse::Param(CommandLine,TEXT("FactoryFPVAutomation")) ||
        !FParse::Param(CommandLine,TEXT("RenderOffScreen")) ||
        !FParse::Param(CommandLine,TEXT("NoAutoSave"))) {
        Reason=TEXT("Build Gun QA requires -FactoryFPVAutomation, -RenderOffScreen and -NoAutoSave.");
        return false;
    }
    const FString Saved=NormalizedPath(FPaths::ProjectSavedDir());
    const FString NativeSaveDir=UFGSaveSystem::GetSaveDirectoryPath();
    if (NativeSaveDir.IsEmpty() || !Saved.Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase) ||
        !NormalizedPath(NativeSaveDir).StartsWith(Saved+TEXT("/"),ESearchCase::IgnoreCase)) {
        Reason=TEXT("The actual Satisfactory save directory must be inside the isolated QA Saved directory.");
        return false;
    }
    return true;
}
FString Escape(FString Text) {
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
void Marker(const TCHAR* Result,const FString& Reason,const FString& Evidence=FString()) {
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"buildgun\",\"result\":\"%s\",\"reason\":\"%s\",\"injectedControls\":false,\"savedWorld\":false,\"seededIngredients\":false%s}"),
        Result,*Escape(Reason),*Evidence);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
const TCHAR* JsonBool(bool Value) { return Value ? TEXT("true") : TEXT("false"); }

struct FIngredient {
    TSubclassOf<UFGItemDescriptor> Item;
    int32 Expected=0,InventoryBefore=0,DepotBefore=0,InventoryDelta=0,DepotDelta=0;
};
struct FPlacement {
    FVector Stand;
    FVector Ground;
};
enum class EPhase { Waiting, Aim, Preview, Pressed, Observe, Restore };

struct FRun {
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> Controller;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<AFGBuildGun> Gun;
    TWeakObjectPtr<UFGInventoryComponent> Inventory;
    TWeakObjectPtr<AFGCentralStorageSubsystem> Depot;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> BuildCourseSession;
    TSet<TWeakObjectPtr<AFactoryFPVRaceGate>> ExistingGates;
    TArray<FIngredient> Ingredients;
    TArray<FPlacement> Candidates;
    FTransform OriginalPose;
    FRotator OriginalControl;
    FVector ApprovedLocation=FVector::ZeroVector;
    FVector FloorAnchorTop=FVector::ZeroVector;
    FVector FloorGateTop=FVector::ZeroVector;
    FRotator FloorRotation{0.,27.,0.};
    EPhase Phase=EPhase::Waiting;
    int32 NextCandidate=0,ConstructedCount=0;
    int32 TargetCourse=0,ExpectedOrder=0,BuiltCourse=0,BuiltOrder=0,NextOrder=0;
    double Deadline=FPlatformTime::Seconds()+TimeLimit,PhaseTime=0.;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bMoveIgnored=false,bLookIgnored=false,bOwnedGun=false,bPoseCaptured=false,bPrimaryDown=false;
    bool bRecipeAvailable=false,bEquipped=false,bPreviewValidated=false,bFireDispatched=false;
    bool bPaymentVerified=false,bVerticalVerified=false,bRestored=false,bPaymentSampled=false,bCostMatched=false;
    const bool bCheckBuildCourse=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQABuildCourse"));
    const bool bFloorGate=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAFloorGate"));
    bool bBuildCourseRestored=false,bPreviewConstructionInstigator=false,bPreviewOwnedByBuildGun=false;
    bool bFloorAnchorStaged=false,bFloorAnchorWalkable=false,bFloorSnapVerified=false,bFloorPassageVerified=false;
    bool bFloorStagingRefused=false;
    FString Reason=TEXT("Waiting for the live pioneer in the isolated clone.");
    FString LastFloorPreviewRefusal;

    TSubclassOf<UFGRecipe> Recipe() const {
        return bFloorGate ? UFactoryFPVRaceGateFloorRecipe::StaticClass() : UFactoryFPVRaceGateVerticalRecipe::StaticClass();
    }
    UClass* GateClass() const {
        return bFloorGate ? AFactoryFPVRaceGateFloor::StaticClass() : AFactoryFPVRaceGateVertical::StaticClass();
    }
    FString Evidence() const {
        TArray<FString> Payments;
        for (const auto& Ingredient : Ingredients) {
            Payments.Add(FString::Printf(TEXT("{\"item\":\"%s\",\"expected\":%d,\"inventoryDelta\":%d,\"depotDelta\":%d}"),
                *Escape(Ingredient.Item->GetPathName()),Ingredient.Expected,Ingredient.InventoryDelta,Ingredient.DepotDelta));
        }
        FString Fields=FString::Printf(TEXT(",\"verticalRecipeAvailable\":%s,\"buildGunEquipped\":%s,\"hologramValidated\":%s,\"nativeFireDispatched\":%s,\"exactPaymentVerified\":%s,\"verticalGateVerified\":%s,\"buildGunRestored\":%s,\"constructedCount\":%d,\"payments\":[%s],\"candidatesTried\":%d"),
            JsonBool(bRecipeAvailable),JsonBool(bEquipped),JsonBool(bPreviewValidated),JsonBool(bFireDispatched),
            JsonBool(bPaymentVerified),JsonBool(bVerticalVerified),JsonBool(bRestored),ConstructedCount,*FString::Join(Payments,TEXT(",")),NextCandidate);
        if (bCheckBuildCourse) {
            Fields+=FString::Printf(TEXT(",\"targetBuildCourse\":%d,\"expectedBuildOrder\":%d,\"builtCourse\":%d,\"builtOrder\":%d,\"nextBuildOrder\":%d,\"buildCourseRestored\":%s,\"previewConstructionInstigator\":%s,\"previewOwnedByBuildGun\":%s"),
                TargetCourse,ExpectedOrder,BuiltCourse,BuiltOrder,NextOrder,JsonBool(bBuildCourseRestored),
                JsonBool(bPreviewConstructionInstigator),JsonBool(bPreviewOwnedByBuildGun));
        }
        if (bFloorGate) {
            Fields+=FString::Printf(TEXT(",\"floorAnchorStaged\":%s,\"floorAnchorWalkable\":%s,\"floorFoundationSnapVerified\":%s,\"floorPassageVerified\":%s"),
                JsonBool(bFloorAnchorStaged),JsonBool(bFloorAnchorWalkable),JsonBool(bFloorSnapVerified),JsonBool(bFloorPassageVerified));
        }
        return Fields;
    }
    bool Finish(bool Success,const FString& Message) {
        if (!Success) Restore();
        Marker(Success ? TEXT("PASS") : TEXT("FAIL"),Message,Evidence());
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
        return false;
    }
    void SetPhase(EPhase Next) {
        Phase=Next;
        PhaseTime=FPlatformTime::Seconds();
    }
    bool Ready() {
        auto* Current=World.Get();
        auto* Mode=Current ? Current->GetAuthGameMode<AFGGameMode>() : nullptr;
        auto* Save=Mode ? Mode->GetSaveSession() : nullptr;
        auto* PC=Current ? Cast<AFGPlayerController>(Current->GetFirstPlayerController()) : nullptr;
        auto* Character=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() ||
            !Mode || Mode->IsMainMenuGameMode() || !Save || Save->HasSaveGameOperationPending() || !PC || !PC->GetLocalPlayer() ||
            PC->IsPaused() || !PC->PlayerCameraManager || !Character || !Character->IsAliveAndWell()) return false;
        const auto& Header=Save->GetLoadedSaveHeader();
        if (Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (!Mode->GetLoadData().LoadName.Equals(CloneName,ESearchCase::CaseSensitive) &&
                !Header.SaveName.Equals(CloneName,ESearchCase::CaseSensitive))) {
            Reason=TEXT("The loaded native save identity is not FPV-QA-Clone.");
            return false;
        }
        if (Phase!=EPhase::Waiting) return Pioneer.Get()==Character && Controller.Get()==PC;
        if (!FactoryFPVShippingStartup::Ready(PC->GetGameUI(),Reason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
        Pioneer=Character;
        Controller=PC;
        return true;
    }
    FCollisionQueryParams Query() const {
        return FCollisionQueryParams(SCENE_QUERY_STAT(FactoryFPVBuildGunQA),false,Pioneer.Get());
    }
    bool Floor(const FVector& Above,const FVector& Below,FHitResult& Hit) const {
        return World->LineTraceSingleByChannel(Hit,Above,Below,ECC_WorldStatic,Query()) &&
            Hit.ImpactNormal.Z>=.95 && !Hit.bStartPenetrating;
    }
    bool SafeStand(const FVector& Stand) const {
        auto* Capsule=Pioneer->GetCapsuleComponent();
        auto* Movement=Pioneer->GetCharacterMovement();
        if (!Capsule || !Movement ||
            World->OverlapBlockingTestByChannel(Stand,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),
                FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),
                Query(),FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()))) return false;
        FFindFloorResult Result;
        Movement->FindFloor(Stand,Result,false);
        return Result.IsWalkableFloor();
    }
    void AddCandidates(const FVector& Stand,const FVector& EyeOffset) {
        const FVector Eye=Stand+EyeOffset;
        for (const double Radius : {550.,360.,820.}) {
            for (int32 Index=0;Index<12 && Candidates.Num()<MaxCandidates;++Index) {
                const double Angle=OriginalControl.Yaw*(UE_PI/180.)+Index*(UE_PI/6.);
                const FVector Sample=Stand+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0.);
                FHitResult Ground,LineOfSight;
                if (!Floor(Sample+FVector(0.,0.,250.),Sample-FVector(0.,0.,300.),Ground)) continue;
                if (!World->LineTraceSingleByChannel(LineOfSight,Eye,Ground.ImpactPoint-FVector(0.,0.,5.),ECC_Visibility,Query()) ||
                    FVector::DistSquared(LineOfSight.ImpactPoint,Ground.ImpactPoint)>FMath::Square(35.)) continue;
                Candidates.Add({Stand,Ground.ImpactPoint});
            }
        }
    }
    bool CollectCandidates() {
        const FVector Start=OriginalPose.GetLocation();
        FVector Eye;
        FRotator View;
        Controller->GetPlayerViewPoint(Eye,View);
        const FVector EyeOffset=Eye-Start;
        if (!SafeStand(Start) || EyeOffset.Size()>250.) return false;
        AddCandidates(Start,EyeOffset);
        const double CapsuleHeight=Pioneer->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        for (const double Radius : {750.,1400.,2400.}) {
            for (int32 Index=0;Index<8 && Candidates.Num()<MaxCandidates;++Index) {
                const double Angle=Index*(UE_PI/4.);
                const FVector Sample=Start+FVector(FMath::Cos(Angle)*Radius,FMath::Sin(Angle)*Radius,0.);
                FHitResult Ground;
                if (!Floor(Sample+FVector(0.,0.,350.),Sample-FVector(0.,0.,650.),Ground)) continue;
                const FVector Stand=Ground.ImpactPoint+FVector(0.,0.,CapsuleHeight+2.5);
                if (SafeStand(Stand)) AddCandidates(Stand,EyeOffset);
            }
        }
        return !Candidates.IsEmpty();
    }
    bool StageFloorAnchor() {
        auto* Lightweight=AFGLightweightBuildableSubsystem::Get(World.Get());
        auto* Capsule=Pioneer->GetCapsuleComponent();
        if (!Lightweight || !Capsule) {
            Reason=TEXT("The disposable floor fixture requires the native lightweight subsystem and pioneer capsule.");
            return false;
        }
        if (!SafeStand(OriginalPose.GetLocation())) {
            Reason=TEXT("The disposable pioneer does not have a safe original pose for the floor gate test.");
            return false;
        }
        constexpr const TCHAR* Foundations[][2]={
            {TEXT("/Game/FactoryGame/Buildable/Building/Foundation/Build_Foundation_8x1_01.Build_Foundation_8x1_01_C"),
                TEXT("/Game/FactoryGame/Recipes/Buildings/Foundations/Recipe_Foundation_8x1_01.Recipe_Foundation_8x1_01_C")},
            {TEXT("/Game/FactoryGame/Buildable/Building/Foundation/ConcreteSet/Build_Foundation_Concrete_8x1.Build_Foundation_Concrete_8x1_C"),
                TEXT("/Game/FactoryGame/Buildable/Building/Foundation/ConcreteSet/Recipe_Foundation_Concrete_8x1.Recipe_Foundation_Concrete_8x1_C")}
        };
        TArray<TPair<UClass*,UClass*>> Eligible;
        TArray<FString> Descriptions;
        for (const auto& Paths:Foundations) {
            auto* FoundationClass=LoadClass<AFGBuildableFoundation>(nullptr,Paths[0]);
            auto* FoundationRecipe=LoadClass<UFGRecipe>(nullptr,Paths[1]);
            const auto* Defaults=FoundationClass ? FoundationClass->GetDefaultObject<AFGBuildableFoundation>() : nullptr;
            if (!Defaults || !FoundationRecipe) {
                Descriptions.Add(FString::Printf(TEXT("%s asset=%s recipe=%s"),Paths[0],JsonBool(Defaults!=nullptr),JsonBool(FoundationRecipe!=nullptr)));
                continue;
            }
            if (!FMath::IsNearlyEqual(Defaults->mWidth,800.f,1.f) || !FMath::IsNearlyEqual(Defaults->mDepth,800.f,1.f) ||
                !FMath::IsNearlyEqual(Defaults->mHeight,100.f,1.f) || Defaults->mIsFrame || !FMath::IsNearlyZero(Defaults->mElevation,1.f)) {
                Descriptions.Add(FString::Printf(TEXT("%s is %.1fx%.1fx%.1f cm, frame=%s, elevation=%.1f cm"),
                    *FoundationClass->GetName(),Defaults->mWidth,Defaults->mDepth,Defaults->mHeight,JsonBool(Defaults->mIsFrame),Defaults->mElevation));
                continue;
            }
            Eligible.Emplace(FoundationClass,FoundationRecipe);
        }
        if (Eligible.IsEmpty()) {
            Reason=TEXT("No native flat 8 m by 8 m by 1 m fixture asset resolved. ")+FString::Join(Descriptions,TEXT(" | "));
            return false;
        }
        const FVector Forward=FloorRotation.Vector();
        const FVector TileOffset=Forward*FloorTileSizeCm;
        const FCollisionShape Placement=FCollisionShape::MakeBox(FVector(850.,450.,260.));
        for (const double Height : {650.,1000.,1400.,2000.,2800.}) {
            for (const double Radius : {0.,1200.,2400.}) {
                const int32 Directions=Radius==0. ? 1 : 8;
                for (int32 Index=0;Index<Directions;++Index) {
                    const double Angle=Index*(UE_PI/4.);
                    const FVector Top=OriginalPose.GetLocation()+FVector(Radius*FMath::Cos(Angle),Radius*FMath::Sin(Angle),Height);
                    const FVector BetweenTiles=Top+TileOffset*.5-FVector(0.,0.,35.);
                    if (World->OverlapBlockingTestByChannel(BetweenTiles,FloorRotation.Quaternion(),ECC_WorldStatic,Placement,Query())) continue;
                    const FTransform AnchorPose(FloorRotation,Top-FVector(0.,0.,FloorTileDepthCm*.5));
                    for (const auto& Variant:Eligible) {
                        FFactoryCustomizationData Customization;
                        FRuntimeBuildableInstanceData Anchor(AnchorPose,Customization,Variant.Value,nullptr);
                        if (Lightweight->AddFromBuildableInstanceData(Variant.Key,Anchor)==INDEX_NONE) continue;
                        FloorAnchorTop=Top;
                        FloorGateTop=Top+TileOffset;
                        const FVector Stand=Top+FVector(0.,0.,Capsule->GetScaledCapsuleHalfHeight()+2.5);
                        for (const double Inset : {30.,50.,12.}) {
                            Candidates.Add({Stand,Top+Forward*(FloorTileSizeCm*.5-Inset)});
                        }
                        bFloorAnchorStaged=true;
                        return true;
                    }
                    Reason=TEXT("The native lightweight subsystem refused the eligible foundation assets at a clear fixture location.");
                    return false;
                }
            }
        }
        Reason=FString::Printf(TEXT("No clear two-tile fixture volume was found within 24 m horizontally and 28 m above pioneer %s."),
            *OriginalPose.GetLocation().ToCompactString());
        return false;
    }
    bool VerifyFloorPassage(const AFactoryFPVRaceGate& Gate) const {
        const FTransform Aperture=Gate.GetApertureTransform();
        if (!Gate.IsFloorRaceGate() || !Gate.GetActorLocation().Equals(FloorGateTop,2.) ||
            !Gate.GetActorRotation().Equals(FloorRotation,1.) ||
            !Aperture.GetLocation().Equals(FloorGateTop-FVector(0.,0.,FloorTileDepthCm*.5),2.) ||
            Aperture.GetUnitAxis(EAxis::X).Z<.99 ||
            !Gate.GetApertureHalfExtents().Equals(FVector2D(FloorTileOpeningHalfCm))) return false;
        const FVector Vertical(0.,0.,210.);
        const FVector Center=Aperture.GetLocation();
        const FVector Rim=Center+Gate.GetActorRightVector()*380.;
        FHitResult Hit;
        return !Gate.ProbeVerticalClearance(12.).HasObstruction() &&
            !World->LineTraceSingleByChannel(Hit,Center+Vertical,Center-Vertical,ECC_Visibility,Query()) &&
            World->LineTraceSingleByChannel(Hit,Rim+Vertical,Rim-Vertical,ECC_Visibility,Query()) && Hit.GetActor()==&Gate;
    }
    bool CapturePaymentBaseline() {
        for (auto& Ingredient : Ingredients) {
            Ingredient.InventoryBefore=Inventory->GetNumItems(Ingredient.Item);
            Ingredient.DepotBefore=Depot->GetNumItemsFromCentralStorage(Ingredient.Item);
            if (Ingredient.InventoryBefore+Ingredient.DepotBefore<Ingredient.Expected) {
                Reason=FString::Printf(TEXT("Insufficient existing %s. Inventory %d plus Depot %d requires %d."),
                    *Ingredient.Item->GetName(),Ingredient.InventoryBefore,Ingredient.DepotBefore,Ingredient.Expected);
                return false;
            }
        }
        return true;
    }
    bool PrepareBuildCourse() {
        auto* Session=Controller->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        if (!Session || Session->GetRaceBuildCourse()!=0) {
            Reason=TEXT("Build Gun course QA requires an empty local build target on startup.");
            return false;
        }
        TSet<int32> UsedCourses;
        for (TActorIterator<AFactoryFPVRaceGate> It(World.Get());It;++It) UsedCourses.Add(It->GetCourseId());
        const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World.Get());
        for (int32 Course=AFactoryFPVRaceGate::MaximumCourseId;Course>1;--Course) {
            if (UsedCourses.Contains(Course) || (Registry && !Registry->GetNameForCourse(Course).IsEmpty())) continue;
            TargetCourse=Course;
            Session->SetRaceBuildCourse(Course);
            ExpectedOrder=Session->GetNextRaceBuildOrder();
            if (Session->GetRaceBuildCourse()==Course && ExpectedOrder==1) {
                BuildCourseSession=Session;
                return true;
            }
            Session->SetRaceBuildCourse(0);
            break;
        }
        Reason=TEXT("Build Gun course QA could not select a free custom course starting at checkpoint 1.");
        return false;
    }
    bool Prepare() {
        auto* State=World->GetGameState<AFGGameState>();
        Gun=Pioneer->GetBuildGun();
        Inventory=Gun.IsValid() ? Gun->GetInventory() : nullptr;
        Depot=AFGCentralStorageSubsystem::Get(World.Get());
        if (!Gun.IsValid() || !Inventory.IsValid() || !Depot.IsValid() || !State ||
            Inventory->GetNoBuildCost() || State->GetCheatTurboBuildMode() ||
            Pioneer->IsBuildGunEquipped() || !Pioneer->CanEquipBuildGun() ||
            Controller->GetViewTarget()!=Pioneer.Get() || !Pioneer->GetCharacterMovement() ||
            !Pioneer->GetCharacterMovement()->IsMovingOnGround() || Pioneer->GetVelocity().Size()>5.) {
            Reason=TEXT("Waiting for a grounded pioneer with a native Build Gun, paid inventory and Depot, and normal gameplay view.");
            return false;
        }
        TArray<TSubclassOf<UFGRecipe>> Available;
        TArray<TSubclassOf<UFGCustomizationRecipe>> Customizations;
        Gun->GetAvailableRecipes(Available,Customizations);
        bRecipeAvailable=Available.Contains(Recipe());
        if (!bRecipeAvailable) {
            Reason=TEXT("The native Build Gun does not offer the selected race gate recipe in this clone.");
            return false;
        }
        Ingredients.Reset();
        for (const FItemAmount& Part : UFGRecipe::GetIngredients(World.Get(),Recipe())) {
            if (!Part.ItemClass || Part.Amount<=0) {
                Reason=TEXT("The race gate recipe contains an invalid ingredient.");
                return false;
            }
            Ingredients.Add({Part.ItemClass,Part.Amount});
        }
        if (Ingredients.Num()!=4) {
            Reason=TEXT("The race gate recipe does not have the expected four ingredients.");
            return false;
        }
        if (!CapturePaymentBaseline()) return false;
        OriginalPose=Pioneer->GetActorTransform();
        OriginalControl=Controller->GetControlRotation();
        OriginalView=Controller->GetViewTarget();
        bMoveIgnored=Controller->IsMoveInputIgnored();
        bLookIgnored=Controller->IsLookInputIgnored();
        if (bFloorGate) {
            if (!StageFloorAnchor()) { bFloorStagingRefused=true;return false; }
        } else if (!CollectCandidates()) {
            Reason=TEXT("No safe local stand and unobstructed floor target were found.");return false;
        }
        for (TActorIterator<AFactoryFPVRaceGate> It(World.Get());It;++It) if (It->GetClass()==GateClass()) ExistingGates.Add(*It);
        if (bCheckBuildCourse && !PrepareBuildCourse()) return false;
        bPoseCaptured=true;
        bOwnedGun=true;
        Pioneer->HotKeyRecipe(Recipe());
        Reason=TEXT("Waiting for the native Build Gun and a validated hologram.");
        SetPhase(EPhase::Aim);
        return true;
    }
    bool Aim() {
        while (Candidates.IsValidIndex(NextCandidate)) {
            const FPlacement& Candidate=Candidates[NextCandidate++];
            if (!SafeStand(Candidate.Stand)) continue;
            if (!Pioneer->GetActorLocation().Equals(Candidate.Stand,4.)) {
                if (!Pioneer->SetActorLocationAndRotation(Candidate.Stand,OriginalPose.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics)) continue;
                Pioneer->GetCharacterMovement()->StopMovementImmediately();
            }
            const FVector Eye=Pioneer->GetPawnViewLocation();
            Controller->SetControlRotation((Candidate.Ground-Eye).Rotation());
            SetPhase(EPhase::Preview);
            return true;
        }
        return false;
    }
    bool MatchPreviewCost(UFGBuildGunStateBuild& Build) {
        const auto Cost=Build.GetHologramCost();
        if (Cost.Num()!=Ingredients.Num()) return false;
        for (const auto& Ingredient : Ingredients) {
            if (!Cost.ContainsByPredicate([&](const FItemAmount& Part) {
                return Part.ItemClass==Ingredient.Item && Part.Amount==Ingredient.Expected;
            })) return false;
        }
        return true;
    }
    bool RefusePreview(const TCHAR* Guard,UFGBuildGunStateBuild* Build=nullptr,AFGHologram* Hologram=nullptr) {
        if (!bFloorGate) return false;
        const FHitResult& Hit=Gun->GetHitResult();
        const FVector Target=Candidates[NextCandidate-1].Ground;
        FVector Eye;
        FRotator View;
        Controller->GetPlayerViewPoint(Eye,View);
        LastFloorPreviewRefusal=FString::Printf(TEXT("Floor preview %d rejected at %s. target=[%s] gunHit=%d actor=%s component=%s item=%d hit=[%s] normal=[%s] hitError=%.1f view=[%s] viewRotation=[%s] activeRecipe=%s hologram=%s"),
            NextCandidate,Guard,*Target.ToCompactString(),Hit.bBlockingHit, *GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),Hit.Item,
            *Hit.ImpactPoint.ToCompactString(),*Hit.ImpactNormal.ToCompactString(),FVector::Dist(Hit.ImpactPoint,Target),
            *Eye.ToCompactString(),*View.ToString(),*GetNameSafe(Build ? Build->GetActiveRecipe().Get() : nullptr),*GetNameSafe(Hologram));
        if (Hologram) {
            TArray<TSubclassOf<UFGConstructDisqualifier>> Disqualifiers;
            Hologram->GetConstructDisqualifiers(Disqualifiers);
            TArray<FString> Names;
            for (const auto& Disqualifier:Disqualifiers) Names.Add(GetNameSafe(Disqualifier.Get()));
            LastFloorPreviewRefusal+=FString::Printf(TEXT(" class=%s recipe=%s buildClass=%s canConstruct=%d disabled=%d upgrade=%d pending=%d pose=[%s | %s] expected=[%s | %s] disqualifiers=[%s]"),
                *GetNameSafe(Hologram->GetClass()),*GetNameSafe(Hologram->GetRecipe().Get()),*GetNameSafe(Hologram->GetBuildClass().Get()),
                Hologram->CanConstruct(),Hologram->IsDisabled(),Hologram->IsUpgrade(),Hologram->GetIsPendingToBeConstructed(),
                *Hologram->GetActorLocation().ToCompactString(),*Hologram->GetActorRotation().ToString(),
                *FloorGateTop.ToCompactString(),*FloorRotation.ToString(),*FString::Join(Names,TEXT(",")));
        }
        if (GLog) GLog->Serialize(*LastFloorPreviewRefusal,ELogVerbosity::Display,NAME_None);
        return false;
    }
    bool ValidPreview() {
        if (!Pioneer->IsBuildGunEquipped() || !Gun->IsInState(EBuildGunState::BGS_BUILD)) return RefusePreview(TEXT("equipped build state"));
        bEquipped=true;
        auto* Build=Cast<UFGBuildGunStateBuild>(Gun->GetCurrentState());
        auto* Hologram=Build ? Build->GetHologram() : nullptr;
        if (!Hologram) return RefusePreview(TEXT("missing hologram"),Build);
        if (Build->GetActiveRecipe()!=Recipe() || Hologram->GetRecipe()!=Recipe() || Hologram->GetBuildClass()!=GateClass()) {
            return RefusePreview(TEXT("wrong recipe or build class"),Build,Hologram);
        }
        const FHitResult& Hit=Gun->GetHitResult();
        const FVector Target=Candidates[NextCandidate-1].Ground;
        // The weapon trace need only hit the intended edge region. Exactness
        // is asserted on the resolved tile pose below, independent of aim offset.
        if (!Hit.bBlockingHit || Hit.ImpactNormal.Z<.9 || FVector::DistSquared(Hit.ImpactPoint,Target)>FMath::Square(bFloorGate ? 100. : 200.)) {
            return RefusePreview(TEXT("native hit outside the target face"),Build,Hologram);
        }
        if (Hologram->IsDisabled() || Hologram->IsUpgrade() || Hologram->GetIsPendingToBeConstructed() || !Hologram->CanConstruct()) {
            return RefusePreview(TEXT("hologram placement state"),Build,Hologram);
        }
        if (bFloorGate) {
            bFloorSnapVerified=Hologram->GetActorLocation().Equals(FloorGateTop,2.) && Hologram->GetActorRotation().Equals(FloorRotation,1.);
            if (!bFloorSnapVerified) return RefusePreview(TEXT("foundation snap pose"),Build,Hologram);
        } else if (FVector::DistSquared(Hologram->GetActorLocation(),Target)>FMath::Square(450.)) return false;
        bCostMatched=MatchPreviewCost(*Build);
        if (!bCostMatched) return RefusePreview(TEXT("exact preview cost"),Build,Hologram);
        if (bCheckBuildCourse) {
            bPreviewConstructionInstigator=Hologram->GetConstructionInstigator()==Pioneer.Get();
            bPreviewOwnedByBuildGun=Hologram->GetOwner()==Gun.Get();
        }
        ApprovedLocation=Hologram->GetActorLocation();
        return true;
    }
    void ObserveConstruction() {
        ConstructedCount=0;
        AFactoryFPVRaceGate* Built=nullptr;
        for (TActorIterator<AFactoryFPVRaceGate> It(World.Get());It;++It) {
            if (It->GetClass()==GateClass() && !ExistingGates.Contains(*It)) {
                ++ConstructedCount;
                Built=*It;
            }
        }
        if (ConstructedCount!=1) return;
        if (bFloorGate) bFloorPassageVerified=VerifyFloorPassage(*Built);
        bVerticalVerified=Built->IsVerticalRaceGate() && Built->GetBuiltWithRecipe()==Recipe() && Built->GetGateId().IsValid() &&
            Built->GetRaceDirection()==EFactoryFPVRaceGateDirection::Ascend && Built->GetActorLocation().Equals(ApprovedLocation,35.);
        if (bCheckBuildCourse) {
            BuiltCourse=Built->GetCourseId();
            BuiltOrder=Built->GetGateOrder();
            if (auto* Session=BuildCourseSession.Get()) {
                NextOrder=Session->GetRaceBuildCourse()==TargetCourse ? Session->GetNextRaceBuildOrder() : 0;
            }
        }
        if (bPaymentSampled) return;
        bPaymentSampled=true;
        bPaymentVerified=true;
        for (auto& Ingredient : Ingredients) {
            Ingredient.InventoryDelta=Ingredient.InventoryBefore-Inventory->GetNumItems(Ingredient.Item);
            Ingredient.DepotDelta=Ingredient.DepotBefore-Depot->GetNumItemsFromCentralStorage(Ingredient.Item);
            bPaymentVerified&=Ingredient.InventoryDelta>=0 && Ingredient.DepotDelta>=0 &&
                Ingredient.InventoryDelta+Ingredient.DepotDelta==Ingredient.Expected;
        }
    }
    void Release() {
        if (bPrimaryDown && Gun.IsValid()) Gun->OnPrimaryFireReleased();
        bPrimaryDown=false;
    }
    void Restore() {
        Release();
        if (auto* Session=BuildCourseSession.Get()) {
            Session->SetRaceBuildCourse(0);
            bBuildCourseRestored=Session->GetRaceBuildCourse()==0;
            BuildCourseSession.Reset();
        }
        if (!Pioneer.IsValid() || !Controller.IsValid() || Controller->GetPawn()!=Pioneer.Get()) return;
        if (bOwnedGun && Pioneer->IsBuildGunEquipped()) Pioneer->UnequipBuildGun();
        if (!bPoseCaptured) return;
        if (SafeStand(OriginalPose.GetLocation())) {
            Pioneer->SetActorLocationAndRotation(OriginalPose.GetLocation(),OriginalPose.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
            Pioneer->GetCharacterMovement()->StopMovementImmediately();
        }
        Controller->SetControlRotation(OriginalControl);
    }
    bool Tick() {
        if (FPlatformTime::Seconds()>Deadline) return Finish(false,Reason);
        if (!Ready()) {
            if (Phase==EPhase::Waiting) return true;
            return Finish(false,TEXT("The live pioneer or disposable save changed during native Build Gun QA."));
        }
        if (Phase==EPhase::Waiting) {
            Prepare();
            if (bFloorStagingRefused) return Finish(false,Reason);
            return true;
        }
        if (!Gun.IsValid() || !Inventory.IsValid() || !Depot.IsValid() || Inventory->GetNoBuildCost())
            return Finish(false,TEXT("The native Build Gun or paid inventory became unavailable."));
        const double Elapsed=FPlatformTime::Seconds()-PhaseTime;
        if (Phase==EPhase::Aim) {
            if (bFloorGate && !bFloorAnchorWalkable) {
                FHitResult Hit;
                bFloorAnchorWalkable=Floor(FloorAnchorTop+FVector(0.,0.,25.),FloorAnchorTop-FVector(0.,0.,25.),Hit) &&
                    Hit.ImpactPoint.Equals(FloorAnchorTop,2.) && SafeStand(Candidates[0].Stand);
                if (!bFloorAnchorWalkable) {
                    if (Elapsed<5.) return true;
                    return Finish(false,TEXT("The staged lightweight foundation did not expose a walkable native top surface."));
                }
            }
            if (!Aim()) {
                const FString Detail=FString::Printf(TEXT("None of %d aim targets produced an eligible native race gate hologram. Gun equipped: %s. Exact preview cost matched: %s."),
                    NextCandidate,JsonBool(bEquipped),JsonBool(bCostMatched));
                return Finish(false,LastFloorPreviewRefusal.IsEmpty() ? Detail : Detail+TEXT(" ")+LastFloorPreviewRefusal);
            }
            return true;
        }
        if (Phase==EPhase::Preview) {
            if (Elapsed<(bFloorGate ? .6 : PreviewSettleTime)) return true;
            if (!ValidPreview()) {
                SetPhase(EPhase::Aim);
                return true;
            }
            bPreviewValidated=true;
            if (!CapturePaymentBaseline()) return Finish(false,Reason);
            Reason=TEXT("The native fire handlers did not create the approved race gate.");
            Gun->OnPrimaryFirePressed();
            bPrimaryDown=true;
            bFireDispatched=true;
            ObserveConstruction();
            SetPhase(EPhase::Pressed);
            return true;
        }
        if (Phase==EPhase::Pressed) {
            Release();
            ObserveConstruction();
            SetPhase(EPhase::Observe);
            return true;
        }
        if (Phase==EPhase::Observe) {
            ObserveConstruction();
            if (ConstructedCount>1) return Finish(false,TEXT("Native Build Gun constructed multiple race gates for one input."));
            if ((ConstructedCount==0 || (bFloorGate && !bFloorPassageVerified)) && Elapsed<5.) return true;
            if (!bVerticalVerified || !bPaymentVerified)
                return Finish(false,TEXT("Native construction did not yield the one approved race gate and its exact ingredient debit."));
            if (bFloorGate && !bFloorPassageVerified)
                return Finish(false,TEXT("The built floor tile did not verify its level top, clear center passage and solid rim."));
            Restore();
            SetPhase(EPhase::Restore);
            return true;
        }
        if (Elapsed<PreviewSettleTime) return true;
        ObserveConstruction();
        bRestored=!Pioneer->IsBuildGunEquipped() && Controller->GetViewTarget()==OriginalView.Get() &&
            Pioneer->GetActorLocation().Equals(OriginalPose.GetLocation(),4.) &&
            Controller->GetControlRotation().Equals(OriginalControl,1.) &&
            Controller->IsMoveInputIgnored()==bMoveIgnored && Controller->IsLookInputIgnored()==bLookIgnored;
        if (!bRestored && Elapsed<2.) return true;
        if (!bRestored || ConstructedCount!=1 || !bVerticalVerified || (bFloorGate && !bFloorPassageVerified))
            return Finish(false,TEXT("Build Gun, view or pioneer pose did not restore after exactly one native construction."));
        if (bCheckBuildCourse && (BuiltCourse!=TargetCourse || BuiltOrder!=ExpectedOrder ||
            NextOrder!=ExpectedOrder+1 || !bBuildCourseRestored))
            return Finish(false,TEXT("Native placement did not assign and advance the selected course or clear the temporary build target."));
        return Finish(true,bFloorGate ? TEXT("Native Build Gun placed one floor race gate flush with a lightweight foundation, verified its open passage and rim collision, debited the exact costs and restored the pioneer in the disposable clone.") :
            TEXT("Native Build Gun placed one vertical gate, debited the exact costs and restored the pioneer in the disposable clone."));
    }
};
TWeakPtr<FRun> Active;
}

void FactoryFPVShippingBuildGunQA::Begin(UWorld* World,FOutputDevice& Output) {
    namespace QA=FactoryFPVShippingBuildGunQAInternal;
    FString Reason;
    if (!QA::Guard(Reason) || !World || World->GetNetMode()!=NM_Standalone || QA::Active.IsValid()) {
        if (Reason.IsEmpty()) Reason=TEXT("A standalone world and one Build Gun QA run are required.");
        Output.Log(*Reason);
        QA::Marker(TEXT("FAIL"),Reason);
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    auto Run=MakeShared<QA::FRun>();
    Run->World=World;
    QA::Active=Run;
    QA::Marker(TEXT("PENDING"),TEXT("Waiting for the live pioneer, available paid recipe and a native placement preview."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.08f);
}
