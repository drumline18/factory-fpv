#pragma once
#include "CoreMinimal.h"

class UWorld;
class FOutputDevice;

// Opt-in qualification against an actual disposable Shipping world/viewport.
// Uses ordinary launch/return and camera-local lens APIs; injects no controls.
class FFactoryFPVShippingCameraQA {
public:
    static void Begin(UWorld* World,FOutputDevice& Output);
};
