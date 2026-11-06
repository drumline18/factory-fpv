#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVInteractionState.h"
#include "FactoryFPVSettings.h"
#include "FactoryFPVPanel.h"
#include "FactoryFPVPanelViewportLayout.h"
#include "FactoryFPVRacePanel.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRacingContent.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVReturnStatusWidget.h"
#include "Styling/CoreStyle.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/Console.h"
#include "FGCharacterBase.h"
#include "FGPlayerController.h"
#include "UI/FGGameUI.h"
#include "UI/FGInteractWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

class SFactoryFPVDiscardEditsDialog final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVDiscardEditsDialog) {}
        SLATE_ARGUMENT(FText, Description)
        SLATE_EVENT(FOnClicked, OnKeepEditing)
        SLATE_EVENT(FOnClicked, OnDiscard)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args) {
        KeepEditing=Args._OnKeepEditing;
        const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        ChildSlot
        [ SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(0.f,0.f,0.f,.78f))
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            .OnMouseButtonDown_Lambda([](const FGeometry&,const FPointerEvent&) { return FReply::Handled(); })
            [ SNew(SBox).WidthOverride(445.f)
                [ SNew(SBorder).BorderImage(Brush).BorderBackgroundColor(FLinearColor(.042f,.055f,.069f)).Padding(22.f)
                    [ SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("Discard unsaved edits?")))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),18))
                            .ColorAndOpacity(FLinearColor(1.f,.45f,.12f)) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,10,0,18)
                        [ SNew(STextBlock).Text(Args._Description).AutoWrapText(true)
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)) ]
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
                        [ SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth()
                            [ SNew(SButton).Text(FText::FromString(TEXT("Keep editing"))).OnClicked(KeepEditing) ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                            [ SNew(SButton).Text(FText::FromString(TEXT("Discard edits")))
                                .ButtonColorAndOpacity(FLinearColor(1.f,.45f,.12f)).OnClicked(Args._OnDiscard) ] ] ] ] ] ];
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override {
        if (Event.GetKey()==EKeys::Escape || Event.GetKey()==GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar) {
            if (!Event.IsRepeat() && KeepEditing.IsBound()) {
                const auto KeepAlive=AsShared();
                return KeepEditing.Execute();
            }
            return FReply::Handled();
        }
        return SCompoundWidget::OnPreviewKeyDown(Geometry,Event);
    }

private:
    FOnClicked KeepEditing;
};

TStatId UFactoryFPVLocalPlayerSubsystem::GetStatId() const {
    RETURN_QUICK_DECLARE_CYCLE_STAT(UFactoryFPVLocalPlayerSubsystem, STATGROUP_Tickables);
}

bool UFactoryFPVLocalPlayerSubsystem::BindController(APlayerController* Candidate, UGameViewportClient* CandidateViewport) {
    // The player can appear before its viewport during loading/travel. Do not
    // cache a half-bound controller: Tick must be able to retry the same player.
    if (Controller.IsValid() || !Candidate || !CandidateViewport) return false;
    Controller=Candidate;
    Viewport=CandidateViewport;
    Controls = NewObject<UInputComponent>(Candidate, TEXT("FactoryFPVHangarControls"));
    Controls->RegisterComponent();
    Controls->Priority = 1000;
    Controls->bBlockInput = false;
    RefreshHangarShortcut();
    Candidate->PushInputComponent(Controls);
    HintUntil = FPlatformTime::Seconds() + 10.;
    Hint = SNew(SOverlay).Visibility(EVisibility::HitTestInvisible)
        + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(24.f, 0.f, 0.f, 100.f)
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(0.025f, 0.035f, 0.045f, 0.94f)).Padding(12.f)
            [
                SNew(STextBlock).Text_Lambda([] {
                    const FKey& Key=GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar;
                    return FText::FromString(FFactoryFPVKeyboardShortcuts::KeyLabel(Key)+TEXT("   FACTORY FPV   /   Hangar & radio setup"));
                })
                .ColorAndOpacity(FLinearColor(1.f, 0.45f, 0.12f))
            ]
        ];
    Viewport->AddViewportWidgetContent(Hint.ToSharedRef(), 20);
    return true;
}

void UFactoryFPVLocalPlayerSubsystem::RefreshHangarShortcut() {
    if (!Controls) return;
    const FKey& Key=GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar;
    if (Controls->KeyBindings.IsEmpty()) Controls->BindKey(Key,IE_Pressed,this,&UFactoryFPVLocalPlayerSubsystem::TogglePanel);
    else Controls->KeyBindings[0].Chord.Key=Key;
}

