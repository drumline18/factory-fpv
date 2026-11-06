#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"
#include "UObject/StrongObjectPtr.h"
#include "FactoryFPVFlightCueAudioComponent.h"
#include "FactoryFPVPropAudioComponent.h"
#include "FactoryFPVSettings.h"
#include "RadioCalibration.h"
#include "WindowsRadio.h"

class APlayerController;
class UFactoryFPVLocalPlayerSubsystem;
class UWorld;
class UTexture2D;
class AFactoryFPVPioneerAnchor;
class SButton;
class SWidgetSwitcher;
class SFactoryFPVRacePanel;
class SFactoryFPVOSD;

struct FFactoryFPVDeviceOption {
    FString Id;
    FString Name;
};

struct FFactoryFPVTuningOption {
    FFactoryFPVTuningPreset Preset;
    bool bBuiltIn=false;
};

class SFactoryFPVPanel final : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SFactoryFPVPanel) {}
        SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Controller)
        SLATE_ARGUMENT(TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem>, Owner)
        SLATE_ARGUMENT(FString, InitialMessage)
        SLATE_ARGUMENT(FName, RetryInputMode)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    virtual ~SFactoryFPVPanel() override;
    virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override;
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    bool HasPendingEdits() const;
private:
    friend class UFactoryFPVLocalPlayerSubsystem;
    friend class FFactoryFPVShippingSettingsQA;
    friend class FFactoryFPVCalibrationWorkflowTest;
    friend class FFactoryFPVHangarTabsTest;
    friend class FFactoryFPVHangarRenderTest;
    friend class FFactoryFPVRadioCapabilityTest;
    friend class FFactoryFPVGateDraftLaunchTest;
    enum class EPage : uint8 { Radio, Flight, Presets, Presentation, Simulation, Racing, Count };
    enum class ERadioControl : uint8 { Arm, Return, Recover, Mode };
    TSharedRef<SWidget> BuildRadioPage();
    TSharedRef<SWidget> BuildFlightPage();
    TSharedRef<SWidget> BuildRateTuning();
    TSharedRef<SWidget> BuildPresetsPage();
    TSharedRef<SWidget> BuildPresentationPage();
    TSharedRef<SWidget> BuildOsdLayoutEditor();
    void UpdateOsdPreview();
    FReply ToggleAudioPreview(bool bFlightCue);
    EActiveTimerReturnType TickAudioPreview(double CurrentTime,float DeltaTime);
    bool CanPreviewAudio() const;
    void StopAudioPreview();
    void AudioPreviewActivationChanged(bool bActive);
    void AudioPreviewWorldTearingDown(UWorld* World);
    TSharedRef<SWidget> BuildSimulationPage();
    TSharedRef<SWidget> BuildRacingPage();
    FText FlightSelectionText() const;
    TSharedRef<SWidget> TabButton(EPage Page, const FString& Title, const FString& Help);
    TSharedRef<SWidget> BindingRow(ERadioControl ControlId, const FString& Title);
    FFactoryFPVArmBinding& ControlBinding(ERadioControl ControlId);
    FText BindingStatus(ERadioControl ControlId) const;
    FReply CaptureRadioControl(ERadioControl ControlId);
    FReply ClearRadioControl(ERadioControl ControlId);
    bool ValidateBindings();
    bool RadioReady(FString& Error) const;
    FText RadioPreflightText() const;
    bool FlightChannelsAvailable(FString& Error) const;
    FName SelectedInputMode() const;
    bool ValidateFlightInput();
    bool ValidateSelectedCourse();
    bool AxisAvailable(int Index) const;
    FReply SelectPage(EPage Page);
    bool HasUnsavedChanges() const;
    FText DraftStatus() const;
    TSharedRef<SWidget> SettingsRow(const FString& Label, const FString& Help, float* Value,
        float Minimum, float Maximum, float Step);
    TSharedRef<SWidget> InputRow(int Index);
    void RefreshDevices();
    void UpdateDeviceOptions(const std::vector<factoryfpv::RadioDevice>& Detected);
    void ChooseDevice(TSharedPtr<FFactoryFPVDeviceOption> Device, ESelectInfo::Type);
    void OpenPreview();
    void ResetRadioPreview();
    FReply BeginCalibration();
    FReply CaptureAxis();
    FReply RetryAxis();
    FReply CancelCalibration();
    FReply CaptureArmControl();
    FReply CancelArmControl();
    FText ArmStatus() const;
    void RefreshTuningOptions();
    FReply ApplySelectedTuning();
    FReply SaveTuning();
    FReply DeleteTuning();
    FReply RecoverPioneer();
    AFactoryFPVPioneerAnchor* PendingRecovery() const;
    TSharedRef<SWidget> ToggleRow(const FString& Label,bool* Value);
    FReply SaveSettings();
    bool CanFly() const;
    FReply Fly();
    FReply FlyCourse(int32 CourseId,bool bPractice);
    FReply Close();
    FText Guidance() const;
    FText ConnectionText() const;
    FFactoryFPVRadioAxis Axis(int Index) const;
    double NormalizedAxis(int Index) const;
    void SetReverse(int Index, bool Reverse);
    void Commit();
    bool Calibrating() const { return Wizard.stage >= 0 && Wizard.stage < 4; }

    TWeakObjectPtr<APlayerController> Controller;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Owner;
    TStrongObjectPtr<UFactoryFPVSettings> Draft;
    TStrongObjectPtr<UFactoryFPVSettings> SavedDraft;
    TStrongObjectPtr<UTexture2D> ScoutPreviewTexture;
    FSlateBrush ScoutPreviewBrush;
    EPage ActivePage=EPage::Radio;
    TSharedPtr<SFactoryFPVOSD> OsdPreview;
    EFactoryFPVOsdItem OsdLayoutItem=EFactoryFPVOsdItem::Speed;
    bool bOsdPreviewRace=false;
    bool bOsdPreviewWarning=true;
    TStrongObjectPtr<UFactoryFPVPropAudioComponent> PropPreview;
    TStrongObjectPtr<UFactoryFPVFlightCueAudioComponent> CuePreview;
    int32 CuePreviewOption=0;
    TWeakPtr<FActiveTimerHandle> AudioPreviewTimer;
    FDelegateHandle AudioPreviewActivationHandle,AudioPreviewWorldHandle;
    double PropPreviewStartedAt=0.;
    FString AudioPreviewMessage;
    TSharedPtr<SWidgetSwitcher> PageSwitcher;
    TSharedPtr<SFactoryFPVRacePanel> RacingPanel;
    TArray<TSharedPtr<SButton>> PageButtons;
    TArray<TSharedPtr<FFactoryFPVDeviceOption>> Devices;
    TSharedPtr<FFactoryFPVDeviceOption> SelectedDevice;
    TSharedPtr<SComboBox<TSharedPtr<FFactoryFPVDeviceOption>>> DeviceCombo;
    TArray<TSharedPtr<FString>> InputModes;
    TSharedPtr<FString> InputMode;
    TSharedPtr<SComboBox<TSharedPtr<FString>>> InputModeCombo;
    TArray<TSharedPtr<FFactoryFPVTuningOption>> TuningOptions;
    TSharedPtr<FFactoryFPVTuningOption> SelectedTuning;
    TSharedPtr<SComboBox<TSharedPtr<FFactoryFPVTuningOption>>> TuningCombo;
    FString TuningName=TEXT("My tune");
    std::unique_ptr<factoryfpv::WindowsRadio> Radio;
    factoryfpv::RadioCalibration Wizard;
    std::array<long, 8> Raw{};
    std::array<factoryfpv::RadioAxisInfo,8> RadioAxes{};
    std::array<bool,128> Buttons{},ArmOffButtons{};
    std::array<bool,128> RadioButtonsPresent{};
    std::array<long,8> ArmOffAxes{};
    int32 ArmCaptureStage=-1;
    ERadioControl CaptureTarget=ERadioControl::Arm;
    bool bConnected = false;
    bool bScannedRadioDevices=false;
    double NextRadioScan=0.;
    FString RadioPreviewError;
    FString Message;
};
