#include "FactoryFPVRacePanel.h"
#include "FactoryFPVRaceGate.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVRaceDebrief.h"
#include "FactoryFPVRaceGhost.h"
#include "FactoryFPVRaceCourseRegistry.h"
#include "FactoryFPVRaceRoutePreview.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace {
const FLinearColor RaceBackground(.025f,.032f,.042f);
const FLinearColor RaceCard(.055f,.067f,.082f);
const FLinearColor RaceOrange(1,.42f,.08f);
const FLinearColor RaceCyan(.15f,.82f,.85f);
const FLinearColor RaceMuted(.57f,.63f,.7f);
const FLinearColor RaceRow(.038f,.047f,.06f);
const FLinearColor RaceSelectedRow(.10f,.09f,.065f);
TSharedRef<STextBlock> RaceLabel(const FString& Text,int32 Size=11,FLinearColor Color=FLinearColor::White,bool bWrap=false) {
    return SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(bWrap)
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),Size)).ColorAndOpacity(Color);
}
FString DebriefTime(double Seconds) {
    return FMath::IsFinite(Seconds) && Seconds>0. ? FString::Printf(TEXT("%.3f s"),Seconds) : TEXT("--");
}

TSharedRef<SWidget> DebriefRow(const FString& Label,const FString& Time,const FString& Pace,
    FLinearColor LabelTint=FLinearColor::White,FLinearColor PaceTint=RaceMuted) {
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(RaceRow).Padding(FMargin(8.f,3.f))
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[RaceLabel(Label,10,LabelTint)]
            + SHorizontalBox::Slot().FillWidth(1.f)[RaceLabel(Time,10)]
            + SHorizontalBox::Slot().FillWidth(1.35f)[RaceLabel(Pace,10,PaceTint)] ];
}

TSharedRef<SWidget> DebriefResultRow(const FString& Label,double Seconds,double Reference,FLinearColor LabelTint=FLinearColor::White) {
    if (FMath::IsFinite(Seconds) && Seconds>0. && FMath::IsFinite(Reference) && Reference>0.) {
        const double Delta=Seconds-Reference;
        if (FMath::Abs(Delta)>=.0005)
            return DebriefRow(Label,DebriefTime(Seconds),FString::Printf(TEXT("%+.3f s"),Delta),LabelTint,Delta<0. ? RaceCyan : RaceOrange);
        return DebriefRow(Label,DebriefTime(Seconds),TEXT("0.000 s"),LabelTint);
    }
    return DebriefRow(Label,DebriefTime(Seconds),TEXT("--"),LabelTint);
}

const TCHAR* CheckpointType(const AFactoryFPVRaceGate& Gate) {
    if (Gate.IsPoleCheckpoint()) return TEXT("Pole");
    if (Gate.IsFloorRaceGate()) return TEXT("Floor gate");
    if (Gate.IsVerticalRaceGate()) return TEXT("Raised vertical gate");
    return Gate.IsFullSizeGate() ? TEXT("Full-size gate") : TEXT("Compact gate");
}
const TCHAR* CheckpointPassage(const AFactoryFPVRaceGate& Gate) {
    if (Gate.IsPoleCheckpoint()) return Gate.GetPolePassSide()<0 ? TEXT("Pass left") : TEXT("Pass right");
    if (Gate.IsVerticalRaceGate()) return Gate.GetRaceDirection()==EFactoryFPVRaceGateDirection::Descend ? TEXT("Descend from above") : TEXT("Ascend from below");
    return TEXT("Follow the forward arrow");
}
const TCHAR* VerticalObstructionLocation(const AFactoryFPVRaceGate::FVerticalClearance& Clearance) {
    if (Clearance.bOpeningBlocked) return TEXT("at the opening");
    if (Clearance.bAboveBlocked && Clearance.bBelowBlocked) return TEXT("above and below the opening");
    if (Clearance.bAboveBlocked) return TEXT("above the opening");
    if (Clearance.bBelowBlocked) return TEXT("below the opening");
    return nullptr;
}
}

