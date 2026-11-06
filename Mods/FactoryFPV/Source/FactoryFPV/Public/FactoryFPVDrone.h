#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/MovementComponent.h"
#include "InputCoreTypes.h"
#include "FlightCore.h"
#include "DroneBattery.h"
#include "DroneDamage.h"
#include "FactoryFPVSettings.h"
#include "FactoryFPVInputLease.h"
#include "FactoryFPVDrone.generated.h"

class UCameraComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USphereComponent;
class USceneComponent;
class UWorldPartitionStreamingSourceComponent;
class APlayerController;
class APlayerCameraManager;
class AFGHUD;
class AFactoryFPVPioneerAnchor;
class AFactoryFPVPilotVisual;
class UFactoryFPVPropAudioComponent;
class UFactoryFPVFlightCueAudioComponent;
enum class EFactoryFPVFlightCue : uint8;
class UFactoryFPVLensComponent;
class UFactoryFPVRaceComponent;
class APawn;
class UInputComponent;
class UGameViewportClient;
class SFactoryFPVOSD;
class SWidget;
namespace factoryfpv { class WindowsRadio; }

// The flight core supplies motion. Unreal resolves sweeps and initial overlaps.
UCLASS()
class FACTORYFPV_API UFactoryFPVMovementComponent : public UMovementComponent
{
    GENERATED_BODY()
public:
    UFactoryFPVMovementComponent();
    bool bAdjustedForPenetration=false;
protected:
    virtual bool ResolvePenetrationImpl(const FVector& Adjustment,const FHitResult& Hit,const FQuat& NewRotation) override;
};

