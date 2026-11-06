#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

enum class EFactoryFPVRaceGateDirection : uint8;

class SFactoryFPVRaceRoutePreview final : public SLeafWidget {
public:
    struct FCheckpoint {
        int32 Order=0;
        FVector Position=FVector::ZeroVector;
        bool bPowered=false;
        bool bSelected=false;
        EFactoryFPVRaceGateDirection Direction{};

        bool operator==(const FCheckpoint& Other) const {
            return Order==Other.Order && Position==Other.Position && bPowered==Other.bPowered &&
                bSelected==Other.bSelected && Direction==Other.Direction;
        }
    };

    SLATE_BEGIN_ARGS(SFactoryFPVRaceRoutePreview) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&) {}
    void SetRoute(TArray<FCheckpoint> InCheckpoints);
    void SetElevation(bool bEnabled);
    bool IsElevation() const { return bElevation; }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(0,165); }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override;

private:
    friend class FFactoryFPVRacePanelTest;
    TArray<FCheckpoint> Checkpoints;
    bool bElevation=false;
};
