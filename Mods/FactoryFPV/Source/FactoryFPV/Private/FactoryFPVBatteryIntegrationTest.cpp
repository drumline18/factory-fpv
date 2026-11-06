#include "FactoryFPVDrone.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVSettings.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVBatteryIntegrationTest,"FactoryFPV.Flight.BatteryCutoffAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVBatteryIntegrationTest::RunTest(const FString&) {
 UWorld::InitializationValues Values;
 Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
 auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
 auto* Settings=NewObject<UFactoryFPVSettings>();
 Settings->bSimulateBattery=true;Settings->BatteryCells=1;Settings->BatteryCapacityMah=50;Settings->BatteryMaxPowerWatts=5000;
 auto* Drone=World->SpawnActor<AFactoryFPVDrone>();
 Drone->ApplyFlightSettings(*Settings);
 Drone->LaunchPosition=FVector(0,0,1000);
 Drone->SetActorLocation(Drone->LaunchPosition);
 Drone->Flight.reset({0,0,10});
 TestTrue(TEXT("Configured simulation starts full"),Drone->Battery.enabled() && Drone->Battery.remainingPercent()==100);
 TestTrue(TEXT("Fresh pack allows low-throttle arm"),Drone->TryArmFlight({0,0,0,0,true}));
 for(int Index=0;Index<2400 && !Drone->Battery.empty();++Index) Drone->SimulateFlight({0,0,0,1,true},factoryfpv::StepSeconds);
 TestTrue(TEXT("Actual simulated flight consumes the pack"),Drone->Battery.empty());
 TestFalse(TEXT("Empty pack cuts actual drone thrust/arm state"),Drone->Flight.armed);
 TestEqual(TEXT("Cutoff zeros flight thrust"),Drone->Flight.thrust,0.);
 TestFalse(TEXT("Arming entry rejects empty pack"),Drone->TryArmFlight({0,0,0,0,true}));
 const FVector FlightPosition=Drone->GetActorLocation();
 Drone->ResetDrone();
 TestTrue(TEXT("Unsupported recovery leaves the pack empty"),Drone->Battery.empty());
 TestEqual(TEXT("Unsupported battery recovery leaves position unchanged"),Drone->GetActorLocation(),FlightPosition);

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
 TestEqual(TEXT("Successful launch reset recharges simulated pack"),Drone->Battery.remainingPercent(),100.);
 TestFalse(TEXT("Recovery never automatically arms"),Drone->Flight.armed);
 TestTrue(TEXT("Fresh low-throttle arm works after recovery"),Drone->TryArmFlight({0,0,0,0,true}));
 Settings->bSimulateBattery=false;
 Drone->ApplyFlightSettings(*Settings);
 TestFalse(TEXT("Disabled simulation has no battery telemetry"),Drone->Battery.enabled());
 return true;
}
#endif