void UFactoryFPVLocalPlayerSubsystem::Tick(float) {
    UWorld* World = GetWorld();
    APlayerController* LocalController = World ? GetLocalPlayer()->GetPlayerController(World) : nullptr;
    auto* Pioneer = LocalController ? Cast<AFGCharacterBase>(LocalController->GetPawn()) : nullptr;
    const bool Gameplay = World && World->GetNetMode() == NM_Standalone && Pioneer && Pioneer->IsAliveAndWell();
    if (!Gameplay || LocalController != Controller.Get()) {
        UnbindController();
        if (!Gameplay) return;
    }
    if (!Controller.IsValid()) {
        if (!BindController(LocalController,World->GetGameViewport())) return;
    }
    RenewPanelInputLease();
    // Retry after native save loading and future milestone purchases. This
    // public-manager migration only adds the newly introduced missing recipe;
    // it never purchases a milestone or alters an unrelated world/profile.
    const double RecipeNow=FPlatformTime::Seconds();
    if (RecipeNow>=NextRacingRecipeCheck) {
        NextRacingRecipeCheck=RecipeNow+1.;
        FactoryFPVRacingContent::EnsureUnlockedRacingRecipes(World);
    }
    UpdateReturnStatus();
    if (Hint && (IsPanelOpen() || AFactoryFPVDrone::FindActive(World) || FPlatformTime::Seconds() > HintUntil)) {
        Viewport->RemoveViewportWidgetContent(Hint.ToSharedRef());
        Hint.Reset();
    }
}

void UFactoryFPVLocalPlayerSubsystem::TogglePanel() {
    if (IsPanelOpen()) RequestClosePanel();
    else OpenPanel(); // Configuration stays accessible; session entry enforces equipment access.
}

bool UFactoryFPVLocalPlayerSubsystem::CanOpenPanel() const {
    return Viewport.IsValid() && !FactoryFPVInteractionState::BlocksPlayerInteraction(Controller.Get());
}

bool UFactoryFPVLocalPlayerSubsystem::CanPreviewHangarAudio(const SFactoryFPVPanel* Requester) const {
    return Panel.Get()==Requester && !DiscardDialog && Viewport.IsValid() && !NativeUIOwnsInput();
}

bool UFactoryFPVLocalPlayerSubsystem::CanOpenRaceGate(AFactoryFPVRaceGate* Gate) const {
    return CanOpenPanel() && IsValid(Gate) && Gate->GetWorld()==Controller->GetWorld() && Gate->IsUseable_Implementation();
}

void UFactoryFPVLocalPlayerSubsystem::OpenPanel(const FString& Message, FName RetryInputMode) {
    if (Panel.IsValid() || !CanOpenPanel()) return;
    if (RacePanel && !CanLeavePanel({EPanelDeparture::OpenHangar,{},Message,RetryInputMode})) return;
    if (RacePanel) ClosePanel();
    if (auto* Drone = AFactoryFPVDrone::FindActive(GetWorld())) Drone->ExitDrone();

    auto* PC=Controller.Get();
    bPreviousCursor = PC->bShowMouseCursor;
    Panel = SNew(SFactoryFPVPanel).Controller(PC).Owner(this).InitialMessage(Message).RetryInputMode(RetryInputMode);
    ShowPanel(Panel.ToSharedRef());
}

void UFactoryFPVLocalPlayerSubsystem::OpenRaceGatePanel(AFactoryFPVRaceGate* Gate) {
    if (!CanOpenRaceGate(Gate) || !CanLeavePanel({EPanelDeparture::OpenGate,Gate})) return;
    if (IsPanelOpen()) ClosePanel();
    if (auto* Drone=AFactoryFPVDrone::FindActive(GetWorld())) Drone->ExitDrone();
    auto* PC=Controller.Get();
    bPreviousCursor=PC->bShowMouseCursor;
    RacePanel=SNew(SFactoryFPVRacePanel).Owner(this).Gate(Gate);
    ShowPanel(RacePanel.ToSharedRef());
}

