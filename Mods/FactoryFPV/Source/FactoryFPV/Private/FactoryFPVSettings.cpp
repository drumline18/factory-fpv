#include "FactoryFPVSettings.h"
#include "FlightCore.h"
#include "UObject/UnrealType.h"

namespace {
const TCHAR* const RadioBindingNames[]={TEXT("Arm"),TEXT("Return to pioneer"),TEXT("Recover drone"),TEXT("Toggle flight mode")};

float FiniteClamp(float Value, float Minimum, float Maximum, float Fallback) {
    return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
}
float SanitizeOptionalRate(float Value) {
    return Value>0.f ? FiniteClamp(Value,120.f,1200.f,0.f) : 0.f;
}
float SanitizeCenterRate(float Value,float MaxRate) {
    return Value>0.f ? FiniteClamp(Value,10.f,MaxRate,0.f) : 0.f;
}
bool ValidAxis(const FFactoryFPVRadioAxis& Axis) {
    return Axis.Channel >= 0 && Axis.Channel < 8 &&
        factoryfpv::Calibration{Axis.Range.X, Axis.Range.Y, Axis.Range.Z, Axis.bReverse}.valid();
}
FString TuningName(FString Name) {
    FString Printable;
    for (TCHAR Character:Name) if (Character>=TEXT(' ') && Character!=127) Printable.AppendChar(Character);
    return Printable.TrimStartAndEnd().Left(40);
}
void SanitizeTuning(FFactoryFPVTuningPreset& Preset) {
    Preset.Name=TuningName(Preset.Name);
    Preset.CameraTilt=FiniteClamp(Preset.CameraTilt,-10.f,65.f,25.f);
    Preset.CameraFov=FiniteClamp(Preset.CameraFov,60.f,140.f,110.f);
    Preset.LensStrength=FiniteClamp(Preset.LensStrength,0.f,1.f,0.f);
    Preset.FrameSizeCm=FiniteClamp(Preset.FrameSizeCm,15.f,100.f,24.f);
    Preset.MassKg=FiniteClamp(Preset.MassKg,.1f,5.f,.65f);
    Preset.ThrustToWeight=FiniteClamp(Preset.ThrustToWeight,1.2f,10.f,4.08f);
    Preset.ThrottleExpo=FiniteClamp(Preset.ThrottleExpo,0.f,.8f,0.f);
    Preset.AcroRate=FiniteClamp(Preset.AcroRate,120.f,1200.f,600.f);
    Preset.PitchRate=SanitizeOptionalRate(Preset.PitchRate);
    Preset.YawRate=SanitizeOptionalRate(Preset.YawRate);
    Preset.RollCenterRate=SanitizeCenterRate(Preset.RollCenterRate,Preset.AcroRate);
    Preset.PitchCenterRate=SanitizeCenterRate(Preset.PitchCenterRate,
        static_cast<float>(factoryfpv::rateOrLinked(Preset.PitchRate,Preset.AcroRate)));
    Preset.YawCenterRate=SanitizeCenterRate(Preset.YawCenterRate,
        static_cast<float>(factoryfpv::rateOrLinked(Preset.YawRate,Preset.AcroRate)));
    Preset.Expo=FiniteClamp(Preset.Expo,0.f,.85f,.3f);
    Preset.Deadzone=FiniteClamp(Preset.Deadzone,0.f,.15f,.03f);
    Preset.PropVolume=FiniteClamp(Preset.PropVolume,0.f,1.f,.5f);
    Preset.AngleLimit=FiniteClamp(Preset.AngleLimit,5.f,80.f,45.f);
    Preset.LevelResponse=FiniteClamp(Preset.LevelResponse,.5f,20.f,6.f);
    Preset.BatteryCells=FMath::Clamp(Preset.BatteryCells,1,12);
    Preset.BatteryCapacityMah=FiniteClamp(Preset.BatteryCapacityMah,50,50000,1500);
    Preset.BatteryMaxPowerWatts=FiniteClamp(Preset.BatteryMaxPowerWatts,10,5000,600);
    Preset.CrashToleranceJoules=FiniteClamp(Preset.CrashToleranceJoules,5,200,30);
}
bool BuiltInName(const FString& Name) {
    return UFactoryFPVSettings::BuiltInTunings().ContainsByPredicate([&](const auto& Item) {
        return Item.Name.Equals(Name,ESearchCase::IgnoreCase);
    });
}

std::array<int,4> FlightChannels(const FFactoryFPVRadioProfile& Profile) {
    return {Profile.Roll.Channel,Profile.Pitch.Channel,Profile.Yaw.Channel,Profile.Throttle.Channel};
}

bool SameBinding(const FFactoryFPVArmBinding& A,const FFactoryFPVArmBinding& B) {
    return A.Kind==B.Kind && (A.Kind==0 || (A.Channel==B.Channel && A.Off==B.Off && A.On==B.On));
}

bool SameBindingConfiguration(const FFactoryFPVRadioProfile& A,const FFactoryFPVRadioProfile& B) {
    return FlightChannels(A)==FlightChannels(B) && SameBinding(A.Arm,B.Arm) &&
        SameBinding(A.ReturnControl,B.ReturnControl) && SameBinding(A.RecoverControl,B.RecoverControl) &&
        SameBinding(A.ModeControl,B.ModeControl);
}

bool RadioBindingsValid(const factoryfpv::ArmBinding& Arm,const std::array<factoryfpv::ArmBinding,factoryfpv::RadioActionCount>& Actions,
    const std::array<int,4>& Channels,FString& Error) {
    const auto Result=factoryfpv::validateRadioBindings(Arm,Actions,Channels);
    Error.Reset();
    if (Result.valid()) return true;
    if (Result.issue==factoryfpv::RadioBindingIssue::Invalid)
        Error=FString::Printf(TEXT("%s has an invalid radio binding. Learn it again or clear it."),RadioBindingNames[Result.first]);
    else if (Result.issue==factoryfpv::RadioBindingIssue::FlightAxis)
        Error=FString::Printf(TEXT("%s uses a flight axis. Choose a separate switch, button or auxiliary channel."),RadioBindingNames[Result.first]);
    else
        Error=FString::Printf(TEXT("%s and %s use the same radio control. Choose separate controls or clear one binding."),
            RadioBindingNames[Result.first],RadioBindingNames[Result.second]);
    return false;
}
}

