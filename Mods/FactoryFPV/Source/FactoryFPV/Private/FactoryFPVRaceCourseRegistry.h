#pragma once

#include "FGSaveInterface.h"
#include "Subsystem/ModSubsystem.h"
#include "FactoryFPVRaceCourseRegistry.generated.h"

UCLASS()
class AFactoryFPVRaceCourseRegistry final : public AModSubsystem, public IFGSaveInterface {
    GENERATED_BODY()
public:
    static constexpr int32 MaximumNameLength=36;

    AFactoryFPVRaceCourseRegistry();
    static AFactoryFPVRaceCourseRegistry* Get(UWorld* World);
    const TMap<int32,FString>& GetNames() const { return CourseNames; }
    FString GetNameForCourse(int32 CourseId) const { return CourseNames.FindRef(CourseId); }
    bool SetCourseName(int32 CourseId,FString Name,FString& Error);

    virtual void PostLoadGame_Implementation(int32 SaveVersion,int32 GameVersion) override;
    virtual bool ShouldSave_Implementation() const override { return !CourseNames.IsEmpty(); }
    virtual bool NeedTransform_Implementation() override { return false; }

private:
    // Names belong to course numbers. Checkpoint geometry and PB keys do not use this map.
    UPROPERTY(SaveGame) TMap<int32,FString> CourseNames;
};
