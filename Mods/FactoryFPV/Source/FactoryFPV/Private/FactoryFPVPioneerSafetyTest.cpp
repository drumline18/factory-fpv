#include "FactoryFPVPioneerAnchor.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVDrone.h"
#include "GameFramework/Character.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVPioneerSafetyTest,
    "FactoryFPV.Session.PioneerFloorRetention",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVPioneerSafetyTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Pioneer safety world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Floor=World->SpawnActor<AActor>();
    auto* FloorBody=NewObject<UBoxComponent>(Floor);
    Floor->SetRootComponent(FloorBody);
    Floor->AddInstanceComponent(FloorBody);
    FloorBody->SetBoxExtent(FVector(10000,10000,50));
    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    FloorBody->SetCollisionObjectType(ECC_WorldStatic);
    FloorBody->SetCollisionResponseToAllChannels(ECR_Block);
    FloorBody->RegisterComponent();
    Floor->SetActorLocation(FVector(0,0,-50));

    auto* Character=World->SpawnActor<ACharacter>(FVector(0,0,89),FRotator::ZeroRotator);
    auto* Movement=Character->GetCharacterMovement();
    Movement->bRunPhysicsWithNoController=true;
    Movement->SetMovementMode(MOVE_Walking);
    auto* Anchor=World->SpawnActor<AFactoryFPVPioneerAnchor>();
    FString Error;
    if (!TestTrue(TEXT("Can anchor a grounded character"),Anchor->Hold(Character,Error))) {
        AddError(Error);
        return false;
    }
    TestTrue(TEXT("Movement is held without disabling character collision"),Anchor->IsHolding());
    FWorldPartitionStreamingSource Source;
    TestTrue(TEXT("Independent pioneer streaming source is enabled"),Anchor->Streaming->GetStreamingSource(Source));
    auto* Drone=World->SpawnActor<AFactoryFPVDrone>();
    Drone->SetActorLocation(FVector(1000000,0,10000));
    Anchor->Streaming->GetStreamingSource(Source);
    TestTrue(TEXT("Pioneer source stays at the body when drone travels ten kilometres"),
        Source.Location.Equals(Character->GetActorLocation(),.1) && !Source.Location.Equals(Drone->GetActorLocation(),.1));

    const FVector HeldPosition=Character->GetActorLocation();
    FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int Step=0;Step<120;++Step) Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
    TestTrue(TEXT("Pioneer does not fall even when ground collision disappears"),Character->GetActorLocation().Equals(HeldPosition,.1));
    Anchor->RequestRelease();
    TestFalse(TEXT("Return waits rather than destroying source with no ground"),Anchor->IsActorBeingDestroyed());
    TestTrue(TEXT("Return keeps the pioneer held while ground is missing"),Movement->MovementMode==MOVE_None);
    TestTrue(TEXT("Return keeps streaming requested until collision is ready"),Anchor->Streaming->IsStreamingSourceEnabled());
    TestTrue(TEXT("Pending return can be found for its exact pioneer"),AFactoryFPVPioneerAnchor::FindPending(World,Character)==Anchor);
    auto* UnrelatedPioneer=World->SpawnActor<ACharacter>(FVector(1000,0,89),FRotator::ZeroRotator);
    TestNull(TEXT("Another pioneer cannot access this return hold"),AFactoryFPVPioneerAnchor::FindPending(World,UnrelatedPioneer));
    TestFalse(TEXT("Recovery refuses a world with no safe floor"),Anchor->RecoverToNearbyFloor(Error));
    TestTrue(TEXT("Failed recovery explains that the pioneer remains held"),Error.Contains(TEXT("Movement stays held")));
    TestTrue(TEXT("Failed recovery never drops or relocates the pioneer"),Movement->MovementMode==MOVE_None && Character->GetActorLocation().Equals(HeldPosition,.1));

    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Anchor->Tick(1.f/60.f);
    TestTrue(TEXT("Restores walking after ground returns"),Movement->MovementMode==MOVE_Walking);
    TestTrue(TEXT("Completed return removes temporary anchor"),Anchor->IsActorBeingDestroyed());
    TestNull(TEXT("Completed return is not exposed as pending recovery"),AFactoryFPVPioneerAnchor::FindPending(World,Character));

    auto MakeBox=[&](FVector Position,FVector Extent,FRotator Rotation=FRotator(0,0,0)) {
        auto* Actor=World->SpawnActor<AActor>();
        auto* Box=NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
        Box->SetBoxExtent(Extent);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Actor->SetActorLocationAndRotation(Position,Rotation);
        return Box;
    };
    auto BeginPending=[&]() {
        FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Character->SetActorLocation(HeldPosition,false,nullptr,ETeleportType::TeleportPhysics);
        Movement->SetMovementMode(MOVE_Walking);
        Movement->bForceNextFloorCheck=true;
        auto* Waiting=World->SpawnActor<AFactoryFPVPioneerAnchor>();
        TestTrue(TEXT("Recovery fixture starts with a genuinely grounded pioneer"),Waiting->Hold(Character,Error));
        FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Waiting->RequestRelease();
        TestTrue(TEXT("Recovery fixture waits without walkable support"),Waiting->IsReleasePending());
        return Waiting;
    };
    // A small isolated platform near the body is a real alternate support, not
    // a restored original floor or a forced movement-mode-only success.
    auto* Recovery=BeginPending();
    auto* Platform=MakeBox(FVector(300,0,-25),FVector(50,50,25));
    auto* FartherPlatform=MakeBox(FVector(500,0,-25),FVector(50,50,25));
    TestTrue(TEXT("Explicit recovery finds a nearby isolated loaded platform"),Recovery->RecoverToNearbyFloor(Error));
    TestTrue(TEXT("Recovery prefers the nearest actual safe displacement"),Character->GetActorLocation().X>200 && Character->GetActorLocation().X<400 && FMath::Abs(Character->GetActorLocation().Y)<100);
    FFindFloorResult RecoveredFloor;
    Movement->FindFloor(Character->GetActorLocation(),RecoveredFloor,false);
    TestTrue(TEXT("Recovery restores walking only with actual walkable support"),RecoveredFloor.IsWalkableFloor() && Movement->MovementMode==MOVE_Walking);
    TestTrue(TEXT("Successful recovery retires its independent anchor"),Recovery->IsActorBeingDestroyed());
    TestFalse(TEXT("Successful destination is free of blocking capsule overlap"),World->OverlapBlockingTestByChannel(
        Character->GetActorLocation(),Character->GetCapsuleComponent()->GetComponentQuat(),Character->GetCapsuleComponent()->GetCollisionObjectType(),
        Character->GetCapsuleComponent()->GetCollisionShape(),FCollisionQueryParams(SCENE_QUERY_STAT(FactoryFPVRecoveryTest),false,Character)));
    Platform->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FartherPlatform->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto* RoofRecovery=BeginPending();
    Platform->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    auto* Roof=MakeBox(FVector(300,0,165),FVector(75,75,25)); // 140 cm headroom, less than the 176 cm capsule.
    TestFalse(TEXT("A platform under an obstructing ceiling is not a safe recovery"),RoofRecovery->RecoverToNearbyFloor(Error));
    TestTrue(TEXT("Ceiling refusal preserves held location and movement"),Character->GetActorLocation().Equals(HeldPosition,.1) && Movement->MovementMode==MOVE_None);
    Roof->SetWorldLocation(FVector(300,0,225)); // 200 cm headroom fits, even though the raised search begins in the roof.
    TestTrue(TEXT("Adequate low-room headroom makes the same support recoverable"),RoofRecovery->RecoverToNearbyFloor(Error));
    Roof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Platform->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    auto* SlopeRecovery=BeginPending();
    auto* Slope=MakeBox(FVector(300,0,-30),FVector(75,75,10),FRotator(0,0,20));
    const float PreviousFloorAngle=Movement->GetWalkableFloorAngle();
    Movement->SetWalkableFloorAngle(10);
    TestFalse(TEXT("Recovery respects the pioneer's walkable slope limit"),SlopeRecovery->RecoverToNearbyFloor(Error));
    TestTrue(TEXT("Nonwalkable support does not release the hold"),SlopeRecovery->IsReleasePending() && Character->GetActorLocation().Equals(HeldPosition,.1));
    Slope->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->SetWalkableFloorAngle(PreviousFloorAngle);
    auto* OutOfRange=MakeBox(FVector(700,0,-25),FVector(25,25,25));
    auto* TooDeep=MakeBox(FVector(0,0,-4025),FVector(200,200,25));
    TestFalse(TEXT("Distant and excessively deep supports do not turn recovery into teleport travel"),SlopeRecovery->RecoverToNearbyFloor(Error));
    OutOfRange->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TooDeep->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Movement->SetMovementMode(MOVE_Flying);
    TestFalse(TEXT("Recovery cannot overwrite a newer external movement mode"),SlopeRecovery->RecoverToNearbyFloor(Error));
    SlopeRecovery->Tick(1.f/60.f);
    TestTrue(TEXT("External movement retires only our pending anchor"),SlopeRecovery->IsActorBeingDestroyed() && Movement->MovementMode==MOVE_Flying);
    TestFalse(TEXT("Retired pending anchor releases its streaming source"),SlopeRecovery->Streaming->IsStreamingSourceEnabled());

    FloorBody->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Character->SetActorLocation(HeldPosition,false,nullptr,ETeleportType::TeleportPhysics);
    Movement->SetMovementMode(MOVE_Walking);

    auto* OtherAnchor=World->SpawnActor<AFactoryFPVPioneerAnchor>();
    TestTrue(TEXT("Can begin a second session after returning"),OtherAnchor->Hold(Character,Error));
    Movement->SetMovementMode(MOVE_Flying);
    OtherAnchor->RequestRelease();
    TestTrue(TEXT("Does not overwrite a newer external movement mode"),Movement->MovementMode==MOVE_Flying);

    auto* AirborneAnchor=World->SpawnActor<AFactoryFPVPioneerAnchor>();
    Movement->SetMovementMode(MOVE_Falling);
    TestFalse(TEXT("Rejects launch while airborne"),AirborneAnchor->Hold(Character,Error));
    TestTrue(TEXT("Rejected launch preserves movement"),Movement->MovementMode==MOVE_Falling);
    AirborneAnchor->Destroy();
    // Positive control: this scene really simulates falling without the hold.
    FloorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    for (int Step=0;Step<120;++Step) Movement->TickComponent(1.f/60.f,LEVELTICK_All,&Movement->PrimaryComponentTick);
    TestTrue(TEXT("Unheld character falls in the same collision-free scene"),Character->GetActorLocation().Z<HeldPosition.Z-100);
    return true;
}
#endif
