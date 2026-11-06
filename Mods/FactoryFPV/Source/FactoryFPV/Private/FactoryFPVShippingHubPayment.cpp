#include "FactoryFPVShippingHubPayment.h"
#include "Engine/World.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "FGRemoteCallObject.h"
#include "FGSchematicManager.h"
#include "ItemAmount.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace FactoryFPVShippingHubPaymentInternal {
int32 AmountFor(const TArray<FItemAmount>& Amounts,TSubclassOf<UFGItemDescriptor> Item) {
    int32 Total=0;
    for (const auto& Entry:Amounts) if (Entry.ItemClass==Item) Total+=Entry.Amount;
    return Total;
}

UFunction* GetServerFunction(UFGRemoteCallObject& Remote,const TCHAR* Name,int32 ExpectedParameters) {
    UFunction* Function=Remote.FindFunction(Name);
    if (!Function || !Function->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer)) return nullptr;
    int32 Parameters=0;
    for (TFieldIterator<FProperty> It(Function);It;++It) if (It->HasAnyPropertyFlags(CPF_Parm)) ++Parameters;
    return Parameters==ExpectedParameters ? Function : nullptr;
}

template <class T>
T* FindInput(UFunction* Function,const TCHAR* Name) {
    T* Property=Function ? FindFProperty<T>(Function,Name) : nullptr;
    return Property && Property->HasAnyPropertyFlags(CPF_Parm) && !Property->HasAnyPropertyFlags(CPF_OutParm | CPF_ReturnParm) ? Property : nullptr;
}

struct FBalance {
    int32 Inventory=0;
    int32 Depot=0;
    int32 Paid=0;
};
}

