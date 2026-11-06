#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRaceGateHologram.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include <cmath>

namespace {
constexpr double VerticalGateHeightCm=260.;
constexpr double VerticalGateOpeningHalfCm=180.;
constexpr double FloorGateOpeningHalfCm=360.;
constexpr double VerticalApproachProbeLengthCm=200.;
bool IsValidLapTime(double Seconds) {
    return std::isfinite(Seconds) && Seconds>0.;
}
bool IsValidLapSplitSequence(TConstArrayView<double> Splits,double FinishSeconds) {
    if (Splits.IsEmpty() || Splits.Num()>=AFactoryFPVRaceGate::MaximumGateOrder) return false;
    double Previous=0.;
    for (const double Split:Splits) {
        if (!(Split>Previous && Split<FinishSeconds)) return false;
        Previous=Split;
    }
    return true;
}
bool IsPreferredLapRecord(double Seconds,TConstArrayView<double> Splits,double Previous,TConstArrayView<double> PreviousSplits) {
    return IsValidLapTime(Seconds) && (!IsValidLapTime(Previous) || Seconds<Previous ||
        (Seconds==Previous && IsValidLapSplitSequence(Splits,Seconds) && !IsValidLapSplitSequence(PreviousSplits,Previous)));
}
void ShapeRaceGatePart(UStaticMeshComponent* Mesh,FVector Location,FVector Size,bool Visible,bool Collision) {
    Mesh->SetRelativeLocation(Location);
    Mesh->SetRelativeScale3D(Size/100.);
    Mesh->SetVisibility(Visible);
    Mesh->SetHiddenInGame(!Visible);
    Mesh->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
}
}

AFactoryFPVRaceGate::AFactoryFPVRaceGate() {
    mDisplayName=NSLOCTEXT("FactoryFPV","RaceGateCompactName","FPV Compact Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","RaceGateDescriptionCompact","A 3.4 m wide powered timing gate with a 3 m square clear opening. Connect power and configure its course and checkpoint number. Follow the arrow; checkpoint 1 is start/finish.");
    mHologramClass=AFactoryFPVRaceGateHologram::StaticClass();
    mFactoryTickFunction.bCanEverTick=true;
    mFactoryTickFunction.bStartWithTickEnabled=true;
    mFactoryTickFunction.TickInterval=.25f;
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bStartWithTickEnabled=true;
    PrimaryActorTick.TickInterval=.1f;

    PowerInfo=CreateDefaultSubobject<UFGPowerInfoComponent>(TEXT("RacePowerInfo"));
    PowerConnection=CreateDefaultSubobject<UFGPowerConnectionComponent>(TEXT("PowerConnection"));
    PowerConnection->SetupAttachment(RootComponent);
    PowerConnection->SetRelativeLocation(FVector(-25,-160,210));

    auto Part=[&](const TCHAR* Name,FVector Location,FVector Size,bool bCollision) {
        auto* Mesh=CreateMeshPart(Name,bCollision);
        Mesh->SetRelativeLocation(Location);
        Mesh->SetRelativeScale3D(Size/100.);
        return Mesh;
    };
    // Preserve saved subobject names. Geometry is applied centrally below,
    // including the pole subclass before its components register.
    FrameParts.Add(Part(TEXT("MainMesh_LeftPost"),FVector(0,-160,170),FVector(25,20,340),true));
    FrameParts.Add(Part(TEXT("MainMesh_RightPost"),FVector(0,160,170),FVector(25,20,340),true));
    FrameParts.Add(Part(TEXT("MainMesh_TopBar"),FVector(0,0,330),FVector(25,300,20),true));
    FrameParts.Add(Part(TEXT("MainMesh_BottomBar"),FVector(0,0,10),FVector(25,300,20),true));
    FrameParts.Add(Part(TEXT("MainMesh_PowerSocket"),FVector(-19,-160,210),FVector(15,24,25),true));
    // Permanent orange floor arrow beside the left post. Approaching from -X,
    // its head points through the gate in +X. It remains visible without power
    // and never adds collision or an invisible obstruction to the aperture.
    auto* DirectionMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/FactoryFPV/Materials/M_Accent.M_Accent"));
    auto Direction=[&](const TCHAR* Name,FVector Location,FVector Size,float Yaw) {
        auto* Marker=Part(Name,Location,Size,false);
        Marker->SetMobility(EComponentMobility::Movable);
        Marker->SetRelativeRotation(FRotator(0,Yaw,0));
        if (DirectionMaterial) Marker->SetMaterial(0,DirectionMaterial);
        DirectionParts.Add(Marker);
    };
    Direction(TEXT("MainMesh_DirectionShaft"),FVector(-105,-210,2.5),FVector(60,7,3),0);
    Direction(TEXT("MainMesh_DirectionHeadLeft"),FVector(-75,-222.9,2.5),FVector(45,8,3),35);
    Direction(TEXT("MainMesh_DirectionHeadRight"),FVector(-75,-197.1,2.5),FVector(45,8,3),-35);
    // Thin strips sit on both faces of the frame. Their materials and the real
    // point lights share exactly the same grid-derived powered state.
    for (int32 Side=0;Side<2;++Side) {
        const float X=Side==0 ? -13.f : 13.f;
        LightStrips.Add(Part(*FString::Printf(TEXT("MainMesh_Light%d_Left"),Side),FVector(X,-160,170),FVector(2,5,300),false));
        LightStrips.Add(Part(*FString::Printf(TEXT("MainMesh_Light%d_Right"),Side),FVector(X,160,170),FVector(2,5,300),false));
        LightStrips.Add(Part(*FString::Printf(TEXT("MainMesh_Light%d_Top"),Side),FVector(X,0,330),FVector(2,300,5),false));
        LightStrips.Add(Part(*FString::Printf(TEXT("MainMesh_Light%d_Bottom"),Side),FVector(X,0,10),FVector(2,300,5),false));
        auto* Light=CreateDefaultSubobject<UPointLightComponent>(*FString::Printf(TEXT("GateLight%d"),Side));
        Light->SetupAttachment(RootComponent);
        Light->SetRelativeLocation(FVector(X*4,0,235));
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetCastShadows(false);
        Light->SetAttenuationRadius(400);
        Light->SetIntensity(1200);
        Light->SetVisibility(false);
        Lights.Add(Light);
        auto* Label=CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("GateLabel%d"),Side));
        Label->SetupAttachment(RootComponent);
        Label->SetRelativeLocation(FVector(X*1.5,0,361));
        Label->SetRelativeRotation(FRotator(0,Side==0 ? 180 : 0,0));
        Label->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
        Label->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
        Label->SetWorldSize(34);
        Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Label->SetCastShadow(false);
        Labels.Add(Label);
    }
    // DefaultTextMaterialOpaque is two-sided. A solid opaque board between
    // the outward-facing labels occludes the other face's mirrored glyphs.
    LabelBoard=Part(TEXT("MainMesh_LabelBoard"),FVector(0,0,455),FVector(14,360,100),false);
    if (auto* Material=LoadObject<UMaterialInterface>(nullptr,
        TEXT("/FactoryFPV/Materials/M_RaceLight.M_RaceLight"),nullptr,LOAD_NoWarn|LOAD_Quiet))
        for (UStaticMeshComponent* Strip:LightStrips) Strip->SetMaterial(0,Material);
    ApplyCheckpointGeometry();
}

