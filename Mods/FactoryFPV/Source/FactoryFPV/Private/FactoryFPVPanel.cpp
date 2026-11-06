#include "FactoryFPVPanel.h"
#include "Styling/CoreStyle.h"
#include "FactoryFPVAudioSettings.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVPioneerAnchor.h"
#include "AudioDeviceHandle.h"
#include "GameFramework/Character.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVInteractionState.h"
#include "FactoryFPVOSD.h"
#include "FactoryFPVRacePanel.h"
#include "FactoryFPVRaceComponent.h"
#include "FactoryFPVReturnStatusWidget.h"
#include "FlightCore.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "UObject/UnrealType.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

namespace {
const FLinearColor Background(0.025f, 0.032f, 0.042f);
const FLinearColor Card(0.055f, 0.067f, 0.082f);
const FLinearColor Orange(1.f, 0.42f, 0.08f);
const FLinearColor Cyan(0.15f, 0.82f, 0.85f);
const FLinearColor Muted(0.57f, 0.63f, 0.7f);
const FLinearColor RateColors[]{Orange,Cyan,FLinearColor(.78f,.68f,1.f)};
constexpr float MinimumRate=120.f;
constexpr float MaximumRate=1200.f;
constexpr float MinimumCenterRate=10.f;
constexpr double PropPreviewSpindownAt=1.6;
constexpr double PropPreviewEndsAt=2.2;
constexpr struct {
    EFactoryFPVFlightCue Cue;
    const TCHAR* Label;
} FlightCueOptions[]{
    {EFactoryFPVFlightCue::Checkpoint,TEXT("Checkpoint")},
    {EFactoryFPVFlightCue::Lap,TEXT("Lap complete")},
    {EFactoryFPVFlightCue::PersonalBest,TEXT("Personal best")},
    {EFactoryFPVFlightCue::ReceiverLost,TEXT("Receiver lost")},
};
constexpr double RadioScanInterval=1.;
const TCHAR* Controls[] = {TEXT("ROLL"), TEXT("PITCH"), TEXT("YAW"), TEXT("THROTTLE")};
const TCHAR* AxisNames[] = {TEXT("X"),TEXT("Y"),TEXT("Z"),TEXT("Rx"),TEXT("Ry"),TEXT("Rz"),TEXT("Slider 0"),TEXT("Slider 1")};
const TCHAR* Directions[] = {
    TEXT("Move the RIGHT stick fully LEFT and RIGHT. Hold it RIGHT, then press Capture."),
    TEXT("Center roll. Move the RIGHT stick fully FORWARD and BACK. Hold it BACK toward you, then press Capture."),
    TEXT("Center the right stick. Move the LEFT stick fully LEFT and RIGHT. Hold it RIGHT, then press Capture."),
    TEXT("Move the LEFT stick fully DOWN and UP. Hold it fully UP, then press Capture.")
};
using FShortcutKeys=FFactoryFPVKeyboardShortcuts;
struct FShortcutControl {
    const TCHAR* Label;
    const TCHAR* Tag;
    FKey FShortcutKeys::*Key;
};
constexpr FShortcutControl ShortcutControls[]{
    {TEXT("Hangar"),TEXT("FactoryFPVShortcutHangar"),&FShortcutKeys::Hangar},
    {TEXT("Arm / disarm"),TEXT("FactoryFPVShortcutArm"),&FShortcutKeys::Arm},
    {TEXT("Recover drone"),TEXT("FactoryFPVShortcutRecover"),&FShortcutKeys::Recover},
    {TEXT("Return to pioneer"),TEXT("FactoryFPVShortcutReturn"),&FShortcutKeys::Return}
};
TSharedRef<STextBlock> Label(const FString& Text, int Size = 11, FLinearColor Color = FLinearColor::White) {
    return SNew(STextBlock).Text(FText::FromString(Text))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size)).ColorAndOpacity(Color)
        .AutoWrapText(true);
}
TSharedRef<SWidget> Section(const FString& Text) {
    return SNew(SBox).Padding(0.f, 16.f, 0.f, 8.f)
        [ SNew(STextBlock).Text(FText::FromString(Text))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10)).ColorAndOpacity(Orange) ];
}
TSharedRef<SWidget> Columns(TSharedRef<SWidget> Left,TSharedRef<SWidget> Right) {
    auto CardWrap=[](TSharedRef<SWidget> Content) -> TSharedRef<SWidget> {
        return SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(Card).Padding(FMargin(18.f,0.f,18.f,18.f))[Content];
    };
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1)[CardWrap(Left)]
        + SHorizontalBox::Slot().FillWidth(1).Padding(16.f,0.f,0.f,0.f)[CardWrap(Right)];
}
TSharedRef<SWidget> Page(const FString& Title,const FString& Description,TSharedRef<SWidget> Content) {
    return SNew(SScrollBox)
        + SScrollBox::Slot().Padding(0.f,0.f,12.f,0.f)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()[Label(Title,18)]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,5.f,0.f,14.f)[Label(Description,10,Muted)]
            + SVerticalBox::Slot().AutoHeight()[Content] ];
}

FVector EffectiveRates(const UFactoryFPVSettings& Settings) {
    return {Settings.AcroRate,factoryfpv::rateOrLinked(Settings.PitchRate,Settings.AcroRate),
        factoryfpv::rateOrLinked(Settings.YawRate,Settings.AcroRate)};
}

FVector CenterRates(const UFactoryFPVSettings& Settings) {
    return {Settings.RollCenterRate,Settings.PitchCenterRate,Settings.YawCenterRate};
}

float ExpoCenterRate(const UFactoryFPVSettings& Settings,int32 Axis) {
    const float Maximum=static_cast<float>(EffectiveRates(Settings)[Axis]);
    return FMath::Clamp((1.f-Settings.Expo)*Maximum,MinimumCenterRate,Maximum);
}

void ClampCenterRates(UFactoryFPVSettings& Settings) {
    const FVector Maximums=EffectiveRates(Settings);
    float* Centers[]{&Settings.RollCenterRate,&Settings.PitchCenterRate,&Settings.YawCenterRate};
    for (int32 Axis=0;Axis<UE_ARRAY_COUNT(Centers);++Axis)
        if (*Centers[Axis]>0.f) *Centers[Axis]=FMath::Clamp(*Centers[Axis],MinimumCenterRate,static_cast<float>(Maximums[Axis]));
}

class SFactoryFPVRateCurve final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVRateCurve) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UFactoryFPVSettings>, Settings)
        SLATE_ATTRIBUTE(FVector, StickMagnitudes)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args) {
        Settings=Args._Settings;
        StickMagnitudes=Args._StickMagnitudes;
        ForceVolatile(true);
    }

private:
    FVector2D ComputeDesiredSize(float) const override { return {240.,74.}; }

    int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool) const override {
        const auto* Draft=Settings.Get();
        const FVector2f Size(Geometry.GetLocalSize());
        if (!Draft || Size.X<25.f || Size.Y<15.f) return Layer;

        const auto Tint=Style.GetColorAndOpacityTint();
        const auto Paint=Geometry.ToPaintGeometry();
        const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        FSlateDrawElement::MakeBox(Elements,Layer,Paint,Brush,ESlateDrawEffect::None,Background*Tint);
        const FVector2f TopLeft(9.f,7.f),BottomRight=Size-FVector2f(9.f,7.f);
        const FVector2f PlotSize=BottomRight-TopLeft;
        const FLinearColor Grid(.12f,.16f,.20f);
        for (const float Fraction:{0.f,.5f,1.f}) {
            const float X=TopLeft.X+PlotSize.X*Fraction,Y=TopLeft.Y+PlotSize.Y*Fraction;
            FSlateDrawElement::MakeLines(Elements,Layer+1,Paint,TArray<FVector2f>{{X,TopLeft.Y},{X,BottomRight.Y}},
                ESlateDrawEffect::None,Grid*Tint,true,1.f);
            FSlateDrawElement::MakeLines(Elements,Layer+1,Paint,TArray<FVector2f>{{TopLeft.X,Y},{BottomRight.X,Y}},
                ESlateDrawEffect::None,Grid*Tint,true,1.f);
        }

        const FVector Rates=EffectiveRates(*Draft),Centers=CenterRates(*Draft),Sticks=StickMagnitudes.Get(FVector(-1.));
        const int32 FirstAxis=Draft->bAngleMode ? 2 : 0;
        for (int32 Axis=FirstAxis;Axis<3;++Axis) {
            const auto Project=[&](double Stick) {
                const double Fraction=factoryfpv::rateCommand(Stick,Rates[Axis],Draft->Expo,Draft->Deadzone,Centers[Axis])/MaximumRate;
                return FVector2f(TopLeft.X+PlotSize.X*Stick,BottomRight.Y-PlotSize.Y*Fraction);
            };
            TArray<FVector2f> Curve;
            Curve.Reserve(25);
            for (int32 Sample=0;Sample<=24;++Sample) Curve.Add(Project(Sample/24.));
            FSlateDrawElement::MakeLines(Elements,Layer+2,Paint,Curve,ESlateDrawEffect::None,RateColors[Axis]*Tint,true,2.f);
            if (Sticks[Axis]>=0.) {
                const FVector2f At=Project(Sticks[Axis])-FVector2f(2.5f,2.5f);
                FSlateDrawElement::MakeBox(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2f(5.f,5.f),FSlateLayoutTransform(At)),
                    Brush,ESlateDrawEffect::None,RateColors[Axis]*Tint);
            }
        }
        return Layer+3;
    }

    TWeakObjectPtr<UFactoryFPVSettings> Settings;
    TAttribute<FVector> StickMagnitudes;
};

class SFactoryFPVThrottleCurve final : public SLeafWidget {
public:
    SLATE_BEGIN_ARGS(SFactoryFPVThrottleCurve) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UFactoryFPVSettings>, Settings)
        SLATE_ATTRIBUTE(double, Stick)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args) {
        Settings=Args._Settings;
        Stick=Args._Stick;
        ForceVolatile(true);
    }

private:
    FVector2D ComputeDesiredSize(float) const override { return {240.,72.}; }

    int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,
        int32 Layer,const FWidgetStyle& Style,bool) const override {
        const auto* Draft=Settings.Get();
        const FVector2f Size(Geometry.GetLocalSize());
        if (!Draft || Size.X<25.f || Size.Y<15.f) return Layer;

        const auto Tint=Style.GetColorAndOpacityTint();
        const auto Paint=Geometry.ToPaintGeometry();
        const auto* Brush=FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
        FSlateDrawElement::MakeBox(Elements,Layer,Paint,Brush,ESlateDrawEffect::None,Background*Tint);
        const FVector2f PlotSize=Size-FVector2f(18.f,14.f);
        const auto Plot=[&](double Input,double Thrust) {
            return FVector2f(9.f+PlotSize.X*Input,7.f+PlotSize.Y*(1.-Thrust));
        };
        const auto Line=[&](int32 Offset,const TArray<FVector2f>& Points,const FLinearColor& Color,float Width=1.f) {
            FSlateDrawElement::MakeLines(Elements,Layer+Offset,Paint,Points,ESlateDrawEffect::None,Color*Tint,true,Width);
        };
        const auto Mark=[&](FVector2f Point,const FLinearColor& Color) {
            const auto At=Point-FVector2f(2.5f,2.5f);
            FSlateDrawElement::MakeBox(Elements,Layer+3,Geometry.ToPaintGeometry(FVector2f(5.f,5.f),FSlateLayoutTransform(At)),
                Brush,ESlateDrawEffect::None,Color*Tint);
        };
        const double Hover=1./Draft->ThrustToWeight;
        const auto Command=[&](double Input) { return factoryfpv::throttleCommand(Input,Hover,Draft->ThrottleExpo); };
        const auto HoverPoint=Plot(Hover,Hover);
        const FLinearColor Grid(.12f,.16f,.20f);
        Line(1,{Plot(0,1),Plot(0,0),Plot(1,0)},Grid);
        Line(1,{Plot(0,0),Plot(1,1)},Grid);
        Line(1,{Plot(0,Hover),HoverPoint,Plot(Hover,0)},Orange.CopyWithNewOpacity(.45f));

        TArray<FVector2f> Curve;
        Curve.Reserve(33);
        for (int32 Sample=0;Sample<=32;++Sample) {
            const double Input=Sample/32.;
            Curve.Add(Plot(Input,Command(Input)));
        }
        Line(2,Curve,Cyan,2.f);
        Mark(HoverPoint,Orange);
        const double Input=Stick.Get(-1.);
        if (Input>=0.) Mark(Plot(Input,Command(Input)),FLinearColor::White);
        return Layer+3;
    }

    TWeakObjectPtr<UFactoryFPVSettings> Settings;
    TAttribute<double> Stick;
};

}

