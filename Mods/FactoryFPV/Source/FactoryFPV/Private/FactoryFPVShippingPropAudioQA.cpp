#include "FactoryFPVShippingPropAudioQA.h"
#include "FactoryFPVAudioSettings.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVFlightCueAudioComponent.h"
#include "FactoryFPVPropAudioComponent.h"
#include "FactoryFPVSettings.h"
#include "FactoryFPVShippingStartup.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ISubmixBufferListener.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundWave.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"

namespace FactoryFPVShippingPropAudioQAInternal {
constexpr TCHAR CloneName[]=TEXT("FPV-QA-Clone");
constexpr TCHAR WwiseSection[]=TEXT("/Script/AkAudio.AkSettings");
constexpr TCHAR WwiseKey[]=TEXT("bWwiseSoundEngineEnabled");
constexpr TCHAR WwiseOverride[]=TEXT("-ini:Game:[/Script/AkAudio.AkSettings]:bWwiseSoundEngineEnabled=False");
constexpr float TestVolume=.55f;
constexpr TCHAR ReceiverCuePath[]=TEXT("/FactoryFPV/Audio/SW_FPVCueReceiverLost.SW_FPVCueReceiverLost");

bool CueAssetsCooked() {
    const TCHAR* Paths[]={TEXT("/FactoryFPV/Audio/SW_FPVCueCheckpoint.SW_FPVCueCheckpoint"),
        TEXT("/FactoryFPV/Audio/SW_FPVCueLap.SW_FPVCueLap"),
        TEXT("/FactoryFPV/Audio/SW_FPVCuePersonalBest.SW_FPVCuePersonalBest"),ReceiverCuePath};
    for (const TCHAR* Path:Paths) {
        const auto* Wave=LoadObject<USoundWave>(nullptr,Path);
        if (!Wave || Wave->IsLooping() || !FMath::IsFinite(Wave->GetDuration()) || Wave->GetDuration()<=0.f) return false;
    }
    return true;
}

FString NormalizedPath(FString Path) {
    Path=FPaths::ConvertRelativePathToFull(Path);
    FPaths::NormalizeDirectoryName(Path);
    if (!FPaths::CollapseRelativeDirectories(Path)) return {};
    return Path;
}

bool Guard(UWorld* World,FString& Reason) {
    const TCHAR* CommandLine=FCommandLine::Get();
    for (const TCHAR* Flag : {TEXT("FactoryFPVAutomation"),TEXT("FactoryFPVQAPropAudio"),TEXT("RenderOffScreen"),
        TEXT("DeterministicAudio"),TEXT("NoAutoSave"),TEXT("SavesUseProjectSavedDir")}) {
        if (!FParse::Param(CommandLine,Flag)) {
            Reason=FString::Printf(TEXT("Missing required isolated prop audio QA flag -%s."),Flag);
            return false;
        }
    }
    bool bExactOverride=false,bWwiseEnabled=true;
    const TCHAR* Remaining=CommandLine;
    FString Arg;
    while (FParse::Token(Remaining,Arg,false)) bExactOverride|=Arg.Equals(WwiseOverride,ESearchCase::IgnoreCase);
    if (FParse::Param(CommandLine,TEXT("nosound")) || !bExactOverride || !GConfig ||
        !GConfig->GetBool(WwiseSection,WwiseKey,bWwiseEnabled,GGameIni) || bWwiseEnabled) {
        Reason=TEXT("The Wwise hardware path must be disabled by the explicit per-process config override while the native mixer remains enabled.");
        return false;
    }
    const FString Saved=NormalizedPath(FPaths::ProjectSavedDir());
    const FString NativePath=UFGSaveSystem::GetSaveDirectoryPath();
    const FString Native=NativePath.IsEmpty() ? FString() : NormalizedPath(NativePath);
    const FString Run=FPaths::GetPath(Saved);
    const FString RunName=FPaths::GetCleanFilename(Run);
    const FString Prefix=TEXT("shipping-world-qa-");
    FGuid RunId;
    if (!World || World->GetNetMode()!=NM_Standalone ||
        !FPaths::GetCleanFilename(Saved).Equals(TEXT("Saved"),ESearchCase::IgnoreCase) ||
        !FPaths::GetCleanFilename(FPaths::GetPath(Run)).Equals(TEXT("build"),ESearchCase::IgnoreCase) ||
        !RunName.StartsWith(Prefix,ESearchCase::IgnoreCase) ||
        !FGuid::ParseExact(RunName.Mid(Prefix.Len()),EGuidFormats::Digits,RunId) || !RunId.IsValid() ||
        !Native.StartsWith(Saved+TEXT("/"),ESearchCase::IgnoreCase)) {
        Reason=TEXT("Prop audio QA requires a standalone world and native saves within build/shipping-world-qa-<guid>/Saved.");
        return false;
    }
    return true;
}

const TCHAR* JsonBool(bool Value) { return Value ? TEXT("true") : TEXT("false"); }

void Marker(const TCHAR* Result,FString Reason,const FString& Evidence={}) {
    Reason=Reason.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"propaudio\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"source-specific PCM from the real non-realtime Shipping mixer, production drone arm and suspension with synthetic flight steps\",\"hardwareAudio\":false,\"physicalInput\":false,\"savedWorld\":false%s}"),
        Result,*Reason,*Evidence);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}