FFactoryFPVRadioProfile UFactoryFPVSettings::CurrentProfile() const {
    FFactoryFPVRadioProfile Profile;
    Profile.DeviceId = RadioDeviceId;
    Profile.Arm=RadioArm;
    Profile.ReturnControl=RadioReturn;
    Profile.RecoverControl=RadioRecover;
    Profile.ModeControl=RadioMode;
    Profile.Roll = {RadioRollAxis, RadioRollCalibration, ReverseRoll};
    Profile.Pitch = {RadioPitchAxis, RadioPitchCalibration, ReversePitch};
    Profile.Yaw = {RadioYawAxis, RadioYawCalibration, ReverseYaw};
    Profile.Throttle = {RadioThrottleAxis, RadioThrottleCalibration, ReverseThrottle};
    if (const auto* Stored = RadioProfiles.FindByPredicate([&](const auto& Item) {
        return Item.DeviceId.Equals(RadioDeviceId, ESearchCase::IgnoreCase);
    })) Profile.DisplayName = Stored->DisplayName;
    return Profile;
}

void UFactoryFPVSettings::SetProfile(const FFactoryFPVRadioProfile& Profile) {
    RadioDeviceId = Profile.DeviceId;
    RadioArm=Profile.Arm;
    RadioReturn=Profile.ReturnControl;
    RadioRecover=Profile.RecoverControl;
    RadioMode=Profile.ModeControl;
    RadioRollAxis = Profile.Roll.Channel;
    RadioPitchAxis = Profile.Pitch.Channel;
    RadioYawAxis = Profile.Yaw.Channel;
    RadioThrottleAxis = Profile.Throttle.Channel;
    RadioRollCalibration = Profile.Roll.Range;
    RadioPitchCalibration = Profile.Pitch.Range;
    RadioYawCalibration = Profile.Yaw.Range;
    RadioThrottleCalibration = Profile.Throttle.Range;
    ReverseRoll = Profile.Roll.bReverse;
    ReversePitch = Profile.Pitch.bReverse;
    ReverseYaw = Profile.Yaw.bReverse;
    ReverseThrottle = Profile.Throttle.bReverse;
    if (Profile.DeviceId.IsEmpty()) return;
    auto* Stored = RadioProfiles.FindByPredicate([&](const auto& Item) {
        return Item.DeviceId.Equals(Profile.DeviceId, ESearchCase::IgnoreCase);
    });
    if (Stored) *Stored = Profile;
    else RadioProfiles.Add(Profile);
}

