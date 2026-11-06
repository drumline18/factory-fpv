#include "FactoryFPVDrone.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVPropAudioComponent.h"
#include "FactoryFPVFlightCueAudioComponent.h"
#include "FactoryFPVAudioSettings.h"
#include "FactoryFPVLensComponent.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceScreen.h"
#include "FactoryFPVPilotVisual.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVPioneerAnchor.h"
#include "GameFramework/Character.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVInteractionState.h"
#include "Engine/LocalPlayer.h"
#include "SceneView.h"
#include "UnrealClient.h"
#include "EngineUtils.h"
#include "FGCharacterBase.h"
#include "FGPlayerController.h"
#include "FGHUD.h"
#include "UI/FGGameUI.h"
#include "WindowsRadio.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/InputDeviceSubsystem.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GenericPlatform/GenericWindow.h"
#include "Widgets/SWindow.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Widgets/Layout/SBorder.h"
#include "Styling/CoreStyle.h"
#include "Components/InputComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Math/RotationMatrix.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"

namespace {
FVector ToUE(factoryfpv::Vec V) { return FVector(V.x,V.y,V.z); }
factoryfpv::Vec ToCore(FVector V) { return {V.X,V.Y,V.Z}; }
factoryfpv::Calibration Calibration(FVector Range, bool Reverse = false) {
    return {Range.X, Range.Y, Range.Z, Reverse};
}
std::array<int, 4> RadioChannels(const UFactoryFPVSettings& Settings) {
    return {Settings.RadioRollAxis, Settings.RadioPitchAxis, Settings.RadioYawAxis, Settings.RadioThrottleAxis};
}
bool IsClockwiseRotor(int32 Index) { return Index==0 || Index==3; }
constexpr double GhostHideDistanceCm=250., GhostFullOpacityDistanceCm=400.;
const FName RaceGhostName(TEXT("FactoryFPVRaceGhost"));
}
AFactoryFPVDrone* AFactoryFPVDrone::FindActive(UWorld* World) {
    if (!World) return nullptr;
    for (TActorIterator<AFactoryFPVDrone> It(World); It; ++It) {
        if (!It->IsActorBeingDestroyed()) return *It;
    }
    return nullptr;
}

AFactoryFPVDrone* AFactoryFPVDrone::Launch(APlayerController* PC, FName Mode, FString& Error, int32 PracticeCourse) {
    UWorld* World = PC ? PC->GetWorld() : nullptr;
    if (!World || World->GetNetMode() != NM_Standalone || !PC->IsLocalController()) {
        Error = TEXT("Launch requires a living pioneer in a single-player world.");
        return nullptr;
    }
    // Save & fly releases the panel before launching. A direct command must
    // not take control away from an editor or a native game menu.
    const auto* Player = PC->GetLocalPlayer();
    const auto* Hangar = Player ? Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
    if (Hangar && Hangar->IsPanelOpen()) {
        Error = TEXT("Close the Factory FPV panel or use Save & fly from the hangar.");
        return nullptr;
    }
    if (FactoryFPVInteractionState::BlocksPlayerInteraction(PC)) {
        Error = TEXT("Close the game menu or resume the game before launching FPV.");
        return nullptr;
    }
    // An open console is allowed to issue fpv.start. The flight input guard
    // prevents arming until the console closes.
    const auto* Pioneer = Cast<AFGCharacterBase>(PC->GetPawn());
    if (!Pioneer || !Pioneer->IsAliveAndWell()) {
        Error = TEXT("Launch requires a living pioneer in a single-player world.");
        return nullptr;
    }
    if (const auto* Active=FindActive(World)) {
        Error = FString::Printf(TEXT("A drone is already flying. %s returns to your pioneer."),
            *FFactoryFPVKeyboardShortcuts::KeyLabel(Active->Shortcuts.Return));
        return nullptr;
    }
    if (Mode != TEXT("radio") && Mode != TEXT("keyboard") && Mode != TEXT("gamepad") && Mode != TEXT("calibrate")) {
        Error = TEXT("Choose radio, keyboard, or gamepad.");
        return nullptr;
    }
    const FRotator Heading(0, PC->GetControlRotation().Yaw, 0);
    const FVector Position = PC->GetPawn()->GetActorLocation() + Heading.Vector() * 150. + FVector(0, 0, 100);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
    Spawn.ObjectFlags |= RF_Transient;
    Spawn.Owner = PC;
    auto* Drone = World->SpawnActor<AFactoryFPVDrone>(Position, Heading, Spawn);
    if (!Drone) {
        Error = TEXT("No clear launch position. Move to an open area.");
        return nullptr;
    }
    if (!Drone->StartSession(PC, Mode, Error, PracticeCourse)) {
        Drone->Destroy();
        return nullptr;
    }
    return Drone;
}

UFactoryFPVMovementComponent::UFactoryFPVMovementComponent() {
    PrimaryComponentTick.bCanEverTick = false;
}
bool UFactoryFPVMovementComponent::ResolvePenetrationImpl(const FVector& Adjustment,const FHitResult& Hit,const FQuat& NewRotation) {
    bAdjustedForPenetration=true;
    return Super::ResolvePenetrationImpl(Adjustment,Hit,NewRotation);
}

