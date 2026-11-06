#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVSettings.h"
#include "Components/SceneComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "GameFramework/Character.h"
#include "FGCharacterBase.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

AFactoryFPVPioneerAnchor::AFactoryFPVPioneerAnchor() {
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    PrimaryActorTick.TickGroup=TG_PrePhysics;
    bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PioneerLocation")));
    Streaming=CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("PioneerStreaming"));
    Streaming->Priority=EStreamingSourcePriority::Highest;
    Streaming->DisableStreamingSource();
}

AFactoryFPVPioneerAnchor* AFactoryFPVPioneerAnchor::FindPending(UWorld* World,ACharacter* Character) {
    if (!World || !Character || Character->GetWorld()!=World) return nullptr;
    for (TActorIterator<AFactoryFPVPioneerAnchor> It(World);It;++It) {
        if (It->Pioneer.Get()==Character && It->IsReleasePending()) return *It;
    }
    return nullptr;
}

bool AFactoryFPVPioneerAnchor::IsPioneerAlive() const {
    auto* Character=Pioneer.Get();
    if (!Character || Character->IsActorBeingDestroyed()) return false;
    auto* GameCharacter=Cast<AFGCharacterBase>(Character);
    return !GameCharacter || GameCharacter->IsAliveAndWell();
}

bool AFactoryFPVPioneerAnchor::HasFloor() const {
    if (!Pioneer.IsValid() || !Movement.IsValid()) return false;
    FFindFloorResult Floor;
    Movement->FindFloor(Pioneer->GetActorLocation(),Floor,false);
    return Floor.IsWalkableFloor();
}

bool AFactoryFPVPioneerAnchor::Hold(ACharacter* Character,FString& Error) {
    if (bHolding || !Character || !Character->GetCharacterMovement()) {
        Error=TEXT("The pioneer cannot enter remote flight right now.");
        return false;
    }
    Pioneer=Character;
    Movement=Character->GetCharacterMovement();
    if (!IsPioneerAlive() || !Movement->IsMovingOnGround() || !HasFloor()) {
        Error=TEXT("Stand on solid ground before launching the drone.");
        return false;
    }
    SetActorTransform(Character->GetActorTransform());
    PreviousMode=Movement->MovementMode;
    PreviousCustomMode=Movement->CustomMovementMode;
    Streaming->EnableStreamingSource();
    Movement->StopMovementImmediately();
    Movement->SetMovementMode(MOVE_None);
    bHolding=true;
    return true;
}

bool AFactoryFPVPioneerAnchor::IsHolding() const {
    return bHolding && !bReleaseRequested && IsPioneerAlive() && Movement.IsValid() && Movement->MovementMode==MOVE_None;
}

bool AFactoryFPVPioneerAnchor::IsReleasePending() const {
    return !IsActorBeingDestroyed() && bHolding && bReleaseRequested && IsPioneerAlive() &&
        Movement.IsValid() && Movement->MovementMode==MOVE_None;
}

EFactoryFPVReturnStatus AFactoryFPVPioneerAnchor::GetReturnStatus() const {
    if (!IsReleasePending()) return EFactoryFPVReturnStatus::None;
    if (!IsValid(Streaming) || !Streaming->IsStreamingCompleted()) return EFactoryFPVReturnStatus::LoadingGround;
    // The existing Tick will release a ready hold. Do not briefly label that
    // already-present support as missing while presentation precedes that Tick.
    return HasFloor() ? EFactoryFPVReturnStatus::None : EFactoryFPVReturnStatus::MissingFloor;
}

bool AFactoryFPVPioneerAnchor::IsDestinationLoaded(const FVector& Location) const {
    auto* World=GetWorld();
    if (!World) return false;
    if (!World->GetWorldPartition()) return true;
    const auto* Partition=World->GetSubsystem<UWorldPartitionSubsystem>();
    const auto* Capsule=Pioneer.IsValid() ? Pioneer->GetCapsuleComponent() : nullptr;
    if (!Partition || !Capsule) return false;
    FWorldPartitionStreamingQuerySource Query(Location);
    Query.bUseGridLoadingRange=false;
    Query.Radius=Capsule->GetScaledCapsuleHalfHeight()+50.f;
    if (const auto* PioneerDataLayerManager=World->GetWorldPartition()->GetDataLayerManager())
        Query.DataLayers=PioneerDataLayerManager->GetEffectiveActiveDataLayerNames().Array();
    TArray<FWorldPartitionStreamingQuerySource> Queries;
    Queries.Add(Query);
    // Query only: this never moves/enables a source to load an unknown destination.
    return Partition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated,Queries,false);
}

