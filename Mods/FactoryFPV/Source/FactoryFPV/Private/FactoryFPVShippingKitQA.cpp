#include "FactoryFPVShippingKitQA.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "Buildables/FGBuildable.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHUD.h"
#include "FGGameUserSettings.h"
#include "FGInventoryComponent.h"
#include "FGInventoryComponentEquipment.h"
#include "FGInventoryLibrary.h"
#include "FGPlayerController.h"
#include "FGRecipeManager.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "FGSchematicManager.h"
#include "FGWorkBench.h"
#include "FGUseableInterface.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "UI/FGManufacturingButton.h"
#include "Animation/Skeleton.h"
#include "Animation/WidgetAnimation.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/Button.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Patching/NativeHookManager.h"
#include "ReferenceSkeleton.h"
#include "SceneView.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Settings/FGUserSetting.h"
#include "Settings/FGUserSettingApplyType.h"
#include "UnrealClient.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"

namespace FactoryFPVShippingKitQAInternal {
constexpr const TCHAR* WorkshopWidgetClassPath=TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/Widget_ManualManufacturing_Container.Widget_ManualManufacturing_Container_C");
constexpr const TCHAR* CloneSaveName=TEXT("FPV-QA-Clone");
constexpr const TCHAR* ProgressionSaveName=TEXT("FPV-QA-Progression");
FString Escape(FString Text) {
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
FString VectorJson(const FVector& Value) {
    return FString::Printf(TEXT("[%.17g,%.17g,%.17g]"),Value.X,Value.Y,Value.Z);
}
FString TransformJson(const FTransform& Value) {
    const FQuat Q=Value.GetRotation();
    return FString::Printf(TEXT("{\"translation\":%s,\"rotationXYZW\":[%.17g,%.17g,%.17g,%.17g],\"scale\":%s}"),
        *VectorJson(Value.GetTranslation()),Q.X,Q.Y,Q.Z,Q.W,*VectorJson(Value.GetScale3D()));
}
FString ComponentJson(const USceneComponent* Component) {
    return FString::Printf(TEXT("{\"path\":\"%s\",\"relative\":%s,\"world\":%s,\"attachParentPath\":\"%s\",\"attachSocket\":\"%s\"}"),
        *Escape(Component->GetPathName()),*TransformJson(Component->GetRelativeTransform()),*TransformJson(Component->GetComponentTransform()),
        *Escape(GetPathNameSafe(Component->GetAttachParent())),*Escape(Component->GetAttachSocketName().ToString()));
}
FString NumberJson(double Value) { return FMath::IsFinite(Value) ? FString::Printf(TEXT("%.17g"),Value) : FString(TEXT("null")); }
FString VariantJson(const FVariant& Value) {
    switch (Value.GetType()) {
        case EVariantTypes::Bool: return Value.GetValue<bool>() ? TEXT("true") : TEXT("false");
        case EVariantTypes::Float: return NumberJson(Value.GetValue<float>());
        case EVariantTypes::Double: return NumberJson(Value.GetValue<double>());
        case EVariantTypes::Int32: return FString::Printf(TEXT("%d"),Value.GetValue<int32>());
        case EVariantTypes::Int64: return FString::Printf(TEXT("%lld"),static_cast<long long>(Value.GetValue<int64>()));
        case EVariantTypes::String: return TEXT("\"")+Escape(Value.GetValue<FString>())+TEXT("\"");
        default: return TEXT("null");
    }
}
FString CVarJson(const TCHAR* Name) {
    const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Name);
    return Variable ? NumberJson(Variable->GetFloat()) : FString(TEXT("null"));
}
bool MatrixJson(const FMatrix& Matrix,FString& Json) {
    Json=TEXT("[");
    for (int32 Row=0;Row<4;++Row) {
        if (Row) Json+=TEXT(",");Json+=TEXT("[");
        for (int32 Column=0;Column<4;++Column) {
            if (!FMath::IsFinite(Matrix.M[Row][Column])) return false;
            if (Column) Json+=TEXT(",");Json+=NumberJson(Matrix.M[Row][Column]);
        }
        Json+=TEXT("]");
    }
    Json+=TEXT("]");return true;
}
FString RectJson(const FIntRect& Rect) {
    return FString::Printf(TEXT("{\"min\":[%d,%d],\"max\":[%d,%d],\"width\":%d,\"height\":%d}"),Rect.Min.X,Rect.Min.Y,Rect.Max.X,Rect.Max.Y,Rect.Width(),Rect.Height());
}
bool RigProjectionPolicy(UWorld* World,APlayerController* Controller,const USkeletalMeshComponent* Mesh,
    const UStaticMeshComponent* Transmitter,FString& Fields,FString& Error) {
    auto* Player=Controller ? Controller->GetLocalPlayer() : nullptr;
    auto* Client=Player ? Player->ViewportClient.Get() : nullptr;
    auto* Camera=Controller ? Controller->PlayerCameraManager.Get() : nullptr;
    FSceneViewProjectionData Projection;
    FString ProjectionMatrix,ViewRotation;
    if (!World || !Player || Player->GetWorld()!=World || !Client || !Client->Viewport || !Camera ||
        !Player->GetProjectionData(Client->Viewport,Projection) || !Projection.IsValidViewRectangle() || Projection.ViewOrigin.ContainsNaN() ||
        !MatrixJson(Projection.ProjectionMatrix,ProjectionMatrix) || !MatrixJson(Projection.ViewRotationMatrix,ViewRotation)) {
        Error=TEXT("Actual local-player viewport projection was unavailable or non-finite.");return false;
    }
    const FMinimalViewInfo& View=Camera->GetCameraCacheView();
    if (!FMath::IsFinite(View.FOV) || !FMath::IsFinite(View.DesiredFOV) || !FMath::IsFinite(View.AspectRatio) ||
        !FMath::IsFinite(View.FirstPersonFOV) || !FMath::IsFinite(View.FirstPersonScale)) {
        Error=TEXT("Current native camera cache contains non-finite projection parameters.");return false;
    }
    const FString Axis=View.AspectRatioAxisConstraint.IsSet() ? FString::Printf(TEXT("%d"),static_cast<int32>(View.AspectRatioAxisConstraint.GetValue())) : FString(TEXT("null"));
    Fields=FString::Printf(TEXT(",\"projection\":{\"viewOrigin\":%s,\"viewRotationMatrix\":%s,\"projectionMatrix\":%s,\"viewRect\":%s,\"constrainedViewRect\":%s,\"localPlayerAxisConstraint\":%d,\"cameraCacheResolvedAxisConstraint\":%d,\"cameraCache\":{\"fov\":%.17g,\"desiredFov\":%.17g,\"aspectRatio\":%.17g,\"axisConstraintOverride\":%s,\"constrainAspectRatio\":%s,\"useFirstPersonParameters\":%s,\"firstPersonFov\":%.17g,\"firstPersonScale\":%.17g}},\"materialPolicy\":{\"transmitterMaterial0\":\"%s\",\"mesh1PMaterials\":["),
        *VectorJson(Projection.ViewOrigin),*ViewRotation,*ProjectionMatrix,*RectJson(Projection.GetViewRect()),*RectJson(Projection.GetConstrainedViewRect()),
        static_cast<int32>(Player->AspectRatioAxisConstraint.GetValue()),static_cast<int32>(View.AspectRatioAxisConstraint.Get(Player->AspectRatioAxisConstraint.GetValue())),
        View.FOV,View.DesiredFOV,View.AspectRatio,*Axis,View.bConstrainAspectRatio ? TEXT("true") : TEXT("false"),View.bUseFirstPersonParameters ? TEXT("true") : TEXT("false"),
        View.FirstPersonFOV,View.FirstPersonScale,*Escape(GetPathNameSafe(Transmitter->GetMaterial(0))));
    for (int32 Index=0;Index<Mesh->GetNumMaterials();++Index) Fields+=FString::Printf(TEXT("%s{\"slot\":%d,\"material\":\"%s\"}"),
        Index ? TEXT(",") : TEXT(""),Index,*Escape(GetPathNameSafe(Mesh->GetMaterial(Index))));
    auto* Collection=LoadObject<UMaterialParameterCollection>(nullptr,TEXT("/Game/FactoryGame/-Shared/Collections/MPC_Panini.MPC_Panini"),nullptr,LOAD_NoWarn|LOAD_Quiet);
    auto* Instance=Collection ? World->GetParameterCollectionInstance(Collection) : nullptr;
    Fields+=FString::Printf(TEXT("],\"paniniCollection\":{\"asset\":\"%s\",\"instance\":\"%s\",\"scalars\":["),*Escape(GetPathNameSafe(Collection)),*Escape(GetPathNameSafe(Instance)));
    if (Collection) for (int32 Index=0;Index<Collection->ScalarParameters.Num();++Index) {
        const auto& Parameter=Collection->ScalarParameters[Index];float Value=0;
        const bool Found=Instance && Instance->GetScalarParameterValue(Parameter.ParameterName,Value);
        Fields+=FString::Printf(TEXT("%s{\"name\":\"%s\",\"default\":%s,\"value\":%s}"),Index ? TEXT(",") : TEXT(""),
            *Escape(Parameter.ParameterName.ToString()),*NumberJson(Parameter.DefaultValue),*(Found ? NumberJson(Value) : FString(TEXT("null"))));
    }
    Fields+=TEXT("]},\"knownCVars\":{");
    const TCHAR* CVars[]={TEXT("FG.FOV"),TEXT("FG.FirstPersonFOVModifier"),TEXT("FG.MaintainYAxisFOV"),TEXT("r.UseLegacyMaintainYFOVViewMatrix")};
    for (int32 Index=0;Index<UE_ARRAY_COUNT(CVars);++Index) Fields+=FString::Printf(TEXT("%s\"%s\":%s"),Index ? TEXT(",") : TEXT(""),CVars[Index],*CVarJson(CVars[Index]));
    auto* Settings=UFGGameUserSettings::GetFGGameUserSettings();
    TMap<FString,TObjectPtr<UFGUserSettingApplyType>> Applied;
    if (IsValid(Settings) && Settings->HasInitialized()) Settings->GetAllUserSettingsMap(Applied);
    TArray<FString> Keys;Applied.GetKeys(Keys);Keys.Sort();
    Fields+=TEXT("},\"appliedNativeOptions\":[");
    const TCHAR* Assets[]={TEXT("US_FieldofView"),TEXT("US_FirstPersonFOVModifier"),TEXT("US_MaintainY-AxisFOVAspectRatio")};
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Assets);++Index) {
        const FString AssetPath=FString::Printf(TEXT("/Game/FactoryGame/Settings/OptionsMenu/Video/%s.%s"),Assets[Index],Assets[Index]);
        UFGUserSettingApplyType* Match=nullptr;FString Key;int32 Matches=0;
        for (const auto& Candidate:Keys) {
            auto* Apply=Applied.FindRef(Candidate).Get();const auto* Setting=IsValid(Apply) ? Apply->GetUserSetting() : nullptr;
            if (IsValid(Setting) && Setting->GetPathName()==AssetPath) { ++Matches;if (!Match) { Match=Apply;Key=Candidate; } }
        }
        const auto* Setting=Match ? Match->GetUserSetting() : nullptr;
        const FVariant Value=Match ? Match->GetAppliedValue() : FVariant();
        Fields+=FString::Printf(TEXT("%s{\"asset\":\"%s\",\"matchingMapEntries\":%d,\"mapKey\":\"%s\",\"apply\":\"%s\",\"strIdMetadata\":\"%s\",\"registeredCvarValue\":%s,\"variantType\":%d,\"appliedValue\":%s}"),
            Index ? TEXT(",") : TEXT(""),*Escape(AssetPath),Matches,*Escape(Key),*Escape(GetPathNameSafe(Match)),*Escape(Setting ? Setting->StrId : FString()),
            *(Setting && !Setting->StrId.IsEmpty() ? CVarJson(*Setting->StrId) : FString(TEXT("null"))),static_cast<int32>(Value.GetType()),*VariantJson(Value));
    }
    Fields+=TEXT("]}");return true;
}
FString ProgressionStage() {
    FString Stage;
    FParse::Value(FCommandLine::Get(),TEXT("-FactoryFPVQAProgression="),Stage);
    return Stage;
}
void Marker(const TCHAR* Result,const FString& Reason,const FString& Extra=FString()) {
    const bool bProgression=ProgressionStage().Equals(TEXT("kit"),ESearchCase::CaseSensitive);
    const TCHAR* Scope=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAWorkshopUI")) ?
        TEXT("real Workshop UMG selection and Slate-held crafting, inventory equip and Primary; excludes physical pointer/E trace, unlock progression, visual fit and manual radio flight") :
        TEXT("timed native workshop production, inventory equip and primary-action hangar backend; excludes Workshop UI, unlock progression, visual fit and manual radio flight");
    const FString EquipmentScope=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAEquipmentFlight")) ?
        TEXT("; additionally no-kit policy, disarmed equipped launch and programmatic native inventory unequip callbacks; excludes physical radio/equipment hand controls") : FString();
    if (bProgression) Scope=TEXT("exact native HUB save reload, real paid Workshop UMG selection and Slate-held crafting without seeds, native inventory equip and public Primary; excludes physical pointer/E trace, visual fit and manual radio flight");
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"%s%s\"%s}"),
        bProgression ? TEXT("progression-kit") : TEXT("kit"),Result,*Escape(Reason),Scope,*EquipmentScope,*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