void SFactoryFPVPanel::Construct(const FArguments& Args) {
    Controller = Args._Controller;
    Owner = Args._Owner;
    Message = Args._InitialMessage;
    Draft.Reset(DuplicateObject<UFactoryFPVSettings>(GetDefault<UFactoryFPVSettings>(), GetTransientPackage()));
    Draft->Sanitize();
    InputModes = {MakeShared<FString>(TEXT("Radio / USB")), MakeShared<FString>(TEXT("Keyboard")),
        MakeShared<FString>(TEXT("Gamepad"))};
    const FName Retry=Args._RetryInputMode;
    const FName Initial=Retry==TEXT("radio") || Retry==TEXT("keyboard") || Retry==TEXT("gamepad") ? Retry : Draft->PreferredInputMode;
    InputMode=InputModes[Initial==TEXT("keyboard") ? 1 : Initial==TEXT("gamepad") ? 2 : 0];
    RefreshTuningOptions();
    SavedDraft.Reset(DuplicateObject<UFactoryFPVSettings>(Draft.Get(),GetTransientPackage()));
    PageButtons.SetNum(static_cast<int32>(EPage::Count));
    if (!Args._InitialMessage.IsEmpty()) Message = Args._InitialMessage;


    ChildSlot
    [
        SNew(SBox).WidthOverride(1060.f).HeightOverride(730.f)
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(Background).Padding(24.f)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Text(FText::FromString(TEXT("FACTORY FPV")))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 26)).ColorAndOpacity(Orange) ]
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
                        [ Label(TEXT("HANGAR   /   SINGLE PLAYER"), 10, Muted) ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(SButton).Text(FText::FromString(TEXT("Close   Esc"))).OnClicked(this, &SFactoryFPVPanel::Close) ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [ FactoryFPVReturnStatusWidget::MakeHangarCard(TAttribute<EFactoryFPVReturnStatus>::CreateLambda([this] {
                    const auto* Anchor=PendingRecovery();
                    return Anchor ? Anchor->GetReturnStatus() : EFactoryFPVReturnStatus::None;
                }),FOnClicked::CreateSP(this,&SFactoryFPVPanel::RecoverPioneer)) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f,16.f,0.f,0.f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)[TabButton(EPage::Radio,TEXT("Radio"),TEXT("USB device, calibration and arm control"))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(5.f,0.f,0.f,0.f)[TabButton(EPage::Flight,TEXT("Flight"),TEXT("Camera, airframe, acro and Angle mode"))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(5.f,0.f,0.f,0.f)[TabButton(EPage::Presets,TEXT("Presets"),TEXT("Built-in and custom tuning presets"))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(5.f,0.f,0.f,0.f)[TabButton(EPage::Presentation,TEXT("OSD & Audio"),TEXT("FPV readouts, prop sound and flight cues"))]
                    + SHorizontalBox::Slot().FillWidth(1.25f).Padding(5.f,0.f,0.f,0.f)[TabButton(EPage::Simulation,TEXT("Simulation / Access"),TEXT("Optional battery endurance and crafted-radio access"))]
                    + SHorizontalBox::Slot().FillWidth(1).Padding(5.f,0.f,0.f,0.f)[TabButton(EPage::Racing,TEXT("Racing"),TEXT("Choose a recorded course or free flight"))]
                ]
                + SVerticalBox::Slot().FillHeight(1).Padding(0.f,16.f,0.f,12.f)
                [
                    SAssignNew(PageSwitcher,SWidgetSwitcher)
                    + SWidgetSwitcher::Slot()[BuildRadioPage()]
                    + SWidgetSwitcher::Slot()[BuildFlightPage()]
                    + SWidgetSwitcher::Slot()[BuildPresetsPage()]
                    + SWidgetSwitcher::Slot()[BuildPresentationPage()]
                    + SWidgetSwitcher::Slot()[BuildSimulationPage()]
                    + SWidgetSwitcher::Slot()[BuildRacingPage()]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(Message); })
                        .ColorAndOpacity(Cyan).AutoWrapText(true)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(16.f,0.f,0.f,0.f).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(this,&SFactoryFPVPanel::DraftStatus)
                        .ColorAndOpacity_Lambda([this] { return HasPendingEdits() ? Orange : Muted; })
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)) ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                    [ SNew(SVerticalBox)
                        + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
                        [ SNew(STextBlock).Text(this,&SFactoryFPVPanel::FlightSelectionText)
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10)).ColorAndOpacity(Cyan) ]
                        + SVerticalBox::Slot().AutoHeight()
                        [ SNew(STextBlock).Text(this,&SFactoryFPVPanel::RadioPreflightText)
                            .ColorAndOpacity_Lambda([this] { FString Error; return InputMode==InputModes[0] && !RadioReady(Error) ? Orange : Muted; })
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).AutoWrapText(true) ] ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(10.f, 0.f)
                    [
                        SAssignNew(InputModeCombo,SComboBox<TSharedPtr<FString>>).OptionsSource(&InputModes).InitiallySelectedItem(InputMode)
                        .OnGenerateWidget_Lambda([](TSharedPtr<FString> Mode) { return Label(*Mode); })
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Mode, ESelectInfo::Type) {
                            if (!Mode) return;
                            InputMode=Mode;
                            Draft->PreferredInputMode=SelectedInputMode();
                        })
                        [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(*InputMode); }) ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 10.f, 0.f)
                    [ SNew(SButton).Text(FText::FromString(TEXT("Save settings")))
                        .IsEnabled_Lambda([this] { return !Calibrating() && ArmCaptureStage<0; }).OnClicked(this, &SFactoryFPVPanel::SaveSettings) ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [ SNew(SButton).Text(FText::FromString(TEXT("Save & fly"))).ButtonColorAndOpacity(Orange)
                        .ContentPadding(FMargin(16.f, 8.f)).IsEnabled(this,&SFactoryFPVPanel::CanFly)
                        .ToolTipText(FText::FromString(TEXT("Save tuning and launch. With sandbox flights disabled, equip your FPV Radio Kit first.")))
                        .OnClicked(this, &SFactoryFPVPanel::Fly) ]
                ]
            ]
        ]
    ];
}

TSharedRef<SWidget> SFactoryFPVPanel::TabButton(EPage PageId,const FString& Title,const FString& Help) {
    return SAssignNew(PageButtons[static_cast<int32>(PageId)],SButton)
        .Tag(PageId==EPage::Racing ? FName(TEXT("FactoryFPVHangarRacingTab")) : NAME_None)
        .HAlign(HAlign_Center).ContentPadding(FMargin(8.f,10.f))
        .ToolTipText(FText::FromString(Help))
        .ButtonColorAndOpacity_Lambda([this,PageId] { return ActivePage==PageId ? Orange : Card; })
        .IsEnabled_Lambda([this,PageId] { return PageId==EPage::Radio || (!Calibrating() && ArmCaptureStage<0); })
        .OnClicked(this,&SFactoryFPVPanel::SelectPage,PageId)
        [ SNew(STextBlock).Text(FText::FromString(Title))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10)) ];
}

FReply SFactoryFPVPanel::SelectPage(EPage PageId) {
    if (static_cast<uint8>(PageId)>=static_cast<uint8>(EPage::Count)) return FReply::Handled();
    // Keep the active capture and its Cancel button visible until it finishes.
    if ((Calibrating() || ArmCaptureStage>=0) && PageId!=EPage::Radio) return FReply::Handled();
    if (PageId!=EPage::Presentation) {
        StopAudioPreview();
        AudioPreviewMessage.Reset();
    }
    ActivePage=PageId;
    if (PageSwitcher) PageSwitcher->SetActiveWidgetIndex(static_cast<int32>(PageId));
    return FReply::Handled();
}

bool SFactoryFPVPanel::HasUnsavedChanges() const {
    if (!Draft.IsValid() || !SavedDraft.IsValid()) return false;
    for (TFieldIterator<FProperty> Property(UFactoryFPVSettings::StaticClass());Property;++Property) {
        if (Property->HasAnyPropertyFlags(CPF_Config) && !Property->Identical_InContainer(Draft.Get(),SavedDraft.Get())) return true;
    }
    return false;
}

bool SFactoryFPVPanel::HasPendingEdits() const {
    return Calibrating() || ArmCaptureStage>=0 || (RacingPanel && RacingPanel->HasPendingGateEdits()) || HasUnsavedChanges();
}

