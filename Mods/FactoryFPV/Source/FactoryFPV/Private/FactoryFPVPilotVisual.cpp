#include "FactoryFPVPilotVisual.h"
#include "FGCharacterBase.h"
#include "GameFramework/Character.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "UObject/ConstructorHelpers.h"

AFactoryFPVPilotVisual::AFactoryFPVPilotVisual() {
    bReplicates=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PilotLocation")));
    Body=CreateDefaultSubobject<UPoseableMeshComponent>(TEXT("RemotePilot"));
    Body->SetupAttachment(RootComponent);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetGenerateOverlapEvents(false);
    Body->SetCastShadow(true);
    Radio=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Transmitter"));
    Radio->SetupAttachment(Body);
    Goggles=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Goggles"));
    Goggles->SetupAttachment(Body);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> RadioAsset(TEXT("/FactoryFPV/Pilot/SM_Radio_CM.SM_Radio_CM"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> GogglesAsset(TEXT("/FactoryFPV/Pilot/SM_Goggles_CM.SM_Goggles_CM"));
    Radio->SetStaticMesh(RadioAsset.Object);
    Goggles->SetStaticMesh(GogglesAsset.Object);
    for (auto* Part:{Radio.Get(),Goggles.Get()}) {
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCastShadow(true);
    }
}

// Solve a two-bone chain in the pioneer's skeletal component space. The game
// skeleton faces +Y, with left along +X. Twist/finger children follow naturally.
bool AFactoryFPVPilotVisual::PoseArm(bool bLeft) {
    const FName Upper=bLeft ? TEXT("upperarm_l") : TEXT("upperarm_r");
    const FName Lower=bLeft ? TEXT("lowerarm_l") : TEXT("lowerarm_r");
    const FName Hand=bLeft ? TEXT("hand_l") : TEXT("hand_r");
    if (Body->GetBoneIndex(Upper)==INDEX_NONE || Body->GetBoneIndex(Lower)==INDEX_NONE || Body->GetBoneIndex(Hand)==INDEX_NONE) return false;
    const double Side=bLeft ? 1 : -1;
    auto At=[&](FName Bone) { return Body->GetBoneLocationByName(Bone,EBoneSpaces::ComponentSpace); };
    FVector Shoulder=At(Upper), Elbow=At(Lower), Wrist=At(Hand);
    const double A=(Elbow-Shoulder).Length(),B=(Wrist-Elbow).Length();
    const FTransform Transmitter=Radio->GetRelativeTransform();
    const FVector Target=Transmitter.TransformPosition(FVector(-Side*11.8,-2,-6));
    const FVector Direction=(Target-Shoulder).GetSafeNormal();
    const double Distance=FMath::Clamp((Target-Shoulder).Length(),FMath::Abs(A-B)+.1,A+B-.1);
    const double Along=(A*A-B*B+Distance*Distance)/(2*Distance);
    const double Height=FMath::Sqrt(FMath::Max(0.,A*A-Along*Along));
    const FVector Pole=FVector(Side*38,-3,115)-Shoulder;
    const FVector Bend=(Pole-Direction*FVector::DotProduct(Pole,Direction)).GetSafeNormal();
    const FVector NewElbow=Shoulder+Direction*Along+Bend*Height;
    auto Aim=[&](FName Bone,FVector OldDirection,FVector NewDirection) {
        auto Transform=Body->GetBoneTransformByName(Bone,EBoneSpaces::ComponentSpace);
        Transform.SetRotation((FQuat::FindBetweenNormals(OldDirection.GetSafeNormal(),NewDirection.GetSafeNormal())*Transform.GetRotation()).GetNormalized());
        Body->SetBoneTransformByName(Bone,Transform,EBoneSpaces::ComponentSpace);
        Body->RefreshBoneTransforms();
    };
    Aim(Upper,Elbow-Shoulder,NewElbow-Shoulder);
    Elbow=At(Lower);Wrist=At(Hand);
    Aim(Lower,Wrist-Elbow,Target-Elbow);
    // Align the palm with the actual tilted case, rather than a world-space
    // direction. The wrist is below each side grip, with thumbs towards its face.
    const FName Middle=bLeft ? TEXT("middle_01_l") : TEXT("middle_01_r");
    const FName Thumb=bLeft ? TEXT("thumb_01_l") : TEXT("thumb_01_r");
    const FName Pinky=bLeft ? TEXT("pinky_01_l") : TEXT("pinky_01_r");
    if (Body->GetBoneIndex(Middle)!=INDEX_NONE && Body->GetBoneIndex(Thumb)!=INDEX_NONE && Body->GetBoneIndex(Pinky)!=INDEX_NONE) {
        auto Transform=Body->GetBoneTransformByName(Hand,EBoneSpaces::ComponentSpace);
        const FQuat OldBasis=FRotationMatrix::MakeFromXY(At(Middle)-At(Hand),At(Thumb)-At(Pinky)).ToQuat();
        const FQuat NewBasis=FRotationMatrix::MakeFromXY(Transmitter.TransformVectorNoScale(FVector::UpVector),
            Transmitter.TransformVectorNoScale(FVector::RightVector)).ToQuat();
        Transform.SetRotation((NewBasis*OldBasis.Inverse()*Transform.GetRotation()).GetNormalized());
        Body->SetBoneTransformByName(Hand,Transform,EBoneSpaces::ComponentSpace);
        Body->RefreshBoneTransforms();
    }
    // Explicit finger articulation: the idle snapshot leaves a flat open hand.
    // Wrap the four fingers over the rear side grips without scaling any bones.
    for (const TCHAR* Finger:{TEXT("index"),TEXT("middle"),TEXT("ring"),TEXT("pinky")}) {
        const FName First(*FString::Printf(TEXT("%s_01_%s"),Finger,bLeft ? TEXT("l") : TEXT("r")));
        const FName Second(*FString::Printf(TEXT("%s_02_%s"),Finger,bLeft ? TEXT("l") : TEXT("r")));
        const FName Third(*FString::Printf(TEXT("%s_03_%s"),Finger,bLeft ? TEXT("l") : TEXT("r")));
        if (Body->GetBoneIndex(First)==INDEX_NONE || Body->GetBoneIndex(Second)==INDEX_NONE || Body->GetBoneIndex(Third)==INDEX_NONE) continue;
        Aim(First,At(Second)-At(First),Transmitter.TransformVectorNoScale(FVector(Side*.65,-.2,.75)));
        Aim(Second,At(Third)-At(Second),Transmitter.TransformVectorNoScale(FVector(Side*.95,.2,-.2)));
        Aim(Third,At(Third)-At(Second),Transmitter.TransformVectorNoScale(FVector(Side*.6,.5,-.6)));
    }
    const FName ThumbSecond=bLeft ? TEXT("thumb_02_l") : TEXT("thumb_02_r");
    const FName ThumbThird=bLeft ? TEXT("thumb_03_l") : TEXT("thumb_03_r");
    if (Body->GetBoneIndex(Thumb)!=INDEX_NONE && Body->GetBoneIndex(ThumbSecond)!=INDEX_NONE && Body->GetBoneIndex(ThumbThird)!=INDEX_NONE) {
        const FVector Start=At(Thumb),Joint=At(ThumbSecond),End=At(ThumbThird);
        // The distal phalanx sits just behind the stick; its glove tip extends
        // forward to the gimbal. Use a two-bone solve to preserve hand proportions.
        const FVector ThumbTarget=Transmitter.TransformPosition(FVector(-Side*6.8,3,-.3));
        const FVector ThumbDirection=(ThumbTarget-Start).GetSafeNormal();
        const double LengthA=(Joint-Start).Length(),LengthB=(End-Joint).Length();
        const double Reach=FMath::Clamp((ThumbTarget-Start).Length(),FMath::Abs(LengthA-LengthB)+.01,LengthA+LengthB-.01);
        const double Offset=(LengthA*LengthA-LengthB*LengthB+Reach*Reach)/(2*Reach);
        const FVector ThumbPole=Transmitter.TransformVectorNoScale(FVector(0,1,1));
        const FVector ThumbBend=(ThumbPole-ThumbDirection*FVector::DotProduct(ThumbPole,ThumbDirection)).GetSafeNormal();
        const FVector ThumbElbow=Start+ThumbDirection*Offset+ThumbBend*FMath::Sqrt(FMath::Max(0.,LengthA*LengthA-Offset*Offset));
        Aim(Thumb,Joint-Start,ThumbElbow-Start);
        Aim(ThumbSecond,At(ThumbThird)-At(ThumbSecond),ThumbTarget-At(ThumbSecond));
        Aim(ThumbThird,At(ThumbThird)-At(ThumbSecond),Transmitter.TransformVectorNoScale(FVector::RightVector));
    }
    return At(Hand).Equals(Target,.5);
}

void AFactoryFPVPilotVisual::HideOriginal(UPrimitiveComponent* Component) {
    if (!Component) return;
    HiddenParts.Add({Component,Component->bHiddenInGame!=0,Component->bCastHiddenShadow!=0});
    Component->SetHiddenInGame(true);
    Component->SetCastHiddenShadow(false);
}

bool AFactoryFPVPilotVisual::Begin(ACharacter* Character) {
    if (!Character || !HiddenParts.IsEmpty() || bReleased) return false;
    auto* FGCharacter=Cast<AFGCharacterBase>(Character);
    auto* Source=FGCharacter ? FGCharacter->GetMesh3P() : Character->GetMesh();
    if (!Source || !Source->GetSkeletalMeshAsset()) return false;
    auto* Idle=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/FactoryGame/Character/Player/Animation/ThirdPerson/Idle_01.Idle_01"));
    if (!Idle || !Radio->GetStaticMesh() || !Goggles->GetStaticMesh()) return false;
    SetActorTransform(Character->GetActorTransform());
    AttachToComponent(Character->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
    Body->SetWorldTransform(Source->GetComponentTransform());
    Body->SetSkinnedAssetAndUpdate(Source->GetSkeletalMeshAsset());
    for (int32 Index=0;Index<Source->GetNumMaterials();++Index) Body->SetMaterial(Index,Source->GetMaterial(Index));
    // Evaluate an independent idle snapshot. Never change the live anim instance.
    auto* Sampler=NewObject<USkeletalMeshComponent>(this);
    Sampler->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
    Sampler->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sampler->SetVisibility(false);
    Sampler->RegisterComponent();
    Sampler->PlayAnimation(Idle,false);
    Sampler->TickAnimation(0,false);
    Sampler->RefreshBoneTransforms();
    Body->CopyPoseFromSkeletalComponent(Sampler);
    Sampler->DestroyComponent();
    Body->RefreshBoneTransforms();
    Radio->SetRelativeLocation(FVector(0,31,125));
    Radio->SetRelativeRotation(FQuat(FVector::ForwardVector,FMath::DegreesToRadians(-35.f))*FQuat(FVector::UpVector,PI));
    if (!PoseArm(true) || !PoseArm(false)) {
        UE_LOG(LogTemp,Warning,TEXT("FactoryFPV: pilot pose failed; bones=%d left=%s right=%s"),Body->GetNumBones(),
            *Body->GetBoneLocationByName(TEXT("hand_l"),EBoneSpaces::ComponentSpace).ToString(),
            *Body->GetBoneLocationByName(TEXT("hand_r"),EBoneSpaces::ComponentSpace).ToString());
        return false;
    }
    Goggles->SetRelativeLocation(Body->GetBoneLocationByName(TEXT("head"),EBoneSpaces::ComponentSpace)+FVector(0,7,8)); // RC10: 1.5 cm back from RC9; skeletal +Y is forward.
    Goggles->SetRelativeRotation(FRotator::ZeroRotator);
    // Copy third-person helmet/hat meshes using their actual socket and materials.
    TArray<USceneComponent*> MeshChildren;
    Source->GetChildrenComponents(true,MeshChildren);
    for (auto* Child:MeshChildren) {
        auto* Part=Cast<UStaticMeshComponent>(Child);
        if (!Part || !Part->GetStaticMesh() || Part->bHiddenInGame || !Part->IsVisible()) continue;
        auto* Copy=NewObject<UStaticMeshComponent>(this);
        Copy->SetStaticMesh(Part->GetStaticMesh());
        for(int32 Index=0;Index<Part->GetNumMaterials();++Index) Copy->SetMaterial(Index,Part->GetMaterial(Index));
        Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Copy->SetGenerateOverlapEvents(false);
        Copy->RegisterComponent();
        // Preserve nested attachment offsets, using the closest bone/socket parent.
        auto* Branch=Part->GetAttachParent();
        FName Socket=Part->GetAttachSocketName();
        while (Branch && Branch!=Source) {
            Socket=Branch->GetAttachSocketName();
            Branch=Branch->GetAttachParent();
        }
        Copy->AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform,Socket);
        Copy->SetRelativeTransform(Part->GetComponentTransform().GetRelativeTransform(Source->GetSocketTransform(Socket)));
        Accessories.Add(Copy);
    }
    TInlineComponentArray<UPrimitiveComponent*> Parts(Character);
    for (auto* Part:Parts) HideOriginal(Part);
    TArray<AActor*> Attached;
    Character->GetAttachedActors(Attached,true,true);
    for (auto* Actor:Attached) {
        if (Actor==this) continue;
        TInlineComponentArray<UPrimitiveComponent*> EquipmentParts(Actor);
        for(auto* Part:EquipmentParts) HideOriginal(Part);
    }
    UE_LOG(LogTemp,Log,TEXT("FactoryFPV: pilot presentation ready; source=%s bones=%d accessories=%d"),
        *GetNameSafe(Source->GetSkeletalMeshAsset()),Body->GetNumBones(),Accessories.Num());
    return true;
}

void AFactoryFPVPilotVisual::Release() {
    if (bReleased) return;
    bReleased=true;
    for (const auto& Part:HiddenParts) if (auto* Component=Part.Component.Get()) {
        if (Component->bHiddenInGame) Component->SetHiddenInGame(Part.bHidden);
        if (!Component->bCastHiddenShadow) Component->SetCastHiddenShadow(Part.bHiddenShadow);
    }
    HiddenParts.Reset();
    SetActorHiddenInGame(true);
}
void AFactoryFPVPilotVisual::EndPlay(const EEndPlayReason::Type Reason) {
    Release();
    Super::EndPlay(Reason);
}
void AFactoryFPVPilotVisual::Destroyed() {
    Release();
    Super::Destroyed();
}







