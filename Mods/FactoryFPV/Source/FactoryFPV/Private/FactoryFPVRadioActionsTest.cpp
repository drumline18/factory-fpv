#include "FactoryFPVSettings.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRadioActionsTest,"FactoryFPV.Settings.RadioActionProfiles",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRadioActionsTest::RunTest(const FString&) {
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->RadioProfiles.Reset();
    FFactoryFPVRadioProfile First;
    First.DeviceId=TEXT("actions-radio-a");First.DisplayName=TEXT("First radio");
    First.Roll={0,FVector(0,32767,65535),false};
    First.Pitch={1,FVector(0,32767,65535),true};
    First.Yaw={3,FVector(0,32767,65535),false};
    First.Throttle={2,FVector(0,32767,65535),false};
    First.Arm.Kind=1;First.Arm.Channel=7;
    First.ReturnControl.Kind=1;First.ReturnControl.Channel=8;
    First.RecoverControl.Kind=2;First.RecoverControl.Channel=4;First.RecoverControl.Off=65535;First.RecoverControl.On=0;
    First.ModeControl.Kind=1;First.ModeControl.Channel=9;First.ModeControl.Off=1;First.ModeControl.On=0;
    Settings->SetProfile(First);
    FString Error;
    TestTrue(TEXT("Distinct optional actions are valid"),Settings->AreRadioBindingsValid(Error));
    TestTrue(TEXT("Valid bindings clear validation message"),Error.IsEmpty());
    const auto Actions=Settings->RadioActionBindings();
    TestEqual(TEXT("Native return slot"),Actions[static_cast<int>(factoryfpv::RadioAction::Return)].index,8);
    TestEqual(TEXT("Native recovery direction"),Actions[static_cast<int>(factoryfpv::RadioAction::Recover)].off,65535L);
    TestEqual(TEXT("Native mode direction"),Actions[static_cast<int>(factoryfpv::RadioAction::ToggleMode)].off,1L);
    Settings->SelectRadio(TEXT("actions-radio-b"),TEXT("Second radio"));
    TestEqual(TEXT("New device has no inherited return control"),Settings->RadioReturn.Kind,0);
    TestEqual(TEXT("New device has no inherited recovery control"),Settings->RadioRecover.Kind,0);
    TestEqual(TEXT("New device has no inherited mode control"),Settings->RadioMode.Kind,0);
    Settings->RadioReturn.Kind=1;Settings->RadioReturn.Channel=11;
    Settings->SelectRadio(TEXT("ACTIONS-RADIO-A"),TEXT("First radio renamed"));
    TestEqual(TEXT("Device round trip restores return control"),Settings->RadioReturn.Channel,8);
    TestEqual(TEXT("Device round trip restores recovery inversion"),Settings->RadioRecover.Off,65535);
    TestEqual(TEXT("Device round trip restores inverted mode button"),Settings->RadioMode.Off,1);
    TestEqual(TEXT("Case-insensitive identity creates no duplicate profile"),Settings->RadioProfiles.Num(),2);

    auto* Draft=DuplicateObject<UFactoryFPVSettings>(Settings,GetTransientPackage());
    Draft->RadioReturn.Channel=42;
    Draft->SetProfile(Draft->CurrentProfile());
    TestEqual(TEXT("Cancelled draft keeps selected return binding"),Settings->RadioReturn.Channel,8);
    TestEqual(TEXT("Cancelled draft keeps stored profile binding"),Settings->CurrentProfile().ReturnControl.Channel,8);
    Settings->ApplyTuning(UFactoryFPVSettings::BuiltInTunings()[0]);
    TestEqual(TEXT("Flight preset leaves return control intact"),Settings->RadioReturn.Channel,8);
    TestEqual(TEXT("Flight preset leaves recovery direction intact"),Settings->RadioRecover.Off,65535);
    TestEqual(TEXT("Flight preset leaves mode direction intact"),Settings->RadioMode.Off,1);

    Settings->RadioReturn=Settings->RadioArm;
    TestFalse(TEXT("Return cannot reuse arm control"),Settings->AreRadioBindingsValid(Error));
    TestTrue(TEXT("Conflict names both actions"),Error.Contains(TEXT("Arm")) && Error.Contains(TEXT("Return to pioneer")));
    TestTrue(TEXT("Binding error does not discard valid stick calibration"),Settings->IsRadioCalibrated());
    Settings->Sanitize();
    TestEqual(TEXT("Sanitization preserves binding for explicit correction"),Settings->RadioReturn.Channel,7);
    Settings->RadioReturn=First.ReturnControl;
    Settings->RadioMode=Settings->RadioRecover;
    TestFalse(TEXT("Mode cannot reuse recovery control"),Settings->AreRadioBindingsValid(Error));
    TestTrue(TEXT("Auxiliary conflict names mode and recovery"),Error.Contains(TEXT("Recover drone")) && Error.Contains(TEXT("Toggle flight mode")));
    Settings->RadioMode=First.ModeControl;
    Settings->RadioReturn.Kind=2;Settings->RadioReturn.Channel=0;Settings->RadioReturn.Off=0;Settings->RadioReturn.On=65535;
    TestFalse(TEXT("Loaded bindings cannot use flight axes"),Settings->AreRadioBindingsValid(Error));
    TestTrue(TEXT("Flight-axis error gives corrective guidance"),Error.Contains(TEXT("flight axis")));
    Settings->RadioReturn.Kind=99;
    TestFalse(TEXT("Unknown binding kind rejected"),Settings->AreRadioBindingsValid(Error));
    TestTrue(TEXT("Malformed binding error names action"),Error.Contains(TEXT("Return to pioneer")) && Error.Contains(TEXT("invalid")));
    Settings->RadioReturn=First.ReturnControl;
    Settings->SetProfile(Settings->CurrentProfile());

    // Use Unreal's serializer and a unique disk-backed cache entry, preserving
    // the player's GameUserSettings.ini and the process-wide config defaults.
    const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/
        FString::Printf(TEXT("FactoryFPV/RadioActionsConfig-%llu/GameUserSettings.ini"),FPlatformTime::Cycles64()));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
    GConfig->SetBool(TEXT("SectionsToSave"),TEXT("bCanSaveAllSections"),true,Path);
    Settings->SaveConfig(CPF_Config,*Path,GConfig,false);
    GConfig->Flush(false,Path);
    FString SavedText;
    TestTrue(TEXT("Action settings INI exists on disk"),FFileHelper::LoadFileToString(SavedText,*Path));
    TestTrue(TEXT("Selected actions written to INI"),SavedText.Contains(TEXT("RadioReturn=")) && SavedText.Contains(TEXT("RadioRecover=")) && SavedText.Contains(TEXT("RadioMode=")));
    TestTrue(TEXT("Nested device actions written to INI"),SavedText.Contains(TEXT("ReturnControl=")) && SavedText.Contains(TEXT("RecoverControl=")) && SavedText.Contains(TEXT("ModeControl=")));
    GConfig->UnloadFile(Path);GConfig->LoadFile(Path);
    auto* Reloaded=NewObject<UFactoryFPVSettings>();
    Reloaded->LoadConfig(nullptr,*Path);
    TestEqual(TEXT("Both profiles survive INI reload"),Reloaded->RadioProfiles.Num(),2);
    TestEqual(TEXT("Selected return button survives INI reload"),Reloaded->RadioReturn.Channel,8);
    TestEqual(TEXT("Recovery kind survives INI reload"),Reloaded->RadioRecover.Kind,2);
    TestEqual(TEXT("Recovery direction survives INI reload"),Reloaded->RadioRecover.Off,65535);
    TestEqual(TEXT("Mode button direction survives INI reload"),Reloaded->RadioMode.Off,1);
    TestTrue(TEXT("Reloaded actions pass validation"),Reloaded->AreRadioBindingsValid(Error));
    Reloaded->SelectRadio(TEXT("actions-radio-b"),TEXT("Second radio"));
    TestEqual(TEXT("Second device restores own return control after reload"),Reloaded->RadioReturn.Channel,11);
    TestEqual(TEXT("Second device retains unbound recovery after reload"),Reloaded->RadioRecover.Kind,0);
    TestEqual(TEXT("Second device retains unbound mode after reload"),Reloaded->RadioMode.Kind,0);
    Reloaded->SelectRadio(TEXT("actions-radio-a"),TEXT("First radio"));
    TestEqual(TEXT("Nested first-device return survives reload and switching"),Reloaded->RadioReturn.Channel,8);
    TestEqual(TEXT("Nested first-device recovery direction survives reload and switching"),Reloaded->RadioRecover.Off,65535);
    TestEqual(TEXT("Nested first-device mode direction survives reload and switching"),Reloaded->RadioMode.Off,1);
    return true;
}
#endif