void SFactoryFPVRacePanel::Construct(const FArguments& Args) {
    Owner=Args._Owner;
    bEmbedded=Args._Embedded;
    AirframeDiameterCm=Args._AirframeDiameterCm;
    CanLaunchRace=Args._CanLaunchRace;
    OnRaceLaunch=Args._OnRaceLaunch;
    ShowRaceGhost=Args._ShowRaceGhost;
    OnShowRaceGhostChanged=Args._OnShowRaceGhostChanged;
    DraftDirection=EFactoryFPVRaceGateDirection::Forward;
    if (auto* Subsystem=Owner.Get()) {
        ViewedCourseId=Subsystem->GetSelectedRacingCourse();
        if (bEmbedded && ViewedCourseId==0) ViewedCourseId=Subsystem->GetRaceBuildCourse();
    }
    EditGate(Args._Gate.Get());
    const bool bDirectLaunch=bEmbedded && OnRaceLaunch.IsBound();
    auto CanUseCourse=[this,bDirectLaunch] {
        return Owner.IsValid() && bCourseReady && !HasDraftChanges() && !PendingNumberingCompaction &&
            (!bDirectLaunch || CanLaunchRace.Get(false));
    };

    auto Content=SNew(SBorder)
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(RaceBackground)
        .Padding(bEmbedded ? 0.f : 24.f)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,10)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[RaceLabel(TEXT("FPV TIME TRIALS"),bEmbedded ? 18 : 24,RaceOrange)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                    [ SNew(STextBlock).Text_Lambda([this] {
                        return FText::FromString(IsShowingDebrief() ? TEXT("Review laps and splits from your most recent scored race.") :
                            TEXT("Enter the pink side of the next checkpoint. Follow the course in order, then return to checkpoint 1 to finish."));
                      }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(RaceMuted).AutoWrapText(true) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(8,0,0,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceDebriefTab")))
                    .Visibility(bEmbedded && Owner.IsValid() ? EVisibility::Visible : EVisibility::Collapsed)
                    .Text_Lambda([this] { return FText::FromString(IsShowingDebrief() ? TEXT("Course tools") : TEXT("Last race")); })
                    .OnClicked(this,&SFactoryFPVRacePanel::ToggleRaceDebrief) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top)
                [ SNew(SButton).Visibility(bEmbedded ? EVisibility::Collapsed : EVisibility::Visible)
                    .Text(FText::FromString(TEXT("Close   Esc"))).OnClicked(this,&SFactoryFPVRacePanel::Close) ] ]
            + SVerticalBox::Slot().FillHeight(1)
            [ SAssignNew(RacePage,SWidgetSwitcher)
                + SWidgetSwitcher::Slot()
                [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.12f).Padding(0,0,8,0)[BuildCourseBrowser()]
                    + SHorizontalBox::Slot().FillWidth(1.f)[BuildGateEditor()] ]
                + SWidgetSwitcher::Slot()[SAssignNew(DebriefContent,SBox)] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
            [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Message); }).ColorAndOpacity(RaceCyan).AutoWrapText(true)
                .Visibility_Lambda([this] { return IsShowingDebrief() || Message.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(RaceCard).Padding(10)
                .Visibility_Lambda([this] { return IsShowingDebrief() ? EVisibility::Collapsed : EVisibility::Visible; })
                [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(this,&SFactoryFPVRacePanel::SelectedMode).ColorAndOpacity(RaceCyan).AutoWrapText(true) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                    [ SNew(SButton).Text(FText::FromString(TEXT("Free flight"))).IsEnabled_Lambda([this] { return Owner.IsValid(); })
                        .OnClicked(this,&SFactoryFPVRacePanel::FreeFlight) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                    [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceSaveAndPractice")))
                        .Visibility(bDirectLaunch ? EVisibility::Visible : EVisibility::Collapsed)
                        .Text(FText::FromString(TEXT("Save & practice")))
                        .ToolTipText(FText::FromString(TEXT("Save hangar settings and fly timed practice laps. Official times and ghosts stay as they are.")))
                        .IsEnabled_Lambda(CanUseCourse)
                        .OnClicked(this,&SFactoryFPVRacePanel::LaunchViewedCourse,true) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                    [ SAssignNew(RaceButton,SButton).Tag(FName(bDirectLaunch ? TEXT("FactoryFPVRaceSaveAndLaunch") : TEXT("FactoryFPVRaceSelectCourse")))
                        .Text(FText::FromString(bDirectLaunch ? TEXT("Save & race") : TEXT("Race course"))).ButtonColorAndOpacity(RaceOrange)
                        .ToolTipText(FText::FromString(bDirectLaunch ? TEXT("Save hangar settings and launch directly into the course shown in the browser.") :
                            TEXT("Select the course shown in the browser for your next flight.")))
                        .IsEnabled_Lambda(CanUseCourse)
                        .OnClicked_Lambda([this,bDirectLaunch] { return bDirectLaunch ? LaunchViewedCourse(false) : RaceCourse(); }) ] ] ] ];
    if (bEmbedded) ChildSlot[Content];
    else ChildSlot[SNew(SBox).WidthOverride(920).HeightOverride(650)[Content]];
    RefreshCourse();
}

bool SFactoryFPVRacePanel::IsShowingDebrief() const {
    return RacePage && RacePage->GetActiveWidgetIndex()==1;
}

FReply SFactoryFPVRacePanel::ToggleRaceDebrief() {
    if (!bEmbedded || !Owner.IsValid() || !RacePage || !DebriefContent) return FReply::Handled();
    if (IsShowingDebrief()) RacePage->SetActiveWidgetIndex(0);
    else {
        DebriefContent->SetContent(BuildRaceDebrief());
        RacePage->SetActiveWidgetIndex(1);
    }
    return FReply::Handled();
}

TSharedRef<SWidget> SFactoryFPVRacePanel::BuildRaceDebrief() const {
    const auto* Subsystem=Owner.Get();
    const auto* Report=Subsystem ? Subsystem->GetLastRaceDebrief() : nullptr;
    auto Card=SNew(SBorder).Tag(FName(TEXT("FactoryFPVRaceDebriefReport")))
        .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(RaceCard).Padding(12);
    if (!Report) {
        Card->SetContent(SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceDebriefSummary")))
                .Text(FText::FromString(TEXT("No completed race laps yet.")))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),14)).ColorAndOpacity(RaceOrange) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
            [ RaceLabel(TEXT("Finish a scored lap and return to the pioneer to review your times."),10,RaceMuted,true) ]);
        return Card;
    }

    const FString Track=Report->CourseName.IsEmpty() ? FString::Printf(TEXT("Course %d"),Report->CourseId) :
        FString::Printf(TEXT("%s · #%d"),*Report->CourseName,Report->CourseId);
    const FString Summary=FString::Printf(TEXT("%llu completed %s  /  Session fastest %s"),static_cast<unsigned long long>(Report->LapCount),
        Report->LapCount==1 ? TEXT("lap") : TEXT("laps"),*DebriefTime(Report->FastestLap));
    auto Laps=SNew(SScrollBox).Tag(FName(TEXT("FactoryFPVRaceDebriefRecentLaps")));
    for (int32 Index=Report->RecentLaps.Num()-1;Index>=0;--Index) {
        const auto& Lap=Report->RecentLaps[Index];
        const FString Label=FString::Printf(TEXT("%llu%s"),static_cast<unsigned long long>(Lap.Number),Lap.bCourseBest ? TEXT(" · NEW PB") : TEXT(""));
        Laps->AddSlot().Padding(0,1,4,0)[DebriefResultRow(Label,Lap.Seconds,Lap.ReferenceSeconds,Lap.bCourseBest ? RaceCyan : FLinearColor::White)];
    }
    auto Splits=SNew(SScrollBox).Tag(FName(TEXT("FactoryFPVRaceDebriefSplits")));
    for (const auto& Split:Report->LastLapSplits)
        Splits->AddSlot().Padding(0,1,4,0)[DebriefResultRow(FString::FromInt(Split.Checkpoint),Split.Seconds,Split.ReferenceSeconds)];
    if (Report->LastLapSplits.IsEmpty())
        Splits->AddSlot().Padding(8,4)[RaceLabel(TEXT("No checkpoint splits for this lap."),10,RaceMuted,true)];

    Card->SetContent(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[RaceLabel(Track,16,RaceOrange,true)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,10)
        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceDebriefSummary"))).Text(FText::FromString(Summary))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11)).ColorAndOpacity(RaceCyan).AutoWrapText(true) ]
        + SVerticalBox::Slot().FillHeight(1.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0,0,10,0)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[RaceLabel(TEXT("RECENT LAPS"),11,RaceOrange)]
                + SVerticalBox::Slot().AutoHeight()[DebriefRow(TEXT("Lap"),TEXT("Time"),TEXT("vs previous PB"),RaceMuted)]
                + SVerticalBox::Slot().FillHeight(1.f)[Laps] ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)[RaceLabel(TEXT("LAST LAP SPLITS"),11,RaceOrange)]
                + SVerticalBox::Slot().AutoHeight()[DebriefRow(TEXT("Checkpoint"),TEXT("Elapsed"),TEXT("vs previous PB"),RaceMuted)]
                + SVerticalBox::Slot().FillHeight(1.f)[Splits] ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
        [ RaceLabel(TEXT("Pace compares each result with the personal best it was flown against."),9,RaceMuted,true) ]);
    return Card;
}