UStaticMeshComponent* AFactoryFPVRaceGate::CreateMeshPart(const TCHAR* Name,bool Collision) {
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Box(TEXT("/FactoryFPV/Meshes/SM_FrameBox.SM_FrameBox"));
    auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Mesh->SetupAttachment(RootComponent);
    Mesh->SetStaticMesh(Box.Object);
    Mesh->SetMobility(EComponentMobility::Static);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SetCollisionEnabled(Collision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Mesh->SetCollisionObjectType(ECC_WorldStatic);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    Mesh->SetCastShadow(Collision);
    return Mesh;
}

AFactoryFPVRaceGateFull::AFactoryFPVRaceGateFull() {
    bFullSizeGate=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","RaceGateFullName","FPV Full-Size Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","RaceGateFullDescription","An 8 m wide powered timing gate with a 7.6 m by 3.6 m clear opening. Connect power and configure its course and checkpoint number. Follow the arrow; checkpoint 1 is start/finish.");
    ApplyCheckpointGeometry();
}

AFactoryFPVRaceGateVertical::AFactoryFPVRaceGateVertical() {
    bVerticalRaceGate=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","VerticalRaceGateName","FPV Vertical Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","VerticalRaceGateDescription","A powered horizontal gate for climbs and dives. Its opening is 3.6 m square and 2.6 m above the build surface. Configure Ascend or Descend after building.");
    CreateVerticalSupports();
    ApplyCheckpointGeometry();
}

AFactoryFPVRaceGateFloor::AFactoryFPVRaceGateFloor() {
    bVerticalRaceGate=true;
    bFloorRaceGate=true;
    mHologramClass=AFactoryFPVRaceGateFloorHologram::StaticClass();
    mDisplayName=NSLOCTEXT("FactoryFPV","FloorRaceGateName","FPV Floor Race Gate");
    mDescription=NSLOCTEXT("FactoryFPV","FloorRaceGateDescription","A powered 8 m by 8 m floor tile with a 7.2 m square opening. Snap it next to a flat foundation in an empty tile space. Configure Ascend or Descend after building.");
    ApplyCheckpointGeometry();
}

AFactoryFPVRacePole::AFactoryFPVRacePole() {
    bPoleCheckpoint=true;
    mDisplayName=NSLOCTEXT("FactoryFPV","RacePoleName","FPV Slalom Pole");
    mDescription=NSLOCTEXT("FactoryFPV","RacePoleDescription","A powered open checkpoint. Configure a left/right passing side and fly past that side following the forward arrow. Pass within 8 m of the pole and below its top; this is not a full orbit.");
    ApplyCheckpointGeometry();
}

void AFactoryFPVRaceGate::ApplyCheckpointGeometry() {
    if (FrameParts.Num()!=5 || LightStrips.Num()!=8 || Labels.Num()!=2) return;
    if (bVerticalRaceGate) {
        if (bFloorRaceGate) ApplyFloorCheckpointGeometry();
        else ApplyVerticalCheckpointGeometry();
        RefreshClearance();
        return;
    }
    const auto Shape=ShapeRaceGatePart;
    const bool Compact=!bPoleCheckpoint && !bFullSizeGate;
    const FVector2D Aperture=GetApertureHalfExtents();
    const double CenterZ=GetLocalApertureTransform().GetLocation().Z;
    const double Height=2*CenterZ,TopZ=Height-10;
    const double PostY=bPoleCheckpoint ? 0 : Aperture.X+10;
    const double Span=bPoleCheckpoint ? 26 : 2*Aperture.X;
    const double SocketZ=Compact ? 210 : 250;
    Shape(FrameParts[0],FVector(0,-PostY,CenterZ),FVector(bPoleCheckpoint ? 30 : 25,bPoleCheckpoint ? 30 : 20,Height),true,true);
    Shape(FrameParts[1],FVector(0,PostY,CenterZ),FVector(25,20,Height),!bPoleCheckpoint,!bPoleCheckpoint);
    Shape(FrameParts[2],FVector(0,0,TopZ),FVector(25,Span,20),!bPoleCheckpoint,!bPoleCheckpoint);
    Shape(FrameParts[3],FVector(0,0,10),FVector(25,Span,20),!bPoleCheckpoint,!bPoleCheckpoint);
    Shape(FrameParts[4],FVector(Compact ? -19 : -22,-PostY,SocketZ),FVector(15,24,25),true,true);
    // Unused meshes cannot contribute a ghost frame to a pole hologram/bounds.
    if (bPoleCheckpoint) for (int32 Index=1;Index<=3;++Index) FrameParts[Index]->SetStaticMesh(nullptr);
    PowerConnection->SetRelativeLocation(FVector(Compact ? -25 : -28,-PostY,SocketZ));
    const double ArrowY=bPoleCheckpoint ? GetPolePassSide()*120. : -PostY-50.;
    if (DirectionParts.Num()==3) {
        DirectionParts[0]->SetRelativeLocation(FVector(-105,ArrowY,2.5));
        DirectionParts[1]->SetRelativeLocation(FVector(-75,ArrowY-12.9f,2.5));
        DirectionParts[2]->SetRelativeLocation(FVector(-75,ArrowY+12.9f,2.5));
    }
    for (int32 Side=0;Side<2;++Side) {
        const double FaceX=bPoleCheckpoint ? 16. : 13.;
        const double X=Side==0 ? -FaceX : FaceX;
        const int32 Base=Side*4;
        Shape(LightStrips[Base],FVector(X,bPoleCheckpoint ? -10 : -PostY,CenterZ),FVector(2,bPoleCheckpoint ? 3 : 5,2*Aperture.Y),true,false);
        Shape(LightStrips[Base+1],FVector(X,bPoleCheckpoint ? 10 : PostY,CenterZ),FVector(2,bPoleCheckpoint ? 3 : 5,2*Aperture.Y),true,false);
        Shape(LightStrips[Base+2],FVector(X,0,TopZ),FVector(2,Span,5),true,false);
        Shape(LightStrips[Base+3],FVector(X,0,10),FVector(2,Span,5),true,false);
        Lights[Side]->SetRelativeLocation(FVector(X*4,0,Compact ? 235 : 270));
        Lights[Side]->SetAttenuationRadius(Compact ? 400 : 500);
        Labels[Side]->SetRelativeLocation(FVector(Side==0 ? -10 : 10,0,Height+55));
        Labels[Side]->SetWorldSize(Compact ? 30 : 34);
    }
    Shape(LabelBoard,FVector(0,0,Height+55),FVector(14,bPoleCheckpoint ? 260 : Compact ? 300 : 360,Compact ? 90 : 100),true,false);
    RefreshClearance();
}

void AFactoryFPVRaceGate::CreateVerticalSupports() {
    VerticalSupports.Add(CreateMeshPart(TEXT("MainMesh_VerticalSupportLeft"),true));
    VerticalSupports.Add(CreateMeshPart(TEXT("MainMesh_VerticalSupportRight"),true));
}

void AFactoryFPVRaceGate::ApplyVerticalCheckpointGeometry() {
    const auto Shape=ShapeRaceGatePart;
    constexpr double RailCenter=VerticalGateOpeningHalfCm+10.;
    constexpr double Span=2.*VerticalGateOpeningHalfCm;
    constexpr double SupportHeight=VerticalGateHeightCm-10.;
    constexpr double BoardX=-255.;
    Shape(FrameParts[0],FVector(0,-RailCenter,VerticalGateHeightCm),FVector(Span+40,20,20),true,true);
    Shape(FrameParts[1],FVector(0,RailCenter,VerticalGateHeightCm),FVector(Span+40,20,20),true,true);
    Shape(FrameParts[2],FVector(-RailCenter,0,VerticalGateHeightCm),FVector(20,Span,20),true,true);
    Shape(FrameParts[3],FVector(RailCenter,0,VerticalGateHeightCm),FVector(20,Span,20),true,true);
    Shape(FrameParts[4],FVector(-RailCenter,-RailCenter,282),FVector(22,24,24),true,true);
    PowerConnection->SetRelativeLocation(FVector(-207,-190,282));
    for (int32 Side=0;Side<2;++Side) {
        const double Sign=Side==0 ? -1. : 1.;
        const double Y=Sign*RailCenter;
        Shape(VerticalSupports[Side],FVector(-RailCenter,Y,SupportHeight*.5),FVector(20,20,SupportHeight),true,true);
        const double LightZ=VerticalGateHeightCm+Sign*11.;
        const int32 Base=Side*4;
        Shape(LightStrips[Base],FVector(0,-RailCenter,LightZ),FVector(Span+20,5,2),true,false);
        Shape(LightStrips[Base+1],FVector(0,RailCenter,LightZ),FVector(Span+20,5,2),true,false);
        Shape(LightStrips[Base+2],FVector(-RailCenter,0,LightZ),FVector(5,Span,2),true,false);
        Shape(LightStrips[Base+3],FVector(RailCenter,0,LightZ),FVector(5,Span,2),true,false);
        Lights[Side]->SetRelativeLocation(FVector(0,0,VerticalGateHeightCm+Sign*52.));
        Lights[Side]->SetAttenuationRadius(400);
        Labels[Side]->SetRelativeLocation(FVector(BoardX,0,VerticalGateHeightCm+Sign*9.));
        Labels[Side]->SetRelativeRotation(FRotationMatrix::MakeFromXZ(FVector(0,0,Sign),FVector::ForwardVector).Rotator());
        Labels[Side]->SetWorldSize(30);
    }
    Shape(LabelBoard,FVector(BoardX,0,VerticalGateHeightCm),FVector(110,330,12),true,false);

    // The unpowered arrow sits beside a support. Flip it with the requested
    // crossing direction while leaving the frame, socket and collision fixed.
    const double ArrowZ=GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend ? -1. : 1.;
    const FQuat ArrowRotation=FRotationMatrix::MakeFromXY(FVector(0,0,ArrowZ),FVector::ForwardVector).ToQuat();
    const FVector ArrowOrigin(-RailCenter,-RailCenter-22.,171);
    const FVector Positions[]={FVector(-20,0,0),FVector(10,-12.9,0),FVector(10,12.9,0)};
    const float Yaws[]={0,35,-35};
    for (int32 Index=0;Index<DirectionParts.Num();++Index) {
        DirectionParts[Index]->SetRelativeLocation(ArrowOrigin+ArrowRotation.RotateVector(Positions[Index]));
        DirectionParts[Index]->SetRelativeRotation(ArrowRotation*FRotator(0,Yaws[Index],0).Quaternion());
    }
}

void AFactoryFPVRaceGate::ApplyFloorCheckpointGeometry() {
    constexpr double Tile=AFactoryFPVRaceGateFloor::TileSizeCm;
    constexpr double Depth=AFactoryFPVRaceGateFloor::TileDepthCm;
    constexpr double Opening=2.*FloorGateOpeningHalfCm;
    constexpr double RailWidth=(Tile-Opening)*.5;
    constexpr double RailCenter=(Tile+Opening)*.25;
    constexpr double CenterZ=-Depth*.5;
    ShapeRaceGatePart(FrameParts[0],FVector(0,-RailCenter,CenterZ),FVector(Tile,RailWidth,Depth),true,true);
    ShapeRaceGatePart(FrameParts[1],FVector(0,RailCenter,CenterZ),FVector(Tile,RailWidth,Depth),true,true);
    ShapeRaceGatePart(FrameParts[2],FVector(-RailCenter,0,CenterZ),FVector(RailWidth,Opening,Depth),true,true);
    ShapeRaceGatePart(FrameParts[3],FVector(RailCenter,0,CenterZ),FVector(RailWidth,Opening,Depth),true,true);
    ShapeRaceGatePart(FrameParts[4],FVector(-RailCenter,-RailCenter,10),FVector(24,24,20),true,true);
    PowerConnection->SetRelativeLocation(FVector(-RailCenter,-RailCenter,23));
    for (UStaticMeshComponent* Marker:DirectionParts) {
        // Face lighting and the top/bottom markings carry the direction on
        // this flush design. Remove the raised gate's freestanding arrows.
        Marker->SetStaticMesh(nullptr);
    }
    for (int32 Side=0;Side<2;++Side) {
        const double Sign=Side==0 ? -1. : 1.;
        const double FaceZ=CenterZ+Sign*(Depth*.5+1.);
        const int32 Base=Side*4;
        constexpr double LightRail=FloorGateOpeningHalfCm+8.;
        ShapeRaceGatePart(LightStrips[Base],FVector(0,-LightRail,FaceZ),FVector(Opening+16.,5,2),true,false);
        ShapeRaceGatePart(LightStrips[Base+1],FVector(0,LightRail,FaceZ),FVector(Opening+16.,5,2),true,false);
        ShapeRaceGatePart(LightStrips[Base+2],FVector(-LightRail,0,FaceZ),FVector(5,Opening+16.,2),true,false);
        ShapeRaceGatePart(LightStrips[Base+3],FVector(LightRail,0,FaceZ),FVector(5,Opening+16.,2),true,false);
        Lights[Side]->SetRelativeLocation(FVector(0,0,CenterZ+Sign*(Depth*.5+35.)));
        Labels[Side]->SetRelativeLocation(FVector(-RailCenter-4.,0,FaceZ+Sign));
        Labels[Side]->SetRelativeRotation(FRotationMatrix::MakeFromXZ(FVector(0,0,Sign),FVector::ForwardVector).Rotator());
        Labels[Side]->SetWorldSize(12);
    }
    // Each face already has its own opaque rail, which blocks the far label.
    LabelBoard->SetStaticMesh(nullptr);
}

void AFactoryFPVRaceGate::RefreshClearance() {
    // Clearance describes only physical solids, never the scoring passage.
    mClearanceData.Reset();
    auto AddSolid=[this](const UStaticMeshComponent* Frame) {
        if (Frame->GetCollisionEnabled()==ECollisionEnabled::NoCollision) return;
        FFGClearanceData Clearance;
        const FVector HalfSize=Frame->GetRelativeScale3D()*50.;
        const FVector Center=Frame->GetRelativeLocation();
        FVector Min=-HalfSize,Max=HalfSize;
        if (bFloorRaceGate) {
            // Native clearance comparisons may count shared faces as overlap.
            // Inset only the tile seams; the real collision still meets them.
            constexpr double Edge=AFactoryFPVRaceGateFloor::TileSizeCm*.5;
            if (Center.X+Min.X<=-Edge) Min.X+=1.;
            if (Center.Y+Min.Y<=-Edge) Min.Y+=1.;
            if (Center.X+Max.X>=Edge) Max.X-=1.;
            if (Center.Y+Max.Y>=Edge) Max.Y-=1.;
        }
        Clearance.ClearanceBox=FBox(Min,Max);
        Clearance.RelativeTransform=FTransform(FQuat::Identity,Center);
        mClearanceData.Add(Clearance);
    };
    for (const UStaticMeshComponent* Frame:FrameParts) AddSolid(Frame);
    for (const UStaticMeshComponent* Support:VerticalSupports) AddSolid(Support);
}

void AFactoryFPVRaceGate::BeginPlay() {
    PrepareForWorldEntry();
    ApplyCheckpointGeometry();
    // SetPowerInfo is the supported registration path; a connection being near
    // a cable or a live pole alone never qualifies as a powered timing gate.
    PowerConnection->SetPowerInfo(PowerInfo);
    Super::BeginPlay();
    if (HasAuthority()) {
        EnsureIdentityAndOrder();
        PowerInfo->SetMaximumTargetConsumption(PowerConsumptionMW);
        PowerInfo->SetTargetConsumption(IsBuildableInsideBlueprintDesigner() ? 0.f : PowerConsumptionMW);
    }
    CreateLightMaterials();
    UpdatePresentation();
}

void AFactoryFPVRaceGate::PrepareForWorldEntry() {
    // A streamed level can re-enter play with this same actor instance before
    // garbage collection. EndPlay's temporary stop must not become permanent,
    // and its hidden lights must be reapplied even if the grid state is unchanged.
    bEndingPlay=false;
    bPresentationInitialized=false;
    bNextCheckpoint=false;
}

void AFactoryFPVRaceGate::PostSerializedFromBlueprint(bool bIsBlueprintWorld) {
    Super::PostSerializedFromBlueprint(bIsBlueprintWorld);
    PendingBuildCourse=0;
    if (!HasAuthority()) return;
    // A blueprint copy can be placed after the original is gone. Reset even
    // when no loaded actor has the copied GUID to expose the duplication.
    GateGuid.Invalidate();
    ClearBestLapRecords();
    if (!bIsBlueprintWorld && HasActorBegunPlay()) EnsureIdentityAndOrder();
}

void AFactoryFPVRaceGate::EnsureIdentityAndOrder() {
    if (!HasAuthority() || !IsLiveRaceGate()) return;
    bool Duplicate=false;
    if (GateGuid.IsValid()) for (TActorIterator<AFactoryFPVRaceGate> It(GetWorld());It;++It) {
        if (*It!=this && It->IsLiveRaceGate() && It->GateGuid==GateGuid) {
            Duplicate=true;
            break;
        }
    }
    if (!GateGuid.IsValid() || Duplicate) {
        GateGuid=FGuid::NewGuid();
        // A placed copy has its own identity and must not inherit records from
        // the original gate, including copies made through a save archive.
        ClearBestLapRecords();
    }
    AssignDefaultOrder();
}

void AFactoryFPVRaceGate::EndPlay(const EEndPlayReason::Type Reason) {
    bEndingPlay=true;
    bNextCheckpoint=false;
    if (HasAuthority() && PowerInfo) PowerInfo->SetTargetConsumption(0);
    for (UPointLightComponent* Light:Lights) if (Light) Light->SetVisibility(false);
    Super::EndPlay(Reason);
}

void AFactoryFPVRaceGate::Factory_Tick(float DeltaSeconds) {
    Super::Factory_Tick(DeltaSeconds);
    // Demand belongs to the factory tick. Rendering is updated on the normal
    // actor tick; no Slate/material work is performed in factory simulation.
    if (HasAuthority() && IsLiveRaceGate() && PowerInfo)
        PowerInfo->SetTargetConsumption(PowerConsumptionMW);
}

void AFactoryFPVRaceGate::Tick(float DeltaSeconds) {
    Super::Tick(DeltaSeconds);
    UpdatePresentation();
}

void AFactoryFPVRaceGate::OnConstruction(const FTransform& Transform) {
    Super::OnConstruction(Transform);
    ApplyCheckpointGeometry();
    UpdatePresentation();
}

int32 AFactoryFPVRaceGate::NextCourseBuildOrder(UWorld* World,int32 CourseId) {
    if (!World || CourseId<1 || CourseId>MaximumCourseId) return 0;
    int32 LastOrder=0;
    for (TActorIterator<AFactoryFPVRaceGate> It(World);It;++It) {
        if (It->IsLiveRaceGate() && It->bNumberAssigned && It->CourseId==CourseId) LastOrder=FMath::Max(LastOrder,It->GateOrder);
    }
    return LastOrder<MaximumGateOrder ? LastOrder+1 : 0;
}

void AFactoryFPVRaceGate::AssignDefaultOrder() {
    if (bNumberAssigned || !HasAuthority() || !IsLiveRaceGate()) return;
    const int32 BuildCourse=PendingBuildCourse;
    PendingBuildCourse=0;
    if (BuildCourse>=1 && BuildCourse<=MaximumCourseId && !HasAnyFlags(RF_WasLoaded) && !GetBlueprintProxy()) {
        CourseId=BuildCourse;
        GateOrder=NextCourseBuildOrder(GetWorld(),BuildCourse);
        bNumberAssigned=true;
        return;
    }
    CourseId=FMath::Clamp(CourseId,1,MaximumCourseId);
    TSet<int32> Used;
    for (TActorIterator<AFactoryFPVRaceGate> It(GetWorld());It;++It) {
        if (*It!=this && It->IsLiveRaceGate() &&
            It->bNumberAssigned && It->CourseId==CourseId) Used.Add(It->GateOrder);
    }
    GateOrder=0;
    for (int32 Candidate=1;Candidate<=MaximumGateOrder;++Candidate)
        if (!Used.Contains(Candidate)) { GateOrder=Candidate; break; }
    bNumberAssigned=true;
}

bool AFactoryFPVRaceGate::Configure(int32 NewCourseId,int32 NewGateOrder,FString& Error) {
    return Configure(NewCourseId,NewGateOrder,GetPolePassSide(),Error);
}
bool AFactoryFPVRaceGate::Configure(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,FString& Error) {
    return Configure(NewCourseId,NewGateOrder,NewPassSide,GetRaceDirection(),Error);
}
bool AFactoryFPVRaceGate::Configure(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,EFactoryFPVRaceGateDirection NewDirection,FString& Error) {
    if (!ValidateConfiguration(NewCourseId,NewGateOrder,NewPassSide,NewDirection,Error)) return false;
    ApplyConfiguration(NewCourseId,NewGateOrder,NewPassSide,NewDirection);
    return true;
}

bool AFactoryFPVRaceGate::ValidateConfiguration(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,
    EFactoryFPVRaceGateDirection NewDirection,FString& Error) const {
    Error.Reset();
    if (!HasAuthority() || GetNetMode()!=NM_Standalone || bEndingPlay || GetIsDismantled()) {
        Error=TEXT("Gate configuration is available in local single-player.");
        return false;
    }
    if (NewCourseId<1 || NewCourseId>MaximumCourseId || NewGateOrder<1 || NewGateOrder>MaximumGateOrder) {
        Error=TEXT("Choose course 1-99 and gate 1-128. Gate 1 is start/finish.");
        return false;
    }
    if (NewPassSide!=-1 && NewPassSide!=1) { Error=TEXT("Choose left or right of the pole, following its forward arrow.");return false; }
    const bool VerticalDirection=NewDirection==EFactoryFPVRaceGateDirection::Ascend || NewDirection==EFactoryFPVRaceGateDirection::Descend;
    if (bVerticalRaceGate ? !VerticalDirection : NewDirection!=EFactoryFPVRaceGateDirection::Forward) {
        Error=bVerticalRaceGate ? TEXT("Choose Ascend or Descend for this gate.") : TEXT("This checkpoint uses the forward direction.");
        return false;
    }
    return true;
}

void AFactoryFPVRaceGate::ApplyConfiguration(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,
    EFactoryFPVRaceGateDirection NewDirection) {
    const bool SideChanged=bPoleCheckpoint && NewPassSide!=GetPolePassSide();
    const bool DirectionChanged=NewDirection!=GetRaceDirection();
    CourseId=NewCourseId;
    GateOrder=NewGateOrder;
    bNumberAssigned=true;
    PolePassSide=NewPassSide;
    RaceDirection=NewDirection;
    if (SideChanged || DirectionChanged) ApplyCheckpointGeometry();
    UpdatePresentation();
    ForceNetUpdate();
}

AFactoryFPVRaceGate::FEditorSlotOccupants AFactoryFPVRaceGate::FindEditorSlotOccupants(int32 DestinationCourse,int32 DestinationOrder) const {
    FEditorSlotOccupants Occupants;
    for (TActorIterator<AFactoryFPVRaceGate> It(GetWorld());It;++It) {
        if (*It==this || !It->IsLiveRaceGate()) continue;
        if (It->CourseId==CourseId && It->GateOrder==GateOrder) ++Occupants.SourceCount;
        if (It->CourseId==DestinationCourse && It->GateOrder==DestinationOrder) {
            Occupants.Destination=*It;
            ++Occupants.DestinationCount;
        }
    }
    return Occupants;
}

bool AFactoryFPVRaceGate::ConfigureFromEditor(int32 NewCourseId,int32 NewGateOrder,int32 NewPassSide,
    EFactoryFPVRaceGateDirection NewDirection,FString& Error,TOptional<FEditorOrderSwap>& ProposedSwap) {
    ProposedSwap.Reset();
    if (!IsInGameThread() || IsTemplate() || IsActorBeingDestroyed() || !GetWorld()) {
        Error=TEXT("This checkpoint is no longer available for editing.");
        return false;
    }
    if (!ValidateConfiguration(NewCourseId,NewGateOrder,NewPassSide,NewDirection,Error)) return false;
    if (IsBuildableInsideBlueprintDesigner()) {
        // A designer preview configures its own intended blueprint values. It
        // neither occupies nor exchanges a live course slot.
        ApplyConfiguration(NewCourseId,NewGateOrder,NewPassSide,NewDirection);
        return true;
    }
    if (!IsLiveRaceGate() || !bNumberAssigned || !GateGuid.IsValid()) {
        Error=TEXT("This checkpoint is not ready for editing. Try again shortly.");
        return false;
    }
    const auto Occupants=FindEditorSlotOccupants(NewCourseId,NewGateOrder);
    if (!Occupants.DestinationCount) {
        ApplyConfiguration(NewCourseId,NewGateOrder,NewPassSide,NewDirection);
        return true;
    }
    const auto* Other=Occupants.Destination;
    if (NewCourseId!=CourseId || NewGateOrder==GateOrder || GateOrder<1 || GateOrder>MaximumGateOrder ||
        Occupants.SourceCount!=0 || Occupants.DestinationCount!=1 || !Other->HasAuthority() || !Other->bNumberAssigned ||
        !Other->GateGuid.IsValid() || Other->GateGuid==GateGuid) {
        Error=FString::Printf(TEXT("Checkpoint %d on course %d is already in use. Choose a free checkpoint number."),NewGateOrder,NewCourseId);
        return false;
    }
    auto& Swap=ProposedSwap.Emplace();
    Swap.OtherGate=Occupants.Destination;
    Swap.CourseId=CourseId;
    Swap.FromOrder=GateOrder;
    Swap.ToOrder=NewGateOrder;
    Swap.RequestedPassSide=NewPassSide;
    Swap.RequestedDirection=NewDirection;
    Swap.SourceId=GateGuid;
    Swap.OtherId=Other->GateGuid;
    Swap.PreviousPassSide=GetPolePassSide();
    Swap.PreviousDirection=GetRaceDirection();
    Error=FString::Printf(TEXT("Checkpoint %d on course %d is already in use. Confirm the swap or choose a free number."),NewGateOrder,NewCourseId);
    return false;
}

bool AFactoryFPVRaceGate::ConfirmEditorOrderSwap(const FEditorOrderSwap& Proposal,FString& Error) {
    auto* Other=Proposal.OtherGate.Get();
    if (!IsInGameThread() || !IsLiveRaceGate() || !bNumberAssigned || !GateGuid.IsValid() || GateGuid!=Proposal.SourceId ||
        CourseId!=Proposal.CourseId || GateOrder!=Proposal.FromOrder || GetPolePassSide()!=Proposal.PreviousPassSide ||
        GetRaceDirection()!=Proposal.PreviousDirection || Proposal.FromOrder==Proposal.ToOrder || !Other || Other==this ||
        Other->GetWorld()!=GetWorld() || !Other->IsLiveRaceGate() || !Other->bNumberAssigned || !Other->GateGuid.IsValid() ||
        Other->GateGuid!=Proposal.OtherId || Other->GateGuid==GateGuid || Other->CourseId!=CourseId || Other->GateOrder!=Proposal.ToOrder) {
        Error=TEXT("The checkpoint assignments changed. Review the current course before swapping.");
        return false;
    }
    if (!ValidateConfiguration(CourseId,Proposal.ToOrder,Proposal.RequestedPassSide,Proposal.RequestedDirection,Error) ||
        !Other->ValidateConfiguration(CourseId,Proposal.FromOrder,Other->GetPolePassSide(),Other->GetRaceDirection(),Error)) return false;
    const auto Occupants=FindEditorSlotOccupants(CourseId,Proposal.ToOrder);
    if (Occupants.SourceCount!=0 || Occupants.DestinationCount!=1 || Occupants.Destination!=Other) {
        Error=TEXT("The checkpoint numbers are no longer unique. Review the current course before swapping.");
        return false;
    }
    // Publish either gate only after both numbers have changed. Keep existing
    // records on their physical start gates under their exact layout keys.
    Other->GateOrder=Proposal.FromOrder;
    ApplyConfiguration(CourseId,Proposal.ToOrder,Proposal.RequestedPassSide,Proposal.RequestedDirection);
    Other->UpdatePresentation();
    Other->ForceNetUpdate();
    return true;
}

bool AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(UWorld* World,int32 CourseId,
    FEditorNumberingCompaction& Plan,FString& Error) {
    Plan={};
    Error.Reset();
    if (!IsInGameThread() || !IsValid(World) || !World->IsGameWorld() || World->GetNetMode()!=NM_Standalone) {
        Error=TEXT("Checkpoint numbering repair is available in local single-player.");
        return false;
    }
    if (CourseId<1 || CourseId>MaximumCourseId) {
        Error=TEXT("Choose a loaded course to repair its numbering.");
        return false;
    }

    FEditorNumberingCompaction Proposed;
    Proposed.CourseId=CourseId;
    TSet<FGuid> GateIds;
    TSet<int32> Orders;
    for (TActorIterator<AFactoryFPVRaceGate> It(World);It;++It) {
        auto* Gate=*It;
        if (Gate->CourseId!=CourseId || !Gate->IsLiveRaceGate()) continue;
        if (!Gate->HasAuthority() || !Gate->bNumberAssigned || !Gate->GateGuid.IsValid() ||
            Gate->GateOrder<1 || Gate->GateOrder>MaximumGateOrder || GateIds.Contains(Gate->GateGuid)) {
            Error=TEXT("Every loaded checkpoint needs a valid identity and number before this course can be repaired.");
            return false;
        }
        if (Orders.Contains(Gate->GateOrder)) {
            Error=FString::Printf(TEXT("Checkpoint %d is duplicated. Assign unique numbers before closing gaps."),Gate->GateOrder);
            return false;
        }
        GateIds.Add(Gate->GateGuid);
        Orders.Add(Gate->GateOrder);
        Proposed.Entries.Add({Gate,Gate->GateGuid,Gate->GateOrder});
    }
    if (!Orders.Contains(1) || Proposed.Entries.Num()<2) {
        Error=TEXT("Load checkpoint 1 and at least one other checkpoint before closing gaps.");
        return false;
    }
    Proposed.Entries.Sort([](const auto& A,const auto& B) { return A.OldOrder<B.OldOrder; });
    if (Proposed.Entries.Last().OldOrder==Proposed.Entries.Num()) {
        Error=TEXT("The loaded checkpoint numbers are already consecutive.");
        return false;
    }
    Plan=MoveTemp(Proposed);
    return true;
}

bool AFactoryFPVRaceGate::ConfirmEditorNumberingCompaction(UWorld* World,
    const FEditorNumberingCompaction& Plan,FString& Error) {
    FEditorNumberingCompaction Current;
    if (!ProposeEditorNumberingCompaction(World,Plan.CourseId,Current,Error) || Current.Entries.Num()!=Plan.Entries.Num()) {
        Error=TEXT("The loaded checkpoint assignments changed. Review the course before closing gaps.");
        return false;
    }
    for (int32 Index=0;Index<Current.Entries.Num();++Index) {
        const auto& Entry=Current.Entries[Index];
        const auto& Expected=Plan.Entries[Index];
        auto* Gate=Entry.Gate.Get();
        if (!Gate || Entry.Gate!=Expected.Gate || Entry.GateId!=Expected.GateId || Entry.OldOrder!=Expected.OldOrder) {
            Error=TEXT("The loaded checkpoint assignments changed. Review the course before closing gaps.");
            return false;
        }
        if (!Gate->ValidateConfiguration(Plan.CourseId,Index+1,Gate->GetPolePassSide(),Gate->GetRaceDirection(),Error)) return false;
    }

    // The validated order lets each move fill a free number. No callbacks,
    // save migration, or record reset are needed to change the assignments.
    for (int32 Index=0;Index<Current.Entries.Num();++Index) {
        const auto& Entry=Current.Entries[Index];
        if (Entry.OldOrder==Index+1) continue;
        auto* Gate=Entry.Gate.Get();
        Gate->ApplyConfiguration(Plan.CourseId,Index+1,Gate->GetPolePassSide(),Gate->GetRaceDirection());
    }
    return true;
}

bool AFactoryFPVRaceGate::IsLiveRaceGate() const {
    return !IsTemplate() && GetWorld() && GetWorld()->IsGameWorld() && !bEndingPlay &&
        !IsActorBeingDestroyed() && !GetIsDismantled() && !IsBuildableInsideBlueprintDesigner();
}

bool AFactoryFPVRaceGate::IsGatePowered() const {
    return IsLiveRaceGate() && PowerInfo &&
        PowerInfo->IsConnected() && PowerInfo->HasPower() && !PowerInfo->IsFuseTriggered();
}

EFactoryFPVRaceGateDirection AFactoryFPVRaceGate::GetRaceDirection() const {
    if (!bVerticalRaceGate) return EFactoryFPVRaceGateDirection::Forward;
    return RaceDirection==EFactoryFPVRaceGateDirection::Descend ? RaceDirection : EFactoryFPVRaceGateDirection::Ascend;
}

FVector2D AFactoryFPVRaceGate::GetApertureHalfExtents() const {
    if (bVerticalRaceGate) {
        const double Half=bFloorRaceGate ? FloorGateOpeningHalfCm : VerticalGateOpeningHalfCm;
        return FVector2D(Half,Half);
    }
    return bPoleCheckpoint ? FVector2D(800,180) : bFullSizeGate ? FVector2D(380,180) : FVector2D(150,150);
}

FTransform AFactoryFPVRaceGate::GetLocalApertureTransform() const {
    if (!bVerticalRaceGate) return FTransform(FQuat::Identity,FVector(0,0,20+GetApertureHalfExtents().Y));
    const double Sign=GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend ? -1. : 1.;
    const FQuat Rotation=FRotationMatrix::MakeFromXY(FVector(0,0,Sign),FVector::RightVector).ToQuat();
    const double CenterZ=bFloorRaceGate ? -AFactoryFPVRaceGateFloor::TileDepthCm*.5 : VerticalGateHeightCm;
    return FTransform(Rotation,FVector(0,0,CenterZ));
}

FTransform AFactoryFPVRaceGate::GetApertureTransform() const {
    return GetApertureTransform(GetActorTransform());
}

FTransform AFactoryFPVRaceGate::GetApertureTransform(const FTransform& Placement) const {
    const FTransform Local=GetLocalApertureTransform();
    if (!bVerticalRaceGate) return Local*Placement;
    // Aperture X maps to actor Z and aperture Z maps to actor X. Permute the
    // scales so crossing dimensions follow the actual frame under actor scale.
    const FVector Scale=Placement.GetScale3D();
    return FTransform(Placement.GetRotation()*Local.GetRotation(),Placement.TransformPosition(Local.GetLocation()),FVector(Scale.Z,Scale.Y,Scale.X));
}
FVector AFactoryFPVRaceGate::GetPassageAimPoint() const {
    FVector Local=GetLocalApertureTransform().GetLocation();
    if (bPoleCheckpoint) Local.Y=GetPolePassSide()*(GetPoleRadiusCm()+GetApertureHalfExtents().X)*.5;
    return GetActorTransform().TransformPosition(Local);
}
AFactoryFPVRaceGate::FVerticalClearance AFactoryFPVRaceGate::ProbeVerticalClearance(double DroneRadiusCm) const {
    FVerticalClearance Result;
    const auto* World=GetWorld();
    if (!bVerticalRaceGate || !World || !IsLiveRaceGate() || !FMath::IsFinite(DroneRadiusCm) ||
        DroneRadiusCm<=0. || DroneRadiusCm>TNumericLimits<float>::Max()) return Result;

    const FTransform Aperture=GetApertureTransform();
    const FVector Center=Aperture.GetLocation();
    const FVector Normal=Aperture.GetUnitAxis(EAxis::X);
    const FVector Above=Normal.Z<0. ? -Normal : Normal;
    if (Center.ContainsNaN() || Above.ContainsNaN() || Above.IsNearlyZero()) return Result;

    FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVVerticalClearance),false,this);
    if (const auto* PC=World->GetFirstPlayerController()) {
        if (const auto* Pioneer=Cast<AFGCharacterPlayer>(PC->GetPawn());Pioneer && Pioneer->IsAliveAndWell()) Query.AddIgnoredActor(Pioneer);
    }
    const auto Shape=FCollisionShape::MakeSphere(static_cast<float>(DroneRadiusCm));
    Result.bOpeningBlocked=World->OverlapBlockingTestByChannel(Center,FQuat::Identity,ECC_WorldDynamic,Shape,Query);
    if (Result.bOpeningBlocked) return Result;

    FHitResult Hit;
    const FVector Reach=Above*VerticalApproachProbeLengthCm;
    Result.bAboveBlocked=World->SweepSingleByChannel(Hit,Center,Center+Reach,FQuat::Identity,ECC_WorldDynamic,Shape,Query);
    Result.bBelowBlocked=World->SweepSingleByChannel(Hit,Center,Center-Reach,FQuat::Identity,ECC_WorldDynamic,Shape,Query);
    return Result;
}
FString AFactoryFPVRaceGate::GetCheckpointInstructions() const {
    if (bVerticalRaceGate) return GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend ? TEXT("DESCEND THROUGH THE OPENING FROM ABOVE") : TEXT("ASCEND THROUGH THE OPENING FROM BELOW");
    return bPoleCheckpoint ? (GetPolePassSide()>0 ? TEXT("PASS RIGHT OF POLE - FOLLOW FORWARD ARROW") : TEXT("PASS LEFT OF POLE - FOLLOW FORWARD ARROW")) : TEXT("THROUGH OPENING - FOLLOW FORWARD ARROW");
}
void AFactoryFPVRaceGate::SetNextCheckpointHighlighted(bool Highlighted) {
    if (bNextCheckpoint==Highlighted) return;bNextCheckpoint=Highlighted;UpdatePresentation();
}