bool UFactoryFPVLocalPlayerSubsystem::CanLeavePanel(FPanelDeparture Departure) {
    if (DiscardDialog) return false;
    if (Panel) Panel->StopAudioPreview();
    if ((!Panel || !Panel->HasPendingEdits()) && (!RacePanel || !RacePanel->HasPendingGateEdits())) return true;
    if (!PanelLayers || NativeUIOwnsInput()) return false;
    const TCHAR* Description=TEXT("Discard the edits and close this panel?");
    if (Departure.Destination==EPanelDeparture::OpenHangar) Description=TEXT("Discard the checkpoint edits and open the hangar?");
    if (Departure.Destination==EPanelDeparture::OpenGate) Description=TEXT("Discard the edits and open the requested checkpoint editor?");
    PendingDeparture=MoveTemp(Departure);
    const TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> WeakThis(this);
    DiscardDialog=SNew(SFactoryFPVDiscardEditsDialog).Description(FText::FromString(Description))
        .OnKeepEditing_Lambda([WeakThis] {
            if (auto* Subsystem=WeakThis.Get()) Subsystem->ResolvePanelDeparture(false);
            return FReply::Handled();
        })
        .OnDiscard_Lambda([WeakThis] {
            if (auto* Subsystem=WeakThis.Get()) Subsystem->ResolvePanelDeparture(true);
            return FReply::Handled();
        });
    PanelLayers->AddSlot()[DiscardDialog.ToSharedRef()];
    FocusPanelWidget(DiscardDialog.ToSharedRef());
    return false;
}

void UFactoryFPVLocalPlayerSubsystem::RequestClosePanel() {
    if (DiscardDialog) ResolvePanelDeparture(false);
    else if (IsPanelOpen() && CanLeavePanel({})) ClosePanel();
}

void UFactoryFPVLocalPlayerSubsystem::ResolvePanelDeparture(bool bDiscard) {
    if (!PendingDeparture || !DiscardDialog || !PanelLayers) return;
    const auto KeepAlive=DiscardDialog;
    FPanelDeparture Departure=MoveTemp(PendingDeparture.GetValue());
    PendingDeparture.Reset();
    PanelLayers->RemoveSlot(KeepAlive.ToSharedRef());
    DiscardDialog.Reset();
    auto* Gate=Departure.Gate.Get();
    const bool bNativeOwnsInput=NativeUIOwnsInput();
    const bool bCanDiscard=bDiscard && !bNativeOwnsInput && (Departure.Destination==EPanelDeparture::Close ||
        (Departure.Destination==EPanelDeparture::OpenHangar && CanOpenPanel()) ||
        (Departure.Destination==EPanelDeparture::OpenGate && CanOpenRaceGate(Gate)));
    if (!bCanDiscard) {
        if (!bNativeOwnsInput) {
            if (Panel) FocusPanelWidget(Panel.ToSharedRef());
            else if (RacePanel) FocusPanelWidget(RacePanel.ToSharedRef());
        }
        return;
    }
    ClosePanel();
    if (Departure.Destination==EPanelDeparture::OpenHangar) OpenPanel(Departure.Message,Departure.RetryInputMode);
    else if (Departure.Destination==EPanelDeparture::OpenGate) OpenRaceGatePanel(Gate);
}

void UFactoryFPVLocalPlayerSubsystem::ShowPanel(TSharedRef<SWidget> Widget) {
    auto* PC=Controller.Get();
    if (!PC || !Viewport.IsValid() || !InputLease.Acquire(PC)) {
        InputLease.Release();
        Panel.Reset();
        RacePanel.Reset();
        Overlay.Reset();
        return;
    }
    PanelLayers=SNew(SOverlay)
        + SOverlay::Slot()[SNew(SBox).IsEnabled_Lambda([this] { return !DiscardDialog; })[Widget]];
    Overlay=FactoryFPVPanelViewportLayout::Make(PanelLayers.ToSharedRef(),
        FPointerEventHandler::CreateLambda([this](const FGeometry&,const FPointerEvent&) {
            if (DiscardDialog) return FReply::Handled().SetUserFocus(DiscardDialog.ToSharedRef(),EFocusCause::Mouse);
            if (Panel) return FReply::Handled().SetUserFocus(Panel.ToSharedRef(),EFocusCause::Mouse);
            if (RacePanel) return FReply::Handled().SetUserFocus(RacePanel.ToSharedRef(),EFocusCause::Mouse);
            return FReply::Unhandled();
        }));
    Viewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 1000);
    PC->bShowMouseCursor = true;
    FocusPanelWidget(Widget);
}

void UFactoryFPVLocalPlayerSubsystem::FocusPanelWidget(TSharedRef<SWidget> Widget) const {
    auto* PC=Controller.Get();
    if (!PC) return;
    // Radio preview polls DirectInput independently. Gameplay must not receive
    // inventory/build/interact keys while the hangar owns the mouse and focus.
    FInputModeUIOnly InputMode;
    InputMode.SetWidgetToFocus(Widget);
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    PC->SetInputMode(InputMode);
}