TSharedRef<SWidget> SFactoryFPVRacePanel::BuildCourseBrowser() {
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(RaceCard).Padding(12)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[RaceLabel(TEXT("COURSE BROWSER"),13,RaceOrange)]
                + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceCheckpoints"))).Text(FText::FromString(TEXT("Checkpoints")))
                    .ButtonColorAndOpacity_Lambda([this] { return IsShowingRoute() ? RaceBackground : RaceOrange; })
                    .OnClicked_Lambda([this] { CourseView->SetActiveWidgetIndex(0);return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceRoute"))).Text(FText::FromString(TEXT("Route preview")))
                    .ButtonColorAndOpacity_Lambda([this] { return IsShowingRoute() ? RaceOrange : RaceBackground; })
                    .OnClicked_Lambda([this] { CourseView->SetActiveWidgetIndex(1);RefreshCourse();return FReply::Handled(); }) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,8)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[RaceLabel(TEXT("View course"))]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SAssignNew(CoursePicker,SComboBox<TSharedPtr<int32>>).Tag(FName(TEXT("FactoryFPVRaceCoursePicker")))
                    .OptionsSource(&CourseOptions).MaxListHeight(288.f).HasDownArrow(false)
                    .IsEnabled_Lambda([this] { return !CourseOptions.IsEmpty(); })
                    .OnGenerateWidget_Lambda([this](TSharedPtr<int32> Option) -> TSharedRef<SWidget> {
                        return SNew(STextBlock).Text_Lambda([this,Option] { return CourseOptionLabel(*Option); })
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                            .ToolTipText_Lambda([this,Option] { return CourseOptionLabel(*Option); });
                    })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<int32> Option,ESelectInfo::Type) {
                        if (Option && IsKnownCourse(*Option)) SelectViewedCourse(*Option);
                    })
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)
                        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceSelectedCourseLabel")))
                            .Text_Lambda([this] { return CourseOptionLabel(ViewedCourseId); })
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                            .ToolTipText_Lambda([this] { return CourseOptionLabel(ViewedCourseId); }) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(8,0,1,0).VAlign(VAlign_Center)
                        [ RaceLabel(TEXT("v"),11,RaceCyan) ] ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceRefresh"))).Text(FText::FromString(TEXT("Refresh")))
                    .OnClicked_Lambda([this] { RefreshCourse();return FReply::Handled(); }) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [ BuildCourseTools() ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[RaceLabel(TEXT("Track name"))]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SAssignNew(CourseNameInput,SEditableTextBox).Tag(FName(TEXT("FactoryFPVRaceCourseName")))
                    .HintText(FText::FromString(TEXT("Optional name")))
                    .IsEnabled(this,&SFactoryFPVRacePanel::CanEditCourseName)
                    .ToolTipText(FText::FromString(FString::Printf(TEXT("Use up to %d characters. Leave blank and apply to restore the course number."),
                        AFactoryFPVRaceCourseRegistry::MaximumNameLength)))
                    .OnVerifyTextChanged_Lambda([](const FText& Text,FText& Error) {
                        constexpr int32 Limit=AFactoryFPVRaceCourseRegistry::MaximumNameLength;
                        if (Text.ToString().Len()<=Limit) return true;
                        Error=FText::FromString(FString::Printf(TEXT("Use %d characters or fewer."),Limit));
                        return false;
                    }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceApplyCourseName"))).Text(FText::FromString(TEXT("Apply name")))
                    .IsEnabled(this,&SFactoryFPVRacePanel::HasCourseNameEdit).OnClicked(this,&SFactoryFPVRacePanel::ApplyCourseName)
                    .ToolTipText(FText::FromString(TEXT("Apply the name to this course number. Save the world to keep it."))) ] ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(CourseStatus); }).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11))
                .ColorAndOpacity_Lambda([this] { return bCourseReady ? RaceCyan : RaceOrange; }) ]
            + SVerticalBox::Slot().AutoHeight()
            [ BuildNumberingRepair() ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
            [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceCourseClearance")))
                .Text_Lambda([this] { return FText::FromString(CourseClearanceAdvisory); }).AutoWrapText(true).ColorAndOpacity(RaceOrange)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))
                .Visibility_Lambda([this] { return CourseClearanceAdvisory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,8)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceBestLap")))
                        .Text_Lambda([this] { return FText::FromString(BestLapStatus); }).AutoWrapText(true).ColorAndOpacity(RaceMuted)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                    [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceGhostStatus")))
                        .Text_Lambda([this] { return FText::FromString(GhostStatus); }).AutoWrapText(true).ColorAndOpacity(RaceMuted)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9))
                        .Visibility_Lambda([this] { return GhostStatus.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0).VAlign(VAlign_Top)
                [ SNew(SCheckBox).Tag(FName(TEXT("FactoryFPVShowRaceGhost")))
                    .Visibility(bEmbedded && OnShowRaceGhostChanged.IsBound() ? EVisibility::Visible : EVisibility::Collapsed)
                    .IsChecked_Lambda([this] { return ShowRaceGhost.Get(true) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                    .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { OnShowRaceGhostChanged.ExecuteIfBound(State==ECheckBoxState::Checked); })
                    .ToolTipText(FText::FromString(TEXT("Replay the saved best for this layout. This preference applies when you save or launch.")))
                    [ RaceLabel(TEXT("Show best-lap ghost"),9) ] ] ]
            + SVerticalBox::Slot().FillHeight(1)
            [ SAssignNew(CourseView,SWidgetSwitcher)
                + SWidgetSwitcher::Slot()
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().FillHeight(1)
                    [ SNew(SScrollBox).ScrollBarVisibility(EVisibility::Visible)
                        + SScrollBox::Slot().Padding(0,0,6,0)[SAssignNew(GateRows,SVerticalBox)] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [RaceLabel(TEXT("Shows nearby checkpoints and named courses. Select Edit here or press E on a checkpoint."),9,RaceMuted,true)] ]
                + SWidgetSwitcher::Slot()
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(RaceMuted)
                            .Text_Lambda([this] { return FText::FromString(RouteSummary); }).AutoWrapText(true)
                            .ToolTipText(FText::FromString(TEXT("Loop length connects each checkpoint to the next in 3D and returns to checkpoint 1."))) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceRoutePlan"))).Text(FText::FromString(TEXT("Plan view")))
                            .ButtonColorAndOpacity_Lambda([this] { return RoutePreview && !RoutePreview->IsElevation() ? RaceOrange : RaceBackground; })
                            .OnClicked_Lambda([this] { RoutePreview->SetElevation(false);return FReply::Handled(); }) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceRouteElevation"))).Text(FText::FromString(TEXT("Elevation")))
                            .ToolTipText(FText::FromString(TEXT("Height relative to checkpoint 1. Up and down arrows show Ascend and Descend.")))
                            .ButtonColorAndOpacity_Lambda([this] { return RoutePreview && RoutePreview->IsElevation() ? RaceOrange : RaceBackground; })
                            .OnClicked_Lambda([this] { RoutePreview->SetElevation(true);return FReply::Handled(); }) ] ]
                    + SVerticalBox::Slot().FillHeight(1)
                    [ SNew(SOverlay)
                        + SOverlay::Slot()
                        [ SAssignNew(RoutePreview,SFactoryFPVRaceRoutePreview).Tag(FName(TEXT("FactoryFPVRaceRouteChart")))
                            .Visibility_Lambda([this] { return RouteSummary.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible; }) ]
                        + SOverlay::Slot().VAlign(VAlign_Center).HAlign(HAlign_Center).Padding(12)
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("Load at least two checkpoints numbered in order from 1 to preview the route.")))
                            .AutoWrapText(true).Justification(ETextJustify::Center).ColorAndOpacity(RaceMuted)
                            .Visibility_Lambda([this] { return RouteSummary.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                    [RaceLabel(TEXT("Diamond: start/finish  ·  White outline: edited  ·  Amber: unpowered"),9,RaceMuted,true)] ] ] ];
}

TSharedRef<SWidget> SFactoryFPVRacePanel::BuildCourseTools() {
    return SNew(SHorizontalBox).Visibility(bEmbedded && Owner.IsValid() ? EVisibility::Visible : EVisibility::Collapsed)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceBuildTarget")))
            .Text_Lambda([this] { return FText::FromString(BuildCourseStatus); }).AutoWrapText(true)
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9))
            .ColorAndOpacity_Lambda([this] { return BuildCourseId>0 ? RaceCyan : RaceMuted; })
            .ToolTipText(FText::FromString(TEXT("New race checkpoints use the build target. This setting clears when you leave the world."))) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0).VAlign(VAlign_Center)
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceBuildViewed"))).Text(FText::FromString(TEXT("Build on this course")))
            .Visibility_Lambda([this] { return ViewedCourseId!=BuildCourseId && IsKnownCourse(ViewedCourseId) ? EVisibility::Visible : EVisibility::Collapsed; })
            .OnClicked_Lambda([this] { return SetBuildCourse(ViewedCourseId); }) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0).VAlign(VAlign_Center)
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceBuildNew"))).Text(FText::FromString(TEXT("Start a new course")))
            .Visibility_Lambda([this] { return BuildCourseId==0 ? EVisibility::Visible : EVisibility::Collapsed; })
            .ToolTipText(FText::FromString(TEXT("Choose an unused course number. Your first placed checkpoint starts at 1.")))
            .OnClicked(this,&SFactoryFPVRacePanel::StartNewBuildCourse) ]
        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0).VAlign(VAlign_Center)
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceBuildFinish"))).Text(FText::FromString(TEXT("Finish building")))
            .Visibility_Lambda([this] { return BuildCourseId>0 ? EVisibility::Visible : EVisibility::Collapsed; })
            .ToolTipText(FText::FromString(TEXT("Stop using this build target. Placed checkpoints stay in the course.")))
            .OnClicked(this,&SFactoryFPVRacePanel::SetBuildCourse,0) ];
}

TSharedRef<SWidget> SFactoryFPVRacePanel::BuildNumberingRepair() {
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0,6,0,0)
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceCloseGaps")))
            .Text(FText::FromString(TEXT("Close numbering gaps"))).ButtonColorAndOpacity(RaceOrange)
            .Visibility_Lambda([this] { return bCanCloseNumberingGaps && !PendingNumberingCompaction ? EVisibility::Visible : EVisibility::Collapsed; })
            .IsEnabled_Lambda([this] { return !HasDraftChanges(); })
            .ToolTipText_Lambda([this] { return FText::FromString(HasDraftChanges() ?
                TEXT("Apply or revert checkpoint edits first.") : TEXT("Preview new numbers for the loaded checkpoints in their current order.")); })
            .OnClicked(this,&SFactoryFPVRacePanel::CloseNumberingGaps) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(RaceSelectedRow).Padding(8)
            .Visibility_Lambda([this] { return PendingNumberingCompaction ? EVisibility::Visible : EVisibility::Collapsed; })
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11))
                    .Text_Lambda([this] { return PendingNumberingCompaction ? FText::FromString(FString::Printf(
                        TEXT("Close gaps on course %d?"),PendingNumberingCompaction->CourseId)) : FText::GetEmpty(); }) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceCloseGapsWarning")))
                    .Text(FText::FromString(TEXT("Loaded checkpoints only. Move near missing checkpoints first.")))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(RaceOrange).AutoWrapText(true) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                [ SNew(SBox).MaxDesiredHeight(44.f)
                    [ SNew(SScrollBox)
                        + SScrollBox::Slot().Padding(0,0,6,0)
                        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceCloseGapsPreview")))
                            .Text_Lambda([this] { return PendingNumberingCompaction ? NumberingPreview : FText::GetEmpty(); }).AutoWrapText(true)
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),11)).ColorAndOpacity(RaceCyan) ] ] ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceConfirmCloseGaps")))
                        .Text(FText::FromString(TEXT("Confirm renumber"))).ButtonColorAndOpacity(RaceOrange)
                        .OnClicked(this,&SFactoryFPVRacePanel::ConfirmNumberingCompaction) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                    [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceCancelCloseGaps"))).Text(FText::FromString(TEXT("Cancel")))
                        .OnClicked(this,&SFactoryFPVRacePanel::CancelNumberingCompaction) ] ] ] ];
}

