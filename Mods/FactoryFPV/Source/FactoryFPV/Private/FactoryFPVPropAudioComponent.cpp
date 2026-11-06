#include "FactoryFPVPropAudioComponent.h"
#include "Containers/TripleBuffer.h"
#include "Misc/ScopeLock.h"
#include <atomic>

struct FFactoryFPVPropVoiceState {
    explicit FFactoryFPVPropVoiceState(const factoryfpv::PropSoundParameters& Initial) : MotorState(Initial) {}

    TTripleBuffer<factoryfpv::PropSoundParameters> MotorState;
    std::atomic<bool> bRetired{false};
};

struct FFactoryFPVPropAudioState {
    // Generator creation can run on the audio thread while the game thread
    // publishes a new snapshot. Render callbacks only touch their own voice.
    FCriticalSection VoiceLock;
    factoryfpv::PropSoundParameters LatestMotorState;
    TSharedPtr<FFactoryFPVPropVoiceState,ESPMode::ThreadSafe> Voice;
    std::atomic<uint64> RenderedSamples{0};

    void RetireVoice() {
        if (Voice) Voice->bRetired.store(true,std::memory_order_release);
        Voice.Reset();
    }
};

class FFactoryFPVPropGenerator final : public ISoundGenerator {
    TSharedRef<FFactoryFPVPropAudioState,ESPMode::ThreadSafe> State;
    TSharedRef<FFactoryFPVPropVoiceState,ESPMode::ThreadSafe> Voice;
    factoryfpv::PropSoundCore Sound;
public:
    FFactoryFPVPropGenerator(TSharedRef<FFactoryFPVPropAudioState,ESPMode::ThreadSafe> InState,
        TSharedRef<FFactoryFPVPropVoiceState,ESPMode::ThreadSafe> InVoice,float Rate)
        : State(InState), Voice(InVoice), Sound(Rate) {}
    virtual int32 OnGenerateAudio(float* Output,int32 Count) override {
        if (Voice->bRetired.load(std::memory_order_acquire)) {
            FMemory::Memzero(Output,sizeof(float)*Count);
        } else {
            Sound.set(Voice->MotorState.SwapAndRead());
            Sound.render(Output,Count);
        }
        State->RenderedSamples.fetch_add(static_cast<uint64>(Count),std::memory_order_relaxed);
        return Count;
    }
};

UFactoryFPVPropAudioComponent::UFactoryFPVPropAudioComponent(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer) {
    bAutoActivate=false;
    bAllowSpatialization=false; // Local FPV microphone perspective, not the distant pioneer.
    bIsUISound=false;
    NumChannels=1;
    State=MakeShared<FFactoryFPVPropAudioState,ESPMode::ThreadSafe>();
}
double UFactoryFPVPropAudioComponent::MaxRpmForAirframe(double FrameSizeCm,double ThrustToWeight) {
    return 24000.*FMath::Sqrt(24./FrameSizeCm)*FMath::Sqrt(ThrustToWeight/4.08);
}
void UFactoryFPVPropAudioComponent::SetMotorState(const factoryfpv::PropSoundParameters& Parameters) {
    if (!(Parameters.volume>0.)) {
        StopImmediately();
        return;
    }
    FScopeLock Lock(&State->VoiceLock);
    State->LatestMotorState=Parameters;
    if (State->Voice) State->Voice->MotorState.WriteAndSwap(Parameters);
}
void UFactoryFPVPropAudioComponent::StopImmediately() {
    {
        FScopeLock Lock(&State->VoiceLock);
        State->LatestMotorState={};
        State->RetireVoice();
    }
    // The mixer can retain queued PCM while paused. Retire the source as well
    // as its generator so resuming the game cannot play the old motor state.
    Stop();
}
uint64 UFactoryFPVPropAudioComponent::GetRenderedSampleCount() const { return State->RenderedSamples.load(std::memory_order_relaxed); }
ISoundGeneratorPtr UFactoryFPVPropAudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& Params) {
    FScopeLock Lock(&State->VoiceLock);
    // A previous source can finish its queued render task after a restart.
    // Each source needs its own single-consumer buffer during that overlap.
    State->RetireVoice();
    auto Voice=MakeShared<FFactoryFPVPropVoiceState,ESPMode::ThreadSafe>(State->LatestMotorState);
    State->Voice=Voice;
    return MakeShared<FFactoryFPVPropGenerator,ESPMode::ThreadSafe>(State.ToSharedRef(),Voice,Params.SampleRate);
}
