#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "FactoryFPVShippingQuitQA.generated.h"

class AActor;
class UWorld;
class FOutputDevice;

// Exists only during the explicitly guarded disposable Shipping qualification.
// Reads the real actor delegate; it never routes EndPlay or cleans a session.
UCLASS(Transient)
class UFactoryFPVShippingQuitObserver final : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION()
    void ActorEnded(AActor* Actor, EEndPlayReason::Type Reason);
};

class FFactoryFPVShippingQuitQA final
{
public:
    static void Begin(UWorld* World, FOutputDevice& Output);
};