void UFactoryFPVSettings::SelectRadio(const FString& DeviceId, const FString& DisplayName) {
    SetProfile(CurrentProfile());
    FFactoryFPVRadioProfile Selected;
    if (const auto* Stored = RadioProfiles.FindByPredicate([&](const auto& Item) {
        return Item.DeviceId.Equals(DeviceId, ESearchCase::IgnoreCase);
    })) Selected = *Stored;
    Selected.DeviceId = DeviceId;
    Selected.DisplayName = DisplayName;
    SetProfile(Selected);
}

bool UFactoryFPVSettings::IsRadioCalibrated() const {
    const auto Profile = CurrentProfile();
    if (Profile.DeviceId.IsEmpty() || !ValidAxis(Profile.Roll) || !ValidAxis(Profile.Pitch) ||
        !ValidAxis(Profile.Yaw) || !ValidAxis(Profile.Throttle)) return false;
    const TSet<int32> Channels{Profile.Roll.Channel, Profile.Pitch.Channel, Profile.Yaw.Channel, Profile.Throttle.Channel};
    return Channels.Num() == 4;
}

std::array<factoryfpv::ArmBinding,factoryfpv::RadioActionCount> UFactoryFPVSettings::RadioActionBindings() const {
    return {RadioReturn.Native(),RadioRecover.Native(),RadioMode.Native()};
}

bool UFactoryFPVSettings::AreRadioBindingsValid(FString& Error) const {
    return RadioBindingsValid(RadioArm.Native(),RadioActionBindings(),
        {RadioRollAxis,RadioPitchAxis,RadioYawAxis,RadioThrottleAxis},Error);
}

bool UFactoryFPVSettings::AreEditedRadioBindingsValid(const UFactoryFPVSettings& Baseline,FString& Error) const {
    const auto Active=CurrentProfile();
    const auto SavedActive=Baseline.CurrentProfile();
    const auto Validate=[&](const FFactoryFPVRadioProfile& Profile,bool bActive) {
        if (RadioBindingsValid(Profile.Arm.Native(),{Profile.ReturnControl.Native(),Profile.RecoverControl.Native(),Profile.ModeControl.Native()},
            FlightChannels(Profile),Error)) return true;
        // The selected fields are authoritative for older configs. Switching
        // radios copies those fields into the profile array in the draft.
        const auto* Saved=Profile.DeviceId.Equals(SavedActive.DeviceId,ESearchCase::IgnoreCase) ? &SavedActive :
            Baseline.RadioProfiles.FindByPredicate([&](const auto& Candidate) { return Profile.DeviceId.Equals(Candidate.DeviceId,ESearchCase::IgnoreCase); });
        if (Saved && SameBindingConfiguration(Profile,*Saved)) {
            Error.Reset();
            return true;
        }
        if (bActive) Error+=TEXT(" Clear or relearn the binding on the Radio tab before saving.");
        else {
            const FString& Name=Profile.DisplayName.IsEmpty() ? Profile.DeviceId : Profile.DisplayName;
            Error=FString::Printf(TEXT("%s: %s Connect and select this radio to clear or relearn the binding before saving."),*Name,*Error);
        }
        return false;
    };
    if (!Validate(Active,true)) return false;
    for (const auto& Profile:RadioProfiles) {
        // SaveSettings replaces the selected cache entry with the active fields.
        if (!Profile.DeviceId.Equals(Active.DeviceId,ESearchCase::IgnoreCase) && !Validate(Profile,false)) return false;
    }
    return true;
}

bool UFactoryFPVSettings::AreRadioBindingControlsAvailable(const std::array<factoryfpv::RadioAxisInfo,8>& Axes,
    const std::array<bool,128>& Buttons,FString& Error) const {
    Error.Reset();
    const auto Actions=RadioActionBindings();
    const auto Arm=RadioArm.Native();
    for (int32 Index=0;Index<UE_ARRAY_COUNT(RadioBindingNames);++Index) {
        const auto& Binding=Index==0 ? Arm : Actions[Index-1];
        if (Binding.kind==0 || Binding.available(Axes,Buttons)) continue;
        Error=FString::Printf(TEXT("%s uses a USB control this radio is not exposing. Check the radio's USB joystick channels and Refresh, or learn or clear this binding on the Radio tab."),RadioBindingNames[Index]);
        return false;
    }
    return true;
}

