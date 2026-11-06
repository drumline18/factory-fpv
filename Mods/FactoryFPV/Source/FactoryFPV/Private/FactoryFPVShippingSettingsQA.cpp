#include "FactoryFPVShippingSettingsQA.h"

#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVPanel.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVSettings.h"
#include "FactoryFPVShippingStartup.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Components/InputComponent.h"
#include "Containers/Ticker.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Templates/UnrealTemplate.h"
#include "Types/SlateAttributeMetaData.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

namespace FactoryFPVShippingSettingsQAInternal {
constexpr double DeadlineSeconds=40.;
constexpr float SavedPitchRate=640.f,SavedYawRate=320.f;
constexpr float SavedRollCenterRate=150.f,SavedPitchCenterRate=210.f,SavedYawCenterRate=110.f;
constexpr float SavedThrottleExpo=.6f;
const TCHAR* CloneName=TEXT("FPV-QA-Clone");
const TCHAR* PresetName=TEXT("QA Settings Roundtrip");
const TCHAR* PrimaryId=TEXT("{00111111-2244-4688-8888-123456789001}");
const TCHAR* OtherId=TEXT("{00222222-2244-4688-8888-123456789002}");
const TCHAR* CloseHangarWarning=TEXT("FPV: close the hangar before changing radio settings in the console.");
const TCHAR* ReloadSuccess=TEXT("New process loaded the saved preference, tuning, profiles and shortcuts. The replacement hangar and return callbacks restored ordinary gameplay.");

FString Normalized(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    FPaths::CollapseRelativeDirectories(Path);
    return Path;
}

bool GuardPaths(FString& Config,FString& SaveDirectory,FString& Error) {
    const TCHAR* CommandLine=FCommandLine::Get();
    for (const auto* Flag:{TEXT("FactoryFPVAutomation"),TEXT("RenderOffScreen"),TEXT("NoAutoSave"),TEXT("SavesUseProjectSavedDir")})
        if (!FParse::Param(CommandLine,Flag)) {
            Error=FString::Printf(TEXT("Settings QA is missing its required -%s guard."),Flag);
            return false;
        }
    const FString Saved=Normalized(FPaths::ProjectSavedDir());
    const FString Run=FPaths::GetPath(Saved);
    const FString RunName=FPaths::GetCleanFilename(Run);
    const FString Prefix=TEXT("shipping-world-qa-");
    FGuid RunId;
    if (!FPaths::GetCleanFilename(Saved).Equals(TEXT("Saved"),ESearchCase::IgnoreCase) ||
        !FPaths::GetCleanFilename(FPaths::GetPath(Run)).Equals(TEXT("build"),ESearchCase::IgnoreCase) ||
        !RunName.StartsWith(Prefix,ESearchCase::IgnoreCase) ||
        !FGuid::ParseExact(RunName.Mid(Prefix.Len()),EGuidFormats::Digits,RunId) || !RunId.IsValid()) {
        Error=FString::Printf(TEXT("Settings QA rejected ProjectSavedDir '%s'; expected build/shipping-world-qa-<guid>/Saved."),*Saved);
        return false;
    }
    const FString NativeSaveDirectory=UFGSaveSystem::GetSaveDirectoryPath();
    SaveDirectory=Normalized(NativeSaveDirectory);
    if (NativeSaveDirectory.IsEmpty() || !SaveDirectory.StartsWith(Saved+TEXT("/"),ESearchCase::IgnoreCase)) {
        Error=FString::Printf(TEXT("Settings QA rejected native save directory '%s' outside the disposable Saved root."),*NativeSaveDirectory);
        return false;
    }
    if (!GConfig || GGameUserSettingsIni.IsEmpty()) {
        Error=TEXT("Settings QA has no native GameUserSettings config branch.");
        return false;
    }
    // UE 5.6 exposes a short alias here. SaveConfig writes the destination
    // recorded on that branch, not a relative file named GameUserSettings.
    const FString ClassConfig=UFactoryFPVSettings::StaticClass()->GetConfigName();
    const auto* GameBranch=GConfig->FindBranchWithNoReload(FName(*GGameUserSettingsIni),GGameUserSettingsIni);
    const auto* ClassBranch=GConfig->FindBranchWithNoReload(FName(*ClassConfig),ClassConfig);
    if (!GameBranch || !ClassBranch || ClassBranch!=GameBranch || GameBranch->IniPath.IsEmpty()) {
        Error=FString::Printf(TEXT("Settings QA could not verify matching native config branches: game '%s', class '%s', destination '%s'."),
            *GGameUserSettingsIni,*ClassConfig,GameBranch ? *GameBranch->IniPath : TEXT(""));
        return false;
    }
    Config=Normalized(GameBranch->IniPath);
    const FString Expected=Saved/TEXT("Config/Windows/GameUserSettings.ini");
    if (!Config.Equals(Expected,ESearchCase::IgnoreCase)) {
        Error=FString::Printf(TEXT("Settings QA rejected native config destination '%s'; expected '%s'."),*Config,*Expected);
        return false;
    }
    return true;
}

FString Escape(FString Value) {
    return Value.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}

const TCHAR* Flag(bool Value) { return Value ? TEXT("true") : TEXT("false"); }

void Marker(const TCHAR* Stage,const TCHAR* Result,const FString& Reason,const FString& Extra=FString()) {
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"%s\",\"result\":\"%s\",\"reason\":\"%s\",\"savedWorld\":false,\"injectedControls\":false%s}"),
        Stage,Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}

FFactoryFPVRadioProfile Profile(bool Primary) {
    FFactoryFPVRadioProfile Result;
    Result.DeviceId=Primary ? PrimaryId : OtherId;
    Result.DisplayName=Primary ? TEXT("QA synthetic primary") : TEXT("QA synthetic alternate");
    const int32 Axis=Primary ? 0 : 4;
    const int32 Button=Primary ? 12 : 24;
    const double Offset=Primary ? 0. : 200.;
    Result.Roll={Axis,FVector(100+Offset,501+Offset,900+Offset),!Primary};
    Result.Pitch={Axis+1,FVector(111+Offset,510+Offset,912+Offset),Primary};
    Result.Yaw={Axis+3,FVector(121+Offset,520+Offset,922+Offset),!Primary};
    Result.Throttle={Axis+2,FVector(131+Offset,532+Offset,930+Offset),Primary};
    Result.Arm={1,Button,0,1};
    Result.ReturnControl={1,Button+1,1,0};
    Result.RecoverControl={1,Button+2,0,1};
    Result.ModeControl={1,Button+3,1,0};
    return Result;
}

bool RadioAndPolicyMatch(const UFactoryFPVSettings& Settings) {
    const auto Primary=Profile(true);
    const auto Other=Profile(false);
    const auto Same=[](const FFactoryFPVRadioProfile& A,const FFactoryFPVRadioProfile& B) {
        return FFactoryFPVRadioProfile::StaticStruct()->CompareScriptStruct(&A,&B,0);
    };
    FString Error;
    return Settings.RadioProfiles.Num()==2 && Settings.IsRadioCalibrated() && Settings.AreRadioBindingsValid(Error) &&
        Same(Settings.CurrentProfile(),Primary) &&
        Settings.RadioProfiles.ContainsByPredicate([&](const auto& Entry) { return Same(Entry,Primary); }) &&
        Settings.RadioProfiles.ContainsByPredicate([&](const auto& Entry) { return Same(Entry,Other); }) &&
        !Settings.bAllowSandboxAccess && !Settings.bOsdCrosshair && Settings.OsdOpacity==.75f;
}

bool HasDefaultRateOverrides(const UFactoryFPVSettings& Settings) {
    return Settings.PitchRate==0.f && Settings.YawRate==0.f &&
        Settings.RollCenterRate==0.f && Settings.PitchCenterRate==0.f && Settings.YawCenterRate==0.f;
}

bool ShortcutsMatch(const UFactoryFPVSettings& Settings,bool bRemapped) {
    const auto& Keys=Settings.KeyboardShortcuts;
    return Keys.Hangar==(bRemapped ? EKeys::F2 : EKeys::F6) && Keys.Arm==(bRemapped ? EKeys::F3 : EKeys::F8) &&
        Keys.Recover==(bRemapped ? EKeys::F4 : EKeys::F9) && Keys.Return==(bRemapped ? EKeys::F7 : EKeys::F10);
}

bool OsdLayoutMatches(const FFactoryFPVOsdLayout& Layout,bool bSwapped) {
    return Layout.GetSlot(EFactoryFPVOsdItem::Speed)==(bSwapped ? EFactoryFPVOsdSlot::TopRight : EFactoryFPVOsdSlot::BottomLeft) &&
        Layout.GetSlot(EFactoryFPVOsdItem::Timer)==(bSwapped ? EFactoryFPVOsdSlot::BottomLeft : EFactoryFPVOsdSlot::TopRight);
}

bool LoadedThrottleExpoWorks(const factoryfpv::FlightCore& Flight,double NominalHover) {
    const auto& Parameters=Flight.parameters;
    if (Flight.armed || Flight.thrust!=0. || Parameters.throttleExpo!=SavedThrottleExpo ||
        !FMath::IsNearlyEqual(Parameters.nominalHoverThrottle,NominalHover,1.e-8)) return false;
    // Probe the parameters of the real launched flight without injecting input into the game.
    auto Shaped=Flight;
    factoryfpv::Input Stick;
    Stick.throttle=factoryfpv::MaxArmThrottle+.01;
    if (Shaped.arm(Stick) || Shaped.armed) return false;
    Stick.throttle=factoryfpv::MaxArmThrottle;
    if (!Shaped.arm(Stick)) return false;
    auto Linear=Shaped;
    Linear.parameters.throttleExpo=0.;
    Stick.throttle=(Parameters.nominalHoverThrottle+1.)*.5;
    const double Command=factoryfpv::throttleCommand(Stick.throttle,Parameters.nominalHoverThrottle,Parameters.throttleExpo);
    Shaped.step(Stick);
    Linear.step(Stick);
    return Shaped.thrust>0. && Shaped.thrust<Linear.thrust &&
        FMath::IsNearlyEqual(Shaped.thrust,Linear.thrust*(Command/Stick.throttle),1.e-8);
}

