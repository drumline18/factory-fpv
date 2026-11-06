#pragma once
#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "FactoryFPVOsdLayout.h"
#include "UObject/Object.h"
#include "RadioActions.h"
#include "FactoryFPVSettings.generated.h"

USTRUCT()
struct FFactoryFPVRadioAxis
{
    GENERATED_BODY()
    UPROPERTY() int32 Channel = -1;
    UPROPERTY() FVector Range = FVector::ZeroVector;
    UPROPERTY() bool bReverse = false;
};

USTRUCT()
struct FFactoryFPVArmBinding {
    GENERATED_BODY()
    UPROPERTY() int32 Kind=0;
    UPROPERTY() int32 Channel=-1;
    UPROPERTY() int32 Off=0;
    UPROPERTY() int32 On=1;
    factoryfpv::ArmBinding Native() const { return {Kind,Channel,Off,On}; }
};

USTRUCT()
struct FFactoryFPVRadioProfile
{
    GENERATED_BODY()
    UPROPERTY() FString DeviceId;
    UPROPERTY() FString DisplayName;
    UPROPERTY() FFactoryFPVRadioAxis Roll;
    UPROPERTY() FFactoryFPVRadioAxis Pitch;
    UPROPERTY() FFactoryFPVRadioAxis Yaw;
    UPROPERTY() FFactoryFPVRadioAxis Throttle;
    UPROPERTY() FFactoryFPVArmBinding Arm;
    UPROPERTY() FFactoryFPVArmBinding ReturnControl;
    UPROPERTY() FFactoryFPVArmBinding RecoverControl;
    UPROPERTY() FFactoryFPVArmBinding ModeControl;
};

USTRUCT()
struct FFactoryFPVKeyboardShortcuts {
    GENERATED_BODY()

    UPROPERTY() FKey Hangar = EKeys::F6;
    UPROPERTY() FKey Arm = EKeys::F8;
    UPROPERTY() FKey Recover = EKeys::F9;
    UPROPERTY() FKey Return = EKeys::F10;

    static const TArray<FKey>& FunctionKeys();
    static FString KeyLabel(const FKey& Key) { return Key.GetFName().ToString(); }
    void Normalize();
};

// Flight/camera presentation only. Device calibration, arm bindings and access
// permissions intentionally do not belong to a tuning preset.
USTRUCT()
struct FFactoryFPVTuningPreset {
    GENERATED_BODY()
    UPROPERTY() FString Name;
    UPROPERTY() float CameraTilt=25.f;
    UPROPERTY() float CameraFov=110.f;
    UPROPERTY() float LensStrength=0.f;
    UPROPERTY() float FrameSizeCm=24.f;
    UPROPERTY() float MassKg=.65f;
    UPROPERTY() float ThrustToWeight=4.08f;
    UPROPERTY() float ThrottleExpo=0.f;
    UPROPERTY() float AcroRate=600.f;
    UPROPERTY() float PitchRate=0.f;
    UPROPERTY() float YawRate=0.f;
    UPROPERTY() float RollCenterRate=0.f;
    UPROPERTY() float PitchCenterRate=0.f;
    UPROPERTY() float YawCenterRate=0.f;
    UPROPERTY() float Expo=.3f;
    UPROPERTY() float Deadzone=.03f;
    UPROPERTY() bool bShowFrameInFPV=false;
    UPROPERTY() float PropVolume=.5f;
    UPROPERTY() bool bAngleMode=false;
    UPROPERTY() float AngleLimit=45.f;
    UPROPERTY() float LevelResponse=6.f;
    UPROPERTY() bool bSimulateBattery=false;
    UPROPERTY() int32 BatteryCells=4;
    UPROPERTY() float BatteryCapacityMah=1500.f;
    UPROPERTY() float BatteryMaxPowerWatts=600.f;
    UPROPERTY() bool bCrashDamageEnabled=false;
    UPROPERTY() float CrashToleranceJoules=30.f;
};