FFactoryFPVTuningPreset UFactoryFPVSettings::CaptureTuning(FString Name) const {
    FFactoryFPVTuningPreset Preset;
    Preset.Name=MoveTemp(Name);
    Preset.CameraTilt=CameraTilt; Preset.CameraFov=CameraFov;
    Preset.LensStrength=LensStrength;
    Preset.FrameSizeCm=FrameSizeCm; Preset.MassKg=MassKg; Preset.ThrustToWeight=ThrustToWeight;
    Preset.ThrottleExpo=ThrottleExpo;
    Preset.AcroRate=AcroRate; Preset.PitchRate=PitchRate; Preset.YawRate=YawRate;
    Preset.RollCenterRate=RollCenterRate; Preset.PitchCenterRate=PitchCenterRate; Preset.YawCenterRate=YawCenterRate;
    Preset.Expo=Expo; Preset.Deadzone=Deadzone;
    Preset.bShowFrameInFPV=bShowFrameInFPV; Preset.PropVolume=PropVolume;
    Preset.bAngleMode=bAngleMode; Preset.AngleLimit=AngleLimit; Preset.LevelResponse=LevelResponse;
    Preset.bSimulateBattery=bSimulateBattery; Preset.BatteryCells=BatteryCells;
    Preset.BatteryCapacityMah=BatteryCapacityMah; Preset.BatteryMaxPowerWatts=BatteryMaxPowerWatts;
    Preset.bCrashDamageEnabled=bCrashDamageEnabled; Preset.CrashToleranceJoules=CrashToleranceJoules;
    SanitizeTuning(Preset);
    return Preset;
}

void UFactoryFPVSettings::ApplyTuning(const FFactoryFPVTuningPreset& Preset) {
    auto Clean=Preset;
    SanitizeTuning(Clean);
    CameraTilt=Clean.CameraTilt; CameraFov=Clean.CameraFov;
    LensStrength=Clean.LensStrength;
    FrameSizeCm=Clean.FrameSizeCm; MassKg=Clean.MassKg; ThrustToWeight=Clean.ThrustToWeight;
    ThrottleExpo=Clean.ThrottleExpo;
    AcroRate=Clean.AcroRate; PitchRate=Clean.PitchRate; YawRate=Clean.YawRate;
    RollCenterRate=Clean.RollCenterRate; PitchCenterRate=Clean.PitchCenterRate; YawCenterRate=Clean.YawCenterRate;
    Expo=Clean.Expo; Deadzone=Clean.Deadzone;
    bShowFrameInFPV=Clean.bShowFrameInFPV; PropVolume=Clean.PropVolume;
    bAngleMode=Clean.bAngleMode; AngleLimit=Clean.AngleLimit; LevelResponse=Clean.LevelResponse;
    bSimulateBattery=Clean.bSimulateBattery; BatteryCells=Clean.BatteryCells;
    BatteryCapacityMah=Clean.BatteryCapacityMah; BatteryMaxPowerWatts=Clean.BatteryMaxPowerWatts;
    bCrashDamageEnabled=Clean.bCrashDamageEnabled; CrashToleranceJoules=Clean.CrashToleranceJoules;
}

TArray<FFactoryFPVTuningPreset> UFactoryFPVSettings::BuiltInTunings() {
    FFactoryFPVTuningPreset Cruiser;
    Cruiser.Name=TEXT("Factory cruiser"); Cruiser.bAngleMode=true;
    Cruiser.CameraTilt=10; Cruiser.CameraFov=100; Cruiser.ThrustToWeight=2.6f;
    Cruiser.AcroRate=300; Cruiser.Expo=.35f; Cruiser.Deadzone=.04f;
    Cruiser.PropVolume=.35f; Cruiser.AngleLimit=35; Cruiser.LevelResponse=5;
    FFactoryFPVTuningPreset Freestyle;
    Freestyle.Name=TEXT("Acro freestyle");
    FFactoryFPVTuningPreset Micro;
    Micro.Name=TEXT("Micro explorer"); Micro.bAngleMode=true;
    Micro.CameraTilt=15; Micro.FrameSizeCm=15; Micro.MassKg=.18f;
    Micro.ThrustToWeight=3.2f; Micro.AcroRate=450; Micro.Expo=.35f; Micro.PropVolume=.35f;
    Micro.BatteryCells=2; Micro.BatteryCapacityMah=450; Micro.BatteryMaxPowerWatts=180;
    return {Cruiser,Freestyle,Micro};
}

