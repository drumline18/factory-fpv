#include "FactoryFPVOSD.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/SlateFontInfo.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"

namespace {
constexpr FLinearColor RaceNavigationTint(1.f,.06f,.38f);
constexpr FLinearColor RaceExitSideTint(1.f,.55f,.08f);
constexpr FLinearColor RaceRecordTint(.25f,1.f,.65f);
constexpr double NearVerticalTargetDegrees=75.;

FString FormatRaceTime(double Seconds) {
    if (!FMath::IsFinite(Seconds) || Seconds<=0) return TEXT("--:--.--");
    const int32 Centiseconds=FMath::RoundToInt32(FMath::Min(Seconds,86400.)*100.);
    const int32 WholeSeconds=Centiseconds/100;
    return FString::Printf(TEXT("%02d:%02d.%02d"),WholeSeconds/60,WholeSeconds%60,Centiseconds%100);
}

FSlateFontInfo OsdFont(float Size) {
    static TSharedPtr<const FCompositeFont> Font=[]() -> TSharedPtr<const FCompositeFont> {
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("FactoryFPV"));
        const FString Path=Plugin ? Plugin->GetContentDir()/TEXT("Fonts/VT323-Regular.ttf") : FString();
        if (!FPaths::FileExists(Path)) return nullptr;
        return MakeShared<FCompositeFont>(FName(TEXT("OSD")),Path,EFontHinting::Monochrome,EFontLoadingPolicy::LazyLoad);
    }();
    FSlateFontInfo Result=Font ? FSlateFontInfo(Font,Size,FName(TEXT("OSD"))) :
        FCoreStyle::GetDefaultFontStyle(TEXT("Mono"),Size);
    Result.OutlineSettings=FFontOutlineSettings(2,FLinearColor::Black);
    return Result;
}

// Original vector compass, independent of font glyphs and world projection.
// The fixed block is navigation, not a pixel-aligned post-process reticle.
class SFactoryFPVNavigationArrow final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVNavigationArrow) : _Direction(FVector2D(0,-1)),_Opacity(1.f) {}
        SLATE_ATTRIBUTE(FVector2D,Direction)
        SLATE_ATTRIBUTE(float,Opacity)
    SLATE_END_ARGS()
    void Construct(const FArguments& Args) { Direction=Args._Direction;Opacity=Args._Opacity;SetVisibility(EVisibility::HitTestInvisible); }
    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(46,46); }
    virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool) const override {
        const FVector2f Center(Geometry.GetLocalSize()*.5);
        const FVector2f D(Direction.Get()),Side(-D.Y,D.X);
        const float Alpha=FMath::Clamp(Opacity.Get(),.3f,1.f);
        const FLinearColor Tint=RaceNavigationTint.CopyWithNewOpacity(Alpha)*Style.GetColorAndOpacityTint();
        const FLinearColor Outline=FLinearColor(0,0,0,Alpha)*Style.GetColorAndOpacityTint();
        TArray<FVector2f> Ring;Ring.Reserve(33);
        for (int32 Index=0;Index<=32;++Index) {
            const float Angle=static_cast<float>(Index*(2.*PI/32.));
            Ring.Add(Center+FVector2f(FMath::Sin(Angle),FMath::Cos(Angle))*20.f);
        }
        FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Ring,ESlateDrawEffect::None,Outline,true,4.f);
        FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Ring,ESlateDrawEffect::None,Tint*.5f,true,1.5f);
        const FVector2f Tip=Center+D*15.f,Base=Tip-D*10.f;
        const TArray<FVector2f> Arrow{Center-D*12.f,Tip,Base+Side*7.f,Tip,Base-Side*7.f};
        FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Arrow,ESlateDrawEffect::None,Outline,true,5.f);
        FSlateDrawElement::MakeLines(Elements,Layer+3,Geometry.ToPaintGeometry(),Arrow,ESlateDrawEffect::None,Tint,true,2.5f);
        return Layer+3;
    }