bool SettingsMatch(const UFactoryFPVSettings& Settings) {
    if (Settings.PreferredInputMode!=TEXT("keyboard") || !RadioAndPolicyMatch(Settings) || Settings.TuningPresets.Num()!=1 ||
        !ShortcutsMatch(Settings,true) || !OsdLayoutMatches(Settings.OsdLayout,true)) return false;
    auto Expected=UFactoryFPVSettings::BuiltInTunings()[0];
    Expected.Name=PresetName;
    Expected.PitchRate=SavedPitchRate;
    Expected.YawRate=SavedYawRate;
    Expected.RollCenterRate=SavedRollCenterRate;
    Expected.PitchCenterRate=SavedPitchCenterRate;
    Expected.YawCenterRate=SavedYawCenterRate;
    Expected.ThrottleExpo=SavedThrottleExpo;
    const auto Applied=Settings.CaptureTuning(PresetName);
    const auto* Type=FFactoryFPVTuningPreset::StaticStruct();
    return Type->CompareScriptStruct(&Applied,&Expected,0) && Type->CompareScriptStruct(&Settings.TuningPresets[0],&Expected,0);
}

bool ReadCommittedIni(const FString& Path,FString& Content) {
    return FFileHelper::LoadFileToString(Content,*Path) && Content.Contains(TEXT("[/Script/FactoryFPV.FactoryFPVSettings]")) &&
        Content.Contains(PrimaryId) && Content.Contains(OtherId) && Content.Contains(PresetName) &&
        Content.Contains(TEXT("PreferredInputMode=keyboard"),ESearchCase::IgnoreCase);
}

bool FindRemappedBinding(const UInputComponent& Controls,const UObject* Owner,const FKey& Key,const FKey& Previous,FInputActionUnifiedDelegate& Out) {
    int32 Matches=0;
    for (const auto& Binding:Controls.KeyBindings) {
        if (Binding.KeyEvent!=IE_Pressed || !Binding.KeyDelegate.IsBoundToObject(Owner)) continue;
        if (Binding.Chord.Key==Previous) return false;
        if (Binding.Chord.Key==Key) { Out=Binding.KeyDelegate;++Matches; }
    }
    return Matches==1;
}

TSharedPtr<SButton> FindButton(const TSharedRef<SWidget>& Widget,const TCHAR* Label) {
    FSlateAttributeMetaData::UpdateOnlyVisibilityAttributes(Widget.Get(),FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    if (!Widget->GetVisibility().IsVisible()) return nullptr;
    auto* Children=Widget->GetChildren();
    if (Widget->GetType()==FName(TEXT("SButton")) && Children->Num()==1) {
        const auto Child=Children->GetChildAt(0);
        if (Child->GetType()==FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Child)->GetText().ToString()==Label)
            return StaticCastSharedRef<SButton>(Widget);
    }
    for (int32 I=0;I<Children->Num();++I)
        if (auto Found=FindButton(Children->GetChildAt(I),Label)) return Found;
    return nullptr;
}

bool Click(const TSharedRef<SWidget>& Root,const TCHAR* Label) {
    const auto Button=FindButton(Root,Label);
    if (!Button) return false;
    // Selection and click are synchronous. Refresh the actual enabled
    // binding before invoking the button's public Slate input handlers.
    FSlateAttributeMetaData::UpdateAllAttributes(*Button,FSlateAttributeMetaData::EInvalidationPermission::AllowInvalidationIfConstructed);
    if (!Button->IsEnabled()) return false;
    for (const auto& Key:{EKeys::Enter,EKeys::SpaceBar,EKeys::Virtual_Accept,EKeys::Gamepad_FaceButton_Bottom}) {
        const FKeyEvent Event(Key,FModifierKeysState(),0,false,0,0);
        if (FSlateApplication::Get().GetNavigationActionFromKey(Event)!=EUINavigationAction::Accept) continue;
        const auto Geometry=Button->GetCachedGeometry();
        const bool Down=Button->OnKeyDown(Geometry,Event).IsEventHandled();
        const bool Up=Button->OnKeyUp(Geometry,Event).IsEventHandled();
        return Down && Up;
    }
    return false;
}
}

class FFactoryFPVShippingSettingsQA {
public:
    TWeakObjectPtr<UWorld> World;
    bool bReload=false;

