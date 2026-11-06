#pragma once
#include "CoreMinimal.h"
#include "FactoryFPVRaceGate.h"
#include "Widgets/SCompoundWidget.h"

class UFactoryFPVLocalPlayerSubsystem;
class UWorld;
class SButton;
class SBox;
class SEditableTextBox;
class SFactoryFPVRaceRoutePreview;
class SVerticalBox;
class SWidgetSwitcher;
template <typename OptionType> class SComboBox;

DECLARE_DELEGATE_RetVal_TwoParams(FReply,FOnFactoryFPVRaceLaunch,int32,bool);
DECLARE_DELEGATE_OneParam(FOnFactoryFPVRaceGhostChanged,bool);

class SFactoryFPVRacePanel final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVRacePanel) : _Embedded(false) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem>, Owner)
        SLATE_ARGUMENT(TWeakObjectPtr<AFactoryFPVRaceGate>, Gate)
        SLATE_ARGUMENT(bool, Embedded)
        SLATE_ATTRIBUTE(float, AirframeDiameterCm)
        SLATE_ATTRIBUTE(bool, CanLaunchRace)
        SLATE_EVENT(FOnFactoryFPVRaceLaunch, OnRaceLaunch)
        SLATE_ATTRIBUTE(bool, ShowRaceGhost)
        SLATE_EVENT(FOnFactoryFPVRaceGhostChanged, OnShowRaceGhostChanged)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual void Tick(const FGeometry& Geometry,double Time,float DeltaTime) override;
    virtual bool SupportsKeyboardFocus() const override { return !bEmbedded; }
    virtual FReply OnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
    bool HasPendingGateEdits() const { return HasDraftChanges(); }
private:
    friend class FFactoryFPVRacePanelTest;
    struct FBestReset {
        TWeakObjectPtr<AFactoryFPVRaceGate> Gate;
        FGuid GateId;
        FString Layout;
        double Seconds=0.;
        int32 CourseId=0;
    };
    UWorld* CurrentWorld() const;
    bool CanEditGate() const;
    bool HasDraftChanges() const;
    void EditGate(AFactoryFPVRaceGate* Gate);
    void RefreshCourse();
    void RefreshClearanceAdvisories();
    void RefreshCoursePicker();
    void SelectViewedCourse(int32 CourseId);
    void RefreshCourseNameInput();
    void RefreshRoutePreview(bool bLayoutValid);
    bool IsShowingRoute() const;
    bool IsShowingDebrief() const;
    FString CourseLabel(int32 CourseId) const;
    FText CourseOptionLabel(int32 CourseId) const;
    bool IsKnownCourse(int32 CourseId) const;
    bool CanEditCourseName() const;
    bool HasCourseNameEdit() const;
    FReply ApplyCourseName();
    TSharedRef<SWidget> BuildCourseBrowser();
    TSharedRef<SWidget> BuildRaceDebrief() const;
    TSharedRef<SWidget> BuildCourseTools();
    TSharedRef<SWidget> BuildNumberingRepair();
    TSharedRef<SWidget> BuildGateEditor();
    FReply CloseNumberingGaps();
    FReply ToggleRaceDebrief();
    FReply SetBuildCourse(int32 CourseId);
    FReply StartNewBuildCourse();
    FReply ConfirmNumberingCompaction();
    FReply CancelNumberingCompaction();
    FReply ApplyGate();
    FReply ConfirmOrderSwap();
    FReply CancelOrderSwap();
    FReply RevertGate();
    void GateEditsSaved(const TCHAR* Feedback);
    bool ValidateViewedCourse();
    FReply LaunchViewedCourse(bool bPractice);
    FReply RaceCourse();
    FReply FreeFlight();
    FReply ResetBest();
    FReply ConfirmBestReset();
    FReply CancelBestReset();
    FString BestResetBlockReason() const;
    TOptional<FBestReset> FindBestForReset(FString& Error) const;
    FReply Close();
    FText GateHeading() const;
    FText CheckpointInstructions() const;
    FReply SelectPoleSide(int32 Side);
    FReply SelectRaceDirection(EFactoryFPVRaceGateDirection Direction);
    FText SelectedMode() const;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Owner;
    TAttribute<float> AirframeDiameterCm;
    TAttribute<bool> CanLaunchRace;
    FOnFactoryFPVRaceLaunch OnRaceLaunch;
    TAttribute<bool> ShowRaceGhost;
    FOnFactoryFPVRaceGhostChanged OnShowRaceGhostChanged;
    TWeakObjectPtr<AFactoryFPVRaceGate> EditingGate;
    TArray<TWeakObjectPtr<AFactoryFPVRaceGate>> LoadedGates;
    TMap<int32,int32> LoadedCourseCounts;
    TMap<int32,FString> CourseNames;
    TArray<TSharedPtr<int32>> CourseOptions;
    TSharedPtr<SComboBox<TSharedPtr<int32>>> CoursePicker;
    TSharedPtr<SEditableTextBox> CourseNameInput;
    TSharedPtr<SVerticalBox> GateRows;
    TSharedPtr<SWidgetSwitcher> CourseView;
    TSharedPtr<SWidgetSwitcher> RacePage;
    TSharedPtr<SBox> DebriefContent;
    TSharedPtr<SFactoryFPVRaceRoutePreview> RoutePreview;
    TSharedPtr<SButton> ApplyButton;
    TSharedPtr<SButton> RaceButton;
    int32 DraftCourseId=1;
    int32 DraftGateOrder=1;
    int32 DraftPolePassSide=1;
    EFactoryFPVRaceGateDirection DraftDirection{};
    int32 ViewedCourseId=0;
    int32 BuildCourseId=0;
    int32 NameInputCourseId=0;
    bool bCourseNamesAvailable=false;
    bool bEmbedded=false;
    bool bHadGate=false;
    bool bCourseReady=false;
    bool bCanCloseNumberingGaps=false;
    double NextRefresh=0;
    double ViewedBestSeconds=0.;
    TOptional<FBestReset> PendingBestReset;
    TOptional<AFactoryFPVRaceGate::FEditorOrderSwap> PendingOrderSwap;
    TOptional<AFactoryFPVRaceGate::FEditorNumberingCompaction> PendingNumberingCompaction;
    FText NumberingPreview;
    FString ViewedLayout;
    FString NameInputBaseline;
    FString CourseStatus;
    FString BuildCourseStatus;
    FString CourseClearanceAdvisory;
    FString CheckpointClearanceAdvisory;
    FString BestLapStatus;
    FString GhostStatus;
    FString RouteSummary;
    FString Message;
};