private:
    TAttribute<FVector2D> Direction;
    TAttribute<float> Opacity;
};
}

FString SFactoryFPVOSD::WarningText() const {
    if (!Data.bConnected) {
        if (FMath::Fmod(Data.DisplaySeconds,.8)>=.4) return FString();
        return Data.Receiver==TEXT("PAD") ? FString::Printf(TEXT("PAD LOST - %s RETURN"),*ReturnKey) : TEXT("RX LOSS");
    }
    if (!Data.Warning.IsEmpty()) return Data.Warning.ToUpper();
    if (Data.bDamageEnabled && Data.IntegrityPercent<=0) return TEXT("FRAME LOST - RECOVER OR EXIT");
    if (Data.bBatteryEnabled && Data.bBatteryCritical) return TEXT("SIM BAT CRITICAL - LAND");
    if (Data.bDamageEnabled && Data.IntegrityPercent<30) return TEXT("FRAME DAMAGED - LAND");
    if (Data.bBatteryEnabled && Data.bBatteryLow) return TEXT("SIM BAT LOW - LAND SOON");
    if (Data.bSetup) return TEXT("RADIO SETUP");
    return Data.bArmed ? FString() : TEXT("DISARMED");
}

FString SFactoryFPVOSD::ControlsText() const {
    if (!Data.bConnected && Data.Receiver==TEXT("PAD")) {
        return FString::Printf(TEXT("%s EXIT   RECONNECT GAMEPAD AND RELAUNCH"),*ReturnKey);
    }
    const FString Exit=FString::Printf(TEXT("%s EXIT   %s HANGAR"),*ReturnKey,*HangarKey);
    if (!Data.bConnected) return FString::Printf(TEXT("%s EXIT   RECONNECT RADIO   %s HANGAR"),*ReturnKey,*HangarKey);
    if (Data.bSetup) return Exit;
    return FString::Printf(TEXT("%s%s %s   %s RESET   %s"),Data.bRadioArmBound ? TEXT("RADIO ARM   ") : TEXT(""),
        *ArmKey,Data.bRadioArmBound ? TEXT("DISARM") : TEXT("ARM"),*RecoverKey,*Exit);
}

float SFactoryFPVOSD::FontScale() const {
    return FMath::IsFinite(Data.TextScale) ? FMath::Clamp(Data.TextScale,.7f,1.3f) : 1.f;
}
float SFactoryFPVOSD::TelemetryAlpha() const {
    return FMath::IsFinite(Data.Opacity) ? FMath::Clamp(Data.Opacity,.3f,1.f) : 1.f;
}
FString SFactoryFPVOSD::BatteryText() const {
    if (!FMath::IsFinite(Data.BatteryPercent) || !FMath::IsFinite(Data.BatteryVolts)) return TEXT("SIM BAT --");
    return FString::Printf(TEXT("SIM BAT %.0f%% %.1fV"),FMath::Clamp(Data.BatteryPercent,0.,100.),FMath::Clamp(Data.BatteryVolts,0.,1000.));
}
FString SFactoryFPVOSD::IntegrityText() const {
    if (!FMath::IsFinite(Data.IntegrityPercent)) return TEXT("FRAME --");
    return FString::Printf(TEXT("FRAME %.0f%%"),FMath::Clamp(Data.IntegrityPercent,0.,100.));
}
FString SFactoryFPVOSD::RaceText() const {
    const FString Gate=HasRaceTarget() ? FString() : FString::Printf(TEXT("  GATE %d/%d"),Data.RaceGate,Data.RaceGateCount);
    const TCHAR* Ghost=Data.bRaceGhostActive && Data.bArmed && Data.bConnected && !Data.bSetup ? TEXT("  /  GHOST") : TEXT("");
    return FString::Printf(TEXT("%s %d  /  LAP %llu  /  %s%s\nLAST %s  /  %s %s%s"),
        Data.bRacePractice ? TEXT("PRACTICE") : TEXT("COURSE"),Data.RaceCourse,
        static_cast<unsigned long long>(Data.RaceLaps+1),*FormatRaceTime(Data.RaceElapsed),*Gate,
        *FormatRaceTime(Data.RaceLast),Data.bRacePractice ? TEXT("PB") : TEXT("BEST"),*FormatRaceTime(Data.RaceBest),Ghost);
}

