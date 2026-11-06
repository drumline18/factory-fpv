#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Misc/OutputDevice.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVPropAudioComponent.h"
#include "FactoryFPVAudioSettings.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGGameMode.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "FactoryFPVShippingQA.h"
#include "FactoryFPVShippingKitQA.h"
#include "FactoryFPVShippingHubQA.h"
#include "FactoryFPVShippingBuildGunQA.h"
#include "FactoryFPVShippingCameraQA.h"
#include "FactoryFPVShippingInputQA.h"
#include "FactoryFPVShippingRecoveryQA.h"
#include "FactoryFPVShippingLifecycleQA.h"
#include "FactoryFPVShippingTravelQA.h"
#include "FactoryFPVShippingPossessionQA.h"
#include "FactoryFPVShippingQuitQA.h"
#include "FactoryFPVShippingSaveQA.h"
#include "FactoryFPVShippingSettingsQA.h"
#include "FactoryFPVShippingPropAudioQA.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/UnrealType.h"
#include "WindowsRadio.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "Engine/LocalPlayer.h"

class FFactoryFPVModule final : public IModuleInterface {
    IConsoleObject* StartCommand=nullptr;
    IConsoleObject* MenuCommand=nullptr;
    IConsoleObject* StatusCommand=nullptr;
    IConsoleObject* KitStatusCommand=nullptr;
    IConsoleObject* StopCommand=nullptr;
    IConsoleObject* RadioListCommand=nullptr;
    IConsoleObject* RadioSelectCommand=nullptr;
    IConsoleObject* RadioAxisCommand=nullptr;
    IConsoleObject* WorldQABeginCommand=nullptr;
    IConsoleObject* WorldQAVerifyCommand=nullptr;
    IConsoleObject* KitQACommand=nullptr;
    IConsoleObject* HubQACommand=nullptr;
    IConsoleObject* BuildGunQACommand=nullptr;
    IConsoleObject* CameraQACommand=nullptr;
    IConsoleObject* InputQACommand=nullptr;
    IConsoleObject* RecoveryQACommand=nullptr;
    IConsoleObject* LifecycleQACommand=nullptr;
    IConsoleObject* TravelQACommand=nullptr;
    IConsoleObject* PossessionQACommand=nullptr;
    IConsoleObject* QuitQACommand=nullptr;
    IConsoleObject* SaveQACommand=nullptr;
    IConsoleObject* SettingsQACommand=nullptr;
    IConsoleObject* PropAudioQACommand=nullptr;
public:
    virtual bool SupportsDynamicReloading() override { return false; }
    virtual void StartupModule() override {
        FactoryFPVInputLease::Startup();
        StatusCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.status"),
            TEXT("Report local player/hangar readiness, calibration and session state."),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::Status), ECVF_Default);
        KitStatusCommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.kit.status"),
            TEXT("Read-only developer diagnostic for FPV Radio Kit inventory eligibility and capacity."),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::KitStatus),ECVF_Default);
        MenuCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.menu"),
            TEXT("Open the Factory FPV hangar. The hangar shortcut defaults to F6."),
            FConsoleCommandWithWorldDelegate::CreateStatic(&FFactoryFPVModule::ShowMenu), ECVF_Default);
        StartCommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.start"),
            TEXT("fpv.start [keyboard|gamepad|radio|calibrate] -- start a disarmed single-player flight. Close the console to fly."),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::Start),ECVF_Default);
        StopCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.stop"),
            TEXT("Exit the local FPV session and restore pioneer controls."),
            FConsoleCommandWithWorldDelegate::CreateStatic(&FFactoryFPVModule::Stop), ECVF_Default);
        RadioAxisCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.radio.axis"),
            TEXT("fpv.radio.axis <roll|pitch|yaw|throttle> <axis 0-7> <min> <center> <max> [reverse]"),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::ConfigureAxis), ECVF_Default);
        RadioSelectCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.radio.select"),
            TEXT("fpv.radio.select <GUID> - select and save a connected radio. Exit drone view first."),
            FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::SelectRadio), ECVF_Default);
        RadioListCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.radio.list"),
            TEXT("List connected Windows controllers and their DirectInput IDs."),
            FConsoleCommandWithOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::ListRadios), ECVF_Default);
        // These mutation checks are absent from ordinary launches. The helper
        // independently requires a disposable project/save directory and clone.
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) {
            WorldQABeginCommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.world.begin"),
                TEXT("Opt-in API QA on a disposable factory clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::BeginWorldQA),ECVF_Default);
            WorldQAVerifyCommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.world.verify"),
                TEXT("Opt-in API QA reload verification on its disposable roundtrip only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::VerifyWorldQA),ECVF_Default);
            KitQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.kit"),
                TEXT("Opt-in native workshop/equipment API QA on a disposable factory clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::KitQA),ECVF_Default);
            HubQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.hub"),
                TEXT("Opt-in paid native HUB milestone QA on a disposable factory clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::HubQA),ECVF_Default);
            BuildGunQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.buildgun"),
                TEXT("Opt-in native Build Gun placement QA on a disposable factory clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::BuildGunQA),ECVF_Default);
            CameraQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.camera"),
                TEXT("Opt-in disarmed camera/lens/return QA on a disposable factory clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::CameraQA),ECVF_Default);
            InputQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.input"),
                TEXT("Opt-in real native reset/input ownership QA on a disposable clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::InputQA),ECVF_Default);
            RecoveryQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.recovery"),
                TEXT("Opt-in real pioneer return/recovery QA with transient owned supports on a disposable clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::RecoveryQA),ECVF_Default);
            LifecycleQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.lifecycle"),
                TEXT("Opt-in native death/respawn baseline, flight or panel QA on a disposable clone only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::LifecycleQA),ECVF_Default);
            TravelQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.travel"),
                TEXT("Opt-in native same-clone reload from baseline, disarmed FPV or hangar on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::TravelQA),ECVF_Default);
            PossessionQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.possession"),
                TEXT("Opt-in native unpossess/repossess baseline, disarmed FPV or hangar on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::PossessionQA),ECVF_Default);
            QuitQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.quit"),
                TEXT("Opt-in orderly native quit from baseline, disarmed FPV or hangar on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::QuitQA),ECVF_Default);
            SaveQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.save"),
                TEXT("Opt-in asynchronous save during disarmed FPV and second-process reload on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::SaveQA),ECVF_Default);
            SettingsQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.settings"),
                TEXT("Opt-in hangar settings save and restart QA on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::SettingsQA),ECVF_Default);
            PropAudioQACommand=IConsoleManager::Get().RegisterConsoleCommand(TEXT("fpv.qa.propaudio"),
                TEXT("Opt-in silent mixer and FPV prop-voice QA on disposable local state only."),
                FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateStatic(&FFactoryFPVModule::PropAudioQA),ECVF_Default);
        }
    }
    virtual void ShutdownModule() override {
        FactoryFPVInputLease::Shutdown();
        if (StatusCommand) IConsoleManager::Get().UnregisterConsoleObject(StatusCommand);
        if (KitStatusCommand) IConsoleManager::Get().UnregisterConsoleObject(KitStatusCommand);
        if (MenuCommand) IConsoleManager::Get().UnregisterConsoleObject(MenuCommand);
        if (RadioAxisCommand) IConsoleManager::Get().UnregisterConsoleObject(RadioAxisCommand);
        if (RadioSelectCommand) IConsoleManager::Get().UnregisterConsoleObject(RadioSelectCommand);
        if (StopCommand) IConsoleManager::Get().UnregisterConsoleObject(StopCommand);
        if (RadioListCommand) IConsoleManager::Get().UnregisterConsoleObject(RadioListCommand);
        if(StartCommand) IConsoleManager::Get().UnregisterConsoleObject(StartCommand);
        if(WorldQABeginCommand) IConsoleManager::Get().UnregisterConsoleObject(WorldQABeginCommand);
        if(WorldQAVerifyCommand) IConsoleManager::Get().UnregisterConsoleObject(WorldQAVerifyCommand);
        if(KitQACommand) IConsoleManager::Get().UnregisterConsoleObject(KitQACommand);
        if(HubQACommand) IConsoleManager::Get().UnregisterConsoleObject(HubQACommand);
        if(BuildGunQACommand) IConsoleManager::Get().UnregisterConsoleObject(BuildGunQACommand);
        if(CameraQACommand) IConsoleManager::Get().UnregisterConsoleObject(CameraQACommand);
        if(InputQACommand) IConsoleManager::Get().UnregisterConsoleObject(InputQACommand);
        if(RecoveryQACommand) IConsoleManager::Get().UnregisterConsoleObject(RecoveryQACommand);
        if(LifecycleQACommand) IConsoleManager::Get().UnregisterConsoleObject(LifecycleQACommand);
        if(TravelQACommand) IConsoleManager::Get().UnregisterConsoleObject(TravelQACommand);
        if(PossessionQACommand) IConsoleManager::Get().UnregisterConsoleObject(PossessionQACommand);
        if(QuitQACommand) IConsoleManager::Get().UnregisterConsoleObject(QuitQACommand);
        if(SaveQACommand) IConsoleManager::Get().UnregisterConsoleObject(SaveQACommand);
        if(SettingsQACommand) IConsoleManager::Get().UnregisterConsoleObject(SettingsQACommand);
        if(PropAudioQACommand) IConsoleManager::Get().UnregisterConsoleObject(PropAudioQACommand);
    }
    static void ShowMenu(UWorld* World) {
        auto* PC = World ? World->GetFirstPlayerController() : nullptr;
        if (PC && PC->GetLocalPlayer()) {
            if (auto* Hangar = PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()) Hangar->TogglePanel();
        }
    }
    static void Status(const TArray<FString>&, UWorld* World, FOutputDevice& Output) {
        auto* PC = World ? World->GetFirstPlayerController() : nullptr;
        auto* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
        auto* Hangar = LocalPlayer ? LocalPlayer->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        const auto* Settings = GetDefault<UFactoryFPVSettings>();
        Output.Logf(TEXT("FPV status: local-player=%s hangar-subsystem=%s hangar-open=%s drone=%s radio-calibrated=%s"),
            LocalPlayer ? TEXT("yes") : TEXT("no"), Hangar ? TEXT("ready") : TEXT("missing"),
            Hangar && Hangar->IsPanelOpen() ? TEXT("yes") : TEXT("no"),
            AFactoryFPVDrone::FindActive(World) ? TEXT("yes") : TEXT("no"),
            Settings->IsRadioCalibrated() ? TEXT("yes") : TEXT("no"));
        Output.Logf(TEXT("FPV game audio: %s"),*FactoryFPVAudioSettings::GameGainStatus());
        Output.Logf(TEXT("FPV world: name=%s begun-play=%s net-mode=%d"),
            *GetNameSafe(World),World && World->HasBegunPlay() ? TEXT("yes") : TEXT("no"),
            World ? static_cast<int32>(World->GetNetMode()) : INDEX_NONE);
        if (IsValid(World)) if (auto* Session=UFGSaveSession::Get(World)) {
            const FSaveHeader& Header=Session->GetLoadedSaveHeader();
            const auto* GameMode=World->GetAuthGameMode<AFGGameMode>();
            const FGameModeLoadData Load=GameMode ? GameMode->GetLoadData() : FGameModeLoadData{};
            Output.Logf(TEXT("FPV save: load-valid=%s requested=\"%s\" header=\"%s\" session=\"%s\" identifier=\"%s\" build=%d play-seconds=%d partitioned=%s save-pending=%s"),
                Load.IsValidLoad ? TEXT("yes") : TEXT("no"),*Load.LoadName,*Header.SaveName,
                *Header.SessionName,*Header.SaveIdentifier,Header.BuildVersion,Header.PlayDurationSeconds,
                Header.IsPartitionedWorld ? TEXT("yes") : TEXT("no"),
                Session->HasSaveGameOperationPending() ? TEXT("yes") : TEXT("no"));
            FString Resolved;
            const FString SaveName=Load.LoadName.IsEmpty() ? Header.SaveName : Load.LoadName;
            const bool bResolved=!SaveName.IsEmpty() && UFGSaveSystem::GetAbsolutePathForSaveGame(World,SaveName,Resolved);
            Output.Logf(TEXT("FPV save paths: directory=\"%s\" resolved=%s file=\"%s\""),
                *UFGSaveSystem::GetSaveDirectoryPath(),bResolved ? TEXT("yes") : TEXT("no"),*Resolved);
        }
        if (auto* Drone=AFactoryFPVDrone::FindActive(World)) {
            if (auto* Audio=Drone->FindComponentByClass<UFactoryFPVPropAudioComponent>()) {
                Output.Logf(TEXT("FPV prop audio: playing=%s rendered-samples=%llu volume=%.2f"),
                    Audio->IsPlaying() ? TEXT("yes") : TEXT("no"),Audio->GetRenderedSampleCount(),Settings->PropVolume);
            }
        }
    }
    static void BeginWorldQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        KitStatus(Args,World,Output);
        FFactoryFPVShippingQA::Begin(World,Output);
    }
    static void VerifyWorldQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingQA::Verify(World,Output);
    }
    static void KitQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        KitStatus(Args,World,Output);
        FFactoryFPVShippingKitQA::Begin(World,Output);
    }
    static void HubQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FactoryFPVShippingHubQA::Begin(World,Output);
    }
    static void SettingsQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FactoryFPVShippingSettingsQA::Begin(World,Output);
    }
    static void PropAudioQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FactoryFPVShippingPropAudioQA::Begin(World,Output);
    }
    static void BuildGunQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FactoryFPVShippingBuildGunQA::Begin(World,Output);
    }
    static void CameraQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingCameraQA::Begin(World,Output);
    }
    static void InputQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingInputQA::Begin(World,Output);
    }
    static void RecoveryQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingRecoveryQA::Begin(World,Output);
    }
    static void LifecycleQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingLifecycleQA::Begin(World,Output);
    }
    static void TravelQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingTravelQA::Begin(World,Output);
    }
    static void PossessionQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingPossessionQA::Begin(World,Output);
    }
    static void QuitQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingQuitQA::Begin(World,Output);
    }
    static void SaveQA(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        Status(Args,World,Output);
        FFactoryFPVShippingSaveQA::Begin(World,Output);
    }
    static void Stop(UWorld* World) {
        if (!World || World->GetNetMode() != NM_Standalone) return;
        for (TActorIterator<AFactoryFPVDrone> It(World); It; ++It) {
            if (!It->IsActorBeingDestroyed()) {
                It->Destroy();
                return;
            }
        }
    }
    static void KitStatus(const TArray<FString>&,UWorld* World,FOutputDevice& Output) {
        auto Report=[&](const FString& Line) {
            Output.Log(*Line);
            UE_LOG(LogTemp,Display,TEXT("%s"),*Line);
        };
        if (!IsValid(World)) { Report(TEXT("FPV kit status: no game world."));return; }
        auto* PC=World->GetFirstPlayerController();
        auto* Pioneer=IsValid(PC) ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        auto* Inventory=IsValid(Pioneer) ? Pioneer->GetInventory() : nullptr;
        if (!IsValid(Inventory)) { Report(TEXT("FPV kit status: no current pioneer inventory; load a game first."));return; }
        const TSubclassOf<UFGItemDescriptor> DescriptorClass=UFactoryFPVRadioDescriptor::StaticClass();
        const auto* Descriptor=GetDefault<UFactoryFPVRadioDescriptor>();
        // Capture the cold CDO cache before any normal getter can populate it.
        const auto* CacheProperty=FindFProperty<FIntProperty>(UFGItemDescriptor::StaticClass(),TEXT("mCachedStackSize"));
        const int32 CacheBefore=CacheProperty ? CacheProperty->GetPropertyValue_InContainer(Descriptor) : INDEX_NONE;
        const FInventoryItem Item(DescriptorClass);
        const FInventoryStack Stack(1,DescriptorClass);
        const int32 DescriptorSize=UFGItemDescriptor::GetStackSize(DescriptorClass);
        Report(FString::Printf(TEXT("FPV kit descriptor: class=%s abstract=%s cache-before=%d stack-size=%d item-valid=%s item-stack-size=%d product-count=%d product-item-stack-size=%d product-max-stack-size=%d"),
            *DescriptorClass->GetPathName(),DescriptorClass->HasAnyClassFlags(CLASS_Abstract) ? TEXT("yes") : TEXT("no"),
            CacheBefore,DescriptorSize,Item.IsValid() ? TEXT("yes") : TEXT("no"),Item.GetItemStackSize(),
            Stack.NumItems,Stack.Item.GetItemStackSize(),Stack.CachedMaxStackSize));
        const int32 EmptyIndex=Inventory->FindEmptyIndex();
        const bool ValidEmptyIndex=Inventory->IsValidIndex(EmptyIndex);
        const int32 EmptySlotSize=ValidEmptyIndex ? Inventory->GetSlotSize(EmptyIndex,DescriptorClass) : INDEX_NONE;
        int32 EmptyCount=0;
        for (int32 Index=0;Index<Inventory->GetSizeLinear();++Index) if (Inventory->IsIndexEmpty(Index)) ++EmptyCount;
        Report(FString::Printf(TEXT("FPV kit inventory: slots=%d empty=%d first-empty=%d item-allowed=%s empty-slot-allowed=%s empty-slot-size=%d enough-space-for-one=%s"),
            Inventory->GetSizeLinear(),EmptyCount,EmptyIndex,
            Inventory->IsItemAllowed(DescriptorClass) ? TEXT("yes") : TEXT("no"),
            ValidEmptyIndex && Inventory->IsItemAllowed(DescriptorClass,EmptyIndex) ? TEXT("yes") : TEXT("no"),
            EmptySlotSize,Inventory->HasEnoughSpaceForStack(Stack) ? TEXT("yes") : TEXT("no")));
    }
    static bool RefuseRadioWriteDuringHangar(UWorld& World,FOutputDevice& Output) {
        const auto* PC=World.GetFirstPlayerController();
        const auto* Local=PC ? PC->GetLocalPlayer() : nullptr;
        const auto* Hangar=Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
        if (!Hangar || !Hangar->IsHangarOpen()) return false;
        Output.Log(TEXT("FPV: close the hangar before changing radio settings in the console."));
        return true;
    }
    static void ConfigureAxis(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output) {
        if (!World || World->GetNetMode() != NM_Standalone) return;
        if (RefuseRadioWriteDuringHangar(*World,Output)) return;
        for (TActorIterator<AFactoryFPVDrone> It(World); It; ++It) {
            if (!It->IsActorBeingDestroyed()) {
                Output.Log(TEXT("FPV: return to your pioneer before changing calibration."));
                return;
            }
        }
        int32 Axis = -1;
        FVector Range = FVector::ZeroVector;
        if ((Args.Num() != 5 && Args.Num() != 6) ||
            !LexTryParseString(Axis, *Args[1]) || Axis < 0 || Axis > 7 ||
            !LexTryParseString(Range.X, *Args[2]) || !LexTryParseString(Range.Y, *Args[3]) ||
            !LexTryParseString(Range.Z, *Args[4]) ||
            !factoryfpv::Calibration{Range.X, Range.Y, Range.Z, false}.valid() ||
            (Args.Num() == 6 && !Args[5].Equals(TEXT("reverse"), ESearchCase::IgnoreCase))) {
            Output.Log(TEXT("Usage: fpv.radio.axis <roll|pitch|yaw|throttle> <axis 0-7> <min> <center> <max> [reverse]"));
            return;
        }
        auto* Settings = GetMutableDefault<UFactoryFPVSettings>();
        struct FAxisSetting {
            const TCHAR* Name;
            int32* Channel;
            FVector* Range;
            bool* Reverse;
        };
        FAxisSetting Axes[] = {
            {TEXT("roll"), &Settings->RadioRollAxis, &Settings->RadioRollCalibration, &Settings->ReverseRoll},
            {TEXT("pitch"), &Settings->RadioPitchAxis, &Settings->RadioPitchCalibration, &Settings->ReversePitch},
            {TEXT("yaw"), &Settings->RadioYawAxis, &Settings->RadioYawCalibration, &Settings->ReverseYaw},
            {TEXT("throttle"), &Settings->RadioThrottleAxis, &Settings->RadioThrottleCalibration, &Settings->ReverseThrottle}
        };
        for (const auto& Setting : Axes) {
            if (Args[0].Equals(Setting.Name, ESearchCase::IgnoreCase)) {
                *Setting.Channel = Axis;
                *Setting.Range = Range;
                *Setting.Reverse = Args.Num() == 6;
                Settings->SaveSettings();
                Output.Logf(TEXT("FPV: saved %s calibration."), Setting.Name);
                return;
            }
        }
        Output.Log(TEXT("FPV: choose roll, pitch, yaw, or throttle."));
    }
    static void SelectRadio(const TArray<FString>& Args, UWorld* World, FOutputDevice& Output) {
        if (!World || World->GetNetMode() != NM_Standalone || Args.Num() != 1) {
            Output.Log(TEXT("Usage: fpv.radio.select <GUID>"));
            return;
        }
        if (RefuseRadioWriteDuringHangar(*World,Output)) return;
        for (TActorIterator<AFactoryFPVDrone> It(World); It; ++It) {
            if (!It->IsActorBeingDestroyed()) {
                Output.Log(TEXT("FPV: return to your pioneer before changing radios."));
                return;
            }
        }
        for (const auto& Device : factoryfpv::WindowsRadio::devices()) {
            if (Args[0].Equals(Device.id.c_str(), ESearchCase::IgnoreCase)) {
                auto* Settings = GetMutableDefault<UFactoryFPVSettings>();
                Settings->SelectRadio(Device.id.c_str(), Device.name.c_str());
                Settings->SaveSettings();
                Output.Logf(TEXT("FPV: selected %s. Run fpv.start calibrate."), Device.name.c_str());
                return;
            }
        }
        Output.Log(TEXT("FPV: device not found. Use fpv.radio.list."));
    }
    static void ListRadios(FOutputDevice& Output) {
        Output.Logf(TEXT("FPV configured radio: %s"), *GetDefault<UFactoryFPVSettings>()->RadioDeviceId);
        const auto* Settings = GetDefault<UFactoryFPVSettings>();
        Output.Logf(TEXT("FPV axes roll=%d pitch=%d yaw=%d throttle=%d, pitch reversed=%s"),
            Settings->RadioRollAxis, Settings->RadioPitchAxis, Settings->RadioYawAxis,
            Settings->RadioThrottleAxis, Settings->ReversePitch ? TEXT("yes") : TEXT("no"));
        const auto Devices = factoryfpv::WindowsRadio::devices();
        if (Devices.empty()) Output.Log(TEXT("FPV: no DirectInput controllers found."));
        for (const auto& Device : Devices) {
            Output.Logf(TEXT("FPV radio: %s %s"), Device.name.c_str(), Device.id.c_str());
        }
    }
    static void Start(const TArray<FString>& Args,UWorld* World,FOutputDevice& Output) {
        if (!World) return;
        auto* PC = World->GetFirstPlayerController();
        const FName Mode = Args.IsEmpty() ? FName(TEXT("keyboard")) : FName(*Args[0]);
        FString Error;
        if (!AFactoryFPVDrone::Launch(PC, Mode, Error)) Output.Log(*Error);
    }
};
IMPLEMENT_MODULE(FFactoryFPVModule,FactoryFPV)