bool AFactoryFPVPioneerAnchor::RecoverToNearbyFloor(FString& Error) {
    Error.Reset();
    if (!IsReleasePending()) {
        Error=TEXT("No FPV return hold is waiting for this pioneer.");
        return false;
    }
    if (!Streaming->IsStreamingCompleted()) {
        Error=TEXT("Ground around your pioneer is still loading. Wait, then try recovery again.");
        return false;
    }
    if (HasFloor()) {
        TryCompleteRelease();
        return true;
    }
    auto* Character=Pioneer.Get();
    auto* Capsule=Character->GetCapsuleComponent();
    auto* World=GetWorld();
    if (!Capsule || !World || Capsule->GetCollisionEnabled()==ECollisionEnabled::NoCollision) {
        Error=TEXT("Pioneer collision is unavailable. Recovery will keep movement held.");
        return false;
    }
    const FVector Original=Character->GetActorLocation();
    const FQuat CapsuleRotation=Capsule->GetComponentQuat();
    const ECollisionChannel Channel=Capsule->GetCollisionObjectType();
    const FCollisionResponseParams Responses(Capsule->GetCollisionResponseToChannels());
    const FCollisionShape Shape=FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight());
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVPioneerRecovery),false,Character);
    Query.AddIgnoredActor(this);
    TArray<FVector> Candidates;
    // One-metre columns, restricted to six metres sideways, thirty below and
    // half a metre above the current hold. Full-capsule sweeps reject low roofs.
    constexpr int32 RadiusSteps=6;
    constexpr double Spacing=100.,MaximumDrop=3000.,MaximumRise=50.,FloorClearance=2.4;
    for (int32 X=-RadiusSteps;X<=RadiusSteps;++X) for (int32 Y=-RadiusSteps;Y<=RadiusSteps;++Y) {
        if (X*X+Y*Y>RadiusSteps*RadiusSteps) continue;
        const FVector Column=Original+FVector(X*Spacing,Y*Spacing,0);
        // Try current height first so a valid low room is not rejected just
        // because the raised search capsule would intersect its roof.
        for (double Rise:{0.,MaximumRise}) {
            const FVector Start=Column+FVector(0,0,Rise);
            const FVector End=Column-FVector(0,0,MaximumDrop);
            FHitResult Hit;
            if (!World->SweepSingleByChannel(Hit,Start,End,CapsuleRotation,Channel,Shape,Query,Responses) ||
                Hit.bStartPenetrating || !Movement->IsWalkable(Hit)) continue;
            const FVector Destination=Hit.Location+FVector(0,0,FloorClearance);
            if (Destination.Z>Original.Z+MaximumRise || Destination.Z<Original.Z-MaximumDrop ||
                !IsDestinationLoaded(Destination) ||
                World->OverlapBlockingTestByChannel(Destination,CapsuleRotation,Channel,Shape,Query,Responses)) continue;
            FFindFloorResult Floor;
            Movement->FindFloor(Destination,Floor,false);
            if (Floor.IsWalkableFloor()) Candidates.AddUnique(Destination);
        }
    }
    Candidates.Sort([&](const FVector& A,const FVector& B) {
        return FVector::DistSquared(A,Original)<FVector::DistSquared(B,Original);
    });
    for (const FVector& Destination:Candidates) {
        // Revalidate the full capsule immediately before placement. Do not use
        // ACharacter::TeleportTo here: its OnTeleported path changes MOVE_None
        // to MOVE_Falling before the deliberate return hold can be released.
        // This is a destination-checked physics teleport, not a movement sweep
        // through the empty/unloaded space between the two supported positions.
        if (!IsDestinationLoaded(Destination) ||
            World->OverlapBlockingTestByChannel(Destination,CapsuleRotation,Channel,Shape,Query,Responses)) continue;
        if (!Character->SetActorLocation(Destination,false,nullptr,ETeleportType::TeleportPhysics)) continue;
        if (!Movement.IsValid() || Movement->MovementMode!=MOVE_None) {
            bHolding=false;
            Streaming->DisableStreamingSource();
            Destroy();
            Error=TEXT("Another system took over pioneer movement during recovery.");
            return false;
        }
        const FVector Actual=Character->GetActorLocation();
        const FVector Displacement=Actual-Original;
        if (Displacement.SizeSquared2D()>FMath::Square(RadiusSteps*Spacing)+1. ||
            Displacement.Z>MaximumRise || Displacement.Z<-MaximumDrop || !HasFloor() || !IsDestinationLoaded(Actual) ||
            World->OverlapBlockingTestByChannel(Actual,CapsuleRotation,Channel,Shape,Query,Responses)) {
            // A rare engine adjustment/racing support change must not release
            // movement. Attempt to return to the original held position safely.
            if (IsDestinationLoaded(Original) &&
                !World->OverlapBlockingTestByChannel(Original,CapsuleRotation,Channel,Shape,Query,Responses))
                Character->SetActorLocation(Original,false,nullptr,ETeleportType::TeleportPhysics);
            SetActorLocation(Character->GetActorLocation());
            Error=TEXT("The recovery floor changed. Movement remains held; wait and retry.");
            return false;
        }
        Movement->StopMovementImmediately();
        Movement->bForceNextFloorCheck=true;
        SetActorLocation(Character->GetActorLocation());
        TryCompleteRelease();
        return true;
    }
    Error=TEXT("No clear loaded walkable ground found within 6 m sideways and 30 m below. Movement stays held. Restore a floor beneath the pioneer or try again after nearby ground loads.");
    return false;
}