AFactoryFPVRaceGate::FLapRecordView AFactoryFPVRaceGate::FindBestLap(const FString& LayoutSignature) const {
    FLapRecordView Best;
    if (GateOrder!=1 || LayoutSignature.IsEmpty()) return Best;
    if (LayoutSignature==BestLapLayout && IsValidLapTime(BestLapSeconds)) Best={BestLapSeconds,BestLapSplits};
    for (const auto& Record:BestLapRecords) {
        if (Record.LayoutSignature!=LayoutSignature) continue;
        if (IsPreferredLapRecord(Record.Seconds,Record.Splits,Best.Seconds,Best.Splits) ||
            (Record.Seconds==Best.Seconds && Record.Splits==Best.Splits && Record.Ghost.IsValid(Record.Seconds) &&
                (!Best.Ghost || !Best.Ghost->IsValid(Best.Seconds)))) {
            Best={Record.Seconds,Record.Splits,&Record.Ghost};
        }
    }
    return Best;
}

double AFactoryFPVRaceGate::GetBestLapSeconds(const FString& LayoutSignature) const {
    return FindBestLap(LayoutSignature).Seconds;
}

TConstArrayView<double> AFactoryFPVRaceGate::GetBestLapSplits(const FString& LayoutSignature,int32 ExpectedSplitCount) const {
    const auto Best=FindBestLap(LayoutSignature);
    if (Best.Splits.Num()!=ExpectedSplitCount || !IsValidLapSplitSequence(Best.Splits,Best.Seconds)) return {};
    return Best.Splits;
}

