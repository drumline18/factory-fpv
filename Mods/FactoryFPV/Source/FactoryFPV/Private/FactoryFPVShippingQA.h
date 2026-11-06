#pragma once
#include "CoreMinimal.h"

class UWorld;
class FOutputDevice;

// Explicit disposable-save API qualification only. Never registered in normal
// gameplay; the implementation independently checks the opt-in and save paths.
class FFactoryFPVShippingQA {
public:
    static void Begin(UWorld* World,FOutputDevice& Output);
    static void Verify(UWorld* World,FOutputDevice& Output);
};