bool SFactoryFPVOSD::HasRaceFinish() const {
    return Data.bRacing && !Data.bSetup && Data.bArmed && Data.bConnected && Data.RaceStatus.IsEmpty() &&
        Data.RaceFinish!=EFactoryFPVRaceFinish::None && FMath::IsFinite(Data.RaceLast) && Data.RaceLast>0;
}

FString SFactoryFPVOSD::RaceStatusText() const {
    if (HasRaceFinish()) {
        const TCHAR* Label=Data.RaceFinish==EFactoryFPVRaceFinish::CourseBest ? TEXT("NEW COURSE BEST") : TEXT("LAP COMPLETE");
        return FString::Printf(TEXT("%s  %s"),Label,*FormatRaceTime(Data.RaceLast));
    }
    if (!HasRaceSplit()) return Data.RaceStatus;
    FString Text=FString::Printf(TEXT("GATE %d  /  %s"),Data.RaceSplitGate,*FormatRaceTime(Data.RaceSplitSeconds));
    if (Data.bRaceSplitHasDelta && FMath::IsFinite(Data.RaceSplitDelta)) {
        const int32 Delta=RaceSplitDeltaCentiseconds();
        const int32 Magnitude=FMath::Abs(Delta);
        const TCHAR* Sign=Delta<0 ? TEXT("-") : Delta>0 ? TEXT("+") : TEXT("");
        Text+=FString::Printf(TEXT("  /  %s%d.%02dS PB"),Sign,Magnitude/100,Magnitude%100);
    }
    return Text;
}

FLinearColor SFactoryFPVOSD::RaceStatusTint() const {
    if (HasRaceFinish()) return Data.RaceFinish==EFactoryFPVRaceFinish::CourseBest ? RaceRecordTint : FLinearColor::White;
    if (Data.bRaceWrongWay && !Data.RaceStatus.IsEmpty()) return RaceExitSideTint;
    if (HasRaceSplit()) {
        const int32 Delta=RaceSplitDeltaCentiseconds();
        if (Delta<0) return RaceRecordTint;
        if (Delta>0) return RaceExitSideTint;
    }
    return FLinearColor::White;
}

bool SFactoryFPVOSD::HasRaceSplit() const {
    return Data.bRacing && !Data.bSetup && Data.bArmed && Data.bConnected && Data.RaceStatus.IsEmpty() &&
        Data.RaceSplitGate>1 && Data.RaceSplitGate<=Data.RaceGateCount &&
        FMath::IsFinite(Data.RaceSplitSeconds) && Data.RaceSplitSeconds>0 && WarningText().IsEmpty();
}

int32 SFactoryFPVOSD::RaceSplitDeltaCentiseconds() const {
    if (!Data.bRaceSplitHasDelta || !FMath::IsFinite(Data.RaceSplitDelta)) return 0;
    const int32 Magnitude=FMath::RoundToInt32(FMath::Min(FMath::Abs(Data.RaceSplitDelta),86400.)*100.);
    return Data.RaceSplitDelta<0 ? -Magnitude : Magnitude;
}

