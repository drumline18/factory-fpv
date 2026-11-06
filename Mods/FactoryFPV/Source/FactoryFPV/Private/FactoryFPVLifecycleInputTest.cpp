#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Widgets/SOverlay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVLifecycleInputTest,
    "FactoryFPV.Session.LateViewportAndInputGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVLifecycleInputTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Lifecycle test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* PC=World->SpawnActor<APlayerController>();
    auto* LocalPlayer=NewObject<ULocalPlayer>(GEngine);
    auto* Hangar=NewObject<UFactoryFPVLocalPlayerSubsystem>(LocalPlayer);
    TestFalse(TEXT("Controller binding waits while viewport is unavailable"),Hangar->BindController(PC,nullptr));
    TestFalse(TEXT("Failed bind does not cache controller and block retry"),Hangar->Controller.IsValid());
    TestNull(TEXT("Failed bind creates no input component"),Hangar->Controls.Get());

    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    auto ViewportOverlay=SNew(SOverlay);
    Viewport->SetViewportOverlayWidget(nullptr,ViewportOverlay);
    TestTrue(TEXT("Same controller binds after viewport becomes available"),Hangar->BindController(PC,Viewport));
    TestTrue(TEXT("Successful retry installs registered controls"),Hangar->Controls && Hangar->Controls->IsRegistered());
    TestFalse(TEXT("Already bound controller cannot double-push controls"),Hangar->BindController(PC,Viewport));
    Hangar->UnbindController();
    TestFalse(TEXT("Unbind releases controller"),Hangar->Controller.IsValid());
    TestNull(TEXT("Unbind releases input component"),Hangar->Controls.Get());

    auto* Drone=World->SpawnActor<AFactoryFPVDrone>();
    Drone->InputMode=TEXT("keyboard");
    Drone->Pilot=PC;
    Drone->PilotViewport=Viewport;
    Viewport->ViewportConsole=NewObject<UConsole>(Viewport);
    Viewport->ViewportConsole->ConsoleState=TEXT("Typing");
    TestTrue(TEXT("Active console blocks flight input"),Drone->IsFlightInputBlocked());
    Drone->ArmToggle();
    TestFalse(TEXT("Arm callback cannot arm while console owns interaction"),Drone->Flight.armed);
    Drone->Flight.arm({0,0,0,0,true});
    Drone->ArmToggle();
    TestFalse(TEXT("Emergency disarm remains available while input is blocked"),Drone->Flight.armed);
    Drone->ArmedSeconds=4.;
    Drone->KeyboardThrottle=.4;
    Drone->RecoveryWarning=TEXT("Existing recovery guidance");
    const FTransform BeforeRecovery=Drone->GetActorTransform();
    Drone->RecoverFromKey();
    TestTrue(TEXT("F9 cannot move the drone or clear flight and recovery state while the console owns input"),
        Drone->GetActorTransform().Equals(BeforeRecovery) && Drone->ArmedSeconds==4. && Drone->KeyboardThrottle==.4 &&
        Drone->RecoveryWarning==TEXT("Existing recovery guidance"));
    Drone->Pilot.Reset();
    TestTrue(TEXT("Missing controller blocks flight input"),Drone->IsFlightInputBlocked());

    // Exercise real PlayerController/CameraManager view restoration, including
    // a pawn change while the old original view actor remains valid.
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);
    auto* OriginalPawn=World->SpawnActor<APawn>();
    auto* NewPawn=World->SpawnActor<APawn>();
    auto* OtherView=World->SpawnActor<AActor>();
    PC->SetPawn(NewPawn);
    Drone->Pilot=PC;
    Drone->OriginalPawn=OriginalPawn;
    Drone->OriginalView=OriginalPawn;
    Drone->bSessionActive=true;
    PC->SetViewTarget(Drone);
    Drone->RestorePilot();
    TestEqual(TEXT("Return follows current pawn after possession changes"),PC->GetViewTarget(),static_cast<AActor*>(NewPawn));
    Drone->bSessionActive=true;
    PC->SetViewTarget(OtherView);
    Drone->RestorePilot();
    TestEqual(TEXT("Return preserves a newer external view target"),PC->GetViewTarget(),OtherView);
    PC->SetPawn(nullptr);
    Drone->bSessionActive=true;
    PC->SetViewTarget(Drone);
    Drone->RestorePilot();
    TestEqual(TEXT("Unpossessed return does not reuse the old pawn view"),PC->GetViewTarget(),static_cast<AActor*>(PC));
    PC->SetPawn(OriginalPawn);
    Drone->OriginalView=OtherView;
    Drone->bSessionActive=true;
    PC->SetViewTarget(Drone);
    Drone->RestorePilot();
    TestEqual(TEXT("Unchanged possession restores the prior camera"),PC->GetViewTarget(),OtherView);

    // Reach the authoritative progression gate in StartSession, rather than
    // accidentally passing because no viewport/local player/pawn exists.
    auto* Context=GEngine->GetWorldContextFromWorld(World);
    Context->GameViewport=Viewport;
    TestEqual(TEXT("Progression fixture has the world's actual viewport"),World->GetGameViewport(),Viewport);
    TestTrue(TEXT("Progression fixture has a local standalone controller"),PC->IsLocalController() && World->GetNetMode()==NM_Standalone);
    TestNotNull(TEXT("Progression fixture has a possessed pawn"),PC->GetPawn().Get());
    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const bool PreviousSandboxAccess=Settings->bAllowSandboxAccess;
    ON_SCOPE_EXIT { Settings->bAllowSandboxAccess=PreviousSandboxAccess; };
    Settings->bAllowSandboxAccess=false;
    FString LaunchError;
    const bool MoveIgnoredBefore=PC->IsMoveInputIgnored();
    const bool LookIgnoredBefore=PC->IsLookInputIgnored();
    TestFalse(TEXT("No-kit launch is rejected when sandbox flights are disabled"),Drone->StartSession(PC,TEXT("keyboard"),LaunchError));
    TestEqual(TEXT("Launch reaches and reports the equipment requirement"),LaunchError,FString(TEXT("Equip an FPV Radio Kit, or enable sandbox flights in the hangar.")));
    TestFalse(TEXT("Rejected progression launch does not create an active session"),Drone->bSessionActive);
    TestEqual(TEXT("Rejected progression launch preserves move input"),PC->IsMoveInputIgnored(),MoveIgnoredBefore);
    TestEqual(TEXT("Rejected progression launch preserves look input"),PC->IsLookInputIgnored(),LookIgnoredBefore);
    TestEqual(TEXT("Rejected progression launch preserves the current camera"),PC->GetViewTarget(),OtherView);
    return true;
}
#endif
