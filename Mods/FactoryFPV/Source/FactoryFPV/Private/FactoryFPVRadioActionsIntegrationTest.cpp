#include "FactoryFPVDrone.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRadioActionsIntegrationTest,"FactoryFPV.Session.RadioActions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRadioActionsIntegrationTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Radio action integration world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };

    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const auto SavedProfile=Settings->CurrentProfile();
    const auto SavedProfiles=Settings->RadioProfiles;
    const bool SavedSandboxAccess=Settings->bAllowSandboxAccess;
    const bool SavedAngleMode=Settings->bAngleMode;
    ON_SCOPE_EXIT {
        Settings->SetProfile(SavedProfile);Settings->RadioProfiles=SavedProfiles;
        Settings->bAllowSandboxAccess=SavedSandboxAccess;Settings->bAngleMode=SavedAngleMode;
    };
    FFactoryFPVRadioProfile Profile;
    Profile.DeviceId=TEXT("runtime-actions-fixture");
    Profile.Roll={0,FVector(0,32767,65535),false};
    Profile.Pitch={1,FVector(0,32767,65535),false};
    Profile.Yaw={3,FVector(0,32767,65535),false};
    Profile.Throttle={2,FVector(0,32767,65535),false};
    Profile.Arm.Kind=1;Profile.Arm.Channel=7;
    Profile.ReturnControl.Kind=1;Profile.ReturnControl.Channel=8;
    Profile.RecoverControl.Kind=2;Profile.RecoverControl.Channel=4;Profile.RecoverControl.On=65535;
    Profile.ModeControl.Kind=1;Profile.ModeControl.Channel=9;
    Settings->SetProfile(Profile);Settings->bAllowSandboxAccess=true;Settings->bAngleMode=false;

    auto* Drone=World->SpawnActor<AFactoryFPVDrone>();
    Drone->InputMode=TEXT("radio");
    Drone->LaunchPosition=FVector(0,0,1000);
    Drone->SetActorLocation(FVector(300,0,1000));
    Drone->Flight.reset({3,0,10});
    Drone->Flight.parameters.angleMode=false;

    auto* Floor=World->SpawnActor<AActor>();
    auto* FloorBody=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBody);
    Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(100,100,50));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);
    FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBody->RegisterComponent();
    Floor->SetActorLocation(Drone->LaunchPosition-FVector(0,0,64)); // 12 cm sphere plus 2 cm clearance.

    std::array<long,8> Axes{};
    std::array<bool,128> Buttons{};
    std::array<bool,128> ButtonPresence{};
    ButtonPresence[7]=ButtonPresence[8]=ButtonPresence[9]=true;
    std::array<factoryfpv::RadioAxisInfo,8> AxisInfo{};
    for (auto& Info:AxisInfo) Info={true,0,65535};
    FString BindingError;
    TestTrue(TEXT("Exposed saved radio controls allow launch"),Settings->AreRadioBindingControlsAvailable(AxisInfo,ButtonPresence,BindingError));
    ButtonPresence[7]=false;
    TestFalse(TEXT("A missing saved arm button refuses radio launch"),Settings->AreRadioBindingControlsAvailable(AxisInfo,ButtonPresence,BindingError));
    TestTrue(TEXT("Missing arm button guidance identifies the action and how to fix it"),BindingError.Contains(TEXT("Arm uses")) && BindingError.Contains(TEXT("Radio tab")));
    Settings->RadioArm.Off=1;Settings->RadioArm.On=0;
    Drone->UpdateRadioControlPositions(Axes,Buttons,AxisInfo,ButtonPresence);
    TestTrue(TEXT("An absent active-low arm button reads as lost rather than ON"),Drone->ArmPosition==factoryfpv::ArmPosition::Lost);
    Settings->RadioArm=Profile.Arm;ButtonPresence[7]=true;
    AxisInfo[4].present=false;
    TestFalse(TEXT("A missing configured recovery axis refuses radio launch"),Settings->AreRadioBindingControlsAvailable(AxisInfo,ButtonPresence,BindingError));
    TestTrue(TEXT("Missing optional axis guidance identifies the configured action"),BindingError.Contains(TEXT("Recover drone uses")));
    Settings->RadioRecover.Kind=0;
    TestTrue(TEXT("Clearing the unavailable optional binding makes the remaining controls usable"),
        Settings->AreRadioBindingControlsAvailable(AxisInfo,ButtonPresence,BindingError) && BindingError.IsEmpty());
    Settings->RadioRecover=Profile.RecoverControl;AxisInfo[4].present=true;
    const factoryfpv::Input Input{0,0,0,0,true};
    auto Poll=[&](AFactoryFPVDrone* Target) {
        Target->UpdateRadioControlPositions(Axes,Buttons,AxisInfo,ButtonPresence);
        return Target->ProcessRadioActions(Input);
    };
    Buttons[7]=Buttons[8]=Buttons[9]=true;Axes[4]=65535;
    if (!TestFalse(TEXT("A new flight ignores every action held ON"),Poll(Drone))) return false;
    TestFalse(TEXT("Startup ON does not change mode"),Drone->Flight.parameters.angleMode);
    TestEqual(TEXT("Startup ON does not recover"),Drone->GetActorLocation(),FVector(300,0,1000));
    Buttons={};Axes[4]=0;Poll(Drone);
    Drone->TryArmFlight(Input);
    Drone->Flight.velocity={2,3,4};
    Buttons[9]=true;
    TestFalse(TEXT("Mode toggle continues the flight frame"),Poll(Drone));
    TestTrue(TEXT("Mode switch changes the actual flight parameters"),Drone->Flight.parameters.angleMode);
    TestTrue(TEXT("Mode toggle preserves arm state and momentum"),Drone->Flight.armed && Drone->Flight.velocity.length()>5);
    TestTrue(TEXT("Mode change leaves safety-warning channel available"),Drone->Notice.IsEmpty());
    TestFalse(TEXT("Session toggle does not rewrite starting mode"),Settings->bAngleMode);
    Poll(Drone);
    TestTrue(TEXT("Held mode switch does not repeat toggle"),Drone->Flight.parameters.angleMode);
    Buttons[9]=false;Poll(Drone);Buttons[9]=true;Poll(Drone);
    TestFalse(TEXT("A second release/press returns to acro"),Drone->Flight.parameters.angleMode);

    Buttons={};Axes[4]=0;Poll(Drone);
    Drone->ArmGate.update(factoryfpv::ArmPosition::Off);
    Drone->ProcessRadioActions({0,0,0,0,false});
    Buttons[8]=Buttons[9]=true;Axes[4]=65535;
    if (!TestFalse(TEXT("Reconnect with actions held ON cannot return or recover"),Poll(Drone))) return false;
    TestFalse(TEXT("Reconnect with mode held ON cannot toggle"),Drone->Flight.parameters.angleMode);
    TestFalse(TEXT("Receiver loss resets the arm gate too"),Drone->ArmGate.update(factoryfpv::ArmPosition::On).arm);
    Buttons={};Axes[4]=0;Poll(Drone);
    Drone->ResetRadioGates();
    Buttons[8]=Buttons[9]=true;Axes[4]=65535;
    if (!TestFalse(TEXT("Pause/focus/menu reset ignores held actions on resuming"),Poll(Drone))) return false;
    TestFalse(TEXT("Pause/focus/menu reset cannot toggle mode on resuming"),Drone->Flight.parameters.angleMode);

    Buttons={};Axes[4]=0;Poll(Drone);
    AxisInfo[4].present=false;Axes[4]=65535;
    TestFalse(TEXT("Missing auxiliary HID axis cannot request recovery"),Poll(Drone));
    AxisInfo[4].present=true;
    TestFalse(TEXT("Reappearing axis held ON cannot request recovery"),Poll(Drone));
    TestEqual(TEXT("Missing-axis recovery preserves flight position"),Drone->GetActorLocation(),FVector(300,0,1000));
    Settings->RadioReturn.Kind=0;
    Buttons={};Axes[4]=0;Poll(Drone);Buttons[8]=true;
    TestFalse(TEXT("Cleared return binding ignores its old button"),Poll(Drone));
    Settings->RadioReturn=Profile.ReturnControl;

    Buttons={};Axes[4]=0;Poll(Drone);
    Drone->ArmGate.update(factoryfpv::ArmPosition::Off);
    Drone->KeyboardThrottle=.8;Drone->ArmedSeconds=20;
    // Recovery wins over simultaneous mode/arm inputs, leaves the drone
    // disarmed, clears all edge gates and ends the current simulation frame.
    Buttons[7]=Buttons[9]=true;Axes[4]=65535;
    TestTrue(TEXT("Radio recovery ends the current flight frame"),Poll(Drone));
    TestEqual(TEXT("Radio recovery uses the actual launch position"),Drone->GetActorLocation(),Drone->LaunchPosition);
    TestFalse(TEXT("Radio recovery never auto-arms"),Drone->Flight.armed);
    TestFalse(TEXT("Recovery wins over simultaneous mode toggle"),Drone->Flight.parameters.angleMode);
    TestEqual(TEXT("Recovery resets armed timer"),Drone->ArmedSeconds,0.);
    TestEqual(TEXT("Recovery clears keyboard throttle"),Drone->KeyboardThrottle,0.);
    TestFalse(TEXT("Recovery consumes an earlier arm release"),Drone->ArmGate.update(factoryfpv::ArmPosition::On).arm);
    Drone->SetActorLocation(FVector(400,0,1000));
    TestFalse(TEXT("Held recovery switch cannot recover repeatedly"),Poll(Drone));
    TestEqual(TEXT("Held recovery keeps current position"),Drone->GetActorLocation(),FVector(400,0,1000));
    TestFalse(TEXT("Mode held through recovery stays acro"),Drone->Flight.parameters.angleMode);

    // Reach the session's authoritative preflight validation before a native
    // radio window/device is needed. No physical controller is used by this test.
    auto* PC=World->SpawnActor<APlayerController>();
    auto* Pawn=World->SpawnActor<APawn>();
    PC->SetPawn(Pawn);
    PC->PlayerCameraManager=World->SpawnActor<APlayerCameraManager>();
    PC->PlayerCameraManager->InitializeFor(PC);PC->SetViewTarget(Pawn);
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    GEngine->GetWorldContextFromWorld(World)->GameViewport=Viewport;
    auto* Rejected=World->SpawnActor<AFactoryFPVDrone>();
    Rejected->ActionGates[0].update(factoryfpv::ArmPosition::Off);
    Settings->RadioReturn=Settings->RadioArm;
    FString Error;
    TestFalse(TEXT("Session start rejects arm/return binding conflict"),Rejected->StartSession(PC,TEXT("radio"),Error));
    TestTrue(TEXT("Preflight reports the actual conflicting actions"),Error.Contains(TEXT("Arm")) && Error.Contains(TEXT("Return to pioneer")));
    TestFalse(TEXT("A new launch resets pending action edges even when preflight fails"),Rejected->ActionGates[0].update(factoryfpv::ArmPosition::On));
    Settings->RadioReturn=Profile.ReturnControl;Settings->RadioMode=Settings->RadioRecover;
    TestFalse(TEXT("Session start rejects recovery/mode binding conflict"),Rejected->StartSession(PC,TEXT("radio"),Error));
    TestTrue(TEXT("Preflight reports recovery/mode conflict"),Error.Contains(TEXT("Recover drone")) && Error.Contains(TEXT("Toggle flight mode")));
    Settings->RadioMode=Profile.ModeControl;Settings->RadioRecover.Channel=0;
    TestFalse(TEXT("Session start rejects auxiliary binding on a flight axis"),Rejected->StartSession(PC,TEXT("radio"),Error));
    TestTrue(TEXT("Preflight reports reused flight axis"),Error.Contains(TEXT("flight axis")));
    Settings->RadioRecover=Profile.RecoverControl;
    TestFalse(TEXT("Rejected preflight leaves session inactive"),Rejected->bSessionActive);
    TestEqual(TEXT("Rejected preflight leaves the current view intact"),PC->GetViewTarget(),static_cast<AActor*>(Pawn));
    TestFalse(TEXT("Rejected preflight leaves walking unlocked"),PC->IsMoveInputIgnored());

    const FVector LossStart(1000,0,1500);
    auto* Loss=World->SpawnActor<AFactoryFPVDrone>(LossStart,FRotator::ZeroRotator);
    if (!TestNotNull(TEXT("Radio loss flight fixture"),Loss)) return false;
    Loss->Pilot=PC;
    Loss->InputMode=TEXT("radio");
    Loss->Flight.reset({LossStart.X/100.,LossStart.Y/100.,LossStart.Z/100.});
    constexpr float FrameSeconds=1.f/60.f;
    const auto AdvanceRadio=[&](const factoryfpv::Input& Frame) {
        Loss->UpdateRadioControlPositions(Axes,Buttons,AxisInfo,ButtonPresence);
        Loss->AdvanceActiveFlightFrame(Frame,FrameSeconds);
    };
    Buttons={};Axes[4]=0;AdvanceRadio(Input);
    Buttons[7]=true;AdvanceRadio(Input);
    AdvanceRadio({0,0,0,.6,true});
    if (!TestTrue(TEXT("A fresh arm edge starts actual thrust and eligible flight segments"),
        Loss->IsFlightArmed() && Loss->Flight.thrust>0. && Loss->RaceScoring.Contains(1))) return false;
    const double ArmedTime=Loss->ArmedSeconds;
    const auto Missing=Loss->ReadControls(FrameSeconds);
    Loss->AdvanceActiveFlightFrame(Missing,FrameSeconds);
    TestTrue(TEXT("The disconnected radio sample immediately clears arm state and thrust in the real flight frame"),
        !Missing.valid() && !Loss->LastInput.valid() && !Loss->IsFlightArmed() && Loss->Flight.thrust==0.);
    TestTrue(TEXT("The disconnected frame cannot score its real movement or advance the armed timer"),
        !Loss->RaceScoring.IsEmpty() && !Loss->RaceScoring.Contains(1) && Loss->ArmedSeconds==ArmedTime);
    Buttons[8]=Buttons[9]=true;Axes[4]=65535;
    const FVector BeforeResume=Loss->GetActorLocation();
    AdvanceRadio(Input);
    TestTrue(TEXT("Valid input returning with held controls cannot arm, return, recover or toggle mode"),
        Loss->LastInput.valid() && !Loss->IsActorBeingDestroyed() && !Loss->IsFlightArmed() && Loss->Flight.thrust==0. &&
        !Loss->Flight.parameters.angleMode && Loss->GetActorLocation().Z<BeforeResume.Z);
    Buttons={};Axes[4]=0;AdvanceRadio(Input);
    Buttons[7]=true;AdvanceRadio(Input);
    TestTrue(TEXT("A fresh OFF to ON arm edge after loss can arm at zero throttle"),Loss->IsFlightArmed());
    Loss->Destroy();

    // Return dispatch calls the same restoration path as F10, and its true
    // result forces Tick to stop immediately after ExitDrone destroys the actor.
    Drone->Pilot=PC;Drone->OriginalPawn=Pawn;Drone->OriginalView=Pawn;
    if (!TestTrue(TEXT("Manual return fixture acquires its actual owned input lease"),Drone->InputLease.Acquire(PC))) return false;
    Drone->bSessionActive=true;
    PC->SetViewTarget(Drone);
    Buttons={};Axes[4]=0;Poll(Drone);
    Buttons[8]=Buttons[9]=true;Axes[4]=65535;
    TestTrue(TEXT("Return action ends the flight frame"),Poll(Drone));
    TestFalse(TEXT("Return releases the active session"),Drone->bSessionActive);
    TestTrue(TEXT("Return destroys the disposable drone"),Drone->IsActorBeingDestroyed());
    TestEqual(TEXT("Return restores the pioneer view"),PC->GetViewTarget(),static_cast<AActor*>(Pawn));
    TestFalse(TEXT("Return restores normal movement"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Return restores normal look"),PC->IsLookInputIgnored());
    TestEqual(TEXT("Return wins over simultaneous recovery"),Drone->GetActorLocation(),FVector(400,0,1000));
    TestFalse(TEXT("Return wins over simultaneous mode toggle"),Drone->Flight.parameters.angleMode);
    return true;
}
#endif