struct FPropPCMSnapshot {
    uint64 Samples=0;
    uint64 SignalSamples=0;
    uint64 NonFiniteSamples=0;
    double SquareSum=0.;
};

class FPropPCMMeter final : public ISubmixBufferListener {
public:
    virtual void OnNewSubmixBuffer(const USoundSubmix*,float* Data,int32 NumSamples,int32,int32,double) override {
        FPropPCMSnapshot Block;
        for (int32 Index=0;Index<NumSamples;++Index) {
            const float Sample=Data[Index];
            if (!FMath::IsFinite(Sample)) { ++Block.NonFiniteSamples; continue; }
            Block.SignalSamples+=FMath::Abs(Sample)>1.e-5f ? 1 : 0;
            Block.SquareSum+=double(Sample)*Sample;
        }
        FScopeLock Lock(&Mutex);
        Total.Samples+=NumSamples;
        Total.SignalSamples+=Block.SignalSamples;
        Total.NonFiniteSamples+=Block.NonFiniteSamples;
        Total.SquareSum+=Block.SquareSum;
    }

    FPropPCMSnapshot Read() const {
        FScopeLock Lock(&Mutex);
        return Total;
    }

private:
    mutable FCriticalSection Mutex;
    FPropPCMSnapshot Total;
};
}
namespace PropAudioQA=FactoryFPVShippingPropAudioQAInternal;

class FFactoryFPVShippingPropAudioQA final {
public:
    explicit FFactoryFPVShippingPropAudioQA(UWorld* InWorld)
        : World(InWorld), Deadline(FPlatformTime::Seconds()+55.) {}

