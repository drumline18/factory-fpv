#include "FactoryFPVInputLeaseTestController.h"
#include "FactoryFPVInputLeaseInternal.h"

AFactoryFPVInputLeaseTestController::AFactoryFPVInputLeaseTestController(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer) {}

void AFactoryFPVInputLeaseTestController::ResetIgnoreMoveInput() {
    FFactoryFPVInputResetScope Scope(this,EFactoryFPVInputResetAxes::Move);
    Super::ResetIgnoreMoveInput();
}
void AFactoryFPVInputLeaseTestController::ResetIgnoreLookInput() {
    FFactoryFPVInputResetScope Scope(this,EFactoryFPVInputResetAxes::Look);
    Super::ResetIgnoreLookInput();
}
void AFactoryFPVInputLeaseTestController::ResetIgnoreInputFlags() {
    FFactoryFPVInputResetScope Scope(this,EFactoryFPVInputResetAxes::Both);
    Super::ResetIgnoreInputFlags();
}
