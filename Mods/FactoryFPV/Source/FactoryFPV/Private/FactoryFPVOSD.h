#pragma once
#include "CoreMinimal.h"
#include "FactoryFPVOsdLayout.h"
#include "FactoryFPVSettings.h"
#include "Widgets/SCompoundWidget.h"

class SConstraintCanvas;
class STextBlock;

enum class EFactoryFPVRaceFinish : uint8 { None,LapComplete,CourseBest };

struct FFactoryFPVOSDTelemetry {
    bool bArmed=false;
    bool bConnected=true;
    bool bSetup=false;
    bool bRadioArmBound=false;
    bool bAngleMode=false;
    bool bShowCrosshair=true;
    bool bShowSpeed=true;
    bool bShowAltitude=true;
    bool bShowHome=true;
    bool bShowTimer=true;
    bool bShowThrottle=true;
    float TextScale=1.f;
    float Opacity=1.f;
    FFactoryFPVOsdLayout Layout;
    // Simulated drone pack only. Never display the transmitter's battery here.
    bool bBatteryEnabled=false;
    double BatteryPercent=100;
    double BatteryVolts=0;
    bool bBatteryLow=false;
    bool bBatteryCritical=false;
    bool bDamageEnabled=false;
    double IntegrityPercent=100;
    bool bRacing=false;
    bool bRacePractice=false;
    bool bRaceGhostActive=false;
    int32 RaceCourse=0,RaceGate=0,RaceGateCount=0;
    uint64 RaceLaps=0;
    double RaceElapsed=0,RaceLast=0,RaceBest=0;
    FString RaceStatus;
    bool bRaceWrongWay=false;
    EFactoryFPVRaceFinish RaceFinish=EFactoryFPVRaceFinish::None;
    int32 RaceSplitGate=0;
    double RaceSplitSeconds=0,RaceSplitDelta=0;
    bool bRaceSplitHasDelta=false;
    // Bearing is camera-relative: zero ahead/up, +90 right, +/-180 behind/down.
    // Side is relative to the checkpoint plane. Slate reads no UObjects.
    bool bRaceTargetAvailable=false;
    bool bRaceTargetOnExitSide=false;
    bool bRaceTargetVerticalGate=false;
    double RaceTargetBearingDegrees=0,RaceTargetElevationDegrees=0,RaceTargetDistanceM=0;
    double RaceTargetHeightOffsetM=0; // World height of a vertical opening relative to the drone.
    FVector2D RaceOffscreenDirection=FVector2D::ZeroVector; // Screen axes: right and down. Zero when visible or unavailable.
    FString RaceTargetInstruction;
    double Throttle=0;
    double SpeedMps=0;
    double RelativeAltitudeM=0;
    double HomeDistanceM=0;
    double ArmedSeconds=0;
    double DisplaySeconds=0;
    FString Receiver=TEXT("USB");
    FString Warning;
    FString Debug;
};

class SFactoryFPVOSD final : public SCompoundWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVOSD) {}
        SLATE_ARGUMENT(FFactoryFPVKeyboardShortcuts, Shortcuts)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args);
    void SetTelemetry(const FFactoryFPVOSDTelemetry& Value);
private:
    friend class FFactoryFPVOSDPreferencesTest;
    friend class FFactoryFPVOSDRenderTest;
    friend class FFactoryFPVShippingSettingsQA;
    friend class FFactoryFPVShippingVerticalFlight;
    int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullRect,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override;
    FFactoryFPVOSDTelemetry Data;
    TSharedPtr<SConstraintCanvas> LayoutCanvas;
    FString HangarKey,ArmKey,RecoverKey,ReturnKey;
    TSharedPtr<STextBlock> CrosshairReadout,SpeedReadout,AltitudeReadout,HomeReadout,TimerReadout,ThrottleReadout;
    TSharedPtr<STextBlock> ReceiverReadout,ModeReadout,WarningReadout,BatteryReadout,IntegrityReadout,RaceReadout,RaceStatusReadout;
    TSharedPtr<SWidget> RaceNavigationRoot,RaceNavigationArrow;
    TSharedPtr<STextBlock> RaceTargetReadout,RaceTargetInstructionReadout;
    float FontScale() const;
    float TelemetryAlpha() const;
    FString WarningText() const;
    FString ControlsText() const;
    FString BatteryText() const;
    FString IntegrityText() const;
    FString RaceText() const;
    FString RaceStatusText() const;
    FLinearColor RaceStatusTint() const;
    bool HasRaceFinish() const;
    bool HasRaceSplit() const;
    int32 RaceSplitDeltaCentiseconds() const;
    bool HasRaceTarget() const;
    bool HasRaceEdgeCue() const;
    double RaceTargetBearing() const;
    FVector2D RaceTargetDirection() const;
    FString RaceTargetText() const;
    FString RaceTargetGuidance() const;
};
