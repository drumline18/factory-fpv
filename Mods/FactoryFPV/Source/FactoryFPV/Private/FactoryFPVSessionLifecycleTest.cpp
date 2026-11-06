#include "FactoryFPVDrone.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVInputLeaseTestController.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FactoryFPVSettings.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Widgets/SOverlay.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"

namespace {
template<class T> int32 LiveSessionActors(UWorld* World) {
    int32 Count=0;
    for (TActorIterator<T> It(World);It;++It) if (!It->IsActorBeingDestroyed()) ++Count;
    return Count;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVSessionLifecycleTest,
    "FactoryFPV.Session.RealSessionCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVSessionLifecycleTest::RunTest(const FString&) {
    if (!TestTrue(TEXT("Session lifecycle fixture has native Slate activation events"),FSlateApplication::IsInitialized())) return false;
    auto& Slate=FSlateApplication::Get();
    auto& ActivationEvent=Slate.OnApplicationActivationStateChanged();
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,
        ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Session lifecycle world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    // Most numerical fixtures never begin actor play, so Destroy cannot route
    // EndPlay in them. Initialize actual engine actor lifecycle here. No game
    // mode is installed: SetBegunPlay takes the role normally held by GameState.
    World->InitializeActorsForPlay(FURL());
    World->SetBegunPlay(true);

    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    auto ViewportOverlay=SNew(SOverlay);
    Viewport->SetViewportOverlayWidget(nullptr,ViewportOverlay);
    GEngine->GetWorldContextFromWorld(World)->GameViewport=Viewport;
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);
    LocalPlayer->ViewportClient=Viewport;
    auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
    if (!TestNotNull(TEXT("Local controller"),PC)) return false;
    PC->SetPlayer(LocalPlayer);
    if (!PC->PlayerCameraManager) {
        PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
        PC->PlayerCameraManager->InitializeFor(PC);
    }
    if (!TestNotNull(TEXT("Actual camera manager"),PC->PlayerCameraManager.Get())) return false;
    TestTrue(TEXT("Session fixture uses a local standalone player"),
        PC->IsLocalController() && PC->GetLocalPlayer()==LocalPlayer && World->GetNetMode()==NM_Standalone);
    TestEqual(TEXT("Session fixture uses the world's actual viewport"),World->GetGameViewport(),Viewport);

