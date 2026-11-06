#pragma once
#include "FGConstructDisqualifier.h"
#include "Hologram/FGGenericBuildableHologram.h"
#include "FactoryFPVRaceGateHologram.generated.h"

class UFactoryFPVLocalPlayerSubsystem;

UCLASS()
class UFactoryFPVRaceCourseFull : public UFGConstructDisqualifier {
    GENERATED_BODY()
public:
    UFactoryFPVRaceCourseFull();
};

UCLASS()
class AFactoryFPVRaceGateHologram : public AFGGenericBuildableHologram {
    GENERATED_BODY()
protected:
    virtual void CheckValidPlacement() override;
    virtual void ConfigureActor(AFGBuildable* Buildable) const override;
private:
    const UFactoryFPVLocalPlayerSubsystem* LocalCourseBuilder() const;
};

UCLASS()
class AFactoryFPVRaceGateFloorHologram : public AFactoryFPVRaceGateHologram {
    GENERATED_BODY()
public:
    AFactoryFPVRaceGateFloorHologram();
    virtual bool TrySnapToActor(const FHitResult& Hit) override;
protected:
    virtual void CheckClearance() override;
private:
    TOptional<FTransform> FloorSnap;
};