const FFactoryFPVRaceGhost* AFactoryFPVRaceGate::GetBestLapGhost(const FString& LayoutSignature) const {
    const auto Best=FindBestLap(LayoutSignature);
    return Best.Ghost && Best.Ghost->IsValid(Best.Seconds) ? Best.Ghost : nullptr;
}

bool AFactoryFPVRaceGate::RecordLap(double Seconds,const FString& LayoutSignature) {
    return RecordLap(Seconds,LayoutSignature,{});
}

bool AFactoryFPVRaceGate::RecordLap(double Seconds,const FString& LayoutSignature,TConstArrayView<double> Splits,
    const FFactoryFPVRaceGhost* Ghost) {
    if (!HasAuthority() || !IsLiveRaceGate() || GateOrder!=1 || !IsValidLapTime(Seconds) || LayoutSignature.IsEmpty()) return false;
    const double Previous=GetBestLapSeconds(LayoutSignature);
    if (Previous>0 && Previous<=Seconds) return false;
    if (!Splits.IsEmpty() && !IsValidLapSplitSequence(Splits,Seconds)) return false;
    // Copy before normalizing in case the caller supplied a view into a record.
    FFactoryFPVRaceLapRecord NewRecord{LayoutSignature,Seconds,TArray<double>(Splits),{}};
    if (Ghost && Ghost->IsValid(Seconds)) NewRecord.Ghost=*Ghost;
    NormalizeBestLapRecords();
    BestLapRecords.RemoveAll([&](const auto& Record) { return Record.LayoutSignature==NewRecord.LayoutSignature; });
    BestLapRecords.Add(MoveTemp(NewRecord));
    if (BestLapRecords.Num()>MaximumBestLapLayouts)
        BestLapRecords.RemoveAt(0,BestLapRecords.Num()-MaximumBestLapLayouts,EAllowShrinking::No);
    return true;
}

