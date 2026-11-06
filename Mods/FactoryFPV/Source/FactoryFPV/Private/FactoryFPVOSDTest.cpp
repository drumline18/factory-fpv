#include "FactoryFPVOSD.h"
#include "Math/Box2D.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Interfaces/IPluginManager.h"
#include "Slate/WidgetRenderer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVOSDPreferencesTest,
    "FactoryFPV.Presentation.OsdPreferences",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVOSDPreferencesTest::RunTest(const FString&) {
    auto Osd=SNew(SFactoryFPVOSD);
    FFactoryFPVOSDTelemetry Data;
    Data.bBatteryLow=Data.bBatteryCritical=true;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Disabled battery does not invent a warning"),Osd->WarningText(),FString(TEXT("DISARMED")));
    Data.bBatteryEnabled=true;
    Data.BatteryPercent=9.5; Data.BatteryVolts=13.2;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Critical simulated pack outranks low warning"),Osd->WarningText(),FString(TEXT("SIM BAT CRITICAL - LAND")));
    TestTrue(TEXT("Pack telemetry is explicitly labeled simulated"),Osd->BatteryText().StartsWith(TEXT("SIM BAT ")));
    Data.Warning=TEXT("Disarmed by pilot");
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Session notices retain precedence"),Osd->WarningText(),FString(TEXT("DISARMED BY PILOT")));
    Data.bConnected=false;Data.DisplaySeconds=0;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Receiver loss outranks all other warnings"),Osd->WarningText(),FString(TEXT("RX LOSS")));
    Data.Receiver=TEXT("PAD");
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Gamepad loss gives mode-specific return guidance"),Osd->WarningText(),FString(TEXT("PAD LOST - F10 RETURN")));
    TestTrue(TEXT("Gamepad footer asks for a fresh launch"),Osd->ControlsText().Contains(TEXT("RECONNECT GAMEPAD AND RELAUNCH")));
    Data.Receiver=TEXT("USB");
    Osd->SetTelemetry(Data);
    TestTrue(TEXT("Radio loss keeps its own reconnect hint"),Osd->ControlsText().Contains(TEXT("RECONNECT RADIO")));
    FFactoryFPVKeyboardShortcuts Shortcuts;
    Shortcuts.Hangar=EKeys::F5;
    Shortcuts.Arm=EKeys::F7;
    Shortcuts.Recover=EKeys::F4;
    Shortcuts.Return=EKeys::F12;
    Shortcuts.Normalize();
    const auto CustomOsd=SNew(SFactoryFPVOSD).Shortcuts(Shortcuts);
    CustomOsd->SetTelemetry(Data);
    TestEqual(TEXT("Receiver loss uses the configured exit and hangar shortcuts"),CustomOsd->ControlsText(),
        FString(TEXT("F12 EXIT   RECONNECT RADIO   F5 HANGAR")));
    CustomOsd->SetTelemetry(FFactoryFPVOSDTelemetry());
    TestEqual(TEXT("Flight footer displays every configured shortcut"),CustomOsd->ControlsText(),
        FString(TEXT("F7 ARM   F4 RESET   F12 EXIT   F5 HANGAR")));
    Shortcuts.Return=Shortcuts.Arm;
    Shortcuts.Normalize();
    TestTrue(TEXT("Conflicting file settings restore a valid return shortcut"),Shortcuts.Return==EKeys::F10);
    Data.TextScale=100;Data.Opacity=0;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Font scale is bounded for safe margins"),Osd->FontScale(),1.3f);
    TestEqual(TEXT("Telemetry cannot be made wholly invisible"),Osd->TelemetryAlpha(),.3f);
    Data.TextScale=std::numeric_limits<float>::quiet_NaN();Data.Opacity=std::numeric_limits<float>::infinity();
    Data.BatteryVolts=std::numeric_limits<double>::quiet_NaN();
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Invalid scale uses readable default"),Osd->FontScale(),1.f);
    TestEqual(TEXT("Invalid opacity uses readable default"),Osd->TelemetryAlpha(),1.f);
    TestEqual(TEXT("Invalid battery values are unavailable rather than fake voltage"),Osd->BatteryText(),FString(TEXT("SIM BAT --")));
    Data.bConnected=true;Data.bRadioArmBound=true;Data.Warning.Reset();Data.bBatteryEnabled=false;
    Data.bDamageEnabled=false;Data.IntegrityPercent=0;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Disabled damage has no frame warning"),Osd->WarningText(),FString(TEXT("DISARMED")));
    TestTrue(TEXT("Bound radio arm retains the valid F9 shortcut"),Osd->ControlsText().Contains(TEXT("F9 RESET")));
    Data.bDamageEnabled=true;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Destruction gives explicit recovery guidance"),Osd->WarningText(),FString(TEXT("FRAME LOST - RECOVER OR EXIT")));
    Data.IntegrityPercent=24;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Damaged frame has persistent health readout"),Osd->IntegrityText(),FString(TEXT("FRAME 24%")));
    TestEqual(TEXT("Low health warns pilot"),Osd->WarningText(),FString(TEXT("FRAME DAMAGED - LAND")));
    Data.bRacing=true;Data.RaceGate=2;Data.RaceGateCount=3;
    Osd->SetTelemetry(Data);
    TestFalse(TEXT("Racing does not invent an unavailable checkpoint target"),Osd->HasRaceTarget());
    Data.bRaceTargetAvailable=true;Data.RaceTargetDistanceM=123.4;Data.RaceTargetInstruction=TEXT("pass left");
    Osd->SetTelemetry(Data);
    TestTrue(TEXT("An actual valid checkpoint enables navigation"),Osd->HasRaceTarget());
    TestFalse(TEXT("A visible checkpoint omits the viewport-edge cue"),Osd->HasRaceEdgeCue());
    auto Offscreen=Data;
    Offscreen.RaceOffscreenDirection=FVector2D(1.,0.);
    Osd->SetTelemetry(Offscreen);
    TestTrue(TEXT("An off-screen checkpoint enables the viewport-edge cue"),Osd->HasRaceEdgeCue());
    Offscreen.bConnected=false;
    Osd->SetTelemetry(Offscreen);
    TestFalse(TEXT("Receiver loss suppresses the viewport-edge cue"),Osd->HasRaceEdgeCue());
    Offscreen.bConnected=true;Offscreen.bRaceTargetAvailable=false;
    Osd->SetTelemetry(Offscreen);
    TestFalse(TEXT("An unavailable checkpoint suppresses a stale edge cue"),Osd->HasRaceEdgeCue());
    Offscreen.bRaceTargetAvailable=true;Offscreen.bRacing=false;
    Osd->SetTelemetry(Offscreen);
    TestFalse(TEXT("Free flight suppresses a stale edge cue"),Osd->HasRaceEdgeCue());
    Offscreen.bRacing=true;Offscreen.bSetup=true;
    Osd->SetTelemetry(Offscreen);
    TestFalse(TEXT("Radio setup suppresses the viewport-edge cue"),Osd->HasRaceEdgeCue());
    Offscreen.bSetup=false;Offscreen.RaceOffscreenDirection.Y=std::numeric_limits<double>::infinity();
    Osd->SetTelemetry(Offscreen);
    TestFalse(TEXT("A nonfinite direction suppresses the viewport-edge cue"),Osd->HasRaceEdgeCue());
    Osd->SetTelemetry(Data);
    TestTrue(TEXT("Ahead is the upward compass direction"),Osd->RaceTargetDirection().Equals(FVector2D(0,-1),1.e-6));
    TestEqual(TEXT("Navigation identifies next checkpoint, course length and actual distance"),Osd->RaceTargetText(),FString(TEXT("NEXT 2/3  /  123 M")));
    TestFalse(TEXT("The timing summary does not repeat the live checkpoint"),Osd->RaceText().Contains(TEXT("GATE")));
    auto Practice=Data;
    Practice.RaceCourse=7;Practice.bRacePractice=true;
    Osd->SetTelemetry(Practice);
    TestTrue(TEXT("Practice identifies itself in the timing summary"),Osd->RaceText().StartsWith(TEXT("PRACTICE 7")));
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("Pole instruction is plain uppercase navigation"),Osd->RaceTargetGuidance(),FString(TEXT("PASS LEFT")));
    Data.RaceTargetBearingDegrees=90;Data.RaceTargetElevationDegrees=20;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Right target points right"),Osd->RaceTargetDirection().Equals(FVector2D(1,0),1.e-6));
    TestEqual(TEXT("Above guidance does not imply altitude hold"),Osd->RaceTargetGuidance(),FString(TEXT("PASS LEFT  /  ABOVE")));
    Data.RaceTargetBearingDegrees=-90;Data.RaceTargetElevationDegrees=-20;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Left target points left"),Osd->RaceTargetDirection().Equals(FVector2D(-1,0),1.e-6));
    TestTrue(TEXT("Below cue accompanies lower target"),Osd->RaceTargetGuidance().EndsWith(TEXT("BELOW")));
    Data.bRaceTargetVerticalGate=true;Data.RaceTargetHeightOffsetM=6.4;
    Data.RaceTargetInstruction=TEXT("ascend");Osd->SetTelemetry(Data);
    TestEqual(TEXT("An ascent instruction is independent of camera tilt"),Osd->RaceTargetGuidance(),FString(TEXT("ASCEND")));
    TestEqual(TEXT("A vertical opening shows its true height even when it looks lower through the camera"),
        Osd->RaceTargetText(),FString(TEXT("NEXT 2/3 / 123 M / 6 M ABOVE")));
    Data.RaceTargetInstruction=TEXT("DESCEND");Data.RaceTargetElevationDegrees=20;Osd->SetTelemetry(Data);
    TestEqual(TEXT("A descent instruction is independent of camera tilt"),Osd->RaceTargetGuidance(),FString(TEXT("DESCEND")));
    Data.RaceTargetHeightOffsetM=-5.6;
    Data.bRaceTargetOnExitSide=true;Data.RaceTargetInstruction=TEXT("ENTER FROM BELOW");Osd->SetTelemetry(Data);
    TestEqual(TEXT("Wrong-face guidance never appends a contradictory camera elevation"),Osd->RaceTargetGuidance(),FString(TEXT("ENTER FROM BELOW")));
    TestEqual(TEXT("Wrong-face guidance keeps the actual range and true gate height"),
        Osd->RaceTargetText(),FString(TEXT("NEXT 2/3 / 123 M / 6 M BELOW")));
    Data.RaceTargetHeightOffsetM=.3;Data.RaceTargetElevationDegrees=80;Osd->SetTelemetry(Data);
    TestEqual(TEXT("A nearly level vertical opening omits the height cue"),Osd->RaceTargetText(),FString(TEXT("NEXT 2/3 / 123 M")));
    Data.bRaceTargetOnExitSide=false;Data.bRaceTargetVerticalGate=false;Data.RaceTargetElevationDegrees=20;
    Data.RaceTargetBearingDegrees=180;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Behind target points down the compass"),Osd->RaceTargetDirection().Equals(FVector2D(0,1),1.e-6));
    TestTrue(TEXT("Behind target is explicitly labeled"),Osd->RaceTargetText().Contains(TEXT("BEHIND")));
    Data.RaceTargetElevationDegrees=-89;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Near-vertical lower target points down and is labeled BELOW even with rear azimuth"),
        Osd->RaceTargetDirection().Equals(FVector2D(0,1),1.e-6) && Osd->RaceTargetText().EndsWith(TEXT("BELOW")));
    Data.RaceTargetBearingDegrees=810;Data.RaceTargetElevationDegrees=0;Data.RaceTargetDistanceM=1.e300;
    Data.RaceTargetInstruction=TEXT("pass right\r\nnow\t");Osd->SetTelemetry(Data);
    TestTrue(TEXT("Finite multi-turn bearing wraps safely"),Osd->RaceTargetDirection().Equals(FVector2D(1,0),1.e-6));
    TestTrue(TEXT("Distance is bounded for readable navigation"),Osd->RaceTargetText().Contains(TEXT("999999 M")));
    Data.RaceTargetElevationDegrees=1.e300;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Extreme elevation gives a stable overhead cue"),Osd->RaceTargetText().EndsWith(TEXT("ABOVE")) &&
        Osd->RaceTargetDirection().Equals(FVector2D(0,-1),1.e-6));
    TestFalse(TEXT("Instruction cannot inject new navigation rows"),Osd->RaceTargetGuidance().Contains(TEXT("\n")));
    Data.RaceTargetBearingDegrees=1.e300;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Huge finite bearing remains a finite unit direction"),FMath::IsFinite(Osd->RaceTargetDirection().X) && FMath::IsNearlyEqual(Osd->RaceTargetDirection().Size(),1.,1.e-6));
    Data.RaceTargetBearingDegrees=std::numeric_limits<double>::quiet_NaN();Osd->SetTelemetry(Data);
    TestFalse(TEXT("Nonfinite bearing hides navigation instead of fabricating direction"),Osd->HasRaceTarget());
    Data.RaceTargetBearingDegrees=0;Data.RaceTargetElevationDegrees=std::numeric_limits<double>::infinity();Osd->SetTelemetry(Data);
    TestFalse(TEXT("Nonfinite elevation hides navigation"),Osd->HasRaceTarget());
    Data.RaceTargetElevationDegrees=0;Data.RaceTargetDistanceM=-1;Osd->SetTelemetry(Data);
    TestFalse(TEXT("Negative distance is unavailable"),Osd->HasRaceTarget());
    Data.RaceTargetDistanceM=std::numeric_limits<double>::infinity();Osd->SetTelemetry(Data);
    TestEqual(TEXT("Invalid target never displays a fake distance"),Osd->RaceTargetText(),FString());
    Data.RaceTargetDistanceM=1;Data.RaceGate=0;Osd->SetTelemetry(Data);
    TestFalse(TEXT("No ordered checkpoint means no target navigation"),Osd->HasRaceTarget());
    Data.RaceGate=2;Data.bRacing=false;Osd->SetTelemetry(Data);
    TestFalse(TEXT("Free flight cannot leak a previous racing target"),Osd->HasRaceTarget());
    Data.bRacing=true;Data.bSetup=true;Osd->SetTelemetry(Data);
    TestFalse(TEXT("Radio setup does not display racing navigation"),Osd->HasRaceTarget());
    Data.bSetup=false;Data.bArmed=true;Data.RaceLast=59.999;Data.RaceBest=59.994;
    Data.RaceFinish=EFactoryFPVRaceFinish::LapComplete;Osd->SetTelemetry(Data);
    TestEqual(TEXT("Lap completion rolls rounded seconds into the next minute"),Osd->RaceStatusText(),FString(TEXT("LAP COMPLETE  01:00.00")));
    TestTrue(TEXT("Last and best times share the same correctly rounded format"),Osd->RaceText().Contains(TEXT("LAST 01:00.00  /  BEST 00:59.99")));
    Data.bRaceGhostActive=true;Data.bConnected=true;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Active replay is marked on the existing best-time line"),Osd->RaceText().EndsWith(TEXT("BEST 00:59.99  /  GHOST")));
    Data.bConnected=false;Osd->SetTelemetry(Data);
    TestFalse(TEXT("Receiver loss hides the replay marker"),Osd->RaceText().Contains(TEXT("GHOST")));
    Data.bRaceGhostActive=false;Data.bConnected=true;
    Data.RaceFinish=EFactoryFPVRaceFinish::CourseBest;Osd->SetTelemetry(Data);
    TestEqual(TEXT("A saved course record has a distinct notice"),Osd->RaceStatusText(),FString(TEXT("NEW COURSE BEST  01:00.00")));
    Data.RaceStatus=TEXT("COURSE POWER LOST");Osd->SetTelemetry(Data);
    TestEqual(TEXT("Course fault outranks the finish notice"),Osd->RaceStatusText(),Data.RaceStatus);
    Data.RaceStatus.Reset();Data.bArmed=false;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Disarmed display cannot resurrect a finish notice"),Osd->RaceStatusText().IsEmpty());
    Data.bArmed=true;Data.bConnected=false;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Receiver loss suppresses the finish notice"),Osd->RaceStatusText().IsEmpty());
    Data.bConnected=true;Data.RaceLast=std::numeric_limits<double>::infinity();Osd->SetTelemetry(Data);
    TestTrue(TEXT("Invalid completed time cannot be announced"),Osd->RaceStatusText().IsEmpty());

    Data.bDamageEnabled=false;Data.RaceFinish=EFactoryFPVRaceFinish::None;
    Data.RaceSplitGate=2;Data.RaceSplitSeconds=12.34;
    Osd->SetTelemetry(Data);
    TestEqual(TEXT("A split without a reference displays only the actual elapsed time"),Osd->RaceStatusText(),FString(TEXT("GATE 2  /  00:12.34")));
    TestTrue(TEXT("An elapsed-only split is white"),Osd->RaceStatusTint()==FLinearColor::White);
    Data.bRaceSplitHasDelta=true;Data.RaceSplitDelta=-.42;Osd->SetTelemetry(Data);
    TestEqual(TEXT("A faster checkpoint has a negative PB comparison"),Osd->RaceStatusText(),FString(TEXT("GATE 2  /  00:12.34  /  -0.42S PB")));
    const FLinearColor FasterTint=Osd->RaceStatusTint();
    TestTrue(TEXT("A faster split is mint"),FasterTint.G>FasterTint.B && FasterTint.B>FasterTint.R);
    Data.RaceSplitDelta=.42;Osd->SetTelemetry(Data);
    TestEqual(TEXT("A slower checkpoint has a positive PB comparison"),Osd->RaceStatusText(),FString(TEXT("GATE 2  /  00:12.34  /  +0.42S PB")));
    const FLinearColor SlowerTint=Osd->RaceStatusTint();
    TestTrue(TEXT("A slower split is amber"),SlowerTint.R>SlowerTint.G && SlowerTint.G>SlowerTint.B);
    Data.RaceSplitDelta=-.001;Osd->SetTelemetry(Data);
    TestTrue(TEXT("A delta rounded to zero is white and has no sign"),Osd->RaceStatusTint()==FLinearColor::White &&
        Osd->RaceStatusText().EndsWith(TEXT("  /  0.00S PB")));
    Data.RaceSplitDelta=std::numeric_limits<double>::quiet_NaN();Osd->SetTelemetry(Data);
    TestEqual(TEXT("A nonfinite reference delta falls back to elapsed"),Osd->RaceStatusText(),FString(TEXT("GATE 2  /  00:12.34")));
    Data.RaceFinish=EFactoryFPVRaceFinish::LapComplete;Data.RaceLast=19.25;Osd->SetTelemetry(Data);
    TestEqual(TEXT("Lap finish outranks checkpoint split"),Osd->RaceStatusText(),FString(TEXT("LAP COMPLETE  00:19.25")));
    Data.RaceStatus=TEXT("COURSE POWER LOST");Osd->SetTelemetry(Data);
    TestEqual(TEXT("A course fault outranks both timing notices"),Osd->RaceStatusText(),Data.RaceStatus);
    Data.RaceStatus=TEXT("WRONG WAY - ENTER FROM ABOVE");Data.bRaceWrongWay=true;Osd->SetTelemetry(Data);
    TestEqual(TEXT("Wrong-way guidance outranks both timing notices"),Osd->RaceStatusText(),Data.RaceStatus);
    Data.RaceStatus.Reset();Data.bRaceWrongWay=false;Data.RaceFinish=EFactoryFPVRaceFinish::None;
    Data.Warning=TEXT("RETURN TO PIONEER");Osd->SetTelemetry(Data);
    TestTrue(TEXT("An active safety warning suppresses the split"),Osd->RaceStatusText().IsEmpty());
    Data.Warning.Reset();Data.bArmed=false;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Disarm suppresses the split"),Osd->RaceStatusText().IsEmpty());
    Data.bArmed=true;Data.bConnected=false;Osd->SetTelemetry(Data);
    TestTrue(TEXT("Receiver loss suppresses the split"),Osd->RaceStatusText().IsEmpty());
    Data.bConnected=true;Data.RaceSplitGate=0;Osd->SetTelemetry(Data);
    TestTrue(TEXT("An expired split leaves the race status empty"),Osd->RaceStatusText().IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVOSDRenderTest,
    "FactoryFPV.Presentation.OsdRender",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVOSDRenderTest::RunTest(const FString&) {
    if (!FApp::CanEverRender()) {
        AddInfo(TEXT("OSD visual review requires an RHI; skipped under NullRHI."));
        return true;
    }
    const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("FactoryFPV"));
    if (!TestTrue(TEXT("Packaged OSD font source exists"),Plugin.IsValid() && FPaths::FileExists(Plugin->GetContentDir()/TEXT("Fonts/VT323-Regular.ttf")))) return false;
    auto Osd=SNew(SFactoryFPVOSD);
    // A simple light/dark background checks outline contrast. This is an actual
    // Slate rendering of the production widget, not an in-game screenshot.
    auto Root=SNew(SOverlay)
        + SOverlay::Slot()
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().FillHeight(.55)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(.48f,.55f,.58f)) ]
            + SVerticalBox::Slot().FillHeight(.45)
            [ SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(.12f,.19f,.1f)) ]
        ]
        + SOverlay::Slot()[ Osd ];
    FFactoryFPVOSDTelemetry Data;
    Data.bArmed=true;
    Data.Throttle=.31;
    Data.SpeedMps=16.8;
    Data.RelativeAltitudeM=43;
    Data.HomeDistanceM=987;
    Data.ArmedSeconds=83;
    FWidgetRenderer Renderer(true);
    int32 ArrowPinkPixels=0,CheckpointPinkPixels=0,ManeuverWhitePixels=0,ManeuverAmberPixels=0,RaceStatusMintPixels=0,WrongWayAmberPixels=0;
    FIntRect EdgeRegion(0,0,0,0);
    int32 EdgePinkPixels=0,EdgeAmberPixels=0;
    int32 CenterTextPixels=0;
    const auto IsPink=[](const FColor& Pixel) { return int32(Pixel.R)>int32(Pixel.B)+20 && int32(Pixel.B)>int32(Pixel.G)+20; };
    const auto IsAmber=[](const FColor& Pixel) { return int32(Pixel.R)>int32(Pixel.G)+20 && int32(Pixel.G)>int32(Pixel.B)+60; };
    auto Capture=[&](const TCHAR* Name,int32 Width,int32 Height) {
        Osd->SetTelemetry(Data);
        auto* Target=FWidgetRenderer::CreateTargetFor(FVector2D(Width,Height),TF_Bilinear,false);
        if (!Target) return false;
        Renderer.DrawWidget(Target,Root,FVector2D(Width,Height),0.f,false);
        FlushRenderingCommands();
        Renderer.DrawWidget(Target,Root,FVector2D(Width,Height),1.f/60.f,false);
        FlushRenderingCommands();
        TArray<FColor> Pixels;
        FReadSurfaceDataFlags Flags;
        Flags.SetLinearToGamma(false);
        if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,Flags)) return false;
        ArrowPinkPixels=CheckpointPinkPixels=ManeuverWhitePixels=ManeuverAmberPixels=RaceStatusMintPixels=WrongWayAmberPixels=0;
        EdgePinkPixels=EdgeAmberPixels=0;
        for (int32 Y=EdgeRegion.Min.Y;Y<FMath::Min(Height,EdgeRegion.Max.Y);++Y) {
            for (int32 X=EdgeRegion.Min.X;X<FMath::Min(Width,EdgeRegion.Max.X);++X) {
                const FColor Pixel=Pixels[Y*Width+X];
                EdgePinkPixels+=IsPink(Pixel);
                EdgeAmberPixels+=IsAmber(Pixel);
            }
        }
        // The entire flight corridor should show only the background and the optional crosshair.
        const auto& Canvas=Osd->LayoutCanvas->GetCachedGeometry();
        const auto ClearMinimum=Canvas.LocalToAbsolute(FVector2D(350,365)),ClearMaximum=Canvas.LocalToAbsolute(FVector2D(1570,805));
        const auto& Crosshair=Osd->CrosshairReadout->GetCachedGeometry();
        const FBox2D CrosshairBounds(Crosshair.LocalToAbsolute(FVector2D::ZeroVector)-FVector2D(3,3),
            Crosshair.LocalToAbsolute(Crosshair.GetLocalSize())+FVector2D(3,3));
        CenterTextPixels=0;
        for (int32 Y=FMath::Max(0,FMath::CeilToInt(ClearMinimum.Y));Y<FMath::Min(Height,FMath::FloorToInt(ClearMaximum.Y));++Y) {
            for (int32 X=FMath::Max(0,FMath::CeilToInt(ClearMinimum.X));X<FMath::Min(Width,FMath::FloorToInt(ClearMaximum.X));++X) {
                if (Data.bShowCrosshair && CrosshairBounds.IsInside(FVector2D(X,Y))) continue;
                const FColor Pixel=Pixels[Y*Width+X];
                const bool bBrightText=Pixel.R>220 && Pixel.G>220 && Pixel.B>220;
                const bool bMintText=Pixel.G>220 && int32(Pixel.G)>int32(Pixel.R)+50 && int32(Pixel.G)>int32(Pixel.B)+20;
                CenterTextPixels+=bBrightText || bMintText || IsPink(Pixel) || IsAmber(Pixel);
            }
        }
        if (Osd->HasRaceTarget()) {
            auto CountPixels=[&](const SWidget& Widget,auto Predicate) {
                const auto& Geometry=Widget.GetCachedGeometry();
                const auto Minimum=Geometry.LocalToAbsolute(FVector2D::ZeroVector),Maximum=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
                int32 Matches=0;
                for (int32 Y=FMath::Max(0,FMath::FloorToInt(Minimum.Y));Y<FMath::Min(Height,FMath::CeilToInt(Maximum.Y));++Y)
                    for (int32 X=FMath::Max(0,FMath::FloorToInt(Minimum.X));X<FMath::Min(Width,FMath::CeilToInt(Maximum.X));++X)
                        if (Predicate(Pixels[Y*Width+X])) ++Matches;
                return Matches;
            };
            ArrowPinkPixels=CountPixels(*Osd->RaceNavigationArrow,IsPink);
            CheckpointPinkPixels=CountPixels(*Osd->RaceTargetReadout,IsPink);
            ManeuverWhitePixels=CountPixels(*Osd->RaceTargetInstructionReadout,[](const FColor& Pixel) {
                return Pixel.R>220 && Pixel.G>220 && Pixel.B>220;
            });
            ManeuverAmberPixels=CountPixels(*Osd->RaceTargetInstructionReadout,IsAmber);
            WrongWayAmberPixels=CountPixels(*Osd->RaceStatusReadout,IsAmber);
            if (Osd->HasRaceFinish() || Osd->HasRaceSplit()) RaceStatusMintPixels=CountPixels(*Osd->RaceStatusReadout,[](const FColor& Pixel) {
                return Pixel.G>220 && int32(Pixel.G)>int32(Pixel.R)+50 && int32(Pixel.G)>int32(Pixel.B)+20;
            });
        }
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Width,Height,Pixels,Png);
        return FFileHelper::SaveArrayToFile(Png,*(FPaths::ProjectSavedDir()/TEXT("FactoryFPV")/Name));
    };
    TestTrue(TEXT("Armed OSD at 1600x900"),Capture(TEXT("OSD-Armed.png"),1600,900));
    TestTrue(TEXT("Armed OSD at 1280x720"),Capture(TEXT("OSD-720.png"),1280,720));
    Data.bArmed=false;
    Data.Throttle=0;
    TestTrue(TEXT("Disarmed OSD"),Capture(TEXT("OSD-Disarmed.png"),1600,900));
    Data.bConnected=false;
    TestTrue(TEXT("Receiver-loss OSD"),Capture(TEXT("OSD-RxLoss.png"),1600,900));
    Data.bConnected=true;Data.bArmed=true;Data.bAngleMode=true;
    Data.bShowCrosshair=Data.bShowSpeed=Data.bShowAltitude=Data.bShowHome=Data.bShowTimer=Data.bShowThrottle=false;
    Data.TextScale=1.3f;Data.Opacity=.3f;
    Data.bBatteryEnabled=true;Data.BatteryPercent=19;Data.BatteryVolts=13.8;Data.bBatteryLow=true;
    TestTrue(TEXT("Customized Angle/battery OSD at 1280x720"),Capture(TEXT("OSD-Custom-Angle-720.png"),1280,720));
    for (const auto& Optional:{Osd->CrosshairReadout,Osd->SpeedReadout,Osd->AltitudeReadout,Osd->HomeReadout,Osd->TimerReadout,Osd->ThrottleReadout})
        TestTrue(TEXT("Optional readouts honor hidden preference"),Optional->GetVisibility()==EVisibility::Collapsed);
    TestFalse(TEXT("Receiver remains visible with all optional readouts hidden"),Osd->ReceiverReadout->GetVisibility()==EVisibility::Collapsed);
    TestFalse(TEXT("Flight mode remains visible with all optional readouts hidden"),Osd->ModeReadout->GetVisibility()==EVisibility::Collapsed);
    TestFalse(TEXT("Simulated battery remains visible when its model is enabled"),Osd->BatteryReadout->GetVisibility()==EVisibility::Collapsed);
    TestEqual(TEXT("Low-opacity setting does not dim safety warning"),Osd->WarningReadout->GetColorAndOpacity().GetSpecifiedColor().A,1.f);
    TestEqual(TEXT("Normal telemetry follows the user's opacity"),Osd->ModeReadout->GetColorAndOpacity().GetSpecifiedColor().A,.3f);
    Data.bShowCrosshair=Data.bShowSpeed=Data.bShowAltitude=Data.bShowHome=Data.bShowTimer=Data.bShowThrottle=true;
    Data.HomeDistanceM=999999999;Data.RelativeAltitudeM=-999999999;Data.SpeedMps=999999;
    Data.ArmedSeconds=86400;Data.Throttle=1;
    Data.bDamageEnabled=true;Data.IntegrityPercent=24;
    Data.Warning=TEXT("ARMING BLOCKED - SWITCH OFF, LOWER THROTTLE, THEN SWITCH ON");
    TestTrue(TEXT("Maximum text scale with bounded long telemetry at 1280x720"),Capture(TEXT("OSD-Large-Telemetry-720.png"),1280,720));
    TestEqual(TEXT("Long safety notices leave the flight corridor clear"),CenterTextPixels,0);
    for (const auto& Readout:{Osd->ReceiverReadout,Osd->TimerReadout,Osd->HomeReadout,Osd->SpeedReadout,Osd->AltitudeReadout,Osd->ModeReadout,Osd->ThrottleReadout,Osd->WarningReadout,Osd->BatteryReadout,Osd->IntegrityReadout}) {
        const auto& Geometry=Readout->GetCachedGeometry();
        const FVector2D Minimum=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
        const FVector2D Maximum=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
        TestTrue(TEXT("Readout stays within 720p frame at maximum scale"),Minimum.X>=-.5 && Minimum.Y>=-.5 && Maximum.X<=1280.5 && Maximum.Y<=720.5);
    }
    Data.Warning.Reset();Data.bBatteryCritical=true;
    TestTrue(TEXT("Critical simulated battery warning at 1280x720"),Capture(TEXT("OSD-Battery-Critical-720.png"),1280,720));
    Data.IntegrityPercent=0;
    TestTrue(TEXT("Destroyed frame warning at 1280x720"),Capture(TEXT("OSD-Frame-Lost-720.png"),1280,720));
    Data.bConnected=true;Data.bRacing=true;Data.RaceCourse=12;Data.RaceGate=3;Data.RaceGateCount=8;
    Data.RaceLaps=2;Data.RaceElapsed=37.25;Data.RaceLast=58.32;Data.RaceBest=55.71;
    Data.RaceStatus=TEXT("COURSE POWER LOST - RESTORE POWER, RESTART AT GATE 1");
    TestTrue(TEXT("Racing time, checkpoint and power guidance at maximum scale"),Capture(TEXT("OSD-Racing-720.png"),1280,720));
    TestTrue(TEXT("Racing shows real course and ordered target"),Osd->RaceText().Contains(TEXT("COURSE 12")) && Osd->RaceText().Contains(TEXT("GATE 3/8")));
    TestTrue(TEXT("Racing shows last and best lap times"),Osd->RaceText().Contains(TEXT("00:58.32")) && Osd->RaceText().Contains(TEXT("00:55.71")));
    Data.RaceStatus.Reset();Data.bBatteryEnabled=false;Data.bDamageEnabled=false;Data.Opacity=1;
    Data.bRaceTargetAvailable=true;Data.RaceTargetDistanceM=143;Data.RaceTargetInstruction=TEXT("PASS LEFT");
    auto NavigationBounds=[&](int32 Width,int32 Height) {
        for (const auto& Item:{Osd->RaceNavigationRoot,Osd->RaceNavigationArrow,StaticCastSharedPtr<SWidget>(Osd->RaceTargetReadout),StaticCastSharedPtr<SWidget>(Osd->RaceTargetInstructionReadout)}) {
            const auto& Geometry=Item->GetCachedGeometry();const auto Minimum=Geometry.LocalToAbsolute(FVector2D::ZeroVector),Maximum=Geometry.LocalToAbsolute(Geometry.GetLocalSize());
            TestTrue(TEXT("Checkpoint navigation remains within actual rendered viewport"),Minimum.X>=-.5 && Minimum.Y>=-.5 && Maximum.X<=Width+.5 && Maximum.Y<=Height+.5);
        }
        const auto& Navigation=Osd->RaceNavigationRoot->GetCachedGeometry();
        const auto& Target=Osd->RaceTargetReadout->GetCachedGeometry();
        TestTrue(TEXT("The checkpoint readout fits within its own navigation region"),
            Target.LocalToAbsolute(FVector2D::ZeroVector).X>=Navigation.LocalToAbsolute(FVector2D::ZeroVector).X-.5 &&
            Target.LocalToAbsolute(Target.GetLocalSize()).X<=Navigation.LocalToAbsolute(Navigation.GetLocalSize()).X+.5);
        const auto& Crosshair=Osd->CrosshairReadout->GetCachedGeometry();
        TestTrue(TEXT("Checkpoint instructions sit up and to the right of the crosshair"),
            Navigation.LocalToAbsolute(FVector2D::ZeroVector).X>Crosshair.LocalToAbsolute(Crosshair.GetLocalSize()).X &&
            Navigation.LocalToAbsolute(Navigation.GetLocalSize()).Y<Crosshair.LocalToAbsolute(FVector2D::ZeroVector).Y);
        TestEqual(TEXT("Race overlays leave the flight corridor clear"),CenterTextPixels,0);
        TestTrue(TEXT("Actual arrow and next checkpoint text render in pink"),ArrowPinkPixels>12 && CheckpointPinkPixels>12);
        if (Data.bRaceTargetOnExitSide)
            TestTrue(TEXT("Actual entry-side correction renders amber and keeps the pink cue"),ManeuverAmberPixels>12);
        else
            TestTrue(TEXT("Actual maneuver text renders bright and separate from the pink cue"),ManeuverWhitePixels>12);
    };
    EdgeRegion=FIntRect(1220,325,1280,395);
    TestTrue(TEXT("Next checkpoint ahead with pass-side instruction at 720p"),Capture(TEXT("OSD-Navigation-Ahead-720.png"),1280,720));NavigationBounds(1280,720);
    TestTrue(TEXT("A visible checkpoint paints no edge cue"),EdgePinkPixels==0 && EdgeAmberPixels==0);
    Data.RaceOffscreenDirection=FVector2D(1.,0.);
    TestTrue(TEXT("A checkpoint outside the right edge at 720p"),Capture(TEXT("OSD-Edge-Right-720.png"),1280,720));NavigationBounds(1280,720);
    TestTrue(TEXT("The right edge paints a distinct pink chevron"),EdgePinkPixels>12 && EdgeAmberPixels==0);
    EdgeRegion=FIntRect(440,0,520,48);
    Data.RaceOffscreenDirection=FVector2D(0.,-1.);Data.bRaceTargetOnExitSide=true;
    TestTrue(TEXT("Wrong-side overhead checkpoint in a narrow viewport"),Capture(TEXT("OSD-Edge-Above-Narrow.png"),960,720));NavigationBounds(960,720);
    TestTrue(TEXT("The amber chevron uses the actual top edge above the narrow viewport's telemetry canvas"),EdgeAmberPixels>12 && EdgePinkPixels==0);
    Data.bConnected=false;
    TestTrue(TEXT("Receiver loss with an otherwise valid edge cue"),Capture(TEXT("OSD-Edge-Receiver-Loss-Narrow.png"),960,720));
    TestTrue(TEXT("Receiver loss paints no stale edge cue"),EdgePinkPixels==0 && EdgeAmberPixels==0);
    Data.bConnected=true;Data.RaceOffscreenDirection=FVector2D::ZeroVector;Data.bRaceTargetOnExitSide=false;
    EdgeRegion=FIntRect(0,0,0,0);
    Data.RaceLast=Data.RaceBest=59.999;Data.RaceFinish=EFactoryFPVRaceFinish::CourseBest;
    TestTrue(TEXT("Course record and next checkpoint render together at 720p"),Capture(TEXT("OSD-Course-Best-720.png"),1280,720));NavigationBounds(1280,720);
    const auto RaceStatusBounds=[&] {
        const auto& RaceTiming=Osd->RaceReadout->GetCachedGeometry();
        const auto& RaceStatus=Osd->RaceStatusReadout->GetCachedGeometry();
        const auto& Navigation=Osd->RaceNavigationRoot->GetCachedGeometry();
        const auto StatusMin=RaceStatus.LocalToAbsolute(FVector2D::ZeroVector);
        const auto StatusMax=RaceStatus.LocalToAbsolute(RaceStatus.GetLocalSize());
        const double TimingBottom=RaceTiming.LocalToAbsolute(RaceTiming.GetLocalSize()).Y;
        const double NavigationLeft=Navigation.LocalToAbsolute(FVector2D::ZeroVector).X;
        TestTrue(TEXT("Race status stays below timing and left of navigation within 720p"),TimingBottom<StatusMin.Y && StatusMax.X<NavigationLeft &&
            StatusMin.X>=-.5 && StatusMax.X<=1280.5 && StatusMin.Y>=-.5 && StatusMax.Y<=720.5);
    };
    TestTrue(TEXT("Course record paints distinguishable mint text"),RaceStatusMintPixels>12);
    RaceStatusBounds();
    Data.RaceFinish=EFactoryFPVRaceFinish::None;
    Data.RaceTargetBearingDegrees=90;Data.RaceTargetElevationDegrees=15;
    TestTrue(TEXT("Next checkpoint right/above at 1080p"),Capture(TEXT("OSD-Navigation-Right-1080.png"),1920,1080));NavigationBounds(1920,1080);
    Data.RaceTargetBearingDegrees=180;Data.RaceTargetElevationDegrees=-12;Data.RaceTargetInstruction=TEXT("PASS RIGHT");
    Data.RaceOffscreenDirection=FVector2D(1.,0.);
    EdgeRegion=FIntRect(2500,425,2560,495);
    TestTrue(TEXT("Offscreen-behind checkpoint and lower pass cue at 2560x920"),Capture(TEXT("OSD-Navigation-Behind-Ultrawide.png"),2560,920));NavigationBounds(2560,920);
    TestTrue(TEXT("The ultrawide right chevron stays on the actual viewport edge"),EdgePinkPixels>12 && EdgeAmberPixels==0);
    Data.RaceOffscreenDirection=FVector2D::ZeroVector;
    EdgeRegion=FIntRect(0,0,0,0);
    Data.bRaceTargetVerticalGate=true;Data.RaceTargetHeightOffsetM=11;Data.RaceTargetBearingDegrees=0;Data.RaceTargetInstruction=TEXT("ASCEND");
    TestTrue(TEXT("Ascent and opening height are readable at maximum text scale at 720p"),Capture(TEXT("OSD-Navigation-Ascent-720.png"),1280,720));NavigationBounds(1280,720);
    TestEqual(TEXT("Rendered ascent shows the signed height in the next checkpoint readout"),
        Osd->RaceTargetReadout->GetText().ToString(),FString(TEXT("NEXT 3/8 / 143 M / 11 M ABOVE")));
    Data.RaceTargetHeightOffsetM=-9;Data.RaceTargetBearingDegrees=-45;Data.RaceTargetElevationDegrees=15;Data.RaceTargetInstruction=TEXT("DESCEND");
    TestTrue(TEXT("Descent maneuver is readable at maximum text scale at 1080p"),Capture(TEXT("OSD-Navigation-Descent-1080.png"),1920,1080));NavigationBounds(1920,1080);
    Data.bRaceTargetOnExitSide=true;Data.RaceTargetBearingDegrees=160;Data.RaceTargetInstruction=TEXT("ENTER FROM BELOW");
    TestTrue(TEXT("Wrong-face correction is readable at maximum text scale at 720p"),Capture(TEXT("OSD-Navigation-Wrong-Face-720.png"),1280,720));NavigationBounds(1280,720);
    TestEqual(TEXT("Vertical opening keeps the rear location cue alongside its height"),
        Osd->RaceTargetReadout->GetText().ToString(),FString(TEXT("NEXT 3/8 / 143 M / 9 M BELOW / BEHIND")));
    Data.bRaceTargetOnExitSide=false;
    Data.RaceTargetElevationDegrees=-80;Data.RaceTargetInstruction=TEXT("DESCEND");
    Data.bRaceWrongWay=true;Data.RaceStatus=TEXT("WRONG WAY - ENTER FROM ABOVE");
    TestTrue(TEXT("Wrong-way pass and next checkpoint render together at 720p"),Capture(TEXT("OSD-Race-Wrong-Way-720.png"),1280,720));
    NavigationBounds(1280,720);RaceStatusBounds();
    TestTrue(TEXT("Wrong-way status paints amber without recoloring the pink checkpoint cue"),WrongWayAmberPixels>12);
    TestEqual(TEXT("Rendered status retains the required entry direction"),Osd->RaceStatusReadout->GetText().ToString(),Data.RaceStatus);
    Data.bRaceWrongWay=false;Data.RaceStatus.Reset();
    Data.RaceSplitGate=2;Data.RaceSplitSeconds=12.34;Data.RaceSplitDelta=-.42;Data.bRaceSplitHasDelta=true;
    TestTrue(TEXT("Checkpoint split and next target render together at maximum text scale at 720p"),Capture(TEXT("OSD-Race-Split-720.png"),1280,720));
    NavigationBounds(1280,720);RaceStatusBounds();
    TestTrue(TEXT("A faster split paints mint without recoloring the pink cue"),RaceStatusMintPixels>12);
    TestEqual(TEXT("Rendered split shows elapsed and signed PB comparison"),Osd->RaceStatusReadout->GetText().ToString(),FString(TEXT("GATE 2  /  00:12.34  /  -0.42S PB")));
    Data.RaceSplitGate=0;
    Data.bRaceTargetAvailable=false;
    TestTrue(TEXT("Unavailable next checkpoint clears its navigation block"),Capture(TEXT("OSD-Navigation-Unavailable-720.png"),1280,720));
    TestEqual(TEXT("Unavailable target block is actually collapsed"),Osd->RaceNavigationRoot->GetVisibility(),EVisibility::Collapsed);
    Data.bConnected=false;
    TestTrue(TEXT("Receiver loss retains precedence over battery at 1280x720"),Capture(TEXT("OSD-RxLoss-Battery-720.png"),1280,720));

    Data={};
    Data.TextScale=1.3f;
    Data.SpeedMps=999999.;Data.RelativeAltitudeM=-99999.;Data.HomeDistanceM=999999.;
    Data.ArmedSeconds=86400.;Data.Throttle=1.;
    Data.bBatteryEnabled=Data.bDamageEnabled=true;Data.BatteryVolts=1000.;Data.IntegrityPercent=24.;
    Data.Warning=TEXT("ARMING BLOCKED - SWITCH OFF, LOWER THROTTLE, THEN SWITCH ON");
    Data.bRacing=Data.bRaceTargetAvailable=true;
    Data.RaceCourse=99;Data.RaceGate=2;Data.RaceGateCount=8;Data.RaceTargetDistanceM=143;
    Data.RaceStatus=TEXT("COURSE CHECK");Data.RaceTargetInstruction=TEXT("PASS LEFT");

    using EItem=EFactoryFPVOsdItem;
    using ESlot=EFactoryFPVOsdSlot;
    struct FPlacement { FVector2D Anchor,Alignment; };
    struct FMovableReadout {
        const TCHAR* Label;
        TSharedPtr<STextBlock> Widget;
        EItem Item;
        ESlot Destination;
        FPlacement Default,Moved;
    };
    const FMovableReadout Readouts[]{
        {TEXT("Speed"),Osd->SpeedReadout,EItem::Speed,ESlot::TopRight,{{96,898},{0,0}},{{1824,60},{1,0}}},
        {TEXT("Altitude"),Osd->AltitudeReadout,EItem::Altitude,ESlot::UpperLeft,{{1824,898},{1,0}},{{96,156},{0,0}}},
        {TEXT("Home"),Osd->HomeReadout,EItem::Home,ESlot::BottomRight,{{960,60},{.5,0}},{{1824,898},{1,0}}},
        {TEXT("Timer"),Osd->TimerReadout,EItem::Timer,ESlot::BottomLeft,{{1824,60},{1,0}},{{96,898},{0,0}}},
        {TEXT("Throttle"),Osd->ThrottleReadout,EItem::Throttle,ESlot::UpperRight,{{1824,976},{1,0}},{{1824,156},{1,0}}},
        {TEXT("Battery"),Osd->BatteryReadout,EItem::Battery,ESlot::TopCenter,{{96,820},{0,0}},{{960,60},{.5,0}}},
        {TEXT("Integrity"),Osd->IntegrityReadout,EItem::Integrity,ESlot::LowerLeft,{{1824,820},{1,0}},{{96,820},{0,0}}}
    };
    const TSharedPtr<SWidget> Pinned[]{Osd->ReceiverReadout,Osd->ModeReadout,Osd->CrosshairReadout,Osd->WarningReadout,
        Osd->RaceReadout,Osd->RaceStatusReadout,Osd->RaceNavigationRoot};
    auto Bounds=[](const SWidget& Widget) {
        const auto& Geometry=Widget.GetCachedGeometry();
        return FBox2D(Geometry.LocalToAbsolute(FVector2D::ZeroVector),Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
    };
    constexpr int32 NarrowWidth=960,NarrowHeight=720;
    if (!TestTrue(TEXT("Default telemetry renders at narrow aspect and maximum scale"),
        Capture(TEXT("OSD-Default-Narrow.png"),NarrowWidth,NarrowHeight))) return false;
    TArray<FBox2D> PinnedBounds;
    for (const auto& Widget:Pinned) PinnedBounds.Add(Bounds(*Widget));
    auto CheckPlacement=[&](bool bMoved) {
        TArray<FBox2D> Occupied=PinnedBounds;
        for (const auto& Readout:Readouts) {
            const auto& Placement=bMoved ? Readout.Moved : Readout.Default;
            const auto& Geometry=Readout.Widget->GetCachedGeometry();
            const FVector2D LocalSize=Geometry.GetLocalSize();
            const FVector2D Actual=Geometry.LocalToAbsolute(LocalSize*Placement.Alignment);
            const FVector2D Expected=Osd->LayoutCanvas->GetCachedGeometry().LocalToAbsolute(Placement.Anchor);
            TestTrue(FString::Printf(TEXT("%s has the expected %s anchor and alignment"),Readout.Label,bMoved ? TEXT("moved") : TEXT("default")),
                Actual.Equals(Expected,.6));
            const FBox2D Rect=Bounds(*Readout.Widget);
            TestTrue(FString::Printf(TEXT("%s stays visible inside the narrow viewport"),Readout.Label),
                Rect.Min.X>=-.5 && Rect.Min.Y>=-.5 && Rect.Max.X<=NarrowWidth+.5 && Rect.Max.Y<=NarrowHeight+.5 &&
                Rect.Max.X>Rect.Min.X && Rect.Max.Y>Rect.Min.Y);
            const auto* Overlap=Occupied.FindByPredicate([&](const FBox2D& Other) { return Rect.Intersect(Other); });
            if (Overlap) {
                AddError(FString::Printf(TEXT("%s overlaps a readout in the %s layout. Bounds [%s - %s] intersect [%s - %s]."),
                    Readout.Label,bMoved ? TEXT("custom") : TEXT("default"),*Rect.Min.ToString(),*Rect.Max.ToString(),
                    *Overlap->Min.ToString(),*Overlap->Max.ToString()));
            }
            Occupied.Add(Rect);
        }
    };
    CheckPlacement(false);
    TestEqual(TEXT("Race and warning text leave the narrow flight corridor clear"),CenterTextPixels,0);
    // Moving Speed to the occupied TopRight slot sends Timer to BottomLeft.
    for (const auto& Readout:Readouts) if (Readout.Item!=EItem::Timer) Data.Layout.Move(Readout.Item,Readout.Destination);
    if (!TestTrue(TEXT("All telemetry moves on the same rendered OSD"),
        Capture(TEXT("OSD-Custom-Narrow.png"),NarrowWidth,NarrowHeight))) return false;
    CheckPlacement(true);
    TestEqual(TEXT("Custom telemetry leaves the narrow flight corridor clear"),CenterTextPixels,0);
    for (int32 Index=0;Index<UE_ARRAY_COUNT(Pinned);++Index) {
        const FBox2D Rect=Bounds(*Pinned[Index]);
        TestTrue(TEXT("Fixed flight and race cues retain their positions after moving telemetry"),
            Rect.Min.Equals(PinnedBounds[Index].Min,.6) && Rect.Max.Equals(PinnedBounds[Index].Max,.6));
    }
    return true;
}
#endif