bool UFactoryFPVLocalPlayerSubsystem::NativeUIOwnsInput() const {
    const auto* Console=Viewport.IsValid() ? Viewport->ViewportConsole.Get() : nullptr;
    return FactoryFPVInteractionState::BlocksPlayerInteraction(Controller.Get()) || (Console && Console->ConsoleState!=NAME_None);
}

void UFactoryFPVLocalPlayerSubsystem::RenewPanelInputLease() {
    if (!IsPanelOpen()) return;
    if (NativeUIOwnsInput()) {
        if (Panel) Panel->StopAudioPreview();
        return;
    }
    // Native UI resets surrender the old contributions synchronously. Once it
    // releases interaction, renew our own holds even if another owner also
    // holds input; a boolean ignored state cannot prove our lease survived.
    InputLease.Renew();
}

EFactoryFPVReturnStatus UFactoryFPVLocalPlayerSubsystem::PendingReturnStatus() const {
    auto* PC=Controller.Get();
    if (!PC || PC->GetWorld()!=GetWorld()) return EFactoryFPVReturnStatus::None;
    auto* Anchor=AFactoryFPVPioneerAnchor::FindPending(PC->GetWorld(),Cast<ACharacter>(PC->GetPawn()));
    return Anchor ? Anchor->GetReturnStatus() : EFactoryFPVReturnStatus::None;
}

EVisibility UFactoryFPVLocalPlayerSubsystem::ReturnStatusVisibility() const {
    if (PendingReturnStatus()==EFactoryFPVReturnStatus::None || IsPanelOpen() || NativeUIOwnsInput() ||
        AFactoryFPVDrone::FindActive(GetWorld())) return EVisibility::Collapsed;
    return EVisibility::HitTestInvisible;
}

void UFactoryFPVLocalPlayerSubsystem::UpdateReturnStatus() {
    if (!Viewport.IsValid() || PendingReturnStatus()==EFactoryFPVReturnStatus::None) { ClearReturnStatus(); return; }
    if (!ReturnStatusOverlay) {
        const TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> WeakThis(this);
        ReturnStatusOverlay=FactoryFPVReturnStatusWidget::Make(
            TAttribute<EFactoryFPVReturnStatus>::CreateLambda([WeakThis] { return WeakThis.IsValid() ? WeakThis->PendingReturnStatus() : EFactoryFPVReturnStatus::None; }),
            TAttribute<EVisibility>::CreateLambda([WeakThis] { return WeakThis.IsValid() ? WeakThis->ReturnStatusVisibility() : EVisibility::Collapsed; }));
        Viewport->AddViewportWidgetContent(ReturnStatusOverlay.ToSharedRef(),200);
    }
}

void UFactoryFPVLocalPlayerSubsystem::ClearReturnStatus() {
    if (Viewport.IsValid() && ReturnStatusOverlay) Viewport->RemoveViewportWidgetContent(ReturnStatusOverlay.ToSharedRef());
    ReturnStatusOverlay.Reset();
}

void UFactoryFPVLocalPlayerSubsystem::ClosePanel() {
    if (!IsPanelOpen()) { InputLease.Release(); return; }
    if (Panel) Panel->StopAudioPreview();
    if (Viewport.IsValid() && Overlay) Viewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
    PendingDeparture.Reset();
    DiscardDialog.Reset();
    PanelLayers.Reset();
    Panel.Reset();
    RacePanel.Reset();
    Overlay.Reset();
    InputLease.Release();
    if (auto* PC = Controller.Get()) {
        auto* GamePC=Cast<AFGPlayerController>(PC);
        auto* GameUI=GamePC ? GamePC->GetGameUI() : nullptr;
        UFGInteractWidget* ActiveWidget=nullptr;
        if (GameUI) {
            const auto Stack=GameUI->GetInteractWidgetStack();
            for (int32 Index=Stack.Num()-1;Index>=0;--Index) {
                if (IsValid(Stack[Index])) { ActiveWidget=Stack[Index]; break; }
            }
        }
        if (ActiveWidget) {
            // A death/menu transition may have opened a newer native widget.
            // Let it own focus and cursor instead of forcing gameplay input.
            ActiveWidget->SetInputMode();
        } else if (!FactoryFPVInteractionState::IsNativePauseMenuOpen(GameUI)) {
            PC->bShowMouseCursor = bPreviousCursor;
            PC->SetInputMode(FInputModeGameOnly());
        }
        // A native pause menu can own focus without appearing in the interact
        // stack. Preserve its input mode and cursor after removing our panel.
    }
}