bool UFactoryFPVSettings::StoreTuningPreset(FString Name) {
    Name=TuningName(MoveTemp(Name));
    if (Name.IsEmpty() || BuiltInName(Name)) return false;
    Sanitize();
    auto* Stored=TuningPresets.FindByPredicate([&](const auto& Item) { return Item.Name.Equals(Name,ESearchCase::IgnoreCase); });
    if (Stored) { *Stored=CaptureTuning(MoveTemp(Name)); return true; }
    if (TuningPresets.Num()>=16) return false;
    TuningPresets.Add(CaptureTuning(MoveTemp(Name)));
    return true;
}

bool UFactoryFPVSettings::RemoveTuningPreset(FString Name) {
    Name=TuningName(MoveTemp(Name));
    if (Name.IsEmpty()) return false;
    return TuningPresets.RemoveAll([&](const auto& Item) {
        return TuningName(Item.Name).Equals(Name,ESearchCase::IgnoreCase);
    })>0;
}

const TArray<FKey>& FFactoryFPVKeyboardShortcuts::FunctionKeys() {
    // The game viewport consumes F11 for fullscreen before the input stack sees it.
    static const TArray<FKey> Keys{EKeys::F1,EKeys::F2,EKeys::F3,EKeys::F4,EKeys::F5,EKeys::F6,
        EKeys::F7,EKeys::F8,EKeys::F9,EKeys::F10,EKeys::F12};
    return Keys;
}

void FFactoryFPVKeyboardShortcuts::Normalize() {
    const auto& Keys=FunctionKeys();
    if (!Keys.Contains(Hangar) || !Keys.Contains(Arm) || !Keys.Contains(Recover) || !Keys.Contains(Return) ||
        Hangar==Arm || Hangar==Recover || Hangar==Return || Arm==Recover || Arm==Return || Recover==Return) {
        *this=FFactoryFPVKeyboardShortcuts();
    }
}

void UFactoryFPVSettings::PostInitProperties() {
    Super::PostInitProperties();
    KeyboardShortcuts.Normalize();
    OsdLayout.Normalize();
}

void UFactoryFPVSettings::Sanitize() {
    ApplyTuning(CaptureTuning(FString()));
    KeyboardShortcuts.Normalize();
    OsdLayout.Normalize();
    if (PreferredInputMode!=TEXT("keyboard") && PreferredInputMode!=TEXT("gamepad")) PreferredInputMode=TEXT("radio");
    OsdTextScale=FiniteClamp(OsdTextScale,.7f,1.3f,1.f);
    OsdOpacity=FiniteClamp(OsdOpacity,.3f,1.f,1.f);
    FlightCueVolume=FiniteClamp(FlightCueVolume,0.f,1.f,.45f);
    TArray<FFactoryFPVTuningPreset> Clean;
    for (auto Preset:TuningPresets) {
        SanitizeTuning(Preset);
        if (Preset.Name.IsEmpty() || BuiltInName(Preset.Name)) continue;
        auto* Existing=Clean.FindByPredicate([&](const auto& Item) { return Item.Name.Equals(Preset.Name,ESearchCase::IgnoreCase); });
        if (Existing) *Existing=MoveTemp(Preset);
        else if (Clean.Num()<16) Clean.Add(MoveTemp(Preset));
    }
    TuningPresets=MoveTemp(Clean);
}

void UFactoryFPVSettings::SaveSettings() {
    Sanitize();
    SetProfile(CurrentProfile());
    SaveConfig();
}

void UFactoryFPVSettings::ApplyFrom(const UFactoryFPVSettings& Other) {
    for (TFieldIterator<FProperty> Property(GetClass()); Property; ++Property) {
        if (Property->HasAnyPropertyFlags(CPF_Config)) {
            Property->CopyCompleteValue_InContainer(this, &Other);
        }
    }
    SaveSettings();
}