    bool Tick() {
        using namespace FactoryFPVShippingSettingsQAInternal;
        if (FPlatformTime::Seconds()>Deadline) return Finish(false,Reason);
        auto* Current=World.Get();
        FString SaveDirectory,GuardError;
        bConfigIsolated=GuardPaths(ConfigPath,SaveDirectory,GuardError);
        if (!bConfigIsolated) return Finish(false,GuardError);
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone)
            return Finish(false,TEXT("Settings fixture lost its live standalone world."));
        auto* Mode=Current->GetAuthGameMode<AFGGameMode>();
        auto* Session=Mode ? Mode->GetSaveSession() : nullptr;
        auto* PC=Cast<AFGPlayerController>(Current->GetFirstPlayerController());
        auto* Pioneer=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        auto* Subsystem=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        if (!Mode || Mode->IsMainMenuGameMode() || !Current->HasBegunPlay() || !Session || Session->HasSaveGameOperationPending() ||
            !PC || !Pioneer || !Pioneer->IsAliveAndWell() || PC->IsPaused() || PC->GetViewTarget()!=Pioneer || !Subsystem) return true;
        const auto& Header=Session->GetLoadedSaveHeader();
        FString Clone;
        if (Header.SaveName.IsEmpty() || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (Mode->GetLoadData().LoadName!=CloneName && Header.SaveName!=CloneName) ||
            !UFGSaveSystem::GetAbsolutePathForSaveGame(Current,CloneName,Clone) ||
            !Normalized(Clone).StartsWith(SaveDirectory+TEXT("/"),ESearchCase::IgnoreCase) ||
            FPaths::GetCleanFilename(Clone)!=TEXT("FPV-QA-Clone.sav") || !IFileManager::Get().FileExists(*Clone))
            return Finish(false,TEXT("The settings fixture did not resolve the exact loaded disposable FPV-QA-Clone save."));
        if (!FactoryFPVShippingStartup::Ready(PC->GetGameUI(),Reason,bSplashObserved,bAnimationObserved,bStartupFinished,bFrontEndFinished)) return true;
        if (bAwaitingShortcutReturn) {
            if (AFactoryFPVPioneerAnchor::FindPending(Current,Pioneer)) return true;
            bRemappedFlightVerified=true;
            return Finish(true,ReloadSuccess);
        }
        if (!bHangarOpened) {
            if (Subsystem->IsPanelOpen() || AFactoryFPVDrone::FindActive(Current)) return Finish(false,TEXT("The fixture will not take an existing hangar or flight session."));
            const auto& Settings=*GetDefault<UFactoryFPVSettings>();
            if (bReload) {
                bNaturalConfigReload=SettingsMatch(Settings) && ReadCommittedIni(ConfigPath,Before);
                if (!bNaturalConfigReload) return Finish(false,TEXT("A new game process did not naturally load the saved preset, input mode, shortcuts, OSD layout, tuning and radio profiles."));
            } else if (Settings.PreferredInputMode!=TEXT("radio") || !Settings.RadioDeviceId.IsEmpty() || !Settings.RadioProfiles.IsEmpty() ||
                !Settings.TuningPresets.IsEmpty() || !HasDefaultRateOverrides(Settings) || Settings.ThrottleExpo!=0.f || !ShortcutsMatch(Settings,false) ||
                !OsdLayoutMatches(Settings.OsdLayout,false)) {
                return Finish(false,TEXT("The save stage requires a fresh profile without existing FPV settings."));
            }
            if (bReload) {
                if (!Subsystem->Controls || !PC->IsInputComponentInStack(Subsystem->Controls)) {
                    Reason=TEXT("Waiting for the actual remapped hangar input component.");return true;
                }
                FInputActionUnifiedDelegate OpenHangar;
                if (!FindRemappedBinding(*Subsystem->Controls,Subsystem,Settings.KeyboardShortcuts.Hangar,EKeys::F6,OpenHangar))
                    return Finish(false,TEXT("The reloaded hangar did not bind its replacement key exclusively."));
                OpenHangar.Execute(Settings.KeyboardShortcuts.Hangar);
                if (!Subsystem->IsHangarOpen()) return Finish(false,TEXT("The remapped hangar callback did not open its mounted settings panel."));
                bRemappedHangarVerified=true;
            } else Subsystem->OpenPanel();
            if (!Subsystem->Panel) { Reason=TEXT("Waiting for the native hangar to open on the loaded pioneer.");return true; }
            Hangar=Subsystem;
            Panel=Subsystem->Panel;
            bHangarOpened=true;
            Reason=TEXT("Waiting for the actual hangar to be mounted in Slate.");
            return true;
        }
        const auto Open=Panel.Pin();
        if (!Open || Hangar.Get()!=Subsystem || Subsystem->Panel!=Open)
            return Finish(false,TEXT("The owned hangar was replaced before its settings workflow completed."));
        if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().FindWidgetWindow(Open.ToSharedRef()).IsValid()) return true;
        return Exercise(*Open,Current);
    }

