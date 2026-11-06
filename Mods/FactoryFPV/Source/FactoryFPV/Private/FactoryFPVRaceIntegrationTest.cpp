#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVOSD.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FGActorSaveHeaderTypes.h"
#include "FGPowerInfoComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include <initializer_list>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRaceIntegrationTest,
    "FactoryFPV.Racing.CourseSweepsClockAndRecords",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRaceIntegrationTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Race integration world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };

    // Do not BeginPlay an AFGBuildable in this bare SDK world; normal gameplay
    // requires Satisfactory world/buildable systems. Initialize our gate IDs and
    // configuration explicitly. Circuit setters/simulation are SDK stubs, so
    // inject reported results only, without claiming a working cable/grid.
    auto* Circuit=FindFProperty<FIntProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mCircuitID"));
    auto* HasPower=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mHasPower"));
    auto* Fuse=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mIsFuseTriggered"));
    if (!TestNotNull(TEXT("Reported circuit property"),Circuit) || !TestNotNull(TEXT("Reported power property"),HasPower) ||
        !TestNotNull(TEXT("Reported fuse property"),Fuse)) return false;
    auto ReportPower=[&](AFactoryFPVRaceGate* Gate,bool Powered,bool Tripped=false) {
        Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
        HasPower->SetPropertyValue_InContainer(Gate->GetPowerInfo(),Powered);
        Fuse->SetPropertyValue_InContainer(Gate->GetPowerInfo(),Tripped);
    };
    constexpr int32 Course=11;
    const FRotator Rotation(0,90,0);
    const FVector Normal=Rotation.Vector();
    const FVector Right=Rotation.Quaternion().GetAxisY();
    const FVector Up=Rotation.Quaternion().GetAxisZ();
    const FVector Origin(1000,2000,300);
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto MakeGate=[&](int32 Order,FVector Position,bool FullSize=true) -> AFactoryFPVRaceGate* {
        AFactoryFPVRaceGate* Gate=FullSize ? World->SpawnActor<AFactoryFPVRaceGateFull>(Position,Rotation,Spawn) :
            World->SpawnActor<AFactoryFPVRaceGate>(Position,Rotation,Spawn);
        if (!Gate) return Gate;
        Gate->SetActorScale3D(FVector(2));
        Gate->EnsureIdentityAndOrder();
        FString Error;
        if (!Gate->Configure(Course,Order,Error)) AddError(Error);
        return Gate;
    };
    auto* Start=MakeGate(1,Origin);
    auto* Checkpoint=MakeGate(2,Origin+Normal*1000,false);
    if (!TestNotNull(TEXT("Start gate actor"),Start) || !TestNotNull(TEXT("Checkpoint gate actor"),Checkpoint)) return false;
    TestTrue(TEXT("Integration gates have stable distinct persistent identities"),Start->GetGateId().IsValid() &&
        Checkpoint->GetGateId().IsValid() && Start->GetGateId()!=Checkpoint->GetGateId());
    const FVector Center=Start->GetApertureTransform().GetLocation();
    TestTrue(TEXT("Full-size gate aperture offset is scaled once in centimetres"),Start->IsFullSizeGate() && Center.Equals(Origin+Up*400,.001));
    TestTrue(TEXT("Compact checkpoint retains its distinct scaled center"),!Checkpoint->IsFullSizeGate() &&
        Checkpoint->GetApertureTransform().GetLocation().Equals(Origin+Normal*1000+Up*340,.001));
    auto Point=[&](double Forward,double Across=0,double Height=0) { return Center+Normal*Forward+Right*Across+Up*Height; };
    auto* Owner=World->SpawnActor<AActor>();
    auto MakeRace=[&]() {
        auto* Component=NewObject<UFactoryFPVRaceComponent>(Owner);
        Component->RegisterComponent();
        return Component;
    };
    auto* Race=MakeRace();
    auto Advance=[](UFactoryFPVRaceComponent* Target,std::initializer_list<FVector> Points,double Seconds,double RadiusCm=20) {
        const TArray<FVector> Path(Points);
        Target->AdvanceFrame(MakeArrayView(Path),Seconds,RadiusCm);
    };
    auto AdvanceDisarmed=[](UFactoryFPVRaceComponent* Target,const FVector& Position) {
        const FVector Path[]{Position};
        const uint8 NoScoring[]{0};
        Target->AdvanceFrame(MakeArrayView(Path),1.,20.,MakeArrayView(NoScoring));
        Target->InvalidateLap(); // The drone also invalidates after scoring when it ends the frame disarmed.
    };
    auto CheckApproach=[this](const TCHAR* Label,const UFactoryFPVRaceComponent* Target,const AFactoryFPVRaceGate* Gate,
        const FVector& Position,const TCHAR* ExpectedInstruction,bool bExpectedExitSide,double ClearanceCm=20.) {
        FVector Aim;
        int32 Order=0;
        FString Instruction;
        bool bOnExitSide=!bExpectedExitSide;
        TestTrue(Label,Target->GetNextCheckpointApproach(Position,ClearanceCm,Aim,Order,Instruction,bOnExitSide) &&
            Order==Gate->GetGateOrder() && Aim.Equals(Gate->GetPassageAimPoint(),.001) &&
            Instruction==ExpectedInstruction && bOnExitSide==bExpectedExitSide);
    };
    FFactoryFPVOSDTelemetry Data;
    FString Error;
    TestFalse(TEXT("Null world cannot be validated"),UFactoryFPVRaceComponent::ValidateCourse(nullptr,Course,Error));
    TestFalse(TEXT("Free flight is not a timed course"),UFactoryFPVRaceComponent::ValidateCourse(World,0,Error));
    TestTrue(TEXT("Unpowered but well-formed course can be inspected"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error,false));
    TestFalse(TEXT("Unpowered reported gate prevents timed-course validation"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error,true));
    TestFalse(TEXT("Unpowered course cannot start a session"),Race->StartCourse(World,Course,Error));
    TestTrue(TEXT("Rejected start leaves free flight with no selected core"),Race->GetCourseId()==0 && !Race->GetRaceState().selected);
    ReportPower(Start,true);ReportPower(Checkpoint,true);
    TestTrue(TEXT("Reported connected powered gates validate"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error));
    const FString BeforePreview=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
    auto* DesignerFlag=FindFProperty<FBoolProperty>(AFGBuildable::StaticClass(),TEXT("mReplicatedBuiltInsideBlueprintDesigner"));
    if (!TestNotNull(TEXT("Native designer-state property"),DesignerFlag)) return false;
    auto* Preview=MakeGate(3,Origin+Normal*2000);
    DesignerFlag->SetPropertyValue_InContainer(Preview,true);
    TestTrue(TEXT("Unpowered designer preview cannot invalidate a valid live course"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error));
    TestEqual(TEXT("Designer previews do not change the live layout signature/PB key"),UFactoryFPVRaceComponent::LayoutSignature(World,Course),BeforePreview);
    Preview->Destroy();
    ReportPower(Checkpoint,true,true);
    TestFalse(TEXT("Reported fuse failure invalidates an otherwise powered course"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error));
    TestTrue(TEXT("Fuse guidance identifies the actual affected checkpoint"),Error.Contains(TEXT("Checkpoint 2")) && Error.Contains(TEXT("has no power")));
    ReportPower(Checkpoint,true);
    const FString OriginalSignature=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
    TestTrue(TEXT("Layout signature includes persistent gate identity"),OriginalSignature.Contains(Start->GetGateId().ToString(EGuidFormats::Digits)));
    TestTrue(TEXT("FPV3 records separate full-size and compact extents"),OriginalSignature.StartsWith(TEXT("FPV3:C11;")) &&
        OriginalSignature.Contains(TEXT(":7.600000000,3.600000000:")) && OriginalSignature.Contains(TEXT(":3.000000000,3.000000000:")));

    TestTrue(TEXT("Powered rotated/scaled course starts"),Race->StartCourse(World,Course,Error));
    AdvanceDisarmed(Race,Point(-500));
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Initially disarmed race shows start guidance and keeps the start cue"),Race->GetRaceState().ready &&
        Race->GetRaceState().problem==factoryfpv::RaceProblem::None && Start->IsNextCheckpointHighlighted() &&
        Data.RaceStatus==TEXT("CROSS GATE 1 FROM THE PINK SIDE TO START"));
    Advance(Race,{Point(500),Point(1500)},1);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Wrong-order pass has actionable guidance"),Data.RaceStatus.StartsWith(TEXT("WRONG GATE")));
    Advance(Race,{Point(1500),Point(-500)},.5);
    Advance(Race,{Point(-500),Point(500)},.5);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Fresh ordered crossing clears stale wrong-gate feedback"),Data.RaceStatus.IsEmpty() && Data.RaceGate==2);
    Race->StartCourse(World,Course,Error);
    CheckApproach(TEXT("Rotated gate shows its entry aim while approached across its actual normal"),Race,Start,Point(-500,400),TEXT(""),false);
    CheckApproach(TEXT("A small drone remains in ordinary guidance just inside the minimum clearance"),Race,Start,Point(19.5),TEXT(""),false,5.);
    CheckApproach(TEXT("A small drone receives pink-face guidance beyond the minimum clearance"),Race,Start,Point(20.5),TEXT("FIND PINK ENTRY SIDE"),true,5.);
    CheckApproach(TEXT("A larger drone keeps the same passage aim within one body radius of the plane"),Race,Start,Point(49.5),TEXT(""),false,50.);
    CheckApproach(TEXT("A larger drone receives pink-face guidance beyond its body radius"),Race,Start,Point(50.5),TEXT("FIND PINK ENTRY SIDE"),true,50.);
    Advance(Race,{Point(-500),Center},1);
    AdvanceDisarmed(Race,Center);
    Advance(Race,{Center,Point(500)},1);
    TestFalse(TEXT("Idle disarm clears saved approach to an exact gate-plane endpoint"),Race->GetRaceState().inLap);
    Advance(Race,{Point(-500,740),Point(500,740)},1);
    TestFalse(TEXT("Centimetre radius conversion rejects tangent scaled aperture edge"),Race->GetRaceState().inLap);
    Race->StartCourse(World,Course,Error);
    Advance(Race,{Point(-500,739),Point(500,739)},1);
    TestTrue(TEXT("Rotated scaled aperture accepts sphere just inside its clear edge"),Race->GetRaceState().inLap);
    Race->StartCourse(World,Course,Error);
    Advance(Race,{Point(-500,310),Point(1500,310)},20);
    TestTrue(TEXT("Same lateral pass scores the wide frame and misses the compact checkpoint"),
        Race->GetRaceState().inLap && Race->GetRaceState().nextGate==2);
    Race->StartCourse(World,Course,Error);
    Advance(Race,{Point(-500),Point(1500)},20);
    TestTrue(TEXT("Actual UE-centimetre sweep scores start and checkpoint in order"),Race->GetRaceState().inLap && Race->GetRaceState().nextGate==1);
    TestTrue(TEXT("Crossing time interpolates within actual long frame duration"),FMath::IsNearlyEqual(Race->GetRaceState().elapsed,15.,1.e-8));
    Race->FillTelemetry(Data);
    TestTrue(TEXT("First checkpoint split uses the crossing time rather than the frame end"),Data.RaceSplitGate==2 &&
        FMath::IsNearlyEqual(Data.RaceSplitSeconds,10.,1.e-8) && !Data.bRaceSplitHasDelta);
    Advance(Race,{Point(1500),Point(-500)},20);
    Advance(Race,{Point(-500),Point(1500)},20);
    TestTrue(TEXT("Component completes one actual ordered loop"),Race->GetRaceState().lapCount==1);
    TestTrue(TEXT("Completed loop has interpolated forty-second timing"),FMath::IsNearlyEqual(Race->GetRaceState().lastLap,40.,1.e-8));
    TestTrue(TEXT("Completed component lap writes gate1's layout-specific record"),FMath::IsNearlyEqual(Start->GetBestLapSeconds(OriginalSignature),40.,1.e-8));
    const auto& FirstDebrief=Race->GetDebrief();
    TestTrue(TEXT("First debrief lap uses the scored time and has no prior reference"),FirstDebrief.CourseId==Course &&
        FirstDebrief.LapCount==1 && FMath::IsNearlyEqual(FirstDebrief.FastestLap,40.,1.e-8) && FirstDebrief.RecentLaps.Num()==1 &&
        FirstDebrief.RecentLaps[0].Number==1 && FMath::IsNearlyEqual(FirstDebrief.RecentLaps[0].Seconds,40.,1.e-8) &&
        FirstDebrief.RecentLaps[0].ReferenceSeconds==0. && FirstDebrief.RecentLaps[0].bCourseBest);
    TestTrue(TEXT("First debrief retains the completed lap split, not the following lap's split"),FirstDebrief.LastLapSplits.Num()==1 &&
        FirstDebrief.LastLapSplits[0].Checkpoint==2 && FMath::IsNearlyEqual(FirstDebrief.LastLapSplits[0].Seconds,10.,1.e-8) &&
        FirstDebrief.LastLapSplits[0].ReferenceSeconds==0.);
    const auto* Ghost=Start->GetBestLapGhost(OriginalSignature);
    TestTrue(TEXT("Saved ghost includes both exact start/finish crossings and the sampled line"),Ghost && Ghost->Positions.Num()==801 &&
        Ghost->Sample(0.,40.).Equals(Center-Origin,.01) && Ghost->Sample(40.,40.).Equals(Center-Origin,.01) &&
        Ghost->Sample(5.025,40.).Equals(Point(502.5)-Origin,.01));
    const auto FirstBestSplits=Start->GetBestLapSplits(OriginalSignature,1);
    TestTrue(TEXT("Completed best stores its own checkpoint split"),!FirstBestSplits.IsEmpty() &&
        FMath::IsNearlyEqual(FirstBestSplits[0],10.,1.e-8));
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Race telemetry reports real course/progress/record"),Data.bRacing && Data.RaceCourse==Course &&
        Data.RaceGate==1 && Data.RaceGateCount==2 && Data.RaceLaps==1 && FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8));
    TestTrue(TEXT("First finish reports the new saved course best alongside the live cue"),
        Data.RaceFinish==EFactoryFPVRaceFinish::CourseBest && Data.RaceStatus.IsEmpty() && Start->IsNextCheckpointHighlighted());
    Advance(Race,{Point(1500)},2.);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Finish feedback stays visible for a glance"),Data.RaceFinish==EFactoryFPVRaceFinish::CourseBest);
    Advance(Race,{Point(1500)},1.);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Finish feedback expires without clearing recorded times"),Data.RaceFinish==EFactoryFPVRaceFinish::None &&
        Data.RaceStatus.IsEmpty() && FMath::IsNearlyEqual(Data.RaceLast,40.,1.e-8) && FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8));
    Race->StartCourse(World,Course,Error);
    Advance(Race,{Point(-500),Point(1500)},20);
    Advance(Race,{Point(1500),Point(-500)},20);
    Advance(Race,{Point(-500),Point(1500)},20);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("A tied finish confirms completion without announcing a course record"),
        Data.RaceFinish==EFactoryFPVRaceFinish::LapComplete && FMath::IsNearlyEqual(Data.RaceLast,40.,1.e-8) && FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8));

    const auto* SavedGhost=Start->GetBestLapGhost(OriginalSignature);
    if (!TestNotNull(TEXT("Official ghost before practice"),SavedGhost)) return false;
    const TArray<FVector3f> OfficialGhost=SavedGhost->Positions;
    if (!TestTrue(TEXT("Practice opens the exact powered course"),Race->StartCourse(World,Course,Error,&OriginalSignature,true))) return false;
    Advance(Race,{Point(-500),Point(1500)},10);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("Practice tracks ordered checkpoints and compares pace against the official split"),
        Race->IsPractice() && Data.bRacePractice && Data.RaceGate==1 && Data.RaceSplitGate==2 && Data.bRaceSplitHasDelta &&
        FMath::IsNearlyEqual(Data.RaceSplitSeconds,5.,1.e-8) && FMath::IsNearlyEqual(Data.RaceSplitDelta,-5.,1.e-8));
    Advance(Race,{Point(1500),Point(-500)},10);
    Advance(Race,{Point(-500),Point(1500)},10);
    Race->FillTelemetry(Data);
    TestTrue(TEXT("A faster practice lap still times and announces the finish"),
        Data.RaceLaps==1 && FMath::IsNearlyEqual(Data.RaceLast,20.,1.e-8) && Data.RaceFinish==EFactoryFPVRaceFinish::LapComplete);
    const auto PracticeSplits=Start->GetBestLapSplits(OriginalSignature,1);
    SavedGhost=Start->GetBestLapGhost(OriginalSignature);
    TestTrue(TEXT("A faster practice lap preserves the official time, split and every ghost sample"),
        FMath::IsNearlyEqual(Start->GetBestLapSeconds(OriginalSignature),40.,1.e-8) && PracticeSplits.Num()==1 &&
        FMath::IsNearlyEqual(PracticeSplits[0],10.,1.e-8) && SavedGhost && SavedGhost->Positions==OfficialGhost);
    Race->StopCourse();

    auto* NextFlight=MakeRace();
    TestTrue(TEXT("New flight can select same saved gate layout"),NextFlight->StartCourse(World,Course,Error));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Gate1 PB survives new flight without copying old lap progress"),FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8) &&
        NextFlight->GetRaceState().lapCount==0 && !NextFlight->GetRaceState().inLap && Data.RaceFinish==EFactoryFPVRaceFinish::None);
    FVector GhostPosition,GhostForward;
    TestFalse(TEXT("A saved ghost waits for a new start crossing"),NextFlight->GetGhostPose(GhostPosition,GhostForward));
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    TestTrue(TEXT("Replay follows the saved pilot's line at the new lap's elapsed time"),
        NextFlight->GetGhostPose(GhostPosition,GhostForward) && GhostPosition.Equals(Point(1500),.01));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("New flight compares the same checkpoint against the saved best"),Data.RaceSplitGate==2 &&
        Data.bRaceSplitHasDelta && FMath::IsNearlyZero(Data.RaceSplitDelta,1.e-8));
    Advance(NextFlight,{Point(1500)},60);
    NextFlight->FillTelemetry(Data);
    TestEqual(TEXT("Expired split leaves the ordinary race telemetry"),Data.RaceSplitGate,0);
    TestFalse(TEXT("Replay leaves the course after its own finish time"),NextFlight->GetGhostPose(GhostPosition,GhostForward));
    TestTrue(TEXT("Stationary low-FPS frame adds all sixty active seconds"),FMath::IsNearlyEqual(NextFlight->GetRaceState().elapsed,75.,1.e-8));
    Advance(NextFlight,{Point(1500),Point(-500)},20);
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    TestTrue(TEXT("Actual frame timer is not clamped to physics step budget"),FMath::IsNearlyEqual(NextFlight->GetRaceState().lastLap,100.,1.e-8));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Slower new-flight lap cannot replace saved faster PB"),FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8));
    TestTrue(TEXT("Session best alone cannot be announced as the saved course record"),Data.RaceFinish==EFactoryFPVRaceFinish::LapComplete);

    Checkpoint->SetActorLocation(Checkpoint->GetActorLocation()+Right*25);
    const FString ChangedSignature=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
    TestTrue(TEXT("Actual gate placement changes course signature"),ChangedSignature!=OriginalSignature);
    TestEqual(TEXT("Moved layout cannot borrow the previous PB"),Start->GetBestLapSeconds(ChangedSignature),0.);
    TestNull(TEXT("Moved layout cannot borrow a ghost"),Start->GetBestLapGhost(ChangedSignature));
    AdvanceDisarmed(NextFlight,Point(1500));
    TestTrue(TEXT("Live geometry editing cancels partial timing"),!NextFlight->GetRaceState().inLap &&
        NextFlight->GetRaceState().problem==factoryfpv::RaceProblem::ChangedCourse);
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Course-change guidance clears pending finish feedback"),Data.RaceStatus==TEXT("COURSE CHANGED - RESELECT COURSE") &&
        Data.RaceFinish==EFactoryFPVRaceFinish::None);
    TestTrue(TEXT("Changed layout can be explicitly reselected for practice"),
        NextFlight->StartCourse(World,Course,Error,&ChangedSignature,true));
    NextFlight->FillTelemetry(Data);
    TestEqual(TEXT("New layout's telemetry starts without an inherited PB"),Data.RaceBest,0.);
    Advance(NextFlight,{Point(-500),Point(1500)},10);
    Advance(NextFlight,{Point(1500),Point(-500)},10);
    Advance(NextFlight,{Point(-500),Point(1500)},10);
    TestTrue(TEXT("Practice on a new layout leaves its record empty"),
        NextFlight->GetRaceState().lapCount==1 && Start->GetBestLapSeconds(ChangedSignature)==0. &&
        Start->GetBestLapSplits(ChangedSignature,1).IsEmpty() && !Start->GetBestLapGhost(ChangedSignature));
    TestTrue(TEXT("Restarting in scored mode clears practice and its lap progress"),
        NextFlight->StartCourse(World,Course,Error,&ChangedSignature) && !NextFlight->IsPractice() && NextFlight->GetRaceState().lapCount==0);
    Advance(NextFlight,{Point(-500),Point(1500)},30);
    Advance(NextFlight,{Point(1500),Point(-500)},30);
    Advance(NextFlight,{Point(-500),Point(1500)},30);
    TestTrue(TEXT("First slower lap on changed layout establishes its own PB"),FMath::IsNearlyEqual(Start->GetBestLapSeconds(ChangedSignature),60.,1.e-8));

    NextFlight->StartCourse(World,Course,Error);
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    ReportPower(Checkpoint,false);
    Advance(NextFlight,{Point(1500),Point(-500)},20);
    TestTrue(TEXT("Reported power loss cancels lap through the real component snapshot"),!NextFlight->GetRaceState().inLap &&
        !NextFlight->GetRaceState().ready && NextFlight->GetRaceState().problem==factoryfpv::RaceProblem::UnpoweredGate);
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Power-loss telemetry gives restart guidance"),Data.RaceStatus.Contains(TEXT("POWER")) && Data.RaceGate==1);
    TestTrue(TEXT("A lap interrupted by power loss contributes no debrief results"),NextFlight->GetDebrief().LapCount==0 &&
        NextFlight->GetDebrief().RecentLaps.IsEmpty() && NextFlight->GetDebrief().LastLapSplits.IsEmpty());
    AdvanceDisarmed(NextFlight,Point(-500));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Disarmed frame preserves power-loss guidance and hides the cue"),!Start->IsNextCheckpointHighlighted() &&
        NextFlight->GetRaceState().problem==factoryfpv::RaceProblem::UnpoweredGate && Data.RaceStatus.Contains(TEXT("POWER")));
    ReportPower(Checkpoint,true);
    AdvanceDisarmed(NextFlight,Point(1600));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Restored power cannot resume partial checkpoint progress"),NextFlight->GetRaceState().ready &&
        !NextFlight->GetRaceState().inLap && NextFlight->GetRaceState().nextGate==1 &&
        Data.RaceStatus==TEXT("CROSS GATE 1 FROM THE PINK SIDE TO START"));
    Advance(NextFlight,{Point(1600),Point(-500)},1);
    Advance(NextFlight,{Point(-500),Point(500)},1);
    TestTrue(TEXT("Fresh powered gate1 begins a new lap"),NextFlight->GetRaceState().inLap && NextFlight->GetRaceState().nextGate==2);
    AdvanceDisarmed(NextFlight,Point(500));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Disarming during a real lap cancels it and shows restart guidance"),!NextFlight->GetRaceState().inLap &&
        NextFlight->GetRaceState().nextGate==1 && NextFlight->GetRaceState().problem==factoryfpv::RaceProblem::Recovery &&
        Data.RaceStatus==TEXT("LAP CANCELLED - RESTART AT GATE 1"));

    // False per-segment flags represent disarmed/invalid/teleported movement.
    // They must not score, and later disarm cannot erase a legitimate lap that
    // finished on an earlier armed swept segment of the same captured frame.
    const TArray<FVector> Outbound{Point(-500),Point(1500)};
    const TArray<uint8> Disallowed{0};
    NextFlight->StartCourse(World,Course,Error);
    NextFlight->AdvanceFrame(MakeArrayView(Outbound),20,20,MakeArrayView(Disallowed));
    TestTrue(TEXT("Disarmed or recovery-teleport segment cannot start timing"),!NextFlight->GetRaceState().inLap && NextFlight->GetRaceState().lapCount==0);
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    Advance(NextFlight,{Point(1500),Point(-500)},20);
    const TArray<FVector> FinishThenDisarm{Point(-500),Point(500),Point(1500)};
    const TArray<uint8> ArmedThenDisarmed{1,0};
    NextFlight->AdvanceFrame(MakeArrayView(FinishThenDisarm),20,20,MakeArrayView(ArmedThenDisarmed));
    TestTrue(TEXT("Armed finish before a later same-frame disarm retains completed lap"),NextFlight->GetRaceState().lapCount==1 &&
        FMath::IsNearlyEqual(NextFlight->GetRaceState().lastLap,40.,1.e-8));
    TestTrue(TEXT("Later disarmed segment cancels new partial lap"),!NextFlight->GetRaceState().inLap && NextFlight->GetRaceState().nextGate==1);
    AdvanceDisarmed(NextFlight,Point(1500));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Disarmed frames retain completed lap telemetry and saved best"),Data.RaceLaps==1 &&
        FMath::IsNearlyEqual(Data.RaceLast,40.,1.e-8) && FMath::IsNearlyEqual(Data.RaceBest,40.,1.e-8) &&
        FMath::IsNearlyEqual(Start->GetBestLapSeconds(ChangedSignature),40.,1.e-8));
    TestTrue(TEXT("Same-frame disarm prioritizes restarting the new partial lap over finish feedback"),
        Data.RaceStatus==TEXT("LAP CANCELLED - RESTART AT GATE 1") && Data.RaceFinish==EFactoryFPVRaceFinish::None);
    const auto& DisarmDebrief=NextFlight->GetDebrief();
    TestTrue(TEXT("A valid finish remains in the debrief after same-frame disarm"),DisarmDebrief.LapCount==1 &&
        DisarmDebrief.RecentLaps.Num()==1 && FMath::IsNearlyEqual(DisarmDebrief.RecentLaps[0].Seconds,40.,1.e-8) &&
        FMath::IsNearlyEqual(DisarmDebrief.RecentLaps[0].ReferenceSeconds,60.,1.e-8) && DisarmDebrief.LastLapSplits.Num()==1 &&
        FMath::IsNearlyEqual(DisarmDebrief.LastLapSplits[0].Seconds,10.,1.e-8) &&
        FMath::IsNearlyEqual(DisarmDebrief.LastLapSplits[0].ReferenceSeconds,15.,1.e-8));

    // Simulate a gate actor being replaced between snapshots with its saved
    // identity/layout and PB restored. This is an object-reference regression,
    // not a Satisfactory streaming/save-subsystem integration test.
    NextFlight->StartCourse(World,Course,Error);
    Advance(NextFlight,{Point(-500),Point(1500)},10);
    Advance(NextFlight,{Point(1500),Point(-500)},10);
    const FGuid SavedStartId=Start->GetGateId();
    const FTransform SavedStartTransform=Start->GetActorTransform();
    const FString ReloadSignature=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
    const double SavedStartBest=Start->GetBestLapSeconds(ReloadSignature);
    TestTrue(TEXT("Reload fixture starts with a forty-second saved PB"),FMath::IsNearlyEqual(SavedStartBest,40.,1.e-8));
    Start->Destroy();
    auto* ReloadedStart=MakeGate(1,SavedStartTransform.GetLocation());
    if (!TestNotNull(TEXT("Reloaded start gate actor"),ReloadedStart)) return false;
    ReloadedStart->SetActorTransform(SavedStartTransform);
    auto* GuidProperty=FindFProperty<FStructProperty>(AFactoryFPVRaceGate::StaticClass(),TEXT("GateGuid"));
    if (!TestNotNull(TEXT("Persistent gate identity property"),GuidProperty)) return false;
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(ReloadedStart)=SavedStartId;
    ReportPower(ReloadedStart,true);
    TestTrue(TEXT("Reload fixture restores the gate-owned saved PB"),ReloadedStart->RecordLap(SavedStartBest,ReloadSignature));
    TestEqual(TEXT("Reloaded actor preserves the exact course identity/layout signature"),
        UFactoryFPVRaceComponent::LayoutSignature(World,Course),ReloadSignature);
    Advance(NextFlight,{Point(-500),Point(1500)},10);
    TestTrue(TEXT("Identical start-actor replacement preserves the current ordered lap"),NextFlight->GetRaceState().lapCount==1 &&
        FMath::IsNearlyEqual(NextFlight->GetRaceState().lastLap,20.,1.e-8));
    TestTrue(TEXT("Completed lap retargets the live start actor and persists its improved PB"),
        FMath::IsNearlyEqual(ReloadedStart->GetBestLapSeconds(ReloadSignature),20.,1.e-8));
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Telemetry reads the improved PB from the replacement start gate"),FMath::IsNearlyEqual(Data.RaceBest,20.,1.e-8) &&
        Data.RaceFinish==EFactoryFPVRaceFinish::CourseBest);
    const auto& Debrief=NextFlight->GetDebrief();
    TestTrue(TEXT("Mid-lap gate replacement does not rewrite the captured PB or split reference"),Debrief.RecentLaps.Num()==1 &&
        FMath::IsNearlyEqual(Debrief.RecentLaps[0].ReferenceSeconds,40.,1.e-8) && Debrief.LastLapSplits.Num()==1 &&
        FMath::IsNearlyEqual(Debrief.LastLapSplits[0].Seconds,5.,1.e-8) &&
        FMath::IsNearlyEqual(Debrief.LastLapSplits[0].ReferenceSeconds,10.,1.e-8));
    Start=ReloadedStart;

    constexpr int32 RetainedLaps=FFactoryFPVRaceDebrief::MaxRecentLaps;
    constexpr uint64 FinalLap=RetainedLaps+1;
    for (int32 Index=0;Index<RetainedLaps;++Index) {
        Advance(NextFlight,{Point(1500),Point(-500)},30);
        Advance(NextFlight,{Point(-500),Point(1500)},30);
    }
    TestTrue(TEXT("Debrief keeps the latest twenty rows and fastest session lap even after eviction"),Debrief.LapCount==FinalLap &&
        FMath::IsNearlyEqual(Debrief.FastestLap,20.,1.e-8) && Debrief.RecentLaps.Num()==RetainedLaps &&
        Debrief.RecentLaps[0].Number==2 && Debrief.RecentLaps.Last().Number==FinalLap);
    TestTrue(TEXT("The automatically started next lap uses the newly recorded PB and split"),Debrief.RecentLaps.Num()==RetainedLaps &&
        FMath::IsNearlyEqual(Debrief.RecentLaps[0].ReferenceSeconds,20.,1.e-8) &&
        FMath::IsNearlyEqual(Debrief.RecentLaps.Last().ReferenceSeconds,20.,1.e-8) && !Debrief.RecentLaps.Last().bCourseBest &&
        Debrief.LastLapSplits.Num()==1 && FMath::IsNearlyEqual(Debrief.LastLapSplits[0].Seconds,15.,1.e-8) &&
        FMath::IsNearlyEqual(Debrief.LastLapSplits[0].ReferenceSeconds,5.,1.e-8));

    NextFlight->StartCourse(World,Course,Error);
    TestTrue(TEXT("Reselecting the same course starts a clean debrief session"),Debrief.CourseId==Course &&
        Debrief.LapCount==0 && Debrief.FastestLap==0. && Debrief.RecentLaps.IsEmpty() && Debrief.LastLapSplits.IsEmpty());
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    Advance(NextFlight,{Point(1500),Point(-500)},20);
    auto* Incomplete=MakeGate(3,Origin+Normal*2000);
    if (!TestNotNull(TEXT("Incomplete extra gate fixture"),Incomplete)) return false;
    ReportPower(Incomplete,true);
    *GuidProperty->ContainerPtrToValuePtr<FGuid>(Incomplete)=FGuid();
    // Snapshot appends the two valid original gates before encountering this
    // higher-order invalid ID. That apparently valid prefix must never score.
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    TestTrue(TEXT("Failed snapshot cannot score its valid original-course prefix"),!NextFlight->GetRaceState().inLap &&
        !NextFlight->GetRaceState().ready && NextFlight->GetRaceState().lapCount==0);
    TestTrue(TEXT("Incomplete extra gate cannot change the stored PB"),FMath::IsNearlyEqual(Start->GetBestLapSeconds(ReloadSignature),20.,1.e-8));
    Incomplete->Destroy();

    auto* Duplicate=MakeGate(2,Origin+Normal*2000);
    if (!TestNotNull(TEXT("Duplicate-order actor fixture"),Duplicate)) return false;
    ReportPower(Duplicate,true);
    TestFalse(TEXT("Additional real actor with duplicate gate order invalidates course"),UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error));
    TestTrue(TEXT("Duplicate order guidance explains the conflict"),Error.Contains(TEXT("assigned more than once")));
    TestFalse(TEXT("Duplicate-order course cannot be selected for a new flight"),NextFlight->StartCourse(World,Course,Error));
    TestTrue(TEXT("Rejected course clears all live racing state"),NextFlight->GetCourseId()==0 && !NextFlight->GetRaceState().selected &&
        !NextFlight->GetRaceState().running && NextFlight->GetRaceState().lapCount==0);
    Duplicate->Destroy();
    NextFlight->StartCourse(World,Course,Error);
    Advance(NextFlight,{Point(-500),Point(1500)},20);
    Checkpoint->Destroy();
    AdvanceDisarmed(NextFlight,Point(1500));
    TestTrue(TEXT("Dismantled/missing checkpoint cannot preserve or finish a lap"),!NextFlight->GetRaceState().inLap &&
        NextFlight->GetRaceState().problem==factoryfpv::RaceProblem::MissingGate);
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Disarmed frame keeps the missing checkpoint guidance"),Data.bRacing &&
        Data.RaceStatus==TEXT("COURSE GATE UNAVAILABLE - RESELECT COURSE"));
    NextFlight->StopCourse();
    NextFlight->FillTelemetry(Data);
    TestTrue(TEXT("Free-flight return clears reused race telemetry"),!Data.bRacing && Data.RaceCourse==0 && Data.RaceGate==0 &&
        Data.RaceGateCount==0 && Data.RaceLaps==0 && Data.RaceElapsed==0 && Data.RaceLast==0 && Data.RaceBest==0 &&
        Data.RaceStatus.IsEmpty() && Data.RaceFinish==EFactoryFPVRaceFinish::None);
    TestTrue(TEXT("Stop clears core selection and historical run state"),!NextFlight->GetRaceState().selected && !NextFlight->GetRaceState().running &&
        NextFlight->GetRaceState().nextGate==0 && NextFlight->GetRaceState().lapCount==0 && NextFlight->GetRaceState().bestLap==0);
    TestTrue(TEXT("Stop clears the source debrief after the caller has copied it"),Debrief.CourseId==0 && Debrief.LapCount==0 &&
        Debrief.FastestLap==0. && Debrief.RecentLaps.IsEmpty() && Debrief.LastLapSplits.IsEmpty());
    TestTrue(TEXT("Explicit free-flight selection succeeds without gates"),NextFlight->StartCourse(World,0,Error));

    // A second course exercises the actual native-pole snapshot adapter and
    // public target ownership. Power remains reported SDK fixture state; the
    // path calls below are geometry/timing inputs, not physically flown laps.
    Race->StopCourse();
    auto* WideStart=World->SpawnActor<AFactoryFPVRaceGateFull>(FVector(20000,0,0),FRotator::ZeroRotator,Spawn);
    auto* Pole=World->SpawnActor<AFactoryFPVRacePole>(FVector(21000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Mixed-course full-size gate"),WideStart) || !TestNotNull(TEXT("Mixed-course native pole"),Pole)) return false;
    constexpr int32 PoleCourse=31;WideStart->EnsureIdentityAndOrder();Pole->EnsureIdentityAndOrder();
    TestTrue(TEXT("Wide start gate configures normally"),WideStart->Configure(PoleCourse,1,Error));
    TestTrue(TEXT("Native pole configures right-side passing"),Pole->Configure(PoleCourse,2,1,Error));
    ReportPower(WideStart,true);ReportPower(Pole,true);
    const FString RightSignature=UFactoryFPVRaceComponent::LayoutSignature(World,PoleCourse);
    TestTrue(TEXT("FPV3 encodes rectangular and right-pole checkpoint kinds"),RightSignature.StartsWith(TEXT("FPV3:C31;")) &&
        RightSignature.Contains(WideStart->GetGateId().ToString(EGuidFormats::Digits)+TEXT(":1:0:")) &&
        RightSignature.Contains(Pole->GetGateId().ToString(EGuidFormats::Digits)+TEXT(":2:2:")));
    auto* TargetOwner=World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Target lifecycle owner"),TargetOwner)) return false;
    auto* MixedRace=NewObject<UFactoryFPVRaceComponent>(TargetOwner);MixedRace->RegisterComponent();
    // Spawned actors in this unbegun bare world have registered components but
    // have not completed the native initialization route. RouteEndPlay requires
    // that route as well as BeginPlay; do not make the fixture bypass it.
    if (!TargetOwner->IsActorInitialized()) {
        TargetOwner->PreInitializeComponents();TargetOwner->InitializeComponents();TargetOwner->PostInitializeComponents();
    }
    if (!TestTrue(TEXT("Lifecycle owner completed native component initialization"),TargetOwner->IsActorInitialized())) return false;
    TargetOwner->DispatchBeginPlay();
    auto Highlights=[&]() { int32 Count=0;for (TActorIterator<AFactoryFPVRaceGate> It(World);It;++It) if (It->IsLiveRaceGate() && It->IsNextCheckpointHighlighted()) ++Count;return Count; };
    FVector Aim;int32 TargetOrder=0;FString Instruction;
    auto MixedPoint=[](double X,double Y) { return FVector(20000+X,Y,200); };
    TestTrue(TEXT("Mixed gate/pole course starts through public adapter"),MixedRace->StartCourse(World,PoleCourse,Error));
    TestTrue(TEXT("Exactly the start checkpoint is highlighted at selection"),Highlights()==1 && WideStart->IsNextCheckpointHighlighted() && !Pole->IsNextCheckpointHighlighted());
    TestTrue(TEXT("Public target points to actual clear gate passage"),MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && TargetOrder==1 && Aim.Equals(WideStart->GetPassageAimPoint(),.001));
    // A gate can re-enter the world as the same UObject and clear its transient
    // highlight while this component still caches that exact target pointer.
    // The public state reset models that gate-owned boundary without running
    // AFGBuildable BeginPlay against missing SDK gameplay subsystems.
    WideStart->SetNextCheckpointHighlighted(false);
    TestEqual(TEXT("Same-instance re-entry fixture actually loses its target marker"),Highlights(),0);
    Advance(MixedRace,{MixedPoint(-500,-100)},0);
    TestTrue(TEXT("Stationary update renews unchanged cached gate target after transient reset"),Highlights()==1 && WideStart->IsNextCheckpointHighlighted() &&
        MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && TargetOrder==1 && Aim.Equals(WideStart->GetPassageAimPoint(),.001));
    Advance(MixedRace,{MixedPoint(-500,-100),MixedPoint(500,-100)},1);
    TestTrue(TEXT("Crossing start transfers unique highlight to pole"),Highlights()==1 && !WideStart->IsNextCheckpointHighlighted() && Pole->IsNextCheckpointHighlighted());
    TestTrue(TEXT("Pole target gives selected-side aim and readable instruction"),MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && TargetOrder==2 && Aim.Equals(Pole->GetPassageAimPoint(),.001) && Instruction==TEXT("PASS RIGHT"));
    CheckApproach(TEXT("Exit side of slalom gives pink-face guidance and keeps the open-side aim"),MixedRace,Pole,MixedPoint(1500,100),TEXT("FIND PINK ENTRY SIDE"),true);
    CheckApproach(TEXT("Returning to the slalom entry face restores the configured pass instruction"),MixedRace,Pole,MixedPoint(500,100),TEXT("PASS RIGHT"),false);
    Pole->SetNextCheckpointHighlighted(false);Advance(MixedRace,{MixedPoint(500,-100)},0);
    TestTrue(TEXT("Unchanged cached pole target also renews unique highlight without movement or time"),Highlights()==1 && Pole->IsNextCheckpointHighlighted() &&
        MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && TargetOrder==2 && Instruction==TEXT("PASS RIGHT"));

    auto* NewCheckpoint=World->SpawnActor<AFactoryFPVRaceGate>(MixedPoint(2000,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Newly visible checkpoint fixture"),NewCheckpoint)) return false;
    NewCheckpoint->EnsureIdentityAndOrder();
    if (!TestTrue(TEXT("New checkpoint enters the active course with a duplicate order"),NewCheckpoint->Configure(PoleCourse,2,Error))) return false;
    ReportPower(NewCheckpoint,true);
    const TArray<FVector> Interrupted{MixedPoint(500,100),MixedPoint(1000,100),MixedPoint(1500,100)};
    const TArray<uint8> InterruptedScoring{0,1};
    MixedRace->AdvanceFrame(MakeArrayView(Interrupted),1,20,MakeArrayView(InterruptedScoring));
    TestTrue(TEXT("New duplicate checkpoint is detected before any segment and clears the prior pole cue"),
        MixedRace->GetRaceState().problem==factoryfpv::RaceProblem::MissingGate && !MixedRace->GetRaceState().ready &&
        !MixedRace->GetRaceState().inLap && MixedRace->GetRaceState().nextGate==1 && Highlights()==0 &&
        !MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction));
    if (!TestTrue(TEXT("New checkpoint can be renumbered to form a longer course"),NewCheckpoint->Configure(PoleCourse,3,Error))) return false;
    Advance(MixedRace,{MixedPoint(1500,100)},0);
    TestTrue(TEXT("Consecutively numbered extra gate still cannot join a live selected layout"),
        !MixedRace->GetRaceState().ready && Highlights()==0);
    TestTrue(TEXT("New checkpoint is removed from the live course"),NewCheckpoint->Destroy());
    Advance(MixedRace,{MixedPoint(1500,100)},0);
    TestTrue(TEXT("Restoring the selected layout renews only the start cue and leaves the interrupted lap canceled"),
        MixedRace->GetRaceState().ready && !MixedRace->GetRaceState().inLap && MixedRace->GetRaceState().nextGate==1 &&
        Highlights()==1 && WideStart->IsNextCheckpointHighlighted() && !Pole->IsNextCheckpointHighlighted());
    Advance(MixedRace,{MixedPoint(-500,-100),MixedPoint(0,-100),MixedPoint(500,-100)},1);
    TestTrue(TEXT("A fresh split crossing starts timing and reacquires the pole after the layout returns"),
        MixedRace->GetRaceState().inLap && MixedRace->GetRaceState().nextGate==2 && Highlights()==1 && Pole->IsNextCheckpointHighlighted());

    Advance(MixedRace,{MixedPoint(500,-100),MixedPoint(1500,-100)},1);
    TestTrue(TEXT("Native pole adapter rejects the wrong open side"),MixedRace->GetRaceState().nextGate==2 && Pole->IsNextCheckpointHighlighted());
    Advance(MixedRace,{MixedPoint(1500,-100),MixedPoint(500,-100)},1);
    Advance(MixedRace,{MixedPoint(500,-100),MixedPoint(500,0)},1);
    Advance(MixedRace,{MixedPoint(500,0),MixedPoint(1500,0)},1);
    TestEqual(TEXT("Adapter cannot score a path through solid post center"),MixedRace->GetRaceState().nextGate,2);
    Advance(MixedRace,{MixedPoint(1500,0),MixedPoint(500,0)},1);
    Advance(MixedRace,{MixedPoint(500,0),MixedPoint(500,35)},1);
    Advance(MixedRace,{MixedPoint(500,35),MixedPoint(1500,35)},1);
    TestEqual(TEXT("Centimetre conversion rejects sphere tangent to pole"),MixedRace->GetRaceState().nextGate,2);
    Advance(MixedRace,{MixedPoint(1500,35),MixedPoint(500,35)},1);
    Advance(MixedRace,{MixedPoint(500,35),MixedPoint(500,100)},1);
    Advance(MixedRace,{MixedPoint(500,100),MixedPoint(1500,100)},1);
    TestTrue(TEXT("Correct native pole side advances to start/finish and moves highlight"),MixedRace->GetRaceState().nextGate==1 && Highlights()==1 && WideStart->IsNextCheckpointHighlighted() && !Pole->IsNextCheckpointHighlighted());
    MixedRace->InvalidateLap();
    TestTrue(TEXT("Recovery points back to one start checkpoint"),!MixedRace->GetRaceState().inLap && Highlights()==1 && WideStart->IsNextCheckpointHighlighted());
    ReportPower(Pole,false);Advance(MixedRace,{MixedPoint(1500,100)},1);
    Aim=FVector(1);TargetOrder=99;Instruction=TEXT("stale");
    TestTrue(TEXT("Course power fault removes all navigation ownership and clears public output"),Highlights()==0 && !MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && Aim.IsZero() && TargetOrder==0 && Instruction.IsEmpty());
    Aim=FVector(1);TargetOrder=99;Instruction=TEXT("stale");bool bOnExitSide=true;
    TestTrue(TEXT("Unavailable approach clears every output, including the exit-side flag"),
        !MixedRace->GetNextCheckpointApproach(MixedPoint(1500,100),20.,Aim,TargetOrder,Instruction,bOnExitSide) &&
        Aim.IsZero() && TargetOrder==0 && Instruction.IsEmpty() && !bOnExitSide);
    ReportPower(Pole,true);Advance(MixedRace,{MixedPoint(1500,100)},1);
    TestTrue(TEXT("Restored power reestablishes exactly one start target"),Highlights()==1 && WideStart->IsNextCheckpointHighlighted());
    TestTrue(TEXT("Start gate can store a right-pole layout record"),WideStart->RecordLap(22,RightSignature));
    Pole->Configure(PoleCourse,2,-1,Error);
    const FString LeftSignature=UFactoryFPVRaceComponent::LayoutSignature(World,PoleCourse);
    TestTrue(TEXT("Changing native pole side changes FPV3 layout identity"),LeftSignature!=RightSignature && LeftSignature.Contains(Pole->GetGateId().ToString(EGuidFormats::Digits)+TEXT(":2:1:")));
    TestEqual(TEXT("Changed slalom side cannot borrow previous layout PB"),WideStart->GetBestLapSeconds(LeftSignature),0.);
    Advance(MixedRace,{MixedPoint(1500,100)},1);
    TestTrue(TEXT("Live side edit invalidates timing and removes misleading target"),MixedRace->GetRaceState().problem==factoryfpv::RaceProblem::ChangedCourse && Highlights()==0);
    MixedRace->StartCourse(World,PoleCourse,Error);
    Advance(MixedRace,{MixedPoint(-500,-100),MixedPoint(500,-100)},1);
    TestTrue(TEXT("Reselected pole target reflects left-side configuration"),MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) && TargetOrder==2 && Instruction==TEXT("PASS LEFT") && Aim.Equals(Pole->GetPassageAimPoint(),.001) && Highlights()==1);
    MixedRace->StopCourse();
    TestTrue(TEXT("Stopping course clears unique checkpoint ownership"),Highlights()==0 && !MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction));
    MixedRace->StartCourse(World,PoleCourse,Error);
    TestTrue(TEXT("Lifecycle fixture is initialized and began play with an active highlight"),TargetOwner->IsActorInitialized() && TargetOwner->HasActorBegunPlay() && MixedRace->HasBegunPlay() && Highlights()==1);
    TestTrue(TEXT("Native owner destruction is accepted"),TargetOwner->Destroy());
    TestTrue(TEXT("Actual component EndPlay retires target highlight and race selection"),!MixedRace->HasBegunPlay() && !MixedRace->GetRaceState().selected && Highlights()==0 && !MixedRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction));
    WideStart->Destroy();Pole->Destroy();

    // Directional scoring uses the actual native transform and course adapter.
    // The path samples are synthetic, while the gate actors and configuration
    // are real. Power continues to come from the SDK reported-state fixture.
    using Direction=EFactoryFPVRaceGateDirection;
    constexpr int32 LiftCourse=32;
    auto* LiftStart=World->SpawnActor<AFactoryFPVRaceGate>(FVector(30000,0,0),FRotator::ZeroRotator,Spawn);
    auto* LiftGate=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(32000,0,100),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Vertical course start"),LiftStart) || !TestNotNull(TEXT("Vertical course checkpoint"),LiftGate)) return false;
    const FTransform LiftPlacement(FRotator(0,90,0),FVector(32000,0,100),FVector(2,3,4));
    LiftGate->SetActorTransform(LiftPlacement);
    LiftStart->EnsureIdentityAndOrder();
    LiftGate->EnsureIdentityAndOrder();
    TestTrue(TEXT("Mixed vertical course start configures normally"),LiftStart->Configure(LiftCourse,1,Error));
    TestTrue(TEXT("Mixed vertical course selects an ascending checkpoint"),LiftGate->Configure(LiftCourse,2,1,Direction::Ascend,Error));
    ReportPower(LiftStart,true);
    ReportPower(LiftGate,true);

    const FVector LiftCenter=LiftPlacement.TransformPosition(FVector(0,0,260));
    const FVector ActorForward=LiftPlacement.GetRotation().GetAxisX();
    const FVector ActorRight=LiftPlacement.GetRotation().GetAxisY();
    const FVector ActorUp=LiftPlacement.GetRotation().GetAxisZ();
    auto CheckLiftBasis=[&](bool Ascend) {
        const auto Plane=LiftGate->GetApertureTransform();
        const double Sign=Ascend ? 1. : -1.;
        TestTrue(TEXT("Horizontal aperture maps the actor axes and their signed scales once"),
            Plane.GetLocation().Equals(LiftCenter,.001) && Plane.GetScaledAxis(EAxis::X).Equals(ActorUp*(4*Sign),.001) &&
            Plane.GetScaledAxis(EAxis::Y).Equals(ActorRight*3,.001) && Plane.GetScaledAxis(EAxis::Z).Equals(ActorForward*(-2*Sign),.001));
    };
    CheckLiftBasis(true);
    auto LiftPoint=[&](double Height,double Along=0.) { return LiftCenter+ActorUp*Height+ActorForward*Along; };
    auto* LiftRace=MakeRace();
    const FVector LiftStartCenter=LiftStart->GetApertureTransform().GetLocation();
    const FVector LiftStartEntry=LiftStartCenter-FVector(200,0,0);
    const FVector LiftStartExit=LiftStartCenter+FVector(200,0,0);
    auto StartLiftCourse=[&](const TCHAR* ExpectedInstruction) {
        if (!TestTrue(TEXT("Powered mixed vertical course starts"),LiftRace->StartCourse(World,LiftCourse,Error))) return false;
        Advance(LiftRace,{LiftStartEntry,LiftStartExit},1);
        return TestTrue(TEXT("Horizontal checkpoint target reports its exact direction and center"),
            LiftRace->GetRaceState().nextGate==2 && LiftRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction) &&
            TargetOrder==2 && Instruction==ExpectedInstruction && Aim.Equals(LiftCenter,.001));
    };
    auto FinishLiftLap=[&](const FVector& VerticalExit,double Seconds) {
        // Go around the side of the upright gate to enter the finish from the front.
        const FVector Outside(0,800,0);
        Advance(LiftRace,{VerticalExit,VerticalExit+Outside,LiftStartEntry+Outside,LiftStartEntry,LiftStartExit},Seconds);
    };
    auto CheckLiftRecord=[&](const TCHAR* Label,const FString& Layout,double Time,double Split) {
        const auto Splits=LiftStart->GetBestLapSplits(Layout,1);
        return TestTrue(Label,FMath::IsNearlyEqual(LiftStart->GetBestLapSeconds(Layout),Time,1.e-8) &&
            Splits.Num()==1 && FMath::IsNearlyEqual(Splits[0],Split,1.e-8));
    };
    if (!StartLiftCourse(TEXT("ASCEND"))) return false;
    CheckApproach(TEXT("Ascending gate tells a drone on the exit side to get below it"),LiftRace,LiftGate,LiftPoint(200),TEXT("ENTER FROM BELOW"),true);
    CheckApproach(TEXT("Ascending gate restores ASCEND from below"),LiftRace,LiftGate,LiftPoint(-200),TEXT("ASCEND"),false);
    Advance(LiftRace,{LiftPoint(200,340),LiftPoint(-200,340)},1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("A reverse pass tangent to the scaled aperture has no wrong-way warning"),
        !Data.bRaceWrongWay && Data.RaceStatus.IsEmpty() && Data.RaceGate==2);
    Advance(LiftRace,{LiftPoint(200),LiftPoint(-200)},1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Wrong-way descent tells the pilot to enter from below and keeps the ascending checkpoint selected"),
        Data.bRaceWrongWay && Data.RaceStatus==TEXT("WRONG WAY - ENTER FROM BELOW") && Data.RaceGate==2 &&
        LiftRace->GetRaceState().inLap && LiftGate->IsNextCheckpointHighlighted() && !LiftStart->IsNextCheckpointHighlighted());
    Advance(LiftRace,{LiftPoint(-200)},2.1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Wrong-way guidance expires without advancing or cancelling the active lap"),
        !Data.bRaceWrongWay && Data.RaceStatus.IsEmpty() && Data.RaceGate==2 && LiftRace->GetRaceState().inLap);
    Advance(LiftRace,{LiftPoint(-200,340),LiftPoint(200,340)},1);
    TestEqual(TEXT("Vertical passage rejects a sphere tangent to the actor-X scaled edge"),LiftRace->GetRaceState().nextGate,2);
    Advance(LiftRace,{LiftPoint(-200,339),LiftPoint(200,339)},1);
    TestEqual(TEXT("Ascending just inside that scaled edge scores the checkpoint"),LiftRace->GetRaceState().nextGate,1);

    const FString AscendKey=UFactoryFPVRaceComponent::LayoutSignature(World,LiftCourse);
    TestTrue(TEXT("Ascending course has an FPV3 record key"),AscendKey.StartsWith(TEXT("FPV3:C32;")));
    if (!StartLiftCourse(TEXT("ASCEND"))) return false;
    Advance(LiftRace,{LiftStartExit,LiftPoint(-200)},2);
    Advance(LiftRace,{LiftPoint(-200),LiftPoint(200)},2);
    FinishLiftLap(LiftPoint(200),4);
    TestTrue(TEXT("The ascending layout completes a scored lap"),LiftRace->GetRaceState().lapCount==1);
    CheckLiftRecord(TEXT("Ascending lap stores its own time and split"),AscendKey,8.,3.5);
    TestTrue(TEXT("Fixed horizontal geometry can select descent"),LiftGate->Configure(LiftCourse,2,1,Direction::Descend,Error));
    CheckLiftBasis(false);
    const FString DescendKey=UFactoryFPVRaceComponent::LayoutSignature(World,LiftCourse);
    TestTrue(TEXT("Direction-only edit changes FPV3 and keeps the previous record on its original key"),
        DescendKey.StartsWith(TEXT("FPV3:C32;")) && DescendKey!=AscendKey &&
        FMath::IsNearlyEqual(LiftStart->GetBestLapSeconds(AscendKey),8.,1.e-8) && LiftStart->GetBestLapSeconds(DescendKey)==0.);
    Advance(LiftRace,{LiftPoint(200)},1);
    TestTrue(TEXT("A live direction edit invalidates lap and stale target"),LiftRace->GetRaceState().problem==factoryfpv::RaceProblem::ChangedCourse &&
        !LiftRace->GetRaceState().inLap && !LiftRace->GetNextCheckpointTarget(Aim,TargetOrder,Instruction));
    if (!StartLiftCourse(TEXT("DESCEND"))) return false;
    CheckApproach(TEXT("Descending gate tells a drone on the exit side to get above it"),LiftRace,LiftGate,LiftPoint(-200),TEXT("ENTER FROM ABOVE"),true);
    CheckApproach(TEXT("Descending gate restores DESCEND from above"),LiftRace,LiftGate,LiftPoint(200),TEXT("DESCEND"),false);
    Advance(LiftRace,{LiftStartExit,LiftPoint(-200)},2);
    Advance(LiftRace,{LiftPoint(-200),LiftPoint(200)},1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Wrong-way ascent tells the pilot to enter from above and keeps the descending checkpoint selected"),
        Data.bRaceWrongWay && Data.RaceStatus==TEXT("WRONG WAY - ENTER FROM ABOVE") && Data.RaceGate==2 &&
        LiftRace->GetRaceState().inLap && LiftGate->IsNextCheckpointHighlighted());
    Advance(LiftRace,{LiftPoint(200),LiftPoint(-200)},1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Correct descent scores the checkpoint and clears the wrong-way warning"),
        !Data.bRaceWrongWay && Data.RaceStatus.IsEmpty() && Data.RaceGate==1 && LiftStart->IsNextCheckpointHighlighted());
    FinishLiftLap(LiftPoint(-200),8);
    TestTrue(TEXT("The descending layout completes a scored lap"),LiftRace->GetRaceState().lapCount==1);
    CheckLiftRecord(TEXT("Descending lap stores its distinct time and split"),DescendKey,11.5,4.);
    CheckLiftRecord(TEXT("Racing the descending layout retains the earlier ascending time and split"),AscendKey,8.,3.5);
    if (!StartLiftCourse(TEXT("DESCEND"))) return false;
    Advance(LiftRace,{LiftPoint(-200),LiftPoint(200)},1);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Fresh wrong-way ascent raises the warning again"),Data.bRaceWrongWay);
    AdvanceDisarmed(LiftRace,LiftPoint(200));
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Disarm retires wrong-way guidance and restores the start target"),
        !Data.bRaceWrongWay && Data.RaceStatus==TEXT("LAP CANCELLED - RESTART AT GATE 1") && Data.RaceGate==1);
    TestTrue(TEXT("Restoring ascent restores the exact previous layout"),LiftGate->Configure(LiftCourse,2,1,Direction::Ascend,Error) &&
        UFactoryFPVRaceComponent::LayoutSignature(World,LiftCourse)==AscendKey);
    if (!StartLiftCourse(TEXT("ASCEND"))) return false;
    Advance(LiftRace,{LiftStartExit,LiftPoint(-200)},2);
    Advance(LiftRace,{LiftPoint(-200),LiftPoint(200)},2);
    LiftRace->FillTelemetry(Data);
    TestTrue(TEXT("Returning to ascent uses its own saved best and matching pace"),FMath::IsNearlyEqual(Data.RaceBest,8.,1.e-8) &&
        Data.RaceSplitGate==2 && Data.bRaceSplitHasDelta && FMath::IsNearlyZero(Data.RaceSplitDelta,1.e-8));
    LiftStart->ResetBestLap(AscendKey);
    TestTrue(TEXT("Resetting ascent removes its time and split"),LiftStart->GetBestLapSeconds(AscendKey)==0. &&
        LiftStart->GetBestLapSplits(AscendKey,1).IsEmpty());
    TestNull(TEXT("Resetting ascent also removes its replay"),LiftStart->GetBestLapGhost(AscendKey));
    CheckLiftRecord(TEXT("Resetting ascent leaves the descending record intact"),DescendKey,11.5,4.);
    LiftRace->StopCourse();
    LiftGate->Destroy();
    LiftStart->Destroy();

    // Spawn a longer course out of order, then divide a frame exactly at each
    // checkpoint plane. Every segment must see the same ordered live sample.
    constexpr int32 LongCourse=33,LongGateCount=16;
    TArray<FVector> GateCenters;
    GateCenters.SetNum(LongGateCount);
    for (int32 Order=LongGateCount;Order>=1;--Order) {
        auto* Gate=World->SpawnActor<AFactoryFPVRaceGate>(FVector(45000+Order*1000,0,0),FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("Long-course checkpoint actor"),Gate)) return false;
        Gate->EnsureIdentityAndOrder();
        if (!TestTrue(TEXT("Long-course checkpoint configures"),Gate->Configure(LongCourse,Order,Error))) return false;
        ReportPower(Gate,true);
        GateCenters[Order-1]=Gate->GetApertureTransform().GetLocation();
    }
    auto* LongRace=MakeRace();
    if (!TestTrue(TEXT("Long course starts with its reversed actor creation order"),LongRace->StartCourse(World,LongCourse,Error))) return false;
    TArray<FVector> LongPath{GateCenters[0]-FVector(500,0,0)};
    LongPath.Append(GateCenters);
    LongPath.Add(GateCenters.Last()+FVector(500,0,0));
    const double LongFrameSeconds=LongPath.Num()-1;
    LongRace->AdvanceFrame(MakeArrayView(LongPath),LongFrameSeconds,20);
    TestTrue(TEXT("All split crossings in one long-course frame count in sequence"),
        LongRace->GetRaceState().gateCount==LongGateCount && LongRace->GetRaceState().inLap && LongRace->GetRaceState().nextGate==1);
    Advance(LongRace,{LongPath.Last(),LongPath[0]},LongFrameSeconds);
    LongRace->AdvanceFrame(MakeArrayView(LongPath),LongFrameSeconds,20);
    TestTrue(TEXT("Long multi-segment frame preserves exact full-lap interpolation"),
        LongRace->GetRaceState().lapCount==1 && FMath::IsNearlyEqual(LongRace->GetRaceState().lastLap,2*LongFrameSeconds,1.e-8));
    LongRace->StopCourse();

    // Exercise the drone's actual scoring decision around simulation prechecks.
    // A cutoff before integration is different from a legitimate armed segment
    // that ends in a later collision; depenetration is never a race movement.
    auto* Wall=World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("Race scoring wall fixture"),Wall)) return false;
    auto* WallBody=NewObject<UBoxComponent>(Wall);
    Wall->SetRootComponent(WallBody);Wall->AddInstanceComponent(WallBody);
    WallBody->SetBoxExtent(FVector(25,2000,2000));
    WallBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WallBody->SetCollisionObjectType(ECC_WorldStatic);
    WallBody->SetCollisionResponseToAllChannels(ECR_Block);
    WallBody->RegisterComponent();Wall->SetActorLocation(FVector(25,0,100000));
    auto* Drone=World->SpawnActor<AFactoryFPVDrone>(FVector(-500,0,100000),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Race eligibility drone"),Drone)) return false;
    auto* Settings=NewObject<UFactoryFPVSettings>();
    Settings->FrameSizeCm=24;Settings->MassKg=.65f;Settings->ThrustToWeight=4.08f;
    Settings->bCrashDamageEnabled=false;Settings->bSimulateBattery=true;
    Settings->BatteryCells=1;Settings->BatteryCapacityMah=50;Settings->BatteryMaxPowerWatts=5000;
    Drone->ApplyFlightSettings(*Settings);
    auto PlaceDrone=[&](FVector Position,factoryfpv::Vec Velocity=factoryfpv::Vec{}) {
        Drone->SetActorLocationAndRotation(Position,FRotator::ZeroRotator);
        Drone->Flight.reset({Position.X/100.,Position.Y/100.,Position.Z/100.});
        Drone->Flight.velocity=Velocity;
    };
    PlaceDrone(FVector(-500,0,100000),{1,0,0});
    const factoryfpv::Input Idle{0,0,0,0,true};
    const double Step=factoryfpv::StepSeconds;
    TestTrue(TEXT("Charged eligibility fixture can arm before the final battery step"),Drone->TryArmFlight(Idle));
    Drone->Flight.thrust=1;
    bool NextStepEmpties=false;
    for (int32 Index=0;Index<1000;++Index) {
        auto Predicted=Drone->Battery;
        Predicted.step(Step,true,1);
        if (Predicted.empty()) { NextStepEmpties=true;break; }
        Drone->Battery.step(Step,true,1);
    }
    TestTrue(TEXT("Battery fixture is nonempty but the next real substep will empty it"),NextStepEmpties && !Drone->Battery.empty() && Drone->Flight.armed);
    const bool CutoffEligible=Drone->SimulateFlight({0,0,0,1,true},Step);
    TestTrue(TEXT("Battery cutoff before integration cannot score its inertial movement"),!CutoffEligible && Drone->Battery.empty() && !Drone->Flight.armed);
    Settings->bSimulateBattery=false;
    Drone->ApplyFlightSettings(*Settings);
    PlaceDrone(FVector(-500,0,100000));
    TestTrue(TEXT("Ordinary eligibility fixture can arm"),Drone->TryArmFlight(Idle));
    TestTrue(TEXT("Ordinary armed integration is eligible for swept gate scoring"),Drone->SimulateFlight(Idle,Step));
    PlaceDrone(FVector(-12.5,0,100000),{12,0,0});
    TestTrue(TEXT("Hard-contact eligibility fixture arms outside wall penetration"),Drone->TryArmFlight(Idle));
    const bool CollisionEligible=Drone->SimulateFlight(Idle,Step);
    TestTrue(TEXT("Later hard collision does not erase the preceding armed segment"),CollisionEligible && !Drone->Flight.armed &&
        Drone->GetActorLocation().X>-12.6 && Drone->GetActorLocation().X<=-11.5);
    PlaceDrone(FVector(-10,0,100000));
    TestTrue(TEXT("Initial-overlap eligibility fixture can arm"),Drone->TryArmFlight(Idle));
    const bool PenetrationEligible=Drone->SimulateFlight({0,0,0,.5,true},Step);
    TestTrue(TEXT("Actual penetration correction cannot manufacture a gate crossing"),!PenetrationEligible && Drone->Movement->bAdjustedForPenetration);
    AddInfo(TEXT("Actual gate transforms, reported power predicates, component swept-path timing and gate-owned records tested. Real grid delivery, construction, Satisfactory save/reload and physical racing need shipping acceptance."));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRaceSavePrecisionTest,
    "FactoryFPV.Racing.NativeSaveTransformPreservesRecordKey",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRaceSavePrecisionTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Save-precision race world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AFactoryFPVRaceGate* Gates[]={
        World->SpawnActor<AFactoryFPVRaceGate>(FVector(0,0,0),FRotator::ZeroRotator,Spawn),
        World->SpawnActor<AFactoryFPVRacePole>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn),
        World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(2000,0,0),FRotator::ZeroRotator,Spawn)
    };
    FString Error;
    constexpr int32 Course=19;
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Gates);++Index) {
        if (!TestNotNull(TEXT("Save-precision gate"),Gates[Index])) return false;
        Gates[Index]->EnsureIdentityAndOrder();
        TestTrue(TEXT("Save-precision gate configured"),Gates[Index]->Configure(Course,Index+1,Error));
    }
    // Exercise the real inline native save-header conversion, not a manually
    // rounded string or generated save-session stub. Full disk persistence is
    // covered separately by the disposable Shipping save/reload workflow.
    const FRotator Rotations[]={FRotator(0,225,0),FRotator(13.23456789,126.98765432,-9.87654321),FRotator(-38.7654321,-71.2345678,28.1234567)};
    using Direction=EFactoryFPVRaceGateDirection;
    for (int32 Case=0;Case<UE_ARRAY_COUNT(Rotations);++Case) {
        TestTrue(TEXT("Save-precision fixture includes a real native pole class"),Gates[1]->IsPoleCheckpoint());
        TestTrue(TEXT("Save-precision pole configures either passing side"),Gates[1]->Configure(Course,2,Case%2==0 ? 1 : -1,Error));
        TestTrue(TEXT("Save-precision vertical gate configures either direction"),Gates[2]->IsVerticalRaceGate() &&
            Gates[2]->Configure(Course,3,1,Case%2==0 ? Direction::Ascend : Direction::Descend,Error));
        for (int32 Index=0;Index<UE_ARRAY_COUNT(Gates);++Index) Gates[Index]->SetActorTransform(FTransform(Rotations[Case],
            FVector(-54255.0398562-Index*800.1234567,230314.0968626+Index*743.9876543,-2284.8086909-Index*1021.6285039),
            FVector(1.234567891+Case*.1,.876543219+Index*.05,1.345678912)));
        const FTransform LiveBefore=Gates[0]->GetActorTransform();
        const FString Before=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
        TestTrue(TEXT("Save-compatible key has an explicit new format version"),Before.StartsWith(TEXT("FPV3:C19;")));
        TestTrue(TEXT("Generating the record key does not snap the live actor"),Gates[0]->GetActorTransform().Equals(LiveBefore,0.));
        Gates[0]->ResetBestLap(Before);
        TestTrue(TEXT("Fractional layout records its own PB"),Gates[0]->RecordLap(20.,Before));
        for (int32 Roundtrip=0;Roundtrip<3;++Roundtrip) {
            for (auto* Gate:Gates) {
                FActorSaveHeader Header;
                TestTrue(TEXT("Native actor save header accepts the gate"),Header.Set(Gate));
                if (Roundtrip==0) TestFalse(TEXT("Fixture actually loses unsaved double pose precision"),
                    Gate->GetActorTransform().Equals(FTransform(Header.Transform),0.));
                Gate->SetActorTransform(FTransform(Header.Transform));
            }
            const FString After=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
            TestEqual(FString::Printf(TEXT("Case %d native float-transform roundtrip %d keeps the record key"),Case,Roundtrip),After,Before);
            TestEqual(TEXT("Saved-layout PB remains readable through the current layout"),Gates[0]->GetBestLapSeconds(After),20.);
        }
        Gates[1]->SetActorLocation(Gates[1]->GetActorLocation()+FVector(.5,0,0));
        const FString Moved=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
        TestTrue(TEXT("Actual movement beyond native float-save precision changes the record key"),Moved!=Before);
        TestEqual(TEXT("Moved layout cannot reuse the previous record"),Gates[0]->GetBestLapSeconds(Moved),0.);
    }
    return true;
}
#endif
