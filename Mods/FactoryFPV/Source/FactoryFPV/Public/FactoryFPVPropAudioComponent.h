#pragma once
#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "PropSoundCore.h"
#include "FactoryFPVPropAudioComponent.generated.h"

struct FFactoryFPVPropAudioState;

UCLASS()
class FACTORYFPV_API UFactoryFPVPropAudioComponent : public USynthComponent {
    GENERATED_BODY()
public:
    UFactoryFPVPropAudioComponent(const FObjectInitializer& ObjectInitializer);
    static double MaxRpmForAirframe(double FrameSizeCm,double ThrustToWeight);
    void SetMotorState(const factoryfpv::PropSoundParameters& Parameters);
    void StopImmediately();
    uint64 GetRenderedSampleCount() const;
protected:
    virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& Params) override;
private:
    friend class FFactoryFPVPropAudioHandoffTest;
    TSharedPtr<FFactoryFPVPropAudioState,ESPMode::ThreadSafe> State;
};
