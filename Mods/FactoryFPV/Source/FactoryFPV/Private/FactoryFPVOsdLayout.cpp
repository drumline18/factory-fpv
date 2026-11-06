#include "FactoryFPVOsdLayout.h"

namespace FactoryFPVOsdLayoutInternal {
using Slot=EFactoryFPVOsdSlot;
using Item=EFactoryFPVOsdItem;
using SlotMember=Slot FFactoryFPVOsdLayout::*;

struct FItemDefinition {
    const TCHAR* Label;
    SlotMember Placement;
};
constexpr FItemDefinition Items[]{
    {TEXT("Speed"),&FFactoryFPVOsdLayout::Speed},
    {TEXT("Altitude"),&FFactoryFPVOsdLayout::Altitude},
    {TEXT("Distance home"),&FFactoryFPVOsdLayout::Home},
    {TEXT("Armed timer"),&FFactoryFPVOsdLayout::Timer},
    {TEXT("Throttle"),&FFactoryFPVOsdLayout::Throttle},
    {TEXT("Simulated battery"),&FFactoryFPVOsdLayout::Battery},
    {TEXT("Frame integrity"),&FFactoryFPVOsdLayout::Integrity},
};
struct FSlotDefinition {
    const TCHAR* Label;
    FVector2D Anchor;
    double HorizontalAlignment;
};
const FSlotDefinition Slots[]{
    {TEXT("Top center"),{960,60},.5},
    {TEXT("Top right"),{1824,60},1.},
    {TEXT("Upper left"),{96,156},0.},
    {TEXT("Upper right"),{1824,156},1.},
    {TEXT("Lower left"),{96,820},0.},
    {TEXT("Lower right"),{1824,820},1.},
    {TEXT("Bottom left"),{96,898},0.},
    {TEXT("Bottom right"),{1824,898},1.},
    {TEXT("Footer right"),{1824,976},1.},
};
static_assert(UE_ARRAY_COUNT(Items)==static_cast<uint8>(Item::Count));
static_assert(UE_ARRAY_COUNT(Slots)==static_cast<uint8>(Slot::Count));

const FSlotDefinition& SlotDefinition(Slot Value) {
    return Slots[Value<Slot::Count ? static_cast<uint8>(Value) : 0];
}
}

namespace OsdLayoutData=FactoryFPVOsdLayoutInternal;

EFactoryFPVOsdSlot FFactoryFPVOsdLayout::GetSlot(EFactoryFPVOsdItem Value) const {
    return Value<EFactoryFPVOsdItem::Count ? this->*OsdLayoutData::Items[static_cast<uint8>(Value)].Placement : EFactoryFPVOsdSlot::Count;
}

bool FFactoryFPVOsdLayout::Move(EFactoryFPVOsdItem Value,EFactoryFPVOsdSlot Target) {
    if (Value>=EFactoryFPVOsdItem::Count || Target>=EFactoryFPVOsdSlot::Count) return false;
    auto& Current=this->*OsdLayoutData::Items[static_cast<uint8>(Value)].Placement;
    if (Current==Target) return false;
    for (const auto& Definition:OsdLayoutData::Items) {
        auto& Other=this->*Definition.Placement;
        if (Other==Target) { Other=Current; break; }
    }
    Current=Target;
    return true;
}

bool FFactoryFPVOsdLayout::operator==(const FFactoryFPVOsdLayout& Other) const {
    for (const auto& Definition:OsdLayoutData::Items) {
        if (this->*Definition.Placement!=Other.*Definition.Placement) return false;
    }
    return true;
}

void FFactoryFPVOsdLayout::Normalize() {
    TSet<EFactoryFPVOsdSlot> Used;
    for (const auto& Definition:OsdLayoutData::Items) {
        const auto Position=this->*Definition.Placement;
        if (Position>=EFactoryFPVOsdSlot::Count || Used.Contains(Position)) {
            *this=FFactoryFPVOsdLayout{};
            return;
        }
        Used.Add(Position);
    }
}

const TCHAR* FFactoryFPVOsdLayout::ItemLabel(EFactoryFPVOsdItem Value) {
    return Value<EFactoryFPVOsdItem::Count ? OsdLayoutData::Items[static_cast<uint8>(Value)].Label : TEXT("");
}

const TCHAR* FFactoryFPVOsdLayout::SlotLabel(EFactoryFPVOsdSlot Value) { return OsdLayoutData::SlotDefinition(Value).Label; }
FVector2D FFactoryFPVOsdLayout::Anchor(EFactoryFPVOsdSlot Value) { return OsdLayoutData::SlotDefinition(Value).Anchor; }
FVector2D FFactoryFPVOsdLayout::Alignment(EFactoryFPVOsdSlot Value) { return {OsdLayoutData::SlotDefinition(Value).HorizontalAlignment,0.}; }