FString NormalizedPath(FString Path) {
    if (Path.IsEmpty()) return Path;
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}
bool DisposablePath(const FString& Path) {
    return NormalizedPath(Path).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool GuardPaths(FString& Error,bool bProgression=false) {
    if (!FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) {
        Error=TEXT("Missing explicit -FactoryFPVAutomation opt-in.");return false;
    }
    if (!DisposablePath(FPaths::ProjectSavedDir()) || !DisposablePath(UFGSaveSystem::GetSaveDirectoryPath())) {
        Error=TEXT("Both project Saved and actual save directories must be under /build/shipping-world-qa-.");return false;
    }
    if (bProgression) {
        const TCHAR* CommandLine=FCommandLine::Get();
        if (!FParse::Param(CommandLine,TEXT("RenderOffScreen")) || !FParse::Param(CommandLine,TEXT("NoAutoSave")) ||
            !FParse::Param(CommandLine,TEXT("SavesUseProjectSavedDir")) || !FParse::Param(CommandLine,TEXT("FactoryFPVQAWorkshopUI")) ||
            !FParse::Param(CommandLine,TEXT("FactoryFPVQAEquipmentFlight")) || FParse::Param(CommandLine,TEXT("FactoryFPVQAKitPersistence"))) {
            Error=TEXT("Progression kit QA requires off-screen isolated saves with autosave disabled, WorkshopUI and EquipmentFlight, without KitPersistence.");return false;
        }
        const FString Saved=NormalizedPath(FPaths::ProjectSavedDir());
        const FString Run=FPaths::GetPath(Saved);
        const FString RunName=FPaths::GetCleanFilename(Run);
        const FString Prefix=TEXT("shipping-world-qa-");
        FGuid RunId;
        if (!FPaths::GetCleanFilename(Saved).Equals(TEXT("Saved"),ESearchCase::IgnoreCase) ||
            !FPaths::GetCleanFilename(FPaths::GetPath(Run)).Equals(TEXT("build"),ESearchCase::IgnoreCase) ||
            !RunName.StartsWith(Prefix,ESearchCase::IgnoreCase) ||
            !FGuid::ParseExact(RunName.Mid(Prefix.Len()),EGuidFormats::Digits,RunId) || !RunId.IsValid() ||
            !NormalizedPath(UFGSaveSystem::GetSaveDirectoryPath()).StartsWith(Saved+TEXT("/"),ESearchCase::IgnoreCase)) {
            Error=TEXT("Progression kit QA requires build/shipping-world-qa-<guid>/Saved with the native save directory under it.");return false;
        }
    }
    return true;
}

struct FRun : public TSharedFromThis<FRun> {
    enum class EPhase { Waiting,OpeningWorkshop,SettlingWorkshop,CapturingWorkshop,Producing,ClosingWorkshop,AwaitPayment,Equipping,SettlingFirstPerson,CapturingFirstPerson,
        Opening,SettlingHangar,CapturingHangar,ClosingForEquipmentFlight,EquippedFlight,AwaitingNativeUnequip };
    struct FCaptureEvidence {
        FString Path;
        FIntPoint Size=FIntPoint::ZeroValue;
        bool bDecoded=false;
    };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UFGInventoryComponent> Inventory;
    TWeakObjectPtr<UFGInventoryComponentEquipment> Arms;
    TWeakObjectPtr<AFGCentralStorageSubsystem> Depot;
    TWeakObjectPtr<UFGWorkBench> Bench;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<UFGGameUI> GameUI;
    TWeakObjectPtr<UFGInteractWidget> WorkshopUI;
    TWeakObjectPtr<UUserWidget> ManufacturingUI;
    TWeakObjectPtr<UFGManufacturingButton> ManufacturingButton;
    TWeakObjectPtr<UButton> CraftButton;
    TWeakPtr<SButton> HeldSlateButton;
    FGeometry HeldGeometry;
    FVector2D HeldPosition=FVector2D::ZeroVector;
    TWeakObjectPtr<AFGCharacterPlayer> OldBenchUser;
    TWeakObjectPtr<UFGInventoryComponent> OldBenchInventory;
    TSubclassOf<UFGRecipe> OldRecipe;
    TArray<FItemAmount> Ingredients;
    TArray<int32> BeforeIngredients;
    TArray<int32> SeededIngredients;
    TArray<int32> BaselineIngredients;
    TArray<int32> BeforeDepotIngredients;
    TArray<int32> BaselineDepotIngredients;
    TArray<int32> PaymentLocalIngredients;
    TArray<int32> PaymentDepotIngredients;
    TArray<int32> BeforeRadioIndices;
    EPhase Phase=EPhase::Waiting;
    double Deadline=0;
    double NextSearch=0;
    double LastProduce=0;
    double PhaseStarted=0;
    float OldProgress=0;
    int32 BeforeKits=0;
    int32 ArmIndex=INDEX_NONE;
    int32 PreviousActive=INDEX_NONE;
    int32 ReturnIndex=INDEX_NONE;
    int32 ProduceCalls=0;
    bool bRecipeAvailable=false;
    bool bProducerAvailable=false;
    bool bBenchTouched=false;
    bool bMoved=false;
    bool bOpened=false;
    bool bPrimarySent=false;
    bool bCraftedExactlyOne=false;
    bool bPaymentVerified=false;
    bool bNoBuildCostBefore=false;
    bool bNativeEquipVerified=false;
    double CraftSeconds=0;
    double PaymentWaitSeconds=0;
    float ProduceProgressBefore=0;
    float ProduceProgressAfter=0;
    float NativeCraftDuration=0;
    float NativeManufacturingSpeed=0;
    bool bCaptureEnabled=false;
    bool bCameraModeChanged=false;
    ECameraMode OldCameraMode=ECameraMode::ECM_None;
    uint64 SettleFrame=0;
    FCaptureEvidence FirstPersonCapture;
    FCaptureEvidence HangarCapture;
    FCaptureEvidence WorkshopCapture;
    bool bWorkshopUI=false;
    bool bWorkshopOpened=false;
    bool bWorkshopSelected=false;
    bool bWorkshopButtonEnabled=false;
    bool bWorkshopButtonHeld=false;
    bool bWorkshopButtonReleased=false;
    bool bWorkshopClosed=false;
    bool bWorkshopProduced=false;
    bool bWorkshopButtonHitTestVerified=false;
    bool bWorkshopStartupSplashFinished=false;
    bool bWorkshopSplashObserved=false;
    bool bWorkshopSplashAnimationObserved=false;
    bool bWorkshopFrontEndFinished=false;
    bool bWorkshopSplashInstancePresent=false;
    bool bWorkshopSplashMounted=false;
    bool bWorkshopSplashVisible=false;
    bool bWorkshopSplashAnimationPlaying=false;
    float WorkshopSplashOpacity=0.f;
    FString WorkshopStartupReason=TEXT("Not checked.");
    bool bWorkshopHitWindowFound=false;
    int32 WorkshopHitPathDepth=0;
    int32 WorkshopHitStableSamples=0;
    float WorkshopMinimumRenderOpacity=1.f;
    uint64 WorkshopLastHitFrame=MAX_uint64;
    FString WorkshopHitPath;
    FString WorkshopHitReason=TEXT("Not checked.");
    bool bWorkshopPressSent=false;
    bool bWorkshopCloseSent=false;
    FString RigExportPath;
    int32 RigBoneCount=0;
    FDelegateHandle ScreenshotProcessedHandle;
    FString PendingScreenshot;
    double CaptureStarted=0;
    bool bOwnScreenshotPending=false;
    bool bScreenshotProcessed=false;
    bool bScreenMessagesBeforeCapture=false;
    bool bDone=false;
    FString LastReason=TEXT("Waiting for the exact loaded clone and living pioneer.");
    bool bProgression=false,bProgressionMilestonePurchased=false;
    FString LoadedProgressionSavePath;
    FDelegateHandle WorkshopPaymentBeforeHandle,WorkshopPaymentAfterHandle;
    int32 WorkshopPaymentTransactionCalls=0;
    int32 WorkshopPaymentKitsBefore=-1;
    bool bWorkshopPaymentTransactionVerified=false;
    TArray<int32> PaymentBeforeLocalIngredients,PaymentBeforeDepotIngredients;
    bool bEquipmentFlight=false,bPolicySnapshot=false,bPreviousSandboxPolicy=false,bSandboxPolicyRestored=false;
    bool bNoKitLaunchRefused=false,bNoKitSettingsAccessible=false,bEquippedFlightLaunched=false;
    bool bNativeUnequipReturned=false,bEquipmentFlightStateRestored=false,bNativeEquipmentEmpty=false,bOriginalEquipmentReactivated=false,bNativeInventoryMove=false;
    bool bEquipmentInputsRestored=false,bEquipmentSourcesRetired=false,bEquipmentBodyRestored=false;
    FString NoKitError;
    TWeakObjectPtr<AFactoryFPVDrone> EquipmentDrone;
    TWeakObjectPtr<AFactoryFPVRadioEquipment> FlightRadio;
    TWeakObjectPtr<AFGEquipment> OriginalArmsEquipment;
    struct FInputSnapshot { TWeakObjectPtr<UInputComponent> Component;bool bMember=false;bool bRadioOwned=false;bool bOriginalEquipmentOwned=false; };
    struct FBodySnapshot { TWeakObjectPtr<UPrimitiveComponent> Component;bool bHidden=false;bool bVisible=false;bool bHiddenShadow=false; };
    TArray<FInputSnapshot> EquipmentInputs;
    TArray<FBodySnapshot> EquipmentBody;
    TArray<TWeakObjectPtr<UInputComponent>> EquipmentSessionInputs;
    TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> EquipmentSources;
    TWeakObjectPtr<AActor> EquipmentView;
    TWeakObjectPtr<APlayerCameraManager> EquipmentCameraManager;
    TWeakObjectPtr<AFGHUD> EquipmentHud;
    FVector EquipmentPioneerLocation=FVector::ZeroVector;
    FRotator EquipmentPioneerRotation=FRotator::ZeroRotator;
    EMovementMode EquipmentMovement=MOVE_None;uint8 EquipmentCustomMovement=0;
    float EquipmentFov=0,EquipmentLockedFov=0;
    bool bEquipmentIgnoreMove=false,bEquipmentIgnoreLook=false,bEquipmentCursor=false,bEquipmentViewportIgnore=false;
    bool bEquipmentPartialHud=false,bEquipmentHudVisible=false,bEquipmentShowHud=false;
    uint64 EquipmentPhaseFrame=0;
    int32 NativeEquipmentInputChanges=0;
    bool bPreparationOnly=false,bPreparationHandedOff=false;
    int32 NativeEquipActions=0;
    TFunction<void(bool,const FString&,const FString&)> PreparationCompletion;

    FString Counts() const {
        const auto* Inv=Inventory.Get();
        FString Extra=FString::Printf(TEXT(",\"craftedExactlyOne\":%s,\"nativeEquipVerified\":%s,\"craftSeconds\":%.3f,\"recipeAvailableBeforeFixture\":%s,\"workshopRecipeListedBeforeFixture\":%s,\"kitsBefore\":%d,\"kitsInInventoryNow\":%d,\"produceCalls\":%d,\"armIndex\":%d,\"primaryOpenedHangar\":%s,\"workshop\":\"%s\""),
            bCraftedExactlyOne ? TEXT("true") : TEXT("false"),bNativeEquipVerified ? TEXT("true") : TEXT("false"),CraftSeconds,
            bRecipeAvailable ? TEXT("true") : TEXT("false"),bProducerAvailable ? TEXT("true") : TEXT("false"),
            BeforeKits,Inv ? Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass()) : -1,ProduceCalls,ArmIndex,
            bOpened ? TEXT("true") : TEXT("false"),*Escape(Bench.IsValid() ? Bench->GetPathName() : FString()));
        Extra+=FString::Printf(TEXT(",\"captureEnabled\":%s,\"firstPersonCapture\":{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s},\"hangarCapture\":{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s}"),
            bCaptureEnabled ? TEXT("true") : TEXT("false"),*Escape(FirstPersonCapture.Path),FirstPersonCapture.Size.X,FirstPersonCapture.Size.Y,
            FirstPersonCapture.bDecoded ? TEXT("true") : TEXT("false"),*Escape(HangarCapture.Path),HangarCapture.Size.X,HangarCapture.Size.Y,
            HangarCapture.bDecoded ? TEXT("true") : TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"rigExportPath\":\"%s\",\"rigBoneCount\":%d"),*Escape(RigExportPath),RigBoneCount);
        Extra+=FString::Printf(TEXT(",\"equipmentFlightEnabled\":%s,\"noKitLaunchRefused\":%s,\"noKitSettingsAccessible\":%s,\"equippedFlightLaunched\":%s,\"nativeUnequipReturned\":%s,\"equipmentFlightStateRestored\":%s,\"sandboxPolicyRestored\":%s,\"noKitLaunchError\":\"%s\",\"nativeEquipmentEmpty\":%s,\"originalEquipmentReactivated\":%s,\"equipmentInputsRestored\":%s,\"equipmentSourcesRetired\":%s,\"equipmentBodyRestored\":%s,\"equipmentActiveIndexNow\":%d,\"equipmentActorNow\":\"%s\",\"equipmentFlightDisarmedOnly\":true,\"programmaticInventoryUnequip\":%s,\"savedSettings\":false"),
            bEquipmentFlight?TEXT("true"):TEXT("false"),bNoKitLaunchRefused?TEXT("true"):TEXT("false"),bNoKitSettingsAccessible?TEXT("true"):TEXT("false"),
            bEquippedFlightLaunched?TEXT("true"):TEXT("false"),bNativeUnequipReturned?TEXT("true"):TEXT("false"),bEquipmentFlightStateRestored?TEXT("true"):TEXT("false"),
            bSandboxPolicyRestored?TEXT("true"):TEXT("false"),*Escape(NoKitError),bNativeEquipmentEmpty?TEXT("true"):TEXT("false"),bOriginalEquipmentReactivated?TEXT("true"):TEXT("false"),
            bEquipmentInputsRestored?TEXT("true"):TEXT("false"),bEquipmentSourcesRetired?TEXT("true"):TEXT("false"),bEquipmentBodyRestored?TEXT("true"):TEXT("false"),
            Arms.IsValid()?Arms->GetActiveIndex():INDEX_NONE,*Escape(GetPathNameSafe(Pioneer.IsValid()?Pioneer->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS):nullptr)),bNativeInventoryMove?TEXT("true"):TEXT("false"));
        if (bEquipmentFlight) {
            const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller.Get());auto* Manager=Controller.IsValid()?Controller->PlayerCameraManager.Get():nullptr;
            Extra+=FString::Printf(TEXT(",\"equipmentState\":{\"expectedView\":\"%s\",\"currentView\":\"%s\",\"sourceArmsEquipment\":\"%s\",\"pioneerDeltaCm\":%s,\"expectedFov\":%s,\"currentFov\":%s,\"expectedFovLock\":%s,\"currentFovLock\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"sessionControls\":%d,\"sessionSources\":%d,\"nativeEquipmentInputChanges\":%d,\"membershipScope\":\"non-equipment members unchanged, radio-owned members removed; only actual original-equipment reactivation may change its own native members\"}"),
                *Escape(GetPathNameSafe(EquipmentView.Get())),*Escape(GetPathNameSafe(Controller.IsValid()?Controller->GetViewTarget():nullptr)),*Escape(GetPathNameSafe(OriginalArmsEquipment.Get())),
                *NumberJson(Pioneer.IsValid()?FVector::Distance(Pioneer->GetActorLocation(),EquipmentPioneerLocation):-1.),*NumberJson(EquipmentFov),*NumberJson(Manager?Manager->GetFOVAngle():-1.),
                *NumberJson(EquipmentLockedFov),*NumberJson(Manager?Manager->GetLockedFOV():-1.),Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions,
                EquipmentSessionInputs.Num(),EquipmentSources.Num(),NativeEquipmentInputChanges);
        }
        Extra+=FString::Printf(TEXT(",\"workshopUIEnabled\":%s,\"workshopUIOpened\":%s,\"workshopRecipeSelected\":%s,\"workshopButtonEnabled\":%s,\"workshopButtonHeld\":%s,\"workshopButtonReleased\":%s,\"workshopUIClosed\":%s,\"workshopUIProduced\":%s,\"workshopCapture\":{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s}"),
            bWorkshopUI ? TEXT("true") : TEXT("false"),bWorkshopOpened ? TEXT("true") : TEXT("false"),bWorkshopSelected ? TEXT("true") : TEXT("false"),
            bWorkshopButtonEnabled ? TEXT("true") : TEXT("false"),bWorkshopButtonHeld ? TEXT("true") : TEXT("false"),bWorkshopButtonReleased ? TEXT("true") : TEXT("false"),
            bWorkshopClosed ? TEXT("true") : TEXT("false"),bWorkshopProduced ? TEXT("true") : TEXT("false"),*Escape(WorkshopCapture.Path),
            WorkshopCapture.Size.X,WorkshopCapture.Size.Y,WorkshopCapture.bDecoded ? TEXT("true") : TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"workshopButtonHitTestVerified\":%s,\"workshopHitWindowFound\":%s,\"workshopHitPathDepth\":%d,\"workshopHitStableSamples\":%d,\"workshopMinimumRenderOpacity\":%.6f,\"workshopHitPath\":\"%s\",\"workshopHitReason\":\"%s\""),
            bWorkshopButtonHitTestVerified ? TEXT("true") : TEXT("false"),bWorkshopHitWindowFound ? TEXT("true") : TEXT("false"),
            WorkshopHitPathDepth,WorkshopHitStableSamples,WorkshopMinimumRenderOpacity,*Escape(WorkshopHitPath),*Escape(WorkshopHitReason));
        Extra+=FString::Printf(TEXT(",\"workshopStartupSplashFinished\":%s,\"workshopFrontEndFinished\":%s,\"workshopSplashInstancePresent\":%s,\"workshopSplashMounted\":%s,\"workshopSplashVisible\":%s,\"workshopSplashAnimationPlaying\":%s,\"workshopSplashOpacity\":%.6f,\"workshopStartupReason\":\"%s\""),
            bWorkshopStartupSplashFinished ? TEXT("true") : TEXT("false"),bWorkshopFrontEndFinished ? TEXT("true") : TEXT("false"),
            bWorkshopSplashInstancePresent ? TEXT("true") : TEXT("false"),bWorkshopSplashMounted ? TEXT("true") : TEXT("false"),
            bWorkshopSplashVisible ? TEXT("true") : TEXT("false"),bWorkshopSplashAnimationPlaying ? TEXT("true") : TEXT("false"),
            WorkshopSplashOpacity,*Escape(WorkshopStartupReason));
        Extra+=FString::Printf(TEXT(",\"workshopStartupSplashObserved\":%s"),bWorkshopSplashObserved ? TEXT("true") : TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"workshopStartupSplashAnimationObserved\":%s"),bWorkshopSplashAnimationObserved ? TEXT("true") : TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"paymentVerified\":%s,\"paymentWaitSeconds\":%.3f,\"noBuildCostBefore\":%s,\"noBuildCostNow\":%s,\"centralStorageAvailable\":%s,\"produceProgressBeforeLastCall\":%.6f,\"produceProgressAfterLastCall\":%.6f,\"nativeCraftDuration\":%.6f,\"nativeManufacturingSpeed\":%.6f,\"ingredients\":["),
            bPaymentVerified ? TEXT("true") : TEXT("false"),PaymentWaitSeconds,bNoBuildCostBefore ? TEXT("true") : TEXT("false"),
            Inv && Inv->GetNoBuildCost() ? TEXT("true") : TEXT("false"),Depot.IsValid() ? TEXT("true") : TEXT("false"),
            FMath::IsFinite(ProduceProgressBefore) ? ProduceProgressBefore : 0.f,FMath::IsFinite(ProduceProgressAfter) ? ProduceProgressAfter : 0.f,
            FMath::IsFinite(NativeCraftDuration) ? NativeCraftDuration : 0.f,FMath::IsFinite(NativeManufacturingSpeed) ? NativeManufacturingSpeed : 0.f);
        for (int32 Index=0;Index<Ingredients.Num();++Index) {
            if (Index) Extra+=TEXT(",");
            Extra+=FString::Printf(TEXT("{\"item\":\"%s\",\"required\":%d,\"before\":%d,\"seeded\":%d,\"baseline\":%d,\"now\":%d,\"depotBefore\":%d,\"depotBaseline\":%d,\"depotNow\":%d,\"localAtPayment\":%d,\"depotAtPayment\":%d"),
                *Escape(Ingredients[Index].ItemClass ? Ingredients[Index].ItemClass->GetPathName() : FString()),Ingredients[Index].Amount,
                BeforeIngredients.IsValidIndex(Index) ? BeforeIngredients[Index] : -1,
                SeededIngredients.IsValidIndex(Index) ? SeededIngredients[Index] : 0,
                BaselineIngredients.IsValidIndex(Index) ? BaselineIngredients[Index] : -1,
                Inv && Ingredients[Index].ItemClass ? Inv->GetNumItems(Ingredients[Index].ItemClass) : -1,
                BeforeDepotIngredients.IsValidIndex(Index) ? BeforeDepotIngredients[Index] : -1,
                BaselineDepotIngredients.IsValidIndex(Index) ? BaselineDepotIngredients[Index] : -1,
                Depot.IsValid() && Ingredients[Index].ItemClass ? Depot->GetNumItemsFromCentralStorage(Ingredients[Index].ItemClass) : 0,
                PaymentLocalIngredients.IsValidIndex(Index) ? PaymentLocalIngredients[Index] : -1,
                PaymentDepotIngredients.IsValidIndex(Index) ? PaymentDepotIngredients[Index] : -1);
            if (bProgression) Extra+=FString::Printf(TEXT(",\"localBeforePayment\":%d,\"depotBeforePayment\":%d"),
                PaymentBeforeLocalIngredients.IsValidIndex(Index) ? PaymentBeforeLocalIngredients[Index] : -1,
                PaymentBeforeDepotIngredients.IsValidIndex(Index) ? PaymentBeforeDepotIngredients[Index] : -1);
            Extra+=TEXT("}");
        }
        Extra+=FString::Printf(TEXT("],\"preparationOnly\":%s,\"preparationHandedOff\":%s,\"prepareCalls\":%d,\"radioGrantCalls\":0,\"nativeEquipActions\":%d,\"primaryActionDispatches\":%d"),
            bPreparationOnly?TEXT("true"):TEXT("false"),bPreparationHandedOff?TEXT("true"):TEXT("false"),bPreparationOnly?1:0,NativeEquipActions,bPrimarySent?2:0);
        if (bProgression) {
            const bool bHandoffLoaded=!LoadedProgressionSavePath.IsEmpty();
            Extra+=FString::Printf(TEXT(",\"progressionHandoffLoaded\":%s,\"progressionSaveName\":\"%s\",\"progressionSavePath\":\"%s\",\"progressionMilestonePurchased\":%s,\"workshopPaymentTransactionVerified\":%s,\"workshopPaymentTransactionCalls\":%d,\"savedWorld\":false"),
                bHandoffLoaded ? TEXT("true") : TEXT("false"),bHandoffLoaded ? ProgressionSaveName : TEXT(""),
                *Escape(LoadedProgressionSavePath),bProgressionMilestonePurchased ? TEXT("true") : TEXT("false"),
                bWorkshopPaymentTransactionVerified ? TEXT("true") : TEXT("false"),WorkshopPaymentTransactionCalls);
        }
        return Extra;
    }
    template<class T> static T* ReadObject(UObject* Object,const TCHAR* Name) {
        const auto* Property=Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(),Name) : nullptr;
        return Property ? Cast<T>(Property->GetObjectPropertyValue_InContainer(Object)) : nullptr;
    }
    bool VerifyWorkshopStartupSplash() {
        bWorkshopStartupSplashFinished=false;bWorkshopFrontEndFinished=false;
        bWorkshopSplashInstancePresent=false;bWorkshopSplashMounted=false;
        bWorkshopSplashVisible=false;bWorkshopSplashAnimationPlaying=false;WorkshopSplashOpacity=0.f;
        auto Wait=[&](const TCHAR* Reason) {
            WorkshopStartupReason=Reason;
            LastReason=TEXT("Waiting for actual HUD startup presentation: ")+WorkshopStartupReason;
            return false;
        };
        auto* UI=GameUI.Get();
        if (!UI) return Wait(TEXT("Real GameUI is unavailable."));
        bWorkshopFrontEndFinished=UI->IsFrontEndLoadingScreenFinished();
        const auto* Field=FindFProperty<FObjectPropertyBase>(UI->GetClass(),TEXT("mFicsitLogoSplashWidget"));
        const FString Expected=TEXT("/Game/FactoryGame/Interface/UI/InGame/Widget_FicsitLogoSplash.Widget_FicsitLogoSplash_C");
        if (!Field || !Field->PropertyClass || Field->PropertyClass->GetPathName()!=Expected) {
            Finish(false,FString::Printf(TEXT("Actual GameUI startup-splash field/class differs from known mFicsitLogoSplashWidget schema (actual %s); readiness cannot be inferred."),
                Field && Field->PropertyClass ? *Field->PropertyClass->GetPathName() : TEXT("missing")));return false;
        }
        auto* Object=Field->GetObjectPropertyValue_InContainer(UI);
        if (!Object) {
            if (!bWorkshopFrontEndFinished && !bWorkshopSplashObserved)
                return Wait(TEXT("Direct-map startup has not signaled front-end completion; waiting to observe the actual startup splash lifecycle."));
            bWorkshopStartupSplashFinished=true;
            WorkshopStartupReason=bWorkshopFrontEndFinished ?
                TEXT("Front-end loading finished and the known startup-splash field is empty.") :
                TEXT("The actual startup splash was observed and has now been removed; direct-map front-end flag remains false.");
            return true;
        }
        auto* Splash=Cast<UUserWidget>(Object);
        bWorkshopSplashInstancePresent=true;
        if (!Splash || Splash->GetClass()->GetPathName()!=Expected) {
            Finish(false,TEXT("Actual HUD startup splash instance has an unexpected class."));return false;
        }
        bWorkshopSplashObserved=true;
        const auto* AnimationField=FindFProperty<FObjectPropertyBase>(Splash->GetClass(),TEXT("SpawnAnim"));
        if (!AnimationField || !AnimationField->PropertyClass || !AnimationField->PropertyClass->IsChildOf(UWidgetAnimation::StaticClass())) {
            Finish(false,TEXT("Actual HUD startup splash lacks the known SpawnAnim animation property."));return false;
        }
        auto* Animation=Cast<UWidgetAnimation>(AnimationField->GetObjectPropertyValue_InContainer(Splash));
        if (!Animation) return Wait(TEXT("Known startup splash has not initialized its SpawnAnim instance."));
        bWorkshopSplashAnimationPlaying=Splash->IsAnimationPlaying(Animation);
        if (bWorkshopSplashAnimationPlaying) bWorkshopSplashAnimationObserved=true;
        bool Visible=Splash->IsVisible();
        float Opacity=Splash->GetRenderOpacity()*Splash->GetColorAndOpacity().A;
        auto Slate=Splash->GetCachedWidget();
        if (Slate && FSlateApplication::IsInitialized()) {
            bWorkshopSplashMounted=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef()).IsValid();
            for (int32 Depth=0;Slate && Depth<128;++Depth) {
                Visible=Visible && Slate->GetVisibility().IsVisible();
                Opacity*=Slate->GetRenderOpacity();
                Slate=Slate->GetParentWidget();
            }
            if (Slate.IsValid()) return Wait(TEXT("Startup splash Slate ancestry could not be resolved safely."));
        }
        bWorkshopSplashMounted=bWorkshopSplashMounted || Splash->IsInViewport();
        bWorkshopSplashVisible=Visible;
        WorkshopSplashOpacity=FMath::IsFinite(Opacity) ? Opacity : 1.f;
        if (bWorkshopSplashAnimationPlaying)
            return Wait(TEXT("Known FICSIT/welcome startup SpawnAnim is still playing."));
        // The splash keeps its root Canvas mounted and opaque after its child
        // graphics fade. A witnessed playing -> stopped transition of the exact
        // SpawnAnim is the completion contract; root visibility is diagnostic.
        if (!bWorkshopSplashAnimationObserved && bWorkshopSplashMounted && Visible && WorkshopSplashOpacity>0.f)
            return Wait(TEXT("Mounted startup splash has not yet completed an observed SpawnAnim playback."));
        bWorkshopStartupSplashFinished=true;
        WorkshopStartupReason=TEXT("Known startup SpawnAnim was observed playing and has stopped, or the splash is not presented; root canvas and front-end flag are recorded separately.");
        return true;
    }
    bool WorkshopIsOwnedAndOpen() const {
        auto* UI=GameUI.Get();auto* Widget=WorkshopUI.Get();auto* Work=Bench.Get();
        return UI && Widget && Work && Widget->mInteractObject==Work->GetOwner() && UI->GetInteractWidgetStack().Contains(Widget);
    }
    bool ButtonIsActuallyAvailable() {
        WorkshopMinimumRenderOpacity=1.f;
        auto Opaque=[&](float Opacity) {
            WorkshopMinimumRenderOpacity=FMath::Min(WorkshopMinimumRenderOpacity,FMath::IsFinite(Opacity) ? Opacity : 0.f);
            return FMath::IsFinite(Opacity) && Opacity>.95f;
        };
        const auto* Container=WorkshopUI.Get();const auto* Manual=ManufacturingUI.Get();
        const auto* Manufacture=ManufacturingButton.Get();const auto* Button=CraftButton.Get();
        if (!WorkshopIsOwnedAndOpen() || !Container || !Manual || !Manufacture || !Button ||
            !Container->IsVisible() || !Manual->IsVisible() || !Manufacture->IsVisible() || !Button->IsVisible() ||
            !Container->GetIsEnabled() || !Manual->GetIsEnabled() || !Manufacture->GetIsEnabled() || !Button->GetIsEnabled()) return false;
        if (!Opaque(Container->GetRenderOpacity()) || !Opaque(Manual->GetRenderOpacity()) ||
            !Opaque(Manufacture->GetRenderOpacity()) || !Opaque(Button->GetRenderOpacity()) ||
            !Opaque(Container->GetColorAndOpacity().A) || !Opaque(Manual->GetColorAndOpacity().A) ||
            !Opaque(Manufacture->GetColorAndOpacity().A)) return false;
        auto Slate=Button->GetCachedWidget();
        if (!Slate || Slate->GetType()!=FName(TEXT("SButton"))) return false;
        // Check the mounted Slate ancestry as well: a child's own flag does not
        // establish that an ancestor permits interaction. Never enable it here.
        for (int32 Depth=0;Slate && Depth<128;++Depth) {
            if (!Slate->IsEnabled() || !Slate->GetVisibility().IsVisible() || !Opaque(Slate->GetRenderOpacity())) return false;
            Slate=Slate->GetParentWidget();
        }
        return !Slate.IsValid();
    }
    bool VerifyWorkshopHitTest() {
        bWorkshopButtonHitTestVerified=false;bWorkshopHitWindowFound=false;
        WorkshopHitPathDepth=0;WorkshopHitPath.Reset();
        if (!VerifyWorkshopStartupSplash()) {
            WorkshopHitReason=WorkshopStartupReason;WorkshopHitStableSamples=0;
            return false;
        }
        auto Reject=[&](const TCHAR* Reason) {
            WorkshopHitReason=Reason;WorkshopHitStableSamples=0;
            LastReason=TEXT("Waiting for the presented Workshop craft button: ")+WorkshopHitReason;
            return false;
        };
        if (!ButtonIsActuallyAvailable() || !FSlateApplication::IsInitialized())
            return Reject(TEXT("Real craft widget/button ancestry is not enabled/visible/opaque, or Slate is unavailable."));
        auto Slate=CraftButton->GetCachedWidget();
        const FGeometry& Geometry=CraftButton->GetCachedGeometry();
        const auto Size=Geometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1.f || Size.Y<1.f)
            return Reject(TEXT("Actual cached craft-button geometry has not been painted."));
        const FVector2D Center=Geometry.LocalToAbsolute(FVector2D(Size.X*.5,Size.Y*.5));
        auto Window=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef());
        bWorkshopHitWindowFound=Window.IsValid();
        if (!Window) return Reject(TEXT("Actual craft button has no mounted Slate window."));
        if (!Window->GetClientRectInScreen().ContainsPoint(Center))
            return Reject(TEXT("Actual craft-button center is outside its window client rectangle."));
        // This is a read-only query at a supplied virtual point. It neither
        // consults nor changes the OS cursor, nor processes any input reply.
        const auto Bubble=Window->GetHittestGrid().GetBubblePath(Center,0.f,false,0);
        WorkshopHitPathDepth=Bubble.Num();
        bool ContainsButton=false;
        for (const auto& Entry:Bubble) {
            const bool Target=&Entry.Widget.Get()==Slate.Get();
            if (!WorkshopHitPath.IsEmpty()) WorkshopHitPath+=TEXT(" > ");
            WorkshopHitPath+=Entry.Widget->GetTypeAsString();
            if (Target) { ContainsButton=true;WorkshopHitPath+=TEXT("[craft]"); }
        }
        if (!ContainsButton) return Reject(TEXT("A loading overlay, clipping or another widget owns the actual craft-button hit-test point."));
        bWorkshopButtonHitTestVerified=true;
        if (WorkshopLastHitFrame!=GFrameCounter) { ++WorkshopHitStableSamples;WorkshopLastHitFrame=GFrameCounter; }
        WorkshopHitReason=TEXT("Actual enabled craft SButton belongs to its real window's current hit-test bubble path.");
        return true;
    }
    void ReleaseWorkshopHold() {
        if (!bWorkshopPressSent || bWorkshopButtonReleased) return;
        if (auto Slate=HeldSlateButton.Pin()) {
            const TSet<FKey> NoPressedKeys;
            const FPointerEvent Event(0,HeldPosition,HeldPosition,NoPressedKeys,EKeys::LeftMouseButton,0.f,FModifierKeysState());
            // Deliberately do not route the reply through FSlateApplication:
            // this local widget event must never capture/move the OS pointer.
            Slate->OnMouseButtonUp(HeldGeometry,Event);
        }
        if (auto* Button=ManufacturingButton.Get())
            bWorkshopButtonReleased=!Button->IsButtonHeld() && (!CraftButton.IsValid() || !CraftButton->IsPressed());
        HeldSlateButton.Reset();
    }
    bool CloseWorkshop(bool Cleanup=false) {
        if (!bWorkshopUI || !bBenchTouched) return true;
        ReleaseWorkshopHold();
        auto* UI=GameUI.Get();auto* Work=Bench.Get();
        if (!UI || !Work) return false;
        // A widget may have been created during OnUse before the next ticker
        // discovered it. Recover only this owner's exact interact widget.
        if (!WorkshopUI.IsValid()) {
            TArray<UFGInteractWidget*> Owned;UI->GetInteractWidgetsOfInteractObject(Work->GetOwner(),Owned);
            if (Owned.Num()==1) WorkshopUI=Owned[0];
        }
        if (WorkshopIsOwnedAndOpen()) {
            if (!bWorkshopCloseSent) { bWorkshopCloseSent=true;WorkshopUI->OnEscapePressed(); }
            if (Cleanup && WorkshopIsOwnedAndOpen()) UI->RemoveInteractWidget(WorkshopUI.Get());
        }
        TArray<UFGInteractWidget*> Remaining;UI->GetInteractWidgetsOfInteractObject(Work->GetOwner(),Remaining);
        const bool Closed=Remaining.IsEmpty() && Work->GetWorkBenchUser()==OldBenchUser.Get();
        if (Closed && bWorkshopOpened && bWorkshopCloseSent && bWorkshopButtonReleased) bWorkshopClosed=true;
        return Closed;
    }
    bool DiscoverWorkshop(double Now) {
        auto* UI=GameUI.Get();auto* Work=Bench.Get();
        auto* Owner=Work ? Cast<AFGBuildable>(Work->GetOwner()) : nullptr;
        if (!UI || !Owner) { Finish(false,TEXT("Workshop owner or real game UI disappeared."));return false; }
        TArray<UFGInteractWidget*> Owned;UI->GetInteractWidgetsOfInteractObject(Owner,Owned);
        if (Owned.IsEmpty()) { LastReason=TEXT("Waiting for OnUse to push the existing Workshop's real interaction widget.");return false; }
        if (Owned.Num()!=1 || Owned[0]->GetClass()->GetPathName()!=WorkshopWidgetClassPath || Owned[0]->mInteractObject!=Owner) {
            Finish(false,TEXT("Workshop OnUse returned an ambiguous or incorrectly owned interaction widget."));return false;
        }
        WorkshopUI=Owned[0];bWorkshopOpened=WorkshopIsOwnedAndOpen();
        auto* Manual=ReadObject<UUserWidget>(WorkshopUI.Get(),TEXT("mManufacturingWidget"));
        if (!bWorkshopOpened || !Manual || !Manual->IsConstructed() || !Manual->GetCachedWidget()) {
            LastReason=TEXT("Waiting for the real Workshop manufacturing child to construct.");return false;
        }
        ManufacturingUI=Manual;
        if (ReadObject<UFGWorkBench>(Manual,TEXT("mManualManufacturerComponent"))!=Work || Work->GetWorkBenchUser()!=Pioneer.Get()) {
            LastReason=TEXT("Waiting for the real manufacturing UI to bind the selected Workshop and pioneer.");return false;
        }
        auto* Function=Manual->FindFunction(TEXT("SetSelectedRecipe"));
        auto* RecipeArg=Function ? FindFProperty<FClassProperty>(Function,TEXT("mRecipe")) : nullptr;
        int32 Parameters=0;
        if (Function) for (TFieldIterator<FProperty> It(Function);It;++It) if (It->HasAnyPropertyFlags(CPF_Parm)) ++Parameters;
        if (!Function || Parameters!=1 || !RecipeArg || RecipeArg->MetaClass!=UFGRecipe::StaticClass() ||
            !RecipeArg->HasAnyPropertyFlags(CPF_Parm) || RecipeArg->HasAnyPropertyFlags(CPF_ReturnParm|CPF_OutParm)) {
            Finish(false,TEXT("Actual SetSelectedRecipe signature differs from one input ClassProperty mRecipe with FGRecipe MetaClass."));return false;
        }
        FStructOnScope Arguments(Function);
        RecipeArg->SetObjectPropertyValue_InContainer(Arguments.GetStructMemory(),UFactoryFPVRadioRecipe::StaticClass());
        Manual->ProcessEvent(Function,Arguments.GetStructMemory());
        bWorkshopSelected=ReadObject<UClass>(Manual,TEXT("mCurrentRecipe"))==UFactoryFPVRadioRecipe::StaticClass() &&
            Work->GetCurrentRecipe()==UFactoryFPVRadioRecipe::StaticClass();
        if (!bWorkshopSelected) { Finish(false,TEXT("Real Workshop UI recipe selection did not select the radio in both widget and workbench."));return false; }
        auto* Manufacture=ReadObject<UFGManufacturingButton>(Manual,TEXT("mCraftButton"));
        auto* Button=ReadObject<UButton>(Manufacture,TEXT("mButton"));
        if (!Manufacture || !Button) { Finish(false,TEXT("Real Workshop craft button fields do not match native manufacturing button and UButton."));return false; }
        ManufacturingButton=Manufacture;CraftButton=Button;
        Phase=EPhase::SettlingWorkshop;PhaseStarted=Now;SettleFrame=GFrameCounter;
        LastReason=TEXT("Radio selected through real Workshop UI; waiting for layout and genuine craft availability.");
        Marker(TEXT("PENDING"),LastReason,Counts());
        return true;
    }
    void CapturePaymentInventory(const UFGInventoryComponent& Inv,TArray<int32>& Local,TArray<int32>& Central) const {
        Local.Reset(Ingredients.Num());
        Central.Reset(Ingredients.Num());
        const auto* Storage=Depot.Get();
        for (const auto& Ingredient:Ingredients) {
            Local.Add(Inv.GetNumItems(Ingredient.ItemClass));
            Central.Add(Storage ? Storage->GetNumItemsFromCentralStorage(Ingredient.ItemClass) : 0);
        }
    }
    bool MatchesWorkshopPayment(const UFGWorkBench* Work,const UFGInventoryComponent* Inv,TSubclassOf<UFGRecipe> Recipe) const {
        return IsInGameThread() && !bDone && bWorkshopPressSent && Work==Bench.Get() && Inv==Inventory.Get() &&
            Recipe==UFactoryFPVRadioRecipe::StaticClass();
    }
    void ObserveWorkshopPayment() {
        // Follow the native call without changing it. Depot upload activity
        // before or after the transaction does not enter these cost snapshots.
        const TWeakPtr<FRun> Weak=AsShared();
        WorkshopPaymentBeforeHandle=SUBSCRIBE_METHOD(UFGWorkBench::RemoveIngredientsAndAwardRewards,
            [Weak](auto&,UFGWorkBench* Work,UFGInventoryComponent* Inv,TSubclassOf<UFGRecipe> Recipe) {
                const auto Run=Weak.Pin();
                if (!Run || !Run->MatchesWorkshopPayment(Work,Inv,Recipe)) return;
                ++Run->WorkshopPaymentTransactionCalls;
                Run->bWorkshopPaymentTransactionVerified=false;
                if (Run->WorkshopPaymentTransactionCalls!=1 || Inv->GetNoBuildCost()) return;
                Run->WorkshopPaymentKitsBefore=Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass());
                Run->CapturePaymentInventory(*Inv,Run->PaymentBeforeLocalIngredients,Run->PaymentBeforeDepotIngredients);
            });
        WorkshopPaymentAfterHandle=SUBSCRIBE_METHOD_AFTER(UFGWorkBench::RemoveIngredientsAndAwardRewards,
            [Weak](UFGWorkBench* Work,UFGInventoryComponent* Inv,TSubclassOf<UFGRecipe> Recipe) {
                const auto Run=Weak.Pin();
                if (!Run || !Run->MatchesWorkshopPayment(Work,Inv,Recipe) || Run->WorkshopPaymentTransactionCalls!=1 ||
                    Run->PaymentBeforeLocalIngredients.Num()!=Run->Ingredients.Num()) return;
                Run->CapturePaymentInventory(*Inv,Run->PaymentLocalIngredients,Run->PaymentDepotIngredients);
                if (Inv->GetNoBuildCost() || Run->WorkshopPaymentKitsBefore!=Run->BeforeKits ||
                    Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())!=Run->BeforeKits+1) return;
                for (int32 Index=0;Index<Run->Ingredients.Num();++Index) {
                    const int64 LocalDebit=int64(Run->PaymentBeforeLocalIngredients[Index])-Run->PaymentLocalIngredients[Index];
                    const int64 DepotDebit=int64(Run->PaymentBeforeDepotIngredients[Index])-Run->PaymentDepotIngredients[Index];
                    if (LocalDebit<0 || DepotDebit<0 || LocalDebit+DepotDebit!=Run->Ingredients[Index].Amount) return;
                }
                Run->bWorkshopPaymentTransactionVerified=true;
            });
    }
    void StopObservingWorkshopPayment() {
        if (WorkshopPaymentBeforeHandle.IsValid()) {
            UNSUBSCRIBE_METHOD(UFGWorkBench::RemoveIngredientsAndAwardRewards,WorkshopPaymentBeforeHandle);
            WorkshopPaymentBeforeHandle.Reset();
        }
        if (WorkshopPaymentAfterHandle.IsValid()) {
            UNSUBSCRIBE_METHOD(UFGWorkBench::RemoveIngredientsAndAwardRewards,WorkshopPaymentAfterHandle);
            WorkshopPaymentAfterHandle.Reset();
        }
    }
    bool BeginWorkshopHold(double Now) {
        if (!VerifyWorkshopHitTest() || WorkshopHitStableSamples<2 || !Bench.IsValid() || !Inventory.IsValid() ||
            Inventory->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())!=BeforeKits ||
            !Bench->CanProduce(UFactoryFPVRadioRecipe::StaticClass(),Inventory.Get())) {
            Finish(false,TEXT("Real Workshop craft control is hidden/disabled, unfunded or changed before the UI hold; no UI flags were overridden."));return false;
        }
        auto Slate=StaticCastSharedPtr<SButton>(CraftButton->GetCachedWidget());
        HeldGeometry=CraftButton->GetCachedGeometry();
        const auto Size=HeldGeometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1.f || Size.Y<1.f || ManufacturingButton->IsButtonHeld()) {
            Finish(false,TEXT("Real Workshop button has invalid/unsettled geometry or was already held."));return false;
        }
        bWorkshopButtonEnabled=true;
        HeldPosition=HeldGeometry.LocalToAbsolute(FVector2D(Size.X*.5,Size.Y*.5));
        TSet<FKey> Pressed;Pressed.Add(EKeys::LeftMouseButton);
        const FPointerEvent Event(0,HeldPosition,HeldPosition,Pressed,EKeys::LeftMouseButton,0.f,FModifierKeysState());
        if (bProgression) ObserveWorkshopPayment();
        HeldSlateButton=Slate;bWorkshopPressSent=true;
        const FReply Reply=Slate->OnMouseButtonDown(HeldGeometry,Event);
        bWorkshopButtonHeld=Reply.IsEventHandled() && CraftButton->IsPressed() && ManufacturingButton->IsButtonHeld();
        if (!bWorkshopButtonHeld) { Finish(false,TEXT("Actual Slate button press did not enter the native manufacturing hold state."));return false; }
        NativeCraftDuration=Bench->GetCurrentDuration();NativeManufacturingSpeed=Bench->GetManufacturingSpeed();
        Phase=EPhase::Producing;PhaseStarted=Now;
        LastReason=TEXT("Real Workshop button held; only natural UMG/native widget ticks may produce the radio.");
        Marker(TEXT("PENDING"),LastReason,Counts());
        return true;
    }
    void ReleaseScreenshot() {
        if (ScreenshotProcessedHandle.IsValid()) {
            FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotProcessedHandle);
            ScreenshotProcessedHandle.Reset();
        }
        // Never cancel a screenshot requested by another system. Reset does not
        // restore the screen-message state, unlike the normal viewport handler.
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() &&
            FScreenshotRequest::GetFilename().Equals(PendingScreenshot,ESearchCase::CaseSensitive)) {
            FScreenshotRequest::Reset();
            GAreScreenMessagesEnabled=bScreenMessagesBeforeCapture;
        }
        bOwnScreenshotPending=false;
    }
    bool ExportFirstPersonRig(const FString& Directory) {
        auto* Character=Pioneer.Get();
        auto* Mesh=Character ? Character->GetMesh1P() : nullptr;
        auto* Asset=Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
        auto* Radio=EquippedRadio();
        auto* Transmitter=Radio ? Radio->FindComponentByClass<UStaticMeshComponent>() : nullptr;
        auto* Camera=Controller.IsValid() ? Controller->PlayerCameraManager.Get() : nullptr;
        if (!bCaptureEnabled || !FirstPersonViewReady() || !Asset || !Asset->GetSkeleton() || !Radio->GetRootComponent() ||
            !Transmitter || !Transmitter->GetStaticMesh() || !Camera) {
            Finish(false,TEXT("Actual first-person rig, transmitter, skeleton or active camera is unavailable for export."));return false;
        }
        FString Path=FPaths::Combine(Directory,TEXT("Radio-Rig.json"));
        FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !FPaths::IsUnderDirectory(Path,Directory) ||
            !DisposablePath(Path) || IFileManager::Get().FileExists(*Path)) {
            Finish(false,TEXT("Live rig export requires a fresh file inside the guarded disposable QA directory."));return false;
        }
        const auto& Ref=Asset->GetRefSkeleton();
        const auto& RefLocal=Ref.GetRefBonePose();
        if (Ref.GetNum()==0 || RefLocal.Num()!=Ref.GetNum() || !Mesh->GetComponentTransform().IsValid() ||
            !Transmitter->GetComponentTransform().IsValid() || !Radio->GetActorTransform().IsValid()) {
            Finish(false,TEXT("Live first-person rig has invalid transforms or no reference bones."));return false;
        }
        const FVector CameraLocation=Camera->GetCameraLocation();
        const FRotator CameraRotation=Camera->GetCameraRotation();
        if (CameraLocation.ContainsNaN() || CameraRotation.ContainsNaN() || !FMath::IsFinite(Camera->GetFOVAngle())) {
            Finish(false,TEXT("Active first-person camera has invalid values for rig export."));return false;
        }
        FString ProjectionPolicy,ProjectionError;
        if (!RigProjectionPolicy(World.Get(),Controller.Get(),Mesh,Transmitter,ProjectionPolicy,ProjectionError)) { Finish(false,ProjectionError);return false; }
        const auto& Bounds=Transmitter->Bounds;
        const auto LocalBounds=Transmitter->GetStaticMesh()->GetBounds();
        FString Json=FString::Printf(TEXT("{\"schemaVersion\":1,\"units\":\"centimetres\",\"mesh1P\":{\"mesh\":\"%s\",\"skeleton\":\"%s\",\"component\":%s},\"camera\":{\"location\":%s,\"rotationPitchYawRoll\":[%.17g,%.17g,%.17g],\"fov\":%.17g},\"radio\":{\"actorPath\":\"%s\",\"root\":%s,\"transmitter\":{\"mesh\":\"%s\",\"component\":%s,\"worldBounds\":{\"origin\":%s,\"boxExtent\":%s,\"sphereRadius\":%.17g},\"localBounds\":{\"origin\":%s,\"boxExtent\":%s,\"sphereRadius\":%.17g}}},\"bones\":["),
            *Escape(Asset->GetPathName()),*Escape(Asset->GetSkeleton()->GetPathName()),*ComponentJson(Mesh),
            *VectorJson(CameraLocation),CameraRotation.Pitch,CameraRotation.Yaw,CameraRotation.Roll,Camera->GetFOVAngle(),
            *Escape(Radio->GetPathName()),*ComponentJson(Radio->GetRootComponent()),*Escape(Transmitter->GetStaticMesh()->GetPathName()),
            *ComponentJson(Transmitter),*VectorJson(Bounds.Origin),*VectorJson(Bounds.BoxExtent),Bounds.SphereRadius,
            *VectorJson(LocalBounds.Origin),*VectorJson(LocalBounds.BoxExtent),LocalBounds.SphereRadius);
        for (int32 Index=0;Index<Ref.GetNum();++Index) {
            const FTransform Current=Mesh->GetBoneTransform(Index,FTransform::Identity);
            if (!RefLocal[Index].IsValid() || !Current.IsValid()) {
                Finish(false,TEXT("Live rig bone has an invalid transform: ")+Ref.GetBoneName(Index).ToString());return false;
            }
            if (Index) Json+=TEXT(",");
            const auto* Skeleton=Asset->GetSkeleton();
            const int32 SkeletonIndex=Skeleton ? Skeleton->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(Index)) : INDEX_NONE;
            const int32 RetargetMode=SkeletonIndex!=INDEX_NONE ? static_cast<int32>(Skeleton->GetBoneTranslationRetargetingMode(SkeletonIndex)) : -1;
            Json+=FString::Printf(TEXT("{\"index\":%d,\"name\":\"%s\",\"parent\":%d,\"translationRetargetingMode\":%d,\"refLocal\":%s,\"currentComponent\":%s}"),
                Index,*Escape(Ref.GetBoneName(Index).ToString()),Ref.GetParentIndex(Index),RetargetMode,*TransformJson(RefLocal[Index]),*TransformJson(Current));
        }
        const FName SocketName(TEXT("hand_rSocket"));
        const auto* Socket=Mesh->GetSocketByName(SocketName);
        const int32 BoneIndex=Ref.FindBoneIndex(SocketName);
        if (!Socket && BoneIndex==INDEX_NONE) {
            Finish(false,TEXT("Actual first-person rig has neither a hand_rSocket socket nor bone."));return false;
        }
        const FName BoneName=Socket ? Socket->BoneName : SocketName;
        const FTransform SocketLocal=Socket ? Socket->GetSocketLocalTransform() : RefLocal[BoneIndex];
        Json+=FString::Printf(TEXT("],\"hand_rSocket\":{\"kind\":\"%s\",\"boneName\":\"%s\",\"local\":%s,\"currentComponent\":%s,\"world\":%s}"),
            Socket ? TEXT("socket") : TEXT("bone"),*Escape(BoneName.ToString()),*TransformJson(SocketLocal),
            *TransformJson(Mesh->GetSocketTransform(SocketName,RTS_Component)),*TransformJson(Mesh->GetSocketTransform(SocketName,RTS_World)));
        Json+=ProjectionPolicy+TEXT("}");
        if (!FFileHelper::SaveStringToFile(Json,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_NoReplaceExisting)) {
            Finish(false,TEXT("Failed to save the guarded live first-person rig JSON."));return false;
        }
        RigExportPath=Path;RigBoneCount=Ref.GetNum();
        Marker(TEXT("PENDING"),TEXT("Exported actual Shipping first-person mesh, reference/current bones, camera and radio attachments."),Counts());
        return true;
    }
    bool RequestCapture(FCaptureEvidence& Capture,const TCHAR* Name,bool ShowUI,double Now) {
        if (FScreenshotRequest::IsScreenshotRequested()) {
            Finish(false,TEXT("An unrelated screenshot is pending; refusing to replace it."));return false;
        }
        auto* ViewportClient=World.IsValid() ? World->GetGameViewport() : nullptr;
        if (!ViewportClient || !ViewportClient->Viewport || ViewportClient->Viewport->GetSizeXY().X<320 ||
            ViewportClient->Viewport->GetSizeXY().Y<180) {
            Finish(false,TEXT("Actual game viewport is unavailable or smaller than 320x180 for capture."));return false;
        }
        FString SavedRoot=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
        FPaths::NormalizeDirectoryName(SavedRoot);
        if (!FPaths::CollapseRelativeDirectories(SavedRoot) || !DisposablePath(SavedRoot)) {
            Finish(false,TEXT("Screenshot Saved directory is outside the guarded disposable QA root."));return false;
        }
        const FString Directory=FPaths::Combine(SavedRoot,TEXT("FactoryFPV-QA"));
        FString Path=FPaths::Combine(Directory,Name);
        FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !FPaths::IsUnderDirectory(Directory,SavedRoot) ||
            !FPaths::IsUnderDirectory(Path,Directory)) {
            Finish(false,TEXT("Screenshot output escaped the guarded disposable Saved directory."));return false;
        }
        if (IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Directory,true)) {
            Finish(false,TEXT("Screenshot requires a fresh file in a writable disposable QA directory."));return false;
        }
        if (!ShowUI && !ExportFirstPersonRig(Directory)) return false;
        ReleaseScreenshot();
        Capture.Path=Path;
        PendingScreenshot=Path;
        CaptureStarted=Now;
        bScreenshotProcessed=false;
        bOwnScreenshotPending=true;
        bScreenMessagesBeforeCapture=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> WeakRun=AsShared();
        ScreenshotProcessedHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([WeakRun]() {
            if (const auto Run=WeakRun.Pin(); Run && !Run->bDone && Run->bOwnScreenshotPending) {
                Run->bScreenshotProcessed=true;
                Run->bOwnScreenshotPending=false;
            }
        });
        FScreenshotRequest::RequestScreenshot(Path,ShowUI,false,false);
        LastReason=TEXT("Waiting for the real viewport screenshot to be processed and decoded: ")+Path;
        Marker(TEXT("PENDING"),LastReason,Counts());
        return true;
    }
    bool CaptureFinished(FCaptureEvidence& Capture,double Now) {
        if (Now-CaptureStarted>=5.) {
            Finish(false,TEXT("Actual viewport screenshot did not complete within five seconds: ")+Capture.Path);return false;
        }
        if (!bScreenshotProcessed) return false;
        ReleaseScreenshot();
        FImage Image;
        // Use the buffer decoder: Windows/PCH headers can rename LoadImage to
        // LoadImageW, which does not match the Engine DLL's exported function.
        TArray64<uint8> PngBytes;
        if (!IFileManager::Get().FileExists(*Capture.Path) || !FFileHelper::LoadFileToArray(PngBytes,*Capture.Path) ||
            !FImageUtils::DecompressImage(PngBytes.GetData(),PngBytes.Num(),Image) ||
            Image.SizeX<320 || Image.SizeY<180 || Image.SizeX>8192 || Image.SizeY>8192 || Image.NumSlices!=1 ||
            Image.RawData.IsEmpty()) {
            Finish(false,TEXT("Viewport completion did not produce a decodable image of sane dimensions: ")+Capture.Path);return false;
        }
        // Catch a failed/blank readback, while leaving fit and visual quality to
        // inspection of this actual viewport image rather than pixel heuristics.
        const FLinearColor Reference=Image.GetOnePixelLinear(0,0);
        bool bNonuniform=false;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) {
            for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/40)) {
                const FLinearColor Pixel=Image.GetOnePixelLinear(X,Y);
                if (!FMath::IsFinite(Pixel.R) || !FMath::IsFinite(Pixel.G) || !FMath::IsFinite(Pixel.B)) {
                    Finish(false,TEXT("Viewport image contains invalid pixel values: ")+Capture.Path);return false;
                }
                bNonuniform|=FMath::Abs(Pixel.R-Reference.R)>0.0001f || FMath::Abs(Pixel.G-Reference.G)>0.0001f ||
                    FMath::Abs(Pixel.B-Reference.B)>0.0001f;
            }
        }
        if (!bNonuniform) { Finish(false,TEXT("Actual viewport screenshot is uniform; visual evidence is unavailable: ")+Capture.Path);return false; }
        Capture.Size=FIntPoint(Image.SizeX,Image.SizeY);
        Capture.bDecoded=true;
        Marker(TEXT("PENDING"),TEXT("Fresh actual viewport PNG decoded; visual fit still requires image review."),Counts());
        return true;
    }
    bool NoEquipmentModResources() const {
        if (!World.IsValid() || !Controller.IsValid() || !Hangar.IsValid() || Hangar->IsPanelOpen()) return false;
        for (TActorIterator<AActor> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed() &&
            (It->IsA<AFactoryFPVDrone>() || It->IsA<AFactoryFPVPioneerAnchor>() || It->IsA<AFactoryFPVPilotVisual>())) return false;
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() &&
            (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner())) && It->IsStreamingSourceEnabled()) return false;
        const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller.Get());
        if (Lease.ActiveLeases || Lease.OwnedMoveContributions || Lease.OwnedLookContributions || Lease.ResetDepth) return false;
        auto* Viewport=World->GetGameViewport();auto Window=Viewport?Viewport->GetWindow():nullptr;
        if (!Window) return false;
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Window.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768) {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());
            const FName Type=Widget->GetType();
            if (Type==FName(TEXT("SFactoryFPVOSD")) || Type==FName(TEXT("SFactoryFPVPanel")) || Type==FName(TEXT("SFactoryFPVRacePanel"))) return false;
            if (auto* Children=Widget->GetChildren()) for (int32 Index=0;Index<Children->Num();++Index) Pending.Add(Children->GetChildAt(Index));
        }
        return Pending.IsEmpty();
    }
    bool SnapshotEquipmentState() {
        auto* PC=Controller.Get();auto* Character=Pioneer.Get();auto* Move=Character?Character->GetCharacterMovement():nullptr;
        auto* Hud=PC?Cast<AFGHUD>(PC->GetHUD()):nullptr;auto* Viewport=World.IsValid()?World->GetGameViewport():nullptr;
        auto* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
        if (!PC || !Character || !Character->IsAliveAndWell() || !Move || !Hud || !Manager || !Viewport || !PC->GetViewTarget() || !NoEquipmentModResources()) return false;
        EquipmentView=PC->GetViewTarget();EquipmentCameraManager=Manager;EquipmentHud=Hud;
        EquipmentFov=Manager->GetFOVAngle();EquipmentLockedFov=Manager->GetLockedFOV();
        if (!FMath::IsFinite(EquipmentFov) || !FMath::IsFinite(EquipmentLockedFov)) return false;
        EquipmentPioneerLocation=Character->GetActorLocation();EquipmentPioneerRotation=Character->GetActorRotation();
        EquipmentMovement=Move->MovementMode;EquipmentCustomMovement=Move->CustomMovementMode;
        bEquipmentIgnoreMove=PC->IsMoveInputIgnored();bEquipmentIgnoreLook=PC->IsLookInputIgnored();bEquipmentCursor=PC->bShowMouseCursor;
        bEquipmentViewportIgnore=Viewport->IgnoreInput();bEquipmentPartialHud=Hud->GetPartialPumpiMode();
        bEquipmentHudVisible=Hud->GetHUDVisibility();bEquipmentShowHud=Hud->bShowHUD;
        EquipmentInputs.Reset();EquipmentBody.Reset();EquipmentSources.Reset();EquipmentSessionInputs.Reset();
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get())
            EquipmentInputs.Add({*It,PC->IsInputComponentInStack(*It),FlightRadio.IsValid() && It->GetOwner()==FlightRadio.Get(),
                OriginalArmsEquipment.IsValid() && It->GetOwner()==OriginalArmsEquipment.Get()});
        TInlineComponentArray<UPrimitiveComponent*> BodyParts(Character);
        for (auto* Part:BodyParts) EquipmentBody.Add({Part,Part->bHiddenInGame!=0,Part->IsVisible(),Part->bCastHiddenShadow!=0});
        return !EquipmentInputs.IsEmpty() && !EquipmentBody.IsEmpty();
    }
    bool CheckEquipmentState(bool AfterNativeUnequip) {
        auto* PC=Controller.Get();auto* Character=Pioneer.Get();auto* Move=Character?Character->GetCharacterMovement():nullptr;
        auto* Viewport=World.IsValid()?World->GetGameViewport():nullptr;
        bEquipmentInputsRestored=PC!=nullptr;NativeEquipmentInputChanges=0;
        for (const auto& Item:EquipmentInputs) {
            const bool Member=PC && Item.Component.IsValid() && PC->IsInputComponentInStack(Item.Component.Get());
            if (AfterNativeUnequip && Item.bRadioOwned) { if (Member) bEquipmentInputsRestored=false; }
            else if (AfterNativeUnequip && bOriginalEquipmentReactivated && Item.bOriginalEquipmentOwned) { NativeEquipmentInputChanges+=Member!=Item.bMember; }
            else if (Member!=Item.bMember) bEquipmentInputsRestored=false;
        }
        for (const auto& Item:EquipmentSessionInputs) if (PC && Item.IsValid() && PC->IsInputComponentInStack(Item.Get())) bEquipmentInputsRestored=false;
        for (TObjectIterator<UInputComponent> It;PC && It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It) &&
            !EquipmentInputs.ContainsByPredicate([&](const FInputSnapshot& Item) { return Item.Component.Get()==*It; })) {
            if (AfterNativeUnequip && bOriginalEquipmentReactivated && It->GetOwner()==OriginalArmsEquipment.Get()) ++NativeEquipmentInputChanges;
            else bEquipmentInputsRestored=false;
        }
        bEquipmentBodyRestored=!EquipmentBody.IsEmpty();
        for (const auto& Item:EquipmentBody) if (!Item.Component.IsValid() || (Item.Component->bHiddenInGame!=0)!=Item.bHidden ||
            Item.Component->IsVisible()!=Item.bVisible || (Item.Component->bCastHiddenShadow!=0)!=Item.bHiddenShadow) bEquipmentBodyRestored=false;
        bEquipmentSourcesRetired=true;
        for (const auto& Source:EquipmentSources) if (Source.IsValid() && Source->IsStreamingSourceEnabled()) bEquipmentSourcesRetired=false;
        return PC && Character && Character->IsAliveAndWell() && Move && Viewport && EquipmentCameraManager.IsValid() && EquipmentHud.IsValid() &&
            PC->GetPawn()==Character && PC->GetViewTarget()==EquipmentView.Get() && PC->PlayerCameraManager==EquipmentCameraManager.Get() && PC->GetHUD()==EquipmentHud.Get() &&
            Character->GetActorLocation().Equals(EquipmentPioneerLocation,.5) && Character->GetActorRotation().Equals(EquipmentPioneerRotation,.1) &&
            Move->MovementMode==EquipmentMovement && Move->CustomMovementMode==EquipmentCustomMovement &&
            PC->IsMoveInputIgnored()==bEquipmentIgnoreMove && PC->IsLookInputIgnored()==bEquipmentIgnoreLook && PC->bShowMouseCursor==bEquipmentCursor &&
            Viewport->IgnoreInput()==bEquipmentViewportIgnore && EquipmentHud->GetPartialPumpiMode()==bEquipmentPartialHud &&
            EquipmentHud->GetHUDVisibility()==bEquipmentHudVisible && EquipmentHud->bShowHUD==bEquipmentShowHud &&
            FMath::IsNearlyEqual(EquipmentCameraManager->GetFOVAngle(),EquipmentFov,.1f) && FMath::IsNearlyEqual(EquipmentCameraManager->GetLockedFOV(),EquipmentLockedFov,.01f) &&
            bEquipmentInputsRestored && bEquipmentBodyRestored && bEquipmentSourcesRetired && NoEquipmentModResources();
    }
    bool FindHangarF6(FInputActionUnifiedDelegate& Callback) const {
        int32 Matches=0;auto* PC=Controller.Get();
        for (TObjectIterator<UInputComponent> It;PC && It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It))
            for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Hangar.Get())) {
                Callback=Binding.KeyDelegate;++Matches;
            }
        return Matches==1;
    }
    void RestoreSandboxPolicy() {
        if (!bEquipmentFlight || !bPolicySnapshot) return;
        auto* Settings=GetMutableDefault<UFactoryFPVSettings>();Settings->bAllowSandboxAccess=bPreviousSandboxPolicy;
        bSandboxPolicyRestored=Settings->bAllowSandboxAccess==bPreviousSandboxPolicy; // Runtime memory only; never SaveSettings.
    }
    bool PrepareEquipmentPolicy() {
        if (!bEquipmentFlight || bNoKitSettingsAccessible) return true;
        FInputActionUnifiedDelegate Callback;
        if (!FindHangarF6(Callback)) { LastReason=TEXT("Equipment policy probe waits for one actual bound F6 callback.");return false; }
        if (AFactoryFPVRadioEquipment::IsEquippedBy(Controller.Get()) || !SnapshotEquipmentState()) {
            Finish(false,TEXT("Equipment policy probe requires no equipped FPV radio, prior FPV resources or invalid native state."));return false;
        }
        OriginalArmsEquipment=Pioneer->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS);
        auto* Settings=GetMutableDefault<UFactoryFPVSettings>();bPreviousSandboxPolicy=Settings->bAllowSandboxAccess;bPolicySnapshot=true;Settings->bAllowSandboxAccess=false;
        auto* Unexpected=AFactoryFPVDrone::Launch(Controller.Get(),TEXT("keyboard"),NoKitError);
        if (Unexpected) EquipmentDrone=Unexpected;
        bNoKitLaunchRefused=!Unexpected && NoKitError==TEXT("Equip an FPV Radio Kit, or enable sandbox flights in the hangar.") && CheckEquipmentState(false);
        if (!bNoKitLaunchRefused) { Finish(false,TEXT("Ordinary no-kit launch did not reject on the real equipment policy with unchanged session state: ")+NoKitError);return false; }
        Callback.Execute(EKeys::F6);
        if (!Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual bound F6 could not open settings without an equipped kit and with sandbox disabled."));return false; }
        Callback.Execute(EKeys::F6);
        bNoKitSettingsAccessible=!Hangar->IsPanelOpen() && CheckEquipmentState(false) && !Settings->bAllowSandboxAccess;
        if (!bNoKitSettingsAccessible) { Finish(false,TEXT("No-kit settings did not close through bound F6 with native state and disabled sandbox policy retained."));return false; }
        Marker(TEXT("PENDING"),TEXT("Real no-kit launch refused with no session changes; bound F6 settings still opens/closes. Runtime sandbox policy stays disabled for crafted-kit launch."),Counts());
        return true;
    }
    void CompleteKit(double Now,const FString& Reason) {
        if (!bEquipmentFlight) { Finish(true,Reason);return; }
        if (!bOpened || !bPaymentVerified || !EquippedRadio() || !Hangar.IsValid() || !Hangar->IsPanelOpen()) {
            Finish(false,TEXT("Equipment-flight segment requires the paid crafted radio and Primary-opened hangar."));return;
        }
        Hangar->ClosePanel();Phase=EPhase::ClosingForEquipmentFlight;PhaseStarted=Now;EquipmentPhaseFrame=GFrameCounter;
        if (bPreparationOnly && bCameraModeChanged && Pioneer.IsValid()) { Pioneer->SetCameraMode(OldCameraMode);bCameraModeChanged=false; }
        LastReason=TEXT("Waiting for normal hangar close before snapshot and ordinary equipped disarmed launch.");
    }
    AFactoryFPVRadioEquipment* EquippedRadio() const {
        auto* Slot=Arms.Get();
        auto* Radio=Slot ? Cast<AFactoryFPVRadioEquipment>(Slot->GetStackEquipmentActorAtIdx(ArmIndex)) : nullptr;
        return Radio && Pioneer.IsValid() && Pioneer->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS)==Radio && Radio->IsEquipped() &&
            Radio->GetInstigatorCharacter()==Pioneer.Get() && AFactoryFPVRadioEquipment::IsEquippedBy(Controller.Get()) ? Radio : nullptr;
    }
    bool FirstPersonViewReady() const {
        return Pioneer.IsValid() && Controller.IsValid() && Controller->GetViewTarget()==Pioneer.Get() &&
            Pioneer->GetCameraMode()==ECameraMode::ECM_FirstPerson && EquippedRadio()!=nullptr;
    }
    void SendPrimary(double Now) {
        auto* Radio=EquippedRadio();
        if (!Radio || !Hangar.IsValid() || Hangar->IsPanelOpen()) {
            Finish(false,TEXT("Native equipped radio changed or hangar was already open before the primary action."));return;
        }
        bPrimarySent=true;
        Radio->TriggerDefaultEquipmentActionEvent(EDefaultEquipmentAction::PrimaryFire,EDefaultEquipmentActionEvent::Pressed);
        Radio->TriggerDefaultEquipmentActionEvent(EDefaultEquipmentAction::PrimaryFire,EDefaultEquipmentActionEvent::Released);
        bOpened=Hangar->IsPanelOpen();
        Phase=EPhase::Opening;PhaseStarted=Now;
        LastReason=TEXT("Native radio equipped; waiting for its public primary action to open the real hangar.");
    }
    void RestoreBench() {
        if (bBenchTouched) if (auto* Work=Bench.Get()) {
            Work->SetWorkBenchUser(OldBenchUser.Get());
            Work->SetInventory(OldBenchInventory.Get());
            Work->SetRecipe(OldRecipe);
            Work->mCurrentManufacturingProgress=OldProgress;
            bBenchTouched=false;
        }
    }
    void Restore() {
        StopObservingWorkshopPayment();
        ReleaseScreenshot();
        if (auto* Own=EquipmentDrone.Get();Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==Controller.Get()) Own->ExitDrone();
        if (bWorkshopUI) CloseWorkshop(true);
        if (bPrimarySent) if (auto* Panel=Hangar.Get()) if (Panel->IsPanelOpen()) Panel->ClosePanel();
        if (bCameraModeChanged) if (auto* Character=Pioneer.Get()) {
            Character->SetCameraMode(OldCameraMode);
            bCameraModeChanged=false;
        }
        if (bMoved) if (auto* Slot=Arms.Get()) if (auto* Inv=Inventory.Get()) {
            FInventoryStack Stack;
            if (Slot->GetStackFromIndex(ArmIndex,Stack) && Stack.HasItems() &&
                Stack.Item.GetItemClass()==UFactoryFPVRadioDescriptor::StaticClass()) {
                const int32 Destination=ReturnIndex!=INDEX_NONE ? ReturnIndex : Inv->FindEmptyIndex();
                if (Destination!=INDEX_NONE && UFGInventoryLibrary::MoveInventoryItem(Slot,ArmIndex,Inv,Destination)) bMoved=false;
            }
            Slot->SetActiveIndex(PreviousActive);
        }
        if (bEquipmentFlight && bNativeEquipVerified && !bMoved) if (auto* Slot=Arms.Get()) Slot->SetActiveIndex(PreviousActive);
        RestoreBench();
        RestoreSandboxPolicy();
    }
    void Finish(bool Success,const FString& Reason) {
        if (bDone) return;
        bDone=true;
        RestoreSandboxPolicy();
        if (Success && bEquipmentFlight && !bPreparationOnly && (!bNoKitLaunchRefused || !bNoKitSettingsAccessible || !bEquippedFlightLaunched ||
            !bNativeUnequipReturned || !bEquipmentFlightStateRestored || !bSandboxPolicyRestored)) Success=false;
        if (Success && bProgression && (LoadedProgressionSavePath.IsEmpty() || !bProgressionMilestonePurchased ||
            !bRecipeAvailable || !bProducerAvailable || BeforeKits!=0 || ProduceCalls!=0 || !bWorkshopProduced ||
            !bWorkshopClosed || !bWorkshopButtonHitTestVerified || !bPaymentVerified || !bCraftedExactlyOne ||
            !bWorkshopPaymentTransactionVerified || WorkshopPaymentTransactionCalls!=1 ||
            !bNativeEquipVerified || NativeEquipActions!=1 || !bPrimarySent || !bOpened ||
            SeededIngredients.ContainsByPredicate([](int32 Seed) { return Seed!=0; }))) {
            Success=false;
        }
        const FString Evidence=Counts();
        Restore();
        if (bPreparationOnly) {
            Marker(TEXT("PENDING"),TEXT("Kit preparation ended without process exit: ")+Reason,Evidence);
            auto Completion=MoveTemp(PreparationCompletion);if (Completion) Completion(false,Reason,TEXT("{")+Evidence.Mid(1)+TEXT("}"));
            return;
        }
        Marker(Success ? TEXT("PASS") : TEXT("FAIL"),Reason,Evidence);
        Bench.Reset();Arms.Reset();Inventory.Reset();Depot.Reset();Hangar.Reset();Pioneer.Reset();Controller.Reset();
        GameUI.Reset();WorkshopUI.Reset();ManufacturingUI.Reset();ManufacturingButton.Reset();CraftButton.Reset();HeldSlateButton.Reset();
        OldBenchUser.Reset();OldBenchInventory.Reset();OldRecipe=nullptr;Ingredients.Reset();
        World.Reset();
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
    }
    bool ResolveProgressionHandoff(UWorld& Current) {
        FString Clone,Progression;
        if (!UFGSaveSystem::GetAbsolutePathForSaveGame(&Current,CloneSaveName,Clone) ||
            !UFGSaveSystem::GetAbsolutePathForSaveGame(&Current,ProgressionSaveName,Progression)) return false;
        Clone=NormalizedPath(Clone);
        Progression=NormalizedPath(Progression);
        const FString SaveDirectory=NormalizedPath(UFGSaveSystem::GetSaveDirectoryPath());
        if (!Clone.StartsWith(SaveDirectory+TEXT("/"),ESearchCase::IgnoreCase) ||
            !FPaths::GetBaseFilename(Clone).Equals(CloneSaveName,ESearchCase::CaseSensitive) ||
            !FPaths::GetExtension(Clone).Equals(TEXT("sav"),ESearchCase::CaseSensitive) ||
            !FPaths::GetCleanFilename(Progression).Equals(FString(ProgressionSaveName)+TEXT(".sav"),ESearchCase::CaseSensitive) ||
            !FPaths::GetPath(Progression).Equals(FPaths::GetPath(Clone),ESearchCase::IgnoreCase) ||
            !IFileManager::Get().FileExists(*Clone) || !IFileManager::Get().FileExists(*Progression)) return false;
        LoadedProgressionSavePath=MoveTemp(Progression);
        return true;
    }
    bool Ready() {
        auto* Current=World.Get();
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone) return false;
        auto* Mode=Current->GetAuthGameMode<AFGGameMode>();
        auto* Session=Mode ? Mode->GetSaveSession() : nullptr;
        auto* PC=Current->GetFirstPlayerController();
        auto* Character=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Mode || Mode->IsMainMenuGameMode() || !Current->HasBegunPlay() || !Session || !Character ||
            !Character->IsAliveAndWell() || !PC->GetLocalPlayer() || PC->IsPaused() || Session->HasSaveGameOperationPending()) return false;
        const auto& Header=Session->GetLoadedSaveHeader();
        const auto Load=Mode->GetLoadData();
        if (bProgression) {
            if (!Load.IsValidLoad || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
                !Load.LoadName.Equals(ProgressionSaveName,ESearchCase::CaseSensitive) ||
                !Header.SaveName.Equals(ProgressionSaveName,ESearchCase::CaseSensitive) ||
                (LoadedProgressionSavePath.IsEmpty() && !ResolveProgressionHandoff(*Current))) {
                LastReason=TEXT("Progression kit QA requires the actual FPV-QA-Progression save and native header from the exact disposable clone directory.");return false;
            }
        } else if (Header.SaveName.IsEmpty() || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (!Load.LoadName.Equals(CloneSaveName,ESearchCase::CaseSensitive) && !Header.SaveName.Equals(CloneSaveName,ESearchCase::CaseSensitive))) {
            LastReason=TEXT("Loaded identity is not the expected FPV-QA-Clone.");return false;
        }
        if (Phase!=EPhase::Waiting && (Pioneer.Get()!=Character || Controller.Get()!=PC)) {
            Finish(false,TEXT("Pioneer/controller changed during kit QA."));return false;
        }
        Pioneer=Character;Controller=PC;
        return true;
    }
    bool Prepare() {
        auto* Current=World.Get();
        if (bPreparationOnly) {
            FString Clone;const FString Name=TEXT("FPV-QA-KitRoundtrip");
            if (!UFGSaveSystem::GetAbsolutePathForSaveGame(Current,TEXT("FPV-QA-Clone"),Clone)) { LastReason=TEXT("Preparation waits for exact disposable clone path resolution.");return false; }
            FString Expected=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::GetPath(Clone),Name+TEXT(".sav")));
            FString Actual=FPaths::ConvertRelativePathToFull(UFGSaveSystem::CreateAbsolutePath(Current,Name,false));FPaths::NormalizeFilename(Expected);FPaths::NormalizeFilename(Actual);
            FString ExistingPath;auto* System=UFGSaveSystem::Get(Current);
            if (!Actual.Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase) || Actual!=Expected || IFileManager::Get().FileExists(*Actual) ||
                !System || System->SaveGameExistsSync(Name) || UFGSaveSystem::GetAbsolutePathForSaveGame(Current,Name,ExistingPath) ||
                IFileManager::Get().FileExists(*FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FactoryFPV-QA/KitSave-Manifest.txt")))) {
                Finish(false,TEXT("Paid-kit preparation requires fresh exact KitRoundtrip output/manifest in the disposable clone directory before any ingredient or equipment mutation."));return false;
            }
        }
        auto* Character=Pioneer.Get();
        auto* PC=Controller.Get();
        auto* Inv=Character->GetInventory();
        auto* Slot=Character->GetEquipmentSlot(EEquipmentSlot::ES_ARMS);
        auto* Panel=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        auto* Recipes=AFGRecipeManager::Get(Current);
        auto* WorkshopClass=LoadClass<UFGWorkBench>(nullptr,TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C"));
        if (!Inv || !Slot || !Panel || !Recipes || !WorkshopClass || !Current->GetGameViewport()) {
            LastReason=TEXT("Real inventory, arms, local hangar, viewport or Workshop class is unavailable.");return false;
        }
        Inventory=Inv;Arms=Slot;Hangar=Panel;Depot=AFGCentralStorageSubsystem::Get(Current);
        if (bProgression) {
            auto* Schematics=AFGSchematicManager::Get(Current);
            if (!Schematics) { LastReason=TEXT("Waiting for the native schematic manager.");return false; }
            bProgressionMilestonePurchased=Schematics->IsSchematicPurchased(UFactoryFPVRadioSchematic::StaticClass(),PC);
            if (!bProgressionMilestonePurchased) {
                Finish(false,TEXT("The native progression handoff does not have the FPV milestone purchased."));return false;
            }
            if (Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())!=0 ||
                Slot->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())!=0) {
                Finish(false,TEXT("Progression kit QA must start without a radio in the pioneer's inventory or arms."));return false;
            }
        }
        bNoBuildCostBefore=Inv->GetNoBuildCost();
        bRecipeAvailable=Recipes->IsRecipeAvailable(UFactoryFPVRadioRecipe::StaticClass());
        TArray<TSubclassOf<UFGRecipe>> Available;
        Recipes->GetAvailableRecipesForProducer(WorkshopClass,Available);
        bProducerAvailable=Available.Contains(UFactoryFPVRadioRecipe::StaticClass());
        if (Panel->IsPanelOpen()) { LastReason=TEXT("A pre-existing panel is open; refusing to take its state.");return false; }
        if (bWorkshopUI || bEquipmentFlight) {
            auto* GamePC=Cast<AFGPlayerController>(PC);
            auto* UI=GamePC ? GamePC->GetGameUI() : nullptr;
            auto* Viewport=Current->GetGameViewport();auto* Console=Viewport?Viewport->ViewportConsole.Get():nullptr;
            if (!UI || UI->HasActiveInteractWidget() || UI->IsPauseMenuOpen() || GamePC->IsPauseMenuOpen() || (Console && Console->ConsoleState!=NAME_None)) {
                LastReason=TEXT("Workshop/equipment mode requires a ready game UI with no pre-existing interaction, pause menu or console.");return false;
            }
            GameUI=UI;
            if (!VerifyWorkshopStartupSplash()) return false;
            if (bWorkshopUI && (!bRecipeAvailable || !bProducerAvailable)) {
                Finish(false,TEXT("Radio recipe is not already unlocked and listed for the real Workshop UI; no unlock or fixture seed was attempted."));return false;
            }
        }
        ArmIndex=Slot->FindEmptyIndex();
        if (ArmIndex==INDEX_NONE || !Slot->IsItemAllowed(UFactoryFPVRadioDescriptor::StaticClass(),ArmIndex)) {
            LastReason=TEXT("A free arms slot accepting the radio is required before any fixture changes.");return false;
        }
        UFGWorkBench* Selected=nullptr;
        for (TActorIterator<AActor> It(Current);It;++It) {
            if (It->IsActorBeingDestroyed() || !It->HasActorBegunPlay() || It->IsTemplate()) continue;
            if (const auto* Building=Cast<AFGBuildable>(*It); Building &&
                (Building->GetIsDismantled() || Building->GetBlueprintDesigner() || Building->IsBuildableInsideBlueprintDesigner())) continue;
            TInlineComponentArray<UFGWorkBench*> Components(*It);
            for (auto* Candidate:Components) if (IsValid(Candidate) && Candidate->IsA(WorkshopClass) && Candidate->IsRegistered() &&
                !Candidate->GetWorkBenchUser() && !Candidate->IsProducing()) {
                if (!Selected || Candidate->GetPathName()<Selected->GetPathName()) Selected=Candidate;
            }
        }
        if (!Selected) { LastReason=TEXT("No existing live, unoccupied BP_WorkshopComponent_C is loaded; no fixture changes made.");return false; }
        Bench=Selected;
        FUseState WorkshopUseState;
        auto* SelectedOwner=Cast<AFGBuildable>(Selected->GetOwner());
        if (bWorkshopUI) {
            const auto* WidgetClassProperty=SelectedOwner ? FindFProperty<FSoftClassProperty>(SelectedOwner->GetClass(),TEXT("mInteractWidgetSoftClass")) : nullptr;
            const FSoftObjectPtr WidgetClass=WidgetClassProperty ? WidgetClassProperty->GetPropertyValue_InContainer(SelectedOwner) : FSoftObjectPtr();
            if (!SelectedOwner || !SelectedOwner->GetClass()->ImplementsInterface(UFGUseableInterface::StaticClass()) ||
                WidgetClass.ToSoftObjectPath().ToString()!=WorkshopWidgetClassPath || !IFGUseableInterface::Execute_IsUseable(SelectedOwner)) {
                Finish(false,TEXT("Existing Workshop does not expose a usable real interaction widget."));return false;
            }
            WorkshopUseState.UseLocation=SelectedOwner->GetActorLocation();
            WorkshopUseState.UseComponent=SelectedOwner->FindComponentByClass<UPrimitiveComponent>();
            IFGUseableInterface::Execute_UpdateUseState(SelectedOwner,Character,WorkshopUseState.UseLocation,WorkshopUseState.UseComponent,WorkshopUseState);
            if (!WorkshopUseState.GetState() || !UFGUseState::CanUseInState(WorkshopUseState.GetState())) {
                Finish(false,TEXT("Real Workshop UpdateUseState rejected this pioneer; no fixture seed or UI override attempted."));return false;
            }
        }
        if (bNoBuildCostBefore) {
            Finish(false,TEXT("Paid crafting cannot be qualified while session NoCost or player NoBuildCost is enabled; no fixture mutation attempted."));return false;
        }
        Ingredients=UFGRecipe::GetIngredients(Current,UFactoryFPVRadioRecipe::StaticClass());
        const auto Products=UFGRecipe::GetProducts(UFactoryFPVRadioRecipe::StaticClass());
        if (Ingredients.IsEmpty() || Products.Num()!=1 || Products[0].ItemClass!=UFactoryFPVRadioDescriptor::StaticClass() || Products[0].Amount!=1) {
            Finish(false,TEXT("Radio recipe does not contain valid ingredients and exactly one radio output."));return false;
        }
        if (!PrepareEquipmentPolicy()) return false;
        TArray<FInventoryStack> Missing;
        for (const auto& Ingredient:Ingredients) {
            if (!Ingredient.ItemClass || Ingredient.Amount<=0 || Ingredient.ItemClass==UFactoryFPVRadioDescriptor::StaticClass()) {
                Finish(false,TEXT("Invalid effective ingredient; no radio may be seeded."));return false;
            }
            const int32 Before=Inv->GetNumItems(Ingredient.ItemClass);
            const int32 BeforeDepot=Depot.IsValid() ? Depot->GetNumItemsFromCentralStorage(Ingredient.ItemClass) : 0;
            const int32 Seed=static_cast<int32>(FMath::Max<int64>(0,int64(Ingredient.Amount)-Before-BeforeDepot));
            BeforeIngredients.Add(Before);BeforeDepotIngredients.Add(BeforeDepot);SeededIngredients.Add(bProgression ? 0 : Seed);
            if (Seed) Missing.Add(FInventoryStack(Seed,Ingredient.ItemClass));
        }
        if (bProgression && !Missing.IsEmpty()) {
            Finish(false,TEXT("The native progression save cannot fund one paid Workshop radio. No ingredients were added."));return false;
        }
        if (!Inv->HasEnoughSpaceForStacks(Missing) || !Inv->HasEnoughSpaceForStack(FInventoryStack(1,UFactoryFPVRadioDescriptor::StaticClass()))) {
            Finish(false,TEXT("Clone inventory lacks fixture ingredient or product capacity; no mutation attempted."));return false;
        }
        BeforeKits=Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass());
        for (int32 Index=0;Index<Inv->GetSizeLinear();++Index) {
            FInventoryStack Stack;
            if (Inv->GetStackFromIndex(Index,Stack) && Stack.HasItems() && Stack.Item.GetItemClass()==UFactoryFPVRadioDescriptor::StaticClass())
                BeforeRadioIndices.Add(Index);
        }
        PreviousActive=Slot->GetActiveIndex();
        OldBenchUser=Selected->GetWorkBenchUser();OldBenchInventory=Selected->GetInventory();
        OldRecipe=Selected->GetCurrentRecipe();OldProgress=Selected->GetManufacturingProgress();
        if (bProgression) {
            Marker(TEXT("PENDING"),TEXT("Verified the native HUB handoff and existing materials for a paid Workshop radio."),Counts());
        } else {
            Marker(TEXT("PENDING"),TEXT("Preflight passed. Recipe availability is unchanged; only shortages across inventory plus Dimensional Depot will be seeded locally in the disposable clone."),Counts());
            for (const auto& Stack:Missing) if (Inv->AddStack(Stack,false)!=Stack.NumItems) {
                Finish(false,TEXT("Minimal ingredient seeding failed; radio was never granted."));return false;
            }
        }
        for (int32 Index=0;Index<Ingredients.Num();++Index) {
            const int32 Count=Inv->GetNumItems(Ingredients[Index].ItemClass);
            BaselineIngredients.Add(Count);
            BaselineDepotIngredients.Add(Depot.IsValid() ? Depot->GetNumItemsFromCentralStorage(Ingredients[Index].ItemClass) : 0);
            if (Count!=BeforeIngredients[Index]+SeededIngredients[Index]) {
                Finish(false,TEXT("Ingredient seeding did not match the exact fixture baseline."));return false;
            }
        }
        if (bWorkshopUI) {
            if (!GameUI.IsValid() || GameUI->HasActiveInteractWidget() || Selected->GetWorkBenchUser() || Selected->IsProducing()) {
                Finish(false,TEXT("Workshop/UI ownership changed before use; refusing to replace another interaction."));return false;
            }
            bBenchTouched=true;
            // This is a direct use-interface invocation, not a fabricated E-key
            // trace. Real Blueprint/HUD code must create and initialize the UI.
            IFGUseableInterface::Execute_OnUse(SelectedOwner,Character,WorkshopUseState);
            IFGUseableInterface::Execute_OnUseStop(SelectedOwner,Character,WorkshopUseState);
            Phase=EPhase::OpeningWorkshop;PhaseStarted=FPlatformTime::Seconds();
            LastReason=TEXT("Real Workshop use interface invoked; waiting for its owned manufacturing UI.");
            Marker(TEXT("PENDING"),LastReason,Counts());
            return true;
        }
        bBenchTouched=true;
        Selected->SetWorkBenchUser(Character);Selected->SetInventory(Inv);Selected->SetRecipe(UFactoryFPVRadioRecipe::StaticClass());
        if (!Selected->CanProduce(UFactoryFPVRadioRecipe::StaticClass(),Inv)) {
            Finish(false,TEXT("Real Workshop CanProduce rejected the fully funded radio recipe."));return false;
        }
        NativeCraftDuration=Selected->GetCurrentDuration();NativeManufacturingSpeed=Selected->GetManufacturingSpeed();
        Phase=EPhase::Producing;PhaseStarted=LastProduce=FPlatformTime::Seconds();
        Marker(TEXT("PENDING"),TEXT("Calling real Workshop Produce(.1) at real-time intervals; no completion shortcut or radio grant is used."),Counts());
        return true;
    }
    bool CheckCosts() {
        const auto* Inv=Inventory.Get();
        if (!Inv || Inv->GetNoBuildCost() || Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())!=BeforeKits+1) return false;
        if (bProgression) return bWorkshopPaymentTransactionVerified && WorkshopPaymentTransactionCalls==1;
        for (int32 Index=0;Index<Ingredients.Num();++Index) {
            const int32 Local=Inv->GetNumItems(Ingredients[Index].ItemClass);
            const int32 Central=Depot.IsValid() ? Depot->GetNumItemsFromCentralStorage(Ingredients[Index].ItemClass) : 0;
            if (int64(BaselineIngredients[Index])+BaselineDepotIngredients[Index]-Local-Central!=Ingredients[Index].Amount) return false;
        }
        CapturePaymentInventory(*Inv,PaymentLocalIngredients,PaymentDepotIngredients);
        return true;
    }
    bool Tick() {
        if (bDone) return false;
        FString Error;
        if (!GuardPaths(Error,bProgression)) { Finish(false,Error);return false; }
        const double Now=FPlatformTime::Seconds();
        if (Now>Deadline) {
            Finish(false,TEXT("30-second kit QA deadline: ")+LastReason);return false;
        }
        if (!World.IsValid()) { Finish(false,TEXT("Disposable QA world was unloaded."));return false; }
        if (!Ready()) return !bDone;
        if (Phase==EPhase::Waiting) {
            if (Now>=NextSearch) { NextSearch=Now+.5;Prepare(); }
            return !bDone;
        }
        auto* Inv=Inventory.Get();auto* Work=Bench.Get();auto* Slot=Arms.Get();
        if (!Inv || !Work || !Slot || !Hangar.IsValid()) { Finish(false,TEXT("Crafting/equipment fixture became unavailable."));return false; }
        if (Phase==EPhase::OpeningWorkshop) {
            DiscoverWorkshop(Now);
        } else if (Phase==EPhase::SettlingWorkshop) {
            if (!WorkshopIsOwnedAndOpen() || !ManufacturingUI.IsValid() ||
                ReadObject<UClass>(ManufacturingUI.Get(),TEXT("mCurrentRecipe"))!=UFactoryFPVRadioRecipe::StaticClass() ||
                Work->GetCurrentRecipe()!=UFactoryFPVRadioRecipe::StaticClass()) {
                Finish(false,TEXT("Actual Workshop UI closed or changed recipe while settling."));return false;
            }
            if (Now-PhaseStarted>=.5 && GFrameCounter>=SettleFrame+2 && VerifyWorkshopHitTest() && WorkshopHitStableSamples>=2) {
                if (bCaptureEnabled) {
                    if (RequestCapture(WorkshopCapture,TEXT("Workshop.png"),true,Now)) Phase=EPhase::CapturingWorkshop;
                } else BeginWorkshopHold(Now);
            }
        } else if (Phase==EPhase::CapturingWorkshop) {
            if (!WorkshopIsOwnedAndOpen()) { Finish(false,TEXT("Actual Workshop UI closed during its capture."));return false; }
            if (!VerifyWorkshopHitTest()) { Finish(false,TEXT("Actual craft-button hit-test accessibility changed during Workshop capture; refusing to qualify an occluded UI."));return false; }
            if (CaptureFinished(WorkshopCapture,Now)) BeginWorkshopHold(Now);
        } else if (Phase==EPhase::Producing) {
            const int32 Kits=Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass());
            if (Kits!=BeforeKits) {
                if (Kits!=BeforeKits+1) { Finish(false,TEXT("Timed Workshop output differs from exactly one radio."));return false; }
                bCraftedExactlyOne=true;CraftSeconds=Now-PhaseStarted;
                if (bWorkshopUI) {
                    ReleaseWorkshopHold();
                    bWorkshopProduced=bWorkshopButtonHeld && bWorkshopButtonReleased && ProduceCalls==0;
                    if (!bWorkshopProduced) { Finish(false,TEXT("UI produced a radio but the real held button was not cleanly released."));return false; }
                    Phase=EPhase::ClosingWorkshop;PhaseStarted=Now;
                    LastReason=TEXT("Workshop UI produced exactly one radio; button released, waiting for normal UI closure and bench-user release.");
                    Marker(TEXT("PENDING"),LastReason,Counts());
                    return true;
                }
                // A real player leaves the workbench before using equipment.
                // Stop issuing Produce immediately and restore the bench before
                // waiting for any deferred inventory/Depot payment notification.
                RestoreBench();
                Phase=EPhase::AwaitPayment;PhaseStarted=Now;
                LastReason=TEXT("One radio produced; waiting up to two seconds for exact combined inventory plus Dimensional Depot payment.");
                Marker(TEXT("PENDING"),LastReason,Counts());
            } else if (bWorkshopUI) {
                if (!WorkshopIsOwnedAndOpen() || !ManufacturingButton.IsValid() || !ManufacturingButton->IsButtonHeld() ||
                    !ButtonIsActuallyAvailable() || Work->GetWorkBenchUser()!=Pioneer.Get() ||
                    Work->GetCurrentRecipe()!=UFactoryFPVRadioRecipe::StaticClass()) {
                    Finish(false,TEXT("Real Workshop UI hold, enabled state, recipe or pioneer ownership changed before output."));return false;
                }
                ProduceProgressBefore=ProduceProgressAfter;
                ProduceProgressAfter=Work->GetManufacturingProgress();
                LastReason=FString::Printf(TEXT("Waiting for natural Workshop UI hold ticks to produce one radio; progress %.6f."),ProduceProgressAfter);
            } else if (Now-LastProduce>=.099) {
                LastProduce=Now;++ProduceCalls;
                ProduceProgressBefore=Work->GetManufacturingProgress();
                LastReason=FString::Printf(TEXT("Timed Workshop Produce(.1) has not yielded a radio (%d calls, progress %.6f)."),ProduceCalls,ProduceProgressBefore);
                Work->Produce(.1f);
                ProduceProgressAfter=Work->GetManufacturingProgress();
            }
        } else if (Phase==EPhase::ClosingWorkshop) {
            if (CloseWorkshop()) {
                StopObservingWorkshopPayment();
                RestoreBench();
                Phase=EPhase::AwaitPayment;PhaseStarted=Now;
                LastReason=TEXT("Real Workshop UI closed and released the pioneer; waiting for exact inventory/Depot payment.");
            } else if (Now-PhaseStarted>=2.) {
                Finish(false,TEXT("Normal Workshop OnEscapePressed did not close the owned UI and release its pioneer within two seconds."));return false;
            }
        } else if (Phase==EPhase::AwaitPayment) {
            PaymentWaitSeconds=Now-PhaseStarted;
            if (CheckCosts()) {
                bPaymentVerified=true;
                int32 KitIndex=INDEX_NONE;
                for (int32 Index=0;Index<Inv->GetSizeLinear();++Index) {
                    FInventoryStack Stack;
                    if (!BeforeRadioIndices.Contains(Index) && Inv->GetStackFromIndex(Index,Stack) && Stack.NumItems==1 &&
                        Stack.Item.GetItemClass()==UFactoryFPVRadioDescriptor::StaticClass()) { KitIndex=Index;break; }
                }
                if (KitIndex==INDEX_NONE || !UFGInventoryLibrary::MoveInventoryItem(Inv,KitIndex,Slot,ArmIndex)) {
                    Finish(false,TEXT("Craft completed but native inventory-to-arms move failed."));return false;
                }
                bMoved=true;ReturnIndex=KitIndex;
                ++NativeEquipActions;Slot->SetActiveEquipmentIndex(ArmIndex);
                Phase=EPhase::Equipping;PhaseStarted=Now;
                LastReason=TEXT("Waiting for the real slotted radio actor to become active and equipped.");
                Marker(TEXT("PENDING"),TEXT("Exactly one radio was crafted with exact combined inventory/Dimensional Depot consumption, then moved using the native inventory API."),Counts());
            } else if (PaymentWaitSeconds>=2.) {
                Finish(false,bProgression ? TEXT("Workshop produced one radio without one observed exact native ingredient transaction.") :
                    TEXT("Workshop produced one radio but exact combined inventory/Dimensional Depot cost delta was not observed within two seconds; no payment shortcut was used."));return false;
            }
        } else if (Phase==EPhase::Equipping) {
            if (EquippedRadio()) {
                bNativeEquipVerified=true;
                if (Hangar->IsPanelOpen()) { Finish(false,TEXT("Hangar was already open before the primary action."));return false; }
                if (bCaptureEnabled) {
                    if (Controller->GetViewTarget()!=Pioneer.Get()) {
                        Finish(false,TEXT("First-person evidence requires the pioneer to remain the actual view target; refusing to override it."));return false;
                    }
                    OldCameraMode=Pioneer->GetCameraMode();
                    if (OldCameraMode!=ECameraMode::ECM_FirstPerson) {
                        bCameraModeChanged=true;
                        Pioneer->SetCameraMode(ECameraMode::ECM_FirstPerson);
                    }
                    Phase=EPhase::SettlingFirstPerson;PhaseStarted=Now;SettleFrame=GFrameCounter;
                    LastReason=TEXT("Native radio equipped; letting the real first-person equip pose settle before viewport capture.");
                } else SendPrimary(Now);
            }
        } else if (Phase==EPhase::SettlingFirstPerson) {
            if (Now-PhaseStarted>=1. && GFrameCounter>=SettleFrame+2) {
                if (!FirstPersonViewReady() || Hangar->IsPanelOpen()) {
                    Finish(false,TEXT("Settled first-person camera, actual pioneer view target and native radio equip were not retained."));return false;
                }
                if (RequestCapture(FirstPersonCapture,TEXT("Radio-FirstPerson.png"),false,Now)) Phase=EPhase::CapturingFirstPerson;
            }
        } else if (Phase==EPhase::CapturingFirstPerson) {
            if (!FirstPersonViewReady() || Hangar->IsPanelOpen()) {
                Finish(false,TEXT("First-person view or radio changed while capturing the equipped radio."));return false;
            }
            if (CaptureFinished(FirstPersonCapture,Now)) SendPrimary(Now);
        } else if (Phase==EPhase::Opening && Hangar.IsValid() && Hangar->IsPanelOpen()) {
            bOpened=true;
            if (bCaptureEnabled) {
                Phase=EPhase::SettlingHangar;PhaseStarted=Now;SettleFrame=GFrameCounter;
                LastReason=TEXT("Public Primary opened the actual hangar; waiting for its Slate layout and rendering before capture.");
            } else CompleteKit(Now,bWorkshopUI ?
                TEXT("Real Workshop UI selection and held craft button produced one paid radio; native equip and Primary opened the hangar. No physical pointer/E trace, unlock, radio grant, save or flight was used.") :
                TEXT("Timed native Workshop produced one paid radio; inventory equip created/activated its actor; public primary action opened the hangar. No recipe unlock, radio grant, save or flight occurred."));
        } else if (Phase==EPhase::SettlingHangar) {
            if (!Hangar->IsPanelOpen()) { Finish(false,TEXT("Hangar closed before its viewport capture."));return false; }
            if (Now-PhaseStarted>=.5 && GFrameCounter>=SettleFrame+2 &&
                RequestCapture(HangarCapture,TEXT("Hangar.png"),true,Now)) Phase=EPhase::CapturingHangar;
        } else if (Phase==EPhase::CapturingHangar) {
            if (!Hangar->IsPanelOpen()) { Finish(false,TEXT("Hangar closed during its viewport capture."));return false; }
            if (CaptureFinished(HangarCapture,Now)) {
                CompleteKit(Now,bWorkshopUI ?
                    TEXT("Real Workshop UI selected and held its craft button to produce one paid radio; native equip and Primary opened the hangar. Fresh Workshop, first-person and hangar captures were decoded; physical input and visual fit remain separate acceptance.") :
                    TEXT("Timed native Workshop produced one paid radio; native inventory equip and public Primary opened the hangar. Fresh actual first-person and hangar PNGs were decoded for separate visual review. No recipe unlock, radio grant, save or flight occurred."));
            }
        } else if (Phase==EPhase::ClosingForEquipmentFlight) {
            if (Now-PhaseStarted<.25 || GFrameCounter<EquipmentPhaseFrame+2) return true;
            FlightRadio=EquippedRadio();
            if (!FlightRadio.IsValid() || GetDefault<UFactoryFPVSettings>()->bAllowSandboxAccess || !SnapshotEquipmentState()) {
                Finish(false,TEXT("Native equipped radio, disabled sandbox policy or preflight restoration snapshot became unavailable."));return false;
            }
            if (bPreparationOnly) {
                RestoreBench();ReleaseScreenshot();RestoreSandboxPolicy();
                const bool Clean=bNoKitLaunchRefused && bNoKitSettingsAccessible && bPaymentVerified && bCraftedExactlyOne && bNativeEquipVerified &&
                    NativeEquipActions==1 && bPrimarySent && bOpened && bWorkshopUI && bWorkshopProduced && bWorkshopClosed &&
                    !bBenchTouched && !bCameraModeChanged && bSandboxPolicyRestored && NoEquipmentModResources() && EquippedRadio()==FlightRadio.Get() &&
                    (!bCaptureEnabled || (Pioneer->GetCameraMode()==OldCameraMode && WorkshopCapture.bDecoded && FirstPersonCapture.bDecoded && HangarCapture.bDecoded));
                if (!Clean) { Finish(false,TEXT("Paid-kit handoff did not restore its owned bench/UI/camera/policy or retain the exact native-equipped item."));return false; }
                bPreparationHandedOff=true;bDone=true;
                const FString Evidence=Counts();Marker(TEXT("PENDING"),TEXT("Paid native Workshop kit remains equipped; owned presentation/bench restored and ownership transferred to save QA."),Evidence);
                auto Completion=MoveTemp(PreparationCompletion);if (Completion) Completion(true,TEXT("Paid/native-equipped radio prepared without saving or granting a kit."),TEXT("{")+Evidence.Mid(1)+TEXT("}"));
                return false;
            }
            FString LaunchError;EquipmentDrone=AFactoryFPVDrone::Launch(Controller.Get(),TEXT("keyboard"),LaunchError);
            auto* Own=EquipmentDrone.Get();
            bEquippedFlightLaunched=Own && !Own->IsFlightArmed() && Controller->GetPawn()==Pioneer.Get() && Controller->GetViewTarget()==Own && EquippedRadio()==FlightRadio.Get();
            if (!bEquippedFlightLaunched) { Finish(false,TEXT("Ordinary sandbox-disabled equipped launch failed without movement or input overrides: ")+LaunchError);return false; }
            TInlineComponentArray<UInputComponent*> Inputs(Own);for (auto* Input:Inputs) EquipmentSessionInputs.Add(Input);
            for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()) {
                auto* Owner=It->GetOwner();
                if (Owner && !Owner->IsActorBeingDestroyed() && (Owner==Own || Cast<AFactoryFPVPioneerAnchor>(Owner))) EquipmentSources.Add(*It);
            }
            if (EquipmentSessionInputs.IsEmpty() || EquipmentSources.Num()<2) { Finish(false,TEXT("Real equipped session did not expose its actual controls and drone/pioneer sources."));return false; }
            Phase=EPhase::EquippedFlight;PhaseStarted=Now;EquipmentPhaseFrame=GFrameCounter;
            LastReason=TEXT("Ordinary equipped keyboard session launched disarmed with sandbox disabled; no flight controls injected.");
            Marker(TEXT("PENDING"),LastReason,Counts());
        } else if (Phase==EPhase::EquippedFlight) {
            auto* Own=EquipmentDrone.Get();const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller.Get());
            auto* Move=Pioneer->GetCharacterMovement();
            if (!Own || Own->IsActorBeingDestroyed() || Own->IsFlightArmed() || Controller->GetViewTarget()!=Own || EquippedRadio()!=FlightRadio.Get() ||
                !Move || !Pioneer->GetActorLocation().Equals(EquipmentPioneerLocation,.5) || Move->MovementMode!=MOVE_None ||
                !Controller->IsMoveInputIgnored() || !Controller->IsLookInputIgnored() || Lease.ActiveLeases!=1 || Lease.OwnedMoveContributions!=1 || Lease.OwnedLookContributions!=1) {
                Finish(false,TEXT("Equipped session failed to retain its live disarmed pioneer/view/input ownership before native unequip."));return false;
            }
            for (const auto& Input:EquipmentSessionInputs) if (!Input.IsValid() || !Controller->IsInputComponentInStack(Input.Get())) {
                Finish(false,TEXT("Actual equipped-session input component left the controller stack before native unequip."));return false;
            }
            for (const auto& Source:EquipmentSources) if (!Source.IsValid() || !Source->IsStreamingSourceEnabled()) {
                Finish(false,TEXT("Actual equipped-session streaming source was not enabled before native unequip."));return false;
            }
            if (Now-PhaseStarted<.25 || GFrameCounter<EquipmentPhaseFrame+2) return true;
            FInventoryStack Stack,Destination;
            if (Slot->GetActiveIndex()!=ArmIndex || Slot->GetStackEquipmentActorAtIdx(ArmIndex)!=FlightRadio.Get() || !Slot->GetStackFromIndex(ArmIndex,Stack) ||
                Stack.NumItems!=1 || Stack.Item.GetItemClass()!=UFactoryFPVRadioDescriptor::StaticClass()) {
                Finish(false,TEXT("The exact crafted radio is no longer the actual active arms stack."));return false;
            }
            int32 DestinationIndex=ReturnIndex;
            if (DestinationIndex==INDEX_NONE || (Inv->GetStackFromIndex(DestinationIndex,Destination) && Destination.HasItems())) DestinationIndex=Inv->FindEmptyIndex();
            if (DestinationIndex==INDEX_NONE || !UFGInventoryLibrary::MoveInventoryItem(Slot,ArmIndex,Inv,DestinationIndex)) {
                Finish(false,TEXT("Native active-radio arms-to-inventory move refused; no direct unequip/return fallback was used."));return false;
            }
            ReturnIndex=DestinationIndex;bMoved=false;bNativeInventoryMove=true;
            Phase=EPhase::AwaitingNativeUnequip;PhaseStarted=Now;EquipmentPhaseFrame=GFrameCounter;
            LastReason=TEXT("Native inventory move completed; waiting for its normal UnEquip callback and return. ExitDrone has not been called by the fixture.");
        } else if (Phase==EPhase::AwaitingNativeUnequip) {
            if (EquipmentDrone.IsValid() && EquipmentDrone->IsFlightArmed()) { Finish(false,TEXT("Equipment-flight fixture armed unexpectedly."));return false; }
            auto* CurrentEquipment=Pioneer->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS);
            bNativeEquipmentEmpty=CurrentEquipment==nullptr;
            bOriginalEquipmentReactivated=OriginalArmsEquipment.IsValid() && CurrentEquipment==OriginalArmsEquipment.Get() &&
                Slot->GetActiveIndex()==PreviousActive && OriginalArmsEquipment->IsEquipped();
            FInventoryStack Stack,Returned;
            const bool EmptyCraftedSlot=!Slot->GetStackFromIndex(ArmIndex,Stack) || !Stack.HasItems();
            const bool ItemReturned=Inv->GetStackFromIndex(ReturnIndex,Returned) && Returned.NumItems==1 && Returned.Item.GetItemClass()==UFactoryFPVRadioDescriptor::StaticClass() &&
                Inv->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass())==BeforeKits+1;
            bNativeUnequipReturned=EmptyCraftedSlot && ItemReturned && (bNativeEquipmentEmpty || bOriginalEquipmentReactivated) && !AFactoryFPVRadioEquipment::IsEquippedBy(Controller.Get()) &&
                (!FlightRadio.IsValid() || !FlightRadio->IsEquipped()) && !AFactoryFPVDrone::FindActive(World.Get()) &&
                (!EquipmentDrone.IsValid() || (EquipmentDrone->IsActorBeingDestroyed() && !EquipmentDrone->IsFlightOverlayVisible()));
            if (Now-PhaseStarted>=.25 && GFrameCounter>=EquipmentPhaseFrame+2 && bNativeUnequipReturned && CheckEquipmentState(true)) {
                bEquipmentFlightStateRestored=true;
                Finish(true,TEXT("One paid native-crafted radio passes sandbox-disabled no-kit/F6 policy, disarmed equipped launch and real inventory-triggered unequip return with original living pioneer/view/HUD/input/body/source restoration. No physical controls, flight inputs or saved settings."));
            } else LastReason=TEXT("Waiting for native active-radio unequip, exact returned item and pioneer/camera/HUD/input/body/source restoration; no manual Exit fallback.");
        }
        return !bDone;
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingKitQA::Begin(UWorld* World,FOutputDevice& Output) {
    using namespace FactoryFPVShippingKitQAInternal;
    const FString Stage=ProgressionStage();
    const bool bProgression=Stage.Equals(TEXT("kit"),ESearchCase::CaseSensitive);
    FString Error;
    if (!Stage.IsEmpty() && !bProgression) Error=TEXT("Kit QA only accepts -FactoryFPVQAProgression=kit.");
    if (!Error.IsEmpty() || !GuardPaths(Error,bProgression)) {
        Marker(TEXT("FAIL"),Error);Output.Log(*Error);
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    if (!World || World->GetNetMode()!=NM_Standalone) {
        Marker(TEXT("FAIL"),TEXT("A standalone game world is required."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin(); Existing && !Existing->bDone) { Output.Log(TEXT("Kit QA is already running."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Deadline=FPlatformTime::Seconds()+30.;
    Run->bProgression=bProgression;
    if (bProgression) Run->LastReason=TEXT("Waiting for the exact native progression handoff and living pioneer.");
    Run->bWorkshopUI=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAWorkshopUI"));
    Run->bEquipmentFlight=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAEquipmentFlight"));
    Run->bCaptureEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Active=Run;
    Marker(TEXT("PENDING"),TEXT("Guarded disposable kit QA queued; only an existing real Workshop will be used."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.1f);
}

bool FFactoryFPVShippingKitQA::PrepareEquippedKit(UWorld* World,TFunction<void(bool,const FString&,const FString&)> Completion,FSimpleDelegate& OutCancel) {
    using namespace FactoryFPVShippingKitQAInternal;
    OutCancel.Unbind();FString Error;
    if (!Completion || !ProgressionStage().IsEmpty() || !GuardPaths(Error) || !World || World->GetNetMode()!=NM_Standalone ||
        !FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAKitPersistence")) ||
        !FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAWorkshopUI"))) return false;
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) return false;
    auto Run=MakeShared<FRun>();Run->World=World;Run->Deadline=FPlatformTime::Seconds()+30.;
    Run->bPreparationOnly=true;Run->bWorkshopUI=true;Run->bEquipmentFlight=true;
    Run->bCaptureEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));
    Run->PreparationCompletion=MoveTemp(Completion);Active=Run;
    const TWeakPtr<FRun> Weak=Run;
    OutCancel=FSimpleDelegate::CreateLambda([Weak] { if (auto R=Weak.Pin();R && !R->bDone) R->Finish(false,TEXT("Preparation cancelled by its owning save qualification.")); });
    Marker(TEXT("PENDING"),TEXT("Save-owned preparation queued alongside startup observation; actual native Workshop UI/payment/equip only, no kit grant or save."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.1f);
    return true;
}