UCLASS(NotPlaceable, Transient)
class FACTORYFPV_API AFactoryFPVDrone : public AActor
{
    GENERATED_BODY()
public:
    AFactoryFPVDrone();
    static AFactoryFPVDrone* FindActive(UWorld* World);
    static AFactoryFPVDrone* Launch(APlayerController* Controller, FName Mode, FString& Error, int32 PracticeCourse=0);
    static bool CheckGamepadForLaunch(const APlayerController* Controller,FString& Error,FInputDeviceId* SelectedDevice=nullptr);
    void ExitDrone();
    bool StartSession(APlayerController* Controller, FName Mode, FString& Error, int32 PracticeCourse=0);
    bool IsFlightArmed() const { return Flight.armed; }
    bool IsFlightInputSuspended() const { return IsFlightInputBlocked(); }
    bool IsFlightOverlayVisible() const;
    virtual void Tick(float DeltaSeconds) override;
    virtual void CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FFactoryFPVGroundTakeoffTest;
    friend class FFactoryFPVOpticsTest;
    friend class FFactoryFPVLifecycleInputTest;
    friend class FFactoryFPVBatteryIntegrationTest;
    friend class FFactoryFPVDamageIntegrationTest;
    friend class FFactoryFPVShippingSimulationQA;
    friend class FFactoryFPVShippingSettingsQA;
    friend class FFactoryFPVShippingPropAudioQA;
    friend class FFactoryFPVShippingVerticalFlight;
    friend class FFactoryFPVShippingArmedSaveFlight;
    friend class FFactoryFPVSessionLifecycleTest;
    friend class FFactoryFPVRadioActionsIntegrationTest;
    friend class FFactoryFPVRaceIntegrationTest;
    bool TryArmFlight(const factoryfpv::Input& Input);
    static FInputDeviceId FindLocalGamepad(const APlayerController* Controller);
    void StartGamepadMonitoring(FInputDeviceId Device);
    void OnGamepadConnectionChanged(EInputDeviceConnectionState State,FPlatformUserId User,FInputDeviceId Device);
    void OnGamepadPairingChanged(FInputDeviceId Device,FPlatformUserId NewUser,FPlatformUserId OldUser);
    void LoseGamepad();
    bool IsNativeUIBlockingFlight() const;
    bool IsFlightInputBlocked() const;
    void OnApplicationActivationChanged(bool bActive);
    void SuspendFlightInput();
    void UpdateHudForNativeUI(bool NativeUIBlocked);
    void ApplyFlightSettings(const UFactoryFPVSettings& Settings);
    FString RecoveryNotice(const TCHAR* Reason,const TCHAR* Action) const;
    void UpdateOsd(const factoryfpv::Input& Input, const FString& Warning=FString(), const FString& Debug=FString());
    void UpdatePropAudio();
    void PlayFlightCue(EFactoryFPVFlightCue Cue);
    void UpdateRaceGhost();
    void CreateRaceGhost();
    void HideRaceGhost();
    UPROPERTY() TObjectPtr<USphereComponent> Body;
    UPROPERTY() TObjectPtr<USceneComponent> Frame;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> ScoutChassis;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> RaceGhostSurface;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RaceGhost;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RaceGhostMaterial;
    UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Rotors;
    UPROPERTY() TObjectPtr<UFactoryFPVMovementComponent> Movement;
    UPROPERTY() TObjectPtr<UCameraComponent> Camera;
    UPROPERTY() TObjectPtr<UWorldPartitionStreamingSourceComponent> Streaming;
    UPROPERTY() TObjectPtr<UInputComponent> Controls;
    UPROPERTY() TObjectPtr<AFactoryFPVPioneerAnchor> PioneerAnchor;
    UPROPERTY() TObjectPtr<AFactoryFPVPilotVisual> PilotVisual;
    UPROPERTY() TObjectPtr<UFactoryFPVPropAudioComponent> PropAudio;
    UPROPERTY() TObjectPtr<UFactoryFPVFlightCueAudioComponent> CueAudio;
    UPROPERTY() TObjectPtr<UFactoryFPVLensComponent> Lens;
    UPROPERTY() TObjectPtr<UFactoryFPVRaceComponent> Race;
    TArray<FVector> RacePath;
    TArray<uint8> RaceScoring;
    TWeakObjectPtr<APlayerController> Pilot;
    TWeakObjectPtr<APawn> OriginalPawn;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<APlayerCameraManager> SessionCameraManager;
    TWeakObjectPtr<AFGHUD> SessionHud;
    bool bPreviousPartialHideHud = false;
    bool bPreviousHiddenHud = false;
    bool bHudReleasedForNativeUI = false;
    float PreviousLockedFov = 0.f;
    float SessionFov = 0.f;
    TSharedPtr<factoryfpv::WindowsRadio> Radio;
    TWeakObjectPtr<UGameViewportClient> PilotViewport;
    TSharedPtr<SFactoryFPVOSD> Overlay;
    FFactoryFPVKeyboardShortcuts Shortcuts;
    TSharedPtr<SWidget> OverlayRoot;
    FString Notice;
    FString RecoveryWarning;
    factoryfpv::FlightCore Flight;
    factoryfpv::DroneBattery Battery;
    factoryfpv::DroneDamage Damage;
    double NominalThrustToWeight=4.08;
    factoryfpv::Stepper Clock;
    FVector LaunchPosition;
    double LaunchYaw = 0;
    double KeyboardThrottle = 0;
    double ArmedSeconds = 0;
    factoryfpv::Input LastInput;
    factoryfpv::RadioArmGate ArmGate;
    factoryfpv::ArmPosition ArmPosition=factoryfpv::ArmPosition::Lost;
    std::array<factoryfpv::RadioActionGate,factoryfpv::RadioActionCount> ActionGates;
    std::array<factoryfpv::ArmPosition,factoryfpv::RadioActionCount> ActionPositions{};
    FName InputMode;
    FInputDeviceId GamepadDevice=INPUTDEVICEID_NONE;
    FDelegateHandle GamepadConnectionHandle,GamepadPairingHandle;
    bool bGamepadLost=false;
    FFactoryFPVInputLease InputLease;
    FDelegateHandle ApplicationActivationHandle;
    bool bApplicationDeactivated = false;
    bool bSessionActive = false;
    factoryfpv::Input ReadControls(float DeltaSeconds);
    void UpdateRadioControlPositions(const std::array<long,8>& Axes,const std::array<bool,128>& Buttons,
        const std::array<factoryfpv::RadioAxisInfo,8>& AxisInfo,const std::array<bool,128>& ButtonPresence);
    void ResetRadioGates();
    bool ProcessRadioActions(const factoryfpv::Input& Input);
    void AdvanceActiveFlightFrame(const factoryfpv::Input& Input,float DeltaSeconds);
    bool SimulateFlight(const factoryfpv::Input& Input, double Step);
    void ArmToggle();
    void RecoverFromKey();
    void ResetDrone();
    void OpenHangar();
    void EquipmentReturn();
    void RestorePilot();
};
