#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FactoryFPVShippingSaveQA.generated.h"
class UWorld;
class FOutputDevice;

UCLASS(Transient)
class UFactoryFPVShippingAutosaveObserver : public UObject
{
    GENERATED_BODY()
public:
    TFunction<void(float)> Countdown;
    TFunction<void()> Queued;
    UFUNCTION() void OnCountdown(float Seconds);
    UFUNCTION() void OnQueuedNotification();
};

// Explicit disposable-world qualification only. Never registered without the
// caller's FactoryFPVAutomation opt-in; Begin repeats all isolation guards.
// Kit persistence delegates actual paid Workshop preparation in save mode only;
// reload performs no production, item grant, active-slot setter or re-equip.
class FFactoryFPVShippingSaveQA final
{
public:
    static void Begin(UWorld* World,FOutputDevice& Output);
};
