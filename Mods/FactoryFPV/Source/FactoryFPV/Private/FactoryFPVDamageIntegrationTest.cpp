#include "FactoryFPVDrone.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVSettings.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVDamageIntegrationTest,
    "FactoryFPV.Collision.OptionalDamageAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVDamageIntegrationTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,
        ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Damage integration test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };

    // A broad vertical wall presents a single unambiguous -X normal. Its near
    // face is X=0; the real drone's 12 cm sphere begins 5 mm clear of that face.
    auto* Wall=World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Wall actor"),Wall)) return false;
    auto* WallBody=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(WallBody);
    Wall->AddInstanceComponent(WallBody);
    WallBody->SetBoxExtent(FVector(25,2000,2000));
    WallBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WallBody->SetCollisionObjectType(ECC_WorldStatic);
    WallBody->SetCollisionResponseToAllChannels(ECR_Block);
    WallBody->RegisterComponent();
    Wall->SetActorLocation(FVector(25,0,1000));

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Drone=World->SpawnActor<AFactoryFPVDrone>(FVector(-500,0,1000),
        FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Damage test drone"),Drone)) return false;
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->FrameSizeCm=24;
    Settings->MassKg=.65f;
    Settings->ThrustToWeight=4.08f;
    Settings->bSimulateBattery=false;
    Settings->bCrashDamageEnabled=false;
    Settings->CrashToleranceJoules=30;
    Drone->ApplyFlightSettings(*Settings);
    Drone->LaunchPosition=FVector(-500,0,1000);

    const double Step=factoryfpv::StepSeconds;
    const factoryfpv::Input Idle{0,0,0,0,true};
    auto Place=[Drone](FVector Position,factoryfpv::Vec Velocity) {
        Drone->SetActorLocationAndRotation(Position,FRotator::ZeroRotator);
        Drone->Flight.reset({Position.X/100.,Position.Y/100.,Position.Z/100.});
        Drone->Flight.velocity=Velocity;
    };
    auto ApproachWall=[&Place] (factoryfpv::Vec Velocity) {
        Place(FVector(-12.5,0,1000),Velocity);
    };
    auto NominalThrust=[Drone] {
        return Drone->Flight.parameters.mass*Drone->Flight.parameters.gravity*Drone->NominalThrustToWeight;
    };
    auto HasHitWall=[this,Drone] {
        TestTrue(TEXT("Actual sphere sweep stops at the wall"),Drone->GetActorLocation().X>-12.6 &&
            Drone->GetActorLocation().X<=-11.5);
    };

    // Optional damage must preserve the shipped hard-contact disarm behaviour.
    ApproachWall({12,0,0});
    TestTrue(TEXT("Disabled damage permits low-throttle arming"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight(Idle,Step);
    HasHitWall();
    TestFalse(TEXT("Disabled damage still disarms on a hard wall collision"),Drone->Flight.armed);
    TestFalse(TEXT("Damage is explicitly opt in"),Drone->Damage.enabled());
    TestEqual(TEXT("Disabled hard contact preserves full integrity"),Drone->Damage.integrityPercent(),100.);
    TestTrue(TEXT("Disabled hard contact does not reduce configured thrust"),
        FMath::IsNearlyEqual(Drone->Flight.parameters.maxThrust,NominalThrust(),1.e-8));

    Settings->bCrashDamageEnabled=true;
    Drone->ApplyFlightSettings(*Settings);
    TestTrue(TEXT("Flight settings enable crash consequences"),Drone->Damage.enabled());
    TestEqual(TEXT("Structural tolerance reaches crash model"),
        Drone->Damage.parameters.destructionEnergyJoules,30.);

    // This glancing contact has high total speed but a harmless 2 m/s inward
    // component. Testing a real sweep catches accidentally using total speed.
    ApproachWall({2,12,0});
    TestTrue(TEXT("Fresh frame can arm before glancing contact"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight(Idle,Step);
    HasHitWall();
    TestTrue(TEXT("Gentle normal component preserves the armed state"),Drone->Flight.armed);
    TestEqual(TEXT("Fast tangential motion does not damage the frame"),Drone->Damage.integrityPercent(),100.);
    TestTrue(TEXT("Collision removes only inward motion"),
        FMath::Abs(Drone->Flight.velocity.x)<1.e-6 && Drone->Flight.velocity.y>11.9);

    Drone->ApplyFlightSettings(*Settings);
    ApproachWall({6,0,0});
    TestTrue(TEXT("Fresh frame arms before damaging contact"),Drone->TryArmFlight(Idle));
    // Predict the unobstructed physics substep independently of Unreal contact.
    // The hard-contact policy then clears velocity, so matching this energy
    // verifies damage was measured before Flight.contact discarded that speed.
    auto Incoming=Drone->Flight;
    Incoming.step(Idle,Step);
    const double ExpectedLoss=.5*Incoming.parameters.mass*(Incoming.velocity.x*Incoming.velocity.x-9.)/30.*100.;
    Drone->SimulateFlight(Idle,Step);
    HasHitWall();
    TestFalse(TEXT("Damaging hard contact disarms actual flight core"),Drone->Flight.armed);
    TestEqual(TEXT("Hard-contact response clears pre-impact velocity"),Drone->Flight.velocity.length(),0.);
    TestTrue(TEXT("Crash uses the actual incoming normal speed before it is cleared"),
        FMath::IsNearlyEqual(Drone->Damage.integrityPercent(),100.-ExpectedLoss,1.e-6));
    TestTrue(TEXT("Moderate wall collision leaves a degraded recoverable drone"),
        Drone->Damage.integrityPercent()>0 && Drone->Damage.integrityPercent()<100 && !Drone->Damage.destroyed());

    // Move to open space without repairing the frame, then exercise powered
    // flight. Thrust must be derived from the nominal setting each substep;
    // repeatedly multiplying an already degraded value would decay every tick.
    const double DamagedIntegrity=Drone->Damage.integrityPercent();
    const double ExpectedThrust=NominalThrust()*Drone->Damage.powerFraction();
    Place(FVector(-500,0,1000),{});
    TestTrue(TEXT("Partially damaged drone can rearm"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight({0,0,0,.5,true},Step);
    TestTrue(TEXT("Damaged flight has lower available thrust"),
        ExpectedThrust<NominalThrust() && FMath::IsNearlyEqual(Drone->Flight.parameters.maxThrust,ExpectedThrust,1.e-8));
    for (int Index=0;Index<47;++Index) Drone->SimulateFlight({0,0,0,.5,true},Step);
    TestEqual(TEXT("Open-space flight does not invent extra collision damage"),
        Drone->Damage.integrityPercent(),DamagedIntegrity);
    TestTrue(TEXT("Available thrust does not compound down across physics substeps"),
        FMath::IsNearlyEqual(Drone->Flight.parameters.maxThrust,ExpectedThrust,1.e-8));
    TestTrue(TEXT("A damaged armed frame still produces thrust"),Drone->Flight.thrust>0 && Drone->Flight.armed);

    // More than the contact grouping window elapsed through SimulateFlight.
    // A genuinely separate second approach must count as another collision.
    ApproachWall({6,0,0});
    TestTrue(TEXT("Damaged frame arms for a separate later approach"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight(Idle,Step);
    TestTrue(TEXT("Actual simulation advances contact-window time"),
        FMath::IsNearlyEqual(Drone->Damage.integrityPercent(),100.-2.*ExpectedLoss,1.e-6));

    Drone->ApplyFlightSettings(*Settings);
    ApproachWall({12,0,0});
    TestTrue(TEXT("Intact drone can arm before terminal collision"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight(Idle,Step);
    HasHitWall();
    TestTrue(TEXT("Severe sweep impact destroys the simulated frame"),Drone->Damage.destroyed());
    TestEqual(TEXT("Destroyed frame has zero integrity"),Drone->Damage.integrityPercent(),0.);
    TestEqual(TEXT("Destroyed frame has zero usable power"),Drone->Damage.powerFraction(),0.);
    TestFalse(TEXT("Terminal impact leaves motors disarmed"),Drone->Flight.armed);
    TestFalse(TEXT("Arming entry rejects a destroyed frame at low throttle"),Drone->TryArmFlight(Idle));

    Drone->LaunchPosition=FVector(25,0,1000); // Deliberately obstruct the recovery point.
    Drone->ResetDrone();
    TestTrue(TEXT("Blocked recovery cannot silently repair a destroyed frame"),Drone->Damage.destroyed());
    TestFalse(TEXT("Blocked recovery does not bypass destruction arm lockout"),Drone->TryArmFlight(Idle));
    TestEqual(TEXT("Blocked recovery gives a distinct obstruction warning"),Drone->RecoveryWarning,
        FString(TEXT("Launch point obstructed. F10 to return and relaunch.")));
    Drone->LaunchPosition=FVector(-500,0,1000);
    const FVector ImpactPosition=Drone->GetActorLocation();
    Drone->ResetDrone();
    TestTrue(TEXT("Unsupported recovery cannot repair a destroyed frame"),Drone->Damage.destroyed());
    TestEqual(TEXT("Unsupported recovery does not move the destroyed frame"),Drone->GetActorLocation(),ImpactPosition);

    auto* Floor=World->SpawnActor<AActor>();
    auto* FloorBody=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBody);
    Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(100,100,50));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);
    FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBody->RegisterComponent();
    Floor->SetActorLocation(Drone->LaunchPosition-FVector(0,0,64)); // 12 cm sphere plus 2 cm clearance.
    Drone->ResetDrone();
    TestFalse(TEXT("Successful recovery clears destruction"),Drone->Damage.destroyed());
    TestEqual(TEXT("Successful recovery restores integrity"),Drone->Damage.integrityPercent(),100.);
    TestFalse(TEXT("Successful recovery never automatically arms"),Drone->Flight.armed);
    TestTrue(TEXT("Repaired frame can arm with low throttle"),Drone->TryArmFlight(Idle));
    Drone->SimulateFlight(Idle,Step);
    TestTrue(TEXT("Recovery restores nominal available thrust"),
        FMath::IsNearlyEqual(Drone->Flight.parameters.maxThrust,NominalThrust(),1.e-8));
    return true;
}
#endif
