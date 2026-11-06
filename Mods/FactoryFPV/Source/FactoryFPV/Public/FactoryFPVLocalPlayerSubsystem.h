#pragma once
#include "CoreMinimal.h"
#include "FactoryFPVInputLease.h"
#include "FactoryFPVRaceDebrief.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Tickable.h"
#include "FactoryFPVLocalPlayerSubsystem.generated.h"

class APlayerController;
class UWorld;
class UInputComponent;
class UGameViewportClient;
class SFactoryFPVPanel;
class SFactoryFPVRacePanel;
class AFactoryFPVRaceGate;
class SOverlay;
class SWidget;
struct EVisibility;
enum class EFactoryFPVReturnStatus : uint8;

UCLASS()
class FACTORYFPV_API UFactoryFPVLocalPlayerSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return !IsTemplate(); }
    virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
    virtual TStatId GetStatId() const override;
    void TogglePanel();
    void OpenPanel(const FString& Message = FString(), FName RetryInputMode = NAME_None);
    void OpenRaceGatePanel(AFactoryFPVRaceGate* Gate);
    void RequestClosePanel();
    // Forced teardown, or a completed action that has already resolved edits.
    void ClosePanel();
    bool IsPanelOpen() const { return Panel.IsValid() || RacePanel.IsValid(); }
    bool IsHangarOpen() const { return Panel.IsValid(); }
    int32 GetSelectedRacingCourse() const;
    const FString& GetSelectedRacingLayout() const { return SelectedRacingLayout; }
    void SetSelectedRacingCourse(int32 CourseId);
    const FFactoryFPVRaceDebrief* GetLastRaceDebrief() const;
    void StoreRaceDebrief(UWorld* RaceWorld,const FFactoryFPVRaceDebrief& Debrief);
    int32 GetRaceBuildCourse() const;
    int32 GetNextRaceBuildOrder() const;
    bool SetRaceBuildCourse(int32 CourseId);
    int32 StartNewRaceBuildCourse();
private:
    friend class SFactoryFPVPanel;
    friend class FFactoryFPVShippingSettingsQA;
    friend class FFactoryFPVLifecycleInputTest;
    friend class FFactoryFPVRacePanelTest;
    friend class FFactoryFPVGateDraftLaunchTest;
    friend class FFactoryFPVReturnStatusTest;
    enum class EPanelDeparture : uint8 { Close, OpenHangar, OpenGate };
    struct FPanelDeparture {
        EPanelDeparture Destination=EPanelDeparture::Close;
        TWeakObjectPtr<AFactoryFPVRaceGate> Gate;
        FString Message;
        FName RetryInputMode=NAME_None;
    };
    bool BindController(APlayerController* Candidate, UGameViewportClient* CandidateViewport);
    void RefreshHangarShortcut();
    void UnbindController();
    bool CanOpenPanel() const;
    bool CanPreviewHangarAudio(const SFactoryFPVPanel* Requester) const;
    bool CanOpenRaceGate(AFactoryFPVRaceGate* Gate) const;
    bool CanLeavePanel(FPanelDeparture Departure);
    void ResolvePanelDeparture(bool bDiscard);
    void ShowPanel(TSharedRef<SWidget> Widget);
    void FocusPanelWidget(TSharedRef<SWidget> Widget) const;
    void RenewPanelInputLease();
    bool NativeUIOwnsInput() const;
    EFactoryFPVReturnStatus PendingReturnStatus() const;
    EVisibility ReturnStatusVisibility() const;
    void UpdateReturnStatus();
    void ClearReturnStatus();
    FFactoryFPVInputLease InputLease;
    UPROPERTY() TObjectPtr<UInputComponent> Controls;
    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    TSharedPtr<SFactoryFPVPanel> Panel;
    TSharedPtr<SFactoryFPVRacePanel> RacePanel;
    TSharedPtr<SOverlay> PanelLayers;
    TSharedPtr<SWidget> DiscardDialog;
    TOptional<FPanelDeparture> PendingDeparture;
    TSharedPtr<SWidget> Overlay;
    TSharedPtr<SWidget> Hint;
    TSharedPtr<SWidget> ReturnStatusOverlay;
    double HintUntil = 0;
    double NextRacingRecipeCheck = 0;
    bool bPreviousCursor = false;
    TWeakObjectPtr<UWorld> SelectedRacingWorld;
    int32 SelectedRacingCourse=0;
    FString SelectedRacingLayout;
    TWeakObjectPtr<UWorld> RaceDebriefWorld;
    TOptional<FFactoryFPVRaceDebrief> LastRaceDebrief;
    TWeakObjectPtr<UWorld> RaceBuildWorld;
    int32 RaceBuildCourse=0;
};