FText SFactoryFPVPanel::DraftStatus() const {
    if (Calibrating() || ArmCaptureStage>=0) return FText::FromString(TEXT("Radio capture in progress"));
    if (RacingPanel && RacingPanel->HasPendingGateEdits())
        return FText::FromString(HasUnsavedChanges() ? TEXT("Unsaved settings and gate edits") : TEXT("Unapplied checkpoint edits"));
    return FText::FromString(HasUnsavedChanges() ? TEXT("Unsaved settings") : TEXT("All changes saved"));
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildRadioPage() {
    auto Setup = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("USB CONTROLLER")) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [
                SAssignNew(DeviceCombo, SComboBox<TSharedPtr<FFactoryFPVDeviceOption>>)
                .OptionsSource(&Devices).InitiallySelectedItem(SelectedDevice)
                .IsEnabled_Lambda([this] { return !Calibrating() && ArmCaptureStage<0; })
                .OnSelectionChanged(this, &SFactoryFPVPanel::ChooseDevice)
                .OnGenerateWidget_Lambda([](TSharedPtr<FFactoryFPVDeviceOption> Device) {
                    return Label(Device->Name);
                })
                [
                    SNew(STextBlock).Text_Lambda([this] {
                        return FText::FromString(SelectedDevice ? SelectedDevice->Name : TEXT("Select a USB controller"));
                    })
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton).Text(FText::FromString(TEXT("Refresh")))
                .IsEnabled_Lambda([this] { return !Calibrating() && ArmCaptureStage<0; })
                .OnClicked_Lambda([this] { RefreshDevices(); return FReply::Handled(); })
            ]
        ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
        [
            SNew(STextBlock).Text(this, &SFactoryFPVPanel::ConnectionText)
            .ColorAndOpacity_Lambda([this] { FString Error; return !bConnected ? Muted : (RadioReady(Error) ? Cyan : Orange); })
            .AutoWrapText(true)
        ]
        + SVerticalBox::Slot().AutoHeight()
        [ Label(TEXT("Center roll, pitch and yaw. Put throttle fully down. Move one stick at a time during setup."), 10, Muted) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 8.f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [
                SNew(SButton).Text_Lambda([this] {
                    return FText::FromString(Calibrating() ? TEXT("Restart calibration") : TEXT("Calibrate radio"));
                })
                .IsEnabled_Lambda([this] { return bConnected && ArmCaptureStage<0; })
                .OnClicked(this, &SFactoryFPVPanel::BeginCalibration)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton).Text(FText::FromString(TEXT("Capture")))
                .ButtonColorAndOpacity(Orange)
                .Visibility_Lambda([this] { return Calibrating() ? EVisibility::Visible : EVisibility::Collapsed; })
                .OnClicked(this, &SFactoryFPVPanel::CaptureAxis)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton).Text(FText::FromString(TEXT("Retry")))
                .ToolTipText(FText::FromString(TEXT("Center roll, pitch and yaw before pressing Retry. Then move only the requested axis.")))
                .IsEnabled_Lambda([this] { return bConnected && Calibrating(); })
                .Visibility_Lambda([this] { return Calibrating() ? EVisibility::Visible : EVisibility::Collapsed; })
                .OnClicked(this, &SFactoryFPVPanel::RetryAxis)
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f, 0.f, 0.f, 0.f)
            [
                SNew(SButton).Text(FText::FromString(TEXT("Cancel")))
                .Visibility_Lambda([this] { return Calibrating() ? EVisibility::Visible : EVisibility::Collapsed; })
                .OnClicked(this, &SFactoryFPVPanel::CancelCalibration)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(Card).Padding(12.f)
            [
                SNew(STextBlock).Text(this, &SFactoryFPVPanel::Guidance)
                .AutoWrapText(true)
            ]
        ]
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("LIVE INPUT   /   MOTORS OFF")) ]
        + SVerticalBox::Slot().AutoHeight()[ InputRow(0) ]
        + SVerticalBox::Slot().AutoHeight()[ InputRow(1) ]
        + SVerticalBox::Slot().AutoHeight()[ InputRow(2) ]
        + SVerticalBox::Slot().AutoHeight()[ InputRow(3) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
        [
            SNew(STextBlock).Text_Lambda([this] {
                if (!Calibrating()) return FText::GetEmpty();
                return FText::FromString(FString::Printf(
                    TEXT("RAW   X %ld   Y %ld   Z %ld   Rx %ld\n            Ry %ld   Rz %ld   S0 %ld   S1 %ld"),
                    Raw[0], Raw[1], Raw[2], Raw[3], Raw[4], Raw[5], Raw[6], Raw[7]));
            }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9)).ColorAndOpacity(Muted)
        ];

    auto Monitor = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("RADIO SWITCHES & BUTTONS"))]
        + SVerticalBox::Slot().AutoHeight()[BindingRow(ERadioControl::Arm,TEXT("Arm / disarm"))]
        + SVerticalBox::Slot().AutoHeight()[BindingRow(ERadioControl::Return,TEXT("Return to pioneer"))]
        + SVerticalBox::Slot().AutoHeight()[BindingRow(ERadioControl::Recover,TEXT("Recover drone"))]
        + SVerticalBox::Slot().AutoHeight()[BindingRow(ERadioControl::Mode,TEXT("Toggle Acro / Angle"))]
        + SVerticalBox::Slot().AutoHeight().Padding(0,10)
        [ SNew(STextBlock).Text_Lambda([this] {
            FString Error;
            return FText::FromString(Draft->AreRadioBindingsValid(Error) ?
                TEXT("Use separate USB controls. Arm needs low throttle and OFF then ON. The other actions trigger once per OFF to ON cycle; return moves you back to the pioneer, recover resets the drone, mode toggles Acro/Angle. Start switches OFF each flight and after a pause or radio loss.") : Error);
          }).AutoWrapText(true).ColorAndOpacity_Lambda([this] { FString Error; return Draft->AreRadioBindingsValid(Error) ? Muted : Orange; })
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)) ];
    const auto KeySelector=[this](FShortcutControl Control) -> TSharedRef<SWidget> {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Label(Control.Label,10)]
            + SHorizontalBox::Slot().AutoWidth().Padding(6.f,0.f,0.f,0.f)
            [ SNew(SComboButton).Tag(FName(Control.Tag))
                .IsEnabled_Lambda([this] { return !Calibrating() && ArmCaptureStage<0; })
                .ToolTipText(FText::FromString(TEXT("Choose a function key. If a Factory FPV action already uses that key, the actions swap keys.")))
                .OnGetMenuContent_Lambda([this,Control] {
                    FMenuBuilder Menu(true,nullptr);
                    for (const FKey& Key : FShortcutKeys::FunctionKeys()) {
                        FString Choice=FShortcutKeys::KeyLabel(Key);
                        for (const auto& Other : ShortcutControls)
                            if (Draft->KeyboardShortcuts.*Other.Key==Key) Choice+=FString::Printf(TEXT("  ·  %s"),Other.Label);
                        const FUIAction Select(
                            FExecuteAction::CreateSPLambda(this,[this,Control,Key] {
                                if (Calibrating() || ArmCaptureStage>=0) return;
                                auto& Keys=Draft->KeyboardShortcuts;
                                FKey& Current=Keys.*Control.Key;
                                if (Current==Key) return;
                                for (const auto& Other : ShortcutControls) {
                                    if (Keys.*Other.Key!=Key) continue;
                                    Keys.*Other.Key=Current;
                                    Current=Key;
                                    Message=FString::Printf(TEXT("%s and %s swapped keys. Save settings to apply."),Control.Label,Other.Label);
                                    return;
                                }
                                Current=Key;
                                Message=TEXT("Shortcut changed in the draft. Save settings to apply.");
                            }),FCanExecuteAction(),
                            FIsActionChecked::CreateSPLambda(this,[this,Control,Key] { return Draft->KeyboardShortcuts.*Control.Key==Key; }));
                        Menu.AddMenuEntry(FText::FromString(Choice),FText::GetEmpty(),FSlateIcon(),Select,NAME_None,EUserInterfaceActionType::RadioButton);
                    }
                    return Menu.MakeWidget();
                })
                .ButtonContent()
                [ SNew(STextBlock).Text_Lambda([this,Control] {
                    return FText::FromString(FShortcutKeys::KeyLabel(Draft->KeyboardShortcuts.*Control.Key));
                  }) ] ];
    };
    Monitor->AddSlot().AutoHeight().Padding(0.f,4.f,0.f,6.f)
    [ SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(TEXT("KEYBOARD SHORTCUTS")))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),10)).ColorAndOpacity(Orange) ]
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVResetKeyboardShortcuts"))).Text(FText::FromString(TEXT("Reset defaults")))
            .IsEnabled_Lambda([this] { return !Calibrating() && ArmCaptureStage<0; })
            .OnClicked_Lambda([this] {
                if (Calibrating() || ArmCaptureStage>=0) return FReply::Handled();
                Draft->KeyboardShortcuts=FShortcutKeys{};
                Message=TEXT("Default shortcuts restored in the draft. Save settings to apply.");
                return FReply::Handled();
            }) ] ];
    for (int32 Index=0;Index<UE_ARRAY_COUNT(ShortcutControls);Index+=2) {
        Monitor->AddSlot().AutoHeight().Padding(0.f,0.f,0.f,4.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[KeySelector(ShortcutControls[Index])]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(12.f,0.f,0.f,0.f)[KeySelector(ShortcutControls[Index+1])] ];
    }
    Monitor->AddSlot().AutoHeight()
    [ Label(TEXT("Save settings or launch to apply. Esc still closes the hangar."),9,Muted) ];
    return Page(TEXT("Radio setup"),TEXT("Connect in USB Joystick (HID) mode. Calibrate once per radio, then check directions. Switch bindings are optional; keyboard shortcuts remain available."),Columns(Setup,Monitor));
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildFlightPage() {
    ScoutPreviewTexture.Reset(LoadObject<UTexture2D>(nullptr,TEXT("/FactoryFPV/Airframe/T_ScoutPreview.T_ScoutPreview")));
    ScoutPreviewBrush.SetResourceObject(ScoutPreviewTexture.Get());
    ScoutPreviewBrush.ImageType=ESlateBrushImageType::FullColor;
    ScoutPreviewBrush.SetImageSize(FVector2D(175.f,112.f));
    const auto PreviewThrottle=[this] {
        return InputMode==InputModes[0] && Draft->IsRadioCalibrated() && AxisAvailable(3) ? NormalizedAxis(3) : -1.;
    };

    auto AirframeTuning = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Frame size (cm)"), TEXT("Overall drone size and collision diameter."),
            &Draft->FrameSizeCm, 15.f, 100.f, 1.f) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Mass (kg)"), TEXT("Mass changes calculated thrust and impact energy. Thrust-to-weight controls acceleration."),
            &Draft->MassKg, 0.1f, 5.f, 0.05f) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Thrust / weight"), TEXT("Maximum thrust as a multiple of weight. Higher values accelerate faster."),
            &Draft->ThrustToWeight, 1.2f, 10.f, 0.1f) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)
        [ SNew(STextBlock).Text_Lambda([this] {
            return FText::FromString(FString::Printf(TEXT("Max thrust %.1f N"),Draft->MassKg*9.81f*Draft->ThrustToWeight));
          }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9)).ColorAndOpacity(Muted).AutoWrapText(true) ];
    auto CameraAirframe = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("CAMERA")) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Camera tilt"), TEXT("Upward lens angle in degrees."),
            &Draft->CameraTilt, -10.f, 65.f, 1.f) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Field of view"), TEXT("Horizontal camera field of view in degrees."),
            &Draft->CameraFov, 60.f, 140.f, 1.f) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Fisheye lens"), TEXT("Optional barrel distortion. 0 is off; 1 is strongest. Center stays fixed; edges are cropped to avoid black borders. Applies on your next flight."),
            &Draft->LensStrength, 0.f, 1.f, 0.05f) ]
        + SVerticalBox::Slot().AutoHeight()
        [
            SNew(SCheckBox)
            .IsChecked_Lambda([this] { return Draft->bShowFrameInFPV ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft->bShowFrameInFPV = State == ECheckBoxState::Checked; })
            .ToolTipText(FText::FromString(TEXT("Shows front prop tips at the lens edges. Steep tilt or narrow FOV can move props out of view.")))
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Show prop tips in FPV")))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)) ]
        ]
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("SCOUT AIRFRAME")) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[ AirframeTuning ]
            + SHorizontalBox::Slot().AutoWidth().Padding(12.f,0.f,0.f,0.f).VAlign(VAlign_Center)
            [ SNew(SImage).Tag(FName(TEXT("FactoryFPVScoutPreview"))).Image(&ScoutPreviewBrush)
                .Visibility(ScoutPreviewTexture.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed) ] ]
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("THROTTLE RESPONSE")) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Throttle Expo"),
            TEXT("Softens thrust changes around level hover. Zero uses a linear response."),&Draft->ThrottleExpo,0.f,.8f,.05f) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,2.f,0.f,4.f)
        [ Label(TEXT("STICK 0–100%   /   COMMANDED THRUST 0–100%"),9,Muted) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SFactoryFPVThrottleCurve).Tag(FName(TEXT("FactoryFPVThrottleCurve"))).Settings(Draft.Get())
            .Stick_Lambda(PreviewThrottle)
            .ToolTipText(FText::FromString(TEXT("Orange marks estimated level hover on a healthy frame. White tracks your calibrated radio."))) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,5.f,0.f,0.f)
        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVThrottlePreview"))).Text_Lambda([this,PreviewThrottle] {
            const double Hover=1./Draft->ThrustToWeight;
            FString Text=FString::Printf(TEXT("Level hover ~%.0f%% on a healthy frame"),Hover*100.);
            const double Input=PreviewThrottle();
            if (Input>=0.) Text+=FString::Printf(TEXT("  ·  Radio %.0f%% → %.0f%% thrust"),Input*100.,
                factoryfpv::throttleCommand(Input,Hover,Draft->ThrottleExpo)*100.);
            return FText::FromString(Text);
          }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Muted).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,0.f)
        [ Label(TEXT("Expo gives finer control near hover. Zero and full throttle stay the same."),9,Muted) ];
    auto Response = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("FLIGHT RESPONSE")) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f)
        [ SNew(SCheckBox).IsChecked_Lambda([this] { return Draft->bAngleMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft->bAngleMode=State==ECheckBoxState::Checked; })
            [ Label(TEXT("Angle mode / self-level")) ] ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SBox).Visibility_Lambda([this] { return Draft->bAngleMode ? EVisibility::Visible : EVisibility::Collapsed; })
            [ SettingsRow(TEXT("Maximum commanded tilt"), TEXT("Angle mode stick limit in degrees. No altitude or position hold."), &Draft->AngleLimit,5,80,1) ] ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SBox).Visibility_Lambda([this] { return Draft->bAngleMode ? EVisibility::Visible : EVisibility::Collapsed; })
            [ SettingsRow(TEXT("Leveling response"),TEXT("How quickly Angle mode corrects tilt. Higher feels more immediate."),&Draft->LevelResponse,.5,20,.5) ] ]
        + SVerticalBox::Slot().AutoHeight()[ BuildRateTuning() ];
    return Page(TEXT("Flight tuning"),TEXT("Shape the view, airframe and stick response. All tuning applies on your next flight."),Columns(CameraAirframe,Response));
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildRateTuning() {
    const auto RateColumn=[this](const TCHAR* Axis,int32 Index,float* Override) -> TSharedRef<SWidget> {
        const FString Name=FString(TEXT("FactoryFPV"))+Axis+TEXT("Rate");
        float* const Value=Override ? Override : &Draft->AcroRate;
        auto Column=SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,3.f)
            [ Label(Override ? Axis : TEXT("Roll / linked base"),9,RateColors[Index]) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SSpinBox<float>).Tag(FName(*Name))
                .Value_Lambda([this,Value] { return static_cast<float>(factoryfpv::rateOrLinked(*Value,Draft->AcroRate)); })
                .IsEnabled_Lambda([Override] { return !Override || *Override>0.f; })
                .MinValue(MinimumRate).MaxValue(MaximumRate).Delta(10.f).MinFractionalDigits(0).MaxFractionalDigits(0)
                .OnValueChanged_Lambda([this,Value,Override](float NewValue) {
                    if (!Override || *Override>0.f) {
                        *Value=NewValue;
                        ClampCenterRates(*Draft);
                    }
                }) ];
        if (Override) Column->AddSlot().AutoHeight().Padding(0.f,3.f,0.f,0.f)
        [ SNew(SCheckBox).Tag(FName(*(Name+TEXT("Override"))))
            .IsChecked_Lambda([Override] { return *Override>0.f ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this,Override](ECheckBoxState State) {
                *Override=State==ECheckBoxState::Checked ? Draft->AcroRate : 0.f;
                ClampCenterRates(*Draft);
            })
            .ToolTipText(FText::FromString(TEXT("Use a separate rate for this axis. Clear to follow the roll rate.")))
            [ Label(TEXT("Separate rate"),9) ] ];
        return Column;
    };
    const auto CenterColumn=[this](const TCHAR* Axis,int32 Index,float* Value) -> TSharedRef<SWidget> {
        const FString Name=FString(TEXT("FactoryFPV"))+Axis+TEXT("CenterRate");
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f,0.f,4.f,0.f)
            [ SNew(SCheckBox).Tag(FName(*(Name+TEXT("Override"))))
                .IsChecked_Lambda([Value] { return *Value>0.f ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this,Value,Index](ECheckBoxState State) {
                    *Value=State==ECheckBoxState::Checked ? ExpoCenterRate(*Draft,Index) : 0.f;
                })
                .ToolTipText(FText::FromString(TEXT("Set center sensitivity independently for this axis. Clear to use Expo.")))
                [ Label(Axis,9,RateColors[Index]) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(SSpinBox<float>).Tag(FName(*Name))
                .Value_Lambda([this,Value,Index] { return *Value>0.f ? *Value : ExpoCenterRate(*Draft,Index); })
                .IsEnabled_Lambda([Value] { return *Value>0.f; })
                .MinValue(MinimumCenterRate)
                .MaxValue_Lambda([this,Index] { return TOptional<float>(static_cast<float>(EffectiveRates(*Draft)[Index])); })
                .Delta(5.f).MinFractionalDigits(0).MaxFractionalDigits(0)
                .OnValueChanged_Lambda([Value](float NewValue) { if (*Value>0.f) *Value=NewValue; })
                .ToolTipText(FText::FromString(TEXT("Response slope after the deadzone. A larger value makes small stick movements more responsive."))) ];
    };
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("MAX RATE   /   DEG/S")) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,0.f,0.f,6.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[RateColumn(TEXT("Roll"),0,nullptr)]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f,0.f,0.f,0.f)[RateColumn(TEXT("Pitch"),1,&Draft->PitchRate)]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f,0.f,0.f,0.f)[RateColumn(TEXT("Yaw"),2,&Draft->YawRate)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,3.f,0.f,4.f)
        [ Label(TEXT("CENTER SENSITIVITY   /   DEG/S"),9,Orange) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)[CenterColumn(TEXT("Roll"),0,&Draft->RollCenterRate)]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f,0.f,0.f,0.f)[CenterColumn(TEXT("Pitch"),1,&Draft->PitchCenterRate)]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f,0.f,0.f,0.f)[CenterColumn(TEXT("Yaw"),2,&Draft->YawCenterRate)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,4.f,0.f,3.f)
        [ Label(TEXT("Unchecked axes use Expo. With a custom center, Expo shapes the outer stick."),9,Muted) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Expo"), TEXT("Shapes the stick response while retaining the full-stick maximum."),
            &Draft->Expo, 0.f, 0.85f, 0.05f) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Stick deadzone"), TEXT("Ignores small motion around calibrated stick center."),
            &Draft->Deadzone, 0.f, 0.15f, 0.005f) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f,0.f,4.f)
        [ Label(TEXT("STICK TRAVEL 0–100%   /   RATE 0–1,200°/s"),9,Muted) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SFactoryFPVRateCurve).Tag(FName(TEXT("FactoryFPVRateCurve"))).Settings(Draft.Get())
            .ToolTipText(FText::FromString(TEXT("The curves show requested rate at each stick position. Squares track live calibrated USB sticks.")))
            .StickMagnitudes_Lambda([this] {
                FVector Sticks(-1.);
                if (InputMode==InputModes[0] && bConnected && Draft->IsRadioCalibrated())
                    for (int32 Axis=0;Axis<3;++Axis) if (AxisAvailable(Axis)) Sticks[Axis]=FMath::Abs(NormalizedAxis(Axis));
                return Sticks;
            }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,5.f,0.f,0.f)
        [ SNew(STextBlock).Tag(FName(TEXT("FactoryFPVRateHalfStick"))).Text_Lambda([this] {
            const FVector Rates=EffectiveRates(*Draft),Centers=CenterRates(*Draft);
            FVector Half;
            for (int32 Axis=0;Axis<3;++Axis) Half[Axis]=factoryfpv::rateCommand(.5,Rates[Axis],Draft->Expo,Draft->Deadzone,Centers[Axis]);
            return FText::FromString(Draft->bAngleMode ? FString::Printf(TEXT("Half stick   /   Yaw %.0f°/s"),Half.Z) :
                FString::Printf(TEXT("Half stick   /   Roll %.0f · Pitch %.0f · Yaw %.0f°/s"),Half.X,Half.Y,Half.Z));
          }).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Muted).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
        [ Label(TEXT("In Angle mode, roll and pitch command tilt. Yaw keeps its configured rate."),9,Muted) ];
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildPresetsPage() {
    auto Library = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("TUNING LIBRARY")) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SAssignNew(TuningCombo,SComboBox<TSharedPtr<FFactoryFPVTuningOption>>).OptionsSource(&TuningOptions)
            .OnGenerateWidget_Lambda([](TSharedPtr<FFactoryFPVTuningOption> Option) { return Label(Option->Preset.Name+(Option->bBuiltIn ? TEXT("  /  built-in") : TEXT(""))); })
            .OnSelectionChanged_Lambda([this](TSharedPtr<FFactoryFPVTuningOption> Option,ESelectInfo::Type) { SelectedTuning=Option; })
            [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(SelectedTuning ? SelectedTuning->Preset.Name : TEXT("Choose a tuning preset")); }) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1)
            [ SNew(SButton).Text(FText::FromString(TEXT("Apply to draft"))).IsEnabled_Lambda([this] { return SelectedTuning.IsValid(); }).OnClicked(this,&SFactoryFPVPanel::ApplySelectedTuning) ]
            + SHorizontalBox::Slot().AutoWidth().Padding(8.f,0.f)
            [ SNew(SButton).Text(FText::FromString(TEXT("Delete custom"))).IsEnabled_Lambda([this] { return SelectedTuning && !SelectedTuning->bBuiltIn; }).OnClicked(this,&SFactoryFPVPanel::DeleteTuning) ] ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SEditableTextBox).Text_Lambda([this] { return FText::FromString(TuningName); }).HintText(FText::FromString(TEXT("Custom preset name")))
            .OnTextChanged_Lambda([this](const FText& Value) { TuningName=Value.ToString().Left(40); }) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f)
        [ SNew(SButton).Text(FText::FromString(TEXT("Store current tuning in draft"))).OnClicked(this,&SFactoryFPVPanel::SaveTuning) ]
        + SVerticalBox::Slot().AutoHeight()[ Label(TEXT("Presets keep your radio calibration, OSD and access preferences. Use Save settings to keep this draft."),10,Muted) ];
    auto Guide = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("FIND YOUR STARTING POINT"))]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Factory cruiser"),13)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,16)[Label(TEXT("Self-leveling Angle mode with a relaxed camera and response for exploring factories."),10,Muted)]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Acro freestyle"),13)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,16)[Label(TEXT("Attitude stays where you put it. Faster rates and more thrust for pilots comfortable with acro."),10,Muted)]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Micro explorer"),13)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,16)[Label(TEXT("A compact frame for tighter factory spaces. Check your clearance and fly gently near machinery."),10,Muted)]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Make it yours"),13)]
        + SVerticalBox::Slot().AutoHeight().Padding(0,5)[Label(TEXT("Apply a preset, tune it in Flight, then store it with a custom name. Presets include camera, airframe, flight response, prop volume, battery and crash simulation. Radio, OSD and access preferences stay yours."),10,Muted)];
    return Page(TEXT("Tuning presets"),TEXT("Try a built-in tune or keep a library of your own. Applying a preset changes this draft; Save settings commits it."),Columns(Library,Guide));
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildPresentationPage() {
    auto Osd = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("FLIGHT OSD")) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("OSD text size"),TEXT("Scale FPV readouts. Warnings and receiver state remain visible."),&Draft->OsdTextScale,.7,1.3,.05) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Telemetry opacity"),TEXT("Warnings always remain fully opaque."),&Draft->OsdOpacity,.3,1,.05) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show crosshair"),&Draft->bOsdCrosshair) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show speed"),&Draft->bOsdSpeed) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show altitude"),&Draft->bOsdAltitude) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show distance home"),&Draft->bOsdHome) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show armed timer"),&Draft->bOsdTimer) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Show throttle"),&Draft->bOsdThrottle) ];
    auto Audio = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("PROP AUDIO")) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Prop volume"), TEXT("Local FPV motor and prop sound. Zero mutes it."),
            &Draft->PropVolume, 0.f, 1.f, 0.05f) ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(0.f,8.f,0.f,0.f)
        [ SNew(SButton).Tag(FName(TEXT("FactoryFPVPreviewPropSound")))
            .Text_Lambda([this] { return FText::FromString(PropPreview.IsValid() ? TEXT("Stop preview") : TEXT("Preview prop sound")); })
            .ToolTipText(FText::FromString(TEXT("Hear idle, a short throttle rise and spin-down with your draft settings.")))
            .OnClicked(this,&SFactoryFPVPanel::ToggleAudioPreview,false) ]
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("FLIGHT CUES")) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Flight cue volume"),
            TEXT("Checkpoint, lap, personal best and receiver-loss tones. Zero mutes them."),&Draft->FlightCueVolume,0.f,1.f,.05f) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,8.f,0.f,0.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f,0.f,8.f,0.f)
            [ SNew(SComboButton).Tag(FName(TEXT("FactoryFPVFlightCueType")))
                .ToolTipText(FText::FromString(TEXT("Choose which flight cue to preview.")))
                .OnGetMenuContent_Lambda([this] {
                    FMenuBuilder Menu(true,nullptr);
                    for (int32 Index=0;Index<UE_ARRAY_COUNT(FlightCueOptions);++Index) {
                        Menu.AddMenuEntry(FText::FromString(FlightCueOptions[Index].Label),FText::GetEmpty(),FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSPLambda(this,[this,Index] {
                                if (CuePreview.IsValid()) StopAudioPreview();
                                CuePreviewOption=Index;
                                AudioPreviewMessage.Reset();
                            })));
                    }
                    return Menu.MakeWidget();
                })
                .ButtonContent()[ SNew(STextBlock).Text_Lambda([this] {
                    return FText::FromString(FlightCueOptions[CuePreviewOption].Label);
                }) ] ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(SButton).Tag(FName(TEXT("FactoryFPVPreviewFlightCue")))
                .Text_Lambda([this] { return FText::FromString(CuePreview.IsValid() ? TEXT("Stop preview") : TEXT("Preview cue")); })
                .OnClicked(this,&SFactoryFPVPanel::ToggleAudioPreview,true) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,5.f,0.f,0.f)
        [ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(AudioPreviewMessage); })
            .Visibility_Lambda([this] { return AudioPreviewMessage.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),10)).ColorAndOpacity(Orange).AutoWrapText(true) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,12.f)
        [ Label(TEXT("Previews follow game Master and Effects. Save settings to keep these volumes for your next flight."),10,Muted) ];
    auto Safety=SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("READABILITY & SAFETY"))]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Receiver state and flight warnings stay visible even when telemetry is hidden or dimmed. The normal gameplay HUD is hidden only during FPV flight and restored when you return."),10,Muted)];
    auto Content=SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(2.f).VAlign(VAlign_Top)
            [ SNew(SBox).HeightOverride(348.f).Clipping(EWidgetClipping::ClipToBounds)
                [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                    .BorderBackgroundColor(FLinearColor(.014f,.021f,.030f)).Padding(0.f)
                    [ SAssignNew(OsdPreview,SFactoryFPVOSD).Tag(FName(TEXT("FactoryFPVOsdPreview")))
                        .Visibility(EVisibility::HitTestInvisible).Shortcuts(Draft->KeyboardShortcuts) ] ] ]
            + SHorizontalBox::Slot().FillWidth(1.10f).Padding(12.f,0.f,0.f,0.f)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(Card).Padding(FMargin(12.f,0.f,12.f,10.f))
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()[BuildOsdLayoutEditor()]
                    + SVerticalBox::Slot().AutoHeight()[Osd] ] ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,12.f,0.f,0.f)
        [ Columns(Audio,Safety) ];
    UpdateOsdPreview();
    return Page(TEXT("OSD & audio"),TEXT("Preview the draft with sample telemetry. Prop sound and flight cue controls are below."),Content);
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildOsdLayoutEditor() {
    using FLayout=FFactoryFPVOsdLayout;
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("OSD LAYOUT"))]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.f)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Readout"),9,Muted)]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SComboButton).Tag(FName(TEXT("FactoryFPVOsdItem")))
                    .ToolTipText(FText::FromString(TEXT("Choose a movable readout. Battery and frame appear when their simulations are enabled.")))
                    .OnGetMenuContent_Lambda([this] {
                        FMenuBuilder Menu(true,nullptr);
                        for (uint8 Index=0;Index<static_cast<uint8>(EFactoryFPVOsdItem::Count);++Index) {
                            const auto Item=static_cast<EFactoryFPVOsdItem>(Index);
                            Menu.AddMenuEntry(FText::FromString(FLayout::ItemLabel(Item)),FText::GetEmpty(),FSlateIcon(),
                                FUIAction(FExecuteAction::CreateSPLambda(this,[this,Item] { OsdLayoutItem=Item; })));
                        }
                        return Menu.MakeWidget();
                    })
                    .ButtonContent()[ SNew(STextBlock).Text_Lambda([this] { return FText::FromString(FLayout::ItemLabel(OsdLayoutItem)); }) ] ] ]
            + SHorizontalBox::Slot().FillWidth(1.f).Padding(8.f,0.f,0.f,0.f)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Position"),9,Muted)]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SComboButton).Tag(FName(TEXT("FactoryFPVOsdPosition")))
                    .ToolTipText(FText::FromString(TEXT("Choose where the selected readout appears in flight.")))
                    .OnGetMenuContent_Lambda([this] {
                        FMenuBuilder Menu(true,nullptr);
                        const auto Item=OsdLayoutItem;
                        for (uint8 Index=0;Index<static_cast<uint8>(EFactoryFPVOsdSlot::Count);++Index) {
                            const auto Slot=static_cast<EFactoryFPVOsdSlot>(Index);
                            FString Choice=FLayout::SlotLabel(Slot);
                            for (uint8 OtherIndex=0;OtherIndex<static_cast<uint8>(EFactoryFPVOsdItem::Count);++OtherIndex) {
                                const auto Other=static_cast<EFactoryFPVOsdItem>(OtherIndex);
                                if (Item!=Other && Draft->OsdLayout.GetSlot(Other)==Slot)
                                    Choice+=FString::Printf(TEXT("  ·  %s"),FLayout::ItemLabel(Other));
                            }
                            Menu.AddMenuEntry(FText::FromString(Choice),FText::GetEmpty(),FSlateIcon(),
                                FUIAction(FExecuteAction::CreateSPLambda(this,[this,Item,Slot] {
                                    if (Draft->OsdLayout.Move(Item,Slot)) UpdateOsdPreview();
                                })));
                        }
                        return Menu.MakeWidget();
                    })
                    .ButtonContent()[ SNew(STextBlock).Text_Lambda([this] {
                        return FText::FromString(FLayout::SlotLabel(Draft->OsdLayout.GetSlot(OsdLayoutItem)));
                    }) ] ] ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,5.f,0.f,0.f)
        [ Label(TEXT("Choosing an occupied position swaps the two readouts. Warnings, receiver and race cues stay in place."),9,Muted) ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f,0.f,0.f)
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SCheckBox).IsChecked_Lambda([this] { return bOsdPreviewRace ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bOsdPreviewRace=State==ECheckBoxState::Checked; UpdateOsdPreview(); })
                [ Label(TEXT("Racing"),9) ] ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f,0.f,0.f,0.f)
            [ SNew(SCheckBox).IsChecked_Lambda([this] { return bOsdPreviewWarning ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { bOsdPreviewWarning=State==ECheckBoxState::Checked; UpdateOsdPreview(); })
                [ Label(TEXT("Warning"),9) ] ]
            + SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).Padding(8.f,0.f,0.f,0.f)
            [ SNew(SButton).Tag(FName(TEXT("FactoryFPVResetOsdLayout"))).Text(FText::FromString(TEXT("Reset layout")))
                .OnClicked_Lambda([this] { Draft->OsdLayout=FLayout{}; UpdateOsdPreview(); return FReply::Handled(); }) ] ];
}

