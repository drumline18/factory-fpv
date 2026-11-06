#include "FactoryFPVInputLease.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVInputLeaseTestController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVInputLeaseOwnershipTest,
    "FactoryFPV.Session.InputLeaseOwnership",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVInputLeaseOwnershipTest::RunTest(const FString&) {
    FactoryFPVInputLease::Startup();
    ON_SCOPE_EXIT { FactoryFPVInputLease::Startup(); };
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,
        ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Input lease counter world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    AddInfo(TEXT("Uses the private Editor reset adapter around real engine counters and public cinematic/reset APIs. It qualifies the reset protocol, not native Shipping export-hook wiring."));
    auto BothIgnored=[](APlayerController* PC) { return PC->IsMoveInputIgnored() && PC->IsLookInputIgnored(); };
    auto ForeignAcquire=[](APlayerController* PC) { PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true); };
    auto ForeignRelease=[](APlayerController* PC) { PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false); };

    {
        auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
        if (!TestNotNull(TEXT("Shared-lease controller"),PC)) return false;
        ForeignAcquire(PC);
        FFactoryFPVInputLease First,Second;
        if (!TestTrue(TEXT("First simultaneous lease acquires"),First.Acquire(PC)) ||
            !TestTrue(TEXT("Second simultaneous lease acquires"),Second.Acquire(PC))) return false;
        TestEqual(TEXT("Two simultaneous leases are registered"),FactoryFPVInputLease::GetDiagnostics(PC).ActiveLeases,2);
        PC->ResetIgnoreInputFlags();
        TestFalse(TEXT("Real reset clears old move holds from all owners"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("Real reset clears old look holds from all owners"),PC->IsLookInputIgnored());
        ForeignAcquire(PC);
        TestTrue(TEXT("First lease renews over a newer independent hold"),First.Renew());
        TestTrue(TEXT("Second lease renews over a newer independent hold"),Second.Renew());
        First.Release();
        TestTrue(TEXT("Releasing one renewed lease preserves the other owners"),BothIgnored(PC));
        Second.Release();
        TestTrue(TEXT("Both renewed leases leave the newer foreign holds intact"),BothIgnored(PC));
        ForeignRelease(PC);
        TestFalse(TEXT("One newer foreign move release clears the exact remaining count"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("One newer foreign look release clears the exact remaining count"),PC->IsLookInputIgnored());
    }
    {
        auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
        if (!TestNotNull(TEXT("Cinematic-preservation controller"),PC)) return false;
        // Public three-argument overload affects input without changing HUD or
        // pawn presentation. Native PlayerController reset preserves this axis.
        PC->SetCinematicMode(true,true,false);
        TestTrue(TEXT("Public cinematic mode acquires a real move hold"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("Move-only cinematic mode leaves look input free"),PC->IsLookInputIgnored());
        FFactoryFPVInputLease Lease;
        if (!TestTrue(TEXT("Lease acquires over the cinematic move hold"),Lease.Acquire(PC))) return false;
        PC->ResetIgnoreInputFlags();
        TestTrue(TEXT("Real reset preserves the cinematic move contribution"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("Real reset clears look while preserving cinematic move"),PC->IsLookInputIgnored());
        TestTrue(TEXT("Lease renews after a partially preserved native reset"),Lease.Renew());
        TestTrue(TEXT("Repeated renewal does not accumulate counter contributions"),Lease.Renew());
        Lease.Release();
        TestTrue(TEXT("Lease release preserves cinematic move ownership"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("Lease release removes its renewed look contribution"),PC->IsLookInputIgnored());
        PC->SetCinematicMode(false,true,false);
        TestFalse(TEXT("One cinematic exit clears move without a leaked pre-reset lease count"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("Cinematic exit leaves look input free"),PC->IsLookInputIgnored());
    }
    {
        auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
        if (!TestNotNull(TEXT("Nested-reset controller"),PC)) return false;
        ForeignAcquire(PC);
        FFactoryFPVInputLease First,Second;
        if (!TestTrue(TEXT("First nested-reset lease acquires"),First.Acquire(PC)) ||
            !TestTrue(TEXT("Second nested-reset lease acquires"),Second.Acquire(PC))) return false;
        {
            FFactoryFPVInputResetScope Outer(PC,EFactoryFPVInputResetAxes::Both);
            TestEqual(TEXT("Outer reset interval is active"),FactoryFPVInputLease::GetDiagnostics(PC).ResetDepth,1);
            TestTrue(TEXT("Before-reset withdrawal leaves foreign holds intact"),BothIgnored(PC));
            TestFalse(TEXT("First lease cannot renew inside an active reset"),First.Renew());
            {
                FFactoryFPVInputResetScope Inner(PC,EFactoryFPVInputResetAxes::Move);
                TestEqual(TEXT("Nested reset interval is active"),FactoryFPVInputLease::GetDiagnostics(PC).ResetDepth,2);
                TestTrue(TEXT("Nested withdrawal cannot consume foreign holds twice"),BothIgnored(PC));
                TestFalse(TEXT("Second lease cannot renew inside nested reset"),Second.Renew());
                PC->ResetIgnoreInputFlags();
                TestFalse(TEXT("Nested actual engine reset clears move"),PC->IsMoveInputIgnored());
                TestFalse(TEXT("Nested actual engine reset clears look"),PC->IsLookInputIgnored());
                ForeignAcquire(PC);
                First.Release();
                TestFalse(TEXT("Release inside reset retires its lease"),First.IsActive());
                TestTrue(TEXT("Release inside reset preserves newer foreign holds"),BothIgnored(PC));
            }
            TestEqual(TEXT("Nested reset exit retains the outer protection"),FactoryFPVInputLease::GetDiagnostics(PC).ResetDepth,1);
            TestFalse(TEXT("Outer interval still prevents renewal"),Second.Renew());
            Second.Release();
            TestTrue(TEXT("Second release inside reset preserves newer foreign holds"),BothIgnored(PC));
        }
        TestEqual(TEXT("All reset intervals close cleanly"),FactoryFPVInputLease::GetDiagnostics(PC).ResetDepth,0);
        TestFalse(TEXT("Reset completion does not resurrect first released lease"),First.Renew());
        TestFalse(TEXT("Reset completion does not resurrect second released lease"),Second.Renew());
        ForeignRelease(PC);
        TestFalse(TEXT("One foreign move release clears the nested-reset remainder"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("One foreign look release clears the nested-reset remainder"),PC->IsLookInputIgnored());
    }
    {
        auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
        if (!TestNotNull(TEXT("Transactional-acquire controller"),PC)) return false;
        ForeignAcquire(PC);
        FFactoryFPVInputLease Lease;
        if (!TestTrue(TEXT("Transactional-acquire fixture obtains its lease"),Lease.Acquire(PC))) return false;
        {
            FFactoryFPVInputResetScope Reset(PC,EFactoryFPVInputResetAxes::Move);
            TestFalse(TEXT("Same-controller Acquire refuses an active reset"),Lease.Acquire(PC));
            TestFalse(TEXT("Failed Acquire leaves no active partial lease"),Lease.IsActive());
            TestFalse(TEXT("Failed Acquire relinquishes its unaffected look contribution"),Lease.OwnsLook());
            TestTrue(TEXT("Transactional failure preserves both preexisting foreign holds"),BothIgnored(PC));
            PC->ResetIgnoreMoveInput();
            TestFalse(TEXT("Actual move reset clears its old foreign hold"),PC->IsMoveInputIgnored());
            TestTrue(TEXT("Actual move reset leaves foreign look intact"),PC->IsLookInputIgnored());
            PC->SetIgnoreMoveInput(true);
        }
        Lease.Release();
        TestTrue(TEXT("Release after failed Acquire cannot consume newer move or preserved look"),BothIgnored(PC));
        ForeignRelease(PC);
        TestFalse(TEXT("One foreign move release clears after failed Acquire"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("One foreign look release clears after failed Acquire"),PC->IsLookInputIgnored());
    }
    {
        auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
        if (!TestNotNull(TEXT("Service-shutdown controller"),PC)) return false;
        ForeignAcquire(PC);
        FFactoryFPVInputLease First,Second,Fresh;
        if (!TestTrue(TEXT("First shutdown fixture lease acquires"),First.Acquire(PC)) ||
            !TestTrue(TEXT("Second shutdown fixture lease acquires"),Second.Acquire(PC))) return false;
        FactoryFPVInputLease::Shutdown();
        TestFalse(TEXT("Shutdown retires the first active lease"),First.IsActive());
        TestFalse(TEXT("Shutdown retires the second active lease"),Second.IsActive());
        TestTrue(TEXT("Shutdown balances its leases while preserving foreign holds"),BothIgnored(PC));
        TestFalse(TEXT("Stopped service cannot renew an old lease"),First.Renew());
        TestFalse(TEXT("Stopped service refuses fresh acquisition"),Fresh.Acquire(PC));
        TestTrue(TEXT("Refused acquisition leaves foreign holds untouched"),BothIgnored(PC));
        FactoryFPVInputLease::Startup();
        TestFalse(TEXT("Service restart cannot resurrect an old lease"),Second.Renew());
        TestTrue(TEXT("Explicit restart permits a new lease"),Fresh.Acquire(PC));
        Fresh.Release();
        TestTrue(TEXT("New lease release after restart preserves foreign holds"),BothIgnored(PC));
        ForeignRelease(PC);
        TestFalse(TEXT("One foreign move release clears after service restart"),PC->IsMoveInputIgnored());
        TestFalse(TEXT("One foreign look release clears after service restart"),PC->IsLookInputIgnored());
    }
    return true;
}
#endif
