#include "FactoryFPVRaceGateHologram.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVRaceGate.h"
#include "AbstractInstanceManager.h"
#include "Buildables/FGBuildableFoundation.h"
#include "CollisionQueryParams.h"
#include "FGClearanceData.h"
#include "FGLightweightBuildableSubsystem.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace {
constexpr double FloorTileSize=AFactoryFPVRaceGateFloor::TileSizeCm;

bool IsFlatFloorTile(const FTransform& Tile,const AFGBuildableFoundation* Foundation) {
    const FVector Scale=Tile.GetScale3D();
    return !Tile.ContainsNaN() && Scale.X>0. && Scale.Y>0. && Scale.Z>0. &&
        Tile.GetUnitAxis(EAxis::Z).Z>=.999 &&
        (!Foundation || (FMath::IsNearlyZero(Foundation->mElevation,.1f) && Foundation->mHeight>0.)) &&
        FMath::IsNearlyEqual((Foundation ? Foundation->mDepth : FloorTileSize)*Scale.X,FloorTileSize,1.) &&
        FMath::IsNearlyEqual((Foundation ? Foundation->mWidth : FloorTileSize)*Scale.Y,FloorTileSize,1.);
}

FVector FloorTileTop(const FTransform& Tile,const AFGBuildableFoundation* Foundation) {
    return Tile.TransformPosition(FVector(0,0,Foundation ? Foundation->mHeight*.5 : 0.));
}
}

UFactoryFPVRaceCourseFull::UFactoryFPVRaceCourseFull() {
    mDisqfualifyingText=NSLOCTEXT("FactoryFPV","RaceCourseFull","FPV course is full. Choose another course or finish building in the Racing tab.");
}

const UFactoryFPVLocalPlayerSubsystem* AFactoryFPVRaceGateHologram::LocalCourseBuilder() const {
    const UWorld* World=GetWorld();
    if (!World || World->GetNetMode()!=NM_Standalone || GetParentHologram() || GetBlueprintDesigner() || IsUpgrade()) return nullptr;
    const auto* Pioneer=GetConstructionInstigator();
    const auto* PC=Pioneer ? Cast<APlayerController>(Pioneer->GetController()) : nullptr;
    const auto* Local=PC && PC->IsLocalController() && PC->GetPawn()==Pioneer ? PC->GetLocalPlayer() : nullptr;
    return Local ? Local->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>() : nullptr;
}

void AFactoryFPVRaceGateHologram::CheckValidPlacement() {
    Super::CheckValidPlacement();
    if (const auto* Builder=LocalCourseBuilder();Builder && Builder->GetRaceBuildCourse()>0 && Builder->GetNextRaceBuildOrder()==0) {
        AddConstructDisqualifier(UFactoryFPVRaceCourseFull::StaticClass());
    }
}

void AFactoryFPVRaceGateHologram::ConfigureActor(AFGBuildable* Buildable) const {
    Super::ConfigureActor(Buildable);
    auto* Gate=Cast<AFactoryFPVRaceGate>(Buildable);
    if (!Gate || !Gate->HasAuthority() || Gate->HasAnyFlags(RF_WasLoaded) || Gate->bNumberAssigned || Gate->GetGateId().IsValid()) return;
    if (const auto* Builder=LocalCourseBuilder()) Gate->PendingBuildCourse=Builder->GetRaceBuildCourse();
}

AFactoryFPVRaceGateFloorHologram::AFactoryFPVRaceGateFloorHologram() {
    mRotationStep=90;
}

bool AFactoryFPVRaceGateFloorHologram::TrySnapToActor(const FHitResult& Hit) {
    FloorSnap.Reset();
    mSnappedBuilding=nullptr;
    if (!Hit.IsValidBlockingHit()) return false;

    AActor* HitActor=Hit.GetActor();
    const AFGBuildableFoundation* Foundation=Cast<AFGBuildableFoundation>(HitActor);
    const bool bFloorGate=HitActor && HitActor->IsA<AFactoryFPVRaceGateFloor>();
    FTransform Tile;
    if (Foundation || bFloorGate) {
        Tile=HitActor->GetActorTransform();
    } else {
        // Native foundations can be lightweight instances with no buildable
        // actor. Read their placement without spawning or converting them.
        auto* Instances=AAbstractInstanceManager::GetInstanceManager(this);
        FInstanceHandle Handle;
        FLightweightBuildableInstanceRef Instance;
        if (!Instances || !Instances->ResolveHit(Hit,Handle) ||
            !AFGLightweightBuildableSubsystem::ResolveLightweightInstance(Handle,Instance) || !Instance.GetBuildableClass()) return false;
        Foundation=Cast<AFGBuildableFoundation>(Instance.GetBuildableClass()->GetDefaultObject());
        if (!Foundation) return false;
        Tile=Instance.GetBuildableTransform();
    }

    if (!IsFlatFloorTile(Tile,Foundation)) return false;

    const FVector Normal=Tile.InverseTransformVectorNoScale(Hit.ImpactNormal);
    if (Normal.Z<-.25) return false;
    // Side hits point out of the neighboring tile. Top hits use the nearest
    // edge, so players can fill a one-tile opening while standing above it.
    const FVector Edge=Normal.Z>.5 ? Tile.InverseTransformPosition(Hit.ImpactPoint) : Normal;
    const FVector Offset=FMath::Abs(Edge.X)>=FMath::Abs(Edge.Y)
        ? FVector(Edge.X<0. ? -FloorTileSize : FloorTileSize,0,0) : FVector(0,Edge.Y<0. ? -FloorTileSize : FloorTileSize,0);
    const FQuat Rotation=Tile.GetRotation()*FQuat(FVector::UpVector,
        FMath::DegreesToRadians(FMath::GridSnap(static_cast<double>(GetScrollRotateValue()),90.)));
    SetActorLocationAndRotation(FloorTileTop(Tile,Foundation)+Tile.TransformVectorNoScale(Offset),Rotation);
    FloorSnap=GetActorTransform();
    mSnappedBuilding=Cast<AFGBuildable>(HitActor);
    return true;
}

void AFactoryFPVRaceGateFloorHologram::CheckClearance() {
    const FTransform Placement=GetActorTransform();
    const UWorld* World=GetWorld();
    if (!World || !FloorSnap.IsSet() || !Placement.Equals(FloorSnap.GetValue(),.1)) {
        Super::CheckClearance();
        return;
    }

    // The native broad clearance rejects a flush rotated seam. Keep the
    // snapped tile's own rail and terminal boxes oriented to the floor grid.
    TArray<const FFGClearanceData*> Clearances;
    GetClearanceData(Clearances);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVFloorGateClearance),false,this);
    Query.AddIgnoredActor(GetConstructionInstigator());
    for (const FFGClearanceData* Clearance:Clearances) {
        if (!Clearance || !Clearance->IsValid()) continue;
        const FTransform BoxToWorld=Clearance->RelativeTransform*Placement;
        const FVector Center=BoxToWorld.TransformPosition(Clearance->ClearanceBox.GetCenter());
        const FCollisionShape Box=FCollisionShape::MakeBox(Clearance->ClearanceBox.GetExtent()*BoxToWorld.GetScale3D().GetAbs());
        if (World->OverlapBlockingTestByChannel(Center,BoxToWorld.GetRotation(),ECC_WorldStatic,Box,Query) ||
            World->OverlapBlockingTestByChannel(Center,BoxToWorld.GetRotation(),ECC_WorldDynamic,Box,Query)) {
            AddConstructDisqualifier(UFGCDEncroachingClearance::StaticClass());
            return;
        }
    }
}