TSharedRef<SWidget> SFactoryFPVRacePanel::BuildGateEditor() {
    const auto EditorVisibility=[this] { return CanEditGate() ? EVisibility::Visible : EVisibility::Collapsed; };
    const auto ShowingBestReset=[this] { return CanEditGate() && EditingGate->GetGateOrder()==1; };
    const auto ResetHelp=[this] {
        const FString Block=BestResetBlockReason();
        if (!Block.IsEmpty()) return FText::FromString(Block);
        return FText::FromString(ViewedBestSeconds>0. ? TEXT("Clear only the displayed record after confirmation.") :
            TEXT("There is no saved best for this loaded layout."));
    };
    return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .IsEnabled_Lambda([this] { return !PendingNumberingCompaction; })
        .BorderBackgroundColor(RaceCard).Padding(12)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[RaceLabel(TEXT("CHECKPOINT EDITOR"),13,RaceOrange)]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
            [ SNew(STextBlock).Text(this,&SFactoryFPVRacePanel::GateHeading).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),11)) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
            [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(HasDraftChanges() ? TEXT("UNSAVED EDITS") : TEXT("SAVED SETTINGS")); })
                .ColorAndOpacity_Lambda([this] { return HasDraftChanges() ? RaceOrange : RaceMuted; })
                .Visibility_Lambda(EditorVisibility).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)) ]
            + SVerticalBox::Slot().FillHeight(1).Padding(0,8,0,4)
            [ SNew(SScrollBox).Visibility_Lambda(EditorVisibility)
                + SScrollBox::Slot().Padding(0,0,6,0)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)[RaceLabel(TEXT("On course"))]
                        + SHorizontalBox::Slot().FillWidth(1)
                        [ SNew(SSpinBox<int32>).Tag(FName(TEXT("FactoryFPVRaceDraftCourse")))
                            .MinValue(1).MaxValue(99).MinSliderValue(1).MaxSliderValue(99)
                            .Value_Lambda([this] { return DraftCourseId; })
                            .OnValueChanged_Lambda([this](int32 Value) {
                                if (DraftCourseId!=Value) PendingOrderSwap.Reset();
                                DraftCourseId=Value;
                            }) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0,8,0)[RaceLabel(TEXT("Number"))]
                        + SHorizontalBox::Slot().FillWidth(1)
                        [ SNew(SSpinBox<int32>).Tag(FName(TEXT("FactoryFPVRaceDraftOrder")))
                            .MinValue(1).MaxValue(128).MinSliderValue(1).MaxSliderValue(128)
                            .Value_Lambda([this] { return DraftGateOrder; })
                            .OnValueChanged_Lambda([this](int32 Value) {
                                if (DraftGateOrder!=Value) PendingOrderSwap.Reset();
                                DraftGateOrder=Value;
                            }) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(SHorizontalBox).Visibility_Lambda([this] { return CanEditGate() && EditingGate->IsPoleCheckpoint() ? EVisibility::Visible : EVisibility::Collapsed; })
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[RaceLabel(TEXT("Pass pole on"))]
                        + SHorizontalBox::Slot().AutoWidth()
                        [ SNew(SButton).Text(FText::FromString(TEXT("Left"))).ButtonColorAndOpacity_Lambda([this] { return DraftPolePassSide==-1 ? RaceOrange : RaceBackground; })
                            .OnClicked(this,&SFactoryFPVRacePanel::SelectPoleSide,-1) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                        [ SNew(SButton).Text(FText::FromString(TEXT("Right"))).ButtonColorAndOpacity_Lambda([this] { return DraftPolePassSide==1 ? RaceOrange : RaceBackground; })
                            .OnClicked(this,&SFactoryFPVRacePanel::SelectPoleSide,1) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(SHorizontalBox).Visibility_Lambda([this] { return CanEditGate() && EditingGate->IsVerticalRaceGate() ? EVisibility::Visible : EVisibility::Collapsed; })
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)[RaceLabel(TEXT("Flight direction"))]
                        + SHorizontalBox::Slot().AutoWidth()
                        [ SNew(SButton).Text(FText::FromString(TEXT("Ascend")))
                            .ButtonColorAndOpacity_Lambda([this] { return DraftDirection==EFactoryFPVRaceGateDirection::Ascend ? RaceOrange : RaceBackground; })
                            .OnClicked(this,&SFactoryFPVRacePanel::SelectRaceDirection,EFactoryFPVRaceGateDirection::Ascend) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                        [ SNew(SButton).Text(FText::FromString(TEXT("Descend")))
                            .ButtonColorAndOpacity_Lambda([this] { return DraftDirection==EFactoryFPVRaceGateDirection::Descend ? RaceOrange : RaceBackground; })
                            .OnClicked(this,&SFactoryFPVRacePanel::SelectRaceDirection,EFactoryFPVRaceGateDirection::Descend) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(STextBlock).Text(this,&SFactoryFPVRacePanel::CheckpointInstructions).AutoWrapText(true).ColorAndOpacity(RaceCyan)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                    [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRaceCheckpointClearance")))
                        .Text_Lambda([this] { return FText::FromString(CheckpointClearanceAdvisory); }).AutoWrapText(true).ColorAndOpacity(RaceOrange)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10))
                        .Visibility_Lambda([this] { return CheckpointClearanceAdvisory.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [RaceLabel(TEXT("Apply gate saves edits. Only laps on the current layout count toward its best."),9,RaceMuted,true)]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceResetBest")))
                        .Text_Lambda([this] { return FText::FromString(FString::Printf(TEXT("Reset saved best for course %d"),CanEditGate() ? EditingGate->GetCourseId() : 0)); })
                        .Visibility_Lambda([this,ShowingBestReset] { return ShowingBestReset() && !PendingBestReset ? EVisibility::Visible : EVisibility::Collapsed; })
                        .IsEnabled_Lambda([this] { return ViewedBestSeconds>0. && BestResetBlockReason().IsEmpty(); })
                        .ToolTipText_Lambda(ResetHelp).OnClicked(this,&SFactoryFPVRacePanel::ResetBest) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                    [ SNew(STextBlock).Text_Lambda(ResetHelp).AutoWrapText(true).ColorAndOpacity(RaceMuted)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9))
                        .Visibility_Lambda([this,ShowingBestReset] { return ShowingBestReset() && !PendingBestReset &&
                            (ViewedBestSeconds<=0. || !BestResetBlockReason().IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed; }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(SVerticalBox).Visibility_Lambda([this] { return PendingBestReset ? EVisibility::Visible : EVisibility::Collapsed; })
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(RaceOrange).Text_Lambda([this] {
                            return PendingBestReset ? FText::FromString(FString::Printf(TEXT("Clear the %.3f s record for course %d?"),
                                PendingBestReset->Seconds,PendingBestReset->CourseId)) : FText::GetEmpty(); }) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                        [ SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth()
                            [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceConfirmBestReset")))
                                .Text(FText::FromString(TEXT("Confirm reset"))).ButtonColorAndOpacity(RaceOrange)
                                .OnClicked(this,&SFactoryFPVRacePanel::ConfirmBestReset) ]
                            + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                            [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceCancelBestReset")))
                                .Text(FText::FromString(TEXT("Cancel reset"))).OnClicked(this,&SFactoryFPVRacePanel::CancelBestReset) ] ] ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(RaceSelectedRow).Padding(8)
                .Visibility_Lambda([this] { return PendingOrderSwap && CanEditGate() ? EVisibility::Visible : EVisibility::Collapsed; })
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).AutoWrapText(true).ColorAndOpacity(RaceOrange).Text_Lambda([this] {
                        return PendingOrderSwap ? FText::FromString(FString::Printf(TEXT("Swap checkpoints %d and %d on course %d? Best times follow the layout."),
                            PendingOrderSwap->FromOrder,PendingOrderSwap->ToOrder,PendingOrderSwap->CourseId)) : FText::GetEmpty();
                    }) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth()
                        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceConfirmSwap"))).Text(FText::FromString(TEXT("Swap numbers")))
                            .ButtonColorAndOpacity(RaceOrange).OnClicked(this,&SFactoryFPVRacePanel::ConfirmOrderSwap) ]
                        + SHorizontalBox::Slot().AutoWidth().Padding(6,0,0,0)
                        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVRaceCancelSwap"))).Text(FText::FromString(TEXT("Cancel swap")))
                            .OnClicked(this,&SFactoryFPVRacePanel::CancelOrderSwap) ] ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
            [ SNew(SHorizontalBox).Visibility_Lambda(EditorVisibility)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SAssignNew(ApplyButton,SButton).Tag(FName(TEXT("FactoryFPVRaceApplyGate"))).Text(FText::FromString(TEXT("Apply gate"))).HAlign(HAlign_Center)
                    .ButtonColorAndOpacity(RaceOrange).IsEnabled_Lambda([this] { return CanEditGate() && !PendingOrderSwap; })
                    .OnClicked(this,&SFactoryFPVRacePanel::ApplyGate) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(8,0,0,0)
                [ SNew(SButton).Text(FText::FromString(TEXT("Revert"))).IsEnabled_Lambda([this] { return CanEditGate() && HasDraftChanges(); })
                    .OnClicked(this,&SFactoryFPVRacePanel::RevertGate) ] ] ];
}

UWorld* SFactoryFPVRacePanel::CurrentWorld() const {
    if (auto* Subsystem=Owner.Get()) return Subsystem->GetWorld();
    return EditingGate.IsValid() ? EditingGate->GetWorld() : nullptr;
}

void SFactoryFPVRacePanel::EditGate(AFactoryFPVRaceGate* Gate) {
    if (!IsValid(Gate) || Gate->IsActorBeingDestroyed() || Gate->GetIsDismantled() ||
        (Owner.IsValid() && Gate->GetWorld()!=Owner->GetWorld())) return;
    if (HasDraftChanges()) {
        Message=TEXT("Apply or revert the current gate edits before changing the edit selection.");
        return;
    }
    PendingBestReset.Reset();
    PendingOrderSwap.Reset();
    PendingNumberingCompaction.Reset();
    EditingGate=Gate;
    bHadGate=true;
    DraftCourseId=Gate->GetCourseId();
    DraftGateOrder=Gate->GetGateOrder();
    DraftPolePassSide=Gate->GetPolePassSide();
    DraftDirection=Gate->GetRaceDirection();
    ViewedCourseId=Gate->GetCourseId();
    Message.Reset();
    RefreshCourse();
}

bool SFactoryFPVRacePanel::CanEditGate() const {
    const auto* Gate=EditingGate.Get();
    return Gate && !Gate->IsActorBeingDestroyed() && !Gate->GetIsDismantled() && Gate->GetWorld()==CurrentWorld();
}

bool SFactoryFPVRacePanel::HasDraftChanges() const {
    return CanEditGate() && (DraftCourseId!=EditingGate->GetCourseId() || DraftGateOrder!=EditingGate->GetGateOrder() ||
        (EditingGate->IsPoleCheckpoint() && DraftPolePassSide!=EditingGate->GetPolePassSide()) ||
        (EditingGate->IsVerticalRaceGate() && DraftDirection!=EditingGate->GetRaceDirection()));
}

FText SFactoryFPVRacePanel::GateHeading() const {
    if (!CanEditGate()) return FText::FromString(TEXT("Select Edit beside a loaded checkpoint to change its settings."));
    return FText::FromString(FString::Printf(TEXT("%s %d  /  Course %d  /  %s"),CheckpointType(*EditingGate),EditingGate->GetGateOrder(),EditingGate->GetCourseId(),
        EditingGate->IsBuildableInsideBlueprintDesigner() ? TEXT("BLUEPRINT PREVIEW / NOT COUNTING") :
        (EditingGate->IsGatePowered() ? TEXT("POWERED") : TEXT("NO POWER"))));
}

FText SFactoryFPVRacePanel::CheckpointInstructions() const {
    if (!CanEditGate()) return FText::GetEmpty();
    if (EditingGate->IsPoleCheckpoint()) return FText::FromString(FString::Printf(
        TEXT("Pass on the %s following the forward arrow. Stay within 8 m of the pole and 0.2 to 3.8 m above its base."),
        DraftPolePassSide==-1 ? TEXT("LEFT") : TEXT("RIGHT")));
    if (EditingGate->IsVerticalRaceGate()) return FText::FromString(DraftDirection==EFactoryFPVRaceGateDirection::Descend ?
        TEXT("Enter from ABOVE and fly down through the opening.") : TEXT("Enter from BELOW and fly up through the opening."));
    return FText::FromString(EditingGate->GetCheckpointInstructions());
}

FReply SFactoryFPVRacePanel::SelectPoleSide(int32 Side) {
    if (CanEditGate() && EditingGate->IsPoleCheckpoint() && (Side==-1 || Side==1) && Side!=DraftPolePassSide) {
        PendingOrderSwap.Reset();
        DraftPolePassSide=Side;
    }
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::SelectRaceDirection(EFactoryFPVRaceGateDirection Direction) {
    if (CanEditGate() && EditingGate->IsVerticalRaceGate() &&
        (Direction==EFactoryFPVRaceGateDirection::Ascend || Direction==EFactoryFPVRaceGateDirection::Descend) && Direction!=DraftDirection) {
        PendingOrderSwap.Reset();
        DraftDirection=Direction;
    }
    return FReply::Handled();
}

FText SFactoryFPVRacePanel::SelectedMode() const {
    if (bEmbedded && OnRaceLaunch.IsBound())
        return FText::FromString(ViewedCourseId>0 ? FString::Printf(TEXT("TIME TRIAL: %s"),*CourseLabel(ViewedCourseId)) : TEXT("TIME TRIAL: No course selected"));
    const auto* Subsystem=Owner.Get();
    const int32 Course=Subsystem ? Subsystem->GetSelectedRacingCourse() : 0;
    if (Course>0 && Course==ViewedCourseId && ViewedLayout!=Subsystem->GetSelectedRacingLayout())
        return FText::FromString(FString::Printf(TEXT("NEXT FLIGHT: Reselect course %d"),Course));
    return FText::FromString(Course>0 ? FString::Printf(TEXT("NEXT FLIGHT: %s time trial"),*CourseLabel(Course)) : TEXT("NEXT FLIGHT: Free flight"));
}

FString SFactoryFPVRacePanel::CourseLabel(int32 CourseId) const {
    if (const auto* Name=CourseNames.Find(CourseId); Name && !Name->IsEmpty())
        return FString::Printf(TEXT("%s · #%d"),**Name,CourseId);
    return FString::Printf(TEXT("Course %d"),CourseId);
}

FText SFactoryFPVRacePanel::CourseOptionLabel(int32 CourseId) const {
    if (CourseId<=0) return FText::FromString(TEXT("No loaded courses"));
    const FString Label=CourseLabel(CourseId);
    if (const int32* Count=LoadedCourseCounts.Find(CourseId))
        return FText::FromString(FString::Printf(TEXT("%s (%d %s)"),*Label,*Count,*Count==1 ? TEXT("checkpoint") : TEXT("checkpoints")));
    return FText::FromString(Label+(CourseId==BuildCourseId ? TEXT(" (build target)") : TEXT(" (not loaded)")));
}

bool SFactoryFPVRacePanel::IsKnownCourse(int32 CourseId) const {
    return CourseId>0 && (CourseId==BuildCourseId || LoadedCourseCounts.Contains(CourseId) || CourseNames.Contains(CourseId));
}

void SFactoryFPVRacePanel::SelectViewedCourse(int32 CourseId) {
    if (ViewedCourseId==CourseId) return;
    PendingBestReset.Reset();
    PendingOrderSwap.Reset();
    PendingNumberingCompaction.Reset();
    ViewedCourseId=CourseId;
    RefreshCourse();
}

void SFactoryFPVRacePanel::RefreshCoursePicker() {
    TArray<int32> CourseIds;
    LoadedCourseCounts.GenerateKeyArray(CourseIds);
    for (const auto& Name:CourseNames) CourseIds.AddUnique(Name.Key);
    if (BuildCourseId>0) CourseIds.AddUnique(BuildCourseId);
    CourseIds.Sort();
    if (ViewedCourseId==0 && !CourseIds.IsEmpty()) {
        const auto* Loaded=CourseIds.FindByPredicate([this](int32 Id) { return LoadedCourseCounts.Contains(Id); });
        ViewedCourseId=Loaded ? *Loaded : CourseIds[0];
    }

    TArray<TSharedPtr<int32>> Options;
    TSharedPtr<int32> ViewedOption;
    for (int32 CourseId:CourseIds) {
        const auto* Existing=CourseOptions.FindByPredicate([CourseId](const auto& Option) { return *Option==CourseId; });
        auto Option=Existing ? *Existing : MakeShared<int32>(CourseId);
        if (CourseId==ViewedCourseId) ViewedOption=Option;
        Options.Add(MoveTemp(Option));
    }
    if (Options!=CourseOptions) {
        CourseOptions=MoveTemp(Options);
        if (CoursePicker) CoursePicker->RefreshOptions();
    }
    if (CoursePicker && CoursePicker->GetSelectedItem()!=ViewedOption) CoursePicker->SetSelectedItem(ViewedOption);
}

void SFactoryFPVRacePanel::RefreshCourseNameInput() {
    if (!CourseNameInput) return;
    const FString Saved=CourseNames.FindRef(ViewedCourseId);
    // Synchronize clean text after a saved change, without resetting the
    // caret or replacing a draft during the normal periodic refresh.
    if (NameInputCourseId!=ViewedCourseId || (Saved!=NameInputBaseline && CourseNameInput->GetText().ToString()==NameInputBaseline)) {
        NameInputCourseId=ViewedCourseId;
        NameInputBaseline=Saved;
        CourseNameInput->SetText(FText::FromString(Saved));
    }
}

bool SFactoryFPVRacePanel::CanEditCourseName() const {
    return bCourseNamesAvailable && IsKnownCourse(ViewedCourseId);
}

bool SFactoryFPVRacePanel::HasCourseNameEdit() const {
    return CourseNameInput && CanEditCourseName() && NameInputCourseId==ViewedCourseId &&
        CourseNameInput->GetText().ToString().TrimStartAndEnd()!=CourseNames.FindRef(ViewedCourseId);
}

FReply SFactoryFPVRacePanel::ApplyCourseName() {
    if (!HasCourseNameEdit()) return FReply::Handled();
    auto* Registry=AFactoryFPVRaceCourseRegistry::Get(CurrentWorld());
    if (!Registry) { Message=TEXT("Track names are unavailable in this world."); return FReply::Handled(); }
    FString Error;
    if (!Registry->SetCourseName(ViewedCourseId,CourseNameInput->GetText().ToString(),Error)) {
        Message=MoveTemp(Error);
        return FReply::Handled();
    }
    NameInputBaseline=Registry->GetNameForCourse(ViewedCourseId);
    CourseNameInput->SetText(FText::FromString(NameInputBaseline));
    Message=NameInputBaseline.IsEmpty() ? TEXT("Track name cleared. Save the world to keep the change.") :
        TEXT("Track name applied. Save the world to keep the change.");
    RefreshCourse();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::SetBuildCourse(int32 CourseId) {
    auto* Subsystem=Owner.Get();
    if (!bEmbedded || !Subsystem || (CourseId>0 && !IsKnownCourse(CourseId))) return FReply::Handled();
    if (!Subsystem->SetRaceBuildCourse(CourseId)) Message=TEXT("This course cannot be used as a build target here.");
    else Message=CourseId>0 ? TEXT("Build Gun will add the next checkpoint to this course.") : TEXT("Build target cleared.");
    RefreshCourse();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::StartNewBuildCourse() {
    auto* Subsystem=Owner.Get();
    if (!bEmbedded || !Subsystem || Subsystem->GetRaceBuildCourse()>0) return FReply::Handled();
    const int32 CourseId=Subsystem->StartNewRaceBuildCourse();
    if (CourseId>0) {
        Message=TEXT("Build Gun will place checkpoint 1 on the new course.");
        if (ViewedCourseId==CourseId) RefreshCourse();
        else SelectViewedCourse(CourseId);
    } else Message=TEXT("No unused course number is available here.");
    return FReply::Handled();
}

void SFactoryFPVRacePanel::RefreshCourse() {
    UWorld* World=CurrentWorld();
    const auto* Subsystem=Owner.Get();
    BuildCourseId=bEmbedded && Subsystem ? Subsystem->GetRaceBuildCourse() : 0;
    const int32 NextOrder=BuildCourseId>0 ? Subsystem->GetNextRaceBuildOrder() : 0;
    BuildCourseStatus=BuildCourseId<=0 ? TEXT("Build target: default") : NextOrder>0 ?
        FString::Printf(TEXT("Building #%d · next checkpoint %d"),BuildCourseId,NextOrder) :
        FString::Printf(TEXT("Building #%d · checkpoint limit reached"),BuildCourseId);
    TMap<int32,TArray<TWeakObjectPtr<AFactoryFPVRaceGate>>> Courses;
    LoadedCourseCounts.Reset();
    if (World) for (TActorIterator<AFactoryFPVRaceGate> It(World);It;++It) {
        if (!It->IsLiveRaceGate()) continue;
        Courses.FindOrAdd(It->GetCourseId()).Add(*It);
    }
    for (const auto& Course:Courses) LoadedCourseCounts.Add(Course.Key,Course.Value.Num());
    const auto* Registry=AFactoryFPVRaceCourseRegistry::Get(World);
    bCourseNamesAvailable=Registry!=nullptr;
    if (Registry) CourseNames=Registry->GetNames();
    else CourseNames.Reset();
    RefreshCoursePicker();
    RefreshCourseNameInput();
    auto CurrentGates=Courses.FindRef(ViewedCourseId);
    CurrentGates.Sort([](const auto& Left,const auto& Right) {
        if (!Left.IsValid() || !Right.IsValid()) return Left.IsValid();
        if (Left->GetGateOrder()!=Right->GetGateOrder()) return Left->GetGateOrder()<Right->GetGateOrder();
        return Left->GetPathName()<Right->GetPathName();
    });
    const bool bRowsChanged=CurrentGates!=LoadedGates;
    LoadedGates=MoveTemp(CurrentGates);
    AFactoryFPVRaceGate::FEditorNumberingCompaction AvailableCompaction;
    FString Error;
    // Probe only plausible gaps. The actor validates the full loaded set and
    // keeps duplicates, missing starts and unstable identities out of the UI.
    bCanCloseNumberingGaps=LoadedGates.Num()>=2 && LoadedGates[0].IsValid() && LoadedGates[0]->GetGateOrder()==1 &&
        LoadedGates.Last().IsValid() && LoadedGates.Last()->GetGateOrder()>LoadedGates.Num() &&
        AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(World,ViewedCourseId,AvailableCompaction,Error);
    RefreshClearanceAdvisories();
    bCourseReady=World && ViewedCourseId>0 && UFactoryFPVRaceComponent::ValidateCourse(World,ViewedCourseId,Error,true);
    CourseStatus=!World ? TEXT("Load a single-player save to view and select a race course.") :
        ViewedCourseId==0 ? TEXT("No loaded courses to race.") :
        ViewedCourseId==BuildCourseId && LoadedGates.IsEmpty() ? TEXT("Use the Build Gun to place checkpoints on this course.") :
        bCourseReady ? FString::Printf(TEXT("Course %d ready  /  %d powered checkpoints"),ViewedCourseId,LoadedGates.Num()) : Error;
    ViewedLayout=World && ViewedCourseId>0 ? UFactoryFPVRaceComponent::LayoutSignature(World,ViewedCourseId) : FString();
    double Best=0;
    const AFactoryFPVRaceGate* Start=nullptr;
    for (const auto& Entry:LoadedGates) if (Entry.IsValid() && Entry->GetGateOrder()==1) {
        Start=Entry.Get();
        Best=Start->GetBestLapSeconds(ViewedLayout);
        break;
    }
    // Resetting an existing record does not require power, but the loaded
    // layout still needs a unique, complete start and checkpoint sequence.
    const bool bLayoutValid=bCourseReady || ((IsShowingRoute() || Best>0.) && World && ViewedCourseId>0 &&
        UFactoryFPVRaceComponent::ValidateCourse(World,ViewedCourseId,Error,false));
    ViewedBestSeconds=bLayoutValid ? Best : 0.;
    BestLapStatus=ViewedBestSeconds>0. ? FString::Printf(TEXT("Course best: %.3f s"),ViewedBestSeconds) : TEXT("Course best: no completed lap on this layout");
    GhostStatus.Reset();
    if (!LoadedGates.IsEmpty()) {
        if (ViewedBestSeconds<=0.) GhostStatus=TEXT("Ghost: Finish your first lap to record one.");
        else if (Start && Start->GetBestLapGhost(ViewedLayout)) GhostStatus=TEXT("Ghost: Best-lap replay available.");
        else if (ViewedBestSeconds>FFactoryFPVRaceGhost::MaxDuration) GhostStatus=FString::Printf(
            TEXT("Ghost: This lap exceeds the %g-minute recording limit."),FFactoryFPVRaceGhost::MaxDuration/60.);
        else GhostStatus=TEXT("Ghost: Set a new best to record one.");
    }
    if (IsShowingRoute()) RefreshRoutePreview(bLayoutValid);
    // Keep existing buttons alive when the same ordered checkpoints are still
    // loaded. Their text, power and selection bindings update in place.
    if (!GateRows || (!bRowsChanged && GateRows->GetChildren()->Num()>0)) return;
    GateRows->ClearChildren();
    if (LoadedGates.IsEmpty()) GateRows->AddSlot().AutoHeight().Padding(0,8)[RaceLabel(TEXT("Move near an existing course, or build and power at least two checkpoints numbered 1 through N."),10,RaceMuted,true)];
    for (const auto& Entry:LoadedGates) {
        const auto Weak=Entry;
        GateRows->AddSlot().AutoHeight().Padding(0,2)
        [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"))).Padding(8)
            .BorderBackgroundColor_Lambda([this,Weak] { return Weak.IsValid() && EditingGate==Weak ? RaceSelectedRow : RaceRow; })
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [ SNew(SBox).WidthOverride(34)
                    [ SNew(STextBlock).Text_Lambda([Weak] { return Weak.IsValid() ? FText::AsNumber(Weak->GetGateOrder()) : FText::GetEmpty(); })
                        .Justification(ETextJustify::Center).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),15)).ColorAndOpacity(RaceOrange) ] ]
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text_Lambda([Weak] {
                        if (!Weak.IsValid()) return FText::FromString(TEXT("Unavailable"));
                        return FText::FromString(Weak->GetGateOrder()==1 ? FString::Printf(TEXT("%s  /  Start & finish"),CheckpointType(*Weak.Get())) : FString(CheckpointType(*Weak.Get())));
                    }).AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10)) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,3,0,0)
                    [ SNew(STextBlock).Text_Lambda([Weak] { return Weak.IsValid() ? FText::FromString(CheckpointPassage(*Weak.Get())) : FText::GetEmpty(); })
                        .AutoWrapText(true).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(RaceMuted) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8,0,8,0)
                [ SNew(STextBlock).Text_Lambda([Weak] { return FText::FromString(Weak.IsValid() && Weak->IsGatePowered() ? TEXT("POWERED") : TEXT("NO POWER")); })
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9))
                    .ColorAndOpacity_Lambda([Weak] { return Weak.IsValid() && Weak->IsGatePowered() ? RaceCyan : RaceOrange; }) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(FText::FromString(TEXT("Edit")))
                    .OnClicked_Lambda([this,Weak] { EditGate(Weak.Get()); return FReply::Handled(); }) ] ] ];
    }
}

