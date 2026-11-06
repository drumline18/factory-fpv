#include "FactoryFPVRaceScreen.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRaceScreenTest,"FactoryFPV.Racing.OffscreenProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRaceScreenTest::RunTest(const FString&) {
    const FVector2D Scale(1.,16./9.);
    using FactoryFPVRaceScreen::OffscreenDirection;
    TestTrue(TEXT("A centered target leaves the world gate to guide the pilot"),
        OffscreenDirection(FVector(100,0,0),Scale,Scale.Y,0.).IsZero());
    TestTrue(TEXT("A climb target beyond the vertical field points up"),
        OffscreenDirection(FVector(100,0,70),Scale,Scale.Y,0.).Equals(FVector2D(0,-1)));
    TestTrue(TEXT("A descent target beyond the vertical field points down"),
        OffscreenDirection(FVector(100,0,-70),Scale,Scale.Y,0.).Equals(FVector2D(0,1)));
    TestTrue(TEXT("A wider field contains the same checkpoint"),
        OffscreenDirection(FVector(100,0,70),Scale*.7,Scale.Y,0.).IsZero());
    TestTrue(TEXT("A target in the rear left stays left"),
        OffscreenDirection(FVector(-100,-50,0),Scale,Scale.Y,0.).Equals(FVector2D(-1,0)));
    TestTrue(TEXT("Near the rear axis the direction stays at the bottom"),
        OffscreenDirection(FVector(-100,.25,0),Scale,Scale.Y,0.).Equals(FVector2D(0,1)));
    TestTrue(TEXT("Without lens warp the near-edge checkpoint is visible"),
        OffscreenDirection(FVector(100,0,47),Scale,Scale.Y,0.).IsZero());
    TestTrue(TEXT("Lens crop moves that checkpoint outside the visible frame"),
        OffscreenDirection(FVector(100,0,47),Scale,Scale.Y,1.).Equals(FVector2D(0,-1)));
    TestTrue(TEXT("The lens preserves the corners of the source frame"),
        OffscreenDirection(FVector(100,97,97/Scale.Y),Scale,Scale.Y,1.).IsZero());
    TestTrue(TEXT("An uninitialized viewport or coincident checkpoint has no cue"),
        OffscreenDirection(FVector(100,0,70),Scale,0.,0.).IsZero() &&
        OffscreenDirection(FVector::ZeroVector,Scale,Scale.Y,0.).IsZero());
    return true;
}
#endif