private:
    const double Deadline=FPlatformTime::Seconds()+FactoryFPVShippingSettingsQAInternal::DeadlineSeconds;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakPtr<SFactoryFPVPanel> Panel;
    FString Reason=TEXT("Waiting for actual game startup and the disposable pioneer.");
    FString ConfigPath,Before;
    bool bSplashObserved=false,bAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bConfigIsolated=false,bHangarOpened=false,bPresetApplied=false,bDraftDiscarded=false,bProfilePreserved=false,bConfigValuesVerified=false;
    bool bPresetStored=false,bSettingsSaved=false,bRadioAxisRefused=false,bRadioSelectRefused=false,bSeededSyntheticRadio=false;
    bool bNaturalConfigReload=false,bSavedPresetSelectable=false,bInputModeWorkflowVerified=false,bCloseConfirmationVerified=false;
    bool bRemappedHangarVerified=false,bRemappedFlightVerified=false,bAwaitingShortcutReturn=false;
    bool bDefaultCenterRatesVerified=false,bRuntimeOsdLayoutVerified=false,bRuntimeThrottleExpoVerified=false;

    bool VerifyRemappedFlight(UWorld* Current,FString& Error) {
        using namespace FactoryFPVShippingSettingsQAInternal;
        auto* PC=Current->GetFirstPlayerController();
        auto* Pioneer=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!Pioneer || PC->GetViewTarget()!=Pioneer || !Pioneer->IsAliveAndWell() || AFactoryFPVDrone::FindActive(Current)) {
            Error=TEXT("The shortcut probe requires ordinary idle pioneer ownership.");return false;
        }
        const bool bMoveIgnored=PC->IsMoveInputIgnored(),bLookIgnored=PC->IsLookInputIgnored();
        auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
        // The existing settings fixture saves crafted-kit access. Allow one
        // isolated Keyboard launch without writing that temporary policy to disk.
        auto* Drone=[&] {
            const TGuardValue<bool> Sandbox(Settings->bAllowSandboxAccess,true);
            return AFactoryFPVDrone::Launch(PC,TEXT("keyboard"),Error);
        }();
        if (!Drone) return false;
        ON_SCOPE_EXIT { if (IsValid(Drone) && !Drone->IsActorBeingDestroyed()) Drone->ExitDrone(); };
        bRuntimeOsdLayoutVerified=Drone->Overlay.IsValid() && OsdLayoutMatches(Drone->Overlay->Data.Layout,true);
        if (!bRuntimeOsdLayoutVerified) {
            Error=TEXT("The real flight overlay did not receive the saved speed and timer positions.");return false;
        }
        bRuntimeThrottleExpoVerified=LoadedThrottleExpoWorks(Drone->Flight,1. / Settings->ThrustToWeight);
        if (!bRuntimeThrottleExpoVerified) {
            Error=TEXT("The launched flight did not use the saved throttle curve while preserving the arm threshold.");return false;
        }
        TInlineComponentArray<UInputComponent*> Components(Drone);
        const UInputComponent* Controls=nullptr;
        for (const auto* Component:Components) if (PC->IsInputComponentInStack(Component)) {
            if (Controls) { Error=TEXT("More than one drone input component was pushed.");return false; }
            Controls=Component;
        }
        FInputActionUnifiedDelegate Action;
        const auto& Keys=Settings->KeyboardShortcuts;
        if (!Controls || !Controls->bBlockInput || Drone->IsFlightArmed() || PC->GetViewTarget()!=Drone ||
            !FindRemappedBinding(*Controls,Drone,Keys.Hangar,EKeys::F6,Action) ||
            !FindRemappedBinding(*Controls,Drone,Keys.Arm,EKeys::F8,Action) ||
            !FindRemappedBinding(*Controls,Drone,Keys.Recover,EKeys::F9,Action) ||
            !FindRemappedBinding(*Controls,Drone,Keys.Return,EKeys::F10,Action)) {
            Error=TEXT("The disarmed session did not bind all four replacement keys exclusively.");return false;
        }
        Action.Execute(Keys.Return);
        const bool bRestored=!AFactoryFPVDrone::FindActive(Current) && PC->GetPawn()==Pioneer && PC->GetViewTarget()==Pioneer &&
            Pioneer->IsAliveAndWell() && PC->IsMoveInputIgnored()==bMoveIgnored && PC->IsLookInputIgnored()==bLookIgnored &&
            !PC->IsInputComponentInStack(Controls);
        if (!bRestored) Error=TEXT("The remapped return callback did not restore the living pioneer and its native input state.");
        return bRestored;
    }

    bool Exercise(SFactoryFPVPanel& Open,UWorld* Current) {
        using namespace FactoryFPVShippingSettingsQAInternal;
        const auto Root=Open.AsShared();
        const auto& Committed=*GetDefault<UFactoryFPVSettings>();
        const auto DisplaysMode=[&](int32 Index) {
            return Open.InputModeCombo && Open.InputMode==Open.InputModes[Index] &&
                Open.InputModeCombo->GetSelectedItem()==Open.InputModes[Index];
        };
        if (!DisplaysMode(bReload ? 1 : 0))
            return Finish(false,TEXT("The mounted input dropdown did not open on the saved preference or fresh Radio default."));
        const auto Apply=[&](const TCHAR* Name,bool bBuiltIn) {
            const auto* Choice=Open.TuningOptions.FindByPredicate([&](const auto& Option) {
                return Option && Option->bBuiltIn==bBuiltIn && Option->Preset.Name==Name;
            });
            if (!Choice || !Open.TuningCombo) return false;
            Open.TuningCombo->SetSelectedItem(*Choice);
            return Open.SelectedTuning==*Choice && Click(Root,TEXT("Apply to draft"));
        };
        if (!Click(Root,TEXT("Presets")) || Open.ActivePage!=SFactoryFPVPanel::EPage::Presets)
            return Finish(false,TEXT("The actual mounted Presets tab did not switch pages through its Slate action."));
        if (!bReload) {
            Open.Draft->OsdLayout.Move(EFactoryFPVOsdItem::Speed,EFactoryFPVOsdSlot::TopRight);
            if (!OsdLayoutMatches(Open.Draft->OsdLayout,true) || !OsdLayoutMatches(Committed.OsdLayout,false) || !Open.HasUnsavedChanges())
                return Finish(false,TEXT("Moving Speed to Timer's slot did not create an independent, unsaved OSD layout swap."));
            Open.InputModeCombo->SetSelectedItem(Open.InputModes[1]);
            if (!DisplaysMode(1) || Open.Draft->PreferredInputMode!=TEXT("keyboard") ||
                Committed.PreferredInputMode!=TEXT("radio") || !Open.HasUnsavedChanges())
                return Finish(false,TEXT("Selecting Keyboard did not create an independent, unsaved input mode edit."));
            // Seed only the disposable hangar draft. The real Save settings
            // button below is the only configuration write made by the probe.
            Open.Draft->SetProfile(Profile(false));
            Open.Draft->SetProfile(Profile(true));
            Open.Draft->bAllowSandboxAccess=false;
            Open.Draft->bOsdCrosshair=false;
            Open.Draft->OsdOpacity=.75f;
            bSeededSyntheticRadio=RadioAndPolicyMatch(*Open.Draft) && Committed.RadioProfiles.IsEmpty();
            bPresetApplied=Apply(TEXT("Factory cruiser"),true) && Open.Draft->CameraTilt==10.f && Open.Draft->CameraFov==100.f &&
                Open.Draft->bAngleMode && HasDefaultRateOverrides(*Open.Draft) && Open.Draft->ThrottleExpo==0.f &&
                RadioAndPolicyMatch(*Open.Draft) && Open.HasUnsavedChanges();
            if (!bSeededSyntheticRadio || !bPresetApplied)
                return Finish(false,TEXT("Synthetic draft profiles or the actual Factory cruiser apply did not preserve radio and policy settings."));
            Open.Draft->PitchRate=SavedPitchRate;
            Open.Draft->YawRate=SavedYawRate;
            Open.Draft->RollCenterRate=SavedRollCenterRate;
            Open.Draft->PitchCenterRate=SavedPitchCenterRate;
            Open.Draft->YawCenterRate=SavedYawCenterRate;
            Open.Draft->ThrottleExpo=SavedThrottleExpo;
            auto& Keys=Open.Draft->KeyboardShortcuts;
            Keys.Hangar=EKeys::F2;
            Keys.Arm=EKeys::F3;
            Keys.Recover=EKeys::F4;
            Keys.Return=EKeys::F7;
            Open.TuningName=PresetName;
            bPresetStored=Click(Root,TEXT("Store current tuning in draft")) && SettingsMatch(*Open.Draft) &&
                Committed.TuningPresets.IsEmpty() && HasDefaultRateOverrides(Committed) && ShortcutsMatch(Committed,false) && Open.HasUnsavedChanges();
            if (!bPresetStored) return Finish(false,TEXT("The actual Store button did not add exactly one custom preset to the draft."));
            bSettingsSaved=Click(Root,TEXT("Save settings")) && SettingsMatch(Committed) && !Open.HasUnsavedChanges() && ReadCommittedIni(ConfigPath,Before);
            if (!bSettingsSaved) return Finish(false,TEXT("The actual Save settings button did not commit the draft and the disposable INI."));
            const auto Refused=[&](const FString& Command) {
                FStringOutputDevice Reply;
                return IConsoleManager::Get().ProcessUserConsoleInput(*Command,Reply,Current) &&
                    Reply.Contains(CloseHangarWarning) && SettingsMatch(Committed);
            };
            bRadioAxisRefused=Refused(TEXT("fpv.radio.axis roll 7 10 500 1000 reverse"));
            bRadioSelectRefused=Refused(FString(TEXT("fpv.radio.select "))+OtherId);
            if (!bRadioAxisRefused || !bRadioSelectRefused)
                return Finish(false,TEXT("One of the registered console radio writes was not refused while the real hangar draft was open."));
        } else {
            bSavedPresetSelectable=Apply(TEXT("Acro freestyle"),true) && Open.Draft->CameraTilt==25.f && !Open.Draft->bAngleMode &&
                HasDefaultRateOverrides(*Open.Draft) && Open.Draft->ThrottleExpo==0.f && Open.HasUnsavedChanges();
            bPresetApplied=bSavedPresetSelectable && Apply(PresetName,false) && SettingsMatch(*Open.Draft) && !Open.HasUnsavedChanges();
            if (!bPresetApplied) return Finish(false,TEXT("The reloaded custom preset did not apply through the actual dropdown and Slate action."));
        }
        Open.InputModeCombo->SetSelectedItem(Open.InputModes[2]);
        Open.Draft->OsdLayout.Move(EFactoryFPVOsdItem::Speed,EFactoryFPVOsdSlot::BottomLeft);
        if (!DisplaysMode(2) || Open.Draft->PreferredInputMode!=TEXT("gamepad") || !Open.HasUnsavedChanges() ||
            !Apply(TEXT("Micro explorer"),true) || Open.Draft->FrameSizeCm!=15.f || Open.Draft->PreferredInputMode!=TEXT("gamepad") ||
            !OsdLayoutMatches(Open.Draft->OsdLayout,false))
            return Finish(false,TEXT("The second actual draft edit did not remain independent of the committed settings."));
        bDefaultCenterRatesVerified=HasDefaultRateOverrides(*Open.Draft);
        if (!bDefaultCenterRatesVerified || Open.Draft->ThrottleExpo!=0.f || !RadioAndPolicyMatch(*Open.Draft) || !SettingsMatch(Committed))
            return Finish(false,TEXT("The built-in preset did not restore the draft's default rates and throttle or preserve committed settings."));
        bProfilePreserved=RadioAndPolicyMatch(*Open.Draft) && RadioAndPolicyMatch(Committed);
        const auto MountedRoot=Hangar.IsValid() ? Hangar->Overlay : nullptr;
        const auto DraftKept=[&] {
            return Hangar.IsValid() && Hangar->Panel==Panel.Pin() && Hangar->Overlay==MountedRoot &&
                DisplaysMode(2) && Open.Draft->FrameSizeCm==15.f && HasDefaultRateOverrides(*Open.Draft) &&
                OsdLayoutMatches(Open.Draft->OsdLayout,false) && Open.HasPendingEdits() && SettingsMatch(Committed);
        };
        if (!MountedRoot || !Click(Root,TEXT("Close   Esc")) || !DraftKept() ||
            !Click(MountedRoot.ToSharedRef(),TEXT("Keep editing")) || !DraftKept() ||
            FindButton(MountedRoot.ToSharedRef(),TEXT("Keep editing")) || !Click(Root,TEXT("Close   Esc")) || !DraftKept())
            return Finish(false,TEXT("Close or Keep editing did not protect the actual mounted Gamepad and tuning draft."));
        bDraftDiscarded=Click(MountedRoot.ToSharedRef(),TEXT("Discard edits")) && Hangar.IsValid() && !Hangar->IsPanelOpen() && SettingsMatch(Committed);
        bCloseConfirmationVerified=bDraftDiscarded;
        FString After;
        bConfigValuesVerified=ReadCommittedIni(ConfigPath,After) && Before.Equals(After,ESearchCase::CaseSensitive);
        bInputModeWorkflowVerified=bDraftDiscarded && bConfigValuesVerified;
        if (!bProfilePreserved || !bDraftDiscarded || !bConfigValuesVerified)
            return Finish(false,TEXT("The actual Close action or refused writes did not preserve the committed preset and exact disposable config."));
        if (bReload) {
            FString Error;
            if (!VerifyRemappedFlight(Current,Error)) return Finish(false,Error);
            bAwaitingShortcutReturn=true;
            Reason=TEXT("Waiting for the remapped return callback to release the pioneer onto its original ground.");
            return true;
        }
        return Finish(true,TEXT("Real hangar actions saved the Keyboard preference, preset and both synthetic radio profiles, refused console writes and protected the later Gamepad draft until explicit discard."));
    }

    bool Finish(bool Success,const FString& Message) const {
        using namespace FactoryFPVShippingSettingsQAInternal;
        if (auto* Subsystem=Hangar.Get();Subsystem && Panel.IsValid() && Subsystem->Panel==Panel.Pin()) Subsystem->ClosePanel();
        const auto& Settings=*GetDefault<UFactoryFPVSettings>();
        const auto* Preset=Settings.TuningPresets.Num()==1 ? &Settings.TuningPresets[0] : nullptr;
        const FString Name=Preset ? Preset->Name : FString();
        FString Extra=FString::Printf(TEXT(",\"configIsolated\":%s,\"hangarOpened\":%s,\"presetApplied\":%s,\"draftDiscarded\":%s,\"profilePreserved\":%s,\"configValuesVerified\":%s,\"presetStored\":%s,\"settingsSaved\":%s,\"radioAxisRefused\":%s,\"radioSelectRefused\":%s,\"seededSyntheticRadio\":%s,\"naturalConfigReload\":%s,\"savedPresetSelectable\":%s,\"inputModeWorkflowVerified\":%s,\"closeConfirmationVerified\":%s,\"preferredInputMode\":\"%s\",\"configPath\":\"%s\",\"presetCount\":%d,\"radioProfileCount\":%d,\"presetName\":\"%s\",\"cameraTilt\":%.6f,\"cameraFov\":%.6f,\"angleMode\":%s"),
            Flag(bConfigIsolated),Flag(bHangarOpened),Flag(bPresetApplied),Flag(bDraftDiscarded),Flag(bProfilePreserved),Flag(bConfigValuesVerified),
            Flag(bPresetStored),Flag(bSettingsSaved),Flag(bRadioAxisRefused),Flag(bRadioSelectRefused),Flag(bSeededSyntheticRadio),Flag(bNaturalConfigReload),
            Flag(bSavedPresetSelectable),Flag(bInputModeWorkflowVerified),Flag(bCloseConfirmationVerified),*Escape(Settings.PreferredInputMode.ToString().ToLower()),
            *Escape(ConfigPath),Settings.TuningPresets.Num(),Settings.RadioProfiles.Num(),*Escape(Name),Settings.CameraTilt,Settings.CameraFov,Flag(Settings.bAngleMode));
        Extra+=FString::Printf(TEXT(",\"acroRate\":%.6f,\"pitchRate\":%.6f,\"yawRate\":%.6f,\"presetPitchRate\":%.6f,\"presetYawRate\":%.6f"),
            Settings.AcroRate,Settings.PitchRate,Settings.YawRate,Preset ? Preset->PitchRate : 0.f,Preset ? Preset->YawRate : 0.f);
        Extra+=FString::Printf(TEXT(",\"rollCenterRate\":%.6f,\"pitchCenterRate\":%.6f,\"yawCenterRate\":%.6f,"
            "\"presetRollCenterRate\":%.6f,\"presetPitchCenterRate\":%.6f,\"presetYawCenterRate\":%.6f,\"defaultCenterRatesVerified\":%s"),
            Settings.RollCenterRate,Settings.PitchCenterRate,Settings.YawCenterRate,Preset ? Preset->RollCenterRate : 0.f,
            Preset ? Preset->PitchCenterRate : 0.f,Preset ? Preset->YawCenterRate : 0.f,Flag(bDefaultCenterRatesVerified));
        Extra+=FString::Printf(TEXT(",\"osdLayoutVerified\":%s,\"runtimeOsdLayoutVerified\":%s"),
            Flag(OsdLayoutMatches(Settings.OsdLayout,true)),Flag(bRuntimeOsdLayoutVerified));
        Extra+=FString::Printf(TEXT(",\"throttleExpo\":%.6f,\"presetThrottleExpo\":%.6f,\"runtimeThrottleExpoVerified\":%s"),
            Settings.ThrottleExpo,Preset ? Preset->ThrottleExpo : 0.f,Flag(bRuntimeThrottleExpoVerified));
        const auto& Keys=Settings.KeyboardShortcuts;
        Extra+=FString::Printf(TEXT(",\"hangarShortcut\":\"%s\",\"armShortcut\":\"%s\",\"recoverShortcut\":\"%s\",\"returnShortcut\":\"%s\","
            "\"remappedHangarVerified\":%s,\"remappedFlightVerified\":%s"),*Escape(Keys.Hangar.GetFName().ToString()),*Escape(Keys.Arm.GetFName().ToString()),
            *Escape(Keys.Recover.GetFName().ToString()),*Escape(Keys.Return.GetFName().ToString()),Flag(bRemappedHangarVerified),Flag(bRemappedFlightVerified));
        Marker(bReload ? TEXT("settings-reload") : TEXT("settings-save"),Success ? TEXT("PASS") : TEXT("FAIL"),Message,Extra);
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
        return false;
    }
};