void SFactoryFPVRacePanel::RefreshClearanceAdvisories() {
    CourseClearanceAdvisory.Reset();
    CheckpointClearanceAdvisory.Reset();
    const double RadiusCm=AirframeDiameterCm.Get(GetDefault<UFactoryFPVSettings>()->FrameSizeCm)*.5;
    const auto* EditedGate=CanEditGate() && EditingGate->IsLiveRaceGate() && EditingGate->IsVerticalRaceGate() ? EditingGate.Get() : nullptr;
    const auto EditedClearance=EditedGate ? EditedGate->ProbeVerticalClearance(RadiusCm) : AFactoryFPVRaceGate::FVerticalClearance{};
    if (const TCHAR* Location=VerticalObstructionLocation(EditedClearance)) {
        CheckpointClearanceAdvisory=FString::Printf(TEXT("Center line blocked %s. An offset path may still work."),Location);
    }
    for (const auto& Entry:LoadedGates) {
        const auto* Gate=Entry.Get();
        if (!Gate || !Gate->IsVerticalRaceGate()) continue;
        const auto Clearance=Gate==EditedGate ? EditedClearance : Gate->ProbeVerticalClearance(RadiusCm);
        if (const TCHAR* Location=VerticalObstructionLocation(Clearance)) {
            CourseClearanceAdvisory=FString::Printf(TEXT("Checkpoint %d center line is blocked %s. An offset path may still work."),
                Gate->GetGateOrder(),Location);
            break;
        }
    }
}

