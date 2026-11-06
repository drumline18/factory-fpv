#include "FactoryFPVDrone.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVGroundTakeoffTest,
    "FactoryFPV.Collision.GroundTakeoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVGroundTakeoffTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &Values);
    if (!TestNotNull(TEXT("Collision test world"), World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };

    AActor* Floor = World->SpawnActor<AActor>();
    UBoxComponent* FloorBody = NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBody);
    Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(1000, 1000, 50));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);
    FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBody->RegisterComponent();
    Floor->SetActorLocation(FVector(0, 0, -50));

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFactoryFPVDrone* Drone = World->SpawnActor<AFactoryFPVDrone>(FVector(0, 0, 200),
        FRotator::ZeroRotator, Spawn);
    if (!TestNotNull(TEXT("Test drone"), Drone)) return false;

    auto Place = [Drone](double Height) {
        Drone->SetActorLocationAndRotation(FVector(0, 0, Height), FRotator::ZeroRotator);
        Drone->Flight.reset({0, 0, Height / 100.});
    };
    auto Advance = [Drone](factoryfpv::Input Input, int Steps) {
        for (int Step = 0; Step < Steps; ++Step) {
            Drone->SimulateFlight(Input, factoryfpv::StepSeconds);
        }
    };

    Place(200);
    Advance({}, 720);
    TestTrue(TEXT("Disarmed drop settles above the floor"), Drone->GetActorLocation().Z >= 11.5 &&
        Drone->GetActorLocation().Z < 15.);
    TestTrue(TEXT("Can arm after waiting on the ground"), Drone->Flight.arm({}));
    Advance({0, 0, 0, 0.5, true}, 240);
    TestTrue(TEXT("Ground contact preserves arming and motor spool"), Drone->Flight.armed);
    TestTrue(TEXT("Can take off after settling"), Drone->GetActorLocation().Z > 60.);

    Place(10); // Sphere radius is 12 cm, so this deliberately starts inside the floor.
    TestTrue(TEXT("Can arm before depenetration"), Drone->Flight.arm({}));
    Advance({0, 0, 0, 0.5, true}, 240);
    TestTrue(TEXT("Initial shallow floor overlap recovers without a disarm loop"),
        Drone->Flight.armed && Drone->GetActorLocation().Z > 60.);

    Place(200);
    Drone->Flight.arm({});
    Advance({}, 240);
    TestFalse(TEXT("A hard airborne impact still disarms"), Drone->Flight.armed);

    Drone->LaunchPosition = FVector(0, 0, 14);
    Drone->LaunchYaw = factoryfpv::Pi / 4.;
    Place(200);
    TestTrue(TEXT("Drone is armed before attempting recovery"), Drone->Flight.arm({}));
    Advance({0, 0, 0, 0.5, true}, 30);
    const FVector FlightPosition = Drone->GetActorLocation();
    const FRotator FlightRotation = Drone->GetActorRotation();

    FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Drone->ResetDrone();
    TestEqual(TEXT("Removed launch support leaves the drone at its actual flight position"),
        Drone->GetActorLocation(), FlightPosition);
    TestEqual(TEXT("Failed recovery preserves orientation"), Drone->GetActorRotation(), FlightRotation);
    TestFalse(TEXT("Failed recovery still disarms"), Drone->Flight.armed);
    TestEqual(TEXT("Failed recovery cuts thrust"), Drone->Flight.thrust, 0.);
    TestEqual(TEXT("Failed recovery explains that the launch surface is unavailable"), Drone->RecoveryWarning,
        FString(TEXT("Launch surface unavailable. F10 to return and relaunch.")));

    Floor->SetActorLocation(FVector(0, 0, -100));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Drone->ResetDrone();
    TestEqual(TEXT("A floor too far below the original launch point does not count as support"),
        Drone->GetActorLocation(), FlightPosition);

    Floor->SetActorLocation(FVector(0, 0, -50));
    Drone->ResetDrone();
    TestEqual(TEXT("Restored floor permits recovery to the original launch position"),
        Drone->GetActorLocation(), Drone->LaunchPosition);
    TestTrue(TEXT("Supported recovery restores the original heading"),
        FMath::IsNearlyEqual(Drone->GetActorRotation().Yaw, 45.));
    TestEqual(TEXT("Supported recovery clears flight momentum"), Drone->Flight.velocity.length(), 0.);
    TestFalse(TEXT("Supported recovery leaves the drone disarmed"), Drone->Flight.armed);
    TestTrue(TEXT("A successful retry clears the recovery warning"), Drone->RecoveryWarning.IsEmpty());
    return true;
}
#endif