void AFactoryFPVRaceGate::NormalizeBestLapRecords() {
    auto Saved=MoveTemp(BestLapRecords);
    if (!BestLapLayout.IsEmpty() && IsValidLapTime(BestLapSeconds)) {
        Saved.Insert(FFactoryFPVRaceLapRecord{MoveTemp(BestLapLayout),BestLapSeconds,MoveTemp(BestLapSplits),{}},0);
    }
    ClearLegacyBestLap();
    for (auto& Record:Saved) {
        if (Record.LayoutSignature.IsEmpty() || !IsValidLapTime(Record.Seconds)) continue;
        if (!IsValidLapSplitSequence(Record.Splits,Record.Seconds)) Record.Splits.Reset();
        if (!Record.Ghost.IsValid(Record.Seconds)) Record.Ghost.Positions.Reset();
        const int32 Index=BestLapRecords.IndexOfByPredicate([&](const auto& Entry) { return Entry.LayoutSignature==Record.LayoutSignature; });
        if (Index!=INDEX_NONE) {
            auto& Earlier=BestLapRecords[Index];
            const bool bPreferGhost=Record.Seconds==Earlier.Seconds && Record.Splits==Earlier.Splits &&
                !Record.Ghost.Positions.IsEmpty() && Earlier.Ghost.Positions.IsEmpty();
            if (!bPreferGhost && !IsPreferredLapRecord(Record.Seconds,Record.Splits,Earlier.Seconds,Earlier.Splits)) Record=MoveTemp(Earlier);
            BestLapRecords.RemoveAt(Index,1,EAllowShrinking::No);
        }
        BestLapRecords.Add(MoveTemp(Record));
    }
}