bool SFactoryFPVRacePanel::IsShowingRoute() const {
    return CourseView && CourseView->GetActiveWidgetIndex()==1;
}

void SFactoryFPVRacePanel::RefreshRoutePreview(bool bLayoutValid) {
    if (!RoutePreview) return;
    RouteSummary.Reset();
    TArray<SFactoryFPVRaceRoutePreview::FCheckpoint> Points;
    if (bLayoutValid) {
        Points.Reserve(LoadedGates.Num());
        for (const auto& Entry:LoadedGates) if (const auto* Gate=Entry.Get()) {
            Points.Add({Gate->GetGateOrder(),Gate->GetPassageAimPoint(),Gate->IsGatePowered(),Entry==EditingGate,Gate->GetRaceDirection()});
        }
    }
    if (Points.Num()>=2) {
        double Length=0.,Low=Points[0].Position.Z,High=Low;
        for (int32 Index=0;Index<Points.Num();++Index) {
            const FVector& Position=Points[Index].Position;
            Length+=FVector::Dist(Position,Points[(Index+1)%Points.Num()].Position);
            Low=FMath::Min(Low,Position.Z);
            High=FMath::Max(High,Position.Z);
        }
        RouteSummary=FString::Printf(TEXT("%.1f m loop  /  %.1f m elevation span"),Length/100.,(High-Low)/100.);
    }
    RoutePreview->SetRoute(MoveTemp(Points));
}

