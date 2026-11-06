#pragma once
#include "CoreMinimal.h"
#include "Components/AudioComponent.h"
#include "FactoryFPVFlightCueAudioComponent.generated.h"

class USoundWave;

// Order is also playback priority, from lowest to highest.
enum class EFactoryFPVFlightCue : uint8 { None, Checkpoint, Lap, PersonalBest, ReceiverLost };

UCLASS()
class FACTORYFPV_API UFactoryFPVFlightCueAudioComponent : public UAudioComponent {
    GENERATED_BODY()
public:
    UFactoryFPVFlightCueAudioComponent(const FObjectInitializer& ObjectInitializer);
    bool PlayCue(EFactoryFPVFlightCue Cue,double Gain);
    void StopCue();
    EFactoryFPVFlightCue GetActiveCue() const;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<USoundWave>> CueSounds;
    EFactoryFPVFlightCue ActiveCue=EFactoryFPVFlightCue::None;
};
