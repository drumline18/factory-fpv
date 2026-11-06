#pragma once
#include "GameFramework/PlayerController.h"
#include "FactoryFPVInputLeaseTestController.generated.h"

// Private test adapter: Editor native hooks are disabled. Route the reset
// protocol around actual engine counter operations without changing counters
// directly or replacing the native Shipping hook qualification.
UCLASS(NotPlaceable,Transient)
class AFactoryFPVInputLeaseTestController : public APlayerController {
    GENERATED_BODY()
public:
    AFactoryFPVInputLeaseTestController(const FObjectInitializer& ObjectInitializer=FObjectInitializer::Get());
    virtual void ResetIgnoreMoveInput() override;
    virtual void ResetIgnoreLookInput() override;
    virtual void ResetIgnoreInputFlags() override;
};