void FactoryFPVShippingSettingsQA::Begin(UWorld* World,FOutputDevice& Output) {
    using namespace FactoryFPVShippingSettingsQAInternal;
    static TWeakPtr<FFactoryFPVShippingSettingsQA> Active;
    FString Phase,Config,SaveDirectory,Error;
    FParse::Value(FCommandLine::Get(),TEXT("-FactoryFPVQASettings="),Phase);
    const bool Reload=Phase==TEXT("reload");
    const auto* Stage=Reload ? TEXT("settings-reload") : TEXT("settings-save");
    if (Phase!=TEXT("save") && !Reload)
        Error=FString::Printf(TEXT("Settings QA requires -FactoryFPVQASettings=save|reload; received '%s'."),*Phase.Left(24));
    else if (Active.IsValid() || !World || World->GetNetMode()!=NM_Standalone)
        Error=TEXT("Settings QA requires an idle probe and the live standalone world.");
    else GuardPaths(Config,SaveDirectory,Error);
    if (!Error.IsEmpty()) {
        Output.Log(*Error);
        Marker(Stage,TEXT("FAIL"),Error);
        FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    const auto Run=MakeShared<FFactoryFPVShippingSettingsQA>();
    Run->World=World;
    Run->bReload=Reload;
    Active=Run;
    Marker(Stage,TEXT("PENDING"),TEXT("Waiting for the native startup and the exact disposable config workflow."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.1f);
}
