#include "FactoryFPVSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "FactoryFPVPanel.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVSettingsTest, "FactoryFPV.Settings.ProfilesAndBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVSettingsTest::RunTest(const FString&) {
    auto* Settings = NewObject<UFactoryFPVSettings>();
    Settings->RadioProfiles.Reset();
    Settings->RadioDeviceId = TEXT("legacy-radio");
    Settings->RadioRollAxis=0; Settings->RadioPitchAxis=1;
    Settings->RadioYawAxis=3; Settings->RadioThrottleAxis=2;
    Settings->RadioRollCalibration=Settings->RadioPitchCalibration=
        Settings->RadioYawCalibration=Settings->RadioThrottleCalibration=FVector(0,32767,65535);
    Settings->ReversePitch=true;
    Settings->RadioArm.Kind=2;Settings->RadioArm.Channel=4;Settings->RadioArm.Off=65535;Settings->RadioArm.On=0;
    TestTrue(TEXT("Existing calibration remains valid without profile migration"), Settings->IsRadioCalibrated());
    Settings->SelectRadio(TEXT("new-radio"), TEXT("New device"));
    TestFalse(TEXT("A different radio requires its own calibration"), Settings->IsRadioCalibrated());
    TestEqual(TEXT("New device does not inherit arm switch"),Settings->RadioArm.Kind,0);
    Settings->SelectRadio(TEXT("LEGACY-RADIO"), TEXT("Original device"));
    TestTrue(TEXT("Returning to the original radio restores calibration"), Settings->IsRadioCalibrated());
    TestTrue(TEXT("Restores inversion"), Settings->ReversePitch);
    TestEqual(TEXT("Returning device restores arm direction"),Settings->RadioArm.Off,65535);
    TestEqual(TEXT("Profiles have no case-insensitive duplicates"), Settings->RadioProfiles.Num(), 2);
    Settings->RadioYawAxis=Settings->RadioRollAxis;
    TestFalse(TEXT("Duplicate channels cannot fly"), Settings->IsRadioCalibrated());
    Settings->CameraTilt=std::numeric_limits<float>::quiet_NaN();
    Settings->CameraFov=500; Settings->FrameSizeCm=-20;
    Settings->MassKg=0; Settings->ThrustToWeight=100; Settings->Expo=-1;
    Settings->Sanitize();
    TestEqual(TEXT("Invalid camera tilt uses default"),Settings->CameraTilt,25.f);
    TestEqual(TEXT("FOV capped"),Settings->CameraFov,140.f);
    TestEqual(TEXT("Collision size remains positive"),Settings->FrameSizeCm,15.f);
    TestEqual(TEXT("Mass remains positive"),Settings->MassKg,.1f);
    TestEqual(TEXT("Power capped"),Settings->ThrustToWeight,10.f);
    TestEqual(TEXT("Expo bounded"),Settings->Expo,0.f);
    // Exercise Unreal's actual INI serialization, including nested profile arrays.
    // The explicit temporary filename and false copy flag preserve user settings.
    Settings->RadioYawAxis=3;
    Settings->SetProfile(Settings->CurrentProfile());
    const FString ConfigPath=FPaths::ConvertRelativePathToFull(
        FPaths::ProjectSavedDir()/FString::Printf(TEXT("FactoryFPV/ConfigTest-%llu/GameUserSettings.ini"),FPlatformTime::Cycles64()));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath),true);
    GConfig->SetBool(TEXT("SectionsToSave"),TEXT("bCanSaveAllSections"),true,ConfigPath);
    Settings->SaveConfig(CPF_Config,*ConfigPath,GConfig,false);
    GConfig->Flush(false,ConfigPath);
    FString SavedText;
    TestTrue(TEXT("INI exists on disk"),FFileHelper::LoadFileToString(SavedText,*ConfigPath));
    TestTrue(TEXT("INI contains serialized profiles"),SavedText.Contains(TEXT("RadioProfiles=")));
    TestTrue(TEXT("INI contains captured arm binding"),SavedText.Contains(TEXT("RadioArm=(Kind=2,Channel=4,Off=65535,On=0)")));
    GConfig->UnloadFile(ConfigPath);
    GConfig->LoadFile(ConfigPath);
    auto* Reloaded=NewObject<UFactoryFPVSettings>();
    Reloaded->LoadConfig(nullptr,*ConfigPath);
    TestEqual(TEXT("Profile array survives INI serialization"),Reloaded->RadioProfiles.Num(),2);
    TestTrue(TEXT("Selected calibration survives INI serialization"),Reloaded->IsRadioCalibrated());
    TestTrue(TEXT("Inversion survives INI serialization"),Reloaded->ReversePitch);
    TestEqual(TEXT("Arm channel survives INI serialization"),Reloaded->RadioArm.Channel,4);
    TestEqual(TEXT("Arm direction survives INI serialization"),Reloaded->RadioArm.Off,65535);
    TestEqual(TEXT("Nested profile includes arm binding"),Reloaded->CurrentProfile().Arm.Kind,2);
    TestEqual(TEXT("Camera setting survives INI serialization"),Reloaded->CameraFov,140.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVTuningPresetsTest,"FactoryFPV.Settings.TuningPresets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVTuningPresetsTest::RunTest(const FString&) {
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->RadioProfiles.Reset(); Settings->TuningPresets.Reset();
    FFactoryFPVRadioProfile Radio;
    Radio.DeviceId=TEXT("tuning-device-a");
    Radio.Roll={0,FVector(0,32767,65535),false};
    Radio.Pitch={1,FVector(0,32767,65535),true};
    Radio.Yaw={3,FVector(0,32767,65535),false};
    Radio.Throttle={2,FVector(0,32767,65535),false};
    Radio.Arm.Kind=1; Radio.Arm.Channel=7;
    Settings->SetProfile(Radio);
    Settings->bAllowSandboxAccess=false;
    const auto BuiltIns=UFactoryFPVSettings::BuiltInTunings();
    TestEqual(TEXT("Three starter tunes"),BuiltIns.Num(),3);
    TestTrue(TEXT("Cruiser is self-level"),BuiltIns[0].bAngleMode);
    TestFalse(TEXT("Freestyle retains acro"),BuiltIns[1].bAngleMode);
    TestEqual(TEXT("Micro is physically smaller"),BuiltIns[2].FrameSizeCm,15.f);
    for (const auto& Preset : BuiltIns) {
        TestTrue(TEXT("Built-in presets keep pitch and yaw linked"),Preset.PitchRate==0.f && Preset.YawRate==0.f);
    }
    Settings->ApplyTuning(BuiltIns[1]);
    TestTrue(TEXT("Applying tuning preserves selected calibrated radio"),Settings->RadioDeviceId==Radio.DeviceId && Settings->IsRadioCalibrated());
    TestEqual(TEXT("Applying tuning preserves arm switch"),Settings->RadioArm.Channel,7);
    TestTrue(TEXT("Applying tuning preserves pitch inversion"),Settings->ReversePitch);
    TestFalse(TEXT("Applying tuning cannot enable sandbox access"),Settings->bAllowSandboxAccess);
    Settings->SelectRadio(TEXT("tuning-device-b"),TEXT("Second radio"));
    Settings->ApplyTuning(BuiltIns[2]);
    TestEqual(TEXT("Tuning does not transfer first device calibration"),Settings->RadioRollAxis,-1);
    TestEqual(TEXT("Tuning does not transfer first device arm binding"),Settings->RadioArm.Kind,0);
    Settings->SelectRadio(Radio.DeviceId,TEXT("First radio"));
    TestEqual(TEXT("Both device profiles retained"),Settings->RadioProfiles.Num(),2);
    TestEqual(TEXT("Original arm switch remains after device round trip"),Settings->RadioArm.Channel,7);

    TestTrue(TEXT("Store named tune"),Settings->StoreTuningPreset(TEXT("  Personal tune  ")));
    TestEqual(TEXT("Preset name trimmed"),Settings->TuningPresets[0].Name,FString(TEXT("Personal tune")));
    auto* Draft=DuplicateObject<UFactoryFPVSettings>(Settings,GetTransientPackage());
    Draft->ApplyTuning(BuiltIns[0]);
    TestTrue(TEXT("Draft can store independently"),Draft->StoreTuningPreset(TEXT("Only in draft")));
    Draft->RemoveTuningPreset(TEXT("Personal tune"));
    TestEqual(TEXT("Cancelling draft keeps original tune collection"),Settings->TuningPresets.Num(),1);
    TestEqual(TEXT("Cancelling draft keeps original applied settings"),Settings->CameraTilt,BuiltIns[2].CameraTilt);
    Settings->CameraTilt=42;
    TestTrue(TEXT("Case-insensitive name updates existing tune"),Settings->StoreTuningPreset(TEXT(" PERSONAL TUNE ")));
    TestEqual(TEXT("Case-insensitive update does not duplicate"),Settings->TuningPresets.Num(),1);
    TestEqual(TEXT("Update captures changed tuning"),Settings->TuningPresets[0].CameraTilt,42.f);
    TestFalse(TEXT("Empty names rejected"),Settings->StoreTuningPreset(TEXT(" \t\r\n ")));
    TestFalse(TEXT("Built-in names reserved ignoring case and whitespace"),Settings->StoreTuningPreset(TEXT("  FACTORY CRUISER ")));
    TestTrue(TEXT("Long names normalized to bounded label"),Settings->StoreTuningPreset(FString::ChrN(60,TEXT('X'))));
    TestEqual(TEXT("Custom name has at most forty characters"),Settings->TuningPresets.Last().Name.Len(),40);
    TestEqual(TEXT("Control characters removed"),Settings->CaptureTuning(TEXT(" \tMy\nname\r ")).Name,FString(TEXT("Myname")));
    TestTrue(TEXT("Remove accepts case-insensitive trimmed name"),Settings->RemoveTuningPreset(TEXT(" personal TUNE ")));
    TestFalse(TEXT("Removing absent tune reports failure"),Settings->RemoveTuningPreset(TEXT("missing")));
    Settings->TuningPresets.Reset();
    for(int Index=0;Index<16;++Index) TestTrue(TEXT("Custom slot available"),Settings->StoreTuningPreset(FString::Printf(TEXT("Tune %d"),Index)));
    TestFalse(TEXT("Seventeenth custom tune rejected"),Settings->StoreTuningPreset(TEXT("Overflow")));
    Settings->CameraFov=130;
    TestTrue(TEXT("Existing tune can update at capacity"),Settings->StoreTuningPreset(TEXT(" tune 0 ")));
    TestEqual(TEXT("Capacity update preserves count"),Settings->TuningPresets.Num(),16);
    TestEqual(TEXT("Capacity update captures tuning"),Settings->TuningPresets[0].CameraFov,130.f);
    TestTrue(TEXT("Removal frees capacity"),Settings->RemoveTuningPreset(TEXT("TUNE 0")));
    TestTrue(TEXT("Freed slot can be reused"),Settings->StoreTuningPreset(TEXT("Replacement")));

    FFactoryFPVTuningPreset Malformed;
    Malformed.Name=TEXT(" Weird "); Malformed.CameraTilt=std::numeric_limits<float>::quiet_NaN();
    Malformed.CameraFov=999; Malformed.FrameSizeCm=-1; Malformed.MassKg=0;
    Malformed.ThrustToWeight=std::numeric_limits<float>::infinity(); Malformed.AcroRate=0;
    Malformed.ThrottleExpo=4.f;
    Malformed.PitchRate=std::numeric_limits<float>::infinity();
    Malformed.YawRate=2000.f;
    Malformed.RollCenterRate=1.f;
    Malformed.PitchCenterRate=std::numeric_limits<float>::infinity();
    Malformed.YawCenterRate=1300.f;
    Malformed.Expo=2; Malformed.Deadzone=2; Malformed.PropVolume=-1;
    Malformed.AngleLimit=1000; Malformed.LevelResponse=std::numeric_limits<float>::quiet_NaN();
    Settings->TuningPresets={Malformed,FFactoryFPVTuningPreset{},BuiltIns[0]};
    Settings->Sanitize();
    TestEqual(TEXT("Loaded empty/reserved names discarded"),Settings->TuningPresets.Num(),1);
    const auto Clean=Settings->TuningPresets[0];
    TestEqual(TEXT("Loaded name trimmed"),Clean.Name,FString(TEXT("Weird")));
    TestEqual(TEXT("Loaded NaN camera falls back"),Clean.CameraTilt,25.f);
    TestEqual(TEXT("Loaded FOV clamps"),Clean.CameraFov,140.f);
    TestEqual(TEXT("Loaded frame size clamps"),Clean.FrameSizeCm,15.f);
    TestEqual(TEXT("Loaded mass clamps"),Clean.MassKg,.1f);
    TestEqual(TEXT("Loaded infinite thrust falls back"),Clean.ThrustToWeight,4.08f);
    TestEqual(TEXT("Throttle expo is bounded"),Clean.ThrottleExpo,.8f);
    TestEqual(TEXT("Loaded rate clamps"),Clean.AcroRate,120.f);
    TestEqual(TEXT("Invalid optional pitch rate links to roll"),Clean.PitchRate,0.f);
    TestEqual(TEXT("Loaded independent yaw rate clamps"),Clean.YawRate,1200.f);
    TestEqual(TEXT("Enabled center sensitivity has a minimum"),Clean.RollCenterRate,10.f);
    TestEqual(TEXT("Invalid center sensitivity uses the existing curve"),Clean.PitchCenterRate,0.f);
    TestEqual(TEXT("Center sensitivity is bounded by the axis maximum"),Clean.YawCenterRate,1200.f);
    TestEqual(TEXT("Loaded expo clamps"),Clean.Expo,.85f);
    TestEqual(TEXT("Loaded deadzone clamps"),Clean.Deadzone,.15f);
    TestEqual(TEXT("Loaded volume clamps"),Clean.PropVolume,0.f);
    TestEqual(TEXT("Loaded tilt limit clamps"),Clean.AngleLimit,80.f);
    TestEqual(TEXT("Loaded response falls back"),Clean.LevelResponse,6.f);
    Malformed.Name=TEXT("WEIRD"); Malformed.CameraTilt=12;
    Settings->TuningPresets.Add(Malformed);
    for(int Index=0;Index<20;++Index) { auto Item=BuiltIns[1]; Item.Name=FString::Printf(TEXT("Loaded %d"),Index); Settings->TuningPresets.Add(Item); }
    Settings->Sanitize();
    TestEqual(TEXT("Loaded custom collection limited to sixteen"),Settings->TuningPresets.Num(),16);
    TestEqual(TEXT("Loaded duplicate names use latest value"),Settings->TuningPresets[0].CameraTilt,12.f);

    Settings->TuningPresets.Reset();
    Settings->ApplyTuning(BuiltIns[0]); Settings->bShowFrameInFPV=true; Settings->PropVolume=.27f;
    Settings->PitchRate=460.f;
    Settings->YawRate=220.f;
    Settings->RollCenterRate=150.f;
    Settings->PitchCenterRate=220.f;
    Settings->YawCenterRate=110.f;
    Settings->ThrottleExpo=.6f;
    TestTrue(TEXT("Create persistent custom tuning"),Settings->StoreTuningPreset(TEXT("Saved cruise")));
    Settings->ApplyTuning(BuiltIns[1]); Settings->CameraFov=125;
    Settings->YawRate=350.f;
    Settings->YawCenterRate=60.f;
    const FString ConfigPath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/
        FString::Printf(TEXT("FactoryFPV/PresetTest-%llu/GameUserSettings.ini"),FPlatformTime::Cycles64()));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigPath),true);
    GConfig->SetBool(TEXT("SectionsToSave"),TEXT("bCanSaveAllSections"),true,ConfigPath);
    Settings->SaveConfig(CPF_Config,*ConfigPath,GConfig,false);
    GConfig->Flush(false,ConfigPath); GConfig->UnloadFile(ConfigPath); GConfig->LoadFile(ConfigPath);
    auto* Reloaded=NewObject<UFactoryFPVSettings>(); Reloaded->LoadConfig(nullptr,*ConfigPath); Reloaded->Sanitize();
    TestEqual(TEXT("Preset array persists through INI"),Reloaded->TuningPresets.Num(),1);
    if (Reloaded->TuningPresets.Num()==1) {
        const auto Saved=Reloaded->TuningPresets[0];
        TestEqual(TEXT("Preset name persisted"),Saved.Name,FString(TEXT("Saved cruise")));
        TestTrue(TEXT("Preset flight mode persisted"),Saved.bAngleMode);
        TestEqual(TEXT("Preset camera tilt persisted"),Saved.CameraTilt,10.f);
        TestEqual(TEXT("Preset FOV persisted"),Saved.CameraFov,100.f);
        TestEqual(TEXT("Preset size persisted"),Saved.FrameSizeCm,24.f);
        TestEqual(TEXT("Preset mass persisted"),Saved.MassKg,.65f);
        TestEqual(TEXT("Preset power persisted"),Saved.ThrustToWeight,2.6f);
        TestEqual(TEXT("Preset throttle expo persisted"),Saved.ThrottleExpo,.6f);
        TestEqual(TEXT("Built-in preset uses linear throttle"),Reloaded->ThrottleExpo,0.f);
        TestEqual(TEXT("Preset rate persisted"),Saved.AcroRate,300.f);
        TestEqual(TEXT("Preset pitch rate persisted"),Saved.PitchRate,460.f);
        TestEqual(TEXT("Preset yaw rate persisted"),Saved.YawRate,220.f);
        TestEqual(TEXT("Current pitch remains linked independently of the preset"),Reloaded->PitchRate,0.f);
        TestEqual(TEXT("Current yaw rate persists independently of the preset"),Reloaded->YawRate,350.f);
        TestEqual(TEXT("Preset roll center persisted"),Saved.RollCenterRate,150.f);
        TestEqual(TEXT("Preset pitch center persisted"),Saved.PitchCenterRate,220.f);
        TestEqual(TEXT("Preset yaw center persisted"),Saved.YawCenterRate,110.f);
        TestEqual(TEXT("Built-in presets restore the original response"),Reloaded->RollCenterRate+Reloaded->PitchCenterRate,0.f);
        TestEqual(TEXT("Current yaw center persists independently of the preset"),Reloaded->YawCenterRate,60.f);
        TestEqual(TEXT("Preset expo persisted"),Saved.Expo,.35f);
        TestEqual(TEXT("Preset deadzone persisted"),Saved.Deadzone,.04f);
        TestTrue(TEXT("Preset frame visibility persisted"),Saved.bShowFrameInFPV);
        TestEqual(TEXT("Preset volume persisted"),Saved.PropVolume,.27f);
        TestEqual(TEXT("Preset angle limit persisted"),Saved.AngleLimit,35.f);
        TestEqual(TEXT("Preset level response persisted"),Saved.LevelResponse,5.f);
        TestFalse(TEXT("Current mode independent of stored preset"),Reloaded->bAngleMode);
        TestEqual(TEXT("Current FOV independent of stored preset"),Reloaded->CameraFov,125.f);
        Reloaded->ApplyTuning(Saved);
        TestEqual(TEXT("Applying the preset restores throttle expo"),Reloaded->ThrottleExpo,.6f);
        TestEqual(TEXT("Reloaded preset preserves radio identity"),Reloaded->RadioDeviceId,Radio.DeviceId);
        TestEqual(TEXT("Reloaded preset preserves arm binding"),Reloaded->RadioArm.Channel,7);
        TestFalse(TEXT("Reloaded preset preserves access policy"),Reloaded->bAllowSandboxAccess);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVSimulationSettingsTest,"FactoryFPV.Settings.SimulationAndOsdPersistence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVSimulationSettingsTest::RunTest(const FString&) {
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->TuningPresets.Reset();
    Settings->bSimulateBattery=true;Settings->BatteryCells=6;Settings->BatteryCapacityMah=2200;Settings->BatteryMaxPowerWatts=1600;
    Settings->OsdTextScale=1.25;Settings->OsdOpacity=.45;Settings->bOsdCrosshair=false;Settings->bOsdSpeed=false;
    Settings->FlightCueVolume=.38f;
    Settings->bOsdAltitude=false;Settings->bOsdHome=false;Settings->bOsdTimer=false;Settings->bOsdThrottle=false;
    TestTrue(TEXT("An occupied slot swaps with the selected readout"),
        Settings->OsdLayout.Move(EFactoryFPVOsdItem::Home,EFactoryFPVOsdSlot::BottomLeft) &&
        Settings->OsdLayout.GetSlot(EFactoryFPVOsdItem::Speed)==EFactoryFPVOsdSlot::TopCenter);
    TestTrue(TEXT("An empty slot accepts a readout"),Settings->OsdLayout.Move(EFactoryFPVOsdItem::Timer,EFactoryFPVOsdSlot::UpperRight));
    Settings->bAllowSandboxAccess=false;
    TestTrue(TEXT("Endurance preset stores simulated pack"),Settings->StoreTuningPreset(TEXT("Endurance")));
    Settings->bSimulateBattery=false;Settings->BatteryCells=2;
    Settings->ApplyTuning(Settings->TuningPresets[0]);
    TestTrue(TEXT("Applying saved tuning restores battery opt-in"),Settings->bSimulateBattery);
    TestEqual(TEXT("Applying saved tuning restores pack cells"),Settings->BatteryCells,6);
    TestFalse(TEXT("Tuning does not replace OSD preferences"),Settings->bOsdCrosshair);
    TestFalse(TEXT("Tuning cannot turn on sandbox access"),Settings->bAllowSandboxAccess);
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/FString::Printf(TEXT("FactoryFPV/SimulationConfig-%llu/GameUserSettings.ini"),FPlatformTime::Cycles64()));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    GConfig->SetBool(TEXT("SectionsToSave"),TEXT("bCanSaveAllSections"),true,Path);
    Settings->SaveConfig(CPF_Config,*Path,GConfig,false);GConfig->Flush(false,Path);GConfig->UnloadFile(Path);GConfig->LoadFile(Path);
    auto* Reloaded=NewObject<UFactoryFPVSettings>();Reloaded->LoadConfig(nullptr,*Path);
    TestTrue(TEXT("Simulation opt-in persists"),Reloaded->bSimulateBattery);
    TestEqual(TEXT("Cell count persists"),Reloaded->BatteryCells,6);
    TestEqual(TEXT("Capacity persists"),Reloaded->BatteryCapacityMah,2200.f);
    TestEqual(TEXT("Estimated power persists"),Reloaded->BatteryMaxPowerWatts,1600.f);
    TestEqual(TEXT("OSD scale persists"),Reloaded->OsdTextScale,1.25f);
    TestEqual(TEXT("OSD opacity persists"),Reloaded->OsdOpacity,.45f);
    TestEqual(TEXT("Pilot cue volume survives config reload and is independent of presets"),Reloaded->FlightCueVolume,.38f);
    TestTrue(TEXT("OSD layout is independent of tuning and persists"),Reloaded->OsdLayout==Settings->OsdLayout &&
        Reloaded->OsdLayout.GetSlot(EFactoryFPVOsdItem::Home)==EFactoryFPVOsdSlot::BottomLeft &&
        Reloaded->OsdLayout.GetSlot(EFactoryFPVOsdItem::Timer)==EFactoryFPVOsdSlot::UpperRight);
    TestTrue(TEXT("Every hidden OSD option survives restart"),!Reloaded->bOsdCrosshair && !Reloaded->bOsdSpeed && !Reloaded->bOsdAltitude && !Reloaded->bOsdHome && !Reloaded->bOsdTimer && !Reloaded->bOsdThrottle);
    TestTrue(TEXT("Preset includes its battery model"),Reloaded->TuningPresets.Num()==1 && Reloaded->TuningPresets[0].bSimulateBattery && Reloaded->TuningPresets[0].BatteryCells==6);
    Reloaded->OsdTextScale=100;Reloaded->OsdOpacity=0;Reloaded->BatteryCells=-1;
    Reloaded->FlightCueVolume=5.f;
    Reloaded->OsdLayout.Speed=Reloaded->OsdLayout.Home;
    Reloaded->BatteryCapacityMah=std::numeric_limits<float>::quiet_NaN();Reloaded->BatteryMaxPowerWatts=99999;Reloaded->Sanitize();
    TestEqual(TEXT("OSD remains bounded/readable"),Reloaded->OsdTextScale,1.3f);
    TestTrue(TEXT("Conflicting saved positions restore the default layout"),Reloaded->OsdLayout==FFactoryFPVOsdLayout{});
    TestEqual(TEXT("Normal telemetry cannot become invisible"),Reloaded->OsdOpacity,.3f);
    TestEqual(TEXT("Pilot cue gain stays within the player volume range"),Reloaded->FlightCueVolume,1.f);
    TestEqual(TEXT("Invalid pack cell count bounded"),Reloaded->BatteryCells,1);
    TestEqual(TEXT("NaN capacity has stable default"),Reloaded->BatteryCapacityMah,1500.f);
    TestEqual(TEXT("Estimated power bounded"),Reloaded->BatteryMaxPowerWatts,5000.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVLensSettingsTest,"FactoryFPV.Settings.LensPersistenceAndLegacyDefaults",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVLensSettingsTest::RunTest(const FString&) {
    TestEqual(TEXT("Old presets omit lens strength and remain undistorted"),FFactoryFPVTuningPreset{}.LensStrength,0.f);
    for (const auto& Preset:UFactoryFPVSettings::BuiltInTunings())
        TestEqual(TEXT("Existing built-in tunes retain the original view"),Preset.LensStrength,0.f);
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->TuningPresets.Reset();
    Settings->LensStrength=.65f;
    TestTrue(TEXT("A custom tune can capture the optional lens"),Settings->StoreTuningPreset(TEXT("Lens tune")));
    Settings->LensStrength=0;
    Settings->ApplyTuning(Settings->TuningPresets[0]);
    TestEqual(TEXT("Applying a tune restores its lens strength"),Settings->LensStrength,.65f);
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/FString::Printf(TEXT("FactoryFPV/LensConfig-%llu/GameUserSettings.ini"),FPlatformTime::Cycles64()));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    GConfig->SetBool(TEXT("SectionsToSave"),TEXT("bCanSaveAllSections"),true,Path);
    Settings->LensStrength=.35f;
    Settings->SaveConfig(CPF_Config,*Path,GConfig,false);
    GConfig->Flush(false,Path);GConfig->UnloadFile(Path);GConfig->LoadFile(Path);
    auto* Reloaded=NewObject<UFactoryFPVSettings>();Reloaded->LoadConfig(nullptr,*Path);Reloaded->Sanitize();
    TestEqual(TEXT("Current lens persists independently of stored tuning"),Reloaded->LensStrength,.35f);
    TestTrue(TEXT("Preset lens strength survives an INI round trip"),Reloaded->TuningPresets.Num()==1 && Reloaded->TuningPresets[0].LensStrength==.65f);
    Reloaded->LensStrength=std::numeric_limits<float>::quiet_NaN();Reloaded->Sanitize();
    TestEqual(TEXT("NaN lens setting safely disables the effect"),Reloaded->LensStrength,0.f);
    Reloaded->LensStrength=std::numeric_limits<float>::infinity();Reloaded->Sanitize();
    TestEqual(TEXT("Infinite lens setting safely disables the effect"),Reloaded->LensStrength,0.f);
    Reloaded->LensStrength=-10;Reloaded->Sanitize();
    TestEqual(TEXT("Negative lens strength is off"),Reloaded->LensStrength,0.f);
    Reloaded->LensStrength=100;Reloaded->Sanitize();
    TestEqual(TEXT("Lens strength is bounded"),Reloaded->LensStrength,1.f);
    auto Malformed=FFactoryFPVTuningPreset{};Malformed.Name=TEXT("Malformed lens");Malformed.LensStrength=std::numeric_limits<float>::quiet_NaN();
    Reloaded->ApplyTuning(Malformed);
    TestEqual(TEXT("Malformed preset lens also safely disables"),Reloaded->LensStrength,0.f);
    return true;
}