int32 UFactoryFPVLocalPlayerSubsystem::GetSelectedRacingCourse() const {
    auto* World=GetWorld();
    return World && SelectedRacingWorld.Get()==World ? SelectedRacingCourse : 0;
}

void UFactoryFPVLocalPlayerSubsystem::SetSelectedRacingCourse(int32 CourseId) {
    auto* World=GetWorld();
    if (!World || CourseId<1 || CourseId>AFactoryFPVRaceGate::MaximumCourseId) {
        SelectedRacingCourse=0;
        SelectedRacingWorld.Reset();
        SelectedRacingLayout.Reset();
        return;
    }
    SelectedRacingWorld=World;
    SelectedRacingCourse=CourseId;
    SelectedRacingLayout=UFactoryFPVRaceComponent::LayoutSignature(World,CourseId);
}

const FFactoryFPVRaceDebrief* UFactoryFPVLocalPlayerSubsystem::GetLastRaceDebrief() const {
    const UWorld* World=GetWorld();
    return World && RaceDebriefWorld.Get()==World && LastRaceDebrief ? &LastRaceDebrief.GetValue() : nullptr;
}

void UFactoryFPVLocalPlayerSubsystem::StoreRaceDebrief(UWorld* RaceWorld,const FFactoryFPVRaceDebrief& Debrief) {
    if (!RaceWorld || RaceWorld!=GetWorld() || Debrief.CourseId<=0 || Debrief.LapCount==0) return;
    RaceDebriefWorld=RaceWorld;
    LastRaceDebrief=Debrief;
    if (const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(RaceWorld)) {
        LastRaceDebrief->CourseName=Registry->GetNameForCourse(Debrief.CourseId);
    }
}

int32 UFactoryFPVLocalPlayerSubsystem::GetRaceBuildCourse() const {
    const UWorld* World=GetWorld();
    return World && RaceBuildWorld.Get()==World ? RaceBuildCourse : 0;
}

int32 UFactoryFPVLocalPlayerSubsystem::GetNextRaceBuildOrder() const {
    return AFactoryFPVRaceGate::NextCourseBuildOrder(GetWorld(),GetRaceBuildCourse());
}

bool UFactoryFPVLocalPlayerSubsystem::SetRaceBuildCourse(int32 CourseId) {
    if (CourseId==0) {
        RaceBuildWorld.Reset();
        RaceBuildCourse=0;
        return true;
    }
    auto* World=GetWorld();
    if (!World || !World->IsGameWorld() || World->GetNetMode()!=NM_Standalone || CourseId<1 ||
        CourseId>AFactoryFPVRaceGate::MaximumCourseId) return false;
    RaceBuildWorld=World;
    RaceBuildCourse=CourseId;
    return true;
}

int32 UFactoryFPVLocalPlayerSubsystem::StartNewRaceBuildCourse() {
    auto* World=GetWorld();
    if (!World || !World->IsGameWorld() || World->GetNetMode()!=NM_Standalone) return 0;
    TSet<int32> Used;
    if (const int32 Current=GetRaceBuildCourse();Current>0) Used.Add(Current);
    if (const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World)) {
        for (const auto& Entry:Registry->GetNames()) Used.Add(Entry.Key);
    }
    for (TActorIterator<AFactoryFPVRaceGate> Gate(World);Gate;++Gate) {
        if (Gate->IsLiveRaceGate()) Used.Add(Gate->GetCourseId());
    }
    for (int32 CourseId=1;CourseId<=AFactoryFPVRaceGate::MaximumCourseId;++CourseId) {
        if (!Used.Contains(CourseId) && SetRaceBuildCourse(CourseId)) return CourseId;
    }
    return 0;
}

void UFactoryFPVLocalPlayerSubsystem::UnbindController() {
    ClosePanel();
    ClearReturnStatus();
    if (auto* PC = Controller.Get()) {
        if (Controls) PC->PopInputComponent(Controls);
    }
    if (Viewport.IsValid() && Hint) Viewport->RemoveViewportWidgetContent(Hint.ToSharedRef());
    Hint.Reset();
    if (Controls) Controls->DestroyComponent();
    Controls = nullptr;
    Controller.Reset();
    Viewport.Reset();
}

void UFactoryFPVLocalPlayerSubsystem::Deinitialize() {
    UnbindController();
    SetRaceBuildCourse(0);
    RaceDebriefWorld.Reset();
    LastRaceDebrief.Reset();
    SelectedRacingWorld.Reset();
    SelectedRacingCourse=0;
    SelectedRacingLayout.Reset();
    Super::Deinitialize();
}
