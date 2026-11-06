#include "FactoryFPVDrone.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVOpticsTest,
    "FactoryFPV.Camera.FrameProjectionAndRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVOpticsTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Optics test world"),World)) return false;
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Drone=World->SpawnActor<AFactoryFPVDrone>();
    if (!TestNotNull(TEXT("Scout drone for optics checks"),Drone)) return false;
    auto* Settings=NewObject<UFactoryFPVSettings>();
    const auto* Chassis=Cast<UStaticMeshComponent>(Drone->GetDefaultSubobjectByName(TEXT("ScoutFrame")));
    if (!TestNotNull(TEXT("Scout chassis component"),Chassis) ||
        !TestNotNull(TEXT("Imported Scout chassis mesh"),Chassis->GetStaticMesh().Get()) ||
        !TestEqual(TEXT("Scout uses four imported rotor meshes"),Drone->Rotors.Num(),4)) return false;
    TestTrue(TEXT("The chassis is present outside the pilot view and has no collision"),
        Chassis->bOwnerNoSee && Chassis->IsVisible() && Chassis->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
    Settings->bShowFrameInFPV=false;
    Drone->ApplyFlightSettings(*Settings);
    for (int32 Index=0;Index<Drone->Rotors.Num();++Index) {
        const auto* Rotor=Drone->Rotors[Index].Get();
        if (!TestNotNull(TEXT("Scout rotor component"),Rotor) || !TestNotNull(TEXT("Imported Scout rotor mesh"),Rotor->GetStaticMesh().Get())) return false;
        TestTrue(TEXT("Rotor tips can be hidden from the pilot while keeping exterior geometry and the existing motor centers"),
            Rotor->bOwnerNoSee && Rotor->IsVisible() && Rotor->GetCollisionEnabled()==ECollisionEnabled::NoCollision &&
            Rotor->GetRelativeLocation().Equals(FVector(Index<2 ? 22 : -22,Index%2 ? 22 : -22,6)));
    }
    Settings->bShowFrameInFPV=true;
    Drone->ApplyFlightSettings(*Settings);
    TestTrue(TEXT("Showing prop tips keeps the chassis out of the pilot view"),Chassis->bOwnerNoSee && Chassis->IsVisible());
    auto CheckFrontProps = [&](float Size,float Tilt,float Fov) {
        Settings->FrameSizeCm=Size;
        Settings->CameraTilt=Tilt;
        Settings->CameraFov=Fov;
        Drone->ApplyFlightSettings(*Settings);
        FMinimalViewInfo View;
        Drone->CalcCamera(0,View);
        const auto Projection=View.CalculateProjectionMatrix();
        for (int Index=0;Index<2;++Index) {
            const auto* Prop=Drone->Rotors[Index].Get();
            bool VisibleCorner=false;
            FVector Minimum,Maximum;
            Prop->GetLocalBounds(Minimum,Maximum);
            for (int Corner=0;Corner<8;++Corner) {
                const FVector Local(Corner&1 ? Maximum.X : Minimum.X,Corner&2 ? Maximum.Y : Minimum.Y,Corner&4 ? Maximum.Z : Minimum.Z);
                const FVector Relative=View.Rotation.UnrotateVector(Prop->GetComponentTransform().TransformPosition(Local)-View.Location);
                const FVector4 Clip=Projection.TransformFVector4(FVector4(Relative.Y,Relative.Z,Relative.X,1));
                VisibleCorner |= Relative.X>View.GetFinalPerspectiveNearClipPlane() && Clip.W>0 &&
                    FMath::Abs(Clip.X/Clip.W)<1 && FMath::Abs(Clip.Y/Clip.W)<1;
            }
            TestTrue(TEXT("Front prop blade has visible geometry in the actual lens frustum"),VisibleCorner && !Prop->bOwnerNoSee);
        }
        return View;
    };
    CheckFrontProps(24,10,100); // Reported missing-frame case.
    CheckFrontProps(15,10,100); // Smallest airframe also needs the near-plane fix.
    CheckFrontProps(24,25,110); // Default optics.
    Settings->CameraFov=80;
    Drone->ApplyFlightSettings(*Settings);
    FMinimalViewInfo NarrowView;
    Drone->CalcCamera(0,NarrowView);
    const auto Narrow=NarrowView.CalculateProjectionMatrix();
    const auto Wide=CheckFrontProps(24,10,130).CalculateProjectionMatrix();
    TestTrue(TEXT("Wide FOV produces a wider actual projection"),Wide.M[0][0]<Narrow.M[0][0]);

    // Verify that the airframe power setting affects actor motion through the
    // actual settings application and swept movement path, not only the core.
    auto Fly = [&](float Power) {
        Settings->ThrustToWeight=Power;
        Drone->ApplyFlightSettings(*Settings);
        Drone->SetActorLocation(FVector(0,0,10000));
        Drone->Flight.reset({0,0,100});
        Drone->Flight.arm({});
        for (int Step=0;Step<480;++Step) Drone->SimulateFlight({0,0,0,.5,true},factoryfpv::StepSeconds);
        return Drone->GetActorLocation().Z;
    };
    TestTrue(TEXT("Two-to-one power at half throttle cannot climb after spool loss"),Fly(2)<10000);
    TestTrue(TEXT("Four-to-one power at half throttle climbs"),Fly(4)>10000);

    // A 40 cm opening must admit a 15 cm airframe but stop a 60 cm one.
    // Exercise the wrapper's actual collision primitive and swept movement.
    for (double Side : {-1.,1.}) {
        auto* Wall=World->SpawnActor<AActor>();
        auto* Shape=NewObject<UBoxComponent>(Wall);
        Wall->SetRootComponent(Shape);
        Wall->AddInstanceComponent(Shape);
        Shape->SetBoxExtent(FVector(5,100,200));
        Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        Shape->SetCollisionObjectType(ECC_WorldStatic);
        Shape->SetCollisionResponseToAllChannels(ECR_Block);
        Shape->RegisterComponent();
        Wall->SetActorLocation(FVector(200,Side*120,100));
    }
    auto PassOpening = [&](float Size) {
        Settings->FrameSizeCm=Size;
        Drone->ApplyFlightSettings(*Settings);
        Drone->SetActorLocationAndRotation(FVector(0,0,100),FRotator::ZeroRotator);
        FHitResult Hit;
        Drone->Movement->SafeMoveUpdatedComponent(FVector(400,0,0),FQuat::Identity,true,Hit);
        return Drone->GetActorLocation().X;
    };
    TestTrue(TEXT("Small configured airframe passes a narrow opening"),PassOpening(15)>350);
    TestTrue(TEXT("Large configured airframe is stopped by the same opening"),PassOpening(60)<195);

    auto* Manager=World->SpawnActor<APlayerCameraManager>();
    auto RecoverFov = [&](float Previous,float Current) {
        Drone->SessionCameraManager=Manager;
        Drone->PreviousLockedFov=Previous;
        Drone->SessionFov=100;
        Manager->SetFOV(Current);
        Drone->bSessionActive=true;
        Drone->RestorePilot();
        return Manager->GetLockedFOV();
    };
    TestEqual(TEXT("Returning restores a previous FOV lock"),RecoverFov(77,100),77.f);
    TestEqual(TEXT("Returning restores an unlocked FOV"),RecoverFov(0,100),0.f);
    TestEqual(TEXT("Returning preserves a newer external FOV lock"),RecoverFov(77,85),85.f);
    return true;
}
#endif