void AFactoryFPVPioneerAnchor::RestoreMovement() {
    if (bHolding && IsPioneerAlive() && Movement.IsValid() && Movement->MovementMode==MOVE_None) {
        Movement->SetMovementMode(PreviousMode,PreviousCustomMode);
        Movement->bForceNextFloorCheck=true;
    }
    bHolding=false;
}

void AFactoryFPVPioneerAnchor::TryCompleteRelease() {
    if (!bReleaseRequested) return;
    if (!IsPioneerAlive()) { bHolding=false; Destroy(); return; }
    if (!Movement.IsValid() || Movement->MovementMode!=MOVE_None) {
        // Another system has taken over movement. Retire only our source.
        bHolding=false;
        Streaming->DisableStreamingSource();
        Destroy();
        return;
    }
    if (Streaming->IsStreamingCompleted() && HasFloor()) {
        RestoreMovement();
        if (GEngine) GEngine->RemoveOnScreenDebugMessage(GetUniqueID());
        Streaming->DisableStreamingSource();
        Destroy();
    } else if (GEngine) {
        GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()),1.f,FColor::Orange,
            Streaming->IsStreamingCompleted() ?
                FString::Printf(TEXT("FPV: original floor is unavailable. %s opens safe pioneer recovery. Movement stays held."),
                    *FFactoryFPVKeyboardShortcuts::KeyLabel(GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar)) :
                TEXT("FPV: loading ground around your pioneer. Movement resumes when the floor is ready."));
    }
}

void AFactoryFPVPioneerAnchor::RequestRelease() {
    bReleaseRequested=true;
    TryCompleteRelease();
}

void AFactoryFPVPioneerAnchor::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    if (!bHolding) return;
    if (!IsPioneerAlive()) { bHolding=false; Destroy(); return; }
    if (!Movement.IsValid() || Movement->MovementMode!=MOVE_None) {
        bHolding=false;
        Streaming->DisableStreamingSource();
        Destroy();
        return;
    }
    SetActorTransform(Pioneer->GetActorTransform());
    TryCompleteRelease();
}

void AFactoryFPVPioneerAnchor::EndPlay(const EEndPlayReason::Type Reason) {
    // Balance our movement override on travel/shutdown as well as normal exit.
    RestoreMovement();
    Streaming->DisableStreamingSource();
    if (GEngine) GEngine->RemoveOnScreenDebugMessage(GetUniqueID());
    Super::EndPlay(Reason);
}
