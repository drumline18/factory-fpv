#pragma once
#include "CoreMinimal.h"
#include "FactoryFPVOsdLayout.generated.h"

enum class EFactoryFPVOsdItem : uint8 { Speed,Altitude,Home,Timer,Throttle,Battery,Integrity,Count };

UENUM()
enum class EFactoryFPVOsdSlot : uint8 {
    TopCenter, TopRight, UpperLeft, UpperRight, LowerLeft, LowerRight, BottomLeft, BottomRight, FooterRight, Count UMETA(Hidden)
};

USTRUCT()
struct FFactoryFPVOsdLayout {
    GENERATED_BODY()

    UPROPERTY() EFactoryFPVOsdSlot Speed=EFactoryFPVOsdSlot::BottomLeft;
    UPROPERTY() EFactoryFPVOsdSlot Altitude=EFactoryFPVOsdSlot::BottomRight;
    UPROPERTY() EFactoryFPVOsdSlot Home=EFactoryFPVOsdSlot::TopCenter;
    UPROPERTY() EFactoryFPVOsdSlot Timer=EFactoryFPVOsdSlot::TopRight;
    UPROPERTY() EFactoryFPVOsdSlot Throttle=EFactoryFPVOsdSlot::FooterRight;
    UPROPERTY() EFactoryFPVOsdSlot Battery=EFactoryFPVOsdSlot::LowerLeft;
    UPROPERTY() EFactoryFPVOsdSlot Integrity=EFactoryFPVOsdSlot::LowerRight;

    EFactoryFPVOsdSlot GetSlot(EFactoryFPVOsdItem Item) const;
    bool Move(EFactoryFPVOsdItem Item,EFactoryFPVOsdSlot Target);
    bool operator==(const FFactoryFPVOsdLayout& Other) const;
    void Normalize();

    static const TCHAR* ItemLabel(EFactoryFPVOsdItem Item);
    static const TCHAR* SlotLabel(EFactoryFPVOsdSlot Slot);
    static FVector2D Anchor(EFactoryFPVOsdSlot Slot);
    static FVector2D Alignment(EFactoryFPVOsdSlot Slot);
};
