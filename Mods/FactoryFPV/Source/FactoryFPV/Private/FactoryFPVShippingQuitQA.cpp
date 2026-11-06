#include "FactoryFPVShippingQuitQA.h"
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
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Engine/Console.h"
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
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace FactoryFPVShippingQuitQAInternal
{
const TCHAR* Bool(bool Value) { return Value?TEXT("true"):TEXT("false"); }
FString Escape(FString Text)
{
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
bool Disposable(FString Path)
{
    if (Path.IsEmpty()) return false;
    Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);
    return FPaths::CollapseRelativeDirectories(Path) && Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard()
{
    return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) &&
        Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath());
}
void Marker(const FString& Mode,const TCHAR* Result,const FString& Reason,const FString& Extra=FString())
{
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"quit-%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"one normal non-forced native RequestExit while baseline/disarmed flight/hangar remains active; observed Quit EndPlay and post-world cleanup; savedSettings=false is fixture-only, native orderly exit may flush disposable config; excludes physical quit controls, armed flight and gameplay return\"%s}"),*Escape(Mode),Result,*Escape(Reason),*Extra);
    if (GLog) { GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);GLog->Flush(); }
}
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double LumaVariance=0;
    FString Json() const { return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Width,Height,Bool(bDecoded),LumaVariance); }
};
struct FRun;
TSharedPtr<FRun> Active;

struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,Settling,Capturing,AwaitingShutdown };
    TWeakObjectPtr<UWorld> World;
    FObjectKey WorldKey;
    // Retain diagnostic memory through native destruction, not play lifetime.
    // Shutdown/EndPlay still run normally; references are released after the
    // post-cleanup readback, before native UObject shutdown/garbage collection.
    TStrongObjectPtr<AFGPlayerController> PC;
    TStrongObjectPtr<AFGCharacterPlayer> Pioneer;
    TStrongObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TStrongObjectPtr<UFactoryFPVShippingQuitObserver> Observer;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TSharedPtr<SWindow> Window;
    TArray<TWeakObjectPtr<AActor>> ModActors;
    TArray<TWeakObjectPtr<UInputComponent>> ModControls;
    TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> ModSources;
    TArray<TWeakPtr<SWidget>> ModWidgets;
    TMap<FString,FString> Snapshots;
    FDelegateHandle TearDownHandle,CleanupHandle,PostCleanupHandle,EnginePreExitHandle,ScreenshotHandle;
    FString Mode,LastReason=TEXT("Waiting for the actual clone and native startup lifecycle."),PendingScreenshot;
    EPhase Phase=EPhase::Waiting;
    FCapture BeforeCapture;
    double Deadline=0,PhaseStarted=0,CaptureStarted=0;
    uint64 PhaseFrame=0;
    bool bCapture=false,bDone=false,bOwnScreenshot=false,bScreenshotProcessed=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bSplashFinished=false,bFrontEndFinished=false;
    bool bBeforeQuitActive=false,bQuitRequested=false,bQuitBoundaryActive=false,bTeardown=false,bCleanup=false,bPostCleanup=false;
    bool bPioneerQuit=false,bDroneQuit=false,bControllerQuit=false,bQuitEndPlay=false;
    bool bActorsRetired=false,bSourcesRetired=false,bControlsDetached=false,bWidgetsRemoved=false,bNoLeases=false,bPostControllerInspectable=false;
    bool bResourcesCaptured=false;
    int32 PioneerEndPlayReason=INDEX_NONE,DroneEndPlayReason=INDEX_NONE,ControllerEndPlayReason=INDEX_NONE;

    FString Snapshot(UWorld* Current) const
    {
        auto* Controller=PC.Get();const bool Inspectable=Controller && !Controller->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed);
        const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller);APawn* Pawn=Inspectable?Controller->GetPawn():nullptr;
        auto* Camera=Inspectable?Controller->PlayerCameraManager.Get():nullptr;auto* Hud=Inspectable?Cast<AFGHUD>(Controller->GetHUD()):nullptr;
        return FString::Printf(TEXT("{\"worldTearingDown\":%s,\"worldBegunPlay\":%s,\"controllerInspectable\":%s,\"controllerDestroyed\":%s,\"pioneerBegunPlay\":%s,\"pioneerAlive\":%s,\"pawn\":\"%s\",\"view\":\"%s\",\"moveIgnored\":%s,\"lookIgnored\":%s,\"fov\":%.6f,\"lockedFov\":%.6f,\"partialHud\":%s,\"panelOpen\":%s,\"droneDisarmed\":%s,\"hooksReady\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d}"),
            Bool(Current && Current->bIsTearingDown),Bool(Current && Current->HasBegunPlay()),Bool(Inspectable),Bool(Inspectable && Controller->IsActorBeingDestroyed()),
            Bool(IsValid(Pioneer.Get()) && Pioneer->HasActorBegunPlay()),Bool(IsValid(Pioneer.Get()) && Pioneer->IsAliveAndWell()),*Escape(GetPathNameSafe(Pawn)),*Escape(GetPathNameSafe(Inspectable?Controller->GetViewTarget():nullptr)),
            Bool(Inspectable && Controller->IsMoveInputIgnored()),Bool(Inspectable && Controller->IsLookInputIgnored()),Camera && FMath::IsFinite(Camera->GetFOVAngle())?Camera->GetFOVAngle():-1.f,
            Camera && FMath::IsFinite(Camera->GetLockedFOV())?Camera->GetLockedFOV():-1.f,Bool(Hud && Hud->GetPartialPumpiMode()),Bool(Hangar.Get() && Hangar->IsPanelOpen()),
            Bool(Drone.IsValid() && !Drone->IsFlightArmed()),Bool(Lease.bNativeHooksReady),Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions);
    }
    FString Evidence() const
    {
        FString Json=FString::Printf(TEXT(",\"mode\":\"%s\",\"variant\":\"%s\",\"beforeQuitActive\":%s,\"quitRequested\":%s,\"quitBoundaryActive\":%s,\"nativeWorldTeardownObserved\":%s,\"nativeQuitEndPlayObserved\":%s,\"nativeWorldCleanupObserved\":%s,\"nativePostWorldCleanupObserved\":%s,\"modActorsRetired\":%s,\"modSourcesRetired\":%s,\"modControlsDetached\":%s,\"modWidgetsRemoved\":%s,\"noModLeases\":%s,\"capturesVerified\":%s,\"captureEnabled\":%s,\"postControllerInspectable\":%s,\"pioneerEndPlayReason\":%d,\"droneEndPlayReason\":%d,\"controllerEndPlayReason\":%d,\"programmaticQuit\":true,\"forcedExit\":false,\"manualSessionCleanup\":false,\"injectedFlightControls\":false,\"savedWorld\":false,\"savedSettings\":false,\"fixtureSavedSettings\":false,\"settingsMutationByFixture\":false,\"nativeEngineMayFlushDisposableSettings\":true,\"snapshots\":{"),
            *Escape(Mode),*Escape(Mode),Bool(bBeforeQuitActive),Bool(bQuitRequested),Bool(bQuitBoundaryActive),Bool(bTeardown),Bool(bQuitEndPlay),Bool(bCleanup),Bool(bPostCleanup),
            Bool(bActorsRetired),Bool(bSourcesRetired),Bool(bControlsDetached),Bool(bWidgetsRemoved),Bool(bNoLeases),Bool(!bCapture || BeforeCapture.bDecoded),Bool(bCapture),Bool(bPostControllerInspectable),
            PioneerEndPlayReason,DroneEndPlayReason,ControllerEndPlayReason);
        bool First=true;
        for (const TCHAR* Name:{TEXT("before"),TEXT("quitBoundary"),TEXT("worldCleanup"),TEXT("postWorldCleanup")}) if (const FString* Value=Snapshots.Find(Name)) {
            if (!First) Json+=TEXT(",");First=false;Json+=FString::Printf(TEXT("\"%s\":%s"),Name,**Value);
        }
        Json+=FString::Printf(TEXT("},\"resourcesCaptured\":%s,\"trackedModActors\":%d,\"trackedModSources\":%d,\"trackedModControls\":%d,\"trackedModWidgets\":%d,\"captures\":["),
            Bool(bResourcesCaptured),ModActors.Num(),ModSources.Num(),ModControls.Num(),ModWidgets.Num());
        if (bCapture) Json+=BeforeCapture.Json();return Json+TEXT("]");
    }
    bool FindModWidgets(TArray<TWeakPtr<SWidget>>& Found) const
    {
        if (!Window) return false;TArray<TSharedRef<SWidget>> Pending;Pending.Add(Window.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768) {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());
            const FName Type=Widget->GetType();
            if (Type==FName(TEXT("SFactoryFPVOSD")) || Type==FName(TEXT("SFactoryFPVPanel")) || Type==FName(TEXT("SFactoryFPVRacePanel"))) Found.Add(Widget);
            if (auto* Children=Widget->GetChildren()) for (int32 Index=0;Index<Children->Num();++Index) Pending.Add(Children->GetChildAt(Index));
        }
        return Pending.IsEmpty();
    }
    bool NoLeases() const
    {
        const auto Lease=FactoryFPVInputLease::GetDiagnostics(PC.Get());return Lease.ActiveLeases==0 && Lease.OwnedMoveContributions==0 && Lease.OwnedLookContributions==0 && Lease.ResetDepth==0;
    }
    bool InvokeF6()
    {
        FInputActionUnifiedDelegate Callback;int32 Count=0;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It))
            for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Hangar.Get())) { Callback=Binding.KeyDelegate;++Count; }
        if (Count!=1) { LastReason=TEXT("Waiting for one actual bound hangar F6 callback.");return false; }Callback.Execute(EKeys::F6);return true;
    }
    bool CheckVariant() const
    {
        auto* Current=World.Get();auto* Controller=PC.Get();auto* Character=Pioneer.Get();auto* UI=Controller?Controller->GetGameUI():nullptr;
        if (!Current || !IsValid(Controller) || !IsValid(Character) || !Character->IsAliveAndWell() || Controller->GetPawn()!=Character || !Hangar.Get() ||
            Controller->IsPaused() || Controller->IsPauseMenuOpen() || !UI || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget()) return false;
        const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller);TArray<TWeakPtr<SWidget>> Widgets;
        if (!FindModWidgets(Widgets)) return false;
        if (Mode==TEXT("baseline")) return !Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(Current) && NoLeases() && Widgets.IsEmpty();
        if (!Lease.bNativeHooksReady || Lease.ResetDepth || Lease.ActiveLeases!=1 || Lease.OwnedMoveContributions!=1 || Lease.OwnedLookContributions!=1 ||
            !Controller->IsMoveInputIgnored() || !Controller->IsLookInputIgnored() || Widgets.Num()!=1) return false;
        if (Mode==TEXT("panel")) return Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(Current) && Controller->GetViewTarget()==Character;
        return Drone.IsValid() && !Drone->IsActorBeingDestroyed() && !Drone->IsFlightArmed() && Controller->GetViewTarget()==Drone.Get() && !Hangar->IsPanelOpen();
    }
    bool InitialReady()
    {
        auto* Current=World.Get();auto* Game=Current?Current->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=Game?Game->GetSaveSession():nullptr;
        auto* Controller=Current?Cast<AFGPlayerController>(Current->GetFirstPlayerController()):nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() || !Game || Game->IsMainMenuGameMode() ||
            !Save || Save->HasSaveGameOperationPending() || !Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer()) return false;
        const auto Load=Game->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        if ((Load.LoadName!=TEXT("FPV-QA-Clone") && Header.SaveName!=TEXT("FPV-QA-Clone")) || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty()) return false;
        auto* UI=Controller->GetGameUI();if (!UI || !FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bSplashAnimationObserved,bSplashFinished,bFrontEndFinished)) return false;
        auto* Character=Cast<AFGCharacterPlayer>(Controller->GetPawn());auto* Viewport=Current->GetGameViewport();auto* Console=Viewport?Viewport->ViewportConsole.Get():nullptr;
        if (!Character || !Character->IsAliveAndWell() || !Character->HasAuthority() || !Controller->PlayerCameraManager || !Controller->GetHUD() ||
            Controller->NeedRespawn() || Controller->IsRespawning() || Controller->IsClientRespawnGracePeriodActive() || Current->IsPaused() || Controller->IsPaused() ||
            Controller->IsPauseMenuOpen() || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget() || (Console && Console->ConsoleState!=NAME_None) || !Viewport || !Viewport->GetWindow()) return false;
        PC.Reset(Controller);Pioneer.Reset(Character);Hangar.Reset(Controller->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>());Window=Viewport->GetWindow();
        return Hangar.Get()!=nullptr;
    }
    bool Prepare(double Now)
    {
        auto* Current=World.Get();if (!NoLeases() || Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(Current) || AFactoryFPVPioneerAnchor::FindPending(Current,Pioneer.Get()) ||
            PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()) { Fail(TEXT("Quit QA requires no pre-existing FPV/input owners."));return false; }
        TArray<TWeakPtr<SWidget>> Existing;if (!FindModWidgets(Existing) || !Existing.IsEmpty()) { Fail(TEXT("Pre-existing FPV presentation prevents ownership-safe quit QA."));return false; }
        auto* Move=Pioneer->GetCharacterMovement();FFindFloorResult Floor;if (Move) Move->FindFloor(Pioneer->GetActorLocation(),Floor,false);
        if (!Move || !Move->IsMovingOnGround() || !Floor.IsWalkableFloor()) { LastReason=TEXT("Waiting for a real walkable native pioneer floor; no placement/movement fallback.");return false; }
        if (Mode==TEXT("flight")) {
            FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Fail(TEXT("Ordinary disarmed pre-quit launch failed: ")+Error);return false; }
        } else if (Mode==TEXT("panel")) {
            if (!InvokeF6()) return false;if (!Hangar->IsPanelOpen()) { Fail(TEXT("Actual bound F6 failed to open the pre-quit hangar."));return false; }
        }
        WorldKey=FObjectKey(Current);Observer.Reset(NewObject<UFactoryFPVShippingQuitObserver>(GetTransientPackage()));
        Pioneer->OnEndPlay.AddDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
        PC->OnEndPlay.AddDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
        if (Drone.IsValid()) Drone->OnEndPlay.AddDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
        if (!CollectResources()) { Fail(TEXT("The actual variant did not expose its expected native mod controls/widgets/actors/sources."));return false; }
        Phase=EPhase::Settling;PhaseStarted=Now;PhaseFrame=GFrameCounter;
        LastReason=TEXT("Native variant is active; waiting for its actual view/panel to settle before ordinary quit.");return true;
    }
    bool CollectResources()
    {
        ModActors.Reset();ModControls.Reset();ModSources.Reset();ModWidgets.Reset();
        for (TActorIterator<AActor> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed() &&
            (It->IsA<AFactoryFPVDrone>() || It->IsA<AFactoryFPVPioneerAnchor>() || It->IsA<AFactoryFPVPilotVisual>())) ModActors.AddUnique(*It);
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() &&
            (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner()))) ModSources.AddUnique(*It);
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It) &&
            ((Drone.IsValid() && It->GetOwner()==Drone.Get()) || (It->GetOwner()==PC.Get() && It->GetName().StartsWith(TEXT("FactoryFPVHangarControls"))))) ModControls.AddUnique(*It);
        const bool FoundWidgets=FindModWidgets(ModWidgets);
        bResourcesCaptured=FoundWidgets && ModControls.Num()>=1 &&
            (Mode==TEXT("flight") ? ModActors.Num()>=2 && ModSources.Num()>=2 && ModControls.Num()>=2 && ModWidgets.Num()==1 :
                ModActors.IsEmpty() && ModSources.IsEmpty() && ModWidgets.Num()==(Mode==TEXT("panel")?1:0));
        if (Mode==TEXT("flight")) for (const auto& Source:ModSources) if (!Source.IsValid() || !Source->IsStreamingSourceEnabled()) bResourcesCaptured=false;
        return bResourcesCaptured;
    }
    void ActorEnded(AActor* Actor,EEndPlayReason::Type Reason)
    {
        if (bDone) return;
        if (Actor==Pioneer.Get()) { PioneerEndPlayReason=static_cast<int32>(Reason);bPioneerQuit=bQuitRequested && Reason==EEndPlayReason::Quit; }
        if (Actor==PC.Get()) { ControllerEndPlayReason=static_cast<int32>(Reason);bControllerQuit=bQuitRequested && Reason==EEndPlayReason::Quit; }
        if (Actor==Drone.Get()) { DroneEndPlayReason=static_cast<int32>(Reason);bDroneQuit=bQuitRequested && Reason==EEndPlayReason::Quit; }
        bQuitEndPlay=bPioneerQuit && (Mode!=TEXT("flight") || bDroneQuit);
    }
    void BindShutdownObservers()
    {
        const TWeakPtr<FRun> Weak=AsShared();
        TearDownHandle=FWorldDelegates::OnWorldBeginTearDown.AddLambda([Weak](UWorld* Current) {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bQuitRequested && Current && FObjectKey(Current)==Run->WorldKey) {
                Run->bTeardown=IsEngineExitRequested();Run->bQuitBoundaryActive=Run->bTeardown && Run->CheckVariant();Run->Snapshots.Add(TEXT("quitBoundary"),Run->Snapshot(Current));
            }
        });
        CleanupHandle=FWorldDelegates::OnWorldCleanup.AddLambda([Weak](UWorld* Current,bool,bool) {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bQuitRequested && Current && FObjectKey(Current)==Run->WorldKey) {
                Run->bCleanup=Run->bTeardown && !Current->HasBegunPlay();Run->Snapshots.Add(TEXT("worldCleanup"),Run->Snapshot(Current));
            }
        });
        PostCleanupHandle=FWorldDelegates::OnPostWorldCleanup.AddLambda([Weak](UWorld* Current,bool,bool) {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bQuitRequested && Current && FObjectKey(Current)==Run->WorldKey) Run->AfterWorldCleanup(Current);
        });
        EnginePreExitHandle=FCoreDelegates::OnEnginePreExit.AddLambda([Weak]() {
            if (auto Run=Weak.Pin();Run && !Run->bDone) Run->Fail(TEXT("Native engine PreExit completed without the required exact-world post-cleanup proof."));
        });
    }
    void AfterWorldCleanup(UWorld* Current)
    {
        bPostCleanup=true;bActorsRetired=true;bSourcesRetired=true;bControlsDetached=true;bWidgetsRemoved=true;
        auto* Controller=PC.Get();bPostControllerInspectable=Controller && !Controller->HasAnyFlags(RF_BeginDestroyed|RF_FinishDestroyed);
        for (const auto& Actor:ModActors) if (Actor.IsValid() && Actor->HasActorBegunPlay()) bActorsRetired=false;
        for (const auto& Source:ModSources) if (Source.IsValid() && Source->IsStreamingSourceEnabled()) bSourcesRetired=false;
        for (const auto& Input:ModControls) if (Input.IsValid() && (Input->IsRegistered() || !bPostControllerInspectable || Controller->IsInputComponentInStack(Input.Get()))) bControlsDetached=false;
        if (!FSlateApplication::IsInitialized()) bWidgetsRemoved=false;
        else for (const auto& Item:ModWidgets) if (auto Widget=Item.Pin();Widget && FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef()).IsValid()) bWidgetsRemoved=false;
        TArray<TWeakPtr<SWidget>> Remaining;if (!FindModWidgets(Remaining) || !Remaining.IsEmpty() || (Hangar.Get() && Hangar->IsPanelOpen())) bWidgetsRemoved=false;
        // The retained identity queries the still-running lease service, not a
        // null weak PC after native RemoveLocalPlayer. No module Shutdown yet.
        bNoLeases=bPostControllerInspectable && NoLeases();Snapshots.Add(TEXT("postWorldCleanup"),Snapshot(Current));
        const bool Success=Guard() && bResourcesCaptured && bBeforeQuitActive && bQuitRequested && bQuitBoundaryActive && bTeardown && bQuitEndPlay && bCleanup && bPostCleanup &&
            bActorsRetired && bSourcesRetired && bControlsDetached && bWidgetsRemoved && bNoLeases && (!bCapture || BeforeCapture.bDecoded);
        Complete(Success,Success?TEXT("Normal native quit retained the requested active variant at teardown, emitted actual Quit EndPlay and retired owned resources before post-world cleanup. Host must independently require normal exit zero."):
            TEXT("Native quit reached post-world cleanup but required active-boundary/EndPlay/resource evidence failed; no manual cleanup or forced completion was used."));
    }
    void ReleaseScreenshot()
    {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshot && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot) FScreenshotRequest::Reset();
        bOwnScreenshot=false;
    }
    void UnbindObservers()
    {
        if (TearDownHandle.IsValid()) { FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);TearDownHandle.Reset(); }
        if (CleanupHandle.IsValid()) { FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);CleanupHandle.Reset(); }
        if (PostCleanupHandle.IsValid()) { FWorldDelegates::OnPostWorldCleanup.Remove(PostCleanupHandle);PostCleanupHandle.Reset(); }
        if (EnginePreExitHandle.IsValid()) { FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);EnginePreExitHandle.Reset(); }
        if (Observer.Get()) {
            if (Pioneer.Get()) Pioneer->OnEndPlay.RemoveDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
            if (PC.Get()) PC->OnEndPlay.RemoveDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
            if (Drone.IsValid()) Drone->OnEndPlay.RemoveDynamic(Observer.Get(),&UFactoryFPVShippingQuitObserver::ActorEnded);
        }
    }
    void Complete(bool Success,const FString& Reason)
    {
        if (bDone) return;bDone=true;ReleaseScreenshot();
        Marker(Mode,Success?TEXT("PASS"):TEXT("FAIL"),Reason,Evidence());UnbindObservers();
        Observer.Reset();Hangar.Reset();Pioneer.Reset();PC.Reset();Window.Reset();World.Reset();Active.Reset();
        // Success is already in the ordinary native Exit path: do not request
        // another exit or call any gameplay/session cleanup. Failure only sets
        // a nonzero orderly status; the host also rejects absent/FAIL markers.
        if (!Success) FPlatformMisc::RequestExitWithStatus(false,1);
    }
    void Fail(const FString& Reason) { Complete(false,Reason); }
    bool RequestCapture(double Now)
    {
        auto* Viewport=World.IsValid()?World->GetGameViewport():nullptr;
        if (!Viewport || !Viewport->Viewport || FScreenshotRequest::IsScreenshotRequested()) { Fail(TEXT("Quit capture requires a real viewport and no pre-existing screenshot request."));return false; }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);const FString Dir=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));
        FString Path=FPaths::Combine(Dir,FString::Printf(TEXT("Quit-Before-%s.png"),*Mode));FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Dir,Saved) || !FPaths::IsUnderDirectory(Path,Dir) ||
            IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Dir,true)) { Fail(TEXT("Quit capture must be fresh and within disposable Saved."));return false; }
        BeforeCapture.Path=PendingScreenshot=Path;CaptureStarted=Now;bOwnScreenshot=true;bScreenshotProcessed=false;
        const TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]() {
            if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshot) { Run->bScreenshotProcessed=true;Run->bOwnScreenshot=false; }
        });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);Phase=EPhase::Capturing;return true;
    }
    bool CaptureFinished(double Now)
    {
        if (Now-CaptureStarted>5.) { Fail(TEXT("Pre-quit viewport screenshot exceeded five seconds."));return false; }
        if (!bScreenshotProcessed) return false;ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*BeforeCapture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) ||
            Image.SizeX<320 || Image.SizeY<180 || Image.SizeX>8192 || Image.SizeY>8192 || Image.NumSlices!=1 || Image.RawData.IsEmpty()) { Fail(TEXT("Actual pre-quit viewport PNG did not decode to sane dimensions."));return false; }
        double Sum=0,SumSq=0;int32 Count=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/72)) {
            const auto Pixel=Image.GetOnePixelLinear(X,Y);const double Luma=.2126*Pixel.R+.7152*Pixel.G+.0722*Pixel.B;
            if (!FMath::IsFinite(Luma)) { Fail(TEXT("Pre-quit viewport contains non-finite pixels."));return false; }Sum+=Luma;SumSq+=Luma*Luma;++Count;
        }
        BeforeCapture.LumaVariance=Count?FMath::Max(0.,SumSq/Count-FMath::Square(Sum/Count)):0.;
        BeforeCapture.bDecoded=BeforeCapture.LumaVariance>.000001;BeforeCapture.Width=Image.SizeX;BeforeCapture.Height=Image.SizeY;
        if (!BeforeCapture.bDecoded) { Fail(TEXT("Pre-quit viewport is uniform; refusing unavailable presentation evidence."));return false; }return true;
    }
    void RequestQuit()
    {
        bBeforeQuitActive=CheckVariant();Snapshots.Add(TEXT("before"),Snapshot(World.Get()));
        if (!bBeforeQuitActive) { Fail(TEXT("Required baseline/disarmed flight/hangar was not still active immediately before ordinary quit."));return; }
        if (!CollectResources()) { Fail(TEXT("Actual resource snapshot changed before orderly quit; refusing vacuous shutdown proof."));return; }
        BindShutdownObservers();bQuitRequested=true;Phase=EPhase::AwaitingShutdown;
        Marker(Mode,TEXT("PENDING"),TEXT("Requesting ordinary native quit with the current variant intact; no ExitDrone, panel close or owner cleanup has run."),Evidence());
        FPlatformMisc::RequestExit(false,TEXT("FactoryFPVShippingQuitQA"));
    }
    bool Tick()
    {
        if (bDone || Phase==EPhase::AwaitingShutdown) return false;
        const double Now=FPlatformTime::Seconds();
        if (!Guard() || !World.IsValid()) { Fail(TEXT("Quit QA isolation/world guard failed."));return false; }
        if (Now>Deadline) { Fail(TEXT("30-second quit preparation deadline: ")+LastReason);return false; }
        if (Phase==EPhase::Waiting) { if (InitialReady()) Prepare(Now);return !bDone; }
        if (!CheckVariant()) { Fail(TEXT("Native pioneer/view/menu/input ownership changed before requested quit."));return false; }
        if (Phase==EPhase::Settling && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
            if (bCapture) RequestCapture(Now);else RequestQuit();
        } else if (Phase==EPhase::Capturing && CaptureFinished(Now)) RequestQuit();
        return !bDone && Phase!=EPhase::AwaitingShutdown;
    }
};
}

void UFactoryFPVShippingQuitObserver::ActorEnded(AActor* Actor,EEndPlayReason::Type Reason)
{
    if (auto Run=FactoryFPVShippingQuitQAInternal::Active) Run->ActorEnded(Actor,Reason);
}

void FFactoryFPVShippingQuitQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingQuitQAInternal;
    FString Mode;FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQAQuit="),Mode);Mode.ToLowerInline();
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (Mode!=TEXT("baseline") && Mode!=TEXT("flight") && Mode!=TEXT("panel"))) {
        Marker(Mode,TEXT("FAIL"),TEXT("Quit QA requires explicit automation, both disposable paths, a standalone world and baseline|flight|panel mode."));
        Output.Log(TEXT("Quit QA guard rejected."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Active && !Active->bDone) { Output.Log(TEXT("Quit QA is already running."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Mode=Mode;Run->Deadline=FPlatformTime::Seconds()+30.;
    Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Active=Run;
    Marker(Mode,TEXT("PENDING"),TEXT("Guarded native orderly-quit qualification queued; no world/settings save or production cleanup will be invoked by the fixture."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.1f);
}