    auto* Floor=World->SpawnActor<AActor>();
    auto* FloorBody=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBody);
    Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(10000,10000,50));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);
    FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBody->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,-50));

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Character=World->SpawnActor<ACharacter>(FVector(0,0,89),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Grounded pioneer fixture"),Character)) return false;
    Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    PC->SetPawn(Character);
    PC->SetViewTarget(Character);

    // The SDK's game health query is a generated stub. A real engine Character
    // exercises StartSession/hold/input/camera teardown without pretending to
    // validate shipping damage, death or Satisfactory respawn implementation.
    // Its empty mesh deliberately exercises safe pilot-presentation fallback;
    // the separate PilotPresentation test covers successful skeleton restoration.
    AddInfo(TEXT("Uses real engine StartSession and actor EndPlay; game health/death and successful pilot rendering remain separate acceptance checks."));
    AddInfo(TEXT("The private controller adapter wraps actual engine resets because Editor native hooks are disabled; Shipping separately qualifies real native reset-hook wiring."));
    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const bool PreviousSandbox=Settings->bAllowSandboxAccess;
    const float PreviousVolume=Settings->PropVolume;
    const float PreviousFov=Settings->CameraFov;
    const float PreviousFrame=Settings->FrameSizeCm;
    ON_SCOPE_EXIT {
        Settings->bAllowSandboxAccess=PreviousSandbox;
        Settings->PropVolume=PreviousVolume;
        Settings->CameraFov=PreviousFov;
        Settings->FrameSizeCm=PreviousFrame;
    };
    Settings->bAllowSandboxAccess=true;
    Settings->PropVolume=0;
    Settings->CameraFov=100;
    Settings->FrameSizeCm=24;

    TArray<TWeakObjectPtr<UInputComponent>> PreviousFlightControls;
    auto Begin=[&]() -> AFactoryFPVDrone* {
        auto* Pawn=Cast<ACharacter>(PC->GetPawn());
        if (!TestNotNull(TEXT("Session starts with its current character"),Pawn)) return nullptr;
        Pawn->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        PC->SetViewTarget(Pawn);
        PC->PlayerCameraManager->SetFOV(87);
        auto* Drone=World->SpawnActor<AFactoryFPVDrone>(Pawn->GetActorLocation()+FVector(150,0,100),
            FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("Session drone"),Drone)) return nullptr;
        FWorldPartitionStreamingSource FlightSource;
        TestFalse(TEXT("A spawned drone advertises no streaming source before StartSession"),
            Drone->Streaming->GetStreamingSource(FlightSource));
        FString Error;
        if (!TestTrue(TEXT("Real StartSession succeeds on supported ground"),Drone->StartSession(PC,TEXT("keyboard"),Error))) {
            AddError(Error);
            Drone->Destroy();
            return nullptr;
        }
        TestTrue(TEXT("Fixture actor entered actual BeginPlay before session teardown"),Drone->HasActorBegunPlay());
        TestTrue(TEXT("StartSession establishes an active session"),Drone->bSessionActive);
        TestTrue(TEXT("An active flight advertises streaming at the drone"),
            Drone->Streaming->GetStreamingSource(FlightSource) && FlightSource.Location.Equals(Drone->GetActorLocation(),.01));
        TestTrue(TEXT("StartSession binds its native activation listener"),
            Drone->ApplicationActivationHandle.IsValid() && ActivationEvent.IsBoundToObject(Drone));
        TestTrue(TEXT("StartSession freezes only pioneer movement"),
            Drone->PioneerAnchor && Drone->PioneerAnchor->IsHolding() && Pawn->GetCharacterMovement()->MovementMode==MOVE_None);
        TestTrue(TEXT("StartSession installs controller move/look holds"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
        for (const auto& Prior:PreviousFlightControls) TestFalse(TEXT("A new session does not retain prior flight controls"),
            PC->IsInputComponentInStack(Prior.Get()));
        TestTrue(TEXT("StartSession pushes its actual controls"),PC->IsInputComponentInStack(Drone->Controls.Get()));
        PreviousFlightControls.Add(Drone->Controls.Get());
        TestEqual(TEXT("StartSession creates one flight overlay"),ViewportOverlay->GetChildren()->Num(),1);
        TestEqual(TEXT("StartSession switches the real camera target"),PC->GetViewTarget(),static_cast<AActor*>(Drone));
        TestEqual(TEXT("StartSession applies session camera FOV"),PC->PlayerCameraManager->GetLockedFOV(),100.f);
        TestEqual(TEXT("There is exactly one live session drone"),LiveSessionActors<AFactoryFPVDrone>(World),1);
        TestEqual(TEXT("There is exactly one independent pioneer hold"),LiveSessionActors<AFactoryFPVPioneerAnchor>(World),1);
        return Drone;
    };
    auto CheckReleased=[&](AFactoryFPVDrone* Drone,UInputComponent* FlightControls,ACharacter* HeldPawn,
        AActor* ExpectedView,bool ExternalInputHold=false) {
        TestFalse(TEXT("Session is inactive after actual lifecycle return"),Drone->bSessionActive);
        TestFalse(TEXT("Returned session removes its native activation listener"),
            Drone->ApplicationActivationHandle.IsValid() || ActivationEvent.IsBoundToObject(Drone));
        TestFalse(TEXT("Returned session releases overlay references"),Drone->Overlay.IsValid() || Drone->OverlayRoot.IsValid());
        TestEqual(TEXT("Returned session removes its actual viewport overlay slot"),ViewportOverlay->GetChildren()->Num(),0);
        TestFalse(TEXT("Returned session releases drone streaming"),Drone->Streaming->IsStreamingSourceEnabled());
        FWorldPartitionStreamingSource FlightSource;
        TestFalse(TEXT("Returned drone advertises no streaming source"),Drone->Streaming->GetStreamingSource(FlightSource));
        TestFalse(TEXT("Returned session pops its controls from the real input stack"),PC->IsInputComponentInStack(FlightControls));
        for (const auto& Prior:PreviousFlightControls) TestFalse(TEXT("Repeated sessions leave no usable prior flight controls in the stack"),
            PC->IsInputComponentInStack(Prior.Get()));
        TestEqual(TEXT("Return balances only its own move-input hold"),PC->IsMoveInputIgnored(),ExternalInputHold);
        TestEqual(TEXT("Return balances only its own look-input hold"),PC->IsLookInputIgnored(),ExternalInputHold);
        if (HeldPawn && IsValid(HeldPawn)) TestEqual(TEXT("Supported pioneer resumes walking"),
            HeldPawn->GetCharacterMovement()->MovementMode,static_cast<TEnumAsByte<EMovementMode>>(MOVE_Walking));
        if (ExpectedView) TestEqual(TEXT("Lifecycle return preserves the correct current camera"),PC->GetViewTarget(),ExpectedView);
        TestEqual(TEXT("Return restores the pre-session FOV lock"),PC->PlayerCameraManager->GetLockedFOV(),87.f);
        TestEqual(TEXT("Return leaves no live pioneer hold behind on ready ground"),LiveSessionActors<AFactoryFPVPioneerAnchor>(World),0);
        TestEqual(TEXT("Return leaves no live pilot visual actor"),LiveSessionActors<AFactoryFPVPilotVisual>(World),0);
        TestEqual(TEXT("Return leaves no live session drone"),LiveSessionActors<AFactoryFPVDrone>(World),0);
        TestNull(TEXT("Active-drone discovery is clear for the next launch"),AFactoryFPVDrone::FindActive(World));
    };

    // An independent input hold must survive every launch/return and be
    // removable with exactly one matching call after repeated sessions.
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    for (int32 Index=0;Index<3;++Index) {
        auto* Drone=Begin();
        if (!Drone) return false;
        auto* FlightControls=Drone->Controls.Get();
        Drone->Tick(1.f/60.f);
        Drone->ExitDrone();
        CheckReleased(Drone,FlightControls,Character,Character,true);
    }
    PC->SetIgnoreMoveInput(false);
    PC->SetIgnoreLookInput(false);
    TestFalse(TEXT("Repeated return does not leak move-input counter increments"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Repeated return does not leak look-input counter increments"),PC->IsLookInputIgnored());

    {
        // Exercise the same Slate entry point as WM_ACTIVATEAPP, without
        // changing OS focus or running a world/drone Tick between transitions.
        const bool bWasActive=Slate.IsActive();
        ON_SCOPE_EXIT { if (Slate.IsActive()!=bWasActive) Slate.OnApplicationActivationChanged(bWasActive); };
        Slate.OnApplicationActivationChanged(true);
        auto* FocusSession=Begin();
        if (!FocusSession) return false;
        auto* FocusControls=FocusSession->Controls.Get();
        const factoryfpv::Input Idle{0,0,0,0,true};
        if (!TestTrue(TEXT("Focus-loss fixture starts armed"),FocusSession->TryArmFlight(Idle))) return false;
        FocusSession->Flight.step({0,0,0,.7,true});
        TestTrue(TEXT("Focus-loss fixture has actual spooled thrust"),FocusSession->Flight.thrust>0.);
        FocusSession->KeyboardThrottle=.7;
        FocusSession->Clock.advance(factoryfpv::StepSeconds*.5,[](double){});
        auto& RecoverGate=FocusSession->ActionGates[static_cast<int>(factoryfpv::RadioAction::Recover)];
        FocusSession->ArmGate.update(factoryfpv::ArmPosition::Off);
        RecoverGate.update(factoryfpv::ArmPosition::Off);

        Slate.OnApplicationActivationChanged(false);
        TestTrue(TEXT("Native deactivation blocks flight input before the next Tick"),
            !Slate.IsActive() && FocusSession->bApplicationDeactivated && FocusSession->IsFlightInputBlocked());
        TestFalse(TEXT("Native deactivation cuts armed state immediately"),FocusSession->IsFlightArmed());
        TestEqual(TEXT("Native deactivation cuts spooled thrust immediately"),FocusSession->Flight.thrust,0.);
        TestEqual(TEXT("Native deactivation clears retained keyboard throttle"),FocusSession->KeyboardThrottle,0.);
        FocusSession->ArmToggle();
        TestFalse(TEXT("F8 cannot rearm during native deactivation"),FocusSession->IsFlightArmed());

        Slate.OnApplicationActivationChanged(true);
        TestTrue(TEXT("Regain releases the activation block without rearming or ending the session"),
            Slate.IsActive() && !FocusSession->bApplicationDeactivated && !FocusSession->IsFlightArmed() && FocusSession->bSessionActive);
        TestEqual(TEXT("Focus loss discarded the pending physics half-step"),
            FocusSession->Clock.advance(factoryfpv::StepSeconds*.5,[](double){}),0);
        TestFalse(TEXT("Arm ON after regain cannot reuse an OFF sample from before loss"),
            FocusSession->ArmGate.update(factoryfpv::ArmPosition::On).arm);
        TestFalse(TEXT("Recovery ON after regain cannot reuse an OFF sample from before loss"),
            RecoverGate.update(factoryfpv::ArmPosition::On));
        FocusSession->ArmGate.update(factoryfpv::ArmPosition::Off);
        TestTrue(TEXT("A fresh OFF-to-ON edge can request arming after regain"),
            FocusSession->ArmGate.update(factoryfpv::ArmPosition::On).arm);
        FocusSession->ExitDrone();
        CheckReleased(FocusSession,FocusControls,Character,Character);
    }

    {
        // Use real engine mapper notifications with freshly allocated test
        // identities. The host may have no physical pad attached.
        auto& Mapper=IPlatformInputDeviceMapper::Get();
        auto& ConnectionEvent=Mapper.GetOnInputDeviceConnectionChange();
        const FPlatformUserId OriginalUser=LocalPlayer->GetPlatformUserId();
        const FPlatformUserId FixtureUser=Mapper.AllocateNewUserId();
        const FPlatformUserId OtherUser=Mapper.AllocateNewUserId();
        const FInputDeviceId Device=Mapper.AllocateNewInputDeviceId();
        const FInputDeviceId OtherDevice=Mapper.AllocateNewInputDeviceId();
        const FPlatformUserId Unpaired=Mapper.GetUserForUnpairedInputDevices();
        TWeakObjectPtr<AFactoryFPVDrone> Session;
        ON_SCOPE_EXIT {
            if (auto* Drone=Session.Get()) Drone->ExitDrone();
            Mapper.Internal_MapInputDeviceToUser(Device,Unpaired,EInputDeviceConnectionState::Disconnected);
            Mapper.Internal_MapInputDeviceToUser(OtherDevice,Unpaired,EInputDeviceConnectionState::Disconnected);
            LocalPlayer->SetPlatformUserId(OriginalUser);
        };
        LocalPlayer->SetPlatformUserId(FixtureUser);
        TestFalse(TEXT("A local player with no connected pad has no launch device"),AFactoryFPVDrone::FindLocalGamepad(PC).IsValid());
        if (!TestTrue(TEXT("Mapper connects the synthetic local pad"),
            Mapper.Internal_MapInputDeviceToUser(Device,FixtureUser,EInputDeviceConnectionState::Connected))) return false;
        if (!TestTrue(TEXT("Mapper connects the synthetic second pad"),
            Mapper.Internal_MapInputDeviceToUser(OtherDevice,FixtureUser,EInputDeviceConnectionState::Connected))) return false;
        TestFalse(TEXT("Two connected local pads make the aggregated axes ambiguous"),AFactoryFPVDrone::FindLocalGamepad(PC).IsValid());
        Mapper.Internal_MapInputDeviceToUser(OtherDevice,OtherUser,EInputDeviceConnectionState::Connected);
        TestTrue(TEXT("A connected pad owned by another player cannot replace the local pad"),AFactoryFPVDrone::FindLocalGamepad(PC)==Device);

        auto* PadSession=Begin();
        if (!PadSession) return false;
        Session=PadSession;
        auto* PadControls=PadSession->Controls.Get();
        PadSession->InputMode=TEXT("gamepad");
        PadSession->StartGamepadMonitoring(Device);
        TestTrue(TEXT("The active pad session owns its mapper callback"),ConnectionEvent.IsBoundToObject(PadSession));
        const factoryfpv::Input Idle{0,0,0,0,true};
        if (!TestTrue(TEXT("Pad-loss fixture starts armed"),PadSession->TryArmFlight(Idle))) return false;
        PadSession->Flight.step({0,0,0,.7,true});
        TestTrue(TEXT("Pad-loss fixture has actual spooled thrust"),PadSession->Flight.thrust>0.);

        Mapper.Internal_MapInputDeviceToUser(OtherDevice,Unpaired,EInputDeviceConnectionState::Disconnected);
        TestTrue(TEXT("Losing another player's pad leaves the session armed"),PadSession->IsFlightArmed() && !PadSession->bGamepadLost);
        // XInput reports disconnect after remapping the old device to an
        // unpaired user. A listener must recognize the retained device ID.
        Mapper.Internal_MapInputDeviceToUser(Device,Unpaired,EInputDeviceConnectionState::Disconnected);
        TestTrue(TEXT("Losing the selected pad latches loss and cuts thrust immediately"),
            PadSession->bGamepadLost && !PadSession->IsFlightArmed() && PadSession->Flight.thrust==0.);
        TestFalse(TEXT("Disconnected pad input is invalid"),PadSession->ReadControls(0).valid());

        Mapper.Internal_MapInputDeviceToUser(Device,FixtureUser,EInputDeviceConnectionState::Connected);
        TestTrue(TEXT("Mapper reports the same pad reconnected"),Mapper.GetInputDeviceConnectionState(Device)==EInputDeviceConnectionState::Connected);
        TestTrue(TEXT("Reconnecting never clears the active session loss latch"),PadSession->bGamepadLost && !PadSession->ReadControls(0).valid());
        TestFalse(TEXT("Reconnected pad input cannot arm through the flight gate"),PadSession->TryArmFlight(PadSession->ReadControls(0)));
        PadSession->ArmToggle();
        TestFalse(TEXT("F8 cannot reuse stale inputs after a pad reconnect"),PadSession->IsFlightArmed());

        PadSession->ExitDrone();
        TestFalse(TEXT("Returning from the pad session removes its mapper callback"),ConnectionEvent.IsBoundToObject(PadSession));
        CheckReleased(PadSession,PadControls,Character,Character);
    }

    // Exercise the real engine controller counter reset seen from native pause
    // UI in Shipping. This fixture proves reacquisition and balanced teardown;
    // actual Satisfactory menu opening/closing is qualified in the clone.
    auto* ResetHolds=Begin();
    if (!ResetHolds) return false;
    auto* ResetControls=ResetHolds->Controls.Get();
    PC->ResetIgnoreInputFlags();
    TestFalse(TEXT("Real engine reset removes the active session's move hold"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Real engine reset removes the active session's look hold"),PC->IsLookInputIgnored());
    ResetHolds->Tick(1.f/60.f);
    TestTrue(TEXT("Owned session Tick reacquires a missing move hold"),PC->IsMoveInputIgnored());
    TestTrue(TEXT("Owned session Tick reacquires a missing look hold"),PC->IsLookInputIgnored());
    TestTrue(TEXT("Reacquisition retains the same active session and actual input component"),
        ResetHolds->bSessionActive && PC->GetViewTarget()==ResetHolds && PC->IsInputComponentInStack(ResetControls));
    ResetHolds->Tick(1.f/60.f);
    ResetHolds->ExitDrone();
    CheckReleased(ResetHolds,ResetControls,Character,Character);

    auto* PartialReset=Begin();
    if (!PartialReset) return false;
    auto* PartialControls=PartialReset->Controls.Get();
    PC->ResetIgnoreMoveInput();
    TestFalse(TEXT("Move-only reset leaves only the move hold missing"),PC->IsMoveInputIgnored());
    TestTrue(TEXT("Move-only reset retains the existing look hold"),PC->IsLookInputIgnored());
    PartialReset->Tick(1.f/60.f);
    TestTrue(TEXT("Move-only reset is repaired without losing look ownership"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    PC->ResetIgnoreLookInput();
    TestTrue(TEXT("Look-only reset retains the existing move hold"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Look-only reset leaves only the look hold missing"),PC->IsLookInputIgnored());
    PartialReset->Tick(1.f/60.f);
    TestTrue(TEXT("Look-only reset is repaired without losing move ownership"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    PartialReset->Tick(1.f/60.f);
    PartialReset->ExitDrone();
    CheckReleased(PartialReset,PartialControls,Character,Character);

    // A reset followed by a newer external hold can happen between our ticks.
    // A true boolean then does not mean our original acquisition still exists.
    auto* ReplacedHolds=Begin();
    if (!ReplacedHolds) return false;
    auto* ReplacedControls=ReplacedHolds->Controls.Get();
    PC->ResetIgnoreInputFlags();
    TestFalse(TEXT("Replacement-hold fixture resets the original move acquisition"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Replacement-hold fixture resets the original look acquisition"),PC->IsLookInputIgnored());
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    ReplacedHolds->Tick(1.f/60.f);
    TestTrue(TEXT("Newer independent move/look holds remain active through the owned session Tick"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    ReplacedHolds->ExitDrone();
    CheckReleased(ReplacedHolds,ReplacedControls,Character,Character,true);
    PC->SetIgnoreMoveInput(false);
    PC->SetIgnoreLookInput(false);
    TestFalse(TEXT("One matching newer external release clears move hold after FPV return"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("One matching newer external release clears look hold after FPV return"),PC->IsLookInputIgnored());

    auto* ImmediateExit=Begin();
    if (!ImmediateExit) return false;
    auto* ImmediateControls=ImmediateExit->Controls.Get();
    PC->ResetIgnoreInputFlags();
    TestFalse(TEXT("Immediate-exit fixture resets the original move acquisition"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Immediate-exit fixture resets the original look acquisition"),PC->IsLookInputIgnored());
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    // No Tick can observe the zero state or renew ownership before this exit.
    ImmediateExit->ExitDrone();
    CheckReleased(ImmediateExit,ImmediateControls,Character,Character,true);
    PC->SetIgnoreMoveInput(false);
    PC->SetIgnoreLookInput(false);
    TestFalse(TEXT("One external move release clears the newer hold after immediate FPV exit"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("One external look release clears the newer hold after immediate FPV exit"),PC->IsLookInputIgnored());

    // A real engine console owns presentation/input independently of OS focus.
    // Its native state exercises the shared guard without SDK pause-flag stubs.
    auto* ConsoleSession=Begin();
    if (!ConsoleSession) return false;
    auto* ConsoleControls=ConsoleSession->Controls.Get();
    auto* Console=NewObject<UConsole>(Viewport);
    Viewport->ViewportConsole=Console;
    TestTrue(TEXT("Started session exposes its actual visible OSD"),ConsoleSession->IsFlightOverlayVisible());
    Console->ConsoleState=TEXT("Typing");
    PC->ResetIgnoreInputFlags();
    TestTrue(TEXT("Actual typing console blocks native session presentation/input"),ConsoleSession->IsNativeUIBlockingFlight());
    ConsoleSession->Tick(1.f/60.f);
    TestFalse(TEXT("Typing console collapses the actual FPV OSD"),ConsoleSession->IsFlightOverlayVisible());
    TestEqual(TEXT("Typing console sets the actual OSD visibility to Collapsed"),ConsoleSession->OverlayRoot->GetVisibility().ToString(),EVisibility::Collapsed.ToString());
    TestFalse(TEXT("Native-owned console input does not reacquire a cleared move hold"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Native-owned console input does not reacquire a cleared look hold"),PC->IsLookInputIgnored());
    ConsoleSession->Tick(1.f/60.f);
    TestFalse(TEXT("Repeated native UI ticks keep cleared controller holds untouched"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    FInputActionUnifiedDelegate HangarKey;
    int32 HangarBindings=0;
    for (const auto& Binding:ConsoleControls->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed) {
        HangarKey=Binding.KeyDelegate;++HangarBindings;
    }
    if (!TestEqual(TEXT("Started session has exactly one real F6 press binding"),HangarBindings,1) ||
        !TestTrue(TEXT("Real F6 delegate is bound to this session"),HangarKey.IsBoundToObject(ConsoleSession))) return false;
    const auto* NativeOwnedOverlay=ConsoleSession->OverlayRoot.Get();
    const int32 NativeOwnedOverlayCount=ViewportOverlay->GetChildren()->Num();
    HangarKey.Execute(EKeys::F6);
    TestTrue(TEXT("Bound F6 refuses native UI before ending the same session"),ConsoleSession->bSessionActive && !ConsoleSession->IsActorBeingDestroyed());
    TestEqual(TEXT("Refused bound F6 retains the current FPV view"),PC->GetViewTarget(),static_cast<AActor*>(ConsoleSession));
    TestTrue(TEXT("Refused bound F6 retains its actual input stack membership"),PC->IsInputComponentInStack(ConsoleControls));
    TestTrue(TEXT("Refused bound F6 retains the same OSD object"),ConsoleSession->OverlayRoot.Get()==NativeOwnedOverlay);
    TestEqual(TEXT("Refused bound F6 leaves viewport overlays unchanged"),ViewportOverlay->GetChildren()->Num(),NativeOwnedOverlayCount);
    TestFalse(TEXT("Refused bound F6 leaves native-owned OSD hidden"),ConsoleSession->IsFlightOverlayVisible());
    TestFalse(TEXT("Refused bound F6 does not reacquire native-cleared controller holds"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    Console->ConsoleState=NAME_None;
    TestFalse(TEXT("Closing the actual console releases native UI ownership independently of OS focus"),ConsoleSession->IsNativeUIBlockingFlight());
    ConsoleSession->Tick(1.f/60.f);
    TestTrue(TEXT("Console close restores the actual FPV OSD"),ConsoleSession->IsFlightOverlayVisible());
    TestEqual(TEXT("Restored OSD cannot take pointer input"),ConsoleSession->OverlayRoot->GetVisibility().ToString(),EVisibility::HitTestInvisible.ToString());
    TestTrue(TEXT("Console close reacquires only the missing session holds"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    ConsoleSession->Tick(1.f/60.f);
    HangarKey.Execute(EKeys::F6);
    TestTrue(TEXT("Bound F6 performs ordinary return after native UI releases ownership"),ConsoleSession->IsActorBeingDestroyed());
    if (auto* Hangar=LocalPlayer->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();Hangar && Hangar->IsPanelOpen()) Hangar->ClosePanel();
    else AddInfo(TEXT("Bound F6 return is exercised; this engine-character fixture has no initialized gameplay hangar binding, so actual panel opening remains a Shipping check."));
    CheckReleased(ConsoleSession,ConsoleControls,Character,Character);
    TestFalse(TEXT("Returned session reports no remaining visible OSD"),ConsoleSession->IsFlightOverlayVisible());

    auto* Takeover=Begin();
    if (!Takeover) return false;
    auto* TakeoverControls=Takeover->Controls.Get();
    auto* OtherView=World->SpawnActor<AActor>();
    PC->SetViewTarget(OtherView);
    Takeover->Tick(1.f/60.f);
    TestTrue(TEXT("Actual Tick retires a session when another camera takes over"),Takeover->IsActorBeingDestroyed());
    CheckReleased(Takeover,TakeoverControls,Character,OtherView);

    auto* PawnChange=Begin();
    if (!PawnChange) return false;
    auto* PawnControls=PawnChange->Controls.Get();
    auto* Replacement=World->SpawnActor<ACharacter>(FVector(1000,0,89),FRotator::ZeroRotator,Spawn);
    Replacement->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    // Drive the authoritative controller pawn-change boundary, independent of
    // game-specific respawn code and its generated SDK health implementation.
    PC->SetPawn(Replacement);
    PawnChange->Tick(1.f/60.f);
    TestTrue(TEXT("Actual Tick retires the session after pawn replacement"),PawnChange->IsActorBeingDestroyed());
    CheckReleased(PawnChange,PawnControls,Character,Replacement);

    auto* Removed=Begin();
    if (!Removed) return false;
    auto* RemovedControls=Removed->Controls.Get();
    Removed->Destroy(); // EndPlay, without calling ExitDrone first.
    CheckReleased(Removed,RemovedControls,Replacement,Replacement);

    auto* LostPawn=Begin();
    if (!LostPawn) return false;
    auto* LostPawnControls=LostPawn->Controls.Get();
    Replacement->Destroy();
    PC->SetPawn(nullptr);
    LostPawn->Tick(1.f/60.f);
    TestTrue(TEXT("Actual Tick retires a session when its pioneer is removed"),LostPawn->IsActorBeingDestroyed());
    CheckReleased(LostPawn,LostPawnControls,nullptr,PC);

    PC->SetPawn(Character);
    auto* Travel=Begin();
    if (!Travel) return false;
    auto* TravelControls=Travel->Controls.Get();
    TestTrue(TEXT("Engine level-transition EndPlay routes over a playing fixture"),World->EndPlay(EEndPlayReason::LevelTransition));
    TestFalse(TEXT("Level transition ends the actual active session"),Travel->bSessionActive);
    TestFalse(TEXT("Level transition removes the native activation listener"),
        Travel->ApplicationActivationHandle.IsValid() || ActivationEvent.IsBoundToObject(Travel));
    TestEqual(TEXT("Level transition removes the viewport overlay"),ViewportOverlay->GetChildren()->Num(),0);
    TestFalse(TEXT("Level transition balances move-input hold"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Level transition balances look-input hold"),PC->IsLookInputIgnored());
    TestFalse(TEXT("Level transition releases drone streaming"),Travel->Streaming->IsStreamingSourceEnabled());
    TestFalse(TEXT("Level transition pops drone controls"),PC->IsInputComponentInStack(TravelControls));
    TestEqual(TEXT("Level transition restores the pioneer's movement"),
        Character->GetCharacterMovement()->MovementMode,static_cast<TEnumAsByte<EMovementMode>>(MOVE_Walking));
    return true;
}
#endif