AFactoryFPVDrone::AFactoryFPVDrone() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickGroup=TG_PostPhysics;
    bReplicates=false;
    Body=CreateDefaultSubobject<USphereComponent>(TEXT("DroneCollision"));
    SetRootComponent(Body);
    Body->InitSphereRadius(12.f);
    Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Body->SetCollisionObjectType(ECC_WorldDynamic);
    Body->SetCollisionResponseToAllChannels(ECR_Block);
    Body->SetGenerateOverlapEvents(false);
    Movement = CreateDefaultSubobject<UFactoryFPVMovementComponent>(TEXT("DroneMovement"));
    Movement->SetUpdatedComponent(Body);
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("FPVCamera"));
    Camera->SetupAttachment(Body);
    Lens=CreateDefaultSubobject<UFactoryFPVLensComponent>(TEXT("FPVLens"));
    Race=CreateDefaultSubobject<UFactoryFPVRaceComponent>(TEXT("RaceTiming"));
    RacePath.Reserve(25);RaceScoring.Reserve(24);
    PropAudio=CreateDefaultSubobject<UFactoryFPVPropAudioComponent>(TEXT("PropAudio"));
    PropAudio->SetupAttachment(Body);
    CueAudio=CreateDefaultSubobject<UFactoryFPVFlightCueAudioComponent>(TEXT("FlightCueAudio"));
    CueAudio->SetupAttachment(Body);
    Frame=CreateDefaultSubobject<USceneComponent>(TEXT("Airframe"));
    Frame->SetupAttachment(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ChassisMesh(TEXT("/FactoryFPV/Airframe/SM_ScoutFrame.SM_ScoutFrame"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ClockwiseMesh(TEXT("/FactoryFPV/Airframe/SM_ScoutPropCW.SM_ScoutPropCW"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CounterclockwiseMesh(TEXT("/FactoryFPV/Airframe/SM_ScoutPropCCW.SM_ScoutPropCCW"));
    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> GhostSurface(
        TEXT("/FactoryFPV/Materials/M_RaceGhost.M_RaceGhost"),LOAD_NoWarn | LOAD_Quiet);
    RaceGhostSurface=GhostSurface.Get();
    if (!RaceGhostSurface) {
        static ConstructorHelpers::FObjectFinder<UMaterialInterface> Fallback(TEXT("/FactoryFPV/Materials/M_RaceLight.M_RaceLight"));
        RaceGhostSurface=Fallback.Object;
    }
    auto Part = [this](const TCHAR* Name, UStaticMesh* Mesh) {
        auto* MeshPart = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        MeshPart->SetupAttachment(Frame);
        MeshPart->SetStaticMesh(Mesh);
        MeshPart->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        MeshPart->SetGenerateOverlapEvents(false);
        MeshPart->SetCastShadow(true);
        return MeshPart;
    };
    ScoutChassis=Part(TEXT("ScoutFrame"),ChassisMesh.Object);
    ScoutChassis->SetOwnerNoSee(true);
    for (int32 Index=0; Index<4; ++Index) {
        auto* Rotor=Part(*FString::Printf(TEXT("Prop%d"),Index),IsClockwiseRotor(Index) ? ClockwiseMesh.Object : CounterclockwiseMesh.Object);
        Rotor->SetRelativeLocation(FVector(Index<2 ? 22 : -22,Index%2 ? 22 : -22,6));
        Rotor->SetRelativeRotation(FRotator(0,Index*37.f,0));
        Rotors.Add(Rotor);
    }
    Streaming=CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("DroneStreaming"));
    Streaming->DisableStreamingSource();
}
void AFactoryFPVDrone::ApplyFlightSettings(const UFactoryFPVSettings& Settings) {
    Body->SetSphereRadius(Settings.FrameSizeCm * 0.5f);
    Frame->SetRelativeScale3D(FVector(Settings.FrameSizeCm / 100.f));
    for (const auto& Rotor : Rotors) Rotor->SetOwnerNoSee(!Settings.bShowFrameInFPV);
    // Keep the lens on the quad. The optional mount admits the front prop tips
    // at the lens edges instead of looking over the entire airframe from behind.
    const FVector Mount = Settings.bShowFrameInFPV ? FVector(.06f,0,.12f) : FVector(.25f,0,.08f);
    Camera->SetRelativeLocation(Mount * Settings.FrameSizeCm);
    Camera->SetRelativeRotation(FRotator(Settings.CameraTilt,0,0));
    Camera->SetFieldOfView(Settings.CameraFov);
    Lens->ApplyToCamera(Camera,Settings.LensStrength);
    Flight.parameters.mass = Settings.MassKg;
    NominalThrustToWeight=Settings.ThrustToWeight;
    Flight.parameters.maxThrust = Settings.MassKg * Flight.parameters.gravity * NominalThrustToWeight;
    Flight.parameters.throttleExpo = Settings.ThrottleExpo;
    Flight.parameters.nominalHoverThrottle = 1. / NominalThrustToWeight;
    Flight.parameters.rateDegrees = Settings.AcroRate;
    Flight.parameters.pitchRateDegrees = Settings.PitchRate;
    Flight.parameters.yawRateDegrees = Settings.YawRate;
    Flight.parameters.rollCenterRateDegrees = Settings.RollCenterRate;
    Flight.parameters.pitchCenterRateDegrees = Settings.PitchCenterRate;
    Flight.parameters.yawCenterRateDegrees = Settings.YawCenterRate;
    Flight.parameters.expo = Settings.Expo;
    Flight.parameters.deadzone = Settings.Deadzone;
    Flight.parameters.angleMode = Settings.bAngleMode;
    Flight.parameters.angleLimitDegrees = Settings.AngleLimit;
    Flight.parameters.levelingGain = Settings.LevelResponse;
    Battery.parameters.enabled=Settings.bSimulateBattery;
    Battery.parameters.cells=Settings.BatteryCells;
    Battery.parameters.capacityMah=Settings.BatteryCapacityMah;
    Battery.parameters.maxPowerWatts=Settings.BatteryMaxPowerWatts;
    Battery.reset();
    Damage.parameters.enabled=Settings.bCrashDamageEnabled;
    Damage.parameters.destructionEnergyJoules=Settings.CrashToleranceJoules;
    Damage.reset();
}

void AFactoryFPVDrone::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) {
    Camera->GetCameraView(DeltaTime, OutResult);
    // The standard 10 cm plane clips a small quad's prop tips. Override only
    // this view, leaving the pioneer and the global clip distance unchanged.
    OutResult.PerspectiveNearClipPlane = 1.f;
}

bool AFactoryFPVDrone::StartSession(APlayerController* PC,FName Mode,FString& Error,int32 PracticeCourse) {
    ResetRadioGates();
    auto* Viewport = GetWorld()->GetGameViewport();
    if (!Viewport || !PC || !PC->IsLocalController() || GetNetMode() != NM_Standalone || !PC->GetPawn()) {
        Error = TEXT("FPV requires a local player in a standalone game.");
        return false;
    }
    FInputDeviceId SelectedGamepad;
    if (Mode==TEXT("gamepad") && !CheckGamepadForLaunch(PC,Error,&SelectedGamepad)) return false;
    Pilot=PC; OriginalPawn=PC->GetPawn(); OriginalView=PC->GetViewTarget(); InputMode=Mode;
    auto* Settings = GetMutableDefault<UFactoryFPVSettings>();
    Settings->Sanitize();
    if (Mode!=TEXT("calibrate") && !Settings->bAllowSandboxAccess && !AFactoryFPVRadioEquipment::IsEquippedBy(PC)) {
        Error=TEXT("Equip an FPV Radio Kit, or enable sandbox flights in the hangar.");
        return false;
    }
    ApplyFlightSettings(*Settings);
    const bool bPractice=PracticeCourse>0 && Mode!=TEXT("calibrate");
    int32 SelectedCourse=bPractice ? PracticeCourse : 0;
    const FString* SelectedLayout=nullptr;
    if (!bPractice && Mode!=TEXT("calibrate") && PC->GetLocalPlayer()) {
        if (auto* Subsystem=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()) {
            SelectedCourse=Subsystem->GetSelectedRacingCourse();
            if (SelectedCourse>0) SelectedLayout=&Subsystem->GetSelectedRacingLayout();
        }
    }
    if (!Race->StartCourse(GetWorld(),SelectedCourse,Error,SelectedLayout,bPractice)) return false;
    TSharedPtr<factoryfpv::WindowsRadio> PreparedRadio;
    if (Mode == TEXT("radio") || Mode == TEXT("calibrate")) {
        const auto Channels = RadioChannels(*Settings);
        if (Mode==TEXT("radio") && !Settings->AreRadioBindingsValid(Error)) return false;
        if (Mode == TEXT("radio") && !Settings->IsRadioCalibrated()) {
            Error = FString::Printf(TEXT("Open the hangar with %s and calibrate your radio before flying."),
                *FFactoryFPVKeyboardShortcuts::KeyLabel(Settings->KeyboardShortcuts.Hangar));
            return false;
        }
        const auto Window = Viewport->GetWindow();
        const auto NativeWindow = Window ? Window->GetNativeWindow() : nullptr;
        PreparedRadio = MakeShared<factoryfpv::WindowsRadio>();
        const auto* RequiredChannels = Mode == TEXT("radio") ? &Channels : nullptr;
        if (!NativeWindow || !PreparedRadio->open(*Settings->RadioDeviceId, NativeWindow->GetOSWindowHandle(), RequiredChannels)) {
            Error = NativeWindow ? FString::Printf(TEXT("FPV: %s"), PreparedRadio->error().c_str()) : TEXT("FPV: game window is unavailable.");
            return false;
        }
        if (Mode==TEXT("radio") && !Settings->AreRadioBindingControlsAvailable(PreparedRadio->axisInfo(),PreparedRadio->buttonPresence(),Error)) return false;
    }
    Body->IgnoreActorWhenMoving(OriginalPawn.Get(),true);
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(this);
    Query.AddIgnoredActor(OriginalPawn.Get());
    FHitResult Ground;
    const FVector SpawnPosition = GetActorLocation();
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Body->GetScaledSphereRadius());
    const FCollisionResponseParams Responses(Body->GetCollisionResponseToChannels());
    if (!GetWorld()->SweepSingleByChannel(Ground, SpawnPosition, SpawnPosition - FVector(0, 0, 500),
        FQuat::Identity, Body->GetCollisionObjectType(), Shape, Query, Responses) ||
        Ground.bStartPenetrating || Ground.ImpactNormal.Z < 0.5f) {
        Error = TEXT("FPV: move to a clear launch surface with ground within five metres.");
        return false;
    }
    // Leave a small clearance for the first gravity step and collision tolerances.
    LaunchPosition = Ground.Location + Ground.Normal * 2.f;
    if (GetWorld()->OverlapBlockingTestByChannel(LaunchPosition, FQuat::Identity,
        Body->GetCollisionObjectType(), Shape, Query, Responses)) {
        Error = TEXT("FPV: the launch surface is obstructed. Move to a clear area.");
        return false;
    }
    SetActorLocation(LaunchPosition);
    LaunchYaw = GetActorRotation().Yaw * factoryfpv::Pi / 180.;
    Flight.reset(ToCore(LaunchPosition / 100.), LaunchYaw);
    FActorSpawnParameters AnchorSpawn;
    AnchorSpawn.ObjectFlags |= RF_Transient;
    AnchorSpawn.OverrideLevel=GetWorld()->PersistentLevel;
    AnchorSpawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PioneerAnchor=GetWorld()->SpawnActor<AFactoryFPVPioneerAnchor>(AnchorSpawn);
    if (!PioneerAnchor || !PioneerAnchor->Hold(Cast<ACharacter>(OriginalPawn.Get()),Error)) {
        if (PioneerAnchor) PioneerAnchor->Destroy();
        PioneerAnchor=nullptr;
        return false;
    }
    if (!InputLease.Acquire(PC)) {
        PioneerAnchor->RequestRelease();
        PioneerAnchor=nullptr;
        Error=TEXT("FPV: input ownership tracking is unavailable. Flight was not started.");
        return false;
    }
    Controls=NewObject<UInputComponent>(this,TEXT("FPVControls"));
    Controls->RegisterComponent(); Controls->bBlockInput=true; Controls->Priority=10000;
    Shortcuts=Settings->KeyboardShortcuts;
    Controls->BindKey(Shortcuts.Hangar,IE_Pressed,this,&AFactoryFPVDrone::OpenHangar);
    Controls->BindKey(Shortcuts.Arm,IE_Pressed,this,&AFactoryFPVDrone::ArmToggle);
    Controls->BindKey(Shortcuts.Recover,IE_Pressed,this,&AFactoryFPVDrone::RecoverFromKey);
    Controls->BindKey(Shortcuts.Return,IE_Pressed,this,&AFactoryFPVDrone::ExitDrone);
    if (AFactoryFPVRadioEquipment::IsEquippedBy(PC)) Controls->BindKey(EKeys::RightMouseButton,IE_Pressed,this,&AFactoryFPVDrone::EquipmentReturn);
    PC->PushInputComponent(Controls);
    Radio=MoveTemp(PreparedRadio);
    bSessionActive=true;
    SessionCameraManager = PC->PlayerCameraManager;
    if (SessionCameraManager.IsValid()) {
        PreviousLockedFov = SessionCameraManager->GetLockedFOV();
        SessionFov = Settings->CameraFov;
        SessionCameraManager->SetFOV(SessionFov);
    }
    PC->SetViewTarget(this);
    PilotVisual=GetWorld()->SpawnActor<AFactoryFPVPilotVisual>(AnchorSpawn);
    if (PilotVisual && !PilotVisual->Begin(Cast<ACharacter>(OriginalPawn.Get()))) {
        UE_LOG(LogTemp,Warning,TEXT("FactoryFPV: pilot presentation unavailable; retaining original character rendering."));
        PilotVisual->Destroy();
        PilotVisual=nullptr;
    }
    SessionHud=Cast<AFGHUD>(PC->GetHUD());
    if (SessionHud.IsValid()) {
        bPreviousPartialHideHud=SessionHud->GetPartialPumpiMode();
        bPreviousHiddenHud=SessionHud->GetHiddenHUDMode();
        bHudReleasedForNativeUI=false;
        // The intro/crosshair partial mode alone does not hide the native
        // PlayerHUD widgets in Shipping. Use the gameplay HUD mode as well.
        SessionHud->SetHiddenHUDMode(true);
        SessionHud->SetPartialPumpiMode(true);
    }
    Streaming->EnableStreamingSource();
    PilotViewport = Viewport;
    Overlay = SNew(SFactoryFPVOSD).Shortcuts(Shortcuts);
    OverlayRoot = Overlay;
    UpdateOsd(LastInput);
    Viewport->AddViewportWidgetContent(OverlayRoot.ToSharedRef(), 100);
    if (FSlateApplication::IsInitialized() && !ApplicationActivationHandle.IsValid()) {
        ApplicationActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged()
            .AddUObject(this,&AFactoryFPVDrone::OnApplicationActivationChanged);
    }
    if (SelectedGamepad.IsValid()) StartGamepadMonitoring(SelectedGamepad);
    return true;
}

FInputDeviceId AFactoryFPVDrone::FindLocalGamepad(const APlayerController* Controller) {
    if (!Controller || !Controller->GetPlatformUserId().IsValid()) return INPUTDEVICEID_NONE;
    const auto& Mapper=IPlatformInputDeviceMapper::Get();
    const auto* Hardware=UInputDeviceSubsystem::Get();
    TArray<FInputDeviceId> Devices;
    Mapper.GetAllInputDevicesForUser(Controller->GetPlatformUserId(),Devices);
    FInputDeviceId Selected=INPUTDEVICEID_NONE;
    for (const auto Device:Devices) {
        if (Device==Mapper.GetDefaultInputDevice() || Mapper.GetInputDeviceConnectionState(Device)!=EInputDeviceConnectionState::Connected) continue;
        const auto Type=Hardware ? Hardware->GetInputDeviceHardwareIdentifier(Device).PrimaryDeviceType : EHardwareDevicePrimaryType::Unspecified;
        if (Type!=EHardwareDevicePrimaryType::Unspecified && Type!=EHardwareDevicePrimaryType::Gamepad) continue;
        // PlayerInput aggregates the gamepad keys for this player. Refuse
        // ambiguous ownership when two connected devices can drive them.
        if (Selected.IsValid()) return INPUTDEVICEID_NONE;
        Selected=Device;
    }
    return Selected;
}

bool AFactoryFPVDrone::CheckGamepadForLaunch(const APlayerController* Controller,FString& Error,FInputDeviceId* SelectedDevice) {
    const auto Device=FSlateApplication::IsInitialized() && FSlateApplication::Get().IsGamepadAttached() ?
        FindLocalGamepad(Controller) : INPUTDEVICEID_NONE;
    if (SelectedDevice) *SelectedDevice=Device;
    Error=Device.IsValid() ? FString() : TEXT("Gamepad mode needs one connected gamepad for this player. Connect one and disconnect extras, or choose Keyboard.");
    return Device.IsValid();
}

void AFactoryFPVDrone::StartGamepadMonitoring(FInputDeviceId Device) {
    GamepadDevice=Device;bGamepadLost=false;
    auto& Mapper=IPlatformInputDeviceMapper::Get();
    GamepadConnectionHandle=Mapper.GetOnInputDeviceConnectionChange().AddUObject(this,&AFactoryFPVDrone::OnGamepadConnectionChanged);
    GamepadPairingHandle=Mapper.GetOnInputDevicePairingChange().AddUObject(this,&AFactoryFPVDrone::OnGamepadPairingChanged);
}

void AFactoryFPVDrone::OnGamepadConnectionChanged(EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId Device) {
    const auto* PC=Pilot.Get();
    if (Device==GamepadDevice && State!=EInputDeviceConnectionState::Connected) LoseGamepad();
    else if (PC && User==PC->GetPlatformUserId() && State==EInputDeviceConnectionState::Connected && FindLocalGamepad(PC)!=GamepadDevice) LoseGamepad();
}

void AFactoryFPVDrone::OnGamepadPairingChanged(FInputDeviceId Device,FPlatformUserId NewUser,FPlatformUserId OldUser) {
    const auto* PC=Pilot.Get();
    if (PC && (Device==GamepadDevice || NewUser==PC->GetPlatformUserId() || OldUser==PC->GetPlatformUserId()) &&
        FindLocalGamepad(PC)!=GamepadDevice) LoseGamepad();
}

void AFactoryFPVDrone::LoseGamepad() {
    if (bGamepadLost) return;
    bGamepadLost=true;
    SuspendFlightInput();
    Race->InvalidateLap();
    LastInput={0,0,0,0,false};
    if (!IsFlightInputBlocked()) PlayFlightCue(EFactoryFPVFlightCue::ReceiverLost);
    UpdateOsd(LastInput);
}

factoryfpv::Input AFactoryFPVDrone::ReadControls(float Dt) {
    ArmPosition=factoryfpv::ArmPosition::Lost;
    ActionPositions.fill(factoryfpv::ArmPosition::Lost);
    auto* PC=Pilot.Get();
    if(!PC) return {0,0,0,0,false};
    auto Axis=[PC](FKey K) { return static_cast<double>(PC->GetInputAnalogKeyState(K)); };
    if(InputMode==TEXT("keyboard")) {
        auto Down=[PC](FKey K) { return PC->IsInputKeyDown(K) ? 1.0 : 0.0; };
        KeyboardThrottle=factoryfpv::clamp(KeyboardThrottle+(Down(EKeys::W)-Down(EKeys::S))*Dt*.4,0,1);
        return {Down(EKeys::Right)-Down(EKeys::Left),Down(EKeys::Down)-Down(EKeys::Up),
            Down(EKeys::D)-Down(EKeys::A),KeyboardThrottle,true};
    }
    if(InputMode==TEXT("gamepad")) {
        const auto& Mapper=IPlatformInputDeviceMapper::Get();
        if (bGamepadLost || !GamepadDevice.IsValid() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsGamepadAttached() ||
            Mapper.GetInputDeviceConnectionState(GamepadDevice)!=EInputDeviceConnectionState::Connected ||
            Mapper.GetUserForInputDevice(GamepadDevice)!=PC->GetPlatformUserId()) {
            LoseGamepad();
            return {0,0,0,0,false};
        }
        return {Axis(EKeys::Gamepad_RightX),-Axis(EKeys::Gamepad_RightY),
            Axis(EKeys::Gamepad_LeftX),Axis(EKeys::Gamepad_RightTriggerAxis),true};
    }
    std::array<long, 8> Axes{};
    std::array<bool,128> Buttons{};
    if (!Radio || !Radio->read(Axes,&Buttons)) return {0, 0, 0, 0, false};
    const auto& S = *GetDefault<UFactoryFPVSettings>();
    UpdateRadioControlPositions(Axes,Buttons,Radio->axisInfo(),Radio->buttonPresence());
    const auto Channels = RadioChannels(S);
    return {
        Calibration(S.RadioRollCalibration, S.ReverseRoll).axis(Axes[Channels[0]]),
        Calibration(S.RadioPitchCalibration, S.ReversePitch).axis(Axes[Channels[1]]),
        Calibration(S.RadioYawCalibration, S.ReverseYaw).axis(Axes[Channels[2]]),
        Calibration(S.RadioThrottleCalibration, S.ReverseThrottle).axis(Axes[Channels[3]], true),
        true
    };
}
void AFactoryFPVDrone::UpdateRadioControlPositions(const std::array<long,8>& Axes,const std::array<bool,128>& Buttons,
    const std::array<factoryfpv::RadioAxisInfo,8>& AxisInfo,const std::array<bool,128>& ButtonPresence) {
    const auto& Settings=*GetDefault<UFactoryFPVSettings>();
    auto Position=[&](const factoryfpv::ArmBinding& Binding) { return Binding.position(Axes,Buttons,Binding.available(AxisInfo,ButtonPresence)); };
    ArmPosition=Position(Settings.RadioArm.Native());
    const auto Bindings=Settings.RadioActionBindings();
    for (int Index=0;Index<factoryfpv::RadioActionCount;++Index) ActionPositions[Index]=Position(Bindings[Index]);
}
void AFactoryFPVDrone::ResetRadioGates() {
    ArmGate.reset();
    ArmPosition=factoryfpv::ArmPosition::Lost;
    for (auto& Gate:ActionGates) Gate.reset();
    ActionPositions.fill(factoryfpv::ArmPosition::Lost);
}
bool AFactoryFPVDrone::ProcessRadioActions(const factoryfpv::Input& Input) {
    if (InputMode!=TEXT("radio") || !Input.valid()) { ResetRadioGates(); return false; }
    std::array<bool,factoryfpv::RadioActionCount> Requests{};
    for (int Index=0;Index<factoryfpv::RadioActionCount;++Index)
        Requests[Index]=ActionGates[Index].update(ActionPositions[Index]);
    // Return/recovery end this frame. In particular, a simultaneous arm request
    // must never arm a newly recovered drone or continue ticking a destroyed one.
    if (Requests[static_cast<int>(factoryfpv::RadioAction::Return)]) {
        ExitDrone();
        return true;
    }
    if (Requests[static_cast<int>(factoryfpv::RadioAction::Recover)]) {
        ResetDrone();
        UpdateOsd(Input);
        return true;
    }
    if (Requests[static_cast<int>(factoryfpv::RadioAction::ToggleMode)]) {
        // Session-local; the hangar's saved starting mode remains unchanged.
        Flight.parameters.angleMode=!Flight.parameters.angleMode;
        // The persistent mode readout shows this immediately. A permanent
        // informational notice would hide low-battery/frame warnings.
        Notice.Reset();
    }
    return false;
}
bool AFactoryFPVDrone::IsNativeUIBlockingFlight() const {
    const auto* Viewport=PilotViewport.Get();
    const bool ConsoleOpen=Viewport && Viewport->ViewportConsole && Viewport->ViewportConsole->ConsoleState!=NAME_None;
    return FactoryFPVInteractionState::BlocksPlayerInteraction(Pilot.Get()) || ConsoleOpen;
}
bool AFactoryFPVDrone::IsFlightInputBlocked() const {
    return bApplicationDeactivated || IsNativeUIBlockingFlight() || !FPlatformApplicationMisc::IsThisApplicationForeground();
}
void AFactoryFPVDrone::OnApplicationActivationChanged(bool bActive) {
    if (!bSessionActive) return;
    bApplicationDeactivated=!bActive;
    if (bActive) return;
    // A loss and regain can both arrive between simulation ticks. Clear thrust
    // and edge gates at the loss itself so a held radio switch cannot resume it.
    SuspendFlightInput();
    Race->InvalidateLap();
}
void AFactoryFPVDrone::SuspendFlightInput() {
    Flight.disarm();
    HideRaceGhost();
    Battery.step(0,false,0);
    ResetRadioGates();
    PropAudio->StopImmediately();
    CueAudio->StopCue();
    KeyboardThrottle=0;
    Clock.clear();
}
void AFactoryFPVDrone::UpdateHudForNativeUI(bool NativeUIBlocked) {
    if (!SessionHud.IsValid() || NativeUIBlocked==bHudReleasedForNativeUI) return;
    if (NativeUIBlocked) {
        // Yield our modes while native UI owns presentation. Changes made by
        // that owner after this release must not be overwritten on exit.
        if (SessionHud->GetHiddenHUDMode()) SessionHud->SetHiddenHUDMode(bPreviousHiddenHud);
        if (SessionHud->GetPartialPumpiMode()) SessionHud->SetPartialPumpiMode(bPreviousPartialHideHud);
    } else {
        // Native UI may reconstruct/show HUD widgets without changing its
        // mode flag. Preserve that owner's latest modes as the new return
        // baseline, then renew through public setters on this transition.
        bPreviousHiddenHud=SessionHud->GetHiddenHUDMode();
        bPreviousPartialHideHud=SessionHud->GetPartialPumpiMode();
        SessionHud->SetHiddenHUDMode(true);
        SessionHud->SetPartialPumpiMode(true);
    }
    bHudReleasedForNativeUI=NativeUIBlocked;
}
bool AFactoryFPVDrone::IsFlightOverlayVisible() const {
    return OverlayRoot.IsValid() && OverlayRoot->GetVisibility().IsVisible();
}
void AFactoryFPVDrone::ArmToggle() {
    // Emergency disarm remains available even if a menu/focus transition has
    // blocked flight input. Arming uses the same guard as simulation polling.
    if (Flight.armed) {
        Flight.disarm();
        HideRaceGhost();
        ResetRadioGates();
        Notice=TEXT("Manual disarm");
    }
    else if (InputMode==TEXT("calibrate") || IsFlightInputBlocked()) return;
    else if (Damage.destroyed()) Notice=RecoveryNotice(TEXT("FRAME LOST"),TEXT("repair"));
    else if (Battery.empty()) Notice=RecoveryNotice(TEXT("SIM BAT EMPTY"),TEXT("recharge"));
    else if (InputMode==TEXT("radio") && GetDefault<UFactoryFPVSettings>()->RadioArm.Kind!=0)
        Notice=TEXT("Use radio arm switch: OFF, throttle down, then ON");
    else Notice = TryArmFlight(ReadControls(0)) ? FString() : TEXT("Lower throttle or check radio");
    UpdatePropAudio();
}
bool AFactoryFPVDrone::TryArmFlight(const factoryfpv::Input& Input) {
    if (Battery.empty() || Damage.destroyed() || !Flight.arm(Input)) return false;
    RecoveryWarning.Reset();
    return true;
}
void AFactoryFPVDrone::RecoverFromKey() {
    if (!IsNativeUIBlockingFlight()) ResetDrone();
}
void AFactoryFPVDrone::ResetDrone() {
    Race->InvalidateLap();
    Flight.disarm();
    HideRaceGhost();
    ResetRadioGates();
    PropAudio->StopImmediately();
    CueAudio->StopCue();
    KeyboardThrottle=0; ArmedSeconds=0; Clock.clear();
    FCollisionQueryParams Query;
    Query.AddIgnoredActor(this);
    if (OriginalPawn.IsValid()) Query.AddIgnoredActor(OriginalPawn.Get());
    const FCollisionShape Shape = FCollisionShape::MakeSphere(Body->GetScaledSphereRadius());
    const FCollisionResponseParams Responses(Body->GetCollisionResponseToChannels());
    const ECollisionChannel Channel = Body->GetCollisionObjectType();
    if (GetWorld()->OverlapBlockingTestByChannel(LaunchPosition, FQuat::Identity, Channel, Shape, Query, Responses)) {
        RecoveryWarning = FString::Printf(TEXT("Launch point obstructed. %s to return and relaunch."),
            *FFactoryFPVKeyboardShortcuts::KeyLabel(Shortcuts.Return));
        return;
    }
    // A clear launch pose can be midair after a floor is dismantled or unloaded.
    // Check nearby support at that pose, without snapping to a lower storey.
    FHitResult Ground;
    constexpr double MaxRecoveryClearanceCm = 10.;
    if (!GetWorld()->SweepSingleByChannel(Ground, LaunchPosition, LaunchPosition - FVector(0, 0, MaxRecoveryClearanceCm),
        FQuat::Identity, Channel, Shape, Query, Responses) || Ground.bStartPenetrating || Ground.ImpactNormal.Z < 0.5f) {
        RecoveryWarning = FString::Printf(TEXT("Launch surface unavailable. %s to return and relaunch."),
            *FFactoryFPVKeyboardShortcuts::KeyLabel(Shortcuts.Return));
        return;
    }
    RecoveryWarning.Reset();
    Flight.reset(ToCore(LaunchPosition/100.),LaunchYaw);
    Battery.reset();
    Damage.reset();
    Flight.parameters.maxThrust=Flight.parameters.mass*Flight.parameters.gravity*NominalThrustToWeight;
    SetActorLocationAndRotation(LaunchPosition,FRotator(0,LaunchYaw*180./factoryfpv::Pi,0));
    Notice = TEXT("Reset and disarmed");
}
void AFactoryFPVDrone::ExitDrone() {
    if (bSessionActive && Race && !Race->IsPractice() && Race->GetDebrief().LapCount>0) {
        const auto* PC=Pilot.Get();
        const auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        if (auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr) {
            Hangar->StoreRaceDebrief(GetWorld(),Race->GetDebrief());
        }
    }
    RestorePilot();
    Destroy();
}
void AFactoryFPVDrone::OpenHangar() {
    // Native UI refusal must happen before ending the owned session. OS focus
    // alone is not a reason to refuse a hangar transition.
    if (IsNativeUIBlockingFlight()) return;
    auto* PC = Pilot.Get();
    ExitDrone();
    if (PC && PC->GetLocalPlayer()) {
        if (auto* Hangar=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()) Hangar->OpenPanel();
    }
}
void AFactoryFPVDrone::EquipmentReturn() {
    if (!IsFlightInputBlocked()) ExitDrone();
}
void AFactoryFPVDrone::RestorePilot() {
    HideRaceGhost();
    if (ApplicationActivationHandle.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ApplicationActivationHandle);
    ApplicationActivationHandle.Reset();
    if (GamepadConnectionHandle.IsValid() || GamepadPairingHandle.IsValid()) {
        auto& Mapper=IPlatformInputDeviceMapper::Get();
        if (GamepadConnectionHandle.IsValid()) Mapper.GetOnInputDeviceConnectionChange().Remove(GamepadConnectionHandle);
        if (GamepadPairingHandle.IsValid()) Mapper.GetOnInputDevicePairingChange().Remove(GamepadPairingHandle);
    }
    GamepadConnectionHandle.Reset();GamepadPairingHandle.Reset();GamepadDevice=INPUTDEVICEID_NONE;
    bApplicationDeactivated=false;
    if (Race) Race->StopCourse();
    if (Lens) Lens->Clear();
    ResetRadioGates();
    InputLease.Release();
    if(!bSessionActive) return;
    bSessionActive=false;
    PropAudio->StopImmediately();
    CueAudio->StopCue();
    if (IsValid(PilotVisual)) { PilotVisual->Release(); PilotVisual->Destroy(); }
    PilotVisual=nullptr;
    if(auto* PC=Pilot.Get()) {
        PC->PopInputComponent(Controls);
        if(PC->GetViewTarget()==this) {
            // Possession may change while the FPV camera is active (respawn,
            // another system taking control). Never snap back to the old pawn.
            if(PC->GetPawn()!=OriginalPawn.Get()) PC->SetViewTarget(PC->GetPawn() ? static_cast<AActor*>(PC->GetPawn()) : PC);
            else if(OriginalView.IsValid()) PC->SetViewTarget(OriginalView.Get());
            else if(PC->GetPawn()) PC->SetViewTarget(PC->GetPawn());
        }
    }
    Streaming->DisableStreamingSource();
    if (SessionCameraManager.IsValid() && FMath::IsNearlyEqual(SessionCameraManager->GetLockedFOV(), SessionFov)) {
        if (PreviousLockedFov > 0.f) SessionCameraManager->SetFOV(PreviousLockedFov);
        else SessionCameraManager->UnlockFOV();
    }
    SessionCameraManager.Reset();
    if (SessionHud.IsValid() && !bHudReleasedForNativeUI) {
        if (SessionHud->GetHiddenHUDMode()) SessionHud->SetHiddenHUDMode(bPreviousHiddenHud);
        if (SessionHud->GetPartialPumpiMode()) SessionHud->SetPartialPumpiMode(bPreviousPartialHideHud);
    }
    SessionHud.Reset();
    bHudReleasedForNativeUI=false;
    if (IsValid(PioneerAnchor)) PioneerAnchor->RequestRelease();
    PioneerAnchor=nullptr;
    Radio.Reset();
    if (PilotViewport.IsValid() && OverlayRoot) PilotViewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
    OverlayRoot.Reset();
    Overlay.Reset();
}
void AFactoryFPVDrone::EndPlay(const EEndPlayReason::Type Reason) {
    RestorePilot(); Super::EndPlay(Reason);
}
void AFactoryFPVDrone::Tick(float Dt) {
    Super::Tick(Dt);
    if(!bSessionActive) return;
    auto* PC=Pilot.Get();
    auto* Pioneer = Cast<AFGCharacterBase>(OriginalPawn.Get());
    if(!PC || !OriginalPawn.IsValid() || (Pioneer && !Pioneer->IsAliveAndWell()) || PC->GetPawn()!=OriginalPawn.Get() || PC->GetViewTarget()!=this) {
        ExitDrone(); return;
    }
    if (!IsValid(PioneerAnchor) || !PioneerAnchor->IsHolding()) { ExitDrone(); return; }
    const bool NativeUIBlocked=IsNativeUIBlockingFlight();
    UpdateHudForNativeUI(NativeUIBlocked);
    if (OverlayRoot) OverlayRoot->SetVisibility(NativeUIBlocked ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
    if (!NativeUIBlocked) {
        // The lease observes native resets before renewing. It must not mistake
        // a newer external hold for one acquired by this session.
        if (!InputLease.Renew()) { ExitDrone(); return; }
    }
    if(IsFlightInputBlocked()) {
        const FVector PausedPath[]{GetActorLocation()};const uint8 NoScoring[]{0};
        Race->AdvanceFrame(MakeArrayView(PausedPath),0,Body->GetScaledSphereRadius(),MakeArrayView(NoScoring));
        SuspendFlightInput();
        UpdateOsd(LastInput,TEXT("PAUSED - DISARMED"));
        return;
    }
    if (InputMode == TEXT("calibrate")) {
        std::array<long, 8> Axes{};
        if (!Radio->read(Axes)) {
            UpdateOsd({0,0,0,0,false},TEXT("RX LOSS"));
        } else {
            UpdateOsd({0,0,0,0,true},FString(),FString::Printf(
                TEXT("X %ld  Y %ld  Z %ld  RX %ld\nRY %ld  RZ %ld  S0 %ld  S1 %ld"),
                Axes[0], Axes[1], Axes[2], Axes[3], Axes[4], Axes[5], Axes[6], Axes[7]));
        }
        return;
    }
    AdvanceActiveFlightFrame(ReadControls(FMath::Min(Dt,.1f)),Dt);
}

void AFactoryFPVDrone::AdvanceActiveFlightFrame(const factoryfpv::Input& Input,float Dt) {
    const bool bRadioLost=InputMode==TEXT("radio") && LastInput.valid() && !Input.valid();
    LastInput=Input;
    if (!Input.valid()) {
        Flight.disarm();
        PropAudio->StopImmediately();
    }
    if (ProcessRadioActions(Input)) return;
    if (InputMode==TEXT("radio") && GetDefault<UFactoryFPVSettings>()->RadioArm.Kind!=0) {
        const auto Action=ArmGate.update(Input.valid() ? ArmPosition : factoryfpv::ArmPosition::Lost);
        if (Action.disarm) Flight.disarm();
        if (Action.arm) Notice=Damage.destroyed() ? RecoveryNotice(TEXT("FRAME LOST"),TEXT("repair")) :
            Battery.empty() ? RecoveryNotice(TEXT("SIM BAT EMPTY"),TEXT("recharge")) :
            (TryArmFlight(Input) ? FString() : TEXT("Throttle too high: switch OFF, lower throttle, then ON"));
        else if (!Flight.armed && ArmPosition==factoryfpv::ArmPosition::On && Notice.IsEmpty())
            Notice=TEXT("Switch OFF, throttle down, then ON to arm");
    }
    if (Flight.armed) ArmedSeconds+=FMath::Max(0.f,Dt);
    RacePath.Reset();RaceScoring.Reset();RacePath.Add(GetActorLocation());
    Clock.advance(Dt, [&](double Step) {
        RaceScoring.Add(SimulateFlight(Input,Step) ? 1 : 0);RacePath.Add(GetActorLocation());
    });
    if (RaceScoring.IsEmpty()) RaceScoring.Add(Input.valid() && Flight.armed && !Damage.destroyed() ? 1 : 0);
    // Timing consumes the actual active frame duration, even when the flight
    // integrator caps a long hitch. Keep its swept substep path for gate order.
    const auto Before=Race->GetRaceState();
    Race->AdvanceFrame(RacePath,FMath::Max(0.f,Dt),Body->GetScaledSphereRadius(),RaceScoring);
    const auto& After=Race->GetRaceState();
    if (bRadioLost) PlayFlightCue(EFactoryFPVFlightCue::ReceiverLost);
    else if (After.lapCount>Before.lapCount) {
        const auto& LastLap=Race->GetDebrief().RecentLaps.Last();
        PlayFlightCue(LastLap.bCourseBest ? EFactoryFPVFlightCue::PersonalBest : EFactoryFPVFlightCue::Lap);
    } else if (After.inLap && (!Before.inLap || After.nextGate!=Before.nextGate)) {
        PlayFlightCue(EFactoryFPVFlightCue::Checkpoint);
    }
    if (!Flight.armed) Race->InvalidateLap();
    UpdateRaceGhost();
    const float RotorDegrees = Flight.armed ? static_cast<float>(6000. * (.15 + .85 * Flight.thrust) * Dt) : 0.f;
    for (int32 Index=0; Index<Rotors.Num(); ++Index)
        Rotors[Index]->AddLocalRotation(FRotator(0,(IsClockwiseRotor(Index) ? 1.f : -1.f)*RotorDegrees,0));
    if (Input.valid()) UpdatePropAudio();
    UpdateOsd(Input);
}

void AFactoryFPVDrone::CreateRaceGhost() {
    UStaticMesh* Mesh=ScoutChassis->GetStaticMesh();
    if (!Mesh || !RaceGhostSurface) return;
    RaceGhost=NewObject<UStaticMeshComponent>(this,RaceGhostName,RF_Transient);
    RaceGhost->SetStaticMesh(Mesh);
    RaceGhost->SetMobility(EComponentMobility::Movable);
    RaceGhost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RaceGhost->SetGenerateOverlapEvents(false);
    RaceGhost->SetCanEverAffectNavigation(false);
    RaceGhost->SetCastShadow(false);
    RaceGhost->SetReceivesDecals(false);
    RaceGhost->SetVisibleInRayTracing(false);
    RaceGhost->SetHiddenInGame(true);
    RaceGhost->ComponentTags.Add(RaceGhostName);
    RaceGhost->SetupAttachment(Body);
    RaceGhost->SetAbsolute(true,true,true);
    RaceGhostMaterial=UMaterialInstanceDynamic::Create(RaceGhostSurface,RaceGhost);
    if (RaceGhostMaterial->GetBlendMode()!=BLEND_Translucent) {
        RaceGhostMaterial->SetVectorParameterValue(TEXT("LightColor"),FLinearColor(.07f,.36f,1.f));
        RaceGhostMaterial->SetScalarParameterValue(TEXT("LightStrength"),.75f);
    }
    for (int32 Slot=0;Slot<RaceGhost->GetNumMaterials();++Slot) RaceGhost->SetMaterial(Slot,RaceGhostMaterial);
    AddInstanceComponent(RaceGhost);
    RaceGhost->RegisterComponent();
}

void AFactoryFPVDrone::HideRaceGhost() {
    if (RaceGhost) RaceGhost->SetHiddenInGame(true);
}

void AFactoryFPVDrone::UpdateRaceGhost() {
    FVector Position,Forward;
    if (!bSessionActive || !Flight.armed || !Race->GetGhostPose(Position,Forward) || Position.ContainsNaN() || Forward.ContainsNaN()) {
        HideRaceGhost();
        return;
    }
    const double DistanceSquared=FVector::DistSquared(Camera->GetComponentLocation(),Position);
    if (DistanceSquared<=FMath::Square(GhostHideDistanceCm)) {
        HideRaceGhost();
        return;
    }
    if (!RaceGhost) CreateRaceGhost();
    if (!RaceGhost) return;
    const FRotator Heading=Forward.IsNearlyZero() ? RaceGhost->GetComponentRotation() : Forward.Rotation();
    // Keep a distant replay readable even when the pilot is flying a micro frame.
    const double DisplayScale=FMath::Max(42.,2.*Body->GetScaledSphereRadius())/100.;
    RaceGhost->SetWorldTransform(FTransform(Heading,Position,FVector(DisplayScale)));
    if (RaceGhostMaterial->GetBlendMode()==BLEND_Translucent) {
        RaceGhostMaterial->SetScalarParameterValue(TEXT("DistanceFade"),static_cast<float>(
            FMath::SmoothStep(GhostHideDistanceCm,GhostFullOpacityDistanceCm,FMath::Sqrt(DistanceSquared))));
    }
    RaceGhost->SetHiddenInGame(false);
}

void AFactoryFPVDrone::UpdatePropAudio() {
    const auto* Settings=GetDefault<UFactoryFPVSettings>();
    factoryfpv::PropSoundParameters Parameters;
    Parameters.armed=Flight.armed && InputMode!=TEXT("calibrate");
    Parameters.thrust=Flight.thrust;
    Parameters.volume=Settings->PropVolume*FactoryFPVAudioSettings::GameGain();
    Parameters.maxRpm=UFactoryFPVPropAudioComponent::MaxRpmForAirframe(Settings->FrameSizeCm,Settings->ThrustToWeight);
    Parameters.roll=Flight.bodyRates.x*.1;
    Parameters.pitch=Flight.bodyRates.y*.1;
    Parameters.yaw=Flight.bodyRates.z*.1;
    Parameters.speedMps=Flight.velocity.length();
    PropAudio->SetMotorState(Parameters);
    if (Parameters.armed && Parameters.volume>0.) PropAudio->Start();
}

void AFactoryFPVDrone::PlayFlightCue(EFactoryFPVFlightCue Cue) {
    if (!bSessionActive) return;
    const double Volume=GetDefault<UFactoryFPVSettings>()->FlightCueVolume;
    if (Volume>0.) CueAudio->PlayCue(Cue,Volume*FactoryFPVAudioSettings::GameGain());
}

void AFactoryFPVDrone::UpdateOsd(const factoryfpv::Input& Input,const FString& Warning,const FString& Debug) {
    if (!Overlay) return;
    FFactoryFPVOSDTelemetry Data;
    Data.bArmed=Flight.armed;
    Data.bConnected=Input.connected;
    Data.bSetup=InputMode==TEXT("calibrate");
    Data.Throttle=Input.throttle;
    Data.SpeedMps=Flight.velocity.length();
    Data.RelativeAltitudeM=(GetActorLocation().Z-LaunchPosition.Z)/100.;
    Data.HomeDistanceM=OriginalPawn.IsValid() ? FVector::Dist(GetActorLocation(),OriginalPawn->GetActorLocation())/100. : 0;
    Data.ArmedSeconds=ArmedSeconds;
    Data.DisplaySeconds=FPlatformTime::Seconds();
    Data.bRadioArmBound=InputMode==TEXT("radio") && GetDefault<UFactoryFPVSettings>()->RadioArm.Kind!=0;
    Data.bAngleMode=Flight.parameters.angleMode;
    const auto* Settings=GetDefault<UFactoryFPVSettings>();
    Data.bShowCrosshair=Settings->bOsdCrosshair; Data.bShowSpeed=Settings->bOsdSpeed;
    Data.bShowAltitude=Settings->bOsdAltitude; Data.bShowHome=Settings->bOsdHome;
    Data.bShowTimer=Settings->bOsdTimer; Data.bShowThrottle=Settings->bOsdThrottle;
    Data.TextScale=Settings->OsdTextScale; Data.Opacity=Settings->OsdOpacity;
    Data.Layout=Settings->OsdLayout;
    Data.bBatteryEnabled=Battery.enabled(); Data.BatteryPercent=Battery.remainingPercent();
    Data.BatteryVolts=Battery.packVoltage(); Data.bBatteryLow=Battery.low(); Data.bBatteryCritical=Battery.critical();
    Data.bDamageEnabled=Damage.enabled(); Data.IntegrityPercent=Damage.integrityPercent();
    Race->FillTelemetry(Data);
    FVector TargetAim;int32 TargetOrder=0;
    const FVector DronePosition=GetActorLocation();
    if (Camera && Race->GetNextCheckpointApproach(DronePosition,Body->GetScaledSphereRadius(),TargetAim,TargetOrder,
        Data.RaceTargetInstruction,Data.bRaceTargetOnExitSide,&Data.bRaceTargetVerticalGate)) {
        const FVector Direction=Camera->GetComponentTransform().InverseTransformPosition(TargetAim);
        Data.bRaceTargetAvailable=!Direction.ContainsNaN();
        Data.RaceTargetBearingDegrees=FMath::RadiansToDegrees(FMath::Atan2(Direction.Y,Direction.X));
        Data.RaceTargetElevationDegrees=FMath::RadiansToDegrees(FMath::Atan2(Direction.Z,FMath::Sqrt(Direction.X*Direction.X+Direction.Y*Direction.Y)));
        Data.RaceTargetDistanceM=FVector::Dist(Camera->GetComponentLocation(),TargetAim)/100.;
        Data.RaceTargetHeightOffsetM=(TargetAim.Z-DronePosition.Z)/100.;
        const auto* PC=Pilot.Get();
        const auto* Player=PC ? PC->GetLocalPlayer() : nullptr;
        const auto* Viewport=Player && Player->ViewportClient ? Player->ViewportClient->Viewport : nullptr;
        const FIntPoint Size=Viewport ? Viewport->GetSizeXY() : FIntPoint::ZeroValue;
        if (Input.connected && !Data.bSetup && Size.X>0 && Size.Y>0) {
            // The drone owns a centered, unconstrained perspective camera.
            // Use Unreal's FOV-axis rule with the current camera FOV and view size.
            FMinimalViewInfo View;
            View.FOV=Camera->FieldOfView;
            View.AspectRatio=Camera->AspectRatio;
            if (Camera->bOverrideAspectRatioAxisConstraint) View.AspectRatioAxisConstraint=Camera->AspectRatioAxisConstraint;
            FSceneViewProjectionData Projection;
            const FIntRect ViewRect(0,0,Size.X,Size.Y);
            Projection.SetViewRectangle(ViewRect);
            FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View,Player->AspectRatioAxisConstraint,ViewRect,Projection);
            const FVector2D Scale(Projection.ProjectionMatrix.M[0][0],Projection.ProjectionMatrix.M[1][1]);
            Data.RaceOffscreenDirection=FactoryFPVRaceScreen::OffscreenDirection(Direction,Scale,
                static_cast<double>(Size.X)/Size.Y,Settings->LensStrength);
        }
    }
    Data.Receiver=InputMode==TEXT("keyboard") ? TEXT("KEY") : (InputMode==TEXT("gamepad") ? TEXT("PAD") : TEXT("USB"));
    Data.Warning=Warning.IsEmpty() ? (RecoveryWarning.IsEmpty() ? Notice : RecoveryWarning) : Warning;
    Data.Debug=Debug;
    Overlay->SetTelemetry(Data);
}

FString AFactoryFPVDrone::RecoveryNotice(const TCHAR* Reason,const TCHAR* Action) const {
    return FString::Printf(TEXT("%s - %s recover/%s or %s return"),Reason,
        *FFactoryFPVKeyboardShortcuts::KeyLabel(Shortcuts.Recover),Action,
        *FFactoryFPVKeyboardShortcuts::KeyLabel(Shortcuts.Return));
}

bool AFactoryFPVDrone::SimulateFlight(const factoryfpv::Input& Input, double Step) {
    Damage.step(Step);
    Flight.parameters.maxThrust=Flight.parameters.mass*Flight.parameters.gravity*NominalThrustToWeight*Damage.powerFraction();
    if (Damage.destroyed()) { Flight.disarm(); Notice=RecoveryNotice(TEXT("FRAME LOST"),TEXT("repair")); }
    Battery.step(Step,Flight.armed,Flight.thrust);
    if (Battery.empty()) { Flight.disarm(); Notice=RecoveryNotice(TEXT("SIM BAT EMPTY"),TEXT("recharge")); }
    // Cutoffs precede integration. A later collision can disarm after a valid
    // finish crossing, so preserve eligibility at this precise point.
    const bool WasScoringEligible=Input.valid() && Flight.armed && !Damage.destroyed();
    Flight.step(Input, Step);
    const FQuat Rotation = FRotationMatrix::MakeFromXZ(ToUE(Flight.forward), ToUE(Flight.up)).ToQuat();
    const FVector Delta = ToUE(Flight.position) * 100. - GetActorLocation();
    FHitResult Hit;
    Movement->bAdjustedForPenetration=false;
    Movement->SafeMoveUpdatedComponent(Delta, Rotation, true, Hit);
    const auto Position = ToCore(GetActorLocation() / 100.);
    if (Hit.bStartPenetrating) {
        // SafeMove already attempted depenetration. Remaining overlap needs a reset.
        Flight.impact(Position);
        Notice = FString::Printf(TEXT("Drone obstructed. %s resets to the launch surface."),
            *FFactoryFPVKeyboardShortcuts::KeyLabel(Shortcuts.Recover));
    } else if (Hit.bBlockingHit) {
        // Collision response discards velocity on hard contacts; capture its
        // normal approach energy first. Tangential motion does not break props.
        const auto Normal=ToCore(Hit.Normal);
        Damage.reportImpact(FMath::Max(0.,-Flight.velocity.dot(Normal)),Flight.parameters.mass);
        Flight.contact(Position, ToCore(Hit.Normal));
        Flight.parameters.maxThrust=Flight.parameters.mass*Flight.parameters.gravity*NominalThrustToWeight*Damage.powerFraction();
        if (Damage.destroyed()) { Flight.disarm(); Notice=RecoveryNotice(TEXT("FRAME LOST"),TEXT("repair")); }
    }
    // Depenetration can change position even when the retried sweep is clear.
    Flight.position = Position;
    return WasScoringEligible && !Movement->bAdjustedForPenetration && !Hit.bStartPenetrating;
}