void SFactoryFPVPanel::UpdateOsdPreview() {
    if (!OsdPreview || !Draft.IsValid()) return;
    FFactoryFPVOSDTelemetry Data;
    Data.Layout=Draft->OsdLayout;
    Data.bArmed=true;
    Data.bAngleMode=Draft->bAngleMode;
    Data.bShowCrosshair=Draft->bOsdCrosshair;
    Data.bShowSpeed=Draft->bOsdSpeed;
    Data.bShowAltitude=Draft->bOsdAltitude;
    Data.bShowHome=Draft->bOsdHome;
    Data.bShowTimer=Draft->bOsdTimer;
    Data.bShowThrottle=Draft->bOsdThrottle;
    Data.TextScale=Draft->OsdTextScale;
    Data.Opacity=Draft->OsdOpacity;
    Data.SpeedMps=16.8;
    Data.RelativeAltitudeM=18.;
    Data.HomeDistanceM=125.;
    Data.ArmedSeconds=83.;
    Data.Throttle=.36;
    Data.bBatteryEnabled=Draft->bSimulateBattery;
    Data.BatteryPercent=65.;
    Data.BatteryVolts=Draft->BatteryCells*3.81;
    Data.bDamageEnabled=Draft->bCrashDamageEnabled;
    Data.IntegrityPercent=82.;
    if (bOsdPreviewRace) {
        Data.bRacing=true;
        Data.RaceCourse=7;
        Data.RaceGate=2;
        Data.RaceGateCount=5;
        Data.RaceElapsed=12.34;
        Data.RaceLast=29.81;
        Data.RaceBest=28.60;
        Data.bRaceTargetAvailable=true;
        Data.bRaceTargetVerticalGate=true;
        Data.RaceTargetElevationDegrees=18.;
        Data.RaceTargetDistanceM=42.;
        Data.RaceTargetHeightOffsetM=8.;
        Data.RaceTargetInstruction=TEXT("ASCEND");
    }
    if (bOsdPreviewWarning) Data.Warning=TEXT("SAMPLE WARNING");
    OsdPreview->SetTelemetry(Data);
}