    bool Tick() {
        const double Now=FPlatformTime::Seconds();
        if (Now>=Deadline) return Finish(false,LastReason);
        if (!World.IsValid() || World->bIsTearingDown) return Finish(false,TEXT("The disposable world changed during prop audio QA."));
        if (Phase==EPhase::Waiting) return WaitForWorld(Now);
        if (!Controller.IsValid() || !Pioneer.IsValid() || !Pioneer->IsAliveAndWell() || Controller->GetPawn()!=Pioneer.Get())
            return Finish(false,TEXT("The original pioneer/controller changed during prop audio QA."));
        if (!Voice.IsValid() || (Phase!=EPhase::Returned && (!Drone.IsValid() || Controller->GetViewTarget()!=Drone.Get())))
            return Finish(false,TEXT("The native drone or its audio component disappeared early."));
        if (Meter->Read().NonFiniteSamples!=0) return Finish(false,TEXT("The dedicated Shipping mixer submix received non-finite PCM."));

        switch (Phase) {
            case EPhase::Launched:
                return Silence(Now,TEXT("The new disarmed session retained a prop voice."),bLaunchSilent,[&] {
                    if (!Arm()) return Finish(false,TEXT("The actual drone did not start a mixer voice when armed."));
                    SetPhase(EPhase::Armed,Now);
                    return true;
                });
            case EPhase::Armed:
                return Signal(TEXT("Waiting for mixer callback samples and finite nonzero source PCM after initial arm."),InitialRms,[&] {
                    Drone->SuspendFlightInput();
                    if (Drone->IsFlightArmed()) return Finish(false,TEXT("Flight suspension failed to disarm."));
                    SetPhase(EPhase::Suspended,Now);
                    return true;
                });
            case EPhase::Suspended:
                return Silence(Now,TEXT("Flight suspension failed to retire the mixer voice."),bSuspensionSilent,[&] {
                    Drone->UpdatePropAudio();
                    SetPhase(EPhase::ResumedDisarmed,Now);
                    return true;
                });
            case EPhase::ResumedDisarmed:
                return Silence(Now,TEXT("Resuming updates without arming restarted the mixer voice."),bResumeSilent,[&] {
                    if (!Arm()) return Finish(false,TEXT("The drone did not start a fresh voice on re-arm after suspension."));
                    SetPhase(EPhase::Rearmed,Now);
                    return true;
                });
            case EPhase::Rearmed:
                return Signal(TEXT("Waiting for a fresh callback and source PCM after re-arm."),RearmRms,[&] {
                    GetMutableDefault<UFactoryFPVSettings>()->PropVolume=0.f;
                    Drone->UpdatePropAudio();
                    SetPhase(EPhase::Muted,Now);
                    return true;
                });
            case EPhase::Muted:
                return Silence(Now,TEXT("Explicit zero prop volume failed to retire the armed mixer voice."),bMuteSilent,[&] {
                    GetMutableDefault<UFactoryFPVSettings>()->PropVolume=PropAudioQA::TestVolume;
                    Drone->UpdatePropAudio();
                    if (!SendToMeter()) return Finish(false,TEXT("Raising prop volume failed to restore the armed voice."));
                    SetPhase(EPhase::Unmuted,Now);
                    return true;
                });
            case EPhase::Unmuted:
                return Signal(TEXT("Waiting for mixer callback and source PCM after unmuting."),UnmuteRms,[&] {
                    if (bCheckFlightCues) {
                        GetMutableDefault<UFactoryFPVSettings>()->PropVolume=0.f;
                        Drone->UpdatePropAudio();
                        SetPhase(EPhase::CueReady,Now);
                        return true;
                    }
                    Drone->ExitDrone();
                    SetPhase(EPhase::Returned,Now);
                    return true;
                });
            case EPhase::CueReady:
                return Silence(Now,TEXT("Waiting for prop mixer silence before the receiver-loss cue."),bCueBaselineSilent,[&] {
                    SetPhase(EPhase::ReceiverCue,Now);
                    if (!StartReceiverCue())
                        return Finish(false,TEXT("The native receiver-loss edge did not disarm and start the cooked cue at its expected gain."));
                    return true;
                });
            case EPhase::ReceiverCue:
                return CheckReceiverCueSignal(Now);
            case EPhase::Returned:
                return Silence(Now,TEXT("Returning to the pioneer failed to retire the mixer voice."),bReturnSilent,[&] {
                    const auto* Move=Pioneer->GetCharacterMovement();
                    bPioneerRestored=OriginalView.IsValid() && Controller->GetViewTarget()==OriginalView.Get() &&
                        !Controller->IsMoveInputIgnored() && !Controller->IsLookInputIgnored() &&
                        Move && Move->MovementMode==OriginalMovement && Move->CustomMovementMode==OriginalCustomMovement &&
                        !AFactoryFPVDrone::FindActive(World.Get());
                    if (!bPioneerRestored) {
                        LastReason=TEXT("Waiting for the original view, movement and input to return.");
                        return true;
                    }
                    return Finish(true,TEXT("Actual prop callbacks and source-only PCM verified through arm, suspension, re-arm, explicit mute, unmute and return."));
                });
            case EPhase::Waiting:
                break;
        }
        return Finish(false,TEXT("Unexpected prop audio QA phase."));
    }

private:
    enum class EPhase { Waiting,Launched,Armed,Suspended,ResumedDisarmed,Rearmed,Muted,Unmuted,CueReady,ReceiverCue,Returned };
    EPhase Phase=EPhase::Waiting;
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> Controller;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TStrongObjectPtr<UFactoryFPVPropAudioComponent> Voice;
    TStrongObjectPtr<UFactoryFPVFlightCueAudioComponent> CueVoice;
    FAudioDeviceHandle Device;
    TStrongObjectPtr<USoundSubmix> Submix;
    TSharedPtr<PropAudioQA::FPropPCMMeter,ESPMode::ThreadSafe> Meter;
    EMovementMode OriginalMovement=MOVE_None;
    uint8 OriginalCustomMovement=0;
    float OriginalVolume=0.f;
    double Deadline=0.,PhaseStarted=0.;
    uint64 PhaseFrame=0,CallbackBaseline=0;
    PropAudioQA::FPropPCMSnapshot PCMBaseline;
    FString LastReason=TEXT("Waiting for the actual isolated clone and native startup lifecycle.");
    bool bOriginalSandbox=false,bSettingsCaptured=false,bVoiceVerified=false,bSilenceBaseline=false,bNonRealtime=false;
    bool bSplashObserved=false,bAnimationObserved=false,bSplashFinished=false,bFrontEndFinished=false;
    bool bLaunchSilent=false,bSuspensionSilent=false,bResumeSilent=false,bMuteSilent=false,bReturnSilent=false,bPioneerRestored=false;
    const bool bCheckFlightCues=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAFlightCues"));
    bool bCueAssetsCooked=false,bCueBaselineSilent=false,bReceiverCueStarted=false,bReceiverCueStopped=false;
    double InitialRms=0.,RearmRms=0.,UnmuteRms=0.;
    double ReceiverCueRms=0.,ReceiverCueGain=0.;

