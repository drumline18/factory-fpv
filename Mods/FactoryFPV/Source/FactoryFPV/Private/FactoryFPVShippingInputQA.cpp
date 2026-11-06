#include "FactoryFPVShippingInputQA.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHUD.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/SWindow.h"

namespace FactoryFPVShippingInputQAInternal {
const TCHAR* LaunchCommand=TEXT("fpv.start keyboard");
const TCHAR* PanelRefusal=TEXT("Close the Factory FPV panel or use Save & fly from the hangar.");

FString Escape(FString Value) {
    return Value.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n"));
}
void Marker(const TCHAR* Result,const FString& Reason,const FString& Extra=FString()) {
    const FString Message=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"input\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"registered launch refusal, native console command entry, actual reset hooks and owned input counter balance; excludes physical input, armed flight and arbitrary unbalanced foreign decrements\"%s}"),Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Message,ELogVerbosity::Display,NAME_None);
}
bool Disposable(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);FPaths::CollapseRelativeDirectories(Path);
    return Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard() {
    return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) &&
        Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath());
}
FString DiagnosticJson(const FFactoryFPVInputLeaseDiagnostics& State) {
    return FString::Printf(TEXT("{\"hooksReady\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"resetDepth\":%d,\"moveResets\":%llu,\"lookResets\":%llu,\"allResets\":%llu}"),
        State.bNativeHooksReady?TEXT("true"):TEXT("false"),State.ActiveLeases,State.OwnedMoveContributions,State.OwnedLookContributions,
        State.ResetDepth,static_cast<unsigned long long>(State.MoveResetEvents),static_cast<unsigned long long>(State.LookResetEvents),static_cast<unsigned long long>(State.AllResetEvents));
}
TSharedPtr<SWidget> MountedHangar(UGameViewportClient& Viewport) {
    const auto Window=Viewport.GetWindow();
    if (!Window) return nullptr;
    TSharedPtr<SWidget> Match;
    TArray<TSharedRef<SWidget>> Widgets{Window.ToSharedRef()};
    while (!Widgets.IsEmpty()) {
        const auto Widget=Widgets.Pop(EAllowShrinking::No);
        if (Widget->GetTypeAsString()==TEXT("SFactoryFPVPanel")) {
            if (Match) return nullptr;
            Match=Widget;
        }
        auto* Children=Widget->GetChildren();
        for (int32 Index=0;Index<Children->Num();++Index) Widgets.Add(Children->GetChildAt(Index));
    }
    return Match;
}
struct FInputMembership { TWeakObjectPtr<UInputComponent> Component;bool bPresent=false; };
struct FRun : public TSharedFromThis<FRun> {
    enum class EPhase { Waiting,DroneAll,DroneMove,DroneLook,DroneBaseMove,DroneBaseAll,DronePlayerAll,DroneReturn,PanelImmediate,PanelRenew,Restore };
    enum class EResetEntry { Virtual,Controller,PlayerController };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> PC;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<APlayerCameraManager> CameraManager;
    TWeakObjectPtr<AFGHUD> HUD;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<UConsole> OwnedConsole;
    TArray<FInputMembership> OriginalInputs;
    FVector OriginalLocation=FVector::ZeroVector;
    EMovementMode OriginalMovement=MOVE_None;uint8 OriginalCustomMovement=0;
    float OriginalFov=0,OriginalFovLock=0;
    bool bOriginalPartialHUD=false,bOriginalCursor=false;
    bool bForeignMove=false,bForeignLook=false,bDone=false,bSnapshot=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bSplashFinished=false,bFrontEndFinished=false;
    bool bNativeHooksVerified=false,bAllResetVerified=false,bMoveResetVerified=false,bLookResetVerified=false;
    bool bBaseMoveVerified=false,bBaseAllVerified=false,bPlayerAllVerified=false;
    bool bRegisteredLaunchRefusedDuringHangar=false,bViewportConsoleLaunchVerified=false;
    bool bDroneRenewed=false,bDroneForeignSurvived=false,bDroneBalanced=false,bPanelImmediateVerified=false,bPanelRenewVerified=false,bRestored=false;
    double Deadline=0,PhaseStarted=0;uint64 PhaseFrame=0;
    EPhase Phase=EPhase::Waiting;
    FString LastReason=TEXT("Waiting for the actual loaded clone and native startup lifecycle."),Snapshots=TEXT("[");int32 SnapshotCount=0;