bool SFactoryFPVOSD::HasRaceTarget() const {
    return Data.bRacing && !Data.bSetup && Data.bRaceTargetAvailable && Data.RaceGate>0 &&
        FMath::IsFinite(Data.RaceTargetBearingDegrees) && FMath::IsFinite(Data.RaceTargetElevationDegrees) &&
        FMath::IsFinite(Data.RaceTargetDistanceM) && Data.RaceTargetDistanceM>=0;
}
bool SFactoryFPVOSD::HasRaceEdgeCue() const {
    return Data.bConnected && HasRaceTarget() && !Data.RaceOffscreenDirection.ContainsNaN() && !Data.RaceOffscreenDirection.IsNearlyZero();
}

int32 SFactoryFPVOSD::OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullRect,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const {
    const int32 ContentLayer=SCompoundWidget::OnPaint(Args,Geometry,CullRect,Elements,Layer,Style,bParentEnabled);
    if (!HasRaceEdgeCue()) return ContentLayer;

    // This geometry covers the actual viewport, including space outside the scaled telemetry canvas.
    const FVector2D Size=Geometry.GetLocalSize();
    const double Scale=FMath::Min(Size.X/1920.,Size.Y/1080.);
    if (Scale<=0.) return ContentLayer;
    const FVector2D Direction=Data.RaceOffscreenDirection;
    const FVector2D Edge=Direction/FMath::Max(FMath::Abs(Direction.X),FMath::Abs(Direction.Y));
    const FVector2D Center=Size*.5;
    const double VerticalInset=Edge.Y>0. ? 51. : 34.; // Leave room for the centered shortcut footer.
    const FVector2D Position=Center+Edge*FVector2D(Center.X-46.*Scale,Center.Y-VerticalInset*Scale);
    const FVector2D Along=(Position-Center).GetSafeNormal(),Across(-Along.Y,Along.X);
    const FVector2D Tip=Position+Along*(12.*Scale),Tail=Position-Along*(5.*Scale);
    const TArray<FVector2f> Chevron{FVector2f(Tail+Across*(9.*Scale)),FVector2f(Tip),FVector2f(Tail-Across*(9.*Scale))};
    const float Alpha=TelemetryAlpha();
    const FLinearColor StyleTint=Style.GetColorAndOpacityTint();
    const FLinearColor CueTint=Data.bRaceTargetOnExitSide ? RaceExitSideTint : RaceNavigationTint;
    const auto PaintGeometry=Geometry.ToPaintGeometry();
    FSlateDrawElement::MakeLines(Elements,ContentLayer+1,PaintGeometry,Chevron,ESlateDrawEffect::None,FLinearColor(0,0,0,Alpha)*StyleTint,true,6.*Scale);
    FSlateDrawElement::MakeLines(Elements,ContentLayer+2,PaintGeometry,Chevron,ESlateDrawEffect::None,CueTint.CopyWithNewOpacity(Alpha)*StyleTint,true,3.*Scale);
    return ContentLayer+2;
}

