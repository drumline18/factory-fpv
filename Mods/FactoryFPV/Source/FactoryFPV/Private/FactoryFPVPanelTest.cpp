#include "FactoryFPVPanel.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVDrone.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRacePanel.h"
#include "FactoryFPVRaceComponent.h"
#include "FGPowerInfoComponent.h"
#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVGateDraftLaunchTest,
    "FactoryFPV.Interface.HangarLaunchPreservesGateDraft",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVGateDraftLaunchTest::RunTest(const FString&) {
    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Gate-draft launch world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    auto Overlay=SNew(SOverlay);Viewport->SetViewportOverlayWidget(nullptr,Overlay);
    GEngine->GetWorldContextFromWorld(World)->GameViewport=Viewport;
    auto* WorldProperty=FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("World"));
    if (!TestNotNull(TEXT("Viewport's native world property"),WorldProperty)) return false;
    WorldProperty->SetObjectPropertyValue_InContainer(Viewport,World);
    auto* Player=NewObject<ULocalPlayer>(GEngine);
    Player->PlayerAdded(Viewport,0);
    ON_SCOPE_EXIT { Player->PlayerRemoved(); };
    auto* PC=World->SpawnActor<APlayerController>();PC->SetPlayer(Player);
    auto* Hangar=Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
    if (!TestNotNull(TEXT("Real registered local-player hangar subsystem"),Hangar)) return false;
    if (!TestTrue(TEXT("Real hangar fixture binds"),Hangar->BindController(PC,Viewport))) return false;
    const int32 BaselineOverlays=Overlay->GetChildren()->Num();
    const auto ExpectDirectRefusal=[&](const TCHAR* Context,const TCHAR* ExpectedError) {
        const FString Label(Context);
        const auto BeforeLease=FactoryFPVInputLease::GetDiagnostics(PC);
        auto* const ViewTarget=PC->GetViewTarget();
        const bool bCursor=PC->bShowMouseCursor,bViewportInputIgnored=Viewport->IgnoreInput();
        const bool bMoveIgnored=PC->IsMoveInputIgnored(),bLookIgnored=PC->IsLookInputIgnored();
        const int32 OverlayCount=Overlay->GetChildren()->Num();
        int32 SpawnedDrones=0;
        const auto SpawnObserver=World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor) {
            if (Actor && Actor->IsA<AFactoryFPVDrone>()) ++SpawnedDrones;
        }));
        ON_SCOPE_EXIT { World->RemoveOnActorSpawnedHandler(SpawnObserver); };
        FString LaunchError;
        TestNull(*(Label+TEXT(" refuses the direct launch")),AFactoryFPVDrone::Launch(PC,TEXT("keyboard"),LaunchError));
        TestEqual(*(Label+TEXT(" reports the expected guard")),LaunchError,FString(ExpectedError));
        TestEqual(*(Label+TEXT(" refuses before spawning a drone")),SpawnedDrones,0);
        TestEqual(*(Label+TEXT(" preserves the view target")),PC->GetViewTarget(),ViewTarget);
        const auto AfterLease=FactoryFPVInputLease::GetDiagnostics(PC);
        TestTrue(*(Label+TEXT(" preserves cursor, overlay and input ownership")),
            PC->bShowMouseCursor==bCursor && Viewport->IgnoreInput()==bViewportInputIgnored &&
            PC->IsMoveInputIgnored()==bMoveIgnored && PC->IsLookInputIgnored()==bLookIgnored &&
            Overlay->GetChildren()->Num()==OverlayCount && BeforeLease.ActiveLeases==AfterLease.ActiveLeases &&
            BeforeLease.OwnedMoveContributions==AfterLease.OwnedMoveContributions &&
            BeforeLease.OwnedLookContributions==AfterLease.OwnedLookContributions);
    };
    const TCHAR* PanelLaunchError=TEXT("Close the Factory FPV panel or use Save & fly from the hangar.");
    FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Gate=World->SpawnActor<AFactoryFPVRaceGate>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Editable native gate"),Gate)) return false;
    Gate->EnsureIdentityAndOrder();FString Error;
    TestTrue(TEXT("Editable gate configured"),Gate->Configure(1,1,Error));
    auto* OtherGate=World->SpawnActor<AFactoryFPVRaceGate>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Selected course's second real gate"),OtherGate)) return false;
    OtherGate->EnsureIdentityAndOrder();
    TestTrue(TEXT("Second gate configured"),OtherGate->Configure(1,2,Error));
    TestTrue(TEXT("Course layout is valid before its gate disappears"),UFactoryFPVRaceComponent::ValidateCourse(World,1,Error,false));
    AddInfo(TEXT("Selected-course UI regression uses real gate destruction. SDK circuit delivery is stubbed, so only initial layout validity skips power; no power reports or input counters are fabricated."));
    Hangar->SetSelectedRacingCourse(1);
    Hangar->OpenPanel();
    auto Panel=Hangar->Panel;
    if (!TestNotNull(TEXT("Actual outer hangar panel"),Panel.Get()) || !TestNotNull(TEXT("Retained embedded racing panel"),Panel->RacingPanel.Get())) return false;
    Panel->Radio.reset();Panel->InputMode=Panel->InputModes[1];Panel->Draft->bAllowSandboxAccess=true;
    const float SavedTilt=GetDefault<UFactoryFPVSettings>()->CameraTilt;
    ON_SCOPE_EXIT { GetMutableDefault<UFactoryFPVSettings>()->CameraTilt=SavedTilt; };
    Panel->Draft->CameraTilt=SavedTilt==42.f ? 41.f : 42.f;
    const float DraftTilt=Panel->Draft->CameraTilt;
    Panel->SelectPage(SFactoryFPVPanel::EPage::Racing);
    TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>,const FString&)> FindButton;
    FindButton=[&](TSharedRef<SWidget> Widget,const FString& Text)->TSharedPtr<SButton> {
        auto* Children=Widget->GetChildren();
        if (Widget->GetTypeAsString()==TEXT("SButton") && Children->Num()==1 && Children->GetChildAt(0)->GetTypeAsString()==TEXT("STextBlock") &&
            StaticCastSharedRef<STextBlock>(Children->GetChildAt(0))->GetText().ToString()==Text) return StaticCastSharedRef<SButton>(Widget);
        for (int32 Index=0;Index<Children->Num();++Index) if (auto Found=FindButton(Children->GetChildAt(Index),Text)) return Found;
        return nullptr;
    };
    auto Edit=FindButton(Panel->RacingPanel.ToSharedRef(),TEXT("Edit"));
    if (!TestNotNull(TEXT("Gate row's actual Edit button"),Edit.Get())) return false;
    Edit->SimulateClick();
    TFunction<TSharedPtr<SSpinBox<int32>>(TSharedRef<SWidget>)> FindNumber;
    FindNumber=[&](TSharedRef<SWidget> Widget)->TSharedPtr<SSpinBox<int32>> {
        // The browser and the checkpoint editor both have course spin boxes.
        if (Widget->GetTag()==FName(TEXT("FactoryFPVRaceDraftCourse")) && Widget->GetTypeAsString()==TEXT("SSpinBox<int32>"))
            return StaticCastSharedRef<SSpinBox<int32>>(Widget);
        auto* Children=Widget->GetChildren();
        for (int32 Index=0;Index<Children->Num();++Index) if (auto Found=FindNumber(Children->GetChildAt(Index))) return Found;
        return nullptr;
    };
    auto Number=FindNumber(Panel->RacingPanel.ToSharedRef());
    if (!TestNotNull(TEXT("Actual gate course numeric control"),Number.Get())) return false;
    Number->OnKeyDown(FGeometry(),FKeyEvent(EKeys::Up,FModifierKeysState(),0,false,0,0));
    const int32 DraftCourse=Number->GetValue();
    if (!TestTrue(TEXT("Numeric key interaction creates a real unapplied gate draft"),Panel->RacingPanel->HasPendingGateEdits() && DraftCourse!=Gate->GetCourseId())) return false;
    ExpectDirectRefusal(TEXT("Dirty hangar"),PanelLaunchError);
    TestTrue(TEXT("Direct launch preserves the same hangar, tuning and gate draft"),
        Hangar->Panel==Panel && Panel->RacingPanel->HasPendingGateEdits() && Number->GetValue()==DraftCourse &&
        Panel->Draft->CameraTilt==DraftTilt && GetDefault<UFactoryFPVSettings>()->CameraTilt==SavedTilt &&
        Hangar->GetSelectedRacingCourse()==1 && Gate->GetCourseId()==1);
    auto FlyButton=FindButton(Panel.ToSharedRef(),TEXT("Save & fly"));
    if (!TestNotNull(TEXT("Outer Save & fly button"),FlyButton.Get())) return false;
    Panel->SlatePrepass();
    TestTrue(TEXT("Launch regression exercises an enabled real button"),FlyButton->IsEnabled());
    const int32 OpenOverlays=Overlay->GetChildren()->Num();
    {
        auto* Pauser=World->SpawnActor<APlayerState>();
        auto* WorldSettings=World->GetWorldSettings();
        if (!TestNotNull(TEXT("Pause owner for Save & fly"),Pauser) || !TestNotNull(TEXT("Native world settings"),WorldSettings)) return false;
        WorldSettings->SetPauserPlayerState(Pauser);
        ON_SCOPE_EXIT { WorldSettings->SetPauserPlayerState(nullptr); Pauser->Destroy(); };
        FlyButton->SimulateClick();
        TestTrue(TEXT("Save & fly preserves the same draft and panel while the native pause owns input"),
            Hangar->Panel==Panel && Panel->Message.Contains(TEXT("resume the game")) && Panel->Draft->CameraTilt==DraftTilt &&
            GetDefault<UFactoryFPVSettings>()->CameraTilt==SavedTilt && Panel->RacingPanel->HasPendingGateEdits() &&
            Overlay->GetChildren()->Num()==OpenOverlays && !AFactoryFPVDrone::FindActive(World));
    }
    FlyButton->SimulateClick();
    TestTrue(TEXT("Actual Fly handler retains the same open panel and input ownership"),Hangar->Panel==Panel && Hangar->IsPanelOpen() && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    TestEqual(TEXT("Blocked launch keeps its viewport overlay"),Overlay->GetChildren()->Num(),OpenOverlays);
    TestTrue(TEXT("Blocked launch explains Apply/Revert"),Panel->Message.Contains(TEXT("Apply or revert")));
    TestEqual(TEXT("Launch cannot commit tuning before gate edits are resolved"),GetDefault<UFactoryFPVSettings>()->CameraTilt,SavedTilt);
    TestEqual(TEXT("Tuning draft survives blocked launch"),Panel->Draft->CameraTilt,DraftTilt);
    TestTrue(TEXT("Gate draft and selected course survive blocked launch"),Panel->RacingPanel->HasPendingGateEdits() && Number->GetValue()==DraftCourse && Hangar->GetSelectedRacingCourse()==1);
    TestEqual(TEXT("Unapplied native gate settings remain unchanged"),Gate->GetCourseId(),1);
    TestNull(TEXT("Blocked launch never creates a drone"),AFactoryFPVDrone::FindActive(World));
    auto Revert=FindButton(Panel->RacingPanel.ToSharedRef(),TEXT("Revert"));
    if (!TestNotNull(TEXT("Actual gate Revert button"),Revert.Get())) return false;
    Panel->RacingPanel->SlatePrepass();Revert->SimulateClick();
    TestFalse(TEXT("Explicit Revert releases the launch draft guard"),Panel->RacingPanel->HasPendingGateEdits());
    Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);
    const auto ChosenInput=Panel->InputModes[1];Panel->InputMode=ChosenInput;
    const auto ChosenPreset=Panel->TuningOptions[0];Panel->SelectedTuning=ChosenPreset;
    Panel->TuningName=TEXT("Retained race tune");

    auto* Third=World->SpawnActor<AFactoryFPVRaceGate>(FVector(2000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Third checkpoint for an otherwise valid shortened layout"),Third)) return false;
    Third->EnsureIdentityAndOrder();
    if (!TestTrue(TEXT("Third checkpoint is ordered last"),Third->Configure(1,3,Error))) return false;
    auto* Circuit=FindFProperty<FIntProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mCircuitID"));
    auto* Powered=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mHasPower"));
    if (!TestNotNull(TEXT("Reported circuit field"),Circuit) || !TestNotNull(TEXT("Reported power field"),Powered)) return false;
    for (auto* Checkpoint:{Gate,OtherGate,Third}) {
        Circuit->SetPropertyValue_InContainer(Checkpoint->GetPowerInfo(),17);
        Powered->SetPropertyValue_InContainer(Checkpoint->GetPowerInfo(),true);
    }
    if (!TestTrue(TEXT("Three-checkpoint course is powered and valid"),UFactoryFPVRaceComponent::ValidateCourse(World,1,Error))) return false;
    Hangar->SetSelectedRacingCourse(1);
    const FString SelectedLayout=Hangar->GetSelectedRacingLayout();
    if (!TestTrue(TEXT("Selection captures the complete layout"),!SelectedLayout.IsEmpty()) ||
        !TestTrue(TEXT("Last checkpoint disappears"),Third->Destroy())) return false;
    TestTrue(TEXT("Remaining two-checkpoint course is independently valid"),UFactoryFPVRaceComponent::ValidateCourse(World,1,Error));
    auto* Timing=NewObject<UFactoryFPVRaceComponent>(World);
    TestFalse(TEXT("Native race start refuses a different valid course with the same ID"),Timing->StartCourse(World,1,Error,&SelectedLayout));
    TestTrue(TEXT("Refused start requests reselect without leaving race state or a pink cue"),Error.Contains(TEXT("layout changed")) &&
        Timing->GetCourseId()==0 && !Gate->IsNextCheckpointHighlighted());
    Panel->SlatePrepass();
    FlyButton->SimulateClick();
    TestTrue(TEXT("Launch preflight preserves the open draft and requests reselect for a valid changed layout"),
        Hangar->Panel==Panel && Panel->ActivePage==SFactoryFPVPanel::EPage::Racing && Panel->Message.Contains(TEXT("checkpoint layout changed")) &&
        Hangar->GetSelectedRacingLayout()==SelectedLayout && Panel->HasUnsavedChanges() && Panel->Draft->CameraTilt==DraftTilt &&
        !AFactoryFPVDrone::FindActive(World));
    Hangar->SetSelectedRacingCourse(1);
    TestTrue(TEXT("Explicit reselect accepts the shorter layout"),Hangar->GetSelectedRacingLayout()!=SelectedLayout &&
        Timing->StartCourse(World,1,Error,&Hangar->GetSelectedRacingLayout()));
    Timing->StopCourse();
    Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);

    if (!TestTrue(TEXT("Previously selected course actually loses its second actor"),OtherGate->Destroy())) return false;
    TestFalse(TEXT("Production validator now rejects that same selected course"),UFactoryFPVRaceComponent::ValidateCourse(World,Hangar->GetSelectedRacingCourse(),Error,true));
    Panel->SlatePrepass();
    TestTrue(TEXT("Stale-course regression clicks an actual enabled launch button"),FlyButton->IsEnabled());
    FlyButton->SimulateClick();
    TestTrue(TEXT("A missing selected-course gate retains the identical open hangar"),Hangar->Panel==Panel && Hangar->IsPanelOpen());
    TestEqual(TEXT("Course preflight leaves the viewport overlay intact"),Overlay->GetChildren()->Num(),OpenOverlays);
    TestTrue(TEXT("Rejected course launch preserves keyboard mode and preset selection"),Panel->InputMode==ChosenInput && Panel->SelectedTuning==ChosenPreset && Panel->TuningName==TEXT("Retained race tune"));
    TestEqual(TEXT("Rejected course launch preserves the tuning draft"),Panel->Draft->CameraTilt,DraftTilt);
    TestEqual(TEXT("Rejected course launch does not commit default tuning"),GetDefault<UFactoryFPVSettings>()->CameraTilt,SavedTilt);
    TestTrue(TEXT("Rejected course launch keeps the unsaved draft dirty"),Panel->HasUnsavedChanges() && Panel->SavedDraft->CameraTilt==SavedTilt);
    TestTrue(TEXT("Stale-course rejection routes to Racing with the selected course and remedy"),
        Panel->ActivePage==SFactoryFPVPanel::EPage::Racing && Panel->Message.Contains(TEXT("Selected course 1")) &&
        Panel->Message.Contains(TEXT("Only one checkpoint is loaded")) && Panel->Message.Contains(TEXT("Move near the rest or add another")) &&
        Panel->Message.Contains(TEXT("Free flight")));
    TestEqual(TEXT("Course fault never silently selects free flight"),Hangar->GetSelectedRacingCourse(),1);
    TestTrue(TEXT("Rejected course launch retains panel input ownership"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    TestNull(TEXT("Course preflight failure never spawns a drone"),AFactoryFPVDrone::FindActive(World));
    Panel->InputMode=Panel->InputModes[0];Panel->bConnected=false;Panel->Fly();
    TestTrue(TEXT("Resolved draft reaches ordinary radio validation without committing"),
        Panel->ActivePage==SFactoryFPVPanel::EPage::Radio && !Panel->RadioReady(Error));
    Number->OnKeyDown(FGeometry(),FKeyEvent(EKeys::Up,FModifierKeysState(),0,false,0,0));
    TestTrue(TEXT("A new unapplied gate draft remains editable"),Panel->RacingPanel->HasPendingGateEdits());
    Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);
    TestTrue(TEXT("The hangar reports hidden checkpoint edits outside Racing"),Panel->DraftStatus().ToString().Contains(TEXT("gate edits")));
    auto Close=FindButton(Panel.ToSharedRef(),TEXT("Close   Esc"));
    if (!TestNotNull(TEXT("Actual Close button"),Close.Get())) return false;
    const auto GetPromptButton=[&](const TCHAR* Label) { return FindButton(Overlay,Label); };
    Close->SimulateClick();
    auto Keep=GetPromptButton(TEXT("Keep editing"));
    if (!TestNotNull(TEXT("Closing a dirty hangar shows its keep button"),Keep.Get())) return false;
    TestTrue(TEXT("Close confirmation uses the same hangar, overlay and lease"),Hangar->Panel==Panel &&
        Overlay->GetChildren()->Num()==OpenOverlays && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
    Keep->SimulateClick();
    TestTrue(TEXT("Keep editing restores the same tab and both drafts"),Hangar->Panel==Panel &&
        Panel->ActivePage==SFactoryFPVPanel::EPage::Flight && Panel->RacingPanel->HasPendingGateEdits() &&
        Panel->Draft->CameraTilt==DraftTilt && !GetPromptButton(TEXT("Keep editing")));
    const FKeyEvent Escape(EKeys::Escape,FModifierKeysState(),0,false,0,0);
    TestTrue(TEXT("Escape requests confirmation"),Panel->OnKeyDown(FGeometry(),Escape).IsEventHandled());
    auto Discard=GetPromptButton(TEXT("Discard edits"));
    if (!TestNotNull(TEXT("Actual explicit discard button"),Discard.Get())) return false;
    Discard->SimulateClick();
    TestFalse(TEXT("Explicit discard releases the hangar"),Hangar->IsPanelOpen());
    TestEqual(TEXT("Explicit discard restores the overlay baseline"),Overlay->GetChildren()->Num(),BaselineOverlays);
    TestFalse(TEXT("Explicit discard releases input ownership"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    TestEqual(TEXT("Explicit discard never writes settings"),GetDefault<UFactoryFPVSettings>()->CameraTilt,SavedTilt);
    TestEqual(TEXT("Explicit discard never applies the hidden checkpoint edits"),Gate->GetCourseId(),1);

    Hangar->OpenRaceGatePanel(Gate);
    const auto CheckpointEditor=Hangar->RacePanel;
    if (!TestNotNull(TEXT("Actual standalone checkpoint editor"),CheckpointEditor.Get())) return false;
    TestTrue(TEXT("Standalone editor retains the queue-only course action"),
        FindButton(CheckpointEditor.ToSharedRef(),TEXT("Race course")).IsValid() &&
        !FindButton(CheckpointEditor.ToSharedRef(),TEXT("Save & race")));
    ExpectDirectRefusal(TEXT("Standalone checkpoint editor"),PanelLaunchError);
    TestTrue(TEXT("Direct launch preserves the standalone editor"),Hangar->RacePanel==CheckpointEditor && !Hangar->IsHangarOpen());
    Hangar->ClosePanel();

    {
        auto* Pauser=World->SpawnActor<APlayerState>();
        auto* WorldSettings=World->GetWorldSettings();
        if (!TestNotNull(TEXT("Native world pause owner"),Pauser) || !TestNotNull(TEXT("Native world settings"),WorldSettings)) return false;
        auto* Console=NewObject<UConsole>(Viewport);
        Viewport->ViewportConsole=Console;
        Console->ConsoleState=TEXT("Typing");
        WorldSettings->SetPauserPlayerState(Pauser);
        ON_SCOPE_EXIT { WorldSettings->SetPauserPlayerState(nullptr); Viewport->ViewportConsole=nullptr; };
        if (!TestTrue(TEXT("Controller has a real engine pause while the console is open"),PC->IsPaused())) return false;
        ExpectDirectRefusal(TEXT("Paused game with console open"),TEXT("Close the game menu or resume the game before launching FPV."));
        WorldSettings->SetPauserPlayerState(nullptr);
        TestFalse(TEXT("World resumes with the console still open"),PC->IsPaused());
        // This fixture has no native living pioneer. Reaching that ordinary
        // guard proves that an open console alone does not block fpv.start.
        ExpectDirectRefusal(TEXT("Unpaused developer console"),TEXT("Launch requires a living pioneer in a single-player world."));
    }

    Hangar->SetSelectedRacingCourse(0);
    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const TStrongObjectPtr<UFactoryFPVSettings> OriginalSettings(DuplicateObject<UFactoryFPVSettings>(Settings,GetTransientPackage()));
    ON_SCOPE_EXIT {
        for (TFieldIterator<FProperty> Property(Settings->GetClass());Property;++Property)
            if (Property->HasAnyPropertyFlags(CPF_Config)) Property->CopyCompleteValue_InContainer(Settings,OriginalSettings.Get());
    };
    FConfigCacheIni MemoryOnlyConfig(EConfigCacheType::Temporary);
    {
        Settings->PreferredInputMode=TEXT("radio");
        Hangar->OpenPanel();
        const auto Attempt=Hangar->Panel;
        if (!TestNotNull(TEXT("Ordinary new hangar"),Attempt.Get())) return false;
        TestTrue(TEXT("Ordinary new hangar still defaults to USB radio"),Attempt->InputMode==Attempt->InputModes[0]);
        Attempt->Draft->bAllowSandboxAccess=true;
        Attempt->InputModeCombo->SetSelectedItem(Attempt->InputModes[1]);
        const auto AttemptButton=FindButton(Attempt.ToSharedRef(),TEXT("Save & fly"));
        if (!TestNotNull(TEXT("Actual fallback launch button"),AttemptButton.Get())) return false;
        Attempt->SlatePrepass();
        TestTrue(TEXT("Runtime launch refusal regression clicks an enabled button"),AttemptButton->IsEnabled());
        {
            // Save & fly commits before launching. Keep that write in memory
            // while the real runtime launch guard refuses the pawnless PC.
            TGuardValue<FConfigCacheIni*> ConfigGuard(GConfig,&MemoryOnlyConfig);
            AttemptButton->SimulateClick();
        }
        const auto Retry=Hangar->Panel;
        if (!TestNotNull(TEXT("Runtime refusal reopens the actual hangar"),Retry.Get())) return false;
        TestTrue(TEXT("Retry shows the runtime refusal on a new panel"),Retry!=Attempt && Retry->Message.Contains(TEXT("Launch requires a living pioneer")));
        TestTrue(TEXT("Retry preserves the attempted Keyboard selection"),Retry->InputMode==Retry->InputModes[1]);
        TestEqual(TEXT("Save & fly commits the explicitly selected input mode"),Settings->PreferredInputMode,FName(TEXT("keyboard")));
        TestTrue(TEXT("Retry owns the hangar overlay and input"),Overlay->GetChildren()->Num()==OpenOverlays && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
        TestNull(TEXT("Runtime refusal leaves no active drone"),AFactoryFPVDrone::FindActive(World));
        Hangar->ClosePanel();
        TestTrue(TEXT("Closing the retry releases overlay and input ownership"),Overlay->GetChildren()->Num()==BaselineOverlays && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored());
    }
    {
        auto* Finish=World->SpawnActor<AFactoryFPVRaceGate>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("New finishing checkpoint for direct race launch"),Finish)) return false;
        Finish->EnsureIdentityAndOrder();
        if (!TestTrue(TEXT("Direct race fixture has ordered checkpoints"),Finish->Configure(1,2,Error))) return false;
        Circuit->SetPropertyValue_InContainer(Finish->GetPowerInfo(),17);
        Powered->SetPropertyValue_InContainer(Finish->GetPowerInfo(),true);
        if (!TestTrue(TEXT("Direct race fixture has a ready course"),UFactoryFPVRaceComponent::ValidateCourse(World,1,Error))) return false;

        Hangar->OpenPanel();
        const auto Attempt=Hangar->Panel;
        if (!TestNotNull(TEXT("Direct race uses the real hangar"),Attempt.Get())) return false;
        Attempt->Draft->bAllowSandboxAccess=true;
        Attempt->SelectPage(SFactoryFPVPanel::EPage::Racing);
        const auto Race=FindButton(Attempt.ToSharedRef(),TEXT("Save & race"));
        if (!TestNotNull(TEXT("Racing tab shows its direct launch button"),Race.Get())) return false;
        const auto Practice=FindButton(Attempt.ToSharedRef(),TEXT("Save & practice"));
        if (!TestNotNull(TEXT("Racing tab shows its practice launch button"),Practice.Get())) return false;
        TestFalse(TEXT("Racing tab does not show a second course action"),FindButton(Attempt.ToSharedRef(),TEXT("Race course")).IsValid());

        // The browser still views the ready course it opened on. A separate
        // stale next-flight selection must never override the direct action.
        Hangar->SetSelectedRacingCourse(2);
        Attempt->InputModeCombo->SetSelectedItem(Attempt->InputModes[0]);
        Attempt->Radio.reset();Attempt->bConnected=false;
        Attempt->SlatePrepass();
        TestTrue(TEXT("A ready viewed course can attempt a direct launch"),Race->IsEnabled());
        Practice->SimulateClick();
        TestTrue(TEXT("Practice radio preflight keeps the same hangar, queue and tuning draft"),
            Hangar->Panel==Attempt && Attempt->ActivePage==SFactoryFPVPanel::EPage::Radio &&
            !Attempt->RadioReady(Error) && Hangar->GetSelectedRacingCourse()==2 &&
            Settings->PreferredInputMode==TEXT("keyboard") && !AFactoryFPVDrone::FindActive(World));

        Attempt->InputModeCombo->SetSelectedItem(Attempt->InputModes[1]);
        Attempt->SelectPage(SFactoryFPVPanel::EPage::Racing);
        Attempt->SlatePrepass();
        Powered->SetPropertyValue_InContainer(Finish->GetPowerInfo(),false);
        Practice->SimulateClick();
        Race->Invalidate(EInvalidateWidgetReason::Layout);
        Practice->Invalidate(EInvalidateWidgetReason::Layout);
        Attempt->SlatePrepass();
        TestTrue(TEXT("A course losing power before the click is rechecked without replacing the queue"),
            Hangar->Panel==Attempt && !Race->IsEnabled() && !Practice->IsEnabled() &&
            Hangar->GetSelectedRacingCourse()==2 && !AFactoryFPVDrone::FindActive(World));
        Powered->SetPropertyValue_InContainer(Finish->GetPowerInfo(),true);
        const auto Refresh=FindButton(Attempt->RacingPanel.ToSharedRef(),TEXT("Refresh"));
        if (!TestNotNull(TEXT("Actual course refresh button"),Refresh.Get())) return false;
        Refresh->SimulateClick();
        Race->Invalidate(EInvalidateWidgetReason::Layout);
        Practice->Invalidate(EInvalidateWidgetReason::Layout);
        Attempt->SlatePrepass();
        if (!TestTrue(TEXT("Restored course power allows either launch"),Race->IsEnabled() && Practice->IsEnabled())) return false;
        {
            TGuardValue<FConfigCacheIni*> ConfigGuard(GConfig,&MemoryOnlyConfig);
            Race->SimulateClick();
        }
        const auto Retry=Hangar->Panel;
        if (!TestNotNull(TEXT("Direct race shares the ordinary launch-error recovery"),Retry.Get())) return false;
        TestTrue(TEXT("Direct race launches the viewed course instead of the stale queue"),
            Retry!=Attempt && Retry->Message.Contains(TEXT("Launch requires a living pioneer")) &&
            Retry->InputMode==Retry->InputModes[1] && Hangar->GetSelectedRacingCourse()==1 &&
            Hangar->GetSelectedRacingLayout()==UFactoryFPVRaceComponent::LayoutSignature(World,1) && !AFactoryFPVDrone::FindActive(World));
        Hangar->ClosePanel();
        Hangar->SetSelectedRacingCourse(0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVHangarTabsTest,
    "FactoryFPV.Interface.HangarTabs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVHangarTabsTest::RunTest(const FString&) {
    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const float SavedTilt=Settings->CameraTilt;
    const float SavedLens=Settings->LensStrength;
    const FName SavedInputMode=Settings->PreferredInputMode;
    ON_SCOPE_EXIT { Settings->PreferredInputMode=SavedInputMode; };
    Settings->PreferredInputMode=TEXT("invalid-mode");
    auto Panel=SNew(SFactoryFPVPanel);
    Panel->Radio.reset();
    auto AdvanceSlate=[&] {
        // IsEnabled is a cached Slate attribute, evaluated during the frame's
        // prepass rather than synchronously when a calibration field changes.
        Panel->Invalidate(EInvalidateWidgetReason::Layout);
        Panel->SlatePrepass();
    };
    TestEqual(TEXT("First-use hangar opens on radio setup"),Panel->PageSwitcher->GetActiveWidgetIndex(),0);
    TestEqual(TEXT("Settings and racing have dedicated pages"),Panel->PageSwitcher->GetNumWidgets(),6);
    TestEqual(TEXT("Footer states the default launch selection"),Panel->FlightSelectionText().ToString(),FString(TEXT("Next launch: Free flight")));
    TestFalse(TEXT("Opening a sanitized draft does not invent edits"),Panel->HasUnsavedChanges());
    TestTrue(TEXT("Invalid preferences load as Radio without changing committed settings"),
        Panel->SelectedInputMode()==TEXT("radio") && Panel->Draft->PreferredInputMode==TEXT("radio") &&
        Settings->PreferredInputMode==TEXT("invalid-mode"));
    Panel->InputModeCombo->SetSelectedItem(Panel->InputModes[1]);
    TestTrue(TEXT("Explicit Keyboard selection edits only the hangar draft"),Panel->SelectedInputMode()==TEXT("keyboard") &&
        Panel->Draft->PreferredInputMode==TEXT("keyboard") && Panel->HasUnsavedChanges() && Settings->PreferredInputMode==TEXT("invalid-mode"));
    Panel->InputModeCombo->SetSelectedItem(Panel->InputModes[0]);
    TestFalse(TEXT("Selecting the original mode clears the unsaved indicator"),Panel->HasUnsavedChanges());
    const float OriginalTilt=Panel->Draft->CameraTilt;
    Panel->Draft->CameraTilt=OriginalTilt==42.f ? 41.f : 42.f;
    TestTrue(TEXT("A changed setting is marked unsaved"),Panel->HasUnsavedChanges());
    const float EditedTilt=Panel->Draft->CameraTilt;
    const float OriginalLens=Panel->Draft->LensStrength;
    Panel->Draft->LensStrength=.55f;
    for (int32 Index=0;Index<Panel->PageButtons.Num();++Index) {
        TestTrue(FString::Printf(TEXT("Tab %d has a real enabled button"),Index),Panel->PageButtons[Index]->IsEnabled());
        Panel->PageButtons[Index]->SimulateClick();
        TestEqual(FString::Printf(TEXT("Tab %d click activates its page"),Index),Panel->PageSwitcher->GetActiveWidgetIndex(),Index);
        TestEqual(TEXT("Switching tabs retains draft tuning"),Panel->Draft->CameraTilt,EditedTilt);
        TestEqual(TEXT("Switching tabs retains optional lens tuning"),Panel->Draft->LensStrength,.55f);
    }
    Panel->Draft->CameraTilt=OriginalTilt;
    Panel->Draft->LensStrength=OriginalLens;
    TestFalse(TEXT("Restoring a value clears the unsaved indicator without a no-op save"),Panel->HasUnsavedChanges());
    Panel->SelectPage(static_cast<SFactoryFPVPanel::EPage>(255));
    TestEqual(TEXT("Invalid page request does not hide the hangar"),Panel->PageSwitcher->GetActiveWidgetIndex(),5);
    TFunction<TSharedPtr<SWidget>(TSharedRef<SWidget>,FName)> FindTagged;
    FindTagged=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<SWidget> {
        if (Widget->GetTag()==Tag) return Widget;
        auto* Children=Widget->GetChildren();
        for (int32 Index=0;Index<Children->Num();++Index)
            if (auto Match=FindTagged(Children->GetChildAt(Index),Tag)) return Match;
        return nullptr;
    };
    Panel->PageButtons[0]->SimulateClick();
    for (const TCHAR* Tag:{TEXT("FactoryFPVShortcutHangar"),TEXT("FactoryFPVShortcutArm"),TEXT("FactoryFPVShortcutRecover"),TEXT("FactoryFPVShortcutReturn")}) {
        const auto Selector=FindTagged(Panel,FName(Tag));
        TestTrue(TEXT("Radio tab mounts a function-key selector for each shortcut"),Selector && Selector->GetTypeAsString()==TEXT("SComboButton"));
    }
    const auto ResetKeys=StaticCastSharedPtr<SButton>(FindTagged(Panel,FName(TEXT("FactoryFPVResetKeyboardShortcuts"))));
    if (!TestNotNull(TEXT("Actual reset-shortcuts button"),ResetKeys.Get())) return false;
    const auto OriginalKeys=Panel->Draft->KeyboardShortcuts;
    const FKey SavedHangarKey=Settings->KeyboardShortcuts.Hangar;
    const auto* NewKey=FFactoryFPVKeyboardShortcuts::FunctionKeys().FindByPredicate([&](const FKey& Key) {
        return Key!=OriginalKeys.Hangar && Key!=OriginalKeys.Arm && Key!=OriginalKeys.Recover && Key!=OriginalKeys.Return;
    });
    if (!TestNotNull(TEXT("Unused function key for the shortcut draft"),NewKey)) return false;
    Panel->Draft->KeyboardShortcuts.Hangar=*NewKey;
    const FKeyEvent SavedRepeat(SavedHangarKey,FModifierKeysState(),0,true,0,0);
    const FKeyEvent DraftPress(*NewKey,FModifierKeysState(),0,false,0,0);
    TestTrue(TEXT("Slate handles repeats of the committed hangar key and ignores an unsubmitted key"),
        Panel->HasUnsavedChanges() && Panel->OnKeyDown(FGeometry(),SavedRepeat).IsEventHandled() &&
        !Panel->OnKeyDown(FGeometry(),DraftPress).IsEventHandled());
    ResetKeys->SimulateClick();
    TestTrue(TEXT("Reset defaults updates the draft without changing the saved hangar key"),
        Panel->Draft->KeyboardShortcuts.Hangar==EKeys::F6 && Panel->Draft->KeyboardShortcuts.Arm==EKeys::F8 &&
        Panel->Draft->KeyboardShortcuts.Recover==EKeys::F9 && Panel->Draft->KeyboardShortcuts.Return==EKeys::F10 &&
        Settings->KeyboardShortcuts.Hangar==SavedHangarKey);
    Panel->Draft->KeyboardShortcuts=OriginalKeys;
    Panel->PageButtons[5]->SimulateClick();
    const auto GhostCheck=StaticCastSharedPtr<SCheckBox>(FindTagged(Panel,FName(TEXT("FactoryFPVShowRaceGhost"))));
    if (!TestNotNull(TEXT("Racing tab mounts the ghost checkbox"),GhostCheck.Get())) return false;
    const bool bOriginalGhost=Panel->Draft->bShowRaceGhost,bSavedGhost=Settings->bShowRaceGhost;
    const FKeyEvent GhostAccept(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);
    GhostCheck->OnKeyDown(FGeometry(),GhostAccept);GhostCheck->OnKeyUp(FGeometry(),GhostAccept);
    TestTrue(TEXT("The ghost checkbox edits the hangar draft only"),Panel->Draft->bShowRaceGhost!=bOriginalGhost &&
        Settings->bShowRaceGhost==bSavedGhost && Panel->HasUnsavedChanges());
    Panel->Draft->bShowRaceGhost=bOriginalGhost;
    Panel->PageButtons[1]->SimulateClick();
    const auto ThrottlePreview=StaticCastSharedPtr<STextBlock>(FindTagged(Panel,FName(TEXT("FactoryFPVThrottlePreview"))));
    const auto PitchToggle=StaticCastSharedPtr<SCheckBox>(FindTagged(Panel,FName(TEXT("FactoryFPVPitchRateOverride"))));
    const auto PitchSpin=StaticCastSharedPtr<SSpinBox<float>>(FindTagged(Panel,FName(TEXT("FactoryFPVPitchRate"))));
    const auto PitchCenterToggle=StaticCastSharedPtr<SCheckBox>(FindTagged(Panel,FName(TEXT("FactoryFPVPitchCenterRateOverride"))));
    const auto PitchCenterSpin=StaticCastSharedPtr<SSpinBox<float>>(FindTagged(Panel,FName(TEXT("FactoryFPVPitchCenterRate"))));
    const auto HalfStick=StaticCastSharedPtr<STextBlock>(FindTagged(Panel,FName(TEXT("FactoryFPVRateHalfStick"))));
    if (!TestNotNull(TEXT("Flight tab has a throttle curve"),FindTagged(Panel,FName(TEXT("FactoryFPVThrottleCurve"))).Get()) ||
        !TestNotNull(TEXT("Flight tab has a throttle preview readout"),ThrottlePreview.Get()) ||
        !TestNotNull(TEXT("Flight tab has a rate curve"),FindTagged(Panel,FName(TEXT("FactoryFPVRateCurve"))).Get()) ||
        !TestNotNull(TEXT("Flight tab has a separate pitch toggle"),PitchToggle.Get()) ||
        !TestNotNull(TEXT("Flight tab has a pitch rate input"),PitchSpin.Get()) ||
        !TestNotNull(TEXT("Flight tab has a pitch center toggle"),PitchCenterToggle.Get()) ||
        !TestNotNull(TEXT("Flight tab has a pitch center input"),PitchCenterSpin.Get()) ||
        !TestNotNull(TEXT("Flight tab has a half-stick rate readout"),HalfStick.Get())) return false;
    const auto OriginalRates=Panel->Draft->CaptureTuning(FString());
    const float SavedThrottleExpo=Settings->ThrottleExpo;
    Panel->Draft->ThrustToWeight=4.f;
    Panel->Draft->ThrottleExpo=SavedThrottleExpo==.6f ? .4f : .6f;
    AdvanceSlate();
    TestTrue(TEXT("Hover estimate follows draft thrust-to-weight without committing Throttle Expo"),
        ThrottlePreview->GetText().ToString().Contains(TEXT("Level hover ~25%")) &&
        Settings->ThrottleExpo==SavedThrottleExpo && Panel->HasUnsavedChanges());
    const float SavedPitchRate=Settings->PitchRate,SavedYawRate=Settings->YawRate;
    const float SavedPitchCenter=Settings->PitchCenterRate;
    Panel->Draft->AcroRate=600.f;Panel->Draft->PitchRate=0.f;Panel->Draft->YawRate=0.f;
    Panel->Draft->RollCenterRate=0.f;Panel->Draft->PitchCenterRate=0.f;Panel->Draft->YawCenterRate=0.f;
    Panel->Draft->Expo=0.f;Panel->Draft->Deadzone=0.f;Panel->Draft->bAngleMode=false;
    AdvanceSlate();
    TestTrue(TEXT("Inherited pitch displays the current roll rate without enabling editing"),!PitchSpin->IsEnabled() && PitchSpin->GetValue()==600.f);
    const FKeyEvent Accept(EKeys::SpaceBar,FModifierKeysState(),0,false,0,0);
    const auto TogglePitch=[&] {
        PitchToggle->OnKeyDown(FGeometry(),Accept);
        PitchToggle->OnKeyUp(FGeometry(),Accept);
        AdvanceSlate();
    };
    TogglePitch();
    TestTrue(TEXT("Enabling separate pitch starts from the current roll rate"),PitchSpin->IsEnabled() && Panel->Draft->PitchRate==600.f);
    Panel->Draft->PitchRate=450.f;Panel->Draft->YawRate=300.f;
    AdvanceSlate();
    TestTrue(TEXT("Acro shows draft half-stick values for all three axes"),HalfStick->GetText().ToString().Contains(TEXT("Roll 300 · Pitch 225 · Yaw 150")));
    TestFalse(TEXT("Expo remains active until a center override is selected"),PitchCenterSpin->IsEnabled());
    const auto TogglePitchCenter=[&] {
        PitchCenterToggle->OnKeyDown(FGeometry(),Accept);
        PitchCenterToggle->OnKeyUp(FGeometry(),Accept);
        AdvanceSlate();
    };
    TogglePitchCenter();
    TestTrue(TEXT("Enabling a center override starts from that axis's Expo response"),
        PitchCenterSpin->IsEnabled() && Panel->Draft->PitchCenterRate==450.f);
    Panel->Draft->PitchCenterRate=100.f;
    AdvanceSlate();
    const FString ExpectedPitch=FString::Printf(TEXT("Roll 300 · Pitch %.0f · Yaw 150"),factoryfpv::rateCommand(.5,450.,0.,0.,100.));
    TestTrue(TEXT("Half-stick preview uses the actual custom center response on its axis"),HalfStick->GetText().ToString().Contains(ExpectedPitch));
    Panel->Draft->bAngleMode=true;
    AdvanceSlate();
    TestTrue(TEXT("Angle previews yaw rate alone"),HalfStick->GetText().ToString().EndsWith(TEXT("Yaw 150°/s")) &&
        !HalfStick->GetText().ToString().Contains(TEXT("Pitch")));
    Panel->Draft->PitchRate=800.f;Panel->Draft->PitchCenterRate=650.f;
    TogglePitch();
    TestTrue(TEXT("Relinking restores pitch inheritance without changing the yaw draft or committed settings"),
        !PitchSpin->IsEnabled() && Panel->Draft->PitchRate==0.f && Panel->Draft->YawRate==300.f &&
        Settings->PitchRate==SavedPitchRate && Settings->YawRate==SavedYawRate);
    TestEqual(TEXT("Relinking caps an enabled center at the new maximum"),Panel->Draft->PitchCenterRate,600.f);
    TogglePitchCenter();
    TestTrue(TEXT("Clearing center override restores Expo without changing committed settings"),
        !PitchCenterSpin->IsEnabled() && Panel->Draft->PitchCenterRate==0.f && Settings->PitchCenterRate==SavedPitchCenter);
    Panel->Draft->ApplyTuning(OriginalRates);
    Panel->PageButtons[3]->SimulateClick();
    const auto OsdPreview=FindTagged(Panel,FName(TEXT("FactoryFPVOsdPreview")));
    const auto ResetOsd=StaticCastSharedPtr<SButton>(FindTagged(Panel,FName(TEXT("FactoryFPVResetOsdLayout"))));
    if (!TestTrue(TEXT("OSD tab mounts the real flight overlay for its preview"),OsdPreview && OsdPreview->GetTypeAsString()==TEXT("SFactoryFPVOSD")) ||
        !TestNotNull(TEXT("Actual OSD layout reset button"),ResetOsd.Get())) return false;
    const auto OriginalLayout=Panel->Draft->OsdLayout;
    const auto SavedSpeedSlot=Settings->OsdLayout.GetSlot(EFactoryFPVOsdItem::Speed);
    const auto MovedSpeedSlot=SavedSpeedSlot==EFactoryFPVOsdSlot::TopCenter ? EFactoryFPVOsdSlot::TopRight : EFactoryFPVOsdSlot::TopCenter;
    TestTrue(TEXT("Repositioning a readout edits only the hangar draft"),
        Panel->Draft->OsdLayout.Move(EFactoryFPVOsdItem::Speed,MovedSpeedSlot) && Panel->HasUnsavedChanges() &&
        Settings->OsdLayout.GetSlot(EFactoryFPVOsdItem::Speed)==SavedSpeedSlot);
    ResetOsd->SimulateClick();
    TestTrue(TEXT("Reset layout restores the original positions without committing"),
        Panel->Draft->OsdLayout.GetSlot(EFactoryFPVOsdItem::Speed)==FFactoryFPVOsdLayout{}.GetSlot(EFactoryFPVOsdItem::Speed) &&
        Settings->OsdLayout.GetSlot(EFactoryFPVOsdItem::Speed)==SavedSpeedSlot);
    Panel->Draft->OsdLayout=OriginalLayout;
    Panel->UpdateOsdPreview();
    auto PropPreviewButton=StaticCastSharedPtr<SButton>(FindTagged(Panel,FName(TEXT("FactoryFPVPreviewPropSound"))));
    if (!TestNotNull(TEXT("OSD & Audio has a real prop sound preview button"),PropPreviewButton.Get())) return false;
    const float CommittedVolume=Settings->PropVolume;
    const float OriginalDraftVolume=Panel->Draft->PropVolume;
    Panel->Draft->PropVolume=0.f;
    PropPreviewButton->SimulateClick();
    TestTrue(TEXT("Preview explains zero draft volume without starting a source or changing saved settings"),
        !Panel->PropPreview.IsValid() && Panel->AudioPreviewMessage.Contains(TEXT("Prop volume is zero")) && Settings->PropVolume==CommittedVolume);
    Panel->Draft->PropVolume=OriginalDraftVolume;
    const auto CuePreviewButton=StaticCastSharedPtr<SButton>(FindTagged(Panel,FName(TEXT("FactoryFPVPreviewFlightCue"))));
    if (!TestNotNull(TEXT("OSD & Audio has a flight cue selector"),FindTagged(Panel,FName(TEXT("FactoryFPVFlightCueType"))).Get()) ||
        !TestNotNull(TEXT("OSD & Audio has a real flight cue preview button"),CuePreviewButton.Get())) return false;
    const float CommittedCueVolume=Settings->FlightCueVolume;
    const float OriginalDraftCueVolume=Panel->Draft->FlightCueVolume;
    Panel->Draft->FlightCueVolume=0.f;
    CuePreviewButton->SimulateClick();
    TestTrue(TEXT("Cue preview honors draft mute without changing saved settings"),
        !Panel->CuePreview.IsValid() && Panel->AudioPreviewMessage.Contains(TEXT("Flight cue volume is zero")) &&
        Settings->FlightCueVolume==CommittedCueVolume);
    Panel->Draft->FlightCueVolume=OriginalDraftCueVolume;
    Panel->PageButtons[0]->SimulateClick();
    TestTrue(TEXT("Leaving the audio tab clears preview feedback and preserves the draft"),
        Panel->AudioPreviewMessage.IsEmpty() && !Panel->HasUnsavedChanges());
    Panel->Wizard.stage=0;
    AdvanceSlate();
    TestTrue(TEXT("In-progress calibration is protected as a pending edit"),Panel->HasPendingEdits());
    TestFalse(TEXT("An active calibration keeps its controls visible"),Panel->PageButtons[1]->IsEnabled());
    TestFalse(TEXT("Calibration cannot escape into the racing page"),Panel->PageButtons[5]->IsEnabled());
    Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);
    TestEqual(TEXT("Handler also guards calibration tab escape"),Panel->PageSwitcher->GetActiveWidgetIndex(),0);
    Panel->CancelCalibration();
    Panel->ArmCaptureStage=0;
    AdvanceSlate();
    TestTrue(TEXT("In-progress switch capture is protected as a pending edit"),Panel->HasPendingEdits());
    TestFalse(TEXT("A switch capture also keeps its Cancel visible"),Panel->PageButtons[4]->IsEnabled());
    Panel->CancelArmControl();
    AdvanceSlate();
    Panel->PageButtons[4]->SimulateClick();
    TestEqual(TEXT("Cancelling capture releases tab navigation"),Panel->PageSwitcher->GetActiveWidgetIndex(),4);
    Panel->Draft->CameraTilt=EditedTilt;
    Panel->Draft->LensStrength=.55f;
    Panel->InputModeCombo->SetSelectedItem(Panel->InputModes[2]);
    TestTrue(TEXT("Explicit Gamepad selection is retained as a draft without hardware"),Panel->SelectedInputMode()==TEXT("gamepad") &&
        Panel->Draft->PreferredInputMode==TEXT("gamepad"));
    Panel->Close();
    TestEqual(TEXT("Closing never commits the draft"),GetDefault<UFactoryFPVSettings>()->CameraTilt,SavedTilt);
    TestEqual(TEXT("Closing never commits the optional lens"),GetDefault<UFactoryFPVSettings>()->LensStrength,SavedLens);
    TestEqual(TEXT("Closing never commits the draft mode"),Settings->PreferredInputMode,FName(TEXT("invalid-mode")));
    Settings->PreferredInputMode=TEXT("keyboard");
    auto Reopened=SNew(SFactoryFPVPanel);
    TestTrue(TEXT("An ordinary reopen displays the saved Keyboard preference"),Reopened->SelectedInputMode()==TEXT("keyboard") &&
        Reopened->InputModeCombo->GetSelectedItem()==Reopened->InputModes[1] && !Reopened->HasUnsavedChanges());
    auto Retry=SNew(SFactoryFPVPanel).RetryInputMode(FName(TEXT("radio")));
    TestTrue(TEXT("An explicit retry takes display precedence without editing the preference"),Retry->SelectedInputMode()==TEXT("radio") &&
        Retry->InputModeCombo->GetSelectedItem()==Retry->InputModes[0] && Retry->Draft->PreferredInputMode==TEXT("keyboard") && !Retry->HasUnsavedChanges());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVCalibrationWorkflowTest,
    "FactoryFPV.Interface.CalibrationWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVCalibrationWorkflowTest::RunTest(const FString&) {
    auto Panel = SNew(SFactoryFPVPanel);
    Panel->Radio.reset(); // This test supplies samples; it never opens a device or saves user config.
    Panel->bConnected=true;
    Panel->Draft->RadioProfiles.Reset();
    FFactoryFPVRadioProfile Original;
    Original.DeviceId=TEXT("workflow-test");
    Original.Roll={0,FVector(0,32767,65535),false};
    Original.Pitch={1,FVector(0,32767,65535),true};
    Original.Yaw={3,FVector(0,32767,65535),false};
    Original.Throttle={2,FVector(0,32767,65535),false};
    Panel->Draft->SetProfile(Original);
    Panel->Draft->CameraTilt=42;
    std::array<long,8> Neutral{32767,32767,0,32767,0,0,0,0};
    std::array<factoryfpv::RadioAxisInfo,8> Info{};
    for (int Index=0;Index<4;++Index) Info[Index]={true,0,65535};
    Panel->RadioAxes=Info; // Connected alone does not prove that an axis exists.
    auto Begin = [&] {
        Panel->Raw=Neutral;
        Panel->Wizard.begin(Panel->Raw,Info);
    };
    auto Capture = [&](int Channel,bool Reverse) {
        Panel->Raw=Neutral;
        Panel->Raw[Channel]=0;
        Panel->Wizard.sample(Panel->Raw);
        Panel->Raw[Channel]=65535;
        Panel->Wizard.sample(Panel->Raw);
        Panel->Raw[Channel]=Reverse ? 0 : 65535;
        Panel->CaptureAxis();
    };

    Begin();
    // Deliberately change channel mapping. A partial capture must not replace
    // a previously usable profile, even if the user cancels midway through.
    Capture(3,false);
    TestEqual(TEXT("Advances after roll capture"),Panel->Wizard.stage,1);
    TestEqual(TEXT("Partial capture preserves original mapping"),Panel->Draft->RadioRollAxis,0);
    Panel->CancelCalibration();
    TestFalse(TEXT("Cancel leaves calibration mode"),Panel->Calibrating());
    TestTrue(TEXT("Cancel retains the previous usable calibration"),Panel->Draft->IsRadioCalibrated());
    TestEqual(TEXT("Cancel retains unrelated tuning edits"),Panel->Draft->CameraTilt,42.f);

    Begin();
    Capture(0,false);
    Capture(1,true);
    Capture(3,false);
    Capture(2,false);
    TestEqual(TEXT("All four captures reach review"),Panel->Wizard.stage,4);
    TestTrue(TEXT("Completed capture becomes a valid draft profile"),Panel->Draft->IsRadioCalibrated());
    TestTrue(TEXT("Pitch inversion reaches settings"),Panel->Draft->ReversePitch);
    TestEqual(TEXT("Yaw mapping reaches settings"),Panel->Draft->RadioYawAxis,3);
    Panel->Raw=Neutral;
    Panel->Raw[1]=0;
    TestTrue(TEXT("Pitch-back sample reads positive through UI"),Panel->NormalizedAxis(1)>.99);
    TestTrue(TEXT("Throttle-down sample reads zero through UI"),Panel->NormalizedAxis(3)==0);
    Panel->Raw[2]=65535;
    TestTrue(TEXT("Throttle-up sample reads full through UI"),Panel->NormalizedAxis(3)>.99);
    Panel->SetReverse(0,true);
    Panel->Raw[0]=65535;
    TestTrue(TEXT("Invert checkbox handler reverses live input"),Panel->NormalizedAxis(0)<-.99);
    Panel->Wizard.stage=-1;
    Panel->Buttons={};
    for (int32 Button=7;Button<=10;++Button) Panel->RadioButtonsPresent[Button]=true;
    Panel->CaptureArmControl();
    TestEqual(TEXT("Arm setup begins at OFF prompt"),Panel->ArmCaptureStage,0);
    Panel->CaptureArmControl();
    TestEqual(TEXT("OFF capture advances to ON prompt"),Panel->ArmCaptureStage,1);
    Panel->Buttons[7]=true;
    Panel->CaptureArmControl();
    TestEqual(TEXT("Button capture completes"),Panel->ArmCaptureStage,-1);
    TestEqual(TEXT("Button binding reaches device draft"),Panel->Draft->RadioArm.Channel,7);
    TestEqual(TEXT("Button binding kind"),Panel->Draft->RadioArm.Kind,1);
    TestTrue(TEXT("Bound switch live status reads ON"),Panel->ArmStatus().ToString().Contains(TEXT("/   ON\n")));
    Panel->CaptureArmControl();Panel->CaptureArmControl();
    Panel->Buttons[7]=false;Panel->Buttons[8]=true;
    Panel->CaptureArmControl();
    TestEqual(TEXT("Ambiguous capture stays in review"),Panel->ArmCaptureStage,1);
    TestEqual(TEXT("Ambiguous capture preserves previous binding"),Panel->Draft->RadioArm.Channel,7);
    Panel->CancelArmControl();
    TestEqual(TEXT("Cancel keeps prior binding"),Panel->Draft->RadioArm.Channel,7);
    Panel->Buttons={};
    Panel->CaptureRadioControl(SFactoryFPVPanel::ERadioControl::Return);
    Panel->CaptureRadioControl(SFactoryFPVPanel::ERadioControl::Return);
    Panel->Buttons[7]=true;
    Panel->CaptureRadioControl(SFactoryFPVPanel::ERadioControl::Return);
    TestEqual(TEXT("Conflicting arm/return control is rejected"),Panel->Draft->RadioReturn.Kind,0);
    TestEqual(TEXT("Conflict leaves capture available for cancel/retry"),Panel->ArmCaptureStage,1);
    TestTrue(TEXT("Conflict explains the reused control"),Panel->Message.Contains(TEXT("same radio control")));
    Panel->CancelArmControl();
    for (int32 Index=1;Index<4;++Index) {
        const auto ControlId=static_cast<SFactoryFPVPanel::ERadioControl>(Index);
        Panel->Buttons={};
        Panel->CaptureRadioControl(ControlId);Panel->CaptureRadioControl(ControlId);
        Panel->Buttons[7+Index]=true;
        Panel->CaptureRadioControl(ControlId);
        TestEqual(TEXT("Separate action binding completes the shared capture flow"),Panel->ArmCaptureStage,-1);
        TestEqual(TEXT("Action binding is stored in the selected device draft"),Panel->ControlBinding(ControlId).Channel,7+Index);
    }
    FString BindingError;
    TestTrue(TEXT("Learned distinct actions validate together"),Panel->Draft->AreRadioBindingsValid(BindingError));
    const auto StoredProfile=Panel->Draft->CurrentProfile();
    TestEqual(TEXT("Per-device return binding is retained"),StoredProfile.ReturnControl.Channel,8);
    TestEqual(TEXT("Per-device recover binding is retained"),StoredProfile.RecoverControl.Channel,9);
    TestEqual(TEXT("Per-device mode binding is retained"),StoredProfile.ModeControl.Channel,10);
    Panel->ClearRadioControl(SFactoryFPVPanel::ERadioControl::Recover);
    TestEqual(TEXT("Clear affects only its requested action"),Panel->Draft->RadioRecover.Kind,0);
    TestEqual(TEXT("Clear retains the arm control"),Panel->Draft->RadioArm.Channel,7);
    const FString DeviceBefore=Panel->Draft->RadioDeviceId;
    const int32 PresetsBefore=GetDefault<UFactoryFPVSettings>()->TuningPresets.Num();
    Panel->RefreshTuningOptions();
    TestTrue(TEXT("Preset picker contains built-in tunings"),Panel->TuningOptions.Num()>=3);
    Panel->SelectedTuning=Panel->TuningOptions[0];
    Panel->ApplySelectedTuning();
    TestTrue(TEXT("Cruiser preset selects Angle mode through UI"),Panel->Draft->bAngleMode);
    TestEqual(TEXT("Applying tuning retains selected radio"),Panel->Draft->RadioDeviceId,DeviceBefore);
    TestEqual(TEXT("Applying tuning retains arm switch"),Panel->Draft->RadioArm.Channel,7);
    Panel->TuningName=TEXT("UI test tune");
    Panel->SaveTuning();
    TestTrue(TEXT("Store tuning populates draft"),Panel->Draft->TuningPresets.ContainsByPredicate([](const auto& Item) { return Item.Name==TEXT("UI test tune"); }));
    TestEqual(TEXT("Storing draft does not change saved defaults"),GetDefault<UFactoryFPVSettings>()->TuningPresets.Num(),PresetsBefore);
    Panel->SelectedTuning=Panel->TuningOptions.Last();
    Panel->DeleteTuning();
    TestFalse(TEXT("Deleting preset only removes draft entry"),Panel->Draft->TuningPresets.ContainsByPredicate([](const auto& Item) { return Item.Name==TEXT("UI test tune"); }));

    // Exercise the production preview loss path without acquiring a device.
    const float TuningBeforeLoss=Panel->Draft->CameraTilt;
    Begin();Capture(3,false);
    Panel->Buttons[7]=true;
    Panel->Radio=std::make_unique<factoryfpv::WindowsRadio>();
    TestFalse(TEXT("A radio cannot open without a native window"),Panel->Radio->open(L"{00000000-0000-0000-0000-000000000000}",nullptr,nullptr));
    Panel->Tick(FGeometry(),0.,0.f);
    TestTrue(TEXT("Preview loss cancels capture and clears stale controller samples"),!Panel->bConnected && !Panel->Radio && !Panel->Calibrating() &&
        Panel->Raw==std::array<long,8>{} && Panel->Buttons==std::array<bool,128>{} && !Panel->RadioAxes[3].present);
    TestTrue(TEXT("Preview loss retains completed calibration, bindings and tuning"),Panel->Draft->IsRadioCalibrated() &&
        Panel->Draft->RadioRollAxis==0 && Panel->Draft->RadioArm.Channel==7 && Panel->Draft->CameraTilt==TuningBeforeLoss);
    TestTrue(TEXT("Preview loss keeps capture and Windows diagnostics separate"),Panel->Message.Contains(TEXT("Unfinished capture")) &&
        Panel->ConnectionText().ToString().Contains(TEXT("Game window handle")));
    Panel->bConnected=true;
    Panel->RadioAxes=Info;

    // A real second calibration moves Roll onto a previously bound AUX axis.
    // Exercise the save handlers with memory-only config even on regression.
    Original.DisplayName=TEXT("Workflow radio");
    Original.Arm={2,4,0,65535};
    Panel->Draft->SetProfile(Original);
    Panel->SavedDraft.Reset(DuplicateObject<UFactoryFPVSettings>(Panel->Draft.Get(),GetTransientPackage()));
    Neutral[4]=32767;Info[4]={true,0,65535};
    Begin();Capture(4,false);Capture(1,true);Capture(3,false);Capture(2,false);
    TestTrue(TEXT("Conflicting recalibration stays complete in the draft with a remedy"),Panel->Wizard.stage==4 &&
        Panel->Draft->IsRadioCalibrated() && Panel->Draft->RadioRollAxis==4 && Panel->Draft->RadioArm.Channel==4 &&
        Panel->Message.Contains(TEXT("Clear or relearn")));
    auto* Settings=GetMutableDefault<UFactoryFPVSettings>();
    const TStrongObjectPtr<UFactoryFPVSettings> OriginalSettings(DuplicateObject<UFactoryFPVSettings>(Settings,GetTransientPackage()));
    ON_SCOPE_EXIT {
        for (TFieldIterator<FProperty> Property(Settings->GetClass());Property;++Property)
            if (Property->HasAnyPropertyFlags(CPF_Config)) Property->CopyCompleteValue_InContainer(Settings,OriginalSettings.Get());
    };
    FConfigCacheIni MemoryOnlyConfig(EConfigCacheType::Temporary);
    TGuardValue<FConfigCacheIni*> ConfigGuard(GConfig,&MemoryOnlyConfig);
    Panel->SaveSettings();
    TestTrue(TEXT("Save settings blocks a newly conflicting active profile without changing committed settings"),
        Panel->Message.Contains(TEXT("Arm uses a flight axis")) && Settings->RadioDeviceId==OriginalSettings->RadioDeviceId);
    for (const int32 Mode:{0,1,2}) {
        Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);
        Panel->InputMode=Panel->InputModes[Mode];
        Panel->Fly();
        TestTrue(TEXT("Save and fly blocks a new binding conflict in every input mode and preserves the draft"),
            Panel->ActivePage==SFactoryFPVPanel::EPage::Radio && Panel->Message.Contains(TEXT("Arm uses a flight axis")) &&
            Panel->Draft->RadioRollAxis==4 && Panel->SavedDraft->RadioRollAxis==0);
    }

    Panel->Draft->SelectRadio(TEXT("workflow-second"),TEXT("Second radio"));
    TestTrue(TEXT("The newly selected device itself has valid optional controls"),Panel->Draft->AreRadioBindingsValid(BindingError));
    Panel->SaveSettings();
    TestTrue(TEXT("Save still protects the edited inactive profile and identifies its radio"),
        Panel->Message.Contains(TEXT("Workflow radio")) && Panel->Message.Contains(TEXT("Arm uses a flight axis")) &&
        Settings->RadioDeviceId==OriginalSettings->RadioDeviceId);
    Panel->InputMode=Panel->InputModes[1];Panel->Fly();
    TestTrue(TEXT("Keyboard Save and fly also protects the edited inactive profile"),Panel->Message.Contains(TEXT("Workflow radio")));
    Panel->Draft->SelectRadio(TEXT("WORKFLOW-TEST"),TEXT("Workflow radio"));
    Panel->ClearRadioControl(SFactoryFPVPanel::ERadioControl::Arm);
    TestTrue(TEXT("Clearing the conflicting AUX keeps the captured mapping and unblocks save"),
        Panel->Draft->RadioRollAxis==4 && Panel->ValidateBindings());

    auto Legacy=Panel->Draft->CurrentProfile();Legacy.Arm=Original.Arm;
    Panel->Draft->SetProfile(Legacy);
    Panel->SavedDraft.Reset(DuplicateObject<UFactoryFPVSettings>(Panel->Draft.Get(),GetTransientPackage()));
    Panel->Draft->CameraTilt=45;
    Panel->SaveSettings();
    TestTrue(TEXT("Unchanged legacy binding conflict permits a tuning save while retaining the original radio data"),
        Panel->Message.Contains(TEXT("Settings saved")) && Settings->CameraTilt==45.f && Settings->RadioArm.Channel==4 &&
        !Settings->AreRadioBindingsValid(BindingError));
    Panel->Draft->SelectRadio(TEXT("workflow-second"),TEXT("Second radio"));
    TestTrue(TEXT("An unchanged invalid legacy profile also permits fallback with another device selected"),
        Panel->ValidateBindings() && Panel->ValidateFlightInput());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRadioCapabilityTest,
    "FactoryFPV.Interface.RadioCapabilities",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRadioCapabilityTest::RunTest(const FString&) {
    auto Panel=SNew(SFactoryFPVPanel);
    Panel->Radio.reset(); // Synthetic samples/capabilities; no USB access or config writes.
    Panel->InputMode=Panel->InputModes[0];
    Panel->bConnected=true;
    Panel->RadioAxes={};
    for (int Index=0;Index<4;++Index) Panel->RadioAxes[Index]={true,0,65535};
    FFactoryFPVRadioProfile Profile;
    Profile.DeviceId=TEXT("capability-test");
    Profile.Roll={0,FVector(0,32767,65535),false};
    Profile.Pitch={1,FVector(0,32767,65535),true};
    Profile.Yaw={3,FVector(0,32767,65535),false};
    Profile.Throttle={2,FVector(0,500,1000),false};
    Panel->Draft->SetProfile(Profile);
    Panel->SavedDraft.Reset(DuplicateObject<UFactoryFPVSettings>(Panel->Draft.Get(),GetTransientPackage()));
    FString Error;
    TestTrue(TEXT("All four explicitly present flight axes are ready"),Panel->RadioReady(Error));
    TestTrue(TEXT("Ready radio has a calibrated connection label"),Panel->ConnectionText().ToString().Contains(TEXT("CALIBRATED")));
    TestFalse(TEXT("Readiness checks do not dirty the draft"),Panel->HasUnsavedChanges());
    const int Channels[]{0,1,3,2};
    const TCHAR* ControlNames[]{TEXT("ROLL"),TEXT("PITCH"),TEXT("YAW"),TEXT("THROTTLE")};
    for (int Index=0;Index<4;++Index) {
        Panel->RadioAxes[Channels[Index]].present=false;
        TestFalse(FString::Printf(TEXT("%s requires a physical USB axis"),ControlNames[Index]),Panel->RadioReady(Error));
        TestTrue(TEXT("Every missing flight channel is identified by its control"),Error.Contains(ControlNames[Index]));
        Panel->RadioAxes[Channels[Index]].present=true;
    }

    Panel->RadioAxes[3].present=false;
    TestFalse(TEXT("Saved numerical calibration cannot hide a missing physical yaw axis"),Panel->RadioReady(Error));
    TestTrue(TEXT("Missing hardware does not block the binding validation used to save settings"),Panel->ValidateBindings());
    TestTrue(TEXT("Preflight identifies the exact saved control and USB axis"),Error.Contains(TEXT("YAW")) && Error.Contains(TEXT("Rx")));
    TestTrue(TEXT("Connection status identifies missing hardware with the launch reason"),Panel->ConnectionText().ToString().Contains(Error));
    Panel->Raw[3]=0;
    TestFalse(TEXT("Missing yaw has no live preview"),Panel->AxisAvailable(2));
    TestEqual(TEXT("Missing yaw does not become a full-negative live input"),Panel->NormalizedAxis(2),0.);
    const float SavedTilt=GetDefault<UFactoryFPVSettings>()->CameraTilt;
    Panel->Draft->CameraTilt=42;
    Panel->SelectPage(SFactoryFPVPanel::EPage::Flight);
    Panel->Fly(); // Real launch handler must reject before commit/closing the panel.
    TestEqual(TEXT("Missing-axis launch focuses setup without closing"),Panel->PageSwitcher->GetActiveWidgetIndex(),0);
    TestTrue(TEXT("Launch displays the actionable missing-axis error without a duplicate footer message"),
        Panel->Message.IsEmpty() && Panel->ConnectionText().ToString().Contains(Error) && Panel->RadioPreflightText().ToString().Contains(TEXT("axis")));
    TestEqual(TEXT("Rejected launch retains the tuning draft"),Panel->Draft->CameraTilt,42.f);
    TestEqual(TEXT("Rejected launch does not commit user settings"),GetDefault<UFactoryFPVSettings>()->CameraTilt,SavedTilt);

    Panel->Draft->RadioArm={2,4,0,65535};
    Panel->Raw[4]=0;
    TestTrue(TEXT("Absent auxiliary axis cannot falsely report OFF"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Arm).ToString().Contains(TEXT("UNAVAILABLE")));
    Panel->Draft->RadioArm={2,4,65535,0};
    TestTrue(TEXT("Absent inverted auxiliary axis cannot falsely report ON"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Arm).ToString().Contains(TEXT("UNAVAILABLE")));
    Panel->RadioAxes[4]={true,0,65535};
    TestTrue(TEXT("Present inverted auxiliary endpoint reports ON"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Arm).ToString().EndsWith(TEXT("ON")));
    Panel->Raw[4]=65535;
    TestTrue(TEXT("Present inverted auxiliary endpoint reports OFF"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Arm).ToString().EndsWith(TEXT("OFF")));
    Panel->Draft->RadioReturn={1,7,0,1};
    Panel->Buttons[7]=true;
    Panel->RadioAxes[3].present=true;
    TestTrue(TEXT("Absent USB button reports unavailable even if its saved input is held"),
        Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Return).ToString().Contains(TEXT("UNAVAILABLE")));
    TestFalse(TEXT("A missing configured USB button blocks radio launch"),Panel->RadioReady(Error));
    TestTrue(TEXT("The hangar names the missing configured control"),Error.Contains(TEXT("Return to pioneer")) &&
        Panel->ConnectionText().ToString().Contains(Error));
    Panel->RadioButtonsPresent[7]=true;
    TestTrue(TEXT("USB button status remains independent of axis presence"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Return).ToString().EndsWith(TEXT("ON")));
    TestTrue(TEXT("Restoring the button makes radio launch ready again"),Panel->RadioReady(Error));
    Panel->Raw[2]=50;
    TestTrue(TEXT("Five percent throttle is ready to arm after a disarmed launch"),
        Panel->RadioPreflightText().ToString().Contains(TEXT("Switch ON to arm after launch")));
    Panel->Raw[2]=51;Panel->Raw[4]=32767;
    TestTrue(TEXT("Middle switch and high throttle produce advice without blocking a disarmed launch"),
        Panel->RadioReady(Error) && Panel->RadioPreflightText().ToString().Contains(TEXT("Arm MID · Switch OFF and lower throttle")));
    Panel->bConnected=false;
    TestTrue(TEXT("A disconnected USB button is unavailable"),Panel->BindingStatus(SFactoryFPVPanel::ERadioControl::Return).ToString().Contains(TEXT("UNAVAILABLE")));
    Panel->InputMode=Panel->InputModes[1];
    TestTrue(TEXT("Keyboard launch preflight works without a USB radio"),Panel->ValidateFlightInput());
    Panel->InputMode=Panel->InputModes[2];
    TestFalse(TEXT("Gamepad launch needs a connected pad for the current player"),Panel->ValidateFlightInput());
    TestTrue(TEXT("No-gamepad guidance offers the keyboard fallback"),Panel->Message.Contains(TEXT("connected gamepad")) && Panel->Message.Contains(TEXT("Keyboard")));
    Panel->InputMode=Panel->InputModes[0];
    TestFalse(TEXT("Radio preflight explains a disconnected radio"),Panel->ValidateFlightInput());
    const factoryfpv::RadioDevice Other{L"other-test",L"Other joystick"},Expected{L"CAPABILITY-TEST",L"Test TX16S"};
    Panel->UpdateDeviceOptions({Other});
    TestTrue(TEXT("Saved radio missing with another device detected does not fall back"),
        !Panel->SelectedDevice && Panel->Draft->RadioDeviceId==Profile.DeviceId && Panel->ConnectionText().ToString().Contains(TEXT("Saved radio missing")));
    Panel->UpdateDeviceOptions({Other,Expected});
    const auto Selected=Panel->SelectedDevice;
    const auto Options=Panel->Devices;
    Panel->UpdateDeviceOptions({Expected,Other});
    TestTrue(TEXT("Discovery selects only the saved GUID and preserves stable options and calibration"),
        Selected && Selected->Name==TEXT("Test TX16S") && Panel->DeviceCombo->GetSelectedItem()==Selected &&
        Panel->Devices==Options && Panel->Draft->RadioDeviceId==Profile.DeviceId && Panel->Draft->IsRadioCalibrated());
    Panel->RadioPreviewError=TEXT("Acquire failed");
    TestTrue(TEXT("Unreadable detected radio keeps technical detail on Radio and compact guidance across tabs"),
        !Panel->ValidateFlightInput() && Panel->ConnectionText().ToString().Contains(TEXT("Acquire failed")) &&
        Panel->RadioPreflightText().ToString().Contains(TEXT("unreadable")) && !Panel->RadioPreflightText().ToString().Contains(TEXT("Acquire failed")));
    Panel->Draft->RadioDeviceId.Reset();
    Panel->RadioPreviewError.Reset();
    Panel->UpdateDeviceOptions({Other,Expected});
    TestTrue(TEXT("Fresh setup prompts an explicit choice when Windows lists controllers"),
        !Panel->SelectedDevice && Panel->ConnectionText().ToString().Contains(TEXT("Choose your USB controller")));
    return true;
}
#endif