#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SBorder.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVHangarRenderTest, "FactoryFPV.Presentation.HangarRender",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVHangarRenderTest::RunTest(const FString&) {
    if (!FApp::CanEverRender()) {
        AddInfo(TEXT("Hangar rendering skipped with NullRHI; run with -RenderOffscreen for visual review."));
        return true;
    }
    auto Panel=SNew(SFactoryFPVPanel);
    Panel->Radio.reset(); // No hardware samples or user settings are changed by visual review.
    // Match the real viewport's ScaleToFit/DownOnly wrapper. Test small and
    // ultrawide windows, not just a larger render target around an unscaled panel.
    auto View=SNew(SBorder).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(0)
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Panel]];
    FWidgetRenderer Renderer(true);
    const FIntPoint Viewports[]{FIntPoint(1060,730),FIntPoint(1280,720),FIntPoint(2560,1080)};
    const TCHAR* PageNames[]{TEXT("Radio"),TEXT("Flight"),TEXT("Presets"),TEXT("OSD-Audio"),TEXT("Simulation-Access"),TEXT("Racing")};
    const FString Directory=FPaths::ProjectSavedDir()/TEXT("FactoryFPV");
    IFileManager::Get().MakeDirectory(*Directory,true);
    for (const auto& Viewport:Viewports) {
        const FVector2D Size(Viewport.X,Viewport.Y);
        // Slate applies display gamma itself; use a linear render target to
        // avoid a second gamma conversion through the target's sRGB writes.
        auto* Target=FWidgetRenderer::CreateTargetFor(Size,TF_Bilinear,false);
        if (!TestNotNull(TEXT("Hangar render target"),Target)) return false;
        for (int32 Index=0;Index<Panel->PageButtons.Num();++Index) {
            Panel->PageButtons[Index]->SimulateClick();
            TestEqual(TEXT("Real tab button selects the rendered page"),Panel->PageSwitcher->GetActiveWidgetIndex(),Index);
            // Auto-wrap settles after receiving the actual allotted width.
            Renderer.DrawWidget(Target,View,Size,0.f,false);
            FlushRenderingCommands();
            Renderer.DrawWidget(Target,View,Size,1.f/60.f,false);
            FlushRenderingCommands();
            const auto& Geometry=Panel->GetCachedGeometry();
            const FVector2D TopLeft=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
            const FVector2D BottomRight=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
            TestTrue(TEXT("Entire panel remains inside the viewport"),TopLeft.X>=-1 && TopLeft.Y>=-1 && BottomRight.X<=Size.X+1 && BottomRight.Y<=Size.Y+1);
            TestTrue(TEXT("Tabbed content has usable height above the fixed footer"),Panel->PageSwitcher->GetCachedGeometry().GetLocalSize().Y>300);
            TArray<FColor> Pixels;
            FReadSurfaceDataFlags ReadFlags;
            ReadFlags.SetLinearToGamma(false);
            if (!TestTrue(TEXT("Read hangar pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,ReadFlags))) return false;
            TestEqual(TEXT("Rendered viewport pixel count"),Pixels.Num(),Viewport.X*Viewport.Y);
            TArray64<uint8> Png;
            FImageUtils::PNGCompressImageArray(Viewport.X,Viewport.Y,Pixels,Png);
            const FString Path=Directory/FString::Printf(TEXT("Hangar-%s-%dx%d.png"),PageNames[Index],Viewport.X,Viewport.Y);
            TestTrue(TEXT("Save tab and viewport preview"),FFileHelper::SaveArrayToFile(Png,*Path));
            if (Index==0 && Viewport.X==1060) FFileHelper::SaveArrayToFile(Png,*(Directory/TEXT("Hangar.png")));
            AddInfo(Path);
        }
    }
    return true;
}

#endif