double SFactoryFPVOSD::RaceTargetBearing() const {
    if (!FMath::IsFinite(Data.RaceTargetBearingDegrees)) return 0;
    double Bearing=FMath::Fmod(Data.RaceTargetBearingDegrees,360.);
    if (Bearing>180) Bearing-=360.;else if (Bearing< -180) Bearing+=360.;
    return Bearing;
}
FVector2D SFactoryFPVOSD::RaceTargetDirection() const {
    if (FMath::Abs(Data.RaceTargetElevationDegrees)>=NearVerticalTargetDegrees)
        return FVector2D(0,Data.RaceTargetElevationDegrees>0. ? -1. : 1.);
    const double Angle=FMath::DegreesToRadians(RaceTargetBearing());
    return FVector2D(FMath::Sin(Angle),-FMath::Cos(Angle));
}
FString SFactoryFPVOSD::RaceTargetText() const {
    if (!HasRaceTarget()) return FString();
    const FString Order=Data.RaceGateCount>=Data.RaceGate ? FString::Printf(TEXT("%d/%d"),Data.RaceGate,Data.RaceGateCount) : FString::FromInt(Data.RaceGate);
    const double Elevation=Data.RaceTargetElevationDegrees;
    const bool bBehind=FMath::Abs(Elevation)<NearVerticalTargetDegrees && FMath::Abs(RaceTargetBearing())>100.;
    const double Distance=FMath::Clamp(Data.RaceTargetDistanceM,0.,999999.);
    if (Data.bRaceTargetVerticalGate) {
        const double Height=Data.RaceTargetHeightOffsetM;
        const int32 Metres=FMath::IsFinite(Height) ? FMath::RoundToInt32(FMath::Min(FMath::Abs(Height),999999.)) : 0;
        FString Text=FString::Printf(TEXT("NEXT %s / %.0f M"),*Order,Distance);
        if (Metres>0) Text+=FString::Printf(TEXT(" / %d M %s"),Metres,Height>0. ? TEXT("ABOVE") : TEXT("BELOW"));
        if (bBehind) Text+=TEXT(" / BEHIND");
        return Text;
    }
    const TCHAR* Location=Elevation>=NearVerticalTargetDegrees ? TEXT("  /  ABOVE") :
        Elevation<=-NearVerticalTargetDegrees ? TEXT("  /  BELOW") : (bBehind ? TEXT("  /  BEHIND") : TEXT(""));
    return FString::Printf(TEXT("NEXT %s  /  %.0f M%s"),*Order,Distance,Location);
}
FString SFactoryFPVOSD::RaceTargetGuidance() const {
    if (!HasRaceTarget()) return FString();
    FString Text=Data.RaceTargetInstruction.Replace(TEXT("\r"),TEXT(" ")).Replace(TEXT("\n"),TEXT(" ")).Replace(TEXT("\t"),TEXT(" ")).TrimStartAndEnd().Left(48).ToUpper();
    // Entry-side guidance and vertical crossing directions describe the gate,
    // independent of camera tilt and the apparent elevation of its opening.
    if (Data.bRaceTargetOnExitSide || Data.bRaceTargetVerticalGate ||
        FMath::Abs(Data.RaceTargetElevationDegrees)>=NearVerticalTargetDegrees) return Text;
    const double Elevation=FMath::Clamp(Data.RaceTargetElevationDegrees,-90.,90.);
    const TCHAR* Vertical=Elevation>=8 ? TEXT("ABOVE") : Elevation<=-8 ? TEXT("BELOW") : TEXT("");
    if (*Vertical) { if (!Text.IsEmpty()) Text+=TEXT("  /  ");Text+=Vertical; }
    return Text;
}

void SFactoryFPVOSD::SetTelemetry(const FFactoryFPVOSDTelemetry& Value) {
    const bool bLayoutChanged=!(Data.Layout==Value.Layout);
    const bool bHadRaceEdgeCue=HasRaceEdgeCue();
    const bool bEdgeAppearanceChanged=Data.RaceOffscreenDirection!=Value.RaceOffscreenDirection ||
        Data.bRaceTargetOnExitSide!=Value.bRaceTargetOnExitSide || Data.Opacity!=Value.Opacity;
    Data=Value;
    if (bLayoutChanged && LayoutCanvas) LayoutCanvas->Invalidate(EInvalidateWidgetReason::Layout);
    const bool bHasRaceEdgeCue=HasRaceEdgeCue();
    if (bHadRaceEdgeCue!=bHasRaceEdgeCue || (bHasRaceEdgeCue && bEdgeAppearanceChanged)) Invalidate(EInvalidateWidgetReason::Paint);
}

