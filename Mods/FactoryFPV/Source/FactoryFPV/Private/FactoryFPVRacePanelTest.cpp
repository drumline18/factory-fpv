#include "FactoryFPVRacePanel.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceDebrief.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRaceRoutePreview.h"
#include "FactoryFPVInputLeaseTestController.h"
#include "Components/BoxComponent.h"
#include "Components/InputComponent.h"
#include "FGPowerInfoComponent.h"
#include "Engine/Engine.h"
#include "Engine/Console.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Slate/WidgetRenderer.h"
#include "RenderingThread.h"
#include "DynamicRHI.h"
#include "RHI.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVRacePanelTest,
    "FactoryFPV.Interface.RaceGateWorkflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVRacePanelTest::RunTest(const FString&) {
    TFunction<TSharedPtr<SButton>(TSharedRef<SWidget>,const FString&)> FindButton;
    FindButton=[&](TSharedRef<SWidget> Widget,const FString& Text)->TSharedPtr<SButton> {
        auto* Children=Widget->GetChildren();
        if (Widget->GetTypeAsString()==TEXT("SButton") && Children->Num()==1 &&
            Children->GetChildAt(0)->GetTypeAsString()==TEXT("STextBlock") &&
            StaticCastSharedRef<STextBlock>(Children->GetChildAt(0))->GetText().ToString()==Text)
            return StaticCastSharedRef<SButton>(Widget);
        for (int32 Index=0;Index<Children->Num();++Index)
            if (auto Button=FindButton(Children->GetChildAt(Index),Text)) return Button;
        return nullptr;
    };
    TFunction<TSharedPtr<STextBlock>(TSharedRef<SWidget>,FName)> FindText;
    FindText=[&](TSharedRef<SWidget> Widget,FName Tag)->TSharedPtr<STextBlock> {
        if (Widget->GetTag()==Tag && Widget->GetTypeAsString()==TEXT("STextBlock")) return StaticCastSharedRef<STextBlock>(Widget);
        auto* Children=Widget->GetChildren();
        for (int32 Index=0;Index<Children->Num();++Index)
            if (auto Text=FindText(Children->GetChildAt(Index),Tag)) return Text;
        return nullptr;
    };
    const FVector2D RenderSize(1280,720);
    const auto RenderWidget=[&](TSharedRef<SWidget> View,const TArray<TSharedPtr<SWidget>>& Controls,const TCHAR* FileName) {
        if (!GDynamicRHI || !FApp::CanEverRender()) return true;
        FWidgetRenderer RaceEditorRenderer(true);
        auto* Target=FWidgetRenderer::CreateTargetFor(RenderSize,TF_Bilinear,false);
        if (!TestNotNull(TEXT("Actual race-editor render target"),Target)) return false;
        RaceEditorRenderer.DrawWidget(Target,View,RenderSize,0.f,false);
        FlushRenderingCommands();
        RaceEditorRenderer.DrawWidget(Target,View,RenderSize,1.f/60.f,false);
        FlushRenderingCommands();
        for (const auto& Control:Controls) {
            if (!TestTrue(TEXT("Rendered race control exists"),Control.IsValid())) return false;
            const auto& Geometry=Control->GetCachedGeometry();
            const auto A=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
            const auto B=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
            TestTrue(TEXT("Race controls stay inside 720p"),
                B.X>A.X && B.Y>A.Y && A.X>=-1 && A.Y>=-1 && B.X<=RenderSize.X+1 && B.Y<=RenderSize.Y+1);
        }
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags Flags;
        Flags.SetLinearToGamma(false);
        if (!TestTrue(TEXT("Read actual race configuration pixels"),Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags))) return false;
        TestEqual(TEXT("Race editor capture has exact dimensions"),Pixels.Num(),1280*720);
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(1280,720,Pixels,Png);
        const FString Directory=FPaths::ProjectSavedDir()/TEXT("FactoryFPV");
        IFileManager::Get().MakeDirectory(*Directory,true);
        const FString ImagePath=Directory/FileName;
        AddInfo(ImagePath);
        return TestTrue(TEXT("Save rendered production race-editor preview"),FFileHelper::SaveArrayToFile(Png,*ImagePath));
    };
    const auto RenderEditor=[&](TSharedRef<SFactoryFPVRacePanel> Widget,const TArray<TSharedPtr<SWidget>>& Controls,const TCHAR* FileName,bool bEmbedded) {
        auto View=SNew(SBox).WidthOverride(RenderSize.X).HeightOverride(RenderSize.Y)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
                [SNew(SBox).WidthOverride(920).HeightOverride(bEmbedded ? 440.f : 650.f)[Widget]]];
        return RenderWidget(View,Controls,FileName);
    };
    auto Empty=SNew(SFactoryFPVRacePanel).Embedded(true);
    Empty->SlatePrepass();
    TestFalse(TEXT("Embedded racing tab renders safely without a player/world"),Empty->bCourseReady);
    TestFalse(TEXT("Absent game cannot select a race"),Empty->RaceButton->IsEnabled());
    Empty->RaceCourse();
    TestTrue(TEXT("Absent player gets actionable selection guidance"),Empty->Message.Contains(TEXT("single-player")));

    UWorld::InitializationValues Values;
    Values.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false)
        .CreateAISystem(false).ShouldSimulatePhysics(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Race panel world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Viewport=NewObject<UGameViewportClient>(GEngine);
    auto ViewportOverlay=SNew(SOverlay);
    Viewport->SetViewportOverlayWidget(nullptr,ViewportOverlay);
    GEngine->GetWorldContextFromWorld(World)->GameViewport=Viewport;
    // This is a reflected fixture binding, not a private-offset/access cast.
    // LocalPlayer::GetWorld follows its viewport's World property.
    auto* ViewportWorld=FindFProperty<FObjectPropertyBase>(UGameViewportClient::StaticClass(),TEXT("World"));
    if (!TestNotNull(TEXT("Viewport's reflected world binding"),ViewportWorld)) return false;
    ViewportWorld->SetObjectPropertyValue_InContainer(Viewport,World);
    auto* Player=NewObject<ULocalPlayer>(GEngine);
    Player->ViewportClient=Viewport;
    auto* PC=World->SpawnActor<AFactoryFPVInputLeaseTestController>();
    PC->SetPlayer(Player);
    auto* Hangar=NewObject<UFactoryFPVLocalPlayerSubsystem>(Player);
    TestEqual(TEXT("Subsystem fixture follows the real local player's world"),Hangar->GetWorld(),World);
    if (!TestTrue(TEXT("Race fixture binds the real hangar shortcut"),Hangar->BindController(PC,Viewport))) return false;
    ON_SCOPE_EXIT { Hangar->UnbindController(); };
    const int32 BaselineOverlays=ViewportOverlay->GetChildren()->Num();
    {
        auto BuildBrowser=SNew(SFactoryFPVRacePanel).Owner(Hangar).Embedded(true);
        const auto NewCourse=FindButton(BuildBrowser,TEXT("Start a new course"));
        const auto Finish=FindButton(BuildBrowser,TEXT("Finish building"));
        if (!TestNotNull(TEXT("Empty Racing browser has a new course action"),NewCourse.Get()) ||
            !TestNotNull(TEXT("Racing browser has a finish-building action"),Finish.Get())) return false;
        NewCourse->SimulateClick();
        const int32 NewId=Hangar->GetRaceBuildCourse();
        TestTrue(TEXT("New course becomes a viewable build target before any gates exist"),
            NewId>0 && BuildBrowser->ViewedCourseId==NewId && BuildBrowser->CourseOptions.Num()==1 &&
            BuildBrowser->BuildCourseStatus.Contains(TEXT("next checkpoint 1")) && !BuildBrowser->bCourseReady && Hangar->GetSelectedRacingCourse()==0);
        Finish->SimulateClick();
        TestTrue(TEXT("Finish building removes the empty override without changing the race selection"),
            Hangar->GetRaceBuildCourse()==0 && BuildBrowser->CourseOptions.IsEmpty() && Hangar->GetSelectedRacingCourse()==0);
    }
    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    // The SDK has no real circuit simulation. Feed explicitly reported component
    // results to test UI readiness/selection, independent of the power network.
    auto* Circuit=FindFProperty<FIntProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mCircuitID"));
    auto* Powered=FindFProperty<FBoolProperty>(UFGPowerInfoComponent::StaticClass(),TEXT("mHasPower"));
    auto* DesignerFlag=FindFProperty<FBoolProperty>(AFGBuildable::StaticClass(),TEXT("mReplicatedBuiltInsideBlueprintDesigner"));
    if (!TestNotNull(TEXT("Power report circuit property"),Circuit) || !TestNotNull(TEXT("Power report satisfied property"),Powered) ||
        !TestNotNull(TEXT("Native designer state for preview UI regression"),DesignerFlag)) return false;
    FString Error;
    {
        auto* Start=World->SpawnActor<AFactoryFPVRaceGate>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
        auto* Finish=World->SpawnActor<AFactoryFPVRaceGate>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn);
        auto* VerticalGate=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(2000,0,0),FRotator::ZeroRotator,Spawn);
        auto* LaterGate=World->SpawnActor<AFactoryFPVRaceGate>(FVector(3000,0,0),FRotator::ZeroRotator,Spawn);
        auto* DesignerGate=World->SpawnActor<AFactoryFPVRaceGate>(FVector(4000,0,0),FRotator::ZeroRotator,Spawn);
        for (auto* Gate:{Start,Finish,static_cast<AFactoryFPVRaceGate*>(VerticalGate),LaterGate,DesignerGate}) {
            if (!TestNotNull(TEXT("Sparse course checkpoint fixture"),Gate)) return false;
            Gate->EnsureIdentityAndOrder();
        }
        DesignerFlag->SetPropertyValue_InContainer(DesignerGate,true);
        if (!TestTrue(TEXT("Configure loaded courses 7 and 42 plus one designer-only course"),
            Start->Configure(7,1,Error) && Finish->Configure(7,2,Error) &&
            VerticalGate->Configure(42,1,Error) && LaterGate->Configure(42,3,Error) && DesignerGate->Configure(99,1,Error))) return false;
        for (auto* Gate:{Start,Finish}) {
            Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
            Powered->SetPropertyValue_InContainer(Gate->GetPowerInfo(),true);
        }
        auto Browser=SNew(SFactoryFPVRacePanel).Owner(Hangar).Embedded(true);
        if (!TestTrue(TEXT("Course picker discovers only distinct loaded IDs in numeric order"),Browser->CourseOptions.Num()==2 &&
            *Browser->CourseOptions[0]==7 && *Browser->CourseOptions[1]==42)) return false;
        const auto Course7=Browser->CourseOptions[0];
        const auto Course42=Browser->CourseOptions[1];
        TestTrue(TEXT("Initial free-flight browser shows first loaded course without selecting it"),
            Browser->ViewedCourseId==7 && Browser->CoursePicker->GetSelectedItem()==Course7 && Hangar->GetSelectedRacingCourse()==0);
        TestTrue(TEXT("Course label reports real loaded checkpoint count"),Browser->CourseOptionLabel(7).ToString().Contains(TEXT("2 checkpoints")));
        TestTrue(TEXT("A course without a record explains how to capture a ghost"),Browser->GhostStatus.Contains(TEXT("first lap")));
        Browser->RefreshCourse();
        TestTrue(TEXT("Steady refresh preserves course option identities and selection"),Browser->CourseOptions[0]==Course7 &&
            Browser->CourseOptions[1]==Course42 && Browser->CoursePicker->GetSelectedItem()==Course7);
        // The SDK editor disables SML world modules for this target. Supply a
        // name snapshot here and leave world writes to the Shipping fixture.
        Browser->CourseNames.Add(2,TEXT("Far Track"));
        Browser->CourseNames.Add(7,TEXT("Refinery Sprint"));
        Browser->bCourseNamesAvailable=true;
        Browser->ViewedCourseId=0;
        Browser->RefreshCoursePicker();
        Browser->RefreshCourseNameInput();
        TestTrue(TEXT("The picker includes a named unloaded ID and defaults to a loaded course"),
            Browser->CourseOptions.Num()==3 && *Browser->CourseOptions[0]==2 && Browser->CoursePicker->GetSelectedItem()==Course7 &&
            Browser->CourseOptionLabel(2).ToString()==TEXT("Far Track · #2 (not loaded)") &&
            Browser->CourseOptionLabel(7).ToString()==TEXT("Refinery Sprint · #7 (2 checkpoints)"));
        Browser->CourseNameInput->SetText(FText::FromString(TEXT("Unsubmitted rename")));
        Browser->RefreshCourseNameInput();
        TestTrue(TEXT("Name refresh keeps typing separate from checkpoint edits and the saved label"),Browser->HasCourseNameEdit() &&
            Browser->CourseNameInput->GetText().ToString()==TEXT("Unsubmitted rename") && !Browser->HasDraftChanges() &&
            Browser->CourseLabel(7)==TEXT("Refinery Sprint · #7"));
        FFactoryFPVRaceDebrief Report;
        Report.CourseId=42;Report.CourseName=TEXT("Last race fixture");Report.LapCount=2;Report.FastestLap=20.75;
        Report.RecentLaps={{1,22.,0.,true},{2,20.75,22.,true}};
        Report.LastLapSplits={{2,9.25,10.}};
        Hangar->StoreRaceDebrief(World,Report);
        const auto LastRace=FindButton(Browser,TEXT("Last race"));
        if (!TestNotNull(TEXT("Actual embedded last race button"),LastRace.Get())) return false;
        LastRace->SimulateClick();
        const auto Summary=FindText(Browser,FName(TEXT("FactoryFPVRaceDebriefSummary")));
        TestTrue(TEXT("Debrief shows the most recent scored session independently of the viewed course"),Browser->IsShowingDebrief() &&
            Summary && Summary->GetText().ToString().Contains(TEXT("2 completed laps  /  Session fastest 20.750 s")) && Browser->ViewedCourseId==7);
        LastRace->SimulateClick();
        TestTrue(TEXT("Returning to course tools preserves the active browser and name draft"),!Browser->IsShowingDebrief() &&
            Browser->CoursePicker->GetSelectedItem()==Course7 && Browser->CourseNameInput->GetText().ToString()==TEXT("Unsubmitted rename") &&
            Hangar->GetSelectedRacingCourse()==0);
        Browser->ViewedCourseId=2;
        Browser->RefreshCourseNameInput();
        Browser->CourseNameInput->SetText(FText::GetEmpty());
        TestTrue(TEXT("A named unloaded course can be cleared explicitly"),Browser->CanEditCourseName() && Browser->HasCourseNameEdit());
        Browser->ViewedCourseId=7;
        Browser->RefreshCourse();
        Browser->CoursePicker->SetSelectedItem(Course42);
        TestTrue(TEXT("Picker can browse an incomplete course without changing next flight"),Browser->ViewedCourseId==42 &&
            !Browser->bCourseReady && Browser->CourseStatus.Contains(TEXT("Checkpoint 2 is missing")) && Hangar->GetSelectedRacingCourse()==0);
        Browser->CoursePicker->SetSelectedItem(Course7);
        const auto BuildViewed=FindButton(Browser,TEXT("Build on this course"));
        const auto FinishBuild=FindButton(Browser,TEXT("Finish building"));
        if (!TestNotNull(TEXT("Loaded course exposes the build target action"),BuildViewed.Get()) ||
            !TestNotNull(TEXT("Loaded course exposes the finish building action"),FinishBuild.Get())) return false;
        BuildViewed->SimulateClick();
        TestTrue(TEXT("Build target reports the next existing course number without queueing a race"),Hangar->GetRaceBuildCourse()==7 &&
            Browser->BuildCourseStatus.Contains(TEXT("next checkpoint 3")) && Hangar->GetSelectedRacingCourse()==0);
        FinishBuild->SimulateClick();
        Browser->SlatePrepass();
        Browser->RaceButton->SimulateClick();
        TestEqual(TEXT("Only the Race course action selects the viewed track for flight"),Hangar->GetSelectedRacingCourse(),7);
        auto Selected=SNew(SFactoryFPVRacePanel).Owner(Hangar).Embedded(true);
        TestEqual(TEXT("Reopening browser preserves explicitly selected track"),Selected->ViewedCourseId,7);
        auto Direct=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(VerticalGate);
        TestTrue(TEXT("Direct checkpoint editing opens its own course while preserving next flight"),
            Direct->ViewedCourseId==42 && Direct->EditingGate.Get()==VerticalGate && Hangar->GetSelectedRacingCourse()==7);
        if (!RenderEditor(Direct,{Direct->CoursePicker,Direct->ApplyButton,Direct->RaceButton},TEXT("RaceCoursePicker-1280x720.png"),false)) return false;
        Direct->DraftGateOrder=5;
        Direct->CoursePicker->SetSelectedItem(Direct->CourseOptions[0]);
        TestTrue(TEXT("Browsing a different course preserves the actual edited checkpoint and draft"),
            Direct->ViewedCourseId==7 && Direct->EditingGate.Get()==VerticalGate && Direct->DraftGateOrder==5 && Direct->HasDraftChanges());
        Direct->CoursePicker->SetSelectedItem(Direct->CourseOptions[1]);
        LaterGate->Destroy();
        Direct->RefreshCourse();
        TestTrue(TEXT("Course count updates in place when a checkpoint disappears"),Direct->CourseOptionLabel(42).ToString().Contains(TEXT("1 checkpoint")));
        VerticalGate->Destroy();
        Direct->RefreshCourse();
        TestTrue(TEXT("An unavailable viewed course keeps its label and removes the dead dropdown option"),Direct->ViewedCourseId==42 &&
            Direct->CourseOptionLabel(42).ToString().Contains(TEXT("not loaded")) && Direct->CourseOptions.Num()==1 && !Direct->bCourseReady);
        Start->Destroy();Finish->Destroy();DesignerGate->Destroy();
        Selected->RefreshCourse();
        TestTrue(TEXT("Losing all loaded courses does not change the selected next flight"),Selected->CourseOptions.IsEmpty() &&
            Selected->ViewedCourseId==7 && !Selected->bCourseReady && Hangar->GetSelectedRacingCourse()==7);
        Hangar->SetSelectedRacingCourse(0);
        LastRace->SimulateClick();
        const auto RenderedSummary=FindText(Browser,FName(TEXT("FactoryFPVRaceDebriefSummary")));
        if (!RenderEditor(Browser,{LastRace,RenderedSummary},TEXT("RaceDebrief-1280x720.png"),true)) return false;
    }
    auto* First=World->SpawnActor<AFactoryFPVRaceGate>(FVector::ZeroVector,FRotator::ZeroRotator,Spawn);
    auto* Second=World->SpawnActor<AFactoryFPVRaceGate>(FVector(1000,0,0),FRotator::ZeroRotator,Spawn);
    First->EnsureIdentityAndOrder();
    Second->EnsureIdentityAndOrder();
    First->Configure(1,1,Error);
    Second->Configure(1,2,Error);
    TestEqual(TEXT("Race selection starts in free flight"),Hangar->GetSelectedRacingCourse(),0);

    PC->bShowMouseCursor=false;
    PC->SetIgnoreMoveInput(true);
    PC->SetIgnoreLookInput(true);
    for (int32 Index=0;Index<3;++Index) {
        Hangar->OpenRaceGatePanel(First);
        TestTrue(TEXT("E configuration opens the standalone racing panel"),Hangar->RacePanel.IsValid() && Hangar->IsPanelOpen());
        TestTrue(TEXT("Race panel owns cursor and its move/look input holds"),PC->bShowMouseCursor && PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
        TestEqual(TEXT("Race panel adds exactly one shared overlay"),ViewportOverlay->GetChildren()->Num(),BaselineOverlays+1);
        Hangar->OpenRaceGatePanel(Second);
        TestEqual(TEXT("Switching edited gates never stacks overlays"),ViewportOverlay->GetChildren()->Num(),BaselineOverlays+1);
        Hangar->TogglePanel();
        TestFalse(TEXT("F6 closes either panel type"),Hangar->IsPanelOpen());
        TestFalse(TEXT("Closing restores the original cursor"),PC->bShowMouseCursor);
        TestTrue(TEXT("Closing preserves an independent move/look input hold"),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
        TestEqual(TEXT("Repeated panel close restores viewport overlay count"),ViewportOverlay->GetChildren()->Num(),BaselineOverlays);
    }
    PC->SetIgnoreMoveInput(false);
    PC->SetIgnoreLookInput(false);
    TestFalse(TEXT("Repeated race panel transitions leak no move-input hold"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Repeated race panel transitions leak no look-input hold"),PC->IsLookInputIgnored());

    {
        FInputActionUnifiedDelegate HangarKey;
        for (const auto& Binding:Hangar->Controls->KeyBindings) {
            if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed) HangarKey=Binding.KeyDelegate;
        }
        if (!TestTrue(TEXT("Dirty editor regression has the real F6 binding"),HangarKey.IsBoundToObject(Hangar))) return false;
        const auto PressDialogButton=[&](const TCHAR* Label) {
            const auto Button=FindButton(ViewportOverlay,Label);
            if (!TestNotNull(Label,Button.Get())) return false;
            Button->SimulateClick();
            return true;
        };
        const auto IsClosedCleanly=[&] {
            return !Hangar->IsPanelOpen() && !Hangar->DiscardDialog && !Hangar->InputLease.IsActive() &&
                !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() &&
                ViewportOverlay->GetChildren()->Num()==BaselineOverlays;
        };
        Hangar->OpenRaceGatePanel(First);
        const auto DirtyEditor=Hangar->RacePanel;
        if (!TestNotNull(TEXT("Dirty standalone checkpoint editor"),DirtyEditor.Get())) return false;
        const auto OriginalOverlay=Hangar->Overlay;
        DirtyEditor->DraftGateOrder=9;
        const auto KeepsOriginalDraft=[&] {
            return Hangar->RacePanel==DirtyEditor && Hangar->Overlay==OriginalOverlay && DirtyEditor->DraftGateOrder==9 &&
                DirtyEditor->HasDraftChanges() && First->GetGateOrder()==1 && Hangar->InputLease.IsActive() &&
                Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook() && PC->bShowMouseCursor &&
                PC->IsMoveInputIgnored() && PC->IsLookInputIgnored() && ViewportOverlay->GetChildren()->Num()==BaselineOverlays+1;
        };
        HangarKey.Execute(EKeys::F6);
        if (!TestTrue(TEXT("Bound F6 asks before closing the dirty editor and keeps its input lease"),Hangar->DiscardDialog && KeepsOriginalDraft())) return false;
        if (!RenderWidget(ViewportOverlay,{FindButton(ViewportOverlay,TEXT("Keep editing")),FindButton(ViewportOverlay,TEXT("Discard edits"))},
            TEXT("RaceDiscardDraft-1280x720.png"))) return false;
        if (!PressDialogButton(TEXT("Keep editing"))) return false;
        TestTrue(TEXT("Keep editing removes only the prompt and preserves the same dirty editor"),!Hangar->DiscardDialog && KeepsOriginalDraft());

        const FKeyEvent Escape(EKeys::Escape,FModifierKeysState(),0,false,0,0);
        TestTrue(TEXT("Slate Escape asks before closing the dirty editor"),DirtyEditor->OnKeyDown(FGeometry(),Escape).IsEventHandled() &&
            Hangar->DiscardDialog && KeepsOriginalDraft());
        const auto Dialog=Hangar->DiscardDialog;
        if (!TestNotNull(TEXT("Escape opened the actual discard dialog"),Dialog.Get())) return false;
        TestTrue(TEXT("Escape inside the prompt keeps editing"),Dialog->OnPreviewKeyDown(FGeometry(),Escape).IsEventHandled() &&
            !Hangar->DiscardDialog && KeepsOriginalDraft());

        const auto CloseButton=FindButton(DirtyEditor.ToSharedRef(),TEXT("Close   Esc"));
        if (!TestNotNull(TEXT("Actual checkpoint editor Close button"),CloseButton.Get())) return false;
        CloseButton->SimulateClick();
        TestTrue(TEXT("The Close button stages the same protected departure"),Hangar->DiscardDialog && KeepsOriginalDraft());
        if (!PressDialogButton(TEXT("Discard edits"))) return false;
        TestTrue(TEXT("Discard closes the editor, keeps the saved order, and restores input and overlay state"),
            First->GetGateOrder()==1 && IsClosedCleanly());

        Hangar->OpenRaceGatePanel(First);
        const auto Replacing=Hangar->RacePanel;
        if (!TestNotNull(TEXT("Standalone editor for the replacement probe"),Replacing.Get())) return false;
        Replacing->DraftGateOrder=10;
        Hangar->OpenRaceGatePanel(Second);
        TestTrue(TEXT("Opening a second gate asks before replacing the dirty editor"),Hangar->DiscardDialog &&
            Hangar->RacePanel==Replacing && Replacing->DraftGateOrder==10 && First->GetGateOrder()==1);
        Hangar->OpenPanel();
        if (!PressDialogButton(TEXT("Discard edits"))) return false;
        TestTrue(TEXT("Discard resumes the originally requested gate replacement with one owned overlay"),
            !Hangar->DiscardDialog && Hangar->RacePanel && Hangar->RacePanel!=Replacing &&
            Hangar->RacePanel->EditingGate.Get()==Second && !Hangar->IsHangarOpen() && First->GetGateOrder()==1 &&
            Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook() && ViewportOverlay->GetChildren()->Num()==BaselineOverlays+1);
        HangarKey.Execute(EKeys::F6);
        TestTrue(TEXT("Bound F6 closes the clean replacement directly"),IsClosedCleanly());

        Hangar->OpenRaceGatePanel(First);
        if (!TestNotNull(TEXT("Standalone editor for the forced teardown probe"),Hangar->RacePanel.Get())) return false;
        Hangar->RacePanel->DraftGateOrder=11;
        Hangar->OpenPanel();
        TestTrue(TEXT("Opening the hangar asks before replacing the dirty editor"),Hangar->DiscardDialog && !Hangar->IsHangarOpen());
        Hangar->ClosePanel();
        TestTrue(TEXT("Forced teardown removes both the pending prompt and editor and restores the cursor"),IsClosedCleanly());

        Hangar->OpenRaceGatePanel(First);
        if (!TestNotNull(TEXT("Standalone editor for the unbind probe"),Hangar->RacePanel.Get())) return false;
        Hangar->RacePanel->DraftGateOrder=12;
        Hangar->RequestClosePanel();
        TestTrue(TEXT("User close request stages a prompt before controller loss"),Hangar->DiscardDialog.IsValid());
        Hangar->UnbindController();
        TestTrue(TEXT("Unbind clears the pending action, both widgets, and input ownership"),
            !Hangar->DiscardDialog && !Hangar->IsPanelOpen() && !Hangar->InputLease.IsActive() &&
            !PC->bShowMouseCursor && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() && ViewportOverlay->GetChildren()->Num()==0);
        if (!TestTrue(TEXT("Race fixture rebinds after modal teardown"),Hangar->BindController(PC,Viewport))) return false;
        TestTrue(TEXT("Rebind restores only the normal shortcut overlay"),IsClosedCleanly());
    }

    // The adapter wraps real Super reset calls with the same lease observer used
    // by Shipping hooks. No SDK health/menu flags or global Editor hooks are
    // fabricated. The SDK's nonvirtual IsAliveAndWell is a false stub, so call
    // Tick's actual renewal helper rather than pretending to tick a live pioneer.
    for (const bool RacePanel:{false,true}) {
        const FString Kind=RacePanel ? TEXT("Race panel") : TEXT("Hangar");
        const auto Open=[&] {
            if (RacePanel) Hangar->OpenRaceGatePanel(First);
            else Hangar->OpenPanel();
            return TestTrue(*(Kind+TEXT(" opens with its own move/look lease")),Hangar->IsPanelOpen() &&
                Hangar->InputLease.IsActive() && Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook());
        };
        const auto CheckAndReleaseExternal=[&] {
            TestTrue(*(Kind+TEXT(" close retains both newer external holds")),PC->IsMoveInputIgnored() && PC->IsLookInputIgnored());
            PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);
            TestFalse(*(Kind+TEXT(" needs exactly one external move release, without a leaked increment")),PC->IsMoveInputIgnored());
            TestFalse(*(Kind+TEXT(" needs exactly one external look release, without a leaked increment")),PC->IsLookInputIgnored());
            TestFalse(*(Kind+TEXT(" close retires its lease")),Hangar->InputLease.IsActive());
        };

        if (!Open()) return false;
        PC->ResetIgnoreInputFlags();
        TestFalse(*(Kind+TEXT(" reset withdraws both old owned increments")),Hangar->InputLease.OwnsMove() || Hangar->InputLease.OwnsLook());
        PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
        Hangar->ClosePanel(); // No Tick can conceal an incorrect stale release.
        CheckAndReleaseExternal();
        Hangar->ClosePanel();
        TestFalse(*(Kind+TEXT(" repeated close is harmless")),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());

        if (!Open()) return false;
        PC->ResetIgnoreInputFlags();
        PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
        Hangar->RenewPanelInputLease();
        TestTrue(*(Kind+TEXT(" renews its own increments over newer external holds")),Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook());
        Hangar->RenewPanelInputLease();
        Hangar->TogglePanel();
        CheckAndReleaseExternal();

        if (!Open()) return false;
        PC->ResetIgnoreMoveInput();PC->SetIgnoreMoveInput(true);
        TestTrue(*(Kind+TEXT(" move-only reset preserves the existing owned look increment")),!Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook());
        Hangar->RenewPanelInputLease();
        PC->ResetIgnoreLookInput();PC->SetIgnoreLookInput(true);
        TestTrue(*(Kind+TEXT(" look-only reset preserves the renewed owned move increment")),Hangar->InputLease.OwnsMove() && !Hangar->InputLease.OwnsLook());
        Hangar->RenewPanelInputLease();
        Hangar->UnbindController();
        CheckAndReleaseExternal();
        if (!TestTrue(*(Kind+TEXT(" rebinds after owned teardown")),Hangar->BindController(PC,Viewport))) return false;

        if (!Open()) return false;
        auto* Console=NewObject<UConsole>(Viewport);
        Viewport->ViewportConsole=Console;Console->ConsoleState=TEXT("Typing");
        PC->ResetIgnoreInputFlags();
        PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
        Hangar->RenewPanelInputLease();
        TestFalse(*(Kind+TEXT(" leaves withdrawn increments alone while the actual console owns interaction")),Hangar->InputLease.OwnsMove() || Hangar->InputLease.OwnsLook());
        Console->ConsoleState=NAME_None;
        Hangar->RenewPanelInputLease();
        TestTrue(*(Kind+TEXT(" renews only after the console releases interaction")),Hangar->InputLease.OwnsMove() && Hangar->InputLease.OwnsLook());
        Hangar->ClosePanel();
        CheckAndReleaseExternal();
        Viewport->ViewportConsole=nullptr;
        TestEqual(*(Kind+TEXT(" reset/renew transitions restore the overlay baseline")),ViewportOverlay->GetChildren()->Num(),BaselineOverlays);
    }

    Hangar->OpenRaceGatePanel(First);
    auto Draft=Hangar->RacePanel;
    Draft->DraftCourseId=4;
    Draft->DraftGateOrder=8;
    TestTrue(TEXT("Changing numeric controls creates only a draft"),Draft->HasDraftChanges());
    TestTrue(TEXT("Opening/editing never silently changes the world gate"),First->GetCourseId()==1 && First->GetGateOrder()==1);
    if (!TestEqual(TEXT("Standalone edit regression has two real gate rows"),Draft->GateRows->GetChildren()->Num(),2)) return false;
    auto DraftFree=FindButton(Draft.ToSharedRef(),TEXT("Free flight"));
    auto DraftOther=FindButton(Draft->GateRows->GetChildren()->GetChildAt(1),TEXT("Edit"));
    if (!TestNotNull(TEXT("Standalone free-flight action button"),DraftFree.Get()) ||
        !TestNotNull(TEXT("Other gate's actual Edit button"),DraftOther.Get())) return false;
    Draft->RefreshCourse();
    TestTrue(TEXT("Refreshing the same course preserves its actual Edit button"),FindButton(Draft->GateRows->GetChildren()->GetChildAt(1),TEXT("Edit"))==DraftOther);
    DraftFree->SimulateClick();
    TestTrue(TEXT("Free flight cannot close a standalone editor with pending edits"),Hangar->RacePanel==Draft && Draft->HasDraftChanges());
    TestTrue(TEXT("Blocked free-flight action explains Apply/Revert"),Draft->Message.Contains(TEXT("Apply or revert")));
    DraftOther->SimulateClick();
    TestTrue(TEXT("Another gate's Edit action preserves the current gate and draft"),Draft->EditingGate.Get()==First && Draft->DraftCourseId==4 && Draft->DraftGateOrder==8);
    Draft->EditGate(First);
    TestTrue(TEXT("Reselecting the same gate does not silently reload its stored values"),Draft->DraftCourseId==4 && Draft->DraftGateOrder==8);
    Draft->RevertGate();
    DraftOther->SimulateClick();
    TestTrue(TEXT("Explicit Revert permits subsequent Edit selection"),Draft->EditingGate.Get()==Second && !Draft->HasDraftChanges());
    Draft->Close();
    TestTrue(TEXT("Closing without Apply preserves saved gate configuration"),First->GetCourseId()==1 && First->GetGateOrder()==1);
    Hangar->OpenRaceGatePanel(First);
    auto Editor=Hangar->RacePanel;
    Editor->DraftGateOrder=3;
    Editor->ApplyButton->SimulateClick();
    TestEqual(TEXT("Apply button commits through real gate Configure"),First->GetGateOrder(),3);
    Editor->DraftGateOrder=5;
    Editor->RevertGate();
    TestEqual(TEXT("Revert reloads the actual stored gate number"),Editor->DraftGateOrder,3);
    TestFalse(TEXT("Nonconsecutive or missing start gate prevents racing"),Editor->bCourseReady);
    Editor->DraftGateOrder=1;
    Editor->ApplyGate();
    TestFalse(TEXT("A valid layout still requires live reported power"),Editor->bCourseReady);
    Editor->RaceCourse();
    TestEqual(TEXT("Rejected unpowered course does not replace free flight selection"),Hangar->GetSelectedRacingCourse(),0);
    TestTrue(TEXT("Rejected course explains its power requirement"),Editor->Message.Contains(TEXT("power")));

    for (auto* Gate:{First,Second}) {
        Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
        Powered->SetPropertyValue_InContainer(Gate->GetPowerInfo(),true);
    }
    Editor->RefreshCourse();
    TestTrue(TEXT("Consecutive powered course becomes race-ready"),Editor->bCourseReady);
    TestTrue(TEXT("Loaded table is sorted by persisted checkpoint order"),Editor->LoadedGates.Num()==2 && Editor->LoadedGates[0].Get()==First && Editor->LoadedGates[1].Get()==Second);
    auto* Preview=World->SpawnActor<AFactoryFPVRaceGate>(FVector(2000,0,0),FRotator::ZeroRotator,Spawn);
    DesignerFlag->SetPropertyValue_InContainer(Preview,true);
    Preview->Configure(1,3,Error);
    Editor->RefreshCourse();
    TestTrue(TEXT("Configuration table excludes designer previews without invalidating readiness"),Editor->LoadedGates.Num()==2 && Editor->bCourseReady);
    Editor->EditGate(Preview);
    TestTrue(TEXT("Direct preview editing stays available and clearly marked"),Editor->CanEditGate() && Editor->GateHeading().ToString().Contains(TEXT("NOT COUNTING")));
    Editor->DraftGateOrder=2;Editor->ApplyButton->SimulateClick();
    TestTrue(TEXT("Designer preview may store a number already used by a live gate"),
        Preview->GetGateOrder()==2 && Second->GetGateOrder()==2 && !Editor->PendingOrderSwap);
    TestTrue(TEXT("Editing preview does not add it to the live course table"),Editor->LoadedGates.Num()==2 && Editor->bCourseReady);
    Editor->DraftGateOrder=3;Editor->ApplyButton->SimulateClick();
    Editor->EditGate(First);
    Editor->DraftGateOrder=3;Editor->ApplyButton->SimulateClick();
    TestTrue(TEXT("A designer preview cannot reserve a live slot or offer a swap"),
        First->GetGateOrder()==3 && Preview->GetGateOrder()==3 && !Editor->PendingOrderSwap);
    Editor->DraftGateOrder=1;Editor->ApplyButton->SimulateClick();
    Editor->EditGate(Second);
    Preview->Destroy();
    TestTrue(TEXT("Edit selection loads the chosen gate's actual settings"),Editor->EditingGate.Get()==Second && Editor->DraftGateOrder==2 && !Editor->HasDraftChanges());
    const FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World,1);
    First->RecordLap(15,Layout);
    Editor->ResetBest();
    TestEqual(TEXT("Checkpoint editing cannot reset start/finish PB"),First->GetBestLapSeconds(Layout),15.);
    Editor->EditGate(First);
    TestTrue(TEXT("Panel shows PB only for its exact current layout"),Editor->BestLapStatus.Contains(TEXT("15.000")));
    TestTrue(TEXT("A best time without a trace keeps its time and explains how to record a ghost"),
        Editor->GhostStatus.Contains(TEXT("Set a new best")));
    auto Reset=FindButton(Editor.ToSharedRef(),TEXT("Reset saved best for course 1"));
    if (!TestNotNull(TEXT("Reset button explicitly names the saved course it targets"),Reset.Get())) return false;
    auto* OtherStart=World->SpawnActor<AFactoryFPVRaceGate>(FVector(3000,0,0),FRotator::ZeroRotator,Spawn);
    auto* OtherCheckpoint=World->SpawnActor<AFactoryFPVRaceGate>(FVector(4000,0,0),FRotator::ZeroRotator,Spawn);
    OtherStart->EnsureIdentityAndOrder();OtherCheckpoint->EnsureIdentityAndOrder();
    OtherStart->Configure(2,1,Error);OtherCheckpoint->Configure(2,2,Error);
    const FString OtherLayout=UFactoryFPVRaceComponent::LayoutSignature(World,2);
    if (!TestTrue(TEXT("Independent displayed course has a real saved best"),OtherStart->RecordLap(20,OtherLayout))) return false;
    Editor->ViewedCourseId=2;Editor->RefreshCourse();
    TestTrue(TEXT("View selector can display a different course's best without changing the edited gate"),Editor->BestLapStatus.Contains(TEXT("20.000")) && Editor->EditingGate.Get()==First);
    Reset->Invalidate(EInvalidateWidgetReason::Layout);Reset->SlatePrepass();
    TestFalse(TEXT("A different viewed course disables the explicitly targeted reset"),Reset->IsEnabled());
    // SimulateClick deliberately bypasses Slate's enabled check, exercising the
    // production handler guard as well as the actual button's disabled state.
    Reset->SimulateClick();
    TestTrue(TEXT("Wrong-target reset preserves both real saved bests"),First->GetBestLapSeconds(Layout)==15. && OtherStart->GetBestLapSeconds(OtherLayout)==20.);
    TestTrue(TEXT("Wrong-target reset names the course to view"),Editor->Message.Contains(TEXT("View course 1")));
    Editor->ViewedCourseId=1;Editor->RefreshCourse();
    Editor->DraftCourseId=2;Editor->DraftGateOrder=4;
    Reset->Invalidate(EInvalidateWidgetReason::Layout);Reset->SlatePrepass();
    TestFalse(TEXT("Unapplied course/gate draft disables the destructive reset"),Reset->IsEnabled());
    Reset->SimulateClick();
    TestTrue(TEXT("Blocked reset preserves the pending course/gate draft and both saved bests"),Editor->DraftCourseId==2 && Editor->DraftGateOrder==4 && First->GetBestLapSeconds(Layout)==15. && OtherStart->GetBestLapSeconds(OtherLayout)==20.);
    TestTrue(TEXT("Draft-blocked reset gives Apply/Revert guidance"),Editor->Message.Contains(TEXT("Apply or revert")));
    Editor->RevertGate();
    Reset->Invalidate(EInvalidateWidgetReason::Layout);Reset->SlatePrepass();
    TestTrue(TEXT("Matching viewed/saved course with no draft enables reset"),Reset->IsEnabled());
    auto ConfirmReset=FindButton(Editor.ToSharedRef(),TEXT("Confirm reset"));
    auto CancelReset=FindButton(Editor.ToSharedRef(),TEXT("Cancel reset"));
    if (!TestNotNull(TEXT("Actual confirmation button"),ConfirmReset.Get()) ||
        !TestNotNull(TEXT("Actual cancellation button"),CancelReset.Get())) return false;

    Hangar->SetSelectedRacingCourse(1);
    const FVector SecondLocation=Second->GetActorLocation();
    Second->SetActorLocation(SecondLocation+FVector(50,0,0));
    Editor->RefreshCourse();
    Reset->Invalidate(EInvalidateWidgetReason::Layout);Reset->SlatePrepass();
    TestTrue(TEXT("A different valid layout has no visible best and reset is disabled"),
        Editor->bCourseReady && !Reset->IsEnabled() && Editor->BestLapStatus.Contains(TEXT("no completed lap")));
    TestTrue(TEXT("Next flight asks for reselect when the viewed selected layout changes"),
        Editor->SelectedMode().ToString().Contains(TEXT("Reselect course 1")) && Hangar->GetSelectedRacingCourse()==1);
    Reset->SimulateClick();
    TestTrue(TEXT("Resetting the mismatched layout cannot stage or delete the old record"),
        !Editor->PendingBestReset && First->GetBestLapSeconds(Layout)==15.);
    const FString AlternateLayout=UFactoryFPVRaceComponent::LayoutSignature(World,1);
    if (!TestTrue(TEXT("A different configuration of the same course can hold a separate best"),
        !AlternateLayout.IsEmpty() && AlternateLayout!=Layout && First->RecordLap(18,AlternateLayout))) return false;
    Editor->RefreshCourse();
    TestTrue(TEXT("Panel shows the alternate record when that layout is active"),Editor->BestLapStatus.Contains(TEXT("18.000")));
    Second->SetActorLocation(SecondLocation);
    Editor->RefreshCourse();
    TestTrue(TEXT("Panel shows the original record after restoring the original layout"),Editor->BestLapStatus.Contains(TEXT("15.000")));
    TestTrue(TEXT("Restoring the original layout restores the selected mode label"),
        Editor->SelectedMode().ToString().Contains(TEXT("Course 1 time trial")));
    Hangar->SetSelectedRacingCourse(0);

    Reset->SimulateClick();
    TestTrue(TEXT("First click asks for confirmation and keeps the existing record"),
        Editor->PendingBestReset && Editor->PendingBestReset->Seconds==15. && First->GetBestLapSeconds(Layout)==15.);
    if (!RenderEditor(Editor.ToSharedRef(),{ConfirmReset,CancelReset},TEXT("RaceBestReset-1280x720.png"),false)) return false;
    CancelReset->SimulateClick();
    TestTrue(TEXT("Cancel clears only the prompt and keeps the record"),!Editor->PendingBestReset && First->GetBestLapSeconds(Layout)==15.);

    Reset->SimulateClick();
    Second->Configure(1,1,Error); // An external edit before the next UI refresh.
    ConfirmReset->SimulateClick();
    TestTrue(TEXT("A duplicate order introduced before confirmation refuses reset"),
        !Editor->PendingBestReset && Editor->Message.Contains(TEXT("changed")) && First->GetBestLapSeconds(Layout)==15.);
    Second->Configure(1,2,Error);
    Editor->RefreshCourse();

    Reset->SimulateClick();
    if (!TestTrue(TEXT("A new record can replace the staged record before confirmation"),First->RecordLap(12,Layout))) return false;
    ConfirmReset->SimulateClick();
    TestTrue(TEXT("Confirmation for the previous time leaves the newer record intact"),
        !Editor->PendingBestReset && First->GetBestLapSeconds(Layout)==12.);

    Reset->SimulateClick();
    ConfirmReset->SimulateClick();
    TestEqual(TEXT("Confirmed start/finish reset clears only its intended record"),First->GetBestLapSeconds(Layout),0.);
    TestEqual(TEXT("Reset leaves the alternate layout on this same start gate intact"),First->GetBestLapSeconds(AlternateLayout),18.);
    TestEqual(TEXT("Valid reset leaves the other course's saved best intact"),OtherStart->GetBestLapSeconds(OtherLayout),20.);

    {
        using EDirection=EFactoryFPVRaceGateDirection;
        auto* ReorderStart=World->SpawnActor<AFactoryFPVRaceGate>(FVector(5000,1000,0),FRotator::ZeroRotator,Spawn);
        auto* ReorderPartner=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(6000,1000,0),FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("Native reorder start gate"),ReorderStart) ||
            !TestNotNull(TEXT("Native reorder vertical checkpoint"),ReorderPartner)) return false;
        for (auto* Gate:{ReorderStart,static_cast<AFactoryFPVRaceGate*>(ReorderPartner)}) {
            Gate->EnsureIdentityAndOrder();
            Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
            Powered->SetPropertyValue_InContainer(Gate->GetPowerInfo(),true);
        }
        if (!TestTrue(TEXT("Two distinct powered checkpoints define the original order"),
            ReorderStart->Configure(7,1,Error) && ReorderPartner->Configure(7,2,1,EDirection::Descend,Error) &&
            UFactoryFPVRaceComponent::ValidateCourse(World,7,Error,true))) return false;
        const FString ReorderLayout=UFactoryFPVRaceComponent::LayoutSignature(World,7);
        if (!TestTrue(TEXT("Record the reorder course's original best"),ReorderStart->RecordLap(16,ReorderLayout))) return false;
        const FGuid StartId=ReorderStart->GetGateId(),PartnerId=ReorderPartner->GetGateId();
        Hangar->SetSelectedRacingCourse(7);
        auto SwapPanel=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(ReorderStart);
        const auto OriginalRecordKept=[&] {
            return ReorderStart->GetCourseId()==7 && ReorderStart->GetGateOrder()==1 &&
                ReorderStart->GetBestLapSeconds(ReorderLayout)==16. && OtherStart->GetBestLapSeconds(OtherLayout)==20.;
        };
        SwapPanel->DraftCourseId=2;
        SwapPanel->ApplyButton->SimulateClick();
        TestTrue(TEXT("Occupied cross-course destination refuses the move and retains both saved bests"),
            SwapPanel->HasDraftChanges() && SwapPanel->DraftCourseId==2 && !SwapPanel->PendingOrderSwap &&
            SwapPanel->Message.Contains(TEXT("already")) && OriginalRecordKept() && OtherStart->GetGateOrder()==1);
        SwapPanel->RevertGate();

        SwapPanel->DraftGateOrder=2;
        SwapPanel->ApplyButton->SimulateClick();
        TestTrue(TEXT("Apply stages an explicit same-course swap without touching the gates or records"),
            SwapPanel->PendingOrderSwap && ReorderPartner->GetGateOrder()==2 && OriginalRecordKept());
        const auto ConfirmSwap=FindButton(SwapPanel,TEXT("Swap numbers"));
        const auto CancelSwap=FindButton(SwapPanel,TEXT("Cancel swap"));
        if (!TestNotNull(TEXT("Actual swap confirmation button"),ConfirmSwap.Get()) ||
            !TestNotNull(TEXT("Actual swap cancellation button"),CancelSwap.Get())) return false;
        if (!RenderEditor(SwapPanel,{ConfirmSwap,CancelSwap},TEXT("RaceOrderSwap-1280x720.png"),false)) return false;
        CancelSwap->SimulateClick();
        TestTrue(TEXT("Cancel swap keeps the draft, original checkpoint order and record"),
            !SwapPanel->PendingOrderSwap && SwapPanel->HasDraftChanges() && SwapPanel->DraftGateOrder==2 &&
            ReorderPartner->GetGateOrder()==2 && OriginalRecordKept());

        auto* Replacement=World->SpawnActor<AFactoryFPVRaceGate>(FVector(7000,1000,0),FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("Different actor for the stale slot probe"),Replacement)) return false;
        Replacement->EnsureIdentityAndOrder();
        if (!TestTrue(TEXT("Load a duplicate number through the compatibility path"),Replacement->Configure(7,2,Error))) return false;
        SwapPanel->ApplyButton->SimulateClick();
        TestTrue(TEXT("Ambiguous live slot refuses any swap and keeps the original record"),
            !SwapPanel->PendingOrderSwap && ReorderPartner->GetGateOrder()==2 && Replacement->GetGateOrder()==2 && OriginalRecordKept());
        if (!TestTrue(TEXT("Move the external duplicate out of the destination"),Replacement->Configure(7,3,Error))) return false;
        SwapPanel->ApplyButton->SimulateClick();
        if (!TestTrue(TEXT("Native external changes put a different actor in the staged slot"),
            SwapPanel->PendingOrderSwap && ReorderPartner->Configure(7,3,Error) && Replacement->Configure(7,2,Error))) return false;
        ConfirmSwap->SimulateClick();
        TestTrue(TEXT("Stale swap refuses to move either actor and retains the draft and original record"),
            !SwapPanel->PendingOrderSwap && SwapPanel->HasDraftChanges() && SwapPanel->DraftGateOrder==2 &&
            ReorderPartner->GetGateOrder()==3 && Replacement->GetGateOrder()==2 && OriginalRecordKept());
        Replacement->Destroy();
        if (!TestTrue(TEXT("Restore the exact original partner before staging a fresh swap"),ReorderPartner->Configure(7,2,Error))) return false;

        SwapPanel->ApplyButton->SimulateClick();
        ConfirmSwap->SimulateClick();
        const FString ReversedLayout=UFactoryFPVRaceComponent::LayoutSignature(World,7);
        TestTrue(TEXT("Confirmed swap changes both orders and keeps identities and vertical direction"),
            !SwapPanel->PendingOrderSwap && !SwapPanel->HasDraftChanges() &&
            ReorderStart->GetGateOrder()==2 && ReorderPartner->GetGateOrder()==1 &&
            ReorderStart->GetGateId()==StartId && ReorderPartner->GetGateId()==PartnerId && ReorderPartner->GetRaceDirection()==EDirection::Descend);
        TestTrue(TEXT("Swapped order stays race-ready without borrowing the prior layout's best or updating the selected launch"),
            UFactoryFPVRaceComponent::ValidateCourse(World,7,Error,true) && ReversedLayout!=ReorderLayout &&
            ReorderPartner->GetBestLapSeconds(ReversedLayout)==0. && SwapPanel->ViewedBestSeconds==0. &&
            Hangar->GetSelectedRacingLayout()==ReorderLayout && SwapPanel->SelectedMode().ToString().Contains(TEXT("Reselect course 7")));

        SwapPanel->DraftGateOrder=1;
        SwapPanel->ApplyButton->SimulateClick();
        ConfirmSwap->SimulateClick();
        TestTrue(TEXT("An exact reverse swap restores the original layout and its physical start gate's best"),
            !SwapPanel->HasDraftChanges() && ReorderPartner->GetGateOrder()==2 && OriginalRecordKept() &&
            UFactoryFPVRaceComponent::LayoutSignature(World,7)==ReorderLayout && SwapPanel->ViewedBestSeconds==16.);
        Hangar->SetSelectedRacingCourse(0);
        ReorderPartner->Destroy();ReorderStart->Destroy();
    }

    {
        constexpr int32 Course=72;
        using EDirection=EFactoryFPVRaceGateDirection;
        auto* Start=World->SpawnActor<AFactoryFPVRaceGate>(FVector(5000,2000,0),FRotator::ZeroRotator,Spawn);
        auto* Vertical=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(6000,2000,400),FRotator::ZeroRotator,Spawn);
        auto* Pole=World->SpawnActor<AFactoryFPVRacePole>(FVector(7000,2000,0),FRotator::ZeroRotator,Spawn);
        if (!TestNotNull(TEXT("Gap repair start"),Start) || !TestNotNull(TEXT("Gap repair vertical"),Vertical) ||
            !TestNotNull(TEXT("Gap repair pole"),Pole)) return false;
        for (auto* Gate:{Start,static_cast<AFactoryFPVRaceGate*>(Vertical),static_cast<AFactoryFPVRaceGate*>(Pole)}) {
            Gate->EnsureIdentityAndOrder();
            Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
            Powered->SetPropertyValue_InContainer(Gate->GetPowerInfo(),true);
        }
        const FGuid StartId=Start->GetGateId(),VerticalId=Vertical->GetGateId(),PoleId=Pole->GetGateId();
        const FTransform VerticalTransform=Vertical->GetActorTransform(),PoleTransform=Pole->GetActorTransform();
        if (!TestTrue(TEXT("Set up an ordered, powered course with a saved best"),
            Start->Configure(Course,1,Error) && Vertical->Configure(Course,2,1,EDirection::Descend,Error) &&
            Pole->Configure(Course,3,-1,Error) && UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error,true))) return false;
        const FString CompleteLayout=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
        if (!TestTrue(TEXT("Record an exact-layout best before introducing gaps"),Start->RecordLap(17.25,CompleteLayout))) return false;
        Hangar->SetSelectedRacingCourse(Course);
        if (!TestTrue(TEXT("Introduce gaps in two later checkpoint numbers"),
            Vertical->Configure(Course,3,Error) && Pole->Configure(Course,5,Error))) return false;
        auto Repair=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(Vertical).Embedded(true);
        const auto OpenRepair=FindButton(Repair,TEXT("Close numbering gaps"));
        const auto ConfirmRepair=FindButton(Repair,TEXT("Confirm renumber"));
        const auto CancelRepair=FindButton(Repair,TEXT("Cancel"));
        const auto NumberingPreview=FindText(Repair,FName(TEXT("FactoryFPVRaceCloseGapsPreview")));
        if (!TestNotNull(TEXT("Actual close gaps button"),OpenRepair.Get()) ||
            !TestNotNull(TEXT("Actual confirm renumber button"),ConfirmRepair.Get()) ||
            !TestNotNull(TEXT("Actual cancel renumber button"),CancelRepair.Get()) ||
            !TestNotNull(TEXT("Actual numbering preview"),NumberingPreview.Get())) return false;
        const auto OriginalBestKept=[&] { return Start->GetGateOrder()==1 && Start->GetGateId()==StartId &&
            Start->GetBestLapSeconds(CompleteLayout)==17.25 && Hangar->GetSelectedRacingLayout()==CompleteLayout; };
        Repair->DraftGateOrder=4;
        OpenRepair->SimulateClick();
        TestTrue(TEXT("Numbering repair cannot consume unapplied gate edits"),
            Repair->HasDraftChanges() && Repair->DraftGateOrder==4 && NumberingPreview->GetText().IsEmpty() &&
            Vertical->GetGateOrder()==3 && Pole->GetGateOrder()==5 && OriginalBestKept());
        Repair->RevertGate();
        OpenRepair->SimulateClick();
        const FString Plan=NumberingPreview->GetText().ToString();
        TestTrue(TEXT("Preview lists each change and leaves both gates and the selected record intact"),
            Plan.Contains(TEXT("3 \u2192 2")) && Plan.Contains(TEXT("5 \u2192 3")) &&
            Vertical->GetGateOrder()==3 && Pole->GetGateOrder()==5 && OriginalBestKept());
        if (!RenderEditor(Repair,{ConfirmRepair,CancelRepair,NumberingPreview},TEXT("RaceCloseGaps-1280x720.png"),true)) return false;
        CancelRepair->SimulateClick();
        TestTrue(TEXT("Cancel leaves the original numbering and stored best"),
            NumberingPreview->GetText().IsEmpty() && Vertical->GetGateOrder()==3 && Pole->GetGateOrder()==5 && OriginalBestKept());

        OpenRepair->SimulateClick();
        if (!TestTrue(TEXT("A concurrent course edit changes the exact proposed numbering"),Pole->Configure(Course,6,Error))) return false;
        ConfirmRepair->SimulateClick();
        TestTrue(TEXT("Stale confirmation does not renumber any gate"),
            NumberingPreview->GetText().IsEmpty() && Vertical->GetGateOrder()==3 && Pole->GetGateOrder()==6 && OriginalBestKept());
        if (!TestTrue(TEXT("Prepare a fresh numbering plan"),Pole->Configure(Course,5,Error))) return false;
        Repair->RefreshCourse();
        OpenRepair->SimulateClick();
        ConfirmRepair->SimulateClick();
        TestTrue(TEXT("Confirmed repair changes both gaps and refreshes the edit panel without a draft"),
            Vertical->GetGateOrder()==2 && Pole->GetGateOrder()==3 && Repair->DraftGateOrder==2 &&
            !Repair->HasDraftChanges() && Repair->bCourseReady && NumberingPreview->GetText().IsEmpty());
        TestTrue(TEXT("Repair preserves identity, pose, vertical direction and pole side"),
            Vertical->GetGateId()==VerticalId && Pole->GetGateId()==PoleId && Vertical->GetActorTransform().Equals(VerticalTransform) &&
            Pole->GetActorTransform().Equals(PoleTransform) && Vertical->GetRaceDirection()==EDirection::Descend && Pole->GetPolePassSide()==-1);
        TestTrue(TEXT("Restoring the exact layout restores its record and does not replace the selected race"),
            UFactoryFPVRaceComponent::LayoutSignature(World,Course)==CompleteLayout && OriginalBestKept() && Repair->ViewedBestSeconds==17.25);

        AFactoryFPVRaceGate::FEditorNumberingCompaction Proposal;
        TestFalse(TEXT("Contiguous course has no numbering repair"),AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(World,Course,Proposal,Error));
        if (!TestTrue(TEXT("Introduce duplicate live checkpoint orders"),Pole->Configure(Course,2,Error))) return false;
        TestFalse(TEXT("Duplicate numbers require an explicit manual edit"),AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(World,Course,Proposal,Error));
        if (!TestTrue(TEXT("Prepare uniquely ordered checkpoints without their start gate"),
            Pole->Configure(Course,3,Error) && Start->Configure(Course,4,Error))) return false;
        TestFalse(TEXT("A missing start gate cannot move the saved-record owner"),
            AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(World,Course,Proposal,Error));
        Hangar->SetSelectedRacingCourse(0);
        Pole->Destroy();Vertical->Destroy();Start->Destroy();
    }

    auto Embedded=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(First).Embedded(true);
    Embedded->DraftGateOrder=4;
    Embedded->RaceCourse();
    TestEqual(TEXT("Unapplied edits cannot accidentally select a race"),Hangar->GetSelectedRacingCourse(),0);
    Hangar->SetSelectedRacingCourse(1);
    if (!TestEqual(TEXT("Embedded edit regression has two real gate rows"),Embedded->GateRows->GetChildren()->Num(),2)) return false;
    auto EmbeddedFree=FindButton(StaticCastSharedRef<SWidget>(Embedded),TEXT("Free flight"));
    auto EmbeddedOther=FindButton(Embedded->GateRows->GetChildren()->GetChildAt(1),TEXT("Edit"));
    if (!TestNotNull(TEXT("Embedded free-flight action button"),EmbeddedFree.Get()) ||
        !TestNotNull(TEXT("Embedded other-gate Edit button"),EmbeddedOther.Get())) return false;
    EmbeddedFree->SimulateClick();
    TestTrue(TEXT("Embedded free flight preserves selection and pending gate draft"),Hangar->GetSelectedRacingCourse()==1 && Embedded->DraftGateOrder==4 && Embedded->EditingGate.Get()==First);
    EmbeddedOther->SimulateClick();
    TestTrue(TEXT("Embedded gate switching also retains unapplied edits"),Embedded->DraftGateOrder==4 && Embedded->EditingGate.Get()==First);
    Hangar->SetSelectedRacingCourse(0);
    Embedded->RevertGate();
    Embedded->RaceCourse();
    TestEqual(TEXT("Embedded race action selects the validated course"),Hangar->GetSelectedRacingCourse(),1);
    TestTrue(TEXT("Embedded race selection keeps its outer panel open"),Hangar->RacePanel.IsValid());
    TestTrue(TEXT("Embedded selection directs the player to Save & fly"),Embedded->Message.Contains(TEXT("Save & fly")));
    Embedded->FreeFlight();
    TestEqual(TEXT("Embedded free-flight selection clears racing"),Hangar->GetSelectedRacingCourse(),0);
    TestTrue(TEXT("Embedded free-flight selection also keeps outer UI open"),Hangar->IsPanelOpen());

    Editor->RaceCourse();
    TestTrue(TEXT("Standalone race selection returns to the hangar"),Hangar->Panel.IsValid() && !Hangar->RacePanel.IsValid());
    TestEqual(TEXT("Standalone race selection survives the panel transition"),Hangar->GetSelectedRacingCourse(),1);
    TestEqual(TEXT("Race-to-hangar transition keeps exactly one overlay"),ViewportOverlay->GetChildren()->Num(),BaselineOverlays+1);
    Hangar->OpenRaceGatePanel(First);
    auto Free=Hangar->RacePanel;
    Free->FreeFlight();
    TestTrue(TEXT("Standalone free flight returns to hangar"),Hangar->Panel.IsValid() && !Hangar->RacePanel.IsValid());
    TestEqual(TEXT("Standalone free flight clears selected course"),Hangar->GetSelectedRacingCourse(),0);
    Hangar->ClosePanel();
    TestFalse(TEXT("Panel type transitions leave movement unlocked after closing"),PC->IsMoveInputIgnored());
    TestFalse(TEXT("Panel type transitions leave look unlocked after closing"),PC->IsLookInputIgnored());

    Hangar->SetSelectedRacingCourse(1);
    // Pole side belongs to the real Apply/Revert draft. Exercise both widget
    // forms and actual buttons without fabricating this pole's power state.
    auto* Pole=World->SpawnActor<AFactoryFPVRacePole>(FVector(5000,0,0),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Separate pole actor for the configuration UI"),Pole)) return false;
    Pole->EnsureIdentityAndOrder();
    if (!TestTrue(TEXT("Pole starts with stored right-side configuration"),Pole->Configure(3,1,1,Error))) return false;
    for (const bool EmbeddedPole:{false,true}) {
        TSharedPtr<SFactoryFPVRacePanel> PoleEditor;
        if (EmbeddedPole) PoleEditor=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(Pole).Embedded(true);
        else { Hangar->OpenRaceGatePanel(Pole);PoleEditor=Hangar->RacePanel; }
        if (!TestTrue(TEXT("Pole uses the production configuration widget"),PoleEditor.IsValid())) return false;
        PoleEditor->SlatePrepass();
        TestTrue(TEXT("Pole heading identifies the selected checkpoint type"),PoleEditor->GateHeading().ToString().Contains(TEXT("Pole")));
        TestEqual(TEXT("Pole side draft loads the saved right value"),PoleEditor->DraftPolePassSide,1);
        auto LeftSide=FindButton(PoleEditor.ToSharedRef(),TEXT("Left"));
        auto RightSide=FindButton(PoleEditor.ToSharedRef(),TEXT("Right"));
        if (!TestNotNull(TEXT("Real left-side selection button"),LeftSide.Get()) || !TestNotNull(TEXT("Real right-side selection button"),RightSide.Get())) return false;
        if (!EmbeddedPole && !RenderEditor(PoleEditor.ToSharedRef(),{LeftSide,RightSide,PoleEditor->ApplyButton,PoleEditor->RaceButton},
            TEXT("RacePole-Editor-1280x720.png"),false)) return false;
        LeftSide->SimulateClick();
        TestTrue(TEXT("Selecting left changes only the pole draft"),PoleEditor->DraftPolePassSide==-1 && Pole->GetPolePassSide()==1 && PoleEditor->HasDraftChanges());
        TestTrue(TEXT("Pole instructions use the pending side and arrow direction"),PoleEditor->CheckpointInstructions().ToString().Contains(TEXT("LEFT")) && PoleEditor->CheckpointInstructions().ToString().Contains(TEXT("arrow")));
        PoleEditor->FreeFlight();PoleEditor->EditGate(Second);
        TestTrue(TEXT("A pole-only draft blocks navigation without losing its side"),PoleEditor->EditingGate.Get()==Pole && PoleEditor->DraftPolePassSide==-1 && PoleEditor->HasDraftChanges());
        RightSide->SimulateClick();
        TestFalse(TEXT("Returning to the saved side clears a no-op draft"),PoleEditor->HasDraftChanges());
        LeftSide->SimulateClick();PoleEditor->RevertGate();
        TestTrue(TEXT("Revert reloads the saved pole side"),PoleEditor->DraftPolePassSide==1 && !PoleEditor->HasDraftChanges());
        LeftSide->SimulateClick();PoleEditor->ApplyButton->SimulateClick();
        TestTrue(TEXT("Apply commits left through the atomic native Configure overload"),Pole->GetPolePassSide()==-1 && !PoleEditor->HasDraftChanges());
        TestTrue(TEXT("Stored pole instructions identify the committed left side"),Pole->GetCheckpointInstructions().Contains(TEXT("left"),ESearchCase::IgnoreCase));
        PoleEditor->EditGate(Second);
        const int32 GateSide=PoleEditor->DraftPolePassSide;
        LeftSide->SimulateClick();
        TestTrue(TEXT("A stale pole-side button cannot edit a ring gate"),PoleEditor->DraftPolePassSide==GateSide && !PoleEditor->HasDraftChanges() && !Second->IsPoleCheckpoint());
        Pole->Configure(3,1,1,Error);
        if (!EmbeddedPole) Hangar->ClosePanel();
    }
    Pole->Destroy();

    auto* Vertical=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(6000,0,500),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Separate lay-flat gate for the direction editor"),Vertical)) return false;
    Vertical->EnsureIdentityAndOrder();
    using ERaceDirection=EFactoryFPVRaceGateDirection;
    TestTrue(TEXT("New lay-flat gate defaults to Ascend"),Vertical->GetRaceDirection()==ERaceDirection::Ascend);
    if (!TestTrue(TEXT("Set the vertical checkpoint's initial course"),Vertical->Configure(4,1,1,ERaceDirection::Ascend,Error))) return false;
    for (const bool bEmbeddedVertical:{false,true}) {
        TSharedPtr<SFactoryFPVRacePanel> VerticalEditor;
        if (bEmbeddedVertical) VerticalEditor=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(Vertical).Embedded(true);
        else { Hangar->OpenRaceGatePanel(Vertical);VerticalEditor=Hangar->RacePanel; }
        if (!TestTrue(TEXT("Lay-flat gate uses the production editor"),VerticalEditor.IsValid())) return false;
        VerticalEditor->SlatePrepass();
        auto Ascend=FindButton(VerticalEditor.ToSharedRef(),TEXT("Ascend"));
        auto Descend=FindButton(VerticalEditor.ToSharedRef(),TEXT("Descend"));
        auto Revert=FindButton(VerticalEditor.ToSharedRef(),TEXT("Revert"));
        if (!TestNotNull(TEXT("Actual Ascend button"),Ascend.Get()) || !TestNotNull(TEXT("Actual Descend button"),Descend.Get()) ||
            !TestNotNull(TEXT("Actual Revert button"),Revert.Get())) return false;
        TestTrue(TEXT("Saved ascent tells the pilot to enter from below"),VerticalEditor->CheckpointInstructions().ToString().Contains(TEXT("BELOW")));
        Descend->SimulateClick();
        TestTrue(TEXT("Descend changes the draft while the saved gate stays Ascend"),VerticalEditor->DraftDirection==ERaceDirection::Descend &&
            Vertical->GetRaceDirection()==ERaceDirection::Ascend && VerticalEditor->HasDraftChanges());
        TestTrue(TEXT("Draft descent tells the pilot to enter from above"),VerticalEditor->CheckpointInstructions().ToString().Contains(TEXT("ABOVE")));
        VerticalEditor->FreeFlight();
        VerticalEditor->EditGate(Second);
        TestTrue(TEXT("A direction-only draft blocks flight and edit switching without changing the selected course"),
            VerticalEditor->EditingGate.Get()==Vertical && VerticalEditor->HasDraftChanges() && Hangar->GetSelectedRacingCourse()==1);
        Ascend->SimulateClick();
        TestFalse(TEXT("Returning to the saved direction clears the draft"),VerticalEditor->HasDraftChanges());
        Descend->SimulateClick();
        VerticalEditor->SlatePrepass();
        Revert->Invalidate(EInvalidateWidgetReason::Layout);
        Revert->SlatePrepass();
        Revert->SimulateClick();
        TestTrue(TEXT("Revert restores the saved Ascend direction"),VerticalEditor->DraftDirection==ERaceDirection::Ascend && !VerticalEditor->HasDraftChanges());
        Descend->SimulateClick();
        VerticalEditor->ApplyButton->SimulateClick();
        TestTrue(TEXT("Apply commits the native Descend direction and clears the draft"),Vertical->GetRaceDirection()==ERaceDirection::Descend && !VerticalEditor->HasDraftChanges());
        if (!RenderEditor(VerticalEditor.ToSharedRef(),{Ascend,Descend,VerticalEditor->ApplyButton,VerticalEditor->RaceButton},
            bEmbeddedVertical ? TEXT("RaceVertical-Hangar-1280x720.png") : TEXT("RaceVertical-Editor-1280x720.png"),bEmbeddedVertical)) return false;
        VerticalEditor->EditGate(Second);
        Descend->SimulateClick();
        TestTrue(TEXT("A stale direction button cannot change an upright gate"),VerticalEditor->DraftDirection==ERaceDirection::Forward &&
            Second->GetRaceDirection()==ERaceDirection::Forward && !VerticalEditor->HasDraftChanges());
        Vertical->Configure(4,1,1,ERaceDirection::Ascend,Error);
        if (!bEmbeddedVertical) Hangar->ClosePanel();
    }
    auto* Upper=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(6000,0,2500),FRotator::ZeroRotator,Spawn);
    auto* Return=World->SpawnActor<AFactoryFPVRaceGateVertical>(FVector(9000,0,500),FRotator::ZeroRotator,Spawn);
    if (!TestNotNull(TEXT("Vertically stacked route checkpoint"),Upper) ||
        !TestNotNull(TEXT("Horizontal return route checkpoint"),Return)) return false;
    for (auto* Gate:{Upper,Return}) Gate->EnsureIdentityAndOrder();
    if (!TestTrue(TEXT("Configure ascent, descent and return with unique orders"),
        Upper->Configure(4,2,1,ERaceDirection::Descend,Error) && Return->Configure(4,3,1,ERaceDirection::Ascend,Error))) return false;
    for (auto* Gate:{Vertical,Return}) {
        Circuit->SetPropertyValue_InContainer(Gate->GetPowerInfo(),17);
        Powered->SetPropertyValue_InContainer(Gate->GetPowerInfo(),true);
    }
    auto RoutePanel=SNew(SFactoryFPVRacePanel).Owner(Hangar).Gate(Vertical).Embedded(true);
    RoutePanel->SlatePrepass();
    auto RouteButton=FindButton(RoutePanel,TEXT("Route preview"));
    auto CheckpointsButton=FindButton(RoutePanel,TEXT("Checkpoints"));
    if (!TestNotNull(TEXT("Actual route preview button"),RouteButton.Get()) ||
        !TestNotNull(TEXT("Actual checkpoint list button"),CheckpointsButton.Get())) return false;
    RoutePanel->SelectRaceDirection(ERaceDirection::Descend);
    RouteButton->SimulateClick();
    RoutePanel->SlatePrepass();
    TestTrue(TEXT("An unpowered but complete route has a true 3D loop and height span"),RoutePanel->IsShowingRoute() &&
        RoutePanel->RouteSummary==TEXT("86.1 m loop  /  20.0 m elevation span") && !RoutePanel->bCourseReady &&
        RoutePanel->CourseStatus.Contains(TEXT("Checkpoint 2 has no power")));
    TestTrue(TEXT("Viewing route leaves the checkpoint draft and chosen race alone"),RoutePanel->HasDraftChanges() &&
        Vertical->GetRaceDirection()==ERaceDirection::Ascend && Hangar->GetSelectedRacingCourse()==1);
    auto Plan=FindButton(RoutePanel,TEXT("Plan view"));
    auto Elevation=FindButton(RoutePanel,TEXT("Elevation"));
    if (!TestNotNull(TEXT("Actual plan view button"),Plan.Get()) ||
        !TestNotNull(TEXT("Actual elevation view button"),Elevation.Get())) return false;
    TestFalse(TEXT("The route preview starts in plan view"),RoutePanel->RoutePreview->IsElevation());
    if (!RenderEditor(RoutePanel,{RouteButton,Plan,Elevation,RoutePanel->RoutePreview,RoutePanel->RaceButton},TEXT("RaceRoute-Plan-1280x720.png"),true)) return false;
    Elevation->SimulateClick();
    TestTrue(TEXT("Elevation switches projection without a draft write"),RoutePanel->RoutePreview->IsElevation() && RoutePanel->HasDraftChanges());
    const auto& ChartCheckpoints=RoutePanel->RoutePreview->Checkpoints;
    TestTrue(TEXT("Elevation markers use the saved up/down passage directions, including the selected gate"),
        ChartCheckpoints.Num()==3 && ChartCheckpoints[0].Direction==ERaceDirection::Ascend &&
        ChartCheckpoints[1].Direction==ERaceDirection::Descend && ChartCheckpoints[2].Direction==ERaceDirection::Ascend);
    if (!RenderEditor(RoutePanel,{RouteButton,Plan,Elevation,RoutePanel->RoutePreview,RoutePanel->RaceButton},TEXT("RaceRoute-Elevation-1280x720.png"),true)) return false;
    const auto UpperPoint=Upper->GetPassageAimPoint();
    TestTrue(TEXT("Reversing a stacked checkpoint keeps its chart location"),Upper->Configure(4,2,1,ERaceDirection::Ascend,Error) &&
        Upper->GetPassageAimPoint().Equals(UpperPoint));
    RoutePanel->RefreshCourse();
    TestTrue(TEXT("Reversing a checkpoint refreshes the plotted direction with the same height and order"),
        ChartCheckpoints.Num()==3 && ChartCheckpoints[1].Direction==ERaceDirection::Ascend && ChartCheckpoints[1].Position.Equals(UpperPoint));
    if (!TestTrue(TEXT("Restore the original Descend checkpoint"),Upper->Configure(4,2,1,ERaceDirection::Descend,Error))) return false;
    CheckpointsButton->SimulateClick();
    TestFalse(TEXT("Checkpoints returns to the editable list"),RoutePanel->IsShowingRoute());
    RouteButton->SimulateClick();
    TestTrue(TEXT("Reopening the chart keeps the chosen projection"),RoutePanel->IsShowingRoute() && RoutePanel->RoutePreview->IsElevation());

    RoutePanel->RevertGate();
    RoutePanel->EditGate(Upper);
    Circuit->SetPropertyValue_InContainer(Upper->GetPowerInfo(),17);
    Powered->SetPropertyValue_InContainer(Upper->GetPowerInfo(),true);
    RoutePanel->RefreshCourse();
    auto CourseClearance=FindText(RoutePanel,FName(TEXT("FactoryFPVRaceCourseClearance")));
    auto GateClearance=FindText(RoutePanel,FName(TEXT("FactoryFPVRaceCheckpointClearance")));
    if (!TestNotNull(TEXT("Course clearance advisory widget"),CourseClearance.Get()) ||
        !TestNotNull(TEXT("Checkpoint clearance advisory widget"),GateClearance.Get())) return false;
    TestTrue(TEXT("The clear powered course has no center-line advisory"),RoutePanel->bCourseReady &&
        CourseClearance->GetText().IsEmpty() && GateClearance->GetText().IsEmpty());
    auto* Blocker=World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("A real roof above the vertical aperture"),Blocker)) return false;
    auto* Roof=NewObject<UBoxComponent>(Blocker);
    Blocker->SetRootComponent(Roof);Blocker->AddInstanceComponent(Roof);
    Roof->SetBoxExtent(FVector(90.,90.,15.));
    Roof->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Roof->SetCollisionObjectType(ECC_WorldStatic);
    Roof->SetCollisionResponseToAllChannels(ECR_Block);
    Roof->RegisterComponent();
    Blocker->SetActorLocation(UpperPoint+FVector(0.,0.,120.));
    RoutePanel->RefreshCourse();
    RoutePanel->RaceButton->Invalidate(EInvalidateWidgetReason::Layout);
    RoutePanel->SlatePrepass();
    if (!RenderEditor(RoutePanel,{CourseClearance,GateClearance,RoutePanel->RaceButton},TEXT("RaceVertical-Blocked-1280x720.png"),true)) return false;
    TestTrue(TEXT("A real blocker leaves racing available"),RoutePanel->bCourseReady && RoutePanel->RaceButton->IsEnabled());
    TestTrue(TEXT("Both rendered advisories name the blocked approach"),
        CourseClearance->GetText().ToString().Contains(TEXT("Checkpoint 2 center line is blocked above the opening")) &&
        GateClearance->GetText().ToString().Contains(TEXT("Center line blocked above the opening")));
    Blocker->Destroy();
    RoutePanel->RefreshCourse();
    TestTrue(TEXT("Removing the roof clears both advisories immediately on refresh"),RoutePanel->bCourseReady &&
        CourseClearance->GetText().IsEmpty() && GateClearance->GetText().IsEmpty());
    Upper->Destroy();
    RoutePanel->RefreshCourse();
    TestTrue(TEXT("A missing stacked checkpoint removes the misleading route plot"),RoutePanel->RouteSummary.IsEmpty() &&
        RoutePanel->CourseStatus.Contains(TEXT("Checkpoint 2 is missing")));
    Return->Destroy();
    Vertical->Destroy();

    auto* OtherWorld=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Values);
    if (!TestNotNull(TEXT("Travel destination fixture"),OtherWorld)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(OtherWorld);
    ON_SCOPE_EXIT { OtherWorld->DestroyWorld(false); GEngine->DestroyWorldContext(OtherWorld); };
    ViewportWorld->SetObjectPropertyValue_InContainer(Viewport,OtherWorld);
    TestEqual(TEXT("Race selection does not bleed into another world's course IDs"),Hangar->GetSelectedRacingCourse(),0);
    ViewportWorld->SetObjectPropertyValue_InContainer(Viewport,World);
    Hangar->SetSelectedRacingCourse(0);
    // Native mIsDismantled is a non-reflected flag with no public setter;
    // Dismantle_Implementation requires real game subsystems. Keep that shipping
    // path as an explicit acceptance check rather than inventing a private-offset
    // or type-punning fixture. Actual actor destruction is exercised below.
    Hangar->OpenRaceGatePanel(First);
    auto Removed=Hangar->RacePanel;
    TestTrue(TEXT("Deleted gate fixture actually routes actor destruction"),First->Destroy());
    TestTrue(TEXT("Deleted gate fixture is no longer a usable actor"),First->IsActorBeingDestroyed());
    Removed->Tick(FGeometry(),1,0);
    TestFalse(TEXT("Deleted gate is no longer an editable selection"),Removed->CanEditGate());
    Removed->Invalidate(EInvalidateWidgetReason::Layout);
    Removed->SlatePrepass();
    // The editor row collapses when the gate goes away. Slate skips prepass on
    // collapsed descendants, so explicitly evaluate this detached cached leaf.
    Removed->ApplyButton->Invalidate(EInvalidateWidgetReason::Layout);
    Removed->ApplyButton->SlatePrepass();
    TestFalse(TEXT("Dismantled/deleted gate cannot be edited through a stale panel"),Removed->ApplyButton->IsEnabled());
    Removed->ApplyGate();
    TestTrue(TEXT("Stale gate action gives useful feedback"),Removed->Message.Contains(TEXT("Select a loaded gate")));
    Hangar->UnbindController();
    TestFalse(TEXT("Unbind closes racing UI along with normal hangar UI"),Hangar->IsPanelOpen());
    TestFalse(TEXT("Unbind releases race move/look holds"),PC->IsMoveInputIgnored() || PC->IsLookInputIgnored());
    TestEqual(TEXT("Unbind removes both race overlay and shortcut hint"),ViewportOverlay->GetChildren()->Num(),0);
    AddInfo(TEXT("Exercises real Slate handlers and input hold ownership; headless fixture does not claim mouse-focus, live circuit, or shipping interaction verification."));
    return true;
}
#endif