    FString Evidence() const {
        return FString::Printf(TEXT(",\"nativeHooksVerified\":%s,\"allResetVerified\":%s,\"moveResetVerified\":%s,\"lookResetVerified\":%s,\"droneRenewed\":%s,\"droneForeignSurvived\":%s,\"droneBalanced\":%s,\"panelImmediateVerified\":%s,\"panelRenewVerified\":%s,\"pioneerRestored\":%s,\"parentResetEntryPointsVerified\":%s,\"registeredLaunchRefusedDuringHangar\":%s,\"viewportConsoleLaunchVerified\":%s,\"injectedFlightControls\":false,\"manualResetNotifications\":false,\"snapshots\":%s]"),
            bNativeHooksVerified?TEXT("true"):TEXT("false"),bAllResetVerified?TEXT("true"):TEXT("false"),bMoveResetVerified?TEXT("true"):TEXT("false"),bLookResetVerified?TEXT("true"):TEXT("false"),
            bDroneRenewed?TEXT("true"):TEXT("false"),bDroneForeignSurvived?TEXT("true"):TEXT("false"),bDroneBalanced?TEXT("true"):TEXT("false"),bPanelImmediateVerified?TEXT("true"):TEXT("false"),
            bPanelRenewVerified?TEXT("true"):TEXT("false"),bRestored?TEXT("true"):TEXT("false"),
            bBaseMoveVerified && bBaseAllVerified && bPlayerAllVerified?TEXT("true"):TEXT("false"),
            bRegisteredLaunchRefusedDuringHangar?TEXT("true"):TEXT("false"),bViewportConsoleLaunchVerified?TEXT("true"):TEXT("false"),*Snapshots);
    }
    void Record(const TCHAR* Name) {
        const auto State=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        if (SnapshotCount++) Snapshots+=TEXT(",");
        Snapshots+=FString::Printf(TEXT("{\"name\":\"%s\",\"moveIgnored\":%s,\"lookIgnored\":%s,\"state\":%s}"),Name,
            PC.IsValid() && PC->IsMoveInputIgnored()?TEXT("true"):TEXT("false"),PC.IsValid() && PC->IsLookInputIgnored()?TEXT("true"):TEXT("false"),*DiagnosticJson(State));
    }
    void ReleaseForeign() {
        if (PC.IsValid()) {
            if (bForeignMove) { bForeignMove=false;PC->SetIgnoreMoveInput(false); }
            if (bForeignLook) { bForeignLook=false;PC->SetIgnoreLookInput(false); }
        }
    }
    void Cleanup() {
        if (auto* Console=OwnedConsole.Get();Console && Console->ConsoleState!=NAME_None) Console->FakeGotoState(NAME_None);
        OwnedConsole.Reset();
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==PC.Get()) Own->ExitDrone();
        if (Hangar.IsValid() && bSnapshot && Hangar->IsPanelOpen()) Hangar->ClosePanel();
        ReleaseForeign();
    }
    void Finish(bool Success,const FString& Reason) {
        if (bDone) return;bDone=true;
        const FString Detail=Evidence();Cleanup();Marker(Success?TEXT("PASS"):TEXT("FAIL"),Reason,Detail);
        FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    void SetPhase(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    bool TwoFrames(double Now) const { return Now-PhaseStarted>=.25 && GFrameCounter>=PhaseFrame+2; }
    bool Owned(int32 Move,int32 Look,int32 Active=1) const {
        const auto State=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        return State.bNativeHooksReady && State.ResetDepth==0 && State.ActiveLeases==Active && State.OwnedMoveContributions==Move && State.OwnedLookContributions==Look;
    }
    bool Ready() {
        auto* Current=World.Get();auto* Mode=Current?Current->GetAuthGameMode<AFGGameMode>():nullptr;
        auto* Save=Mode?Mode->GetSaveSession():nullptr;auto* Controller=Current?Cast<AFGPlayerController>(Current->GetFirstPlayerController()):nullptr;
        auto* Character=Controller?Cast<AFGCharacterPlayer>(Controller->GetPawn()):nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() || !Mode || Mode->IsMainMenuGameMode() ||
            !Save || Save->HasSaveGameOperationPending() || !Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer() ||
            !Character || !Character->IsAliveAndWell() || Controller->IsPaused() || Controller->IsPauseMenuOpen()) return false;
        const auto Load=Mode->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        if ((Load.LoadName!=TEXT("FPV-QA-Clone") && Header.SaveName!=TEXT("FPV-QA-Clone")) || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty()) return false;
        if (bSnapshot && (PC.Get()!=Controller || Pioneer.Get()!=Character)) { Finish(false,TEXT("Original controller/pioneer changed during input QA."));return false; }
        PC=Controller;Pioneer=Character;
        auto* UI=Controller->GetGameUI();
        if (!UI || UI->HasActiveInteractWidget() || UI->IsPauseMenuOpen()) return false;
        return Phase!=EPhase::Waiting || FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bSplashAnimationObserved,bSplashFinished,bFrontEndFinished);
    }
    bool VerifyLaunchWithOpenHangar(UGameViewportClient& Viewport) {
        Hangar->OpenPanel();
        const auto Mounted=MountedHangar(Viewport);
        if (!Hangar->IsHangarOpen() || !Mounted || !Owned(1,1) || !PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored()) {
            Finish(false,TEXT("Launch refusal requires the mounted hangar and its actual native input lease."));return false;
        }
        const auto Before=DiagnosticJson(FactoryFPVInputLease::GetDiagnostics(PC.Get()));
        const auto ViewBefore=PC->GetViewTarget();
        const bool bCursor=PC->bShowMouseCursor,bViewportIgnored=Viewport.IgnoreInput();
        FStringOutputDevice Reply;
        const bool bHandled=IConsoleManager::Get().ProcessUserConsoleInput(LaunchCommand,Reply,World.Get());
        Drone=AFactoryFPVDrone::FindActive(World.Get());
        bRegisteredLaunchRefusedDuringHangar=bHandled && Reply.TrimStartAndEnd()==PanelRefusal && !Drone.IsValid() &&
            !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) && Hangar->IsHangarOpen() && MountedHangar(Viewport)==Mounted &&
            PC->GetPawn()==Pioneer.Get() && PC->GetViewTarget()==ViewBefore && PC->bShowMouseCursor==bCursor && Viewport.IgnoreInput()==bViewportIgnored &&
            PC->IsMoveInputIgnored() && PC->IsLookInputIgnored() && DiagnosticJson(FactoryFPVInputLease::GetDiagnostics(PC.Get()))==Before;
        if (!bRegisteredLaunchRefusedDuringHangar) {
            Finish(false,TEXT("Registered launch did not return the exact hangar refusal with unchanged panel, view and input: ")+Reply);return false;
        }
        Record(TEXT("registered-launch-refused-during-hangar"));
        Hangar->ClosePanel();
        if (!Restored()) { Finish(false,TEXT("Closing the unchanged hangar did not restore the original launch state."));return false; }
        return true;
    }
    bool LaunchFromViewportConsole(UGameViewportClient& Viewport) {
        auto* Console=Viewport.ViewportConsole.Get();
        if (!FSlateApplication::IsInitialized() || !Console || Console->ConsoleState!=NAME_None) {
            Finish(false,TEXT("Launch QA requires the existing closed viewport console."));return false;
        }
        OwnedConsole=Console;
        Console->StartTyping(LaunchCommand);
        const bool bTyping=Console->ConsoleState==TEXT("Typing") && Console->ConsoleActive();
        const bool bEntered=bTyping && Console->InputKey(IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),EKeys::Enter,IE_Released);
        Drone=AFactoryFPVDrone::FindActive(World.Get());
        bViewportConsoleLaunchVerified=bEntered && Console->ConsoleState==NAME_None && !Console->ConsoleActive() &&
            Drone.IsValid() && !Drone->IsFlightArmed() && PC->GetViewTarget()==Drone.Get() && PC->GetPawn()==Pioneer.Get() &&
            !Hangar->IsPanelOpen() && Owned(1,1);
        if (!bViewportConsoleLaunchVerified) {
            Finish(false,TEXT("The native console's Typing command did not close normally and start an ordinary disarmed drone."));return false;
        }
        OwnedConsole.Reset();
        return true;
    }
    bool CaptureAndLaunch(double Now) {
        Hangar=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        if (!Hangar.IsValid() || Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(World.Get()) || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) ||
            PC->IsMoveInputIgnored() || PC->IsLookInputIgnored() || FactoryFPVInputLease::GetDiagnostics(PC.Get()).ActiveLeases!=0) {
            Finish(false,TEXT("Ownership QA requires an unheld living pioneer with no pre-existing mod session/panel."));return false;
        }
        auto* Move=Pioneer->GetCharacterMovement();CameraManager=PC->PlayerCameraManager;HUD=Cast<AFGHUD>(PC->GetHUD());OriginalView=PC->GetViewTarget();
        auto* Viewport=World->GetGameViewport();
        if (!Move || !CameraManager.IsValid() || !HUD.IsValid() || !OriginalView.IsValid() || !Viewport) return false;
        OriginalLocation=Pioneer->GetActorLocation();OriginalMovement=Move->MovementMode;OriginalCustomMovement=Move->CustomMovementMode;
        OriginalFov=CameraManager->GetFOVAngle();OriginalFovLock=CameraManager->GetLockedFOV();bOriginalPartialHUD=HUD->GetPartialPumpiMode();bOriginalCursor=PC->bShowMouseCursor;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()) OriginalInputs.Add({*It,PC->IsInputComponentInStack(*It)});
        bSnapshot=true;
        if (!VerifyLaunchWithOpenHangar(*Viewport) || !LaunchFromViewportConsole(*Viewport)) return false;
        Record(TEXT("drone-acquired"));
        return Reset(EFactoryFPVInputResetAxes::Both,EPhase::DroneAll,Now);
    }
    bool Reset(EFactoryFPVInputResetAxes Axes,EPhase Next,double Now,EResetEntry Entry=EResetEntry::Virtual) {
        const auto Before=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        if (Entry==EResetEntry::Controller && Axes==EFactoryFPVInputResetAxes::Both) PC->AController::ResetIgnoreInputFlags();
        else if (Entry==EResetEntry::Controller) PC->AController::ResetIgnoreMoveInput();
        else if (Entry==EResetEntry::PlayerController) PC->APlayerController::ResetIgnoreInputFlags();
        else if (Axes==EFactoryFPVInputResetAxes::Both) PC->ResetIgnoreInputFlags();
        else if (Axes==EFactoryFPVInputResetAxes::Move) PC->ResetIgnoreMoveInput();
        else PC->ResetIgnoreLookInput();
        const auto After=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        const bool Move=Axes!=EFactoryFPVInputResetAxes::Look,Look=Axes!=EFactoryFPVInputResetAxes::Move;
        const bool Observed=After.bNativeHooksReady && After.ResetDepth==0 &&
            (!Move || (After.MoveResetEvents>Before.MoveResetEvents && !PC->IsMoveInputIgnored() && After.OwnedMoveContributions==0)) &&
            (!Look || (After.LookResetEvents>Before.LookResetEvents && !PC->IsLookInputIgnored() && After.OwnedLookContributions==0)) &&
            (Axes!=EFactoryFPVInputResetAxes::Both || After.AllResetEvents>Before.AllResetEvents);
        Record(Entry==EResetEntry::Virtual?TEXT("native-virtual-reset-returned"):
            Entry==EResetEntry::PlayerController?TEXT("native-playercontroller-reset-returned"):
            Axes==EFactoryFPVInputResetAxes::Both?TEXT("native-controller-all-reset-returned"):TEXT("native-controller-move-reset-returned"));
        if (!Observed || After.ActiveLeases!=1 || (!Move && After.OwnedMoveContributions!=1) || (!Look && After.OwnedLookContributions!=1)) {
            Finish(false,TEXT("Real native reset hook did not synchronously withdraw exactly the affected owned contributions."));return false;
        }
        bNativeHooksVerified=true;
        if (Axes==EFactoryFPVInputResetAxes::Both) bAllResetVerified=true;
        else if (Axes==EFactoryFPVInputResetAxes::Move) bMoveResetVerified=true;
        else bLookResetVerified=true;
        if (Entry==EResetEntry::Controller && Axes==EFactoryFPVInputResetAxes::Move) bBaseMoveVerified=true;
        if (Entry==EResetEntry::Controller && Axes==EFactoryFPVInputResetAxes::Both) bBaseAllVerified=true;
        if (Entry==EResetEntry::PlayerController) bPlayerAllVerified=true;
        // The public reset deliberately clears earlier foreign holds on its axes.
        if (Move) { bForeignMove=false;PC->SetIgnoreMoveInput(true);bForeignMove=true; }
        if (Look) { bForeignLook=false;PC->SetIgnoreLookInput(true);bForeignLook=true; }
        Record(TEXT("new-foreign-holds"));SetPhase(Next,Now);return true;
    }
    bool VerifyForeignAndRelease(const TCHAR* Name) {
        Record(Name);
        if (!PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored() || !Owned(0,0,0)) {
            Finish(false,TEXT("Returning mod owner consumed a newer foreign hold or retained a lease."));return false;
        }
        ReleaseForeign();Record(TEXT("single-foreign-release"));
        if (PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()) { Finish(false,TEXT("One matching foreign release did not clear the counters; an owned hold leaked."));return false; }
        return true;
    }
    bool Restored() const {
        if (!PC.IsValid() || !Pioneer.IsValid() || !Pioneer->IsAliveAndWell() || !CameraManager.IsValid() || !HUD.IsValid() || !Hangar.IsValid() || Hangar->IsPanelOpen() ||
            PC->GetPawn()!=Pioneer.Get() || PC->GetViewTarget()!=OriginalView.Get() || PC->PlayerCameraManager!=CameraManager.Get() || PC->GetHUD()!=HUD.Get() ||
            PC->IsMoveInputIgnored() || PC->IsLookInputIgnored() || PC->bShowMouseCursor!=bOriginalCursor || HUD->GetPartialPumpiMode()!=bOriginalPartialHUD ||
            !Pioneer->GetActorLocation().Equals(OriginalLocation,.5) || !Pioneer->GetCharacterMovement() ||
            Pioneer->GetCharacterMovement()->MovementMode!=OriginalMovement || Pioneer->GetCharacterMovement()->CustomMovementMode!=OriginalCustomMovement ||
            !FMath::IsNearlyEqual(CameraManager->GetFOVAngle(),OriginalFov,.1f) || !FMath::IsNearlyEqual(CameraManager->GetLockedFOV(),OriginalFovLock,.01f) ||
            AFactoryFPVDrone::FindActive(World.Get()) || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) || !Owned(0,0,0)) return false;
        for (const auto& Input:OriginalInputs) if (Input.Component.IsValid()?PC->IsInputComponentInStack(Input.Component.Get())!=Input.bPresent:Input.bPresent) return false;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It) &&
            !OriginalInputs.ContainsByPredicate([&](const FInputMembership& Item){return Item.Component.Get()==*It;})) return false;
        return true;
    }
    bool Tick() {
        if (bDone) return false;
        const double Now=FPlatformTime::Seconds();
        if (!Guard() || !World.IsValid()) { Finish(false,TEXT("Input QA isolation guard/world changed."));return false; }
        if (Now>Deadline) { Finish(false,TEXT("30-second input QA deadline: ")+LastReason);return false; }
        if (!Ready()) return !bDone;
        if (Phase==EPhase::Waiting) { CaptureAndLaunch(Now);return !bDone; }
        if (Phase==EPhase::DroneAll || Phase==EPhase::DroneMove || Phase==EPhase::DroneLook ||
            Phase==EPhase::DroneBaseMove || Phase==EPhase::DroneBaseAll || Phase==EPhase::DronePlayerAll) {
            if (!Drone.IsValid() || Drone->IsFlightArmed() || PC->GetViewTarget()!=Drone.Get() || PC->GetPawn()!=Pioneer.Get() ||
                !Pioneer->GetActorLocation().Equals(OriginalLocation,.5) || Pioneer->GetCharacterMovement()->MovementMode!=MOVE_None) {
                Finish(false,TEXT("Disarmed drone/pioneer ownership changed during native counter testing."));return false;
            }
            LastReason=TEXT("Waiting for natural session Tick to renew its own input contributions over newer foreign holds.");
            if (!TwoFrames(Now) || !Owned(1,1)) return true;
            Record(TEXT("drone-renewed"));bDroneRenewed=true;
            if (Phase==EPhase::DroneAll) Reset(EFactoryFPVInputResetAxes::Move,EPhase::DroneMove,Now);
            else if (Phase==EPhase::DroneMove) Reset(EFactoryFPVInputResetAxes::Look,EPhase::DroneLook,Now);
            else if (Phase==EPhase::DroneLook) Reset(EFactoryFPVInputResetAxes::Move,EPhase::DroneBaseMove,Now,EResetEntry::Controller);
            else if (Phase==EPhase::DroneBaseMove) Reset(EFactoryFPVInputResetAxes::Both,EPhase::DroneBaseAll,Now,EResetEntry::Controller);
            else if (Phase==EPhase::DroneBaseAll) Reset(EFactoryFPVInputResetAxes::Both,EPhase::DronePlayerAll,Now,EResetEntry::PlayerController);
            else { Drone->ExitDrone();SetPhase(EPhase::DroneReturn,Now); }
        } else if (Phase==EPhase::DroneReturn && TwoFrames(Now)) {
            if (!VerifyForeignAndRelease(TEXT("drone-return-foreign-retained"))) return false;
            bDroneForeignSurvived=bDroneBalanced=true;SetPhase(EPhase::PanelImmediate,Now);
        } else if (Phase==EPhase::PanelImmediate && TwoFrames(Now)) {
            if (!Restored()) { LastReason=TEXT("Waiting for full ordinary drone return before panel tests.");return true; }
            Hangar->OpenPanel();
            if (!Hangar->IsPanelOpen() || !Owned(1,1)) { Finish(false,TEXT("Actual hangar failed to acquire its own native input lease."));return false; }
            if (!Reset(EFactoryFPVInputResetAxes::Both,EPhase::PanelImmediate,Now)) return false;
            Hangar->ClosePanel();
            if (!VerifyForeignAndRelease(TEXT("immediate-panel-close-foreign-retained"))) return false;
            bPanelImmediateVerified=true;
            Hangar->OpenPanel();
            if (!Hangar->IsPanelOpen() || !Owned(1,1)) { Finish(false,TEXT("Repeated actual hangar entry failed."));return false; }
            Reset(EFactoryFPVInputResetAxes::Both,EPhase::PanelRenew,Now);
        } else if (Phase==EPhase::PanelRenew) {
            LastReason=TEXT("Waiting for the actual local-player subsystem Tick to renew the open panel lease.");
            if (!TwoFrames(Now) || !Owned(1,1)) return true;
            Hangar->TogglePanel();
            if (Hangar->IsPanelOpen()) { Finish(false,TEXT("TogglePanel failed to close the renewed hangar."));return false; }
            if (!VerifyForeignAndRelease(TEXT("renewed-panel-close-foreign-retained"))) return false;
            bPanelRenewVerified=true;SetPhase(EPhase::Restore,Now);
        } else if (Phase==EPhase::Restore) {
            LastReason=TEXT("Waiting for final original pioneer/view/HUD/input membership restoration.");
            if (TwoFrames(Now) && Restored()) { bRestored=true;Record(TEXT("final-restored"));Finish(true,TEXT("Registered launch refused an open hangar, and the native console alone launched disarmed. Native reset hooks preserved newer foreign holds across drone return and hangar close. Original pioneer, view, HUD and input restored.")); }
        }
        return !bDone;
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingInputQA::Begin(UWorld* World,FOutputDevice& Output) {
    using namespace FactoryFPVShippingInputQAInternal;
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone) {
        Marker(TEXT("FAIL"),TEXT("Explicit automation and real disposable Saved/save directories in a standalone clone are required."));
        Output.Log(TEXT("Input QA isolation guard refused."));if(FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Input QA is already active."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Deadline=FPlatformTime::Seconds()+30.;Active=Run;
    Marker(TEXT("PENDING"),TEXT("Waiting for exact cloned save and observed native startup lifecycle; native reset notifications will not be injected."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}),.1f);
}