SFactoryFPVPanel::~SFactoryFPVPanel() {
    StopAudioPreview();
}

bool SFactoryFPVPanel::CanPreviewAudio() const {
    const auto* PC=Controller.Get();
    const auto* World=PC ? PC->GetWorld() : nullptr;
    const auto* Subsystem=Owner.Get();
    return ActivePage==EPage::Presentation && World && World->IsGameWorld() && !World->bIsTearingDown && Subsystem &&
        Subsystem->CanPreviewHangarAudio(this) && FSlateApplication::IsInitialized() && FPlatformApplicationMisc::IsThisApplicationForeground();
}

FReply SFactoryFPVPanel::ToggleAudioPreview(bool bFlightCue) {
    AudioPreviewMessage.Reset();
    const bool bWasPlaying=bFlightCue ? CuePreview.IsValid() : PropPreview.IsValid();
    StopAudioPreview();
    if (bWasPlaying) return FReply::Handled();

    const double Volume=bFlightCue ? Draft->FlightCueVolume : Draft->PropVolume;
    if (!(Volume>0.)) {
        AudioPreviewMessage=bFlightCue ? TEXT("Flight cue volume is zero. Raise it to hear the preview.") :
            TEXT("Prop volume is zero. Raise it to hear the preview.");
        return FReply::Handled();
    }
    if (!CanPreviewAudio()) {
        AudioPreviewMessage=TEXT("Resume the game and focus the hangar to preview sound.");
        return FReply::Handled();
    }
    auto* PC=Controller.Get();
    auto* World=PC->GetWorld();
    const double Gain=Volume*FactoryFPVAudioSettings::GameGain();
    if (!(Gain>0.)) {
        AudioPreviewMessage=TEXT("Game Master or Effects volume is muted.");
        return FReply::Handled();
    }
    if (!World->AllowAudioPlayback() || !World->GetAudioDevice().IsValid()) {
        AudioPreviewMessage=TEXT("Game audio is unavailable.");
        return FReply::Handled();
    }
    bool bStarted;
    if (bFlightCue) {
        CuePreview.Reset(NewObject<UFactoryFPVFlightCueAudioComponent>(PC,NAME_None,RF_Transient));
        CuePreview->RegisterComponent();
        bStarted=CuePreview->PlayCue(FlightCueOptions[CuePreviewOption].Cue,Gain);
    } else {
        PropPreview.Reset(NewObject<UFactoryFPVPropAudioComponent>(PC,NAME_None,RF_Transient));
        PropPreview->RegisterComponent();
        PropPreview->SetMotorState({true,0.,Gain,UFactoryFPVPropAudioComponent::MaxRpmForAirframe(Draft->FrameSizeCm,Draft->ThrustToWeight)});
        PropPreview->Start();
        bStarted=PropPreview->IsPlaying();
        PropPreviewStartedAt=FPlatformTime::Seconds();
    }
    if (!bStarted) {
        StopAudioPreview();
        AudioPreviewMessage=bFlightCue ? TEXT("The game could not start the flight cue preview.") :
            TEXT("The game could not start the prop sound preview.");
        return FReply::Handled();
    }
    AudioPreviewActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged()
        .AddSP(this,&SFactoryFPVPanel::AudioPreviewActivationChanged);
    AudioPreviewWorldHandle=FWorldDelegates::OnWorldBeginTearDown.AddSP(this,&SFactoryFPVPanel::AudioPreviewWorldTearingDown);
    AudioPreviewTimer=RegisterActiveTimer(0.f,FWidgetActiveTimerDelegate::CreateSP(this,&SFactoryFPVPanel::TickAudioPreview));
    return FReply::Handled();
}

EActiveTimerReturnType SFactoryFPVPanel::TickAudioPreview(double,float) {
    if (CanPreviewAudio()) {
        const auto* World=Controller->GetWorld();
        const double GameGain=FactoryFPVAudioSettings::GameGain();
        if (IsValid(CuePreview.Get()) && CuePreview->GetWorld()==World && CuePreview->IsPlaying()) {
            const double Gain=Draft->FlightCueVolume*GameGain;
            if (Gain>0.) {
                CuePreview->SetVolumeMultiplier(static_cast<float>(FMath::Min(1.,Gain)));
                return EActiveTimerReturnType::Continue;
            }
        }
        const double Elapsed=FPlatformTime::Seconds()-PropPreviewStartedAt;
        const double Gain=Draft->PropVolume*GameGain;
        if (Gain>0. && IsValid(PropPreview.Get()) && PropPreview->GetWorld()==World && Elapsed<PropPreviewEndsAt) {
            const double Thrust=.45*FMath::SmoothStep(.35,1.15,Elapsed);
            PropPreview->SetMotorState({Elapsed<PropPreviewSpindownAt,Thrust,Gain,
                UFactoryFPVPropAudioComponent::MaxRpmForAirframe(Draft->FrameSizeCm,Draft->ThrustToWeight)});
            return EActiveTimerReturnType::Continue;
        }
    }
    // Slate removes the executing timer when its callback returns Stop.
    AudioPreviewTimer.Reset();
    StopAudioPreview();
    return EActiveTimerReturnType::Stop;
}

