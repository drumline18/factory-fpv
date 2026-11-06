#pragma once

#include "CoreMinimal.h"

namespace FactoryFPVRaceScreen {
// Camera space uses X forward, Y right, Z up. The returned direction uses
// normalized viewport axes, with zero for a visible or unavailable target.
FVector2D OffscreenDirection(const FVector& Target,const FVector2D& ProjectionScale,double AspectRatio,double LensStrength);
}