    bool WaitForWorld(double Now) {
        auto* Mode=World->GetAuthGameMode<AFGGameMode>();
        auto* Save=Mode ? Mode->GetSaveSession() : nullptr;
        auto* PC=Cast<AFGPlayerController>(World->GetFirstPlayerController());
        auto* Player=PC ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
        if (!World->HasBegunPlay() || !Mode || Mode->IsMainMenuGameMode() || !Save || Save->HasSaveGameOperationPending() ||
            !PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || PC->IsPaused() || PC->IsPauseMenuOpen() ||
            !Player || !Player->IsAliveAndWell()) return true;
        const auto& Header=Save->GetLoadedSaveHeader();
        if ((Mode->GetLoadData().LoadName!=PropAudioQA::CloneName && Header.SaveName!=PropAudioQA::CloneName) ||
            Header.MapName.IsEmpty() || Header.SessionName.IsEmpty()) return true;
        if (!FactoryFPVShippingStartup::Ready(PC->GetGameUI(),LastReason,bSplashObserved,bAnimationObserved,bSplashFinished,bFrontEndFinished)) return true;
        if (PC->GetGameUI()->HasActiveInteractWidget()) {
            LastReason=TEXT("Waiting for the native interaction UI to clear.");
            return true;
        }
        if (AFactoryFPVDrone::FindActive(World.Get()) || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored())
            return Finish(false,TEXT("Prop audio QA requires an unheld pioneer with no existing FPV session."));
        const auto* Move=Player->GetCharacterMovement();
        if (!Move || !PC->GetViewTarget()) return true;
        if (FactoryFPVAudioSettings::GameGain()<=0.)
            return Finish(false,TEXT("Disposable native Master and Effects gains must be nonzero for the prop PCM probe."));

        Device=World->GetAudioDevice();
        auto* Audio=Device.GetAudioDevice();
        bNonRealtime=Audio && Audio->IsNonRealtime() &&
            FModuleManager::Get().IsModuleLoaded(TEXT("NonRealtimeAudioRenderer"));
        if (!bNonRealtime) return Finish(false,TEXT("The native Shipping non-realtime output device was not active. No audio was stimulated."));
        Submix.Reset(NewObject<USoundSubmix>(GetTransientPackage()));
        Submix->bAutoDisable=false;
        Meter=MakeShared<PropAudioQA::FPropPCMMeter,ESPMode::ThreadSafe>();
        Audio->RegisterSoundSubmix(Submix.Get(),true);
        Audio->RegisterSubmixBufferListener(Meter.ToSharedRef(),*Submix.Get());

        Controller=PC;
        Pioneer=Player;
        OriginalView=PC->GetViewTarget();
        OriginalMovement=Move->MovementMode;
        OriginalCustomMovement=Move->CustomMovementMode;
        auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
        OriginalVolume=Settings->PropVolume;
        bOriginalSandbox=Settings->bAllowSandboxAccess;
        bSettingsCaptured=true;
        Settings->PropVolume=PropAudioQA::TestVolume;
        Settings->bAllowSandboxAccess=true;
        FString Error;
        Drone=AFactoryFPVDrone::Launch(PC,TEXT("keyboard"),Error);
        if (!Drone.IsValid()) return Finish(false,TEXT("Ordinary keyboard-mode drone launch failed: ")+Error);
        // Off-screen focus is left alone. Only this disposable actor's polling is
        // stopped while the fixture drives isolated native flight/suspension methods.
        Drone->SetActorTickEnabled(false);
        Voice.Reset(Drone->PropAudio.Get());
        if (!Voice.IsValid()) return Finish(false,TEXT("Native drone lacks its production prop audio component."));
        if (bCheckFlightCues) {
            CueVoice.Reset(Drone->CueAudio.Get());
            bCueAssetsCooked=PropAudioQA::CueAssetsCooked();
            if (!CueVoice.IsValid() || !bCueAssetsCooked)
                return Finish(false,TEXT("The cooked package lacks its production flight-cue component or one of its four one-shots."));
        }
        SetPhase(EPhase::Launched,Now);
        return true;
    }