void SFactoryFPVPanel::StopAudioPreview() {
    if (const auto Timer=AudioPreviewTimer.Pin()) UnRegisterActiveTimer(Timer.ToSharedRef());
    AudioPreviewTimer.Reset();
    if (AudioPreviewActivationHandle.IsValid() && FSlateApplication::IsInitialized())
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(AudioPreviewActivationHandle);
    AudioPreviewActivationHandle.Reset();
    if (AudioPreviewWorldHandle.IsValid()) FWorldDelegates::OnWorldBeginTearDown.Remove(AudioPreviewWorldHandle);
    AudioPreviewWorldHandle.Reset();
    if (IsValid(PropPreview.Get())) {
        PropPreview->StopImmediately();
        PropPreview->DestroyComponent();
    }
    PropPreview.Reset();
    if (IsValid(CuePreview.Get())) {
        CuePreview->StopCue();
        CuePreview->DestroyComponent();
    }
    CuePreview.Reset();
}

void SFactoryFPVPanel::AudioPreviewActivationChanged(bool bActive) {
    if (!bActive) StopAudioPreview();
}

void SFactoryFPVPanel::AudioPreviewWorldTearingDown(UWorld* World) {
    if ((IsValid(PropPreview.Get()) && PropPreview->GetWorld()==World) ||
        (IsValid(CuePreview.Get()) && CuePreview->GetWorld()==World)) StopAudioPreview();
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildSimulationPage() {
    auto Battery = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("SIMULATED BATTERY")) ]
        + SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Enable battery endurance simulation"),&Draft->bSimulateBattery) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(TEXT("Pack cells"))]
            + SHorizontalBox::Slot().AutoWidth()[ SNew(SBox).WidthOverride(110)
                [ SNew(SSpinBox<int32>).Value_Lambda([this] { return Draft->BatteryCells; }).MinValue(1).MaxValue(12).Delta(1)
                    .OnValueChanged_Lambda([this](int32 Value) { Draft->BatteryCells=Value; }) ] ] ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Capacity (mAh)"),TEXT("Simulated capacity; no physical radio battery data is read."),&Draft->BatteryCapacityMah,50,50000,50) ]
        + SVerticalBox::Slot().AutoHeight()[ SettingsRow(TEXT("Full-throttle power (W)"),TEXT("Estimated electrical draw for this airframe, not measured hardware power."),&Draft->BatteryMaxPowerWatts,10,5000,10) ]
        + SVerticalBox::Slot().AutoHeight()[ Label(TEXT("SIM BAT is estimated, not radio telemetry. Empty cuts thrust. Recover drone recharges the pack. A new launch starts full. No drain while disarmed or paused. Battery settings are included in tuning presets."),10,Muted) ]
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("CRASH SIMULATION"))]
        + SVerticalBox::Slot().AutoHeight()[ToggleRow(TEXT("Enable airframe crash damage"),&Draft->bCrashDamageEnabled)]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SBox).IsEnabled_Lambda([this] { return Draft->bCrashDamageEnabled; })
            [SettingsRow(TEXT("Impact tolerance (J)"),TEXT("Impact energy that disables the airframe. Higher values withstand harder impacts."),&Draft->CrashToleranceJoules,5,200,5)] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0,6)[Label(TEXT("Optional airframe damage affects the drone, not the pioneer. Gentle contacts under 3 m/s are safe. Use Recover drone after a crash."),10,Muted)];
    auto Access = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight()[ Section(TEXT("GAME ACCESS")) ]
        + SVerticalBox::Slot().AutoHeight()
        [ SNew(SCheckBox).IsChecked_Lambda([this] { return Draft->bAllowSandboxAccess ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this](ECheckBoxState State) { Draft->bAllowSandboxAccess=State==ECheckBoxState::Checked; })
            [ Label(TEXT("Allow sandbox flights without a radio item")) ] ]
        + SVerticalBox::Slot().AutoHeight().Padding(0.f,6.f)
        [ Label(TEXT("When disabled, equip a crafted FPV Radio Kit before flying. The hangar remains available for settings."),10,Muted) ]
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("YOUR PIONEER STAYS BEHIND"))]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Return to pioneer restores control. If the original ground is still loading, movement resumes automatically when it is ready. Recover pioneer becomes available if the original floor is missing."),10,Muted)]
        + SVerticalBox::Slot().AutoHeight()[Section(TEXT("SINGLE PLAYER"))]
        + SVerticalBox::Slot().AutoHeight()[Label(TEXT("Factory FPV is designed for local single-player flight. Battery values are a simulation, not telemetry from your TX16S or a real aircraft."),10,Muted)];
    return Page(TEXT("Simulation & access"),TEXT("Choose optional endurance limits and how drone access fits into your factory."),Columns(Battery,Access));
}

TSharedRef<SWidget> SFactoryFPVPanel::BuildRacingPage() {
    const TWeakObjectPtr<UFactoryFPVSettings> Settings(Draft.Get());
    return SAssignNew(RacingPanel,SFactoryFPVRacePanel).Owner(Owner).Embedded(true)
        .CanLaunchRace(this,&SFactoryFPVPanel::CanFly)
        .OnRaceLaunch(this,&SFactoryFPVPanel::FlyCourse)
        .ShowRaceGhost_Lambda([Settings] {
            const auto* Current=Settings.Get();
            return Current ? Current->bShowRaceGhost : GetDefault<UFactoryFPVSettings>()->bShowRaceGhost;
        })
        .OnShowRaceGhostChanged_Lambda([Settings](bool bShow) {
            if (auto* Current=Settings.Get()) Current->bShowRaceGhost=bShow;
        })
        .AirframeDiameterCm_Lambda([Settings] {
            const auto* Current=Settings.Get();
            return Current ? Current->FrameSizeCm : GetDefault<UFactoryFPVSettings>()->FrameSizeCm;
        });
}

FText SFactoryFPVPanel::FlightSelectionText() const {
    const int32 Course=Owner.IsValid() ? Owner->GetSelectedRacingCourse() : 0;
    if (Course<=0) return FText::FromString(TEXT("Next launch: Free flight"));
    return FText::FromString(FString::Printf(TEXT("Next launch: Course %d"),Course));
}


TSharedRef<SWidget> SFactoryFPVPanel::SettingsRow(const FString& Text, const FString& Help, float* Value,
    float Minimum, float Maximum, float Step) {
    return SNew(SBox).Padding(0.f, 4.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
        [ SNew(STextBlock).Text(FText::FromString(Text)).ToolTipText(FText::FromString(Help)) ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride(110.f)
            [
                SNew(SSpinBox<float>).Value_Lambda([Value] { return *Value; })
                .MinValue(Minimum).MaxValue(Maximum).Delta(Step)
                .MinFractionalDigits(0).MaxFractionalDigits(3)
                .OnValueChanged_Lambda([Value](float NewValue) { *Value = NewValue; })
            ]
        ]
    ];
}

FFactoryFPVRadioAxis SFactoryFPVPanel::Axis(int Index) const {
    const auto Profile = Draft->CurrentProfile();
    const FFactoryFPVRadioAxis Axes[] = {Profile.Roll, Profile.Pitch, Profile.Yaw, Profile.Throttle};
    return Axes[Index];
}

double SFactoryFPVPanel::NormalizedAxis(int Index) const {
    const auto Input = Axis(Index);
    if (!AxisAvailable(Index)) return 0;
    return factoryfpv::Calibration{Input.Range.X, Input.Range.Y, Input.Range.Z, Input.bReverse}
        .axis(Raw[Input.Channel], Index == 3);
}

bool SFactoryFPVPanel::AxisAvailable(int Index) const {
    const int Channel=Axis(Index).Channel;
    return bConnected && Channel>=0 && Channel<8 && RadioAxes[Channel].present;
}

void SFactoryFPVPanel::SetReverse(int Index, bool Reverse) {
    auto Profile = Draft->CurrentProfile();
    FFactoryFPVRadioAxis* Axes[] = {&Profile.Roll, &Profile.Pitch, &Profile.Yaw, &Profile.Throttle};
    Axes[Index]->bReverse = Reverse;
    Draft->SetProfile(Profile);
}