bool FactoryFPVShippingHubPayment::Pay(AFGPlayerController& Controller,AFGCharacterPlayer& Pioneer,AFGSchematicManager& Manager,
    TSubclassOf<UFGSchematic> Schematic,TArray<FPayment>& Payments,FString& Error) {
    using namespace FactoryFPVShippingHubPaymentInternal;
    Payments.Reset();Error.Reset();
    auto* World=Controller.GetWorld();
    auto* Inventory=Pioneer.GetInventory();
    if (!IsInGameThread() || !World || World->GetNetMode()!=NM_Standalone || !Controller.HasAuthority() || !Manager.HasAuthority() ||
        Manager.GetWorld()!=World || Controller.GetPawn()!=&Pioneer || !IsValid(Inventory) || Inventory->GetOwner()!=&Pioneer ||
        !Schematic || Manager.GetActiveSchematic()!=Schematic || Manager.IsSchematicPurchased(Schematic,&Controller) || Inventory->GetNoBuildCost()) {
        Error=TEXT("Native HUB payoff requires an active, unpurchased schematic and the paid inventory of the local standalone pioneer.");
        return false;
    }
    auto* Depot=AFGCentralStorageSubsystem::Get(World);
    if (!IsValid(Depot) || !Depot->HasAuthority()) { Error=TEXT("Native Dimensional Depot balance is unavailable.");return false; }

    const auto Costs=UFGSchematic::GetCost(Schematic);
    const auto Remaining=Manager.GetRemainingCostFor(Schematic);
    const auto ReadBalance=[&](TSubclassOf<UFGItemDescriptor> Item) {
        return FBalance{Inventory->GetNumItems(Item),Depot->GetNumItemsFromCentralStorage(Item),AmountFor(Manager.GetPaidOffCostFor(Schematic),Item)};
    };
    if (Costs.IsEmpty() || Costs.Num()!=Remaining.Num()) { Error=TEXT("Native schematic does not have its complete original cost outstanding.");return false; }
    TArray<FBalance> Before;
    TArray<FInventoryStack> Withdrawals;
    for (const auto& Cost:Costs) {
        if (!Cost.ItemClass || Cost.Amount<=0 || Cost.Amount!=AmountFor(Remaining,Cost.ItemClass) ||
            Payments.ContainsByPredicate([&](const FPayment& Existing) { return Existing.Item==Cost.ItemClass; })) {
            Error=TEXT("Native schematic has invalid, duplicate or partly paid item costs.");return false;
        }
        const auto Balance=ReadBalance(Cost.ItemClass);
        if (Balance.Paid!=0 || static_cast<int64>(Balance.Inventory)+Balance.Depot<Cost.Amount) {
            Error=FString::Printf(TEXT("Existing resources cannot pay %s (inventory %d, Depot %d, paid %d, required %d)."),
                *GetNameSafe(Cost.ItemClass.Get()),Balance.Inventory,Balance.Depot,Balance.Paid,Cost.Amount);
            return false;
        }
        Before.Add(Balance);
        Payments.Add({Cost.ItemClass,Cost.Amount});
        const int32 Deficit=Cost.Amount-Balance.Inventory;
        if (Deficit>0) Withdrawals.Emplace(Deficit,Cost.ItemClass);
    }
    if (!Withdrawals.IsEmpty() && !Inventory->HasEnoughSpaceForStacks(Withdrawals)) {
        Error=TEXT("The pioneer has no space for the exact materials needed from the Depot.");return false;
    }

    auto* RemoteClass=LoadClass<UFGRemoteCallObject>(nullptr,TEXT("/Game/FactoryGame/Character/Player/BP_RemoteCallObject.BP_RemoteCallObject_C"));
    auto* Remote=RemoteClass ? Controller.GetRemoteCallObjectOfClass(RemoteClass) : nullptr;
    if (!IsValid(Remote) || Remote->GetOwnerPlayerController()!=&Controller || Remote->GetOwnerPlayerCharacter()!=&Pioneer) {
        Error=TEXT("The existing base-game Blueprint remote call object is not owned by this pioneer.");return false;
    }
    auto* Payoff=GetServerFunction(*Remote,TEXT("Server_PayOffOnSchematic"),3);
    auto* SchematicArg=FindInput<FClassProperty>(Payoff,TEXT("schematic"));
    auto* SourceArg=FindInput<FObjectProperty>(Payoff,TEXT("FromInventoryComponent"));
    auto* SlotArg=FindInput<FIntProperty>(Payoff,TEXT("idx"));
    if (!SchematicArg || SchematicArg->MetaClass!=UFGSchematic::StaticClass() || !SourceArg || !Inventory->IsA(SourceArg->PropertyClass) || !SlotArg) {
        Error=TEXT("Base-game Server_PayOffOnSchematic signature differs from the pinned schematic, inventory and slot inputs.");return false;
    }

    if (!Withdrawals.IsEmpty()) {
        auto* Transfer=GetServerFunction(*Remote,TEXT("Server_MoveItemFromCentralStorage"),2);
        auto* DestinationArg=FindInput<FObjectProperty>(Transfer,TEXT("destinationComponent"));
        auto* AmountArg=FindInput<FStructProperty>(Transfer,TEXT("ItemAmount"));
        if (!DestinationArg || !Inventory->IsA(DestinationArg->PropertyClass) || !AmountArg || AmountArg->Struct!=FItemAmount::StaticStruct()) {
            Error=TEXT("Base-game Server_MoveItemFromCentralStorage signature differs from the pinned destination and item amount inputs.");return false;
        }
        for (const auto& Stack:Withdrawals) {
            const auto Item=Stack.Item.GetItemClass();
            const auto Prior=ReadBalance(Item);
            FStructOnScope Arguments(Transfer);
            auto* Args=Arguments.GetStructMemory();
            DestinationArg->SetObjectPropertyValue_InContainer(Args,Inventory);
            *AmountArg->ContainerPtrToValuePtr<FItemAmount>(Args)=FItemAmount(Item,Stack.NumItems);
            Remote->ProcessEvent(Transfer,Args);
            const auto Current=ReadBalance(Item);
            if (Current.Inventory-Prior.Inventory!=Stack.NumItems || Prior.Depot-Current.Depot!=Stack.NumItems || Current.Paid!=Prior.Paid) {
                Error=FString::Printf(TEXT("Native Depot withdrawal of %s did not conserve %d items (inventory gain %d, Depot debit %d)."),
                    *GetNameSafe(Item.Get()),Stack.NumItems,Current.Inventory-Prior.Inventory,Prior.Depot-Current.Depot);
                return false;
            }
        }
    }

    for (int32 Index=0;Index<Payments.Num();++Index) {
        auto& Payment=Payments[Index];
        const auto Item=Payment.Item;
        int32 ToPay=Payment.Expected;
        for (int32 Slot=0;Slot<Inventory->GetSizeLinear() && ToPay>0;++Slot) {
            FInventoryStack Stack;
            if (!Inventory->GetStackFromIndex(Slot,Stack) || !Stack.HasItems() || Stack.Item.GetItemClass()!=Item) continue;
            const int32 Expected=FMath::Min(Stack.NumItems,ToPay);
            const auto Prior=ReadBalance(Item);
            if (Manager.GetActiveSchematic()!=Schematic || Manager.IsSchematicPurchased(Schematic,&Controller)) {
                Error=TEXT("The active schematic changed before native HUB payoff.");return false;
            }
            FStructOnScope Arguments(Payoff);
            auto* Args=Arguments.GetStructMemory();
            SchematicArg->SetObjectPropertyValue_InContainer(Args,Schematic.Get());
            SourceArg->SetObjectPropertyValue_InContainer(Args,Inventory);
            SlotArg->SetPropertyValue_InContainer(Args,Slot);
            Remote->ProcessEvent(Payoff,Args);
            const auto Current=ReadBalance(Item);
            if (Prior.Inventory-Current.Inventory!=Expected || Current.Paid-Prior.Paid!=Expected || Prior.Depot!=Current.Depot ||
                AmountFor(Manager.GetRemainingCostFor(Schematic),Item)!=ToPay-Expected) {
                Error=FString::Printf(TEXT("Native payoff for %s did not debit and credit %d (inventory debit %d, paid credit %d)."),
                    *GetNameSafe(Item.Get()),Expected,Prior.Inventory-Current.Inventory,Current.Paid-Prior.Paid);
                return false;
            }
            ToPay-=Expected;
        }
        const auto After=ReadBalance(Item);
        Payment.InventoryDelta=Before[Index].Inventory-After.Inventory;
        Payment.DepotDelta=Before[Index].Depot-After.Depot;
        Payment.PaidDelta=After.Paid-Before[Index].Paid;
        if (ToPay!=0 || Payment.InventoryDelta<0 || Payment.DepotDelta<0 ||
            Payment.InventoryDelta+Payment.DepotDelta!=Payment.Expected || Payment.PaidDelta!=Payment.Expected) {
            Error=FString::Printf(TEXT("Native payoff for %s did not consume the exact existing inventory and Depot resources."),*GetNameSafe(Item.Get()));
            return false;
        }
    }
    if (!Manager.IsSchematicPaidOff(Schematic) || Manager.IsSchematicPurchased(Schematic,&Controller)) {
        Error=TEXT("The native milestone is not fully paid and still awaiting launch.");return false;
    }
    return true;
}