    bool SendToMeter() {
        if (!Voice->IsPlaying()) return false;
        Voice->SetSubmixSend(Submix.Get(),1.f);
        return true;
    }

    bool Arm() {
        factoryfpv::Input Input;
        if (!Drone->TryArmFlight(Input)) return false;
        Input.throttle=.55;
        for (int Step=0;Step<12 && Drone->IsFlightArmed();++Step) Drone->SimulateFlight(Input,1./120.);
        Drone->UpdatePropAudio();
        return Drone->IsFlightArmed() && SendToMeter();
    }

    bool StartReceiverCue() {
        if (!Drone->IsFlightArmed() || Voice->IsPlaying() || CueVoice->IsPlaying()) return false;
        const double ExpectedGain=GetDefault<UFactoryFPVSettings>()->FlightCueVolume*FactoryFPVAudioSettings::GameGain();
        if (ExpectedGain<=0.) return false;
        // Queue a send on this component only so even the first cue samples reach the meter.
        CueVoice->SetSubmixSend(Submix.Get(),1.f);
        const TGuardValue<FName> RadioMode(Drone->InputMode,FName(TEXT("radio")));
        Drone->AdvanceActiveFlightFrame(factoryfpv::Input{},0.f);
        if (!Drone->IsFlightArmed() || CueVoice->IsPlaying()) return false;
        Drone->AdvanceActiveFlightFrame(factoryfpv::Input{0,0,0,0,false},0.f);
        ReceiverCueGain=CueVoice->VolumeMultiplier;
        bReceiverCueStarted=!Drone->IsFlightArmed() && CueVoice->IsPlaying() &&
            CueVoice->GetActiveCue()==EFactoryFPVFlightCue::ReceiverLost &&
            GetPathNameSafe(CueVoice->GetSound()).Equals(PropAudioQA::ReceiverCuePath,ESearchCase::CaseSensitive) &&
            FMath::IsNearlyEqual(ReceiverCueGain,ExpectedGain,1.e-5);
        return bReceiverCueStarted;
    }

    bool CheckReceiverCueSignal(double Now) {
        LastReason=TEXT("Waiting for isolated receiver-loss cue PCM from the native mixer.");
        const auto Current=Meter->Read();
        const uint64 SampleDelta=Current.Samples-PCMBaseline.Samples;
        if (Current.SignalSamples-PCMBaseline.SignalSamples<128 || !SampleDelta) return true;
        ReceiverCueRms=FMath::Sqrt((Current.SquareSum-PCMBaseline.SquareSum)/SampleDelta);
        if (!FMath::IsFinite(ReceiverCueRms) || ReceiverCueRms<1.e-5 || !CueVoice->IsPlaying() || Voice->IsPlaying())
            return Finish(false,TEXT("The dedicated receiver-loss PCM was inaudible or did not come from its active cue alone."));
        Drone->ExitDrone();
        bReceiverCueStopped=!CueVoice->IsPlaying();
        if (!bReceiverCueStopped) return Finish(false,TEXT("Returning to the pioneer did not stop the active receiver-loss cue."));
        SetPhase(EPhase::Returned,Now);
        return true;
    }

    void SetPhase(EPhase Next,double Now) {
        Phase=Next;
        PhaseStarted=Now;
        PhaseFrame=GFrameCounter;
        bSilenceBaseline=false;
        CaptureBaseline();
    }

    void CaptureBaseline() {
        CallbackBaseline=Voice->GetRenderedSampleCount();
        PCMBaseline=Meter->Read();
    }

    bool Signal(const TCHAR* WaitingReason,double& Rms,TFunctionRef<bool()> Next) {
        LastReason=WaitingReason;
        const auto Current=Meter->Read();
        const uint64 SampleDelta=Current.Samples-PCMBaseline.Samples;
        if (!Voice->IsPlaying() || Voice->GetRenderedSampleCount()-CallbackBaseline<512 ||
            Current.SignalSamples-PCMBaseline.SignalSamples<128 || !SampleDelta) return true;
        Rms=FMath::Sqrt((Current.SquareSum-PCMBaseline.SquareSum)/SampleDelta);
        if (!FMath::IsFinite(Rms) || Rms<1.e-5) return Finish(false,TEXT("Actual source-specific prop PCM was invalid or inaudible."));
        bVoiceVerified=true;
        return Next();
    }