TSharedRef<SWidget> SFactoryFPVPanel::InputRow(int Index) {
    return SNew(SBox).Padding(0.f, 5.f)
    [
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().AutoWidth()
        [ SNew(SBox).WidthOverride(85.f)[ Label(Controls[Index], 9, Muted) ] ]
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(6.f, 0.f)
        [
            SNew(SProgressBar).Percent_Lambda([this, Index] {
                if (!AxisAvailable(Index)) return TOptional<float>(0.f);
                const double Value = NormalizedAxis(Index);
                return TOptional<float>(static_cast<float>(Index == 3 ? Value : (Value + 1.) * 0.5));
            }).FillColorAndOpacity(Cyan)
        ]
        + SHorizontalBox::Slot().AutoWidth()
        [
            SNew(SBox).WidthOverride(50.f)
            [ SNew(STextBlock).Text_Lambda([this, Index] {
                if (!AxisAvailable(Index)) return FText::FromString(TEXT("N/A"));
                return FText::FromString(FString::Printf(TEXT("%.0f%%"), NormalizedAxis(Index) * 100.));
            }).Justification(ETextJustify::Right) ]
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(10.f, 0.f, 0.f, 0.f)
        [
            SNew(SCheckBox).ToolTipText(FText::FromString(TEXT("Invert this calibrated control")))
            .IsEnabled_Lambda([this] { return !Calibrating() && Draft->IsRadioCalibrated(); })
            .IsChecked_Lambda([this, Index] { return Axis(Index).bReverse ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State) { SetReverse(Index, State == ECheckBoxState::Checked); })
            [ Label(TEXT("Invert"), 9, Muted) ]
        ]
    ];
}

void SFactoryFPVPanel::RefreshDevices() {
    if (Calibrating() || ArmCaptureStage>=0) return;
    UpdateDeviceOptions(factoryfpv::WindowsRadio::devices());
    OpenPreview();
}

void SFactoryFPVPanel::UpdateDeviceOptions(const std::vector<factoryfpv::RadioDevice>& Detected) {
    bScannedRadioDevices=true;
    TArray<TSharedPtr<FFactoryFPVDeviceOption>> Options;
    TSharedPtr<FFactoryFPVDeviceOption> ExactSelection;
    for (const auto& Device : Detected) {
        const FString Id(Device.id.c_str()),Name(Device.name.c_str());
        const auto* Existing=Devices.FindByPredicate([&](const auto& Entry) {
            return Entry->Id.Equals(Id,ESearchCase::IgnoreCase) && Entry->Name.Equals(Name,ESearchCase::CaseSensitive);
        });
        auto Option=Existing ? *Existing : MakeShared<FFactoryFPVDeviceOption>(FFactoryFPVDeviceOption{Id,Name});
        Options.Add(Option);
        if (Id.Equals(Draft->RadioDeviceId,ESearchCase::IgnoreCase)) ExactSelection=Option;
    }
    Options.Sort([](const auto& Left,const auto& Right) {
        const int32 ByName=Left->Name.Compare(Right->Name,ESearchCase::IgnoreCase);
        return ByName==0 ? Left->Id.Compare(Right->Id,ESearchCase::IgnoreCase)<0 : ByName<0;
    });
    const bool bOptionsChanged=Devices!=Options;
    if (bOptionsChanged) Devices=MoveTemp(Options);
    SelectedDevice=ExactSelection;
    if (DeviceCombo) {
        if (bOptionsChanged) DeviceCombo->RefreshOptions();
        if (DeviceCombo->GetSelectedItem()!=SelectedDevice) DeviceCombo->SetSelectedItem(SelectedDevice);
    }
}

void SFactoryFPVPanel::ChooseDevice(TSharedPtr<FFactoryFPVDeviceOption> Device, ESelectInfo::Type) {
    if (!Device || SelectedDevice==Device || Calibrating() || ArmCaptureStage>=0) return;
    SelectedDevice = Device;
    Wizard.stage = -1;
    Draft->SelectRadio(Device->Id, Device->Name);
    RadioPreviewError.Reset();
    OpenPreview();
}

void SFactoryFPVPanel::ResetRadioPreview() {
    Radio.reset();
    bConnected = false;
    RadioAxes={};
    RadioButtonsPresent={};
    Raw={};
    Buttons={};
}

void SFactoryFPVPanel::OpenPreview() {
    ResetRadioPreview();
    if (!SelectedDevice) return;
    RadioPreviewError.Reset();
    const auto* PC=Controller.Get();
    const auto* World=PC ? PC->GetWorld() : nullptr;
    auto* Viewport = World ? World->GetGameViewport() : nullptr;
    const auto Window = Viewport ? Viewport->GetWindow() : nullptr;
    const auto Native = Window ? Window->GetNativeWindow() : nullptr;
    if (!Native) {
        RadioPreviewError = TEXT("The game window is unavailable.");
        return;
    }
    Radio = std::make_unique<factoryfpv::WindowsRadio>();
    if (!Radio->open(*SelectedDevice->Id, Native->GetOSWindowHandle(), nullptr) || !Radio->read(Raw,&Buttons)) {
        RadioPreviewError=Radio->error().c_str();
        ResetRadioPreview();
        return;
    }
    RadioAxes=Radio->axisInfo();
    RadioButtonsPresent=Radio->buttonPresence();
    bConnected = true;
}

void SFactoryFPVPanel::Tick(const FGeometry& Geometry, double Time, float DeltaTime) {
    SCompoundWidget::Tick(Geometry, Time, DeltaTime);
    if (ActivePage==EPage::Presentation) UpdateOsdPreview();
    if (!Radio) {
        if (!bConnected && Controller.IsValid() && InputMode==InputModes[0] && Time>=NextRadioScan &&
            !Calibrating() && ArmCaptureStage<0) {
            NextRadioScan=Time+RadioScanInterval;
            RefreshDevices();
        }
        return;
    }
    if (!Radio->read(Raw,&Buttons)) {
        RadioPreviewError=Radio->error().c_str();
        const bool bCaptureInterrupted=Calibrating() || ArmCaptureStage>=0;
        ResetRadioPreview();
        Wizard.stage = -1; ArmCaptureStage=-1;
        NextRadioScan=Time+RadioScanInterval;
        if (bCaptureInterrupted) Message=TEXT("Radio input lost. Unfinished capture cancelled. Previous calibration and tuning are unchanged.");
        return;
    }
    RadioAxes=Radio->axisInfo();
    bConnected = true;
    Wizard.sample(Raw);
}

FReply SFactoryFPVPanel::BeginCalibration() {
    if (!bConnected || !Radio) return FReply::Handled();
    SelectPage(EPage::Radio);
    Wizard.begin(Raw, Radio->axisInfo());
    Message = TEXT("Calibration is temporary until Save settings or Save & fly.");
    return FReply::Handled();
}

FReply SFactoryFPVPanel::CaptureAxis() {
    if (!bConnected || !Calibrating()) return FReply::Handled();
    Wizard.sample(Raw);
    if (!Wizard.capture()) {
        Message = UTF8_TO_TCHAR(Wizard.error.c_str());
        return FReply::Handled();
    }
    if (Wizard.stage == 4) {
        auto Profile = Draft->CurrentProfile();
        FFactoryFPVRadioAxis* Axes[] = {&Profile.Roll, &Profile.Pitch, &Profile.Yaw, &Profile.Throttle};
        for (int Index = 0; Index < 4; ++Index) {
            const auto& Result = Wizard.result[Index];
            Axes[Index]->Channel = Result.channel;
            Axes[Index]->Range = FVector(Result.calibration.minimum, Result.calibration.center, Result.calibration.maximum);
            Axes[Index]->bReverse = Result.calibration.reversed;
        }
        Draft->SetProfile(Profile);
        FString Error;
        if (Draft->AreRadioBindingsValid(Error))
            Message = TEXT("Calibration captured. Check all directions and throttle at 0%, then Save settings.");
        else
            Message = TEXT("Calibration captured in the draft. ")+Error+TEXT(" Clear or relearn the binding before saving.");
    } else Message = TEXT("Axis captured. Release the previous stick and follow the next instruction.");
    return FReply::Handled();
}

FReply SFactoryFPVPanel::RetryAxis() {
    if (!bConnected || !Calibrating()) return FReply::Handled();
    Wizard.retry(Raw);
    Message = TEXT("Neutral captured. Move only the requested axis.");
    return FReply::Handled();
}

FReply SFactoryFPVPanel::CancelCalibration() {
    Wizard.stage = -1; ArmCaptureStage=-1;
    Message = TEXT("Calibration cancelled. Your previous mapping and other hangar edits are unchanged.");
    return FReply::Handled();
}

FText SFactoryFPVPanel::Guidance() const {
    if (Calibrating()) {
        return FText::FromString(FString::Printf(TEXT("STEP %d OF 4   /   %s\n\n%s"),
            Wizard.stage + 1, Controls[Wizard.stage], Directions[Wizard.stage]));
    }
    if (!Draft->IsRadioCalibrated()) {
        return FText::FromString(TEXT("Choose your radio and run calibration before your first radio flight. Your pioneer stays behind while you fly."));
    }
    return FText::FromString(TEXT("Direction check\nRoll right, pitch back and yaw right should read positive. Throttle should read 0% at the bottom and 100% at the top. Use Invert to correct a direction."));
}

FText SFactoryFPVPanel::ConnectionText() const {
    FString Error;
    if (RadioReady(Error)) return FText::FromString(TEXT("CONNECTED   /   CALIBRATED"));
    if (bConnected) return FText::FromString(TEXT("CONNECTED   /   SETUP NEEDED\n")+Error);
    if (!RadioPreviewError.IsEmpty()) Error+=TEXT("\nLast Windows input error: ")+RadioPreviewError;
    return FText::FromString(Error);
}

bool SFactoryFPVPanel::FlightChannelsAvailable(FString& Error) const {
    Error.Reset();
    for (int Index=0;Index<4;++Index) {
        const int Channel=Axis(Index).Channel;
        if (Channel<0 || Channel>=8) {
            Error=FString::Printf(TEXT("%s has no valid USB axis. Run radio calibration."),Controls[Index]);
            return false;
        }
        if (!RadioAxes[Channel].present) {
            Error=FString::Printf(TEXT("Saved %s uses USB axis %s, which this radio is not exposing. Check the radio's USB joystick channels, then Refresh or recalibrate."),Controls[Index],AxisNames[Channel]);
            return false;
        }
    }
    return true;
}

bool SFactoryFPVPanel::RadioReady(FString& Error) const {
    Error.Reset();
    if (!bConnected) {
        if (!bScannedRadioDevices) Error=TEXT("Click Refresh to look for a USB joystick.");
        else if (SelectedDevice) Error=TEXT("Selected radio detected but unreadable. Check USB mode or click Refresh.");
        else if (!Draft->RadioDeviceId.IsEmpty()) Error=TEXT("Saved radio missing. Reconnect it in USB Joystick (HID) mode.");
        else if (!Devices.IsEmpty()) Error=TEXT("Choose your USB controller on the Radio tab.");
        else Error=TEXT("No USB joystick detected. Connect your radio in USB Joystick (HID) mode.");
        return false;
    }
    if (!Draft->IsRadioCalibrated()) {
        Error=TEXT("Calibrate your radio on the Radio tab before flying.");
        return false;
    }
    if (!FlightChannelsAvailable(Error)) return false;
    if (!Draft->AreRadioBindingsValid(Error)) {
        Error+=TEXT(" Clear or rebind the control on the Radio tab.");
        return false;
    }
    return Draft->AreRadioBindingControlsAvailable(RadioAxes,RadioButtonsPresent,Error);
}

FText SFactoryFPVPanel::RadioPreflightText() const {
    if (Calibrating() || ArmCaptureStage>=0) return FText::FromString(TEXT("Finish or cancel radio capture before flying."));
    const auto& Keys=GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts;
    if (InputMode!=InputModes[0]) return FText::FromString(FString::Printf(TEXT("%s arm/disarm   ·   %s recover   ·   %s return   ·   %s hangar"),
        *FShortcutKeys::KeyLabel(Keys.Arm),*FShortcutKeys::KeyLabel(Keys.Recover),*FShortcutKeys::KeyLabel(Keys.Return),*FShortcutKeys::KeyLabel(Keys.Hangar)));
    FString Error;
    if (!RadioReady(Error)) {
        if (bConnected && Draft->IsRadioCalibrated()) Error=TEXT("Check the axis or switch setup on the Radio tab.");
        return FText::FromString(TEXT("Radio / USB · ")+Error);
    }
    const double Throttle=NormalizedAxis(3);
    const bool bLowThrottle=Throttle<=factoryfpv::MaxArmThrottle;
    const auto Arm=Draft->RadioArm.Native();
    if (Arm.kind==0) {
        const FString ArmKey=FShortcutKeys::KeyLabel(Keys.Arm);
        const FString Guidance=bLowThrottle ? FString::Printf(TEXT("%s arms after launch"),*ArmKey) :
            FString::Printf(TEXT("Lower throttle before %s"),*ArmKey);
        return FText::FromString(FString::Printf(TEXT("Radio ready · Throttle %.1f%% · %s"),Throttle*100.,*Guidance));
    }

    const auto Position=Arm.position(Raw,Buttons,true);
    const bool bOff=Position==factoryfpv::ArmPosition::Off;
    const TCHAR* Switch=bOff ? TEXT("OFF") : Position==factoryfpv::ArmPosition::On ? TEXT("ON") :
        Position==factoryfpv::ArmPosition::Middle ? TEXT("MID") : TEXT("UNKNOWN");
    const TCHAR* Guidance=bOff ? (bLowThrottle ? TEXT("Switch ON to arm after launch") : TEXT("Lower throttle before arming")) :
        (bLowThrottle ? TEXT("Switch OFF before arming") : TEXT("Switch OFF and lower throttle"));
    return FText::FromString(FString::Printf(TEXT("Radio ready · Throttle %.1f%% · Arm %s · %s"),Throttle*100.,Switch,Guidance));
}

FName SFactoryFPVPanel::SelectedInputMode() const {
    return InputMode==InputModes[1] ? FName(TEXT("keyboard")) :
        InputMode==InputModes[2] ? FName(TEXT("gamepad")) : FName(TEXT("radio"));
}

bool SFactoryFPVPanel::ValidateFlightInput() {
    if (InputMode==InputModes[2]) {
        FString Error;
        if (AFactoryFPVDrone::CheckGamepadForLaunch(Controller.Get(),Error)) return true;
        Message=MoveTemp(Error);
        return false;
    }
    if (InputMode!=InputModes[0]) return true;
    FString Error;
    if (RadioReady(Error)) return true;
    SelectPage(EPage::Radio);
    // The Radio page and footer already show current readiness. A second
    // launch message would repeat it and remain stale after reconnecting.
    Message.Reset();
    return false;
}

bool SFactoryFPVPanel::ValidateSelectedCourse() {
    auto* Subsystem=Owner.Get();
    const int32 Course=Subsystem ? Subsystem->GetSelectedRacingCourse() : 0;
    if (Course<=0) return true;
    FString Error;
    if (UFactoryFPVRaceComponent::ValidateCourse(Subsystem->GetWorld(),Course,Error,true,&Subsystem->GetSelectedRacingLayout())) return true;
    SelectPage(EPage::Racing);
    Message=FString::Printf(TEXT("Selected course %d is not ready: %s\nChoose Free flight to fly without a course. Your current settings remain open."),Course,*Error);
    return false;
}

void SFactoryFPVPanel::Commit() {
    GetMutableDefault<UFactoryFPVSettings>()->ApplyFrom(*Draft);
    if (auto* Subsystem=Owner.Get()) Subsystem->RefreshHangarShortcut();
    // Compare against the committed state rather than marking every interaction
    // dirty. Editing a value and restoring it is a clean draft again.
    SavedDraft.Reset(DuplicateObject<UFactoryFPVSettings>(Draft.Get(),GetTransientPackage()));
}

FReply SFactoryFPVPanel::SaveSettings() {
    if (Calibrating() || ArmCaptureStage>=0 || !ValidateBindings()) return FReply::Handled();
    Commit();
    Message = TEXT("Settings saved. They will load automatically next time.");
    return FReply::Handled();
}

bool SFactoryFPVPanel::CanFly() const {
    return !Calibrating() && ArmCaptureStage<0 &&
        (Draft->bAllowSandboxAccess || AFactoryFPVRadioEquipment::IsEquippedBy(Controller.Get()));
}

FReply SFactoryFPVPanel::Fly() {
    return FlyCourse(0,false);
}

FReply SFactoryFPVPanel::FlyCourse(int32 CourseId,bool bPractice) {
    if (Calibrating() || ArmCaptureStage>=0) return FReply::Handled();
    if (const auto* PC=Controller.Get(); PC && FactoryFPVInteractionState::BlocksPlayerInteraction(PC)) {
        Message=TEXT("Close the game menu or resume the game before launching FPV. Your settings are still open.");
        return FReply::Handled();
    }
    if (RacingPanel.IsValid() && RacingPanel->HasPendingGateEdits()) {
        SelectPage(EPage::Racing);
        Message=TEXT("Apply or revert your gate edits on the Racing tab before flying. Tuning and gate drafts are still open.");
        return FReply::Handled();
    }
    if (!ValidateBindings() || !ValidateFlightInput()) return FReply::Handled();
    if (!Controller.IsValid() || !Owner.IsValid()) return FReply::Handled();
    auto* Subsystem=Owner.Get();
    if (!bPractice) {
        if (CourseId>0) Subsystem->SetSelectedRacingCourse(CourseId);
        // A queued course may lose power or gates after selection. Keep the
        // draft open on that error. Practice already checked its viewed course.
        if (!ValidateSelectedCourse()) return FReply::Handled();
    }
    const auto KeepAlive = SharedThis(this);
    Commit();
    Radio.reset();
    auto* PC = Controller.Get();
    const FName Mode=SelectedInputMode();
    Subsystem->ClosePanel();
    FString Error;
    if (!AFactoryFPVDrone::Launch(PC, Mode, Error, bPractice ? CourseId : 0)) Subsystem->OpenPanel(Error, Mode);
    return FReply::Handled();
}

FReply SFactoryFPVPanel::Close() {
    const auto KeepAlive=SharedThis(this);
    if (auto* Subsystem=Owner.Get()) Subsystem->RequestClosePanel();
    return FReply::Handled();
}

FReply SFactoryFPVPanel::OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) {
    if (Event.GetKey()==EKeys::Escape || Event.GetKey()==GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts.Hangar)
        return Event.IsRepeat() ? FReply::Handled() : Close();
    return SCompoundWidget::OnKeyDown(Geometry, Event);
}