UCLASS(Config=GameUserSettings)
class FACTORYFPV_API UFactoryFPVSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) TArray<FFactoryFPVRadioProfile> RadioProfiles;
    UPROPERTY(Config) TArray<FFactoryFPVTuningPreset> TuningPresets;
    UPROPERTY(Config) FName PreferredInputMode = TEXT("radio");
    UPROPERTY(Config) FFactoryFPVKeyboardShortcuts KeyboardShortcuts;
    // Existing 0.1 profiles retain their selected-device fields.
    UPROPERTY(Config) FString RadioDeviceId;
    UPROPERTY(Config) FFactoryFPVArmBinding RadioArm;
    UPROPERTY(Config) FFactoryFPVArmBinding RadioReturn;
    UPROPERTY(Config) FFactoryFPVArmBinding RadioRecover;
    UPROPERTY(Config) FFactoryFPVArmBinding RadioMode;
    // Indices 0-7 correspond to X, Y, Z, Rx, Ry, Rz, Slider 0, Slider 1.
    UPROPERTY(Config) int32 RadioRollAxis = -1;
    UPROPERTY(Config) int32 RadioPitchAxis = -1;
    UPROPERTY(Config) int32 RadioYawAxis = -1;
    UPROPERTY(Config) int32 RadioThrottleAxis = -1;
    // Raw minimum, center and maximum. A zero range requires calibration.
    UPROPERTY(Config) FVector RadioRollCalibration = FVector::ZeroVector;
    UPROPERTY(Config) FVector RadioPitchCalibration = FVector::ZeroVector;
    UPROPERTY(Config) FVector RadioYawCalibration = FVector::ZeroVector;
    UPROPERTY(Config) FVector RadioThrottleCalibration = FVector::ZeroVector;
    UPROPERTY(Config) bool ReverseRoll = false;
    UPROPERTY(Config) bool ReversePitch = false;
    UPROPERTY(Config) bool ReverseYaw = false;
    UPROPERTY(Config) bool ReverseThrottle = false;
    UPROPERTY(Config) float CameraTilt = 25.f;
    UPROPERTY(Config) float CameraFov = 110.f;
    // Optional barrel/fisheye look. Zero bypasses the camera-local effect.
    UPROPERTY(Config) float LensStrength = 0.f;
    UPROPERTY(Config) float FrameSizeCm = 24.f;
    UPROPERTY(Config) float MassKg = 0.65f;
    UPROPERTY(Config) float ThrustToWeight = 4.08f;
    UPROPERTY(Config) float ThrottleExpo = 0.f;
    UPROPERTY(Config) float AcroRate = 600.f;
    // Zero keeps the axis linked to the roll rate used by older profiles.
    UPROPERTY(Config) float PitchRate = 0.f;
    UPROPERTY(Config) float YawRate = 0.f;
    // Zero keeps the original Expo curve. A custom value is the rate slope after the deadzone.
    UPROPERTY(Config) float RollCenterRate = 0.f;
    UPROPERTY(Config) float PitchCenterRate = 0.f;
    UPROPERTY(Config) float YawCenterRate = 0.f;
    UPROPERTY(Config) float Expo = 0.3f;
    UPROPERTY(Config) float Deadzone = 0.03f;
    UPROPERTY(Config) bool bShowFrameInFPV = false;
    UPROPERTY(Config) float PropVolume = 0.5f;
    UPROPERTY(Config) float FlightCueVolume = .45f;
    UPROPERTY(Config) bool bAngleMode = false;
    UPROPERTY(Config) float AngleLimit = 45.f;
    UPROPERTY(Config) float LevelResponse = 6.f;
    // Gate flight without equipment while keeping configuration accessible.
    UPROPERTY(Config) bool bAllowSandboxAccess = true;
    UPROPERTY(Config) bool bSimulateBattery=false;
    UPROPERTY(Config) int32 BatteryCells=4;
    UPROPERTY(Config) float BatteryCapacityMah=1500.f;
    UPROPERTY(Config) float BatteryMaxPowerWatts=600.f;
    // Optional structural wear applies only to the temporary drone, never the world or pioneer.
    UPROPERTY(Config) bool bCrashDamageEnabled=false;
    UPROPERTY(Config) float CrashToleranceJoules=30.f;
    UPROPERTY(Config) bool bOsdCrosshair=true;
    UPROPERTY(Config) bool bOsdSpeed=true;
    UPROPERTY(Config) bool bOsdAltitude=true;
    UPROPERTY(Config) bool bOsdHome=true;
    UPROPERTY(Config) bool bOsdTimer=true;
    UPROPERTY(Config) bool bOsdThrottle=true;
    UPROPERTY(Config) bool bShowRaceGhost=true;
    UPROPERTY(Config) float OsdTextScale=1.f;
    UPROPERTY(Config) float OsdOpacity=1.f;
    UPROPERTY(Config) FFactoryFPVOsdLayout OsdLayout;

    bool IsRadioCalibrated() const;
    std::array<factoryfpv::ArmBinding,factoryfpv::RadioActionCount> RadioActionBindings() const;
    bool AreRadioBindingsValid(FString& Error) const;
    bool AreEditedRadioBindingsValid(const UFactoryFPVSettings& Baseline,FString& Error) const;
    bool AreRadioBindingControlsAvailable(const std::array<factoryfpv::RadioAxisInfo,8>& Axes,
        const std::array<bool,128>& Buttons,FString& Error) const;
    FFactoryFPVRadioProfile CurrentProfile() const;
    void SelectRadio(const FString& DeviceId, const FString& DisplayName);
    void SetProfile(const FFactoryFPVRadioProfile& Profile);
    FFactoryFPVTuningPreset CaptureTuning(FString Name) const;
    void ApplyTuning(const FFactoryFPVTuningPreset& Preset);
    bool StoreTuningPreset(FString Name);
    bool RemoveTuningPreset(FString Name);
    static TArray<FFactoryFPVTuningPreset> BuiltInTunings();
    virtual void PostInitProperties() override;
    void Sanitize();
    void SaveSettings();
    void ApplyFrom(const UFactoryFPVSettings& Other);
};
