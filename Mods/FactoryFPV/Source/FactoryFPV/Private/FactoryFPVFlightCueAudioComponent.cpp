#include "FactoryFPVFlightCueAudioComponent.h"
#include "AudioDevice.h"
#include "Engine/World.h"
#include "Sound/SoundWave.h"
#include "UObject/ConstructorHelpers.h"

UFactoryFPVFlightCueAudioComponent::UFactoryFPVFlightCueAudioComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
    bAutoActivate=false;
    bAutoDestroy=false;
    bStopWhenOwnerDestroyed=true;
    bAllowSpatialization=false;
    bIsUISound=false;
    bCanPlayMultipleInstances=false;

    static ConstructorHelpers::FObjectFinder<USoundWave> Checkpoint(TEXT("/FactoryFPV/Audio/SW_FPVCueCheckpoint.SW_FPVCueCheckpoint"));
    static ConstructorHelpers::FObjectFinder<USoundWave> Lap(TEXT("/FactoryFPV/Audio/SW_FPVCueLap.SW_FPVCueLap"));
    static ConstructorHelpers::FObjectFinder<USoundWave> PersonalBest(TEXT("/FactoryFPV/Audio/SW_FPVCuePersonalBest.SW_FPVCuePersonalBest"));
    static ConstructorHelpers::FObjectFinder<USoundWave> ReceiverLost(TEXT("/FactoryFPV/Audio/SW_FPVCueReceiverLost.SW_FPVCueReceiverLost"));
    CueSounds={nullptr,Checkpoint.Object.Get(),Lap.Object.Get(),PersonalBest.Object.Get(),ReceiverLost.Object.Get()};
}

bool UFactoryFPVFlightCueAudioComponent::PlayCue(EFactoryFPVFlightCue Cue,double Gain) {
    const int32 Index=static_cast<int32>(Cue);
    if (!FMath::IsFinite(Gain) || Gain<=0. || !CueSounds.IsValidIndex(Index) || !CueSounds[Index] ||
        Cue<GetActiveCue()) return false;
    const auto* World=GetWorld();
    if (!IsRegistered() || !World || World->bIsTearingDown || !World->AllowAudioPlayback() || !World->GetAudioDevice().IsValid())
        return false;

    // SetSound can restart an active component. Stop first so the new clip
    // starts exactly once, with the new gain and no overlapping lower cue.
    Stop();
    SetSound(CueSounds[Index]);
    SetVolumeMultiplier(static_cast<float>(FMath::Min(Gain,1.)));
    ActiveCue=Cue;
    Play();
    if (IsPlaying()) return true;
    StopCue();
    return false;
}

void UFactoryFPVFlightCueAudioComponent::StopCue() {
    ActiveCue=EFactoryFPVFlightCue::None;
    Stop();
    if (GetSound()) SetSound(nullptr);
}

EFactoryFPVFlightCue UFactoryFPVFlightCueAudioComponent::GetActiveCue() const {
    return IsPlaying() ? ActiveCue : EFactoryFPVFlightCue::None;
}
