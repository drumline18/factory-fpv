#include "FactoryFPVPilotVisual.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVPilotVisualTest,"FactoryFPV.Session.PilotPresentation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVPilotVisualTest::RunTest(const FString&) {
 UWorld::InitializationValues Values;
 Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Character=World->SpawnActor<ACharacter>();
 auto* Mesh=Character->GetMesh();
 // The public SDK body is a geometry placeholder with dummy bones. Exercise
 // the real shipped skeleton hierarchy on an in-memory duplicate; never save it.
 auto* Skeleton=LoadObject<USkeleton>(nullptr,TEXT("/Game/FactoryGame/Character/Player/Mesh/Character_Skeleton.Character_Skeleton"));
 auto* Asset=DuplicateObject<USkeletalMesh>(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/FactoryGame/Character/Player/Mesh/SK_Pioneer_01.SK_Pioneer_01")),GetTransientPackage());
 Asset->SetRefSkeleton(Skeleton->GetReferenceSkeleton());
 Asset->SetSkeleton(Skeleton);
 Asset->CalculateInvRefMatrices();
 Mesh->SetSkeletalMeshAsset(Asset);
 Mesh->SetRelativeLocation(FVector(0,0,-88));
 Mesh->SetHiddenInGame(false); Mesh->SetCastHiddenShadow(true);
 auto* Equipment=World->SpawnActor<AActor>();
 auto* Held=NewObject<UStaticMeshComponent>(Equipment);
 Equipment->SetRootComponent(Held); Held->RegisterComponent();
 Equipment->AttachToComponent(Character->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
 Held->SetHiddenInGame(false);
 const auto Mode=Mesh->GetAnimationMode();
 const auto Collision=Character->GetCapsuleComponent()->GetCollisionEnabled();
 auto* Visual=World->SpawnActor<AFactoryFPVPilotVisual>();
 const bool Started=Visual->Begin(Character);
 TestTrue(TEXT("Presentation can pose the actual game skeleton"),Started);
 if (Started) {
  TestTrue(TEXT("Left palm reaches transmitter grip"),Visual->Body->GetBoneLocationByName(TEXT("hand_l"),EBoneSpaces::ComponentSpace).Equals(Visual->Radio->GetRelativeTransform().TransformPosition(FVector(-11.8,-2,-6)),.5));
  TestTrue(TEXT("Right palm reaches transmitter grip"),Visual->Body->GetBoneLocationByName(TEXT("hand_r"),EBoneSpaces::ComponentSpace).Equals(Visual->Radio->GetRelativeTransform().TransformPosition(FVector(11.8,-2,-6)),.5));
  TestTrue(TEXT("Radio imported in centimetres"),(Visual->Radio->GetStaticMesh()->GetBounds().BoxExtent.X*Visual->Radio->GetRelativeScale3D().X)>10 && (Visual->Radio->GetStaticMesh()->GetBounds().BoxExtent.X*Visual->Radio->GetRelativeScale3D().X)<13);
  TestTrue(TEXT("Goggles imported in centimetres"),(Visual->Goggles->GetStaticMesh()->GetBounds().BoxExtent.X*Visual->Goggles->GetRelativeScale3D().X)>10 && (Visual->Goggles->GetStaticMesh()->GetBounds().BoxExtent.X*Visual->Goggles->GetRelativeScale3D().X)<14);
  FString PoseCsv=TEXT("bone,parent,x,y,z\n");
  for (int32 Index=0;Index<Visual->Body->GetNumBones();++Index) {
   const FName Name=Visual->Body->GetBoneName(Index);
   const FVector Position=Visual->Body->GetBoneLocationByName(Name,EBoneSpaces::ComponentSpace);
   PoseCsv+=FString::Printf(TEXT("%s,%s,%f,%f,%f\n"),*Name.ToString(),*Visual->Body->GetParentBone(Name).ToString(),Position.X,Position.Y,Position.Z);
  }
  FFileHelper::SaveStringToFile(PoseCsv,*(FPaths::ProjectSavedDir()/TEXT("FactoryFPV/PilotGrip.csv")));
  for (bool Left:{true,false}) {
   const FString Suffix=Left ? TEXT("l") : TEXT("r");
   auto At=[&](const TCHAR* Bone) { return Visual->Body->GetBoneLocationByName(FName(*(FString(Bone)+Suffix)),EBoneSpaces::ComponentSpace); };
   const FVector ThumbLocal=Visual->Radio->GetRelativeTransform().InverseTransformPosition(At(TEXT("thumb_03_")));
   AddInfo(FString::Printf(TEXT("%s thumb in transmitter space: %s"),*Suffix,*ThumbLocal.ToString()));
   TestTrue(TEXT("Thumb phalanx is within glove-tip reach of stick"),ThumbLocal.Equals(FVector(Left ? -6.8 : 6.8,4.5,-.3),3.));
   const FVector First=(At(TEXT("middle_02_"))-At(TEXT("middle_01_"))).GetSafeNormal();
   const FVector Second=(At(TEXT("middle_03_"))-At(TEXT("middle_02_"))).GetSafeNormal();
   TestTrue(TEXT("Grip fingers are visibly flexed rather than flat"),FVector::DotProduct(First,Second)<.8);
  }
  TestTrue(TEXT("Original mesh temporarily hidden"),Mesh->bHiddenInGame!=0);
  TestTrue(TEXT("Held equipment temporarily hidden"),Held->bHiddenInGame!=0);
  TestFalse(TEXT("Visible clone is never owner-hidden"),Visual->Body->bOwnerNoSee!=0);
  TestEqual(TEXT("Live animation mode untouched"),Mesh->GetAnimationMode(),Mode);
  TestEqual(TEXT("Live gameplay collision untouched"),Character->GetCapsuleComponent()->GetCollisionEnabled(),Collision);
  Visual->Release();Visual->Release();
  TestFalse(TEXT("Original visibility restored"),Mesh->bHiddenInGame!=0);
  TestTrue(TEXT("Original shadow setting restored"),Mesh->bCastHiddenShadow!=0);
  TestFalse(TEXT("Equipment visibility restored"),Held->bHiddenInGame!=0);
  Visual->Destroy();
  Mesh->SetHiddenInGame(true);
  auto* Second=World->SpawnActor<AFactoryFPVPilotVisual>();
  TestTrue(TEXT("Can enter a second presentation"),Second->Begin(Character));
  Second->Destroy();
  TestTrue(TEXT("Destroy restores originally hidden body"),Mesh->bHiddenInGame!=0);
  TestFalse(TEXT("Destroy also restores equipment"),Held->bHiddenInGame!=0);
 }
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif




