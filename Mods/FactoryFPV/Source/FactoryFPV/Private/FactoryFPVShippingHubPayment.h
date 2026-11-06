#pragma once

#include "CoreMinimal.h"

class AFGCharacterPlayer;
class AFGPlayerController;
class AFGSchematicManager;
class UFGItemDescriptor;
class UFGSchematic;

namespace FactoryFPVShippingHubPayment {
struct FPayment {
    TSubclassOf<UFGItemDescriptor> Item;
    int32 Expected=0;
    int32 InventoryDelta=0;
    int32 DepotDelta=0;
    int32 PaidDelta=0;
};

// Called synchronously by the guarded disposable-world HUB fixture.
bool Pay(AFGPlayerController& Controller,AFGCharacterPlayer& Pioneer,AFGSchematicManager& Manager,
    TSubclassOf<UFGSchematic> Schematic,TArray<FPayment>& Payments,FString& Error);
}
