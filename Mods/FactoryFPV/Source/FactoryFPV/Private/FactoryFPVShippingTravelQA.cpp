#include "FactoryFPVShippingTravelQA.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHUD.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Online/FGSessionSettings.h"
#include "OnlineAsyncOperation.h"
#include "SessionMigrationSequence.h"
#include "Sessions/SessionDefinition.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/ObjectKey.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

namespace FactoryFPVShippingTravelQAInternal
{
FString Escape(FString Text)
{
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
const TCHAR* Bool(bool Value) { return Value?TEXT("true"):TEXT("false"); }
FString ClassPath(const UObject* Object) { return Object?Object->GetClass()->GetPathName():FString(); }
FString Number(float Value) { return FMath::IsFinite(Value)?FString::Printf(TEXT("%.9f"),Value):TEXT("null"); }
bool Disposable(FString Path)
{
    if (Path.IsEmpty()) return false;Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);
    return FPaths::CollapseRelativeDirectories(Path) && Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard() { return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) && Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath()); }
void Marker(const FString& Mode,const TCHAR* Result,const FString& Reason,const FString& Extra=FString())
{
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"travel-%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"one native public LoadSaveFile reload of the same disposable local clone; actual old-world EndPlay and new-world readiness, excludes physical flight and migration bypass\"%s}"),*Escape(Mode),Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double LumaVariance=0;
    FString Json() const { return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Width,Height,Bool(bDecoded),LumaVariance); }
};
struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,BeforeSettle,CaptureBefore,Migrating,ReloadedSettle,CaptureReloaded,HangarOpened,HangarClosed,FreshFlight,FreshReturn,CaptureFreshReturn };
    TWeakObjectPtr<UWorld> OldWorld,NewWorld;
    TWeakObjectPtr<UGameInstance> Instance;
    TWeakObjectPtr<ULocalPlayer> Player;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<AFGPlayerController> OldPC,NewPC;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TWeakObjectPtr<USessionMigrationSequence> Operation;
    TArray<TWeakObjectPtr<AActor>> OldModActors;
    TArray<TWeakObjectPtr<UInputComponent>> OldModControls;
    TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> OldModSources;
    TArray<TWeakPtr<SWidget>> OldModWidgets;
    TSet<FObjectKey> PostLoadedKeys;
    FObjectKey OldWorldKey,InstanceKey,PlayerKey;
    FString Mode,LastReason=TEXT("Waiting for the exact local clone and native startup."),OldWorldPath,NewWorldPath,PendingScreenshot;
    FString SessionDefinitions=TEXT("{}"),OperationStatus,OperationErrors=TEXT("[]"),TravelFailureReason,NativeTextProbe=TEXT("[]");
    FString LoadedHeaderName,RequestedLoadName;
    TMap<FString,FString> Snapshots;
    FCapture BeforeCapture,ReloadedCapture,FreshReturnCapture;
    FDelegateHandle TeardownHandle,CleanupHandle,PostLoadHandle,TravelFailureHandle,ScreenshotHandle;
    EPhase Phase=EPhase::Waiting;
    double Deadline=0,PhaseStarted=0,RequestTime=0,ReadySince=0,CaptureStarted=0;
    uint64 PhaseFrame=0;
    bool bDone=false,bPrepared=false,bOwnedPanel=false,bCapture=false,bOwnScreenshotPending=false,bScreenshotProcessed=false,bScreenMessagesBefore=false;
    bool bInitialSplashObserved=false,bInitialAnimationObserved=false,bInitialStartupFinished=false,bInitialFrontEndFinished=false;
    bool bNewSplashObserved=false,bNewAnimationObserved=false,bNewStartupFinished=false,bNewFrontEndFinished=false;
    bool bTravelRequested=false,bOldTeardown=false,bOldCleanup=false,bNewPostLoad=false,bNewReady=false,bOldResourcesRetired=false,bNewControlsClean=false;
    bool bHangarRebound=false,bFreshSessionPassed=false,bNoModLeases=false,bCapturesVerified=false;
    bool bOperationFinished=false,bOperationHasErrors=false,bNullPostLoad=false,bTravelFailed=false;
    int32 PostLoadEvents=0;

    void SetPhase(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    bool Settled(double Now,double Seconds=.5) const { return Now-PhaseStarted>=Seconds && GFrameCounter>=PhaseFrame+3; }
    UWorld* CurrentWorld() const
    {
        if (Player.IsValid() && Player->GetWorld()) return Player->GetWorld();
        return Instance.IsValid()?Instance->GetWorld():nullptr;
    }
    AFGPlayerController* CurrentPC() const { auto* World=CurrentWorld();return World?Cast<AFGPlayerController>(World->GetFirstPlayerController()):nullptr; }
    TSharedPtr<SWindow> Window() const { return Viewport.IsValid()?Viewport->GetWindow():nullptr; }
    bool SameClone(UWorld* World) const
    {
        auto* ModeObject=World?World->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=ModeObject?ModeObject->GetSaveSession():nullptr;if (!Save || Save->HasSaveGameOperationPending()) return false;
        const auto Load=ModeObject->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        return (Load.LoadName==TEXT("FPV-QA-Clone") || Header.SaveName==TEXT("FPV-QA-Clone")) && !Header.MapName.IsEmpty() && !Header.SessionName.IsEmpty();
    }
    bool Grounded(AFGCharacterPlayer* Pawn) const
    {
        auto* Move=Pawn?Pawn->GetCharacterMovement():nullptr;if (!Move || !Move->IsMovingOnGround()) return false;
        FFindFloorResult Floor;Move->FindFloor(Pawn->GetActorLocation(),Floor,false);return Floor.IsWalkableFloor();
    }
    bool NoModLeases(APlayerController* PC) const
    {
        const auto D=FactoryFPVInputLease::GetDiagnostics(PC);return D.ActiveLeases==0 && D.OwnedMoveContributions==0 && D.OwnedLookContributions==0 && D.ResetDepth==0;
    }
    int32 F6Bindings(APlayerController* PC,FInputActionUnifiedDelegate* Out=nullptr) const
    {
        if (!PC || !Hangar.IsValid()) return 0;int32 Count=0;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==PC->GetWorld() && PC->IsInputComponentInStack(*It))
            for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Hangar.Get())) { if (Out) *Out=Binding.KeyDelegate;++Count; }
        return Count;
    }
    bool InvokeF6(APlayerController* PC)
    {
        FInputActionUnifiedDelegate Callback;if (F6Bindings(PC,&Callback)!=1) return false;Callback.Execute(EKeys::F6);return true;
    }
    TArray<TSharedRef<SWidget>> NativeWindowWidgets() const
    {
        TArray<TSharedRef<SWidget>> Result;auto Root=Window();if (!Root) return Result;
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Root.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768)
        {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());Result.Add(Widget);
            if (auto* Children=Widget->GetChildren()) for (int32 Index=0;Index<Children->Num();++Index) Pending.Add(Children->GetChildAt(Index));
        }
        return Result;
    }
    bool Presented(const TSharedRef<SWidget>& Widget) const
    {
        auto Root=Window();if (!Root || !FSlateApplication::IsInitialized() || FSlateApplication::Get().FindWidgetWindow(Widget)!=Root) return false;
        const auto& Geometry=Widget->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1 || Size.Y<1 || !Root->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(Size*.5f))) return false;
        auto Parent=TSharedPtr<SWidget>(Widget);float Opacity=1;
        for (int32 Depth=0;Parent && Depth<128;++Depth) { if (!Parent->GetVisibility().IsVisible()) return false;Opacity*=Parent->GetRenderOpacity();Parent=Parent->GetParentWidget(); }
        return !Parent && FMath::IsFinite(Opacity) && Opacity>.01f;
    }
    bool IsModPresentation(const SWidget& Widget) const
    {
        return Widget.GetType()==FName(TEXT("SFactoryFPVOSD")) || Widget.GetType()==FName(TEXT("SFactoryFPVPanel")) || Widget.GetType()==FName(TEXT("SFactoryFPVRacePanel"));
    }
    bool NoModPresentation() const
    {
        for (const auto& Widget:NativeWindowWidgets()) if (IsModPresentation(Widget.Get()) && Presented(Widget)) return false;return true;
    }
    void UpdateNativeText()
    {
        TArray<FString> Entries;
        for (const auto& Widget:NativeWindowWidgets())
        {
            if (Entries.Num()>=32) break;if (!Presented(Widget)) continue;FString Text;
            if (Widget->GetType()==FName(TEXT("STextBlock"))) Text=StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
            else if (Widget->GetType()==FName(TEXT("SRichTextBlock"))) Text=StaticCastSharedRef<SRichTextBlock>(Widget)->GetText().ToString();
            // Record which actual native UI categories are visible, not raw
            // connection/login text that could contain a code or signed URL.
            FString Category;
            for (const TCHAR* Candidate:{TEXT("offline"),TEXT("login"),TEXT("sign in"),TEXT("session"),TEXT("loading"),TEXT("connecting"),TEXT("incompatible"),TEXT("failed"),TEXT("mod")})
                if (Text.Contains(Candidate,ESearchCase::IgnoreCase)) { Category=Candidate;break; }
            if (!Category.IsEmpty()) Entries.Add(FString::Printf(TEXT("{\"type\":\"%s\",\"tag\":\"%s\",\"observedTextCategory\":\"%s\"}"),*Escape(Widget->GetTypeAsString()),*Escape(Widget->GetTag().ToString()),*Escape(Category)));
        }
        NativeTextProbe=TEXT("[")+FString::Join(Entries,TEXT(","))+TEXT("]");
    }
    void UpdateOperation()
    {
        auto* Op=Operation.Get();if (!Op) return;bOperationHasErrors=Op->HasErrors();
        OperationStatus.Reset();
        if (const FProperty* Status=FindFProperty<FProperty>(Op->GetClass(),TEXT("Status")))
            Status->ExportTextItem_Direct(OperationStatus,Status->ContainerPtrToValuePtr<void>(Op),nullptr,Op,PPF_None);
        TArray<FString> Errors;
        for (const auto& Error:Op->GetErrors())
        {
            // Keep public numeric codes (including inner errors), without
            // formatted provider text or an SDK-only error formatter export.
            TArray<FString> Codes;
            for (const UE::Online::FOnlineError* Part=&Error;Part;Part=Part->GetInner())
                Codes.Add(FString::Printf(TEXT("%016llX"),static_cast<unsigned long long>(Part->GetErrorCode())));
            Errors.Add(TEXT("\"")+FString::Join(Codes,TEXT("-"))+TEXT("\""));
        }
        OperationErrors=TEXT("[")+FString::Join(Errors,TEXT(","))+TEXT("]");
    }
    FString Snapshot(UWorld* World,APlayerController* PC) const
    {
        auto* Hud=PC?Cast<AFGHUD>(PC->GetHUD()):nullptr;auto* Camera=PC?PC->PlayerCameraManager.Get():nullptr;APawn* Pawn=PC?PC->GetPawn():nullptr;auto* View=PC?PC->GetViewTarget():nullptr;
        auto* GI=World?World->GetGameInstance():nullptr;auto* Local=PC?PC->GetLocalPlayer():nullptr;const auto Lease=FactoryFPVInputLease::GetDiagnostics(PC);
        return FString::Printf(TEXT("{\"fov\":%s,\"lockedFov\":%s,\"hudPartial\":%s,\"hudVisible\":%s,\"hudShow\":%s,\"cursor\":%s,\"viewportIgnoresInput\":%s,\"moveIgnored\":%s,\"lookIgnored\":%s,\"viewTargetClass\":\"%s\",\"currentPawnClass\":\"%s\",\"worldPath\":\"%s\",\"worldKeyHash\":%u,\"gameInstanceKeyHash\":%u,\"localPlayerKeyHash\":%u,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"f6Bindings\":%d,\"selectedCourse\":%d,\"netMode\":%d}"),
            Camera?*Number(Camera->GetFOVAngle()):TEXT("null"),Camera?*Number(Camera->GetLockedFOV()):TEXT("null"),Bool(Hud && Hud->GetPartialPumpiMode()),Bool(Hud && Hud->GetHUDVisibility()),Bool(Hud && Hud->bShowHUD),Bool(PC && PC->bShowMouseCursor),
            Bool(Viewport.IsValid() && Viewport->IgnoreInput()),Bool(PC && PC->IsMoveInputIgnored()),Bool(PC && PC->IsLookInputIgnored()),*Escape(ClassPath(View)),*Escape(ClassPath(Pawn)),*Escape(GetPathNameSafe(World)),
            GetTypeHash(FObjectKey(World)),GetTypeHash(FObjectKey(GI)),GetTypeHash(FObjectKey(Local)),Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions,F6Bindings(PC),Hangar.IsValid()?Hangar->GetSelectedRacingCourse():0,World?static_cast<int32>(World->GetNetMode()):INDEX_NONE);
    }
    FString Evidence() const
    {
        FString Out=FString::Printf(TEXT(",\"mode\":\"%s\",\"variant\":\"%s\",\"travelRequested\":%s,\"oldWorldTeardownObserved\":%s,\"oldWorldCleanupObserved\":%s,\"newWorldPostLoadObserved\":%s,\"newWorldReady\":%s,\"oldModResourcesRetired\":%s,\"newWorldControlsClean\":%s,\"hangarRebound\":%s,\"freshSessionPassed\":%s,\"noModLeases\":%s,\"capturesVerified\":%s,\"programmaticNativeLoad\":true,\"programmaticMenuActions\":true,\"injectedFlightControls\":false,\"forcedMigrationCompletion\":false,\"savedWorld\":false"),
            *Escape(Mode),*Escape(Mode),Bool(bTravelRequested),Bool(bOldTeardown),Bool(bOldCleanup),Bool(bNewPostLoad),Bool(bNewReady),Bool(bOldResourcesRetired),Bool(bNewControlsClean),Bool(bHangarRebound),Bool(bFreshSessionPassed),Bool(bNoModLeases),Bool(bCapturesVerified));
        Out+=FString::Printf(TEXT(",\"oldWorldPath\":\"%s\",\"newWorldPath\":\"%s\",\"oldWorldKeyHash\":%u,\"newWorldKeyHash\":%u,\"sameGameInstance\":%s,\"sameLocalPlayer\":%s,\"loadedHeaderName\":\"%s\",\"requestedLoadName\":\"%s\",\"sessionDefinitions\":%s,\"operationFinishedObserved\":%s,\"operationHasErrors\":%s,\"operationStatus\":\"%s\",\"operationErrors\":%s,\"travelFailure\":\"%s\",\"nullPostLoadObserved\":%s,\"postLoadEvents\":%d,\"nativeVisibleText\":%s,\"newSplashObserved\":%s,\"newSplashAnimationObserved\":%s,\"newStartupFinished\":%s"),
            *Escape(OldWorldPath),*Escape(NewWorldPath),GetTypeHash(OldWorldKey),GetTypeHash(FObjectKey(NewWorld.Get())),Bool(Instance.IsValid() && FObjectKey(Instance.Get())==InstanceKey),Bool(Player.IsValid() && FObjectKey(Player.Get())==PlayerKey),
            *Escape(LoadedHeaderName),*Escape(RequestedLoadName),*SessionDefinitions,Bool(bOperationFinished),Bool(bOperationHasErrors),*Escape(OperationStatus),*OperationErrors,*Escape(TravelFailureReason),Bool(bNullPostLoad),PostLoadEvents,*NativeTextProbe,Bool(bNewSplashObserved),Bool(bNewAnimationObserved),Bool(bNewStartupFinished));
        Out+=TEXT(",\"snapshots\":{");bool First=true;for (const TCHAR* Name:{TEXT("initial"),TEXT("before"),TEXT("oldCleanup"),TEXT("reloaded"),TEXT("freshReturn")})
            if (const FString* Value=Snapshots.Find(Name)) { if (!First) Out+=TEXT(",");First=false;Out+=FString::Printf(TEXT("\"%s\":%s"),Name,**Value); }
        Out+=TEXT("},\"captures\":[");if (bCapture) Out+=BeforeCapture.Json()+TEXT(",")+ReloadedCapture.Json()+TEXT(",")+FreshReturnCapture.Json();return Out+TEXT("]");
    }
    bool InitialReady()
    {
        auto* World=OldWorld.Get();auto* PC=World?Cast<AFGPlayerController>(World->GetFirstPlayerController()):nullptr;auto* Pawn=PC?Cast<AFGCharacterPlayer>(PC->GetPawn()):nullptr;
        if (!World || World->bIsTearingDown || World->GetNetMode()!=NM_Standalone || !World->HasBegunPlay() || !SameClone(World) || !PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return false;
        auto* UI=PC->GetGameUI();if (!UI || !FactoryFPVShippingStartup::Ready(UI,LastReason,bInitialSplashObserved,bInitialAnimationObserved,bInitialStartupFinished,bInitialFrontEndFinished)) return false;
        if (!Pawn || !Pawn->IsAliveAndWell() || !Grounded(Pawn) || !PC->PlayerCameraManager || !PC->GetHUD() || PC->NeedRespawn() || PC->IsRespawning() || PC->IsClientRespawnGracePeriodActive()
            || World->IsPaused() || PC->IsPaused() || PC->IsPauseMenuOpen() || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget()) return false;
        OldPC=PC;Instance=World->GetGameInstance();Player=PC->GetLocalPlayer();Viewport=World->GetGameViewport();Hangar=Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        return Instance.IsValid() && Player.IsValid() && Viewport.IsValid() && Hangar.IsValid();
    }
    bool PreflightSession()
    {
        auto* Settings=Instance.IsValid()?Instance->GetSubsystem<UFGSessionSettings>():nullptr;
        auto* Current=Settings?Settings->GetCurrentSessionDefinition():nullptr;auto* Active=Settings?Settings->GetActiveSessionDefinition():nullptr;
        auto DefinitionJson=[](USessionDefinition* Definition)
        {
            FString Out=FString::Printf(TEXT("{\"path\":\"%s\",\"class\":\"%s\",\"createsOnlineSession\":%s,\"travelArgumentKeys\":["),*Escape(GetPathNameSafe(Definition)),*Escape(ClassPath(Definition)),Bool(Definition && Definition->bCreateOnlineSession));bool First=true;
            if (Definition) for (const auto& Pair:Definition->GetTravelArguments()) { if (!First) Out+=TEXT(",");First=false;Out+=TEXT("\"")+Escape(Pair.Key)+TEXT("\""); }
            return Out+TEXT("]}");
        };
        SessionDefinitions=TEXT("{\"current\":")+DefinitionJson(Current)+TEXT(",\"active\":")+DefinitionJson(Active)+TEXT("}");
        if (!Settings || !Current) { Finish(false,TEXT("Native current session definition is unavailable; no migration requested."));return false; }
        if (Current->bCreateOnlineSession) { Finish(false,TEXT("Configured native reload would create an online session; local single-player QA refuses without changing settings."));return false; }
        return true;
    }
    void CollectOldResources()
    {
        for (TActorIterator<AActor> It(OldWorld.Get());It;++It) if (Cast<AFactoryFPVDrone>(*It) || Cast<AFactoryFPVPioneerAnchor>(*It) || Cast<AFactoryFPVPilotVisual>(*It)) OldModActors.AddUnique(*It);
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==OldWorld.Get()
            && (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner()))) OldModSources.AddUnique(*It);
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==OldWorld.Get() && OldPC->IsInputComponentInStack(*It)
            && (It->GetOwner()==Drone.Get() || (It->GetOwner()==OldPC.Get() && It->GetName().StartsWith(TEXT("FactoryFPVHangarControls"))))) OldModControls.AddUnique(*It);
        for (const auto& Widget:NativeWindowWidgets()) if (IsModPresentation(Widget.Get())) OldModWidgets.Add(Widget);
    }
    bool Prepare(double Now)
    {
        if (Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(OldWorld.Get()) || AFactoryFPVPioneerAnchor::FindPending(OldWorld.Get(),Cast<AFGCharacterPlayer>(OldPC->GetPawn()))
            || !NoModLeases(OldPC.Get()) || OldPC->IsMoveInputIgnored() || OldPC->IsLookInputIgnored()) { Finish(false,TEXT("Travel QA requires no existing mod/input owners."));return false; }
        if (!PreflightSession()) return false;
        OldWorldKey=FObjectKey(OldWorld.Get());InstanceKey=FObjectKey(Instance.Get());PlayerKey=FObjectKey(Player.Get());OldWorldPath=OldWorld->GetPathName();Snapshots.Add(TEXT("initial"),Snapshot(OldWorld.Get(),OldPC.Get()));
        if (Mode==TEXT("flight"))
        {
            FString Error;Drone=AFactoryFPVDrone::Launch(OldPC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Ordinary disarmed pre-travel launch failed: ")+Error);return false; }
        }
        else if (Mode==TEXT("panel"))
        {
            if (!InvokeF6(OldPC.Get())) return false;bOwnedPanel=Hangar->IsPanelOpen();if (!bOwnedPanel) { Finish(false,TEXT("Bound F6 did not open pre-travel hangar."));return false; }
        }
        CollectOldResources();bPrepared=true;SetPhase(EPhase::BeforeSettle,Now);return true;
    }
    void BindWorldObservers()
    {
        const TWeakPtr<FRun> Weak=AsShared();
        TeardownHandle=FWorldDelegates::OnWorldBeginTearDown.AddLambda([Weak](UWorld* World)
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bTravelRequested && World && FObjectKey(World)==Run->OldWorldKey) Run->bOldTeardown=true; });
        CleanupHandle=FWorldDelegates::OnWorldCleanup.AddLambda([Weak](UWorld* World,bool,bool)
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bTravelRequested && World && FObjectKey(World)==Run->OldWorldKey) { Run->bOldCleanup=true;Run->Snapshots.Add(TEXT("oldCleanup"),Run->Snapshot(World,Run->OldPC.Get())); } });
        PostLoadHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([Weak](UWorld* World)
        {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bTravelRequested)
            {
                if (!World) { Run->bNullPostLoad=true;return; }
                if (World->GetGameInstance()==Run->Instance.Get() && FObjectKey(World)!=Run->OldWorldKey)
                { ++Run->PostLoadEvents;Run->PostLoadedKeys.Add(FObjectKey(World));Run->NewWorld=World;Run->NewWorldPath=World->GetPathName(); }
            }
        });
        if (GEngine) TravelFailureHandle=GEngine->OnTravelFailure().AddLambda([Weak](UWorld* World,ETravelFailure::Type Failure,const FString&)
        {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bTravelRequested && (!World || World->GetGameInstance()==Run->Instance.Get()))
            { Run->bTravelFailed=true;Run->TravelFailureReason=FString::Printf(TEXT("Native travel failure type %d (connection details omitted)"),static_cast<int32>(Failure)); }
        });
    }
    void UnbindWorldObservers()
    {
        if (TeardownHandle.IsValid()) { FWorldDelegates::OnWorldBeginTearDown.Remove(TeardownHandle);TeardownHandle.Reset(); }
        if (CleanupHandle.IsValid()) { FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);CleanupHandle.Reset(); }
        if (PostLoadHandle.IsValid()) { FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);PostLoadHandle.Reset(); }
        if (TravelFailureHandle.IsValid() && GEngine) { GEngine->OnTravelFailure().Remove(TravelFailureHandle);TravelFailureHandle.Reset(); }
    }
    void RequestTravel(double Now)
    {
        auto* World=OldWorld.Get();auto* PC=OldPC.Get();auto* Pawn=PC?Cast<AFGCharacterPlayer>(PC->GetPawn()):nullptr;
        if (!World || World->bIsTearingDown || CurrentWorld()!=World || !PC || PC->IsActorBeingDestroyed()
            || World->GetFirstPlayerController()!=PC || PC->GetLocalPlayer()!=Player.Get() || !Pawn || !Pawn->IsAliveAndWell() || !Hangar.IsValid())
        { Finish(false,TEXT("Original native world/controller/pioneer changed before reload; no migration requested."));return; }
        const bool bVariantReady=Mode==TEXT("flight")
            ? Drone.IsValid() && AFactoryFPVDrone::FindActive(World)==Drone.Get() && !Drone->IsFlightArmed() && Drone->GetOwner()==PC && PC->GetViewTarget()==Drone.Get() && !Hangar->IsPanelOpen()
            : Mode==TEXT("panel") ? bOwnedPanel && Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(World)
            : !Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(World) && NoModLeases(PC);
        if (!bVariantReady) { Finish(false,TEXT("Requested flight/panel/baseline variant no longer owns its expected state at native load boundary."));return; }
        auto* SaveSystem=UFGSaveSystem::Get(OldWorld.Get());FSaveHeader Header;
        if (!SaveSystem || !SaveSystem->IsSaveManagerAvailable() || !SaveSystem->SaveGameExistsSync(TEXT("FPV-QA-Clone")) || !SaveSystem->LoadSaveGameHeaderSync(TEXT("FPV-QA-Clone"),Header))
        { Finish(false,TEXT("Actual disposable clone header could not be read; no migration requested."));return; }
        LoadedHeaderName=Header.SaveName;RequestedLoadName=TEXT("FPV-QA-Clone");
        if (Header.SaveName!=TEXT("FPV-QA-Clone") || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty())
        { Finish(false,TEXT("Read header does not identify exact FPV-QA-Clone; refusing an alias or another save."));return; }
        FString Resolved;if (!UFGSaveSystem::GetAbsolutePathForSaveGame(OldWorld.Get(),TEXT("FPV-QA-Clone"),Resolved) || !Disposable(Resolved))
        { Finish(false,TEXT("Actual clone resolves outside disposable QA paths."));return; }
        BindWorldObservers();bTravelRequested=true;RequestTime=Now;SetPhase(EPhase::Migrating,Now);
        // Native UFGSaveSystem starts its returned migration sequence itself.
        // Do not call Start again, use low-level SaveSession.LoadGame, or close
        // any mod owner before observing the real old-world cleanup.
        const FLoadSaveFileParameters Parameters;
        Operation=SaveSystem->LoadSaveFile(Header,Parameters,OldPC.Get());
        if (!Operation.IsValid()) { Finish(false,TEXT("Native LoadSaveFile returned no migration operation."));return; }
        const TWeakPtr<FRun> Weak=AsShared();
        Operation->AddOperationFinishedDelegate(FOnOnlineAsyncOperationCompleted_Native::FDelegate::CreateLambda([Weak](UOnlineAsyncOperation*)
        { if (auto Run=Weak.Pin();Run && !Run->bDone) { Run->bOperationFinished=true;Run->UpdateOperation(); } }));
        UpdateOperation();LastReason=TEXT("Waiting for native migration, actual old EndPlay/cleanup and the new same-clone world; no online prompt is accepted.");
    }
    bool NewWorldReady(double Now)
    {
        auto* World=CurrentWorld();if (!World || World->GetGameInstance()!=Instance.Get() || FObjectKey(World)==OldWorldKey || !PostLoadedKeys.Contains(FObjectKey(World))) return false;
        NewWorld=World;NewWorldPath=World->GetPathName();bNewPostLoad=true;
        if (World->bIsTearingDown || World->GetNetMode()!=NM_Standalone || !World->HasBegunPlay() || !SameClone(World)) return false;
        auto* PC=Cast<AFGPlayerController>(World->GetFirstPlayerController());if (!PC || !PC->IsLocalController() || PC->GetLocalPlayer()!=Player.Get() || !Player.IsValid()
            || Player->GetGameInstance()!=Instance.Get() || FObjectKey(Player.Get())!=PlayerKey || FObjectKey(Instance.Get())!=InstanceKey) return false;
        auto* UI=PC->GetGameUI();if (!UI || !FactoryFPVShippingStartup::Ready(UI,LastReason,bNewSplashObserved,bNewAnimationObserved,bNewStartupFinished,bNewFrontEndFinished)) return false;
        auto* Pawn=Cast<AFGCharacterPlayer>(PC->GetPawn());
        if (!Pawn || !Pawn->IsAliveAndWell() || !Grounded(Pawn) || !PC->PlayerCameraManager || !PC->GetHUD() || PC->NeedRespawn() || PC->IsRespawning() || PC->IsClientRespawnGracePeriodActive()
            || World->IsPaused() || PC->IsPaused() || PC->IsPauseMenuOpen() || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget()) { ReadySince=0;return false; }
        NewPC=PC;Hangar=Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();if (!Hangar.IsValid()) return false;
        if (ReadySince==0) ReadySince=Now;return Now-ReadySince>=.75;
    }
    bool VerifyOldResources()
    {
        for (const auto& Item:OldModActors) if (Item.IsValid() && !Item->IsActorBeingDestroyed() && Item->HasActorBegunPlay()) return false;
        for (const auto& Item:OldModSources) if (Item.IsValid() && Item->IsStreamingSourceEnabled()) return false;
        for (const auto& Item:OldModControls) if (Item.IsValid())
        { if (NewPC.IsValid() && NewPC->IsInputComponentInStack(Item.Get())) return false;if (OldPC.IsValid() && !OldPC->IsActorBeingDestroyed() && OldPC->IsInputComponentInStack(Item.Get())) return false; }
        if (FSlateApplication::IsInitialized()) for (const auto& Item:OldModWidgets)
            if (const auto Widget=Item.Pin();Widget && FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef()).IsValid()) return false;
        return true;
    }
    bool NewControlsClean()
    {
        if (!NewPC.IsValid() || !Hangar.IsValid() || Hangar->IsPanelOpen() || !NoModLeases(NewPC.Get()) || F6Bindings(NewPC.Get())!=1 || Hangar->GetSelectedRacingCourse()!=0
            || AFactoryFPVDrone::FindActive(NewWorld.Get()) || AFactoryFPVPioneerAnchor::FindPending(NewWorld.Get(),Cast<AFGCharacterPlayer>(NewPC->GetPawn())) || !NoModPresentation()) return false;
        return true;
    }
    void ReleaseScreenshot()
    {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot) { FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore; }
        bOwnScreenshotPending=false;
    }
    bool RequestCapture(FCapture& Capture,const TCHAR* State,double Now)
    {
        auto* V=Viewport.Get();if (FScreenshotRequest::IsScreenshotRequested() || !V || !V->Viewport || V->Viewport->GetSizeXY().X<320 || V->Viewport->GetSizeXY().Y<180)
        { Finish(false,TEXT("Actual viewport unavailable or an unrelated screenshot is pending."));return false; }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);const FString Dir=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));
        FString Path=FPaths::Combine(Dir,FString::Printf(TEXT("Travel-%s-%s.png"),State,*Mode));FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Path,Dir) || !FPaths::IsUnderDirectory(Dir,Saved) || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Dir,true))
        { Finish(false,TEXT("Capture path is not fresh and inside disposable Saved."));return false; }
        ReleaseScreenshot();Capture.Path=PendingScreenshot=Path;CaptureStarted=Now;bScreenshotProcessed=false;bOwnScreenshotPending=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]()
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshotPending) { Run->bScreenshotProcessed=true;Run->bOwnScreenshotPending=false; } });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);return true;
    }
    bool CaptureFinished(FCapture& Capture,double Now)
    {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Travel viewport screenshot exceeded five seconds: ")+Capture.Path);return false; }
        if (!bScreenshotProcessed) return false;ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) || Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180
            || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty()) { Finish(false,TEXT("Processed travel capture is not a sane decodable PNG."));return false; }
        double Sum=0,Squares=0;int32 Samples=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60))
        { const auto Color=Image.GetOnePixelLinear(X,Y);if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) { Finish(false,TEXT("Travel capture has nonfinite pixels."));return false; }const double L=.2126*Color.R+.7152*Color.G+.0722*Color.B;Sum+=L;Squares+=L*L;++Samples; }
        Capture.LumaVariance=Samples?FMath::Max(0.,Squares/Samples-FMath::Square(Sum/Samples)):0.;
        if (Capture.LumaVariance<1e-8) { Finish(false,TEXT("Actual travel capture is uniformly blank."));return false; }
        Capture.Width=Image.SizeX;Capture.Height=Image.SizeY;Capture.bDecoded=true;return true;
    }
    void Cleanup()
    {
        ReleaseScreenshot();UnbindWorldObservers();
        // Never repair/complete/cancel the native migration or reload another
        // save. Only owners introduced by this fixture may be retired here.
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed() && (Own->GetOwner()==OldPC.Get() || Own->GetOwner()==NewPC.Get())) Own->ExitDrone();
        if (bOwnedPanel && Hangar.IsValid() && Hangar->IsPanelOpen()) Hangar->ClosePanel();
    }
    void Finish(bool Success,const FString& Reason)
    {
        if (bDone) return;UpdateOperation();UpdateNativeText();
        const bool bLateFailure=Success && (bTravelFailed || bNullPostLoad || bOperationHasErrors);
        if (bLateFailure) Success=false;
        bDone=true;Cleanup();Marker(Mode,Success?TEXT("PASS"):TEXT("FAIL"),bLateFailure?TEXT("Native migration reported a failure during final verification."):Reason,Evidence());FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    void Complete()
    {
        bNoModLeases=NoModLeases(NewPC.Get());bCapturesVerified=!bCapture || (BeforeCapture.bDecoded && ReloadedCapture.bDecoded && FreshReturnCapture.bDecoded);
        const bool Pass=bTravelRequested && bOldTeardown && bOldCleanup && bNewPostLoad && bNewReady && bOldResourcesRetired && bNewControlsClean && bHangarRebound && bFreshSessionPassed && bNoModLeases && bCapturesVerified;
        Finish(Pass,Pass?TEXT("One native local clone reload earned old-world teardown/cleanup, new-world readiness, retired old owners and rebound F6/fresh disarmed return; no save or migration bypass."):
            TEXT("Native travel returned but one or more recorded teardown/new-owner workflow checks failed."));
    }
    bool Tick()
    {
        if (bDone) return false;const double Now=FPlatformTime::Seconds();
        if (!Guard()) { Finish(false,TEXT("Travel QA isolation paths/opt-in changed."));return false; }
        if (Now>Deadline) { Finish(false,TEXT("90-second travel QA deadline: ")+LastReason);return false; }
        if (Phase==EPhase::Waiting) { if (InitialReady()) Prepare(Now);return !bDone; }
        if (!Instance.IsValid() || !Player.IsValid() || FObjectKey(Instance.Get())!=InstanceKey || FObjectKey(Player.Get())!=PlayerKey)
        { Finish(false,TEXT("Expected GameInstance/LocalPlayer changed; no migration bypass attempted."));return false; }
        if (bTravelRequested)
        {
            UpdateOperation();
            if (bTravelFailed || bNullPostLoad || bOperationHasErrors) { Finish(false,TEXT("Native migration reported a failure; see operation/errors/native UI evidence."));return false; }
        }
        if (Phase>=EPhase::ReloadedSettle && (!NewWorld.IsValid() || NewWorld->bIsTearingDown || CurrentWorld()!=NewWorld.Get()
            || !NewPC.IsValid() || NewPC->IsActorBeingDestroyed() || NewWorld->GetFirstPlayerController()!=NewPC.Get()
            || NewPC->GetLocalPlayer()!=Player.Get() || !Hangar.IsValid()))
        { Finish(false,TEXT("Reloaded native world/controller owner changed during verification."));return false; }
        if (Phase==EPhase::BeforeSettle)
        {
            if (!Settled(Now)) return true;Snapshots.Add(TEXT("before"),Snapshot(OldWorld.Get(),OldPC.Get()));
            if (bCapture) { if (RequestCapture(BeforeCapture,TEXT("Before"),Now)) SetPhase(EPhase::CaptureBefore,Now); }else RequestTravel(Now);
        }
        else if (Phase==EPhase::CaptureBefore) { if (CaptureFinished(BeforeCapture,Now)) RequestTravel(Now); }
        else if (Phase==EPhase::Migrating)
        {
            if (!bOldCleanup || !bOldTeardown || !NewWorldReady(Now)) return true;
            bOldResourcesRetired=VerifyOldResources();bNewControlsClean=NewControlsClean();
            if (!bOldResourcesRetired || !bNewControlsClean) { LastReason=TEXT("Waiting for natural new-world subsystem ticks to remove old owners and rebind exactly one F6 control.");return true; }
            bNewReady=true;Snapshots.Add(TEXT("reloaded"),Snapshot(NewWorld.Get(),NewPC.Get()));SetPhase(EPhase::ReloadedSettle,Now);
        }
        else if (Phase==EPhase::ReloadedSettle && Settled(Now))
        {
            if (bCapture) { if (RequestCapture(ReloadedCapture,TEXT("Reloaded"),Now)) SetPhase(EPhase::CaptureReloaded,Now); }
            else OpenReboundHangar(Now);
        }
        else if (Phase==EPhase::CaptureReloaded) { if (CaptureFinished(ReloadedCapture,Now)) OpenReboundHangar(Now); }
        else if (Phase==EPhase::HangarOpened && Settled(Now))
        {
            if (!Hangar->IsPanelOpen() || !InvokeF6(NewPC.Get()) || Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual rebound F6 did not close the reloaded hangar."));return false; }
            bOwnedPanel=false;SetPhase(EPhase::HangarClosed,Now);
        }
        else if (Phase==EPhase::HangarClosed && Settled(Now))
        {
            if (!NewControlsClean()) { Finish(false,TEXT("Reloaded F6 close did not balance current input/UI owners."));return false; }
            bHangarRebound=true;FString Error;Drone=AFactoryFPVDrone::Launch(NewPC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Fresh ordinary post-load disarmed launch failed: ")+Error);return false; }SetPhase(EPhase::FreshFlight,Now);
        }
        else if (Phase==EPhase::FreshFlight)
        {
            if (!Drone.IsValid() || Drone->IsFlightArmed() || NewPC->GetViewTarget()!=Drone.Get()) { Finish(false,TEXT("Fresh post-load disarmed session lost its ordinary owner/view."));return false; }
            if (Settled(Now)) { Drone->ExitDrone();SetPhase(EPhase::FreshReturn,Now); }
        }
        else if (Phase==EPhase::FreshReturn)
        {
            auto* Pawn=NewPC.IsValid()?Cast<AFGCharacterPlayer>(NewPC->GetPawn()):nullptr;
            if (!Settled(Now) || !Pawn || !Pawn->IsAliveAndWell() || !Grounded(Pawn) || !NewControlsClean()) return true;
            bFreshSessionPassed=true;Snapshots.Add(TEXT("freshReturn"),Snapshot(NewWorld.Get(),NewPC.Get()));
            if (bCapture) { if (RequestCapture(FreshReturnCapture,TEXT("FreshReturn"),Now)) SetPhase(EPhase::CaptureFreshReturn,Now); }else Complete();
        }
        else if (Phase==EPhase::CaptureFreshReturn && CaptureFinished(FreshReturnCapture,Now)) Complete();
        return !bDone;
    }
    void OpenReboundHangar(double Now)
    {
        if (!InvokeF6(NewPC.Get())) { Finish(false,TEXT("New local player has no unique actual F6 callback."));return; }
        bOwnedPanel=Hangar->IsPanelOpen();if (!bOwnedPanel) { Finish(false,TEXT("Actual rebound F6 did not open the new-world hangar."));return; }SetPhase(EPhase::HangarOpened,Now);
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingTravelQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingTravelQAInternal;
    FString Mode;FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQATravel="),Mode);Mode.ToLowerInline();
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (Mode!=TEXT("baseline") && Mode!=TEXT("flight") && Mode!=TEXT("panel")))
    {
        Marker(Mode.IsEmpty()?TEXT("invalid"):Mode,TEXT("FAIL"),TEXT("Explicit automation, disposable standalone clone and -FactoryFPVQATravel=baseline|flight|panel required."));
        Output.Log(TEXT("Travel QA guard/mode refused."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Travel QA already active."));return; }
    auto Run=MakeShared<FRun>();Run->OldWorld=World;Run->Mode=Mode;Run->Deadline=FPlatformTime::Seconds()+90.;Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Active=Run;
    Marker(Mode,TEXT("PENDING"),TEXT("Waiting for exact local clone/native startup before one supported native load migration; no save, online setting change or forced completion."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}),.1f);
}
