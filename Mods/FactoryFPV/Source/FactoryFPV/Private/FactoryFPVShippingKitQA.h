#pragma once
#include "CoreMinimal.h"

class UWorld;
class FOutputDevice;

// Explicit disposable-save qualification of the real manual production and
// equipment APIs. Never grants a radio, unlocks a recipe or saves. The optional
// -FactoryFPVQAEquipmentFlight segment launches disarmed and observes native
// inventory-triggered unequip/return without injecting flight controls.
// -FactoryFPVQAProgression=kit requires the native HUB output and never adds ingredients.
class FFactoryFPVShippingKitQA {
public:
    static void Begin(UWorld* World,FOutputDevice& Output);
    // Starts alongside the caller's startup observer. Completion transfers one
    // genuinely paid/native-equipped kit, after owned UI/bench/camera cleanup.
    // Failure/cancellation restores this fixture's equipment ownership. Neither
    // path saves, exits the process or emits a second terminal QA result.
    static bool PrepareEquippedKit(UWorld* World,TFunction<void(bool,const FString&,const FString&)> Completion,FSimpleDelegate& OutCancel);
};