void AFactoryFPVRaceGate::ResetBestLap(const FString& LayoutSignature) {
    if (!HasAuthority() || !IsLiveRaceGate() || GateOrder!=1 || LayoutSignature.IsEmpty()) return;
    BestLapRecords.RemoveAll([&](const auto& Record) { return Record.LayoutSignature==LayoutSignature; });
    if (BestLapLayout==LayoutSignature) ClearLegacyBestLap();
}

void AFactoryFPVRaceGate::ClearLegacyBestLap() {
    BestLapSeconds=0;
    BestLapLayout.Reset();
    BestLapSplits.Reset();
}

void AFactoryFPVRaceGate::ClearBestLapRecords() {
    BestLapRecords.Reset();
    ClearLegacyBestLap();
}

void AFactoryFPVRaceGate::CreateLightMaterials() {
    if (!LightMaterials.IsEmpty()) return;
    for (UStaticMeshComponent* Strip:LightStrips) LightMaterials.Add(Strip->CreateDynamicMaterialInstance(0));
    bPresentationInitialized=false;
}

void AFactoryFPVRaceGate::UpdatePresentation() {
    const bool Powered=IsGatePowered();
    const bool Target=Powered && bNextCheckpoint;
    const auto Direction=GetRaceDirection();
    if (bPresentationInitialized && bLastPowered==Powered && LastPresentedCourse==CourseId && LastPresentedOrder==GateOrder &&
        bLastNextCheckpoint==Target && LastPresentedSide==GetPolePassSide() && LastPresentedDirection==Direction) return;
    bPresentationInitialized=true;
    bLastPowered=Powered;
    LastPresentedCourse=CourseId;
    LastPresentedOrder=GateOrder;
    bLastNextCheckpoint=Target;LastPresentedSide=GetPolePassSide();
    LastPresentedDirection=Direction;
    const FLinearColor Color=GateOrder==1 ? FLinearColor(1,.22f,.025f) : FLinearColor(.025f,.65f,1);
    const FLinearColor Pink(1.f,.035f,.42f);
    for (UPointLightComponent* Light:Lights) {
        Light->SetLightColor(Color);
        Light->SetVisibility(Powered);
    }
    // Side 0 is the front of an upright frame or the bottom of a flat frame.
    // Descending makes the top side the entry. Keep ambient lights ordinary.
    const int32 EntrySide=Direction==EFactoryFPVRaceGateDirection::Descend ? 1 : 0;
    for (int32 Index=0;Index<LightMaterials.Num();++Index) if (UMaterialInstanceDynamic* Material=LightMaterials[Index]) {
        const FLinearColor StripColor=Target && Index/4==EntrySide ? Pink : Color;
        Material->SetVectorParameterValue(TEXT("LightColor"),Powered ? StripColor : FLinearColor(.035f,.045f,.055f));
        Material->SetScalarParameterValue(TEXT("LightStrength"),Powered ? 12.f : 0.f);
    }
    for (int32 Side=0;Side<Labels.Num();++Side) {
        const FString Title=FString::Printf(TEXT("%s %d"),bPoleCheckpoint ? TEXT("POLE") : TEXT("GATE"),GateOrder);
        const TCHAR* Detail=TEXT("WRONG WAY");
        if (Side==EntrySide) {
            if (bVerticalRaceGate) Detail=Direction==EFactoryFPVRaceGateDirection::Descend ? TEXT("DESCEND") : TEXT("ASCEND");
            else if (bPoleCheckpoint) Detail=GetPolePassSide()>0 ? TEXT("PASS RIGHT") : TEXT("PASS LEFT");
            else Detail=GateOrder==1 ? TEXT("START / FINISH") : TEXT("FOLLOW ARROW");
        }
        Labels[Side]->SetText(FText::FromString(FString::Printf(TEXT("%s  /  C%d<br>%s"),*Title,CourseId,Detail)));
        Labels[Side]->SetTextRenderColor(Powered ? Color.ToFColor(true) : FColor(115,125,135));
    }
}