    bool Silence(double Now,const TCHAR* Error,bool& Verified,TFunctionRef<bool()> Next) {
        LastReason=Error;
        // Native Stop commands and already-queued render buffers can drain after
        // the game-thread transition. Check a fresh observation window thereafter.
        if (Now-PhaseStarted<.2 || GFrameCounter-PhaseFrame<4) return true;
        if (!bSilenceBaseline) {
            if (Voice->IsPlaying() || (CueVoice.IsValid() && CueVoice->IsPlaying())) return true;
            CaptureBaseline();
            bSilenceBaseline=true;
            PhaseStarted=Now;
            PhaseFrame=GFrameCounter;
            return true;
        }
        const auto Current=Meter->Read();
        if (Voice->IsPlaying() || (CueVoice.IsValid() && CueVoice->IsPlaying()) || Voice->GetRenderedSampleCount()!=CallbackBaseline ||
            Current.SignalSamples!=PCMBaseline.SignalSamples) return Finish(false,Error);
        if (Current.Samples-PCMBaseline.Samples<512) return true;
        Verified=true;
        return Next();
    }

    bool Finish(bool Success,const FString& Reason) {
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed()) Own->ExitDrone();
        if (Device.IsValid() && Submix.IsValid() && Meter.IsValid()) {
            Device.GetAudioDevice()->UnregisterSubmixBufferListener(Meter.ToSharedRef(),*Submix.Get());
            Device.GetAudioDevice()->UnregisterSoundSubmix(Submix.Get(),false);
        }
        if (bSettingsCaptured) {
            auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
            Settings->PropVolume=OriginalVolume;
            Settings->bAllowSandboxAccess=bOriginalSandbox;
        }
        FString Evidence=FString::Printf(TEXT(",\"nonRealtimeMixer\":%s,\"wwiseDisabled\":true,\"voiceCallbackVerified\":%s,\"launchSilent\":%s,\"suspensionSilent\":%s,\"resumedDisarmedSilent\":%s,\"explicitMuteSilent\":%s,\"returnSilent\":%s,\"pioneerRestored\":%s,\"initialArmRms\":%.8f,\"rearmRms\":%.8f,\"unmuteRms\":%.8f,\"renderedSamples\":%llu"),
            PropAudioQA::JsonBool(bNonRealtime),PropAudioQA::JsonBool(bVoiceVerified),PropAudioQA::JsonBool(bLaunchSilent),
            PropAudioQA::JsonBool(bSuspensionSilent),PropAudioQA::JsonBool(bResumeSilent),PropAudioQA::JsonBool(bMuteSilent),
            PropAudioQA::JsonBool(bReturnSilent),PropAudioQA::JsonBool(bPioneerRestored),
            InitialRms,RearmRms,UnmuteRms,static_cast<unsigned long long>(Voice.IsValid() ? Voice->GetRenderedSampleCount() : 0));
        if (bCheckFlightCues) Evidence+=FString::Printf(TEXT(",\"flightCueAssetsCooked\":%s,\"flightCueBaselineSilent\":%s,"
            "\"receiverCueStarted\":%s,\"receiverCueStopped\":%s,\"receiverCueGain\":%.8f,\"receiverCueRms\":%.8f"),
            PropAudioQA::JsonBool(bCueAssetsCooked),PropAudioQA::JsonBool(bCueBaselineSilent),PropAudioQA::JsonBool(bReceiverCueStarted),
            PropAudioQA::JsonBool(bReceiverCueStopped),ReceiverCueGain,ReceiverCueRms);
        PropAudioQA::Marker(Success ? TEXT("PASS") : TEXT("FAIL"),Reason,Evidence);
        FPlatformMisc::RequestExitWithStatus(false,Success ? 0 : 1);
        return false;
    }
};

void FactoryFPVShippingPropAudioQA::Begin(UWorld* World,FOutputDevice& Output) {
    FString Reason;
    if (!PropAudioQA::Guard(World,Reason)) {
        PropAudioQA::Marker(TEXT("FAIL"),Reason);
        Output.Log(*Reason);
        if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);
        return;
    }
    static TWeakPtr<FFactoryFPVShippingPropAudioQA> Active;
    if (Active.IsValid()) return;
    const auto Run=MakeShared<FFactoryFPVShippingPropAudioQA>(World);
    Active=Run;
    PropAudioQA::Marker(TEXT("PENDING"),TEXT("Waiting for the isolated clone and verifying the silent native audio backend before stimulation."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.05f);
}
