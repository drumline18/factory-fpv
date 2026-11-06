#pragma once
#include "CoreMinimal.h"
#include "Resources/FGBuildingDescriptor.h"
#include "FGRecipe.h"
#include "FGSchematic.h"
#include "FactoryFPVRacingContent.generated.h"

class UWorld;

namespace FactoryFPVRacingContent {
// Add newly introduced recipes only for an already purchased racing milestone.
// False means the world/managers are not ready; no purchase or cost is replayed.
FACTORYFPV_API bool EnsureUnlockedRacingRecipes(UWorld* World);
}

UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateDescriptor : public UFGBuildingDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateDescriptor();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateRecipe();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateFullDescriptor : public UFGBuildingDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateFullDescriptor();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateFullRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateFullRecipe();
private:
    friend class FFactoryFPVRacingContentTest;
};

UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateVerticalDescriptor : public UFGBuildingDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateVerticalDescriptor();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateVerticalRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateVerticalRecipe();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateFloorDescriptor : public UFGBuildingDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateFloorDescriptor();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRaceGateFloorRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRaceGateFloorRecipe();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRacePoleDescriptor : public UFGBuildingDescriptor {
    GENERATED_BODY()
public:
    UFactoryFPVRacePoleDescriptor();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRacePoleRecipe : public UFGRecipe {
    GENERATED_BODY()
public:
    UFactoryFPVRacePoleRecipe();
private:
    friend class FFactoryFPVRacingContentTest;
};
UCLASS()
class FACTORYFPV_API UFactoryFPVRacingSchematic : public UFGSchematic {
    GENERATED_BODY()
public:
    UFactoryFPVRacingSchematic();
private:
    friend class FFactoryFPVRacingContentTest;
};