void SFactoryFPVOSD::Construct(const FArguments& Args) {
    HangarKey=FFactoryFPVKeyboardShortcuts::KeyLabel(Args._Shortcuts.Hangar);
    ArmKey=FFactoryFPVKeyboardShortcuts::KeyLabel(Args._Shortcuts.Arm);
    RecoverKey=FFactoryFPVKeyboardShortcuts::KeyLabel(Args._Shortcuts.Recover);
    ReturnKey=FFactoryFPVKeyboardShortcuts::KeyLabel(Args._Shortcuts.Return);
    auto Readout=[this](TFunction<FString()> Value,float Size=34.f,bool bSafety=false) -> TSharedRef<STextBlock> {
        return SNew(STextBlock).Font_Lambda([this,Size] { return OsdFont(Size*FontScale()); })
            .ColorAndOpacity_Lambda([this,bSafety] { return FSlateColor(FLinearColor(1,1,1,bSafety ? 1.f : TelemetryAlpha())); })
            .Text_Lambda([Value=MoveTemp(Value)] { return FText::FromString(Value()); });
    };
    auto Optional=[](const TSharedRef<STextBlock>& Text,TFunction<bool()> IsShown) -> TSharedRef<STextBlock> {
        Text->SetVisibility(TAttribute<EVisibility>::CreateLambda([IsShown=MoveTemp(IsShown)] { return IsShown() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; }));
        return Text;
    };
    auto MovableSlot=[this](EFactoryFPVOsdItem Item) {
        auto Slot=SConstraintCanvas::Slot();
        Slot.AutoSize(true)
            .Offset_Lambda([this,Item] {
                const FVector2D Anchor=FFactoryFPVOsdLayout::Anchor(Data.Layout.GetSlot(Item));
                return FMargin(static_cast<float>(Anchor.X),static_cast<float>(Anchor.Y),0,0);
            })
            .Alignment_Lambda([this,Item] { return FFactoryFPVOsdLayout::Alignment(Data.Layout.GetSlot(Item)); });
        return Slot;
    };
    ChildSlot
    [
        SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
        [
            SNew(SBox).WidthOverride(1920.f).HeightOverride(1080.f)
            [
                SAssignNew(LayoutCanvas,SConstraintCanvas)
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(96,60,0,0)).Alignment(FVector2D(0,0))
                [ (ReceiverReadout=Readout([this] { return FString::Printf(TEXT("RX %s"),Data.bConnected ? *Data.Receiver : TEXT("--")); },34,true)).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Timer)
                [ (TimerReadout=Optional(Readout([this] {
                    const int Seconds=FMath::FloorToInt(FMath::Clamp(Data.ArmedSeconds,0.,86400.));
                    return FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60);
                }),[this] { return Data.bShowTimer; })).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Home)
                [ (HomeReadout=Optional(Readout([this] { return FString::Printf(TEXT("HOME %.0fM"),FMath::Clamp(Data.HomeDistanceM,0.,999999.)); }),[this] { return Data.bShowHome; })).ToSharedRef() ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(96,240,0,0)).Alignment(FVector2D(0,0))
                [ SNew(SBox).WidthOverride(720).Visibility_Lambda([this] { return Data.bRacing ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                    [ SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Left)
                            [ (RaceReadout=Readout([this] { return RaceText(); },22)).ToSharedRef() ] ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                        [ SAssignNew(RaceStatusReadout,STextBlock).Font_Lambda([this] { return OsdFont(21*FontScale()); })
                            .ColorAndOpacity_Lambda([this] { return FSlateColor(RaceStatusTint()); })
                            .AutoWrapText(true).Text_Lambda([this] { return FText::FromString(RaceStatusText()); }) ] ] ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(1824,240,0,0)).Alignment(FVector2D(1,0))
                [ (RaceNavigationRoot=SNew(SBox).WidthOverride(730).Clipping(EWidgetClipping::ClipToBounds)
                    .Visibility_Lambda([this] { return HasRaceTarget() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1)
                        [ SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight()
                            [ SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Right)
                                [ (RaceTargetReadout=Readout([this] { return RaceTargetText(); },29)).ToSharedRef() ] ]
                            + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,0)
                            [ SNew(SScaleBox).Stretch(EStretch::ScaleToFitX).StretchDirection(EStretchDirection::DownOnly).HAlign(HAlign_Right)
                                [ (RaceTargetInstructionReadout=Readout([this] { return RaceTargetGuidance(); },24)).ToSharedRef() ] ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(12,2,0,0)
                        [ (RaceNavigationArrow=SNew(SFactoryFPVNavigationArrow)
                            .Direction_Lambda([this] { return RaceTargetDirection(); }).Opacity_Lambda([this] { return TelemetryAlpha(); })).ToSharedRef() ]
                    ]).ToSharedRef() ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(960,540,0,0)).Alignment(FVector2D(.5,.5))
                [ (CrosshairReadout=Optional(Readout([] { return FString(TEXT("+")); },40),[this] { return Data.bShowCrosshair; })).ToSharedRef() ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(960,995,0,0)).Alignment(FVector2D(.5,1))
                [
                    SNew(SBox).WidthOverride(890)
                    [ SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,4)
                        [ Optional(Readout([this] { return Data.Debug; },22),[this] { return !Data.Debug.IsEmpty(); }) ]
                        + SVerticalBox::Slot().AutoHeight()
                        [ SAssignNew(WarningReadout,STextBlock).Text_Lambda([this] { return FText::FromString(WarningText()); })
                            .Font_Lambda([this] { return OsdFont(33*FontScale()); }).ColorAndOpacity(FLinearColor::White)
                            .Justification(ETextJustify::Center).AutoWrapText(true) ] ]
                ]
                + MovableSlot(EFactoryFPVOsdItem::Battery)
                [ (BatteryReadout=Optional(Readout([this] { return BatteryText(); },30),[this] { return Data.bBatteryEnabled; })).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Integrity)
                [ (IntegrityReadout=Optional(Readout([this] { return IntegrityText(); },30),[this] { return Data.bDamageEnabled; })).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Speed)
                [ (SpeedReadout=Optional(Readout([this] { return FString::Printf(TEXT("%.0f KM/H"),FMath::Clamp(Data.SpeedMps*3.6,0.,9999.)); }),[this] { return Data.bShowSpeed; })).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Altitude)
                [ (AltitudeReadout=Optional(Readout([this] { return FString::Printf(TEXT("ALT %+.0fM"),FMath::Clamp(Data.RelativeAltitudeM,-99999.,99999.)); }),[this] { return Data.bShowAltitude; })).ToSharedRef() ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(96,976,0,0)).Alignment(FVector2D(0,0))
                [ (ModeReadout=Readout([this] { return FString(Data.bSetup ? TEXT("SETUP") : (Data.bAngleMode ? TEXT("ANGLE") : TEXT("ACRO"))); })).ToSharedRef() ]
                + MovableSlot(EFactoryFPVOsdItem::Throttle)
                [ (ThrottleReadout=Optional(Readout([this] { return FString::Printf(TEXT("T %.0f%%"),FMath::Clamp(Data.Throttle*100.,0.,100.)); }),[this] { return Data.bShowThrottle; })).ToSharedRef() ]
                + SConstraintCanvas::Slot().AutoSize(true).Offset(FMargin(960,1045,0,0)).Alignment(FVector2D(.5,0))
                [
                    SNew(SBox).Visibility_Lambda([this] {
                        return !Data.bArmed || Data.ArmedSeconds<3 ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
                    })
                    [ Readout([this] { return ControlsText(); },21) ]
                ]
            ]
        ]
    ];
    RaceTargetReadout->SetColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] { return FSlateColor(RaceNavigationTint.CopyWithNewOpacity(TelemetryAlpha())); }));
    RaceTargetInstructionReadout->SetColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this] {
        const FLinearColor Tint=Data.bRaceTargetOnExitSide ? RaceExitSideTint : FLinearColor::White;
        return FSlateColor(Tint.CopyWithNewOpacity(TelemetryAlpha()));
    }));
    SetVisibility(EVisibility::HitTestInvisible);
}