bool AFactoryFPVRaceGate::IsUseable_Implementation() const {
    return !bEndingPlay && !GetIsDismantled() && GetNetMode()==NM_Standalone;
}

void AFactoryFPVRaceGate::OnUse_Implementation(AFGCharacterPlayer* Character,const FUseState&) {
    if (!IsUseable_Implementation() || !Character) return;
    auto* PC=Cast<APlayerController>(Character->GetController());
    if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer()) return;
    if (auto* Hangar=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>()) Hangar->OpenRaceGatePanel(this);
}

FText AFactoryFPVRaceGate::GetLookAtDecription_Implementation(AFGCharacterPlayer* Character,const FUseState&) const {
    auto* PC=Character ? Cast<AFGPlayerController>(Character->GetController()) : nullptr;
    const FText Key=PC ? PC->GetKeyNameForUseAction() : FText::FromString(TEXT("E"));
    return FText::Format(NSLOCTEXT("FactoryFPV","ConfigureRaceCheckpoint","[{0}] Configure FPV checkpoint  /  Course {1}, checkpoint {2}  /  {3}"),
        Key,FText::AsNumber(CourseId),FText::AsNumber(GateOrder),IsBuildableInsideBlueprintDesigner() ?
        NSLOCTEXT("FactoryFPV","GateBlueprintPreview","BLUEPRINT PREVIEW / NOT COUNTING") : IsGatePowered() ?
        NSLOCTEXT("FactoryFPV","GatePowered","POWERED") : NSLOCTEXT("FactoryFPV","GateNoPower","NO POWER"));
}