FFactoryFPVArmBinding& SFactoryFPVPanel::ControlBinding(ERadioControl ControlId) {
    switch (ControlId) {
        case ERadioControl::Return: return Draft->RadioReturn;
        case ERadioControl::Recover: return Draft->RadioRecover;
        case ERadioControl::Mode: return Draft->RadioMode;
        default: return Draft->RadioArm;
    }
}

TSharedRef<SWidget> SFactoryFPVPanel::BindingRow(ERadioControl ControlId,const FString& Title) {
    return SNew(SBox).Padding(0.f,6.f)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)[Label(Title,11)]
                + SHorizontalBox::Slot().AutoWidth().Padding(5.f,0.f)
                [ SNew(SButton).Text_Lambda([this,ControlId] {
                    return FText::FromString(ArmCaptureStage>=0 && CaptureTarget==ControlId ? (ArmCaptureStage==0 ? TEXT("Capture OFF") : TEXT("Capture ON")) : TEXT("Bind"));
                  }).IsEnabled_Lambda([this,ControlId] { return bConnected && Draft->IsRadioCalibrated() && !Calibrating() && (ArmCaptureStage<0 || CaptureTarget==ControlId); })
                    .OnClicked(this,&SFactoryFPVPanel::CaptureRadioControl,ControlId) ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).Text(FText::FromString(TEXT("Cancel")))
                    .Visibility_Lambda([this,ControlId] { return ArmCaptureStage>=0 && CaptureTarget==ControlId ? EVisibility::Visible : EVisibility::Collapsed; })
                    .OnClicked(this,&SFactoryFPVPanel::CancelArmControl) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5.f,0.f,0.f,0.f)
                [ SNew(SButton).Text(FText::FromString(TEXT("Clear")))
                    .IsEnabled_Lambda([this,ControlId] { return ArmCaptureStage<0 && !Calibrating() && ControlBinding(ControlId).Kind!=0; })
                    .OnClicked(this,&SFactoryFPVPanel::ClearRadioControl,ControlId) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.f,3.f,0.f,0.f)
            [ SNew(STextBlock).Text(this,&SFactoryFPVPanel::BindingStatus,ControlId).AutoWrapText(true)
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),9)).ColorAndOpacity(Cyan) ] ];
}

FReply SFactoryFPVPanel::CaptureArmControl() { return CaptureRadioControl(ERadioControl::Arm); }

FReply SFactoryFPVPanel::CaptureRadioControl(ERadioControl ControlId) {
    if (!bConnected || Calibrating() || !Draft->IsRadioCalibrated()) return FReply::Handled();
    if (ArmCaptureStage>=0 && CaptureTarget!=ControlId) return FReply::Handled();
    SelectPage(EPage::Radio);
    if (ArmCaptureStage<0) {
        CaptureTarget=ControlId;
        ArmCaptureStage=0;
        Message=TEXT("Put your chosen switch/button in its OFF position, then click Capture OFF. Keep all other controls still.");
    } else if (ArmCaptureStage==0) {
        ArmOffAxes=Raw; ArmOffButtons=Buttons; ArmCaptureStage=1;
        Message=TEXT("Move only that switch/button to ON, then click Capture ON.");
    } else {
        const std::array<int,4> FlightAxes{Draft->RadioRollAxis,Draft->RadioPitchAxis,Draft->RadioYawAxis,Draft->RadioThrottleAxis};
        const auto& Info=RadioAxes;
        factoryfpv::ArmBinding Result;
        if (!factoryfpv::learnArmBinding(ArmOffAxes,ArmOffButtons,Raw,Buttons,Info,FlightAxes,Result)) {
            Message=TEXT("No single switch detected. Cancel and retry, changing only one USB button or unused axis. Your radio must expose the control over USB; flight axes cannot be used.");
            return FReply::Handled();
        }
        auto& Target=ControlBinding(ControlId);
        const auto Previous=Target;
        Target.Kind=Result.kind; Target.Channel=Result.index;
        Target.Off=static_cast<int32>(Result.off); Target.On=static_cast<int32>(Result.on);
        FString Error;
        if (!Draft->AreRadioBindingsValid(Error)) {
            Target=Previous;
            Message=Error+TEXT(" The previous binding is unchanged. Cancel and choose another control.");
            return FReply::Handled();
        }
        Draft->SetProfile(Draft->CurrentProfile());
        ArmCaptureStage=-1;
        Message=TEXT("Control captured. Check its OFF/ON state, then Save. Start each flight OFF. Actions need a fresh OFF to ON cycle after pause, reset or radio loss.");
    }
    return FReply::Handled();
}

FReply SFactoryFPVPanel::ClearRadioControl(ERadioControl ControlId) {
    if (Calibrating() || ArmCaptureStage>=0) return FReply::Handled();
    ControlBinding(ControlId)={};
    Draft->SetProfile(Draft->CurrentProfile());
    Message=TEXT("Binding cleared in this draft. Keyboard shortcuts remain available. Save settings to keep the change.");
    return FReply::Handled();
}

FReply SFactoryFPVPanel::CancelArmControl() {
    ArmCaptureStage=-1;
    Message=TEXT("Control setup cancelled; the previous binding is unchanged.");
    return FReply::Handled();
}

FText SFactoryFPVPanel::BindingStatus(ERadioControl ControlId) const {
    if (ArmCaptureStage>=0 && CaptureTarget==ControlId)
        return FText::FromString(ArmCaptureStage==0 ? TEXT("Set switch OFF, then capture.") : TEXT("Set switch ON, then capture."));
    const FFactoryFPVArmBinding* Bindings[]{&Draft->RadioArm,&Draft->RadioReturn,&Draft->RadioRecover,&Draft->RadioMode};
    const auto Binding=Bindings[static_cast<uint8>(ControlId)]->Native();
    if (Binding.kind==0) {
        if (ControlId==ERadioControl::Mode) return FText::FromString(TEXT("Not bound / use Flight tab"));
        const auto& Keys=GetDefault<UFactoryFPVSettings>()->KeyboardShortcuts;
        const FKey& Fallback=ControlId==ERadioControl::Arm ? Keys.Arm : ControlId==ERadioControl::Return ? Keys.Return : Keys.Recover;
        return FText::FromString(TEXT("Not bound / ")+FShortcutKeys::KeyLabel(Fallback));
    }
    const auto Position=Binding.position(Raw,Buttons,bConnected && Binding.available(RadioAxes,RadioButtonsPresent));
    const TCHAR* State=Position==factoryfpv::ArmPosition::On ? TEXT("ON") : (Position==factoryfpv::ArmPosition::Off ? TEXT("OFF") : (Position==factoryfpv::ArmPosition::Middle ? TEXT("MIDDLE") : TEXT("UNAVAILABLE")));
    const FString Control=Binding.kind==1 ? FString::Printf(TEXT("Button %d"),Binding.index+1) :
        (Binding.valid() ? FString::Printf(TEXT("Axis %s"),AxisNames[Binding.index]) : TEXT("Invalid binding"));
    return FText::FromString(FString::Printf(TEXT("%s   /   %s"),*Control,State));
}

FText SFactoryFPVPanel::ArmStatus() const {
    return FText::FromString(BindingStatus(ERadioControl::Arm).ToString()+TEXT("\nLow throttle + OFF to ON arms. OFF disarms. A new OFF cycle is required after pause, reset or failsafe."));
}

bool SFactoryFPVPanel::ValidateBindings() {
    FString Error;
    if (Draft->AreEditedRadioBindingsValid(*SavedDraft,Error)) return true;
    SelectPage(EPage::Radio);
    Message=Error;
    return false;
}



void SFactoryFPVPanel::RefreshTuningOptions() {
    TuningOptions.Reset(); SelectedTuning.Reset();
    for (const auto& Preset:UFactoryFPVSettings::BuiltInTunings()) {
        auto Option=MakeShared<FFactoryFPVTuningOption>(); Option->Preset=Preset; Option->bBuiltIn=true; TuningOptions.Add(Option);
    }
    for (const auto& Preset:Draft->TuningPresets) {
        auto Option=MakeShared<FFactoryFPVTuningOption>(); Option->Preset=Preset; TuningOptions.Add(Option);
    }
    if (TuningCombo) { TuningCombo->ClearSelection(); TuningCombo->RefreshOptions(); }
}
FReply SFactoryFPVPanel::ApplySelectedTuning() {
    if (!SelectedTuning) return FReply::Handled();
    Draft->ApplyTuning(SelectedTuning->Preset);
    TuningName=(SelectedTuning->bBuiltIn ? TEXT("My ") : TEXT(""))+SelectedTuning->Preset.Name;
    Message=TEXT("Preset applied to draft. Save settings or Save & fly commits it. Radio mapping is unchanged.");
    return FReply::Handled();
}
FReply SFactoryFPVPanel::SaveTuning() {
    if (!Draft->StoreTuningPreset(TuningName)) {
        Message=TEXT("Use a unique custom name (not a built-in name), up to 40 characters. At most 16 custom presets are supported.");
        return FReply::Handled();
    }
    RefreshTuningOptions();
    Message=TEXT("Tuning stored in draft. Save settings commits it; closing the hangar discards it. An existing custom name replaces that preset.");
    return FReply::Handled();
}
FReply SFactoryFPVPanel::DeleteTuning() {
    if (SelectedTuning && !SelectedTuning->bBuiltIn) {
        Draft->RemoveTuningPreset(SelectedTuning->Preset.Name);
        RefreshTuningOptions(); Message=TEXT("Preset removed from draft. Save settings commits the deletion; closing discards it.");
    }
    return FReply::Handled();
}

AFactoryFPVPioneerAnchor* SFactoryFPVPanel::PendingRecovery() const {
    auto* PC=Controller.Get();
    return PC ? AFactoryFPVPioneerAnchor::FindPending(PC->GetWorld(),Cast<ACharacter>(PC->GetPawn())) : nullptr;
}
FReply SFactoryFPVPanel::RecoverPioneer() {
    if (auto* Anchor=PendingRecovery()) {
        FString Error;
        Message=Anchor->RecoverToNearbyFloor(Error) ? TEXT("Pioneer moved onto verified nearby ground. Movement resumes when streaming is ready; close the hangar to walk.") : Error;
    } else Message=TEXT("Pioneer recovery is no longer needed.");
    return FReply::Handled();
}
TSharedRef<SWidget> SFactoryFPVPanel::ToggleRow(const FString& Text,bool* Value) {
    return SNew(SBox).Padding(0.f,4.f)
        [ SNew(SCheckBox).IsChecked_Lambda([Value] { return *Value ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([Value](ECheckBoxState State) { *Value=State==ECheckBoxState::Checked; })[Label(Text)] ];
}
