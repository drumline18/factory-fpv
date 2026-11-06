#include "FactoryFPVShippingPossessionQA.h"
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
#include "Components/SkeletalMeshComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
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
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace FactoryFPVShippingPossessionQAInternal
{
FString Escape(FString Text)
{
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
const TCHAR* Bool(bool Value) { return Value?TEXT("true"):TEXT("false"); }
FString ClassPath(const UObject* Object) { return Object?Object->GetClass()->GetPathName():FString(); }
FString Number(double Value) { return FMath::IsFinite(Value)?FString::Printf(TEXT("%.9f"),Value):TEXT("null"); }
bool Disposable(FString Path)
{
    if (Path.IsEmpty()) return false;Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);
    return FPaths::CollapseRelativeDirectories(Path) && Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard() { return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) && Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath()); }
void Marker(const FString& Mode,const TCHAR* Result,const FString& Reason,const FString& Extra=FString())
{
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"possession-%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"public native UnPossess/Possess of the same living pioneer on one disposable clone, natural mod cleanup and rebound disarmed workflow; excludes vehicles, physical input and forced possession completion\"%s}"),*Escape(Mode),Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
FString MeshJson(USkeletalMeshComponent* Mesh)
{
    return FString::Printf(TEXT("{\"present\":%s,\"hiddenInGame\":%s,\"visible\":%s,\"ownerHidden\":%s}"),Bool(IsValid(Mesh)),Bool(Mesh && Mesh->bHiddenInGame),Bool(Mesh && Mesh->IsVisible()),Bool(Mesh && Mesh->GetOwner() && Mesh->GetOwner()->IsHidden()));
}
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double LumaVariance=0;
    FString Json() const { return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Width,Height,Bool(bDecoded),LumaVariance); }
};
struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,BeforeSettle,CaptureBefore,Unpossessed,CaptureUnpossessed,Repossessing,RestoredSettle,CaptureRestored,HangarOpened,HangarClosed,FreshFlight,FreshReturn };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<UGameInstance> Instance;
    TWeakObjectPtr<ULocalPlayer> Player;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<AFGPlayerController> PC;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TArray<TWeakObjectPtr<AActor>> OldModActors;
    TArray<TWeakObjectPtr<UInputComponent>> OldModControls;
    TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> OldModSources;
    TArray<TWeakPtr<SWidget>> OldModWidgets;
    FObjectKey WorldKey,InstanceKey,PlayerKey,PioneerKey;
    FVector OriginalLocation=FVector::ZeroVector;
    FString Mode,LastReason=TEXT("Waiting for exact clone, native startup and living grounded pioneer."),PendingScreenshot;
    TMap<FString,FString> Snapshots;
    FCapture BeforeCapture,UnpossessedCapture,RestoredCapture;
    FDelegateHandle ScreenshotHandle;
    EPhase Phase=EPhase::Waiting;
    double Deadline=0,PhaseStarted=0,ReadySince=0,CaptureStarted=0,MaximumPioneerDisplacementCm=0;
    uint64 PhaseFrame=0,LastUnpossessedFrame=0;
    int32 UnpossessedSamples=0;
    bool bPrepared=false,bDone=false,bOwnedPanel=false,bCapture=false,bOwnScreenshotPending=false,bScreenshotProcessed=false,bScreenMessagesBefore=false;
    bool bSplashObserved=false,bAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bUnpossessRequested=false,bNativeUnpossessed=false,bPioneerStayedAlive=true,bRepossessRequested=false,bNativeRepossessed=false,bRestoredReady=false;
    bool bModSessionCleaned=false,bModWidgetsRemoved=false,bModControlsDetached=false,bModSourcesRetired=false,bUnpossessedNoLeases=false;
    bool bHangarRebound=false,bFreshSessionPassed=false,bNoModLeases=false,bCapturesVerified=false;

    void SetPhase(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    bool Settled(double Now,double Seconds=.5) const { return Now-PhaseStarted>=Seconds && GFrameCounter>=PhaseFrame+3; }
    TSharedPtr<SWindow> Window() const { return Viewport.IsValid()?Viewport->GetWindow():nullptr; }
    bool SameClone() const
    {
        auto* Game=World.IsValid()?World->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=Game?Game->GetSaveSession():nullptr;if (!Save || Save->HasSaveGameOperationPending()) return false;
        const auto Load=Game->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        return (Load.LoadName==TEXT("FPV-QA-Clone") || Header.SaveName==TEXT("FPV-QA-Clone")) && !Header.MapName.IsEmpty() && !Header.SessionName.IsEmpty();
    }
    bool Grounded() const
    {
        auto* Move=Pioneer.IsValid()?Pioneer->GetCharacterMovement():nullptr;if (!Move || !Move->IsMovingOnGround()) return false;
        FFindFloorResult Floor;Move->FindFloor(Pioneer->GetActorLocation(),Floor,false);return Floor.IsWalkableFloor();
    }
    bool NoLeases() const
    {
        const auto State=FactoryFPVInputLease::GetDiagnostics(PC.Get());return State.ActiveLeases==0 && State.OwnedMoveContributions==0 && State.OwnedLookContributions==0 && State.ResetDepth==0;
    }
    int32 F6Bindings(FInputActionUnifiedDelegate* Out=nullptr) const
    {
        if (!PC.IsValid() || !Hangar.IsValid()) return 0;int32 Count=0;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It))
            for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Hangar.Get()))
            { if (Out) *Out=Binding.KeyDelegate;++Count; }
        return Count;
    }
    bool InvokeF6()
    {
        FInputActionUnifiedDelegate Callback;if (F6Bindings(&Callback)!=1) return false;Callback.Execute(EKeys::F6);return true;
    }
    TArray<TSharedRef<SWidget>> WindowWidgets() const
    {
        TArray<TSharedRef<SWidget>> Found;auto Root=Window();if (!Root) return Found;
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Root.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768)
        {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());Found.Add(Widget);
            if (auto* Children=Widget->GetChildren()) for (int32 Index=0;Index<Children->Num();++Index) Pending.Add(Children->GetChildAt(Index));
        }
        return Found;
    }
    bool IsModWidget(const SWidget& Widget) const
    {
        return Widget.GetType()==FName(TEXT("SFactoryFPVOSD")) || Widget.GetType()==FName(TEXT("SFactoryFPVPanel")) || Widget.GetType()==FName(TEXT("SFactoryFPVRacePanel"));
    }
    bool NoMountedModWidgets() const
    {
        for (const auto& Widget:WindowWidgets()) if (IsModWidget(Widget.Get())) return false;return true;
    }
    void CollectResources()
    {
        for (TActorIterator<AActor> It(World.Get());It;++It) if (Cast<AFactoryFPVDrone>(*It) || Cast<AFactoryFPVPioneerAnchor>(*It) || Cast<AFactoryFPVPilotVisual>(*It)) OldModActors.AddUnique(*It);
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It)
            && (It->GetOwner()==Drone.Get() || (It->GetOwner()==PC.Get() && It->GetName().StartsWith(TEXT("FactoryFPVHangarControls"))))) OldModControls.AddUnique(*It);
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()
            && (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner()))) OldModSources.AddUnique(*It);
        for (const auto& Widget:WindowWidgets()) if (IsModWidget(Widget.Get())) OldModWidgets.Add(Widget);
    }
    bool ObserveCleanup()
    {
        bModSessionCleaned=!AFactoryFPVDrone::FindActive(World.Get()) && Hangar.IsValid() && !Hangar->IsPanelOpen();
        for (const auto& Item:OldModActors) if (Item.IsValid() && !Item->IsActorBeingDestroyed() && Item->HasActorBegunPlay()) bModSessionCleaned=false;
        bModWidgetsRemoved=NoMountedModWidgets();
        if (FSlateApplication::IsInitialized()) for (const auto& Weak:OldModWidgets)
            if (const auto Widget=Weak.Pin();Widget && FSlateApplication::Get().FindWidgetWindow(Widget.ToSharedRef()).IsValid()) bModWidgetsRemoved=false;
        bModControlsDetached=F6Bindings()==0;
        for (const auto& Item:OldModControls) if (Item.IsValid() && PC->IsInputComponentInStack(Item.Get())) bModControlsDetached=false;
        bModSourcesRetired=true;for (const auto& Item:OldModSources) if (Item.IsValid() && Item->IsStreamingSourceEnabled()) bModSourcesRetired=false;
        bUnpossessedNoLeases=NoLeases();
        return bModSessionCleaned && bModWidgetsRemoved && bModControlsDetached && bModSourcesRetired && bUnpossessedNoLeases;
    }
    FString SnapshotJson() const
    {
        auto* Controller=PC.Get();APawn* CurrentPawn=Controller?Controller->GetPawn():nullptr;auto* View=Controller?Controller->GetViewTarget():nullptr;
        auto* Camera=Controller?Controller->PlayerCameraManager.Get():nullptr;auto* Hud=Controller?Cast<AFGHUD>(Controller->GetHUD()):nullptr;
        auto* Move=Pioneer.IsValid()?Pioneer->GetCharacterMovement():nullptr;const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller);
        auto Focus=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():nullptr;
        FString Out=FString::Printf(TEXT("{\"fov\":%s,\"lockedFov\":%s,\"hudPartial\":%s,\"hudVisible\":%s,\"hudShow\":%s,\"cursor\":%s,\"viewportIgnoresInput\":%s,\"moveIgnored\":%s,\"lookIgnored\":%s,\"viewTargetClass\":\"%s\",\"currentPawnClass\":\"%s\",\"focusedWidgetType\":\"%s\",\"pioneerAlive\":%s,\"samePioneer\":%s,\"pioneerControllerClass\":\"%s\",\"needRespawn\":%s,\"isRespawning\":%s,\"respawnGracePeriod\":%s,\"movementMode\":%d,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"f6Bindings\":%d,\"mesh1P\":%s,\"mesh3P\":%s}"),
            Camera?*Number(Camera->GetFOVAngle()):TEXT("null"),Camera?*Number(Camera->GetLockedFOV()):TEXT("null"),Bool(Hud && Hud->GetPartialPumpiMode()),Bool(Hud && Hud->GetHUDVisibility()),Bool(Hud && Hud->bShowHUD),Bool(Controller && Controller->bShowMouseCursor),
            Bool(Viewport.IsValid() && Viewport->IgnoreInput()),Bool(Controller && Controller->IsMoveInputIgnored()),Bool(Controller && Controller->IsLookInputIgnored()),*Escape(ClassPath(View)),*Escape(ClassPath(CurrentPawn)),Focus?*Escape(Focus->GetTypeAsString()):TEXT(""),
            Bool(Pioneer.IsValid() && Pioneer->IsAliveAndWell()),Bool(CurrentPawn && Pioneer.IsValid() && FObjectKey(CurrentPawn)==PioneerKey),*Escape(ClassPath(Pioneer.IsValid()?Pioneer->GetController():nullptr)),
            Bool(Controller && Controller->NeedRespawn()),Bool(Controller && Controller->IsRespawning()),Bool(Controller && Controller->IsClientRespawnGracePeriodActive()),Move?static_cast<int32>(Move->MovementMode.GetValue()):INDEX_NONE,
            Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions,F6Bindings(),*MeshJson(Pioneer.IsValid()?Pioneer->GetMesh1P():nullptr),*MeshJson(Pioneer.IsValid()?Pioneer->GetMesh3P():nullptr));
        return Out;
    }
    void Snapshot(const TCHAR* Name) { Snapshots.Add(Name,SnapshotJson()); }
    FString Evidence() const
    {
        FString Out=FString::Printf(TEXT(",\"mode\":\"%s\",\"variant\":\"%s\",\"unpossessRequested\":%s,\"nativeUnpossessed\":%s,\"pioneerStayedAlive\":%s,\"modSessionCleaned\":%s,\"modWidgetsRemoved\":%s,\"modControlsDetached\":%s,\"modSourcesRetired\":%s,\"repossessRequested\":%s,\"nativeRepossessed\":%s,\"restoredGameplayReady\":%s,\"hangarRebound\":%s,\"freshSessionPassed\":%s,\"noModLeases\":%s,\"capturesVerified\":%s,\"programmaticNativePossession\":true,\"programmaticMenuActions\":true,\"injectedFlightControls\":false,\"forcedPossessionCompletion\":false,\"manualNativeInputMutation\":false,\"healingApplied\":false,\"fakePawnCreated\":false,\"savedWorld\":false,\"unpossessedNoModLeases\":%s,\"unpossessedSamples\":%d,\"maximumPioneerDisplacementCm\":%s,\"startupSplashObserved\":%s,\"startupSplashAnimationObserved\":%s,\"startupSplashFinished\":%s"),
            *Escape(Mode),*Escape(Mode),Bool(bUnpossessRequested),Bool(bNativeUnpossessed),Bool(bPioneerStayedAlive),Bool(bModSessionCleaned),Bool(bModWidgetsRemoved),Bool(bModControlsDetached),Bool(bModSourcesRetired),Bool(bRepossessRequested),Bool(bNativeRepossessed),Bool(bRestoredReady),Bool(bHangarRebound),Bool(bFreshSessionPassed),Bool(bNoModLeases),Bool(bCapturesVerified),Bool(bUnpossessedNoLeases),UnpossessedSamples,*Number(MaximumPioneerDisplacementCm),Bool(bSplashObserved),Bool(bAnimationObserved),Bool(bStartupFinished));
        Out+=TEXT(",\"snapshots\":{");bool First=true;
        for (const TCHAR* Name:{TEXT("initial"),TEXT("before"),TEXT("afterUnPossess"),TEXT("unpossessed"),TEXT("afterPossess"),TEXT("restored"),TEXT("freshReturn"),TEXT("failure")})
            if (const FString* Value=Snapshots.Find(Name)) { if (!First) Out+=TEXT(",");First=false;Out+=FString::Printf(TEXT("\"%s\":%s"),Name,**Value); }
        Out+=TEXT("},\"captures\":[");if (bCapture) Out+=BeforeCapture.Json()+TEXT(",")+UnpossessedCapture.Json()+TEXT(",")+RestoredCapture.Json();return Out+TEXT("]");
    }
    bool InitialReady()
    {
        auto* Current=World.Get();auto* Controller=Current?Cast<AFGPlayerController>(Current->GetFirstPlayerController()):nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() || !SameClone() || !Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer()) return false;
        auto* UI=Controller->GetGameUI();if (!UI || !FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
        auto* Character=Cast<AFGCharacterPlayer>(Controller->GetPawn());
        if (!Character || !Character->IsAliveAndWell() || !Character->HasAuthority() || Character->GetController()!=Controller || !Controller->PlayerCameraManager || !Controller->GetHUD()
            || Current->IsPaused() || Controller->IsPaused() || Controller->IsPauseMenuOpen() || Controller->NeedRespawn() || Controller->IsRespawning() || Controller->IsClientRespawnGracePeriodActive()
            || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget()) return false;
        PC=Controller;Pioneer=Character;Instance=Current->GetGameInstance();Player=Controller->GetLocalPlayer();Viewport=Current->GetGameViewport();Hangar=Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        return Instance.IsValid() && Player.IsValid() && Viewport.IsValid() && Hangar.IsValid() && Grounded();
    }
    bool OwnersValid() const
    {
        return World.IsValid() && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone && FObjectKey(World.Get())==WorldKey && SameClone()
            && Instance.IsValid() && World->GetGameInstance()==Instance.Get() && FObjectKey(Instance.Get())==InstanceKey
            && Player.IsValid() && FObjectKey(Player.Get())==PlayerKey && Player->GetGameInstance()==Instance.Get()
            && PC.IsValid() && !PC->IsActorBeingDestroyed() && World->GetFirstPlayerController()==PC.Get() && PC->GetLocalPlayer()==Player.Get()
            && Pioneer.IsValid() && !Pioneer->IsActorBeingDestroyed() && Pioneer->GetWorld()==World.Get() && FObjectKey(Pioneer.Get())==PioneerKey && Hangar.IsValid();
    }
    bool Prepare(double Now)
    {
        if (Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(World.Get()) || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get())
            || !NoLeases() || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()) { Finish(false,TEXT("Possession QA requires no existing mod/input owners."));return false; }
        WorldKey=FObjectKey(World.Get());InstanceKey=FObjectKey(Instance.Get());PlayerKey=FObjectKey(Player.Get());PioneerKey=FObjectKey(Pioneer.Get());OriginalLocation=Pioneer->GetActorLocation();Snapshot(TEXT("initial"));
        if (Mode==TEXT("flight"))
        {
            FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Ordinary disarmed pre-unpossess launch failed: ")+Error);return false; }
        }
        else if (Mode==TEXT("panel"))
        {
            if (!InvokeF6()) return false;bOwnedPanel=Hangar->IsPanelOpen();if (!bOwnedPanel) { Finish(false,TEXT("Actual F6 callback failed to open pre-unpossess panel."));return false; }
        }
        CollectResources();bPrepared=true;SetPhase(EPhase::BeforeSettle,Now);return true;
    }
    bool VariantAtBoundary()
    {
        if (!OwnersValid() || PC->GetPawn()!=Pioneer.Get() || Pioneer->GetController()!=PC.Get() || !Pioneer->IsAliveAndWell()) return false;
        if (Mode==TEXT("flight")) return Drone.IsValid() && !Drone->IsActorBeingDestroyed() && !Drone->IsFlightArmed() && AFactoryFPVDrone::FindActive(World.Get())==Drone.Get()
            && Drone->GetOwner()==PC.Get() && PC->GetViewTarget()==Drone.Get() && !Hangar->IsPanelOpen();
        if (Mode==TEXT("panel")) return bOwnedPanel && Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(World.Get());
        return !Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(World.Get()) && NoLeases();
    }
    void Unpossess(double Now)
    {
        if (!VariantAtBoundary()) { Finish(false,TEXT("Original live owner or selected variant ended before native UnPossess; refusing a mislabeled baseline probe."));return; }
        bUnpossessRequested=true;
        // Native callbacks may synchronously return an unequipped drone. Do not
        // call Exit/Close or repair any state before observing their result.
        PC->UnPossess();Snapshot(TEXT("afterUnPossess"));SetPhase(EPhase::Unpossessed,Now);
        LastReason=TEXT("Waiting for natural mod cleanup while the real living pioneer is genuinely unpossessed.");
    }
    bool GenuinelyUnpossessed() const
    {
        return OwnersValid() && PC->GetPawn()==nullptr && Pioneer->GetController()==nullptr && Pioneer->IsAliveAndWell();
    }
    void Repossess(double Now)
    {
        if (!GenuinelyUnpossessed() || !ObserveCleanup()) { Finish(false,TEXT("Unpossessed live owner/cleanup changed before public Possess; no completion forced."));return; }
        bRepossessRequested=true;PC->Possess(Pioneer.Get());Snapshot(TEXT("afterPossess"));SetPhase(EPhase::Repossessing,Now);
        LastReason=TEXT("Waiting for native repossession and ordinary readiness of the same living pioneer.");
    }
    bool GameplayReady()
    {
        if (!OwnersValid() || PC->GetPawn()!=Pioneer.Get() || Pioneer->GetController()!=PC.Get() || !Pioneer->IsAliveAndWell() || !Grounded()
            || PC->NeedRespawn() || PC->IsRespawning() || PC->IsClientRespawnGracePeriodActive() || World->IsPaused() || PC->IsPaused() || PC->IsPauseMenuOpen()) return false;
        auto* UI=PC->GetGameUI();if (!UI || UI->IsPauseMenuOpen() || UI->HasActiveInteractWidget()) return false;
        if (!FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
        return PC->PlayerCameraManager && PC->GetHUD() && !Hangar->IsPanelOpen() && !AFactoryFPVDrone::FindActive(World.Get())
            && !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) && NoLeases() && NoMountedModWidgets() && F6Bindings()==1;
    }
    void ReleaseScreenshot()
    {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot)
        { FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore; }bOwnScreenshotPending=false;
    }
    bool RequestCapture(FCapture& Capture,const TCHAR* State,double Now)
    {
        auto* V=Viewport.Get();if (FScreenshotRequest::IsScreenshotRequested() || !V || !V->Viewport || V->Viewport->GetSizeXY().X<320 || V->Viewport->GetSizeXY().Y<180)
        { Finish(false,TEXT("Viewport unavailable or unrelated screenshot already pending."));return false; }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);const FString Dir=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));
        FString Path=FPaths::Combine(Dir,FString::Printf(TEXT("Possession-%s-%s.png"),State,*Mode));FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Path,Dir) || !FPaths::IsUnderDirectory(Dir,Saved)
            || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Dir,true)) { Finish(false,TEXT("Capture path is not fresh and inside disposable Saved."));return false; }
        ReleaseScreenshot();Capture.Path=PendingScreenshot=Path;CaptureStarted=Now;bScreenshotProcessed=false;bOwnScreenshotPending=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]()
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshotPending) { Run->bScreenshotProcessed=true;Run->bOwnScreenshotPending=false; } });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);return true;
    }
    bool CaptureFinished(FCapture& Capture,double Now)
    {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Possession screenshot exceeded five seconds: ")+Capture.Path);return false; }
        if (!bScreenshotProcessed) return false;ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) || Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180
            || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty()) { Finish(false,TEXT("Processed possession capture is not a sane decodable PNG."));return false; }
        double Sum=0,Squares=0;int32 Samples=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60))
        {
            const auto Color=Image.GetOnePixelLinear(X,Y);if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) { Finish(false,TEXT("Capture pixels are nonfinite."));return false; }
            const double L=.2126*Color.R+.7152*Color.G+.0722*Color.B;Sum+=L;Squares+=L*L;++Samples;
        }
        Capture.LumaVariance=Samples?FMath::Max(0.,Squares/Samples-FMath::Square(Sum/Samples)):0.;
        if (!FMath::IsFinite(Capture.LumaVariance) || Capture.LumaVariance<1e-8) { Finish(false,TEXT("Actual possession capture is uniformly blank."));return false; }
        Capture.Width=Image.SizeX;Capture.Height=Image.SizeY;Capture.bDecoded=true;return true;
    }
    void Cleanup()
    {
        ReleaseScreenshot();
        // No automatic Possess retry, camera/input fix, healing or native UI
        // closure on failure. The disposable clone is intentionally not saved.
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==PC.Get()) Own->ExitDrone();
        if (bOwnedPanel && Hangar.IsValid() && Hangar->IsPanelOpen()) Hangar->ClosePanel();
    }
    void Finish(bool Success,const FString& Reason)
    {
        if (bDone) return;
        if (!Success && PC.IsValid()) Snapshot(TEXT("failure"));
        bDone=true;Cleanup();Marker(Mode,Success?TEXT("PASS"):TEXT("FAIL"),Reason,Evidence());FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    void Complete()
    {
        // Revalidate after the async screenshots and final ordinary return;
        // earlier flags cannot turn a disappeared/replaced owner into PASS.
        const bool FinalReady=GameplayReady();bNoModLeases=NoLeases();
        bCapturesVerified=!bCapture || (BeforeCapture.bDecoded && UnpossessedCapture.bDecoded && RestoredCapture.bDecoded);
        const bool Pass=FinalReady && bUnpossessRequested && bNativeUnpossessed && bPioneerStayedAlive && bModSessionCleaned && bModWidgetsRemoved && bModControlsDetached
            && bModSourcesRetired && bUnpossessedNoLeases && bRepossessRequested && bNativeRepossessed && bRestoredReady && bHangarRebound && bFreshSessionPassed && bNoModLeases && bCapturesVerified;
        Finish(Pass,Pass?TEXT("Public native UnPossess/Possess of the same living pioneer earned natural mod retirement, native restored readiness, unique bound F6 and fresh disarmed return; no save or forced completion."):
            TEXT("Possession workflow did not retain all actual owner/cleanup/restored-state checks at final verification."));
    }
    bool Tick()
    {
        if (bDone) return false;const double Now=FPlatformTime::Seconds();
        if (!Guard()) { Finish(false,TEXT("Possession QA isolation guard changed."));return false; }
        if (Now>Deadline) { Finish(false,TEXT("45-second possession QA deadline: ")+LastReason);return false; }
        if (Phase==EPhase::Waiting) { if (InitialReady()) Prepare(Now);return !bDone; }
        if (!OwnersValid()) { Finish(false,TEXT("Original live world/controller/pioneer identity changed; no state repaired."));return false; }
        if (!Pioneer->IsAliveAndWell()) { bPioneerStayedAlive=false;Finish(false,TEXT("The original pioneer died; live possession qualification cannot continue."));return false; }
        const double Displacement=FVector::Distance(Pioneer->GetActorLocation(),OriginalLocation);
        if (!FMath::IsFinite(Displacement)) { Finish(false,TEXT("Pioneer pose became nonfinite."));return false; }
        MaximumPioneerDisplacementCm=FMath::Max(MaximumPioneerDisplacementCm,Displacement);
        if (Phase==EPhase::BeforeSettle && Settled(Now))
        {
            if (!VariantAtBoundary()) { Finish(false,TEXT("Selected variant or current living owner ended before capture."));return false; }Snapshot(TEXT("before"));
            if (bCapture) { if (RequestCapture(BeforeCapture,TEXT("Before"),Now)) SetPhase(EPhase::CaptureBefore,Now); }else Unpossess(Now);
        }
        else if (Phase==EPhase::CaptureBefore) { if (CaptureFinished(BeforeCapture,Now)) Unpossess(Now); }
        else if (Phase==EPhase::Unpossessed)
        {
            if (!GenuinelyUnpossessed()) { Finish(false,TEXT("Native UnPossess did not leave the original living pioneer/controller genuinely unpossessed."));return false; }
            if (LastUnpossessedFrame!=GFrameCounter) { ++UnpossessedSamples;LastUnpossessedFrame=GFrameCounter; }
            if (!Settled(Now,.6) || UnpossessedSamples<4 || !ObserveCleanup()) return true;
            bNativeUnpossessed=true;Snapshot(TEXT("unpossessed"));
            if (bCapture) { if (RequestCapture(UnpossessedCapture,TEXT("Unpossessed"),Now)) SetPhase(EPhase::CaptureUnpossessed,Now); }else Repossess(Now);
        }
        else if (Phase==EPhase::CaptureUnpossessed)
        {
            if (!GenuinelyUnpossessed() || !ObserveCleanup()) { Finish(false,TEXT("Unpossessed owner/cleanup changed during native viewport capture."));return false; }
            if (CaptureFinished(UnpossessedCapture,Now)) Repossess(Now);
        }
        else if (Phase==EPhase::Repossessing)
        {
            if (!GameplayReady()) { ReadySince=0;return true; }
            if (ReadySince==0) ReadySince=Now;if (Now-ReadySince<.75) return true;
            bNativeRepossessed=true;bRestoredReady=true;Snapshot(TEXT("restored"));SetPhase(EPhase::RestoredSettle,Now);
        }
        else if (Phase==EPhase::RestoredSettle && Settled(Now))
        {
            if (!GameplayReady()) { Finish(false,TEXT("Native repossessed owner/readiness changed before capture."));return false; }
            if (bCapture) { if (RequestCapture(RestoredCapture,TEXT("Restored"),Now)) SetPhase(EPhase::CaptureRestored,Now); }else OpenRestoredHangar(Now);
        }
        else if (Phase==EPhase::CaptureRestored)
        {
            if (!GameplayReady()) { Finish(false,TEXT("Native restored owner/readiness changed during capture."));return false; }
            if (CaptureFinished(RestoredCapture,Now)) OpenRestoredHangar(Now);
        }
        else if (Phase==EPhase::HangarOpened && Settled(Now))
        {
            if (PC->GetPawn()!=Pioneer.Get() || Pioneer->GetController()!=PC.Get() || !Hangar->IsPanelOpen() || !InvokeF6() || Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual rebound F6 did not close its owned hangar."));return false; }
            bOwnedPanel=false;SetPhase(EPhase::HangarClosed,Now);
        }
        else if (Phase==EPhase::HangarClosed && Settled(Now))
        {
            if (!GameplayReady()) { Finish(false,TEXT("Rebound F6 close did not restore actual native gameplay readiness."));return false; }
            bHangarRebound=true;FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Fresh ordinary post-possession launch failed: ")+Error);return false; }SetPhase(EPhase::FreshFlight,Now);
        }
        else if (Phase==EPhase::FreshFlight)
        {
            if (!Drone.IsValid() || Drone->IsActorBeingDestroyed() || Drone->IsFlightArmed() || PC->GetViewTarget()!=Drone.Get() || PC->GetPawn()!=Pioneer.Get() || Pioneer->GetController()!=PC.Get())
            { Finish(false,TEXT("Fresh ordinary disarmed session lost its actual owner/view."));return false; }
            if (Settled(Now)) { Drone->ExitDrone();SetPhase(EPhase::FreshReturn,Now); }
        }
        else if (Phase==EPhase::FreshReturn)
        {
            if (!Settled(Now) || !GameplayReady()) return true;bFreshSessionPassed=true;Snapshot(TEXT("freshReturn"));Complete();
        }
        return !bDone;
    }
    void OpenRestoredHangar(double Now)
    {
        if (!GameplayReady() || !InvokeF6()) { Finish(false,TEXT("Repossessed local player has no unique actual ready F6 callback."));return; }
        bOwnedPanel=Hangar->IsPanelOpen();if (!bOwnedPanel) { Finish(false,TEXT("Actual rebound F6 did not open restored hangar."));return; }SetPhase(EPhase::HangarOpened,Now);
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingPossessionQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingPossessionQAInternal;
    FString Mode;FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQAPossession="),Mode);Mode.ToLowerInline();
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (Mode!=TEXT("baseline") && Mode!=TEXT("flight") && Mode!=TEXT("panel")))
    {
        Marker(Mode.IsEmpty()?TEXT("invalid"):Mode,TEXT("FAIL"),TEXT("Explicit automation, disposable standalone clone and -FactoryFPVQAPossession=baseline|flight|panel required."));
        Output.Log(TEXT("Possession QA guard/mode refused."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Possession QA already active."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Mode=Mode;Run->Deadline=FPlatformTime::Seconds()+45.;Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Active=Run;
    Marker(Mode,TEXT("PENDING"),TEXT("Waiting for exact living clone/native startup before bounded public UnPossess/Possess of the same real pioneer; no healing, input mutation or save."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}),.1f);
}