void SFactoryFPVRacePanel::Tick(const FGeometry& Geometry,double Time,float DeltaTime) {
    SCompoundWidget::Tick(Geometry,Time,DeltaTime);
    if (bHadGate && !CanEditGate()) {
        bHadGate=false;
        EditingGate.Reset();
        PendingBestReset.Reset();
        PendingOrderSwap.Reset();
        Message=TEXT("The edited gate is no longer available. Choose another loaded gate or close this panel.");
    }
    if (Time>=NextRefresh) { NextRefresh=Time+1.; RefreshCourse(); }
}

FReply SFactoryFPVRacePanel::ApplyGate() {
    if (PendingOrderSwap || PendingNumberingCompaction) return FReply::Handled();
    auto* Gate=EditingGate.Get();
    if (!CanEditGate()) { Message=TEXT("Select a loaded gate before applying settings."); return FReply::Handled(); }
    PendingBestReset.Reset();
    FString Error;
    if (Gate->ConfigureFromEditor(DraftCourseId,DraftGateOrder,DraftPolePassSide,DraftDirection,Error,PendingOrderSwap))
        GateEditsSaved(TEXT("Checkpoint settings saved."));
    else Message=PendingOrderSwap ? FString() : MoveTemp(Error);
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::CloseNumberingGaps() {
    if (PendingNumberingCompaction) return FReply::Handled();
    if (HasDraftChanges()) {
        Message=TEXT("Apply or revert checkpoint edits before closing numbering gaps.");
        return FReply::Handled();
    }
    AFactoryFPVRaceGate::FEditorNumberingCompaction Proposal;
    FString Error;
    if (!AFactoryFPVRaceGate::ProposeEditorNumberingCompaction(CurrentWorld(),ViewedCourseId,Proposal,Error)) {
        Message=MoveTemp(Error);
        RefreshCourse();
        return FReply::Handled();
    }
    FString Preview;
    int32 ChangeCount=0;
    for (int32 Index=0;Index<Proposal.Entries.Num();++Index) {
        const int32 OldOrder=Proposal.Entries[Index].OldOrder;
        const int32 NewOrder=Index+1;
        if (OldOrder==NewOrder) continue;
        if (ChangeCount>0) Preview+=ChangeCount%4==0 ? TEXT("\n") : TEXT("   ·   ");
        Preview+=FString::Printf(TEXT("%d → %d"),OldOrder,NewOrder);
        ++ChangeCount;
    }
    NumberingPreview=FText::FromString(MoveTemp(Preview));
    PendingBestReset.Reset();
    PendingOrderSwap.Reset();
    PendingNumberingCompaction=MoveTemp(Proposal);
    Message.Reset();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::ConfirmNumberingCompaction() {
    if (!PendingNumberingCompaction) return FReply::Handled();
    const auto Proposal=MoveTemp(PendingNumberingCompaction.GetValue());
    PendingNumberingCompaction.Reset();
    if (HasDraftChanges() || Proposal.CourseId!=ViewedCourseId) {
        Message=TEXT("The viewed course or a checkpoint draft changed. Review the numbering again.");
        return FReply::Handled();
    }
    FString Error;
    if (AFactoryFPVRaceGate::ConfirmEditorNumberingCompaction(CurrentWorld(),Proposal,Error)) {
        if (CanEditGate()) DraftGateOrder=EditingGate->GetGateOrder();
        Message=TEXT("Checkpoint numbers updated. Use Race course when the layout is ready.");
    } else Message=MoveTemp(Error);
    RefreshCourse();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::CancelNumberingCompaction() {
    if (PendingNumberingCompaction) {
        PendingNumberingCompaction.Reset();
        Message=TEXT("Checkpoint numbers kept.");
    }
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::ConfirmOrderSwap() {
    if (!PendingOrderSwap) return FReply::Handled();
    const auto Proposal=MoveTemp(PendingOrderSwap.GetValue());
    PendingOrderSwap.Reset();
    if (!CanEditGate() || DraftCourseId!=Proposal.CourseId || DraftGateOrder!=Proposal.ToOrder ||
        DraftPolePassSide!=Proposal.RequestedPassSide || DraftDirection!=Proposal.RequestedDirection) {
        Message=TEXT("The checkpoint or its draft changed. Review your edits and apply again.");
        return FReply::Handled();
    }
    FString Error;
    if (EditingGate->ConfirmEditorOrderSwap(Proposal,Error)) GateEditsSaved(TEXT("Checkpoint numbers swapped."));
    else { Message=MoveTemp(Error);RefreshCourse(); }
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::CancelOrderSwap() {
    if (!PendingOrderSwap) return FReply::Handled();
    PendingOrderSwap.Reset();
    Message=TEXT("Swap cancelled. Choose a different number or revert the draft.");
    return FReply::Handled();
}

void SFactoryFPVRacePanel::GateEditsSaved(const TCHAR* Feedback) {
    const auto& Gate=*EditingGate.Get();
    ViewedCourseId=DraftCourseId=Gate.GetCourseId();
    DraftGateOrder=Gate.GetGateOrder();
    DraftPolePassSide=Gate.GetPolePassSide();
    DraftDirection=Gate.GetRaceDirection();
    Message=Feedback;
    RefreshCourse();
}

FReply SFactoryFPVRacePanel::RevertGate() {
    if (!CanEditGate()) { Message=TEXT("Select a loaded gate before reverting settings.");return FReply::Handled(); }
    PendingBestReset.Reset();
    PendingOrderSwap.Reset();
    DraftCourseId=EditingGate->GetCourseId();
    DraftGateOrder=EditingGate->GetGateOrder();
    DraftPolePassSide=EditingGate->GetPolePassSide();
    DraftDirection=EditingGate->GetRaceDirection();
    ViewedCourseId=EditingGate->GetCourseId();
    Message=TEXT("Draft edits discarded. The gate's saved settings are shown.");
    RefreshCourse();
    return FReply::Handled();
}

bool SFactoryFPVRacePanel::ValidateViewedCourse() {
    if (!Owner.IsValid()) { Message=TEXT("Open the hangar in a loaded single-player game to select racing."); return false; }
    if (HasDraftChanges()) { Message=TEXT("Apply or revert gate edits before selecting a race course."); return false; }
    if (PendingNumberingCompaction) { Message=TEXT("Confirm or cancel the renumbering preview first."); return false; }
    FString Error;
    if (!UFactoryFPVRaceComponent::ValidateCourse(CurrentWorld(),ViewedCourseId,Error,true)) {
        Message=Error;
        RefreshCourse();
        return false;
    }
    return true;
}

FReply SFactoryFPVRacePanel::LaunchViewedCourse(bool bPractice) {
    if (!bEmbedded || !OnRaceLaunch.IsBound() || !CanLaunchRace.Get(false) || !ValidateViewedCourse()) return FReply::Handled();
    const auto KeepAlive=SharedThis(this);
    return OnRaceLaunch.Execute(ViewedCourseId,bPractice);
}

FReply SFactoryFPVRacePanel::RaceCourse() {
    if (!ValidateViewedCourse()) return FReply::Handled();
    auto* Subsystem=Owner.Get();
    Subsystem->SetSelectedRacingCourse(ViewedCourseId);
    Message=FString::Printf(TEXT("Course %d selected. Use Save & fly in the hangar to begin."),ViewedCourseId);
    if (!bEmbedded) {
        const auto KeepAlive=AsShared();
        const FString SelectionMessage=Message;
        Subsystem->ClosePanel();
        Subsystem->OpenPanel(SelectionMessage);
        (void)KeepAlive;
    }
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::FreeFlight() {
    if (HasDraftChanges()) {
        Message=TEXT("Apply or revert gate edits before selecting free flight.");
        return FReply::Handled();
    }
    if (auto* Subsystem=Owner.Get()) {
        PendingNumberingCompaction.Reset();
        Subsystem->SetSelectedRacingCourse(0);
        Message=TEXT("Free flight selected. Use Save & fly in the hangar to begin.");
        if (!bEmbedded) {
            const auto KeepAlive=AsShared();
            const FString SelectionMessage=Message;
            Subsystem->ClosePanel();
            Subsystem->OpenPanel(SelectionMessage);
            (void)KeepAlive;
        }
    }
    return FReply::Handled();
}

FString SFactoryFPVRacePanel::BestResetBlockReason() const {
    if (!CanEditGate() || !EditingGate->IsLiveRaceGate() || !EditingGate->HasAuthority() || EditingGate->GetGateOrder()!=1)
        return TEXT("Select the live start/finish checkpoint to reset its record.");
    if (HasDraftChanges()) return TEXT("Apply or revert checkpoint edits before clearing its saved best.");
    if (ViewedCourseId!=EditingGate->GetCourseId())
        return FString::Printf(TEXT("View course %d before clearing its saved best."),EditingGate->GetCourseId());
    return {};
}

TOptional<SFactoryFPVRacePanel::FBestReset> SFactoryFPVRacePanel::FindBestForReset(FString& Error) const {
    Error=BestResetBlockReason();
    if (!Error.IsEmpty()) return {};
    auto* Gate=EditingGate.Get();
    auto* World=CurrentWorld();
    const int32 Course=Gate->GetCourseId();
    if (!UFactoryFPVRaceComponent::ValidateCourse(World,Course,Error,false)) return {};
    FString Layout=UFactoryFPVRaceComponent::LayoutSignature(World,Course);
    const double Seconds=Gate->GetBestLapSeconds(Layout);
    if (Seconds<=0.) {
        Error=TEXT("There is no saved best for this loaded layout.");
        return {};
    }
    return FBestReset{Gate,Gate->GetGateId(),MoveTemp(Layout),Seconds,Course};
}

FReply SFactoryFPVRacePanel::ResetBest() {
    FString Error;
    PendingBestReset=FindBestForReset(Error);
    Message=MoveTemp(Error);
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::ConfirmBestReset() {
    if (!PendingBestReset) return FReply::Handled();
    const auto Requested=MoveTemp(PendingBestReset.GetValue());
    PendingBestReset.Reset();
    FString Error;
    const auto Current=FindBestForReset(Error);
    if (Current && Current->Gate==Requested.Gate && Current->GateId==Requested.GateId &&
        Current->Layout==Requested.Layout && Current->Seconds==Requested.Seconds) {
        Current->Gate->ResetBestLap(Requested.Layout);
        Message=FString::Printf(TEXT("Saved best for this layout of course %d cleared."),Requested.CourseId);
    } else Message=TEXT("The course or its record changed. Review the current course before resetting its saved best.");
    RefreshCourse();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::CancelBestReset() {
    PendingBestReset.Reset();
    Message=TEXT("Saved best kept.");
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::Close() {
    const auto KeepAlive=SharedThis(this);
    if (!bEmbedded) if (auto* Subsystem=Owner.Get()) Subsystem->RequestClosePanel();
    return FReply::Handled();
}

FReply SFactoryFPVRacePanel::OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) {
    if (!bEmbedded && (Event.GetKey()==EKeys::Escape || Event.GetKey()==GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar))
        return Event.IsRepeat() ? FReply::Handled() : Close();
    return SCompoundWidget::OnKeyDown(Geometry,Event);
}
