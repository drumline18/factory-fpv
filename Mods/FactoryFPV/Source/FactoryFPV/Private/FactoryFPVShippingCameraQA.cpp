#include "FactoryFPVShippingCameraQA.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLensComponent.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVInteractionState.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHUD.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "UI/FGGameUI.h"
#include "UI/FGUserWidget.h"
#include "UI/FGInteractWidget.h"
#include "Animation/WidgetAnimation.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "CoreGlobals.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Console.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputActionValue.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace FactoryFPVShippingCameraQAInternal {
FString Escape(FString Text) {
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
FString Vec(const FVector& V) { return FString::Printf(TEXT("[%.9f,%.9f,%.9f]"),V.X,V.Y,V.Z); }
void Marker(const TCHAR* Result,const FString& Reason,const FString& Extra=FString()) {
    const TCHAR* Scope=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAStreamingRoute")) ?
        TEXT("scripted disarmed Shipping viewport route and native World Partition activation; excludes piloted flight, flight dynamics and physical input") :
        FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAGamepadGuard")) ?
        TEXT("native gamepad launch refusal with controller polling disabled, then ordinary Keyboard launch and return") :
        FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAShowAirframe")) ?
        TEXT("cooked Scout airframe and disarmed lens-off Shipping viewport with ordinary session return") :
        TEXT("actual disarmed Shipping viewport lens and ordinary session return; excludes radio flight feel, physical input and owner visual acceptance");
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"camera\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"%s\"%s}"),Result,*Escape(Reason),Scope,*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
bool Disposable(FString Path) {
    if (Path.IsEmpty()) return false;
    Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);FPaths::CollapseRelativeDirectories(Path);
    return Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard(FString& Error) {
    if (!FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) { Error=TEXT("Missing -FactoryFPVAutomation opt-in.");return false; }
    if (!Disposable(FPaths::ProjectSavedDir()) || !Disposable(UFGSaveSystem::GetSaveDirectoryPath())) {
        Error=TEXT("Both project Saved and actual save directory must be under /build/shipping-world-qa-.");return false;
    }
    return true;
}
bool SameBlendables(const TArray<FWeightedBlendable>& A,const TArray<FWeightedBlendable>& B) {
    if (A.Num()!=B.Num()) return false;
    for (int32 I=0;I<A.Num();++I) if (A[I].Object!=B[I].Object || !FMath::IsNearlyEqual(A[I].Weight,B[I].Weight)) return false;
    return true;
}
struct FCapture {
    FString Path;
    FIntPoint Size=FIntPoint::ZeroValue;
    FImage Image;
    double LumaVariance=0;
    bool bDecoded=false;
    FString Json() const {
        return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Size.X,Size.Y,bDecoded ? TEXT("true") : TEXT("false"),LumaVariance);
    }
};
struct FCameraSnapshot {
    TWeakObjectPtr<UCameraComponent> Camera;
    TArray<FWeightedBlendable> Blendables;
    float BlendWeight=0;
    float Fov=0;
};
struct FInputSnapshot {
    TWeakObjectPtr<UInputComponent> Component;
    bool bMember=false;
};
struct FNativeMenuEvidence {
    FString Kind,OpenApi,WidgetClass,OpenInputState=TEXT("null"),ClosedInputState=TEXT("null");
    FCapture Capture,AfterCapture;
    bool bRequested=false,bOpened=false,bMounted=false,bPresentation=false,bNativeOwnership=false,bSuspended=false,bF6Refused=false,bF9Refused=false,bOverlayHidden=false;
    bool bLeaseStable=false,bClosed=false,bRetained=false,bDisarmed=false;
    int32 OwnedMoveAtOpen=0,OwnedLookAtOpen=0,ReadySamples=0;
    double ReadyStarted=0,F9ProbeOffsetCm=0;
    uint64 ReadyFrame=0,LastReadyFrame=0;
    FString Json() const {
        auto B=[](bool V){return V?TEXT("true"):TEXT("false");};
        return FString::Printf(TEXT("{\"kind\":\"%s\",\"openApi\":\"%s\",\"widgetClass\":\"%s\",\"requested\":%s,\"opened\":%s,\"mounted\":%s,\"presentationVerified\":%s,\"nativeOwnershipVerified\":%s,\"inputSuspended\":%s,\"boundF6Refused\":%s,\"boundF9Refused\":%s,\"f9ProbeOffsetCm\":%.3f,\"overlayHidden\":%s,\"leaseStableWhileOpen\":%s,\"closed\":%s,\"sessionRetained\":%s,\"remainedDisarmed\":%s,\"openInputState\":%s,\"closedInputState\":%s,\"capture\":%s,\"afterCapture\":%s}"),
            *Escape(Kind),*Escape(OpenApi),*Escape(WidgetClass),B(bRequested),B(bOpened),B(bMounted),B(bPresentation),B(bNativeOwnership),B(bSuspended),B(bF6Refused),B(bF9Refused),F9ProbeOffsetCm,B(bOverlayHidden),B(bLeaseStable),B(bClosed),B(bRetained),B(bDisarmed),*OpenInputState,*ClosedInputState,*Capture.Json(),*AfterCapture.Json());
    }
};
struct FLensPerfWindow {
    const TCHAR* Label;
    float Strength;
    int32 Samples=0;
    double MedianMs=0,P95Ms=0,ElapsedSeconds=0;
    FString Json() const {
        return FString::Printf(TEXT("{\"label\":\"%s\",\"strength\":%.3f,\"samples\":%d,\"medianMs\":%.6f,\"p95Ms\":%.6f,\"elapsedSeconds\":%.6f}"),
            Label,Strength,Samples,MedianMs,P95Ms,ElapsedSeconds);
    }
};
struct FRun : public TSharedFromThis<FRun> {
    enum class EPhase { Waiting,OpeningPreLaunchPause,ClosingPreLaunchPause,SettleBefore,CaptureBefore,SettleOn,CaptureOn,SettleAfter,CaptureAfter,OpeningPause,CapturePause,ClosingPause,
        MeasureLens,SettleStreamingLift,MeasureStreamingRoute,SettleStreamingEnd,
        CaptureHudBeforeMenus,CaptureHudAfterPause,CaptureHudFailure,OpeningNativeMenu,CaptureNativeMenu,ClosingNativeMenu,CaptureHudAfterNativeMenu,SettleConsoleBefore,CaptureConsoleBefore,CaptureConsoleAfter,FreshNativeSession,FreshNativeReturn,Returning,CapturePioneer };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> Controller;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<APlayerCameraManager> Manager;
    TWeakObjectPtr<AFGHUD> Hud;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TWeakObjectPtr<UCameraComponent> Camera;
    TWeakObjectPtr<UFactoryFPVLensComponent> Lens;
    TArray<FInputSnapshot> OriginalInputs;
    TArray<TWeakObjectPtr<UInputComponent>> SessionInputs;
    TArray<FCameraSnapshot> PioneerCameras;
    TArray<FWeightedBlendable> OffBlendables;
    FCapture OffBefore,On,OffAfter,PioneerAfterReturn,PauseMenuCapture;
    FCapture ConsoleBefore,ConsoleAfter,HudBeforeMenus,HudAfterPause,HudFailure;
    TArray<FNativeMenuEvidence> NativeMenus;
    TWeakObjectPtr<UFGInteractWidget> OwnedNativeWidget;
    TWeakObjectPtr<UConsole> OwnedConsole;
    TWeakPtr<SWidget> PresentedNativeSlate;
    bool bNativeMenusEnabled=false,bNativeMenusVerified=false,bNativeMenusFreshSessionPassed=false,bNativeMenusCapturesVerified=false,bConsolePixelsVerified=false;
    int32 NativeMenuIndex=INDEX_NONE,ConsolePixelSamples=0;
    double ConsoleChangedFraction=0,ConsoleOpenMeanError=0,ConsoleRestoreMeanError=0;
    EPhase Phase=EPhase::Waiting;
    double Deadline=0,PhaseStarted=0,CaptureStarted=0;
    uint64 PhaseFrame=0;
    FTransform LastPose=FTransform::Identity,FrozenPose=FTransform::Identity,DroneLaunchPose=FTransform::Identity;
    int32 StablePoseSamples=0;
    FVector OriginalPioneerLocation=FVector::ZeroVector;
    EMovementMode OriginalMovement=MOVE_None;
    uint8 OriginalCustomMovement=0;
    float OriginalFov=0,OriginalLockedFov=0,DroneFov=0;
    bool bOriginalIgnoreMove=false,bOriginalIgnoreLook=false,bOriginalPartialHud=false,bOriginalHiddenHud=false,bOriginalHudVisible=false,bOriginalShowHud=false,bOriginalCursor=false;
    bool bCheckGamepadGuard=false,bGamepadUnavailable=false,bGamepadLaunchRefused=false,bGamepadRefusalPreservedState=false;
    FString GamepadRefusalReason;
    bool bShowAirframe=false;
    FString AirframeEvidence=TEXT("null");
    bool bLaunchPassed=false,bLiveDisarmed=false,bViewTargetVerified=false,bOffBeforeApplied=false,bOnApplied=false,bOffAfterApplied=false;
    bool bFixedViewVerified=false,bPixelsPassed=false,bPioneerRestored=false,bInputsRestored=false,bPioneerCamerasUnchanged=false,bExitRequested=false;
    bool bDone=false,bOwnScreenshotPending=false,bScreenshotProcessed=false,bScreenMessagesBefore=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bStartupReadyBeforeGrace=false,bNativeHudBaselineObserved=false,bNativeHudHiddenVerified=false;
    int32 NativeHudBaselineWidgets=0,NativeHudVisibleMaximum=0,LastHudPhase=INDEX_NONE;
    TArray<FString> HudSnapshots;
    FString HudFailureReason;
    bool bPauseMenuEnabled=false,bPauseMenuRequested=false,bPauseMenuOpened=false,bPauseMenuInputSuspended=false,bPauseMenuHangarRefused=false,bPauseMenuClosed=false,bPauseMenuSessionRetained=false,bPauseMenuWorldPaused=false;
    bool bPauseMenuNativeGuardVerified=false,bPauseMenuInteractionStackEmpty=false;
    bool bPreLaunchPauseRequested=false,bPreLaunchPauseRefused=false;
    bool bPauseMenuBoundF6Refused=false,bPauseMenuBoundF9Refused=false,bBoundF9AfterMenuRecovered=false;
    double PauseMenuF9ProbeOffsetCm=0,F9AfterMenuProbeOffsetCm=0;
    bool bPauseMenuMoveIgnored=false,bPauseMenuLookIgnored=false;
    bool bPauseMenuOverlayHidden=false;
    bool bPauseMenuMounted=false,bPauseMenuPresentationVerified=false;
    float PauseMenuMinimumOpacity=1.f;
    double PauseMenuReadyStarted=0;
    uint64 PauseMenuReadyFrame=0,PauseMenuLastReadyFrame=0;
    int32 PauseMenuReadySamples=0;
    TWeakObjectPtr<UFGUserWidget> PresentedPauseMenu;
    TWeakPtr<SWidget> PresentedPauseSlate;
    TWeakPtr<SWindow> PresentedPauseWindow;
    FString PendingScreenshot,LastReason=TEXT("Waiting for the exact loaded disposable clone and living pioneer.");
    FDelegateHandle ScreenshotHandle;
    double MaximumCameraTranslationCm=0,MaximumCameraRotationDegrees=0,PioneerReturnTranslationCm=-1.;
    double OffOnMeanError=0,PredictedWarpMeanError=0,OffRestoreMeanError=0,ChangedFraction=0,CenterMeanError=0;
    int32 PixelSamples=0,OffBlendableCount=0,OnBlendableCount=0;

    static constexpr double LensPerfWarmupSeconds=.75,LensPerfWindowSeconds=2.;
    static constexpr int32 LensPerfWindowCount=4;
    bool bLensPerfEnabled=false,bLensPerfVerified=false,bLensPerfWindowComplete=false,bLensPerfInputSuspended=false;
    int32 LensPerfWindowIndex=0;
    FLensPerfWindow LensPerfWindows[LensPerfWindowCount]={{TEXT("off-a"),0.f},{TEXT("on-a"),.8f},{TEXT("on-b"),.8f},{TEXT("off-b"),0.f}};
    TArray<double> LensPerfIntervals;
    TArray<FWeightedBlendable> LensPerfExpectedBlendables;
    TWeakObjectPtr<UGameViewportClient> LensPerfClient;
    FViewport* LensPerfViewport=nullptr;
    FIntPoint LensPerfViewportSize=FIntPoint::ZeroValue;
    FTransform LensPerfViewPose=FTransform::Identity;
    FDelegateHandle LensPerfDrawHandle,LensPerfEndFrameHandle;
    uint64 LensPerfDrawFrame=~uint64(0),LensPerfPreviousFrame=0;
    double LensPerfFirstTime=0,LensPerfPreviousTime=0;
    float LensPerfMaxFps=-1.f,LensPerfEffectiveMaxTickRate=-1.f,LensPerfScreenPercentage=-1.f;
    int32 LensPerfVSync=-1,LensPerfDynamicResolutionMode=-1;
    FString LensPerfFailure;

    static constexpr double RouteLiftCm=16000.,RouteSpeedCmPerSecond=7500.,RouteMinimumDistanceCm=120000.,RouteTargetRadiusCm=28000.;
    bool bStreamingRouteEnabled=false,bStreamingRouteVerified=false,bRouteInputSuspended=false,bRouteDroneSourceFollowed=false,
        bRoutePioneerSourceHeld=false,bRoutePioneerFloorRetained=false,bRouteSourcesRetired=false,bRouteReachedEnd=false;
    TWeakObjectPtr<AFactoryFPVPioneerAnchor> RouteAnchor;
    TWeakObjectPtr<UWorldPartitionStreamingSourceComponent> RouteDroneSource,RoutePioneerSource;
    TWeakObjectPtr<UWorldPartition> RoutePartition;
    TWeakObjectPtr<UWorldPartitionSubsystem> RouteSubsystem;
    TWeakObjectPtr<UGameViewportClient> RouteViewportClient;
    FViewport* RouteViewport=nullptr;
    FIntPoint RouteViewportSize=FIntPoint::ZeroValue;
    FVector RouteStart=FVector::ZeroVector,RouteDirection=FVector::ZeroVector;
    double RouteLengthCm=0,RouteDistanceCm=0,RoutePioneerSeparationCm=0,RouteMaxPioneerDriftCm=0;
    FName RouteDroneSourceName,RoutePioneerSourceName;
    TArray<TWeakObjectPtr<const UWorldPartitionRuntimeCell>> RouteInitiallyUnloadedCells;
    TArray<FString> RouteActivatedCells;
    FDelegateHandle RouteDrawHandle,RouteEndFrameHandle;
    uint64 RouteDrawFrame=~uint64(0),RoutePreviousFrame=0;
    double RouteFirstTime=0,RoutePreviousTime=0,RouteElapsedSeconds=0,RouteMedianMs=0,RouteP95Ms=0,RouteMaxMs=0;
    double RouteIncompleteStarted=0,RouteMaxIncompleteSeconds=0;
    int32 RouteReadinessSamples=0,RouteIncompleteSamples=0,RouteVSync=-1,RouteDynamicResolutionMode=-1;
    float RouteMaxFps=-1.f,RouteEffectiveMaxTickRate=-1.f,RouteScreenPercentage=-1.f;
    TArray<double> RouteIntervals;
    FString RouteFailure;

    FString Evidence() const {
        FString Extra=FString::Printf(TEXT(",\"launchPassed\":%s,\"liveDisarmed\":%s,\"viewTargetVerified\":%s,\"lensOffBeforeApplied\":%s,\"lensOnApplied\":%s,\"lensOffAfterApplied\":%s,\"fixedViewVerified\":%s,\"pixelComparisonPassed\":%s,\"pioneerRestored\":%s,\"inputMembershipRestored\":%s,\"pioneerCamerasUnchanged\":%s,\"exitRequested\":%s,\"injectedControls\":false"),
            bLaunchPassed?TEXT("true"):TEXT("false"),bLiveDisarmed?TEXT("true"):TEXT("false"),bViewTargetVerified?TEXT("true"):TEXT("false"),
            bOffBeforeApplied?TEXT("true"):TEXT("false"),bOnApplied?TEXT("true"):TEXT("false"),bOffAfterApplied?TEXT("true"):TEXT("false"),
            bFixedViewVerified?TEXT("true"):TEXT("false"),bPixelsPassed?TEXT("true"):TEXT("false"),bPioneerRestored?TEXT("true"):TEXT("false"),
            bInputsRestored?TEXT("true"):TEXT("false"),bPioneerCamerasUnchanged?TEXT("true"):TEXT("false"),bExitRequested?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"startupSplashFinished\":%s,\"startupSplashObserved\":%s,\"startupSplashAnimationObserved\":%s,\"frontEndFinished\":%s"),
            bStartupFinished?TEXT("true"):TEXT("false"),bSplashObserved?TEXT("true"):TEXT("false"),bSplashAnimationObserved?TEXT("true"):TEXT("false"),bFrontEndFinished?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"pioneerReturnTranslationCm\":%.9f,\"pioneerCameraSnapshots\":%d"),PioneerReturnTranslationCm,PioneerCameras.Num());
        Extra+=FString::Printf(TEXT(",\"pauseMenuEnabled\":%s,\"pauseMenuOpened\":%s,\"pauseMenuInputSuspended\":%s,\"pauseMenuHangarRefused\":%s,\"pauseMenuClosed\":%s,\"pauseMenuSessionRetained\":%s,\"pauseMenuWorldPaused\":%s,\"pauseMenuCapture\":%s"),
            bPauseMenuEnabled?TEXT("true"):TEXT("false"),bPauseMenuOpened?TEXT("true"):TEXT("false"),bPauseMenuInputSuspended?TEXT("true"):TEXT("false"),
            bPauseMenuHangarRefused?TEXT("true"):TEXT("false"),bPauseMenuClosed?TEXT("true"):TEXT("false"),bPauseMenuSessionRetained?TEXT("true"):TEXT("false"),bPauseMenuWorldPaused?TEXT("true"):TEXT("false"),*PauseMenuCapture.Json());
        Extra+=FString::Printf(TEXT(",\"pauseMenuNativeGuardVerified\":%s,\"pauseMenuInteractionStackEmpty\":%s,\"foregroundTransitionVerified\":false,\"armedPauseFailsafeVerified\":false"),
            bPauseMenuNativeGuardVerified?TEXT("true"):TEXT("false"),bPauseMenuInteractionStackEmpty?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"preLaunchPauseRefused\":%s"),bPreLaunchPauseRefused?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"pauseMenuMoveIgnored\":%s,\"pauseMenuLookIgnored\":%s"),
            bPauseMenuMoveIgnored?TEXT("true"):TEXT("false"),bPauseMenuLookIgnored?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"pauseMenuOverlayHidden\":%s"),bPauseMenuOverlayHidden?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"pauseMenuBoundF6Refused\":%s,\"programmaticMenuActions\":%s"),
            bPauseMenuBoundF6Refused?TEXT("true"):TEXT("false"),(bPauseMenuEnabled || bNativeMenusEnabled)?TEXT("true"):TEXT("false"));
        Extra+=FString::Printf(TEXT(",\"pauseMenuBoundF9Refused\":%s,\"pauseMenuF9ProbeOffsetCm\":%.3f,\"boundF9AfterMenuRecovered\":%s,\"f9AfterMenuProbeOffsetCm\":%.3f"),
            bPauseMenuBoundF9Refused?TEXT("true"):TEXT("false"),PauseMenuF9ProbeOffsetCm,
            bBoundF9AfterMenuRecovered?TEXT("true"):TEXT("false"),F9AfterMenuProbeOffsetCm);
        Extra+=FString::Printf(TEXT(",\"pauseMenuMounted\":%s,\"pauseMenuPresentationVerified\":%s,\"pauseMenuMinimumOpacity\":%.6f,\"pauseMenuReadySamples\":%d"),
            bPauseMenuMounted?TEXT("true"):TEXT("false"),bPauseMenuPresentationVerified?TEXT("true"):TEXT("false"),PauseMenuMinimumOpacity,PauseMenuReadySamples);
        Extra+=FString::Printf(TEXT(",\"originalFov\":%.6f,\"originalLockedFov\":%.6f,\"droneFov\":%.6f,\"maximumCameraTranslationCm\":%.9f,\"maximumCameraRotationDegrees\":%.9f,\"fixedCameraLocation\":%s,\"offBlendables\":%d,\"onBlendables\":%d,\"originalInputComponents\":%d,\"sessionInputComponents\":%d"),
            OriginalFov,OriginalLockedFov,DroneFov,MaximumCameraTranslationCm,MaximumCameraRotationDegrees,*Vec(FrozenPose.GetLocation()),OffBlendableCount,OnBlendableCount,OriginalInputs.Num(),SessionInputs.Num());
        Extra+=FString::Printf(TEXT(",\"pixelSamples\":%d,\"offOnMeanError\":%.9f,\"predictedWarpMeanError\":%.9f,\"offRestoreMeanError\":%.9f,\"changedFraction\":%.9f,\"centerMeanError\":%.9f,\"offBefore\":%s,\"on\":%s,\"offAfter\":%s,\"pioneerAfterReturn\":%s"),
            PixelSamples,OffOnMeanError,PredictedWarpMeanError,OffRestoreMeanError,ChangedFraction,CenterMeanError,*OffBefore.Json(),*On.Json(),*OffAfter.Json(),*PioneerAfterReturn.Json());
        if (bShowAirframe) Extra+=TEXT(",\"airframe\":")+AirframeEvidence;
        Extra+=FString::Printf(TEXT(",\"nativeMenusEnabled\":%s,\"nativeMenusVerified\":%s,\"nativeMenusFreshSessionPassed\":%s,\"nativeMenusCapturesVerified\":%s,\"armedMenuFailsafeVerified\":false,\"lowThrottleArmingVerified\":false,\"manualNativeInputMutation\":false,\"physicalMenuInputVerified\":false,\"savedWorld\":false,\"nativeMenus\":["),
            bNativeMenusEnabled?TEXT("true"):TEXT("false"),bNativeMenusVerified?TEXT("true"):TEXT("false"),bNativeMenusFreshSessionPassed?TEXT("true"):TEXT("false"),bNativeMenusCapturesVerified?TEXT("true"):TEXT("false"));
        for (int32 I=0;I<NativeMenus.Num();++I) { if (I) Extra+=TEXT(",");Extra+=NativeMenus[I].Json(); }
        Extra+=FString::Printf(TEXT("],\"consoleBeforeCapture\":%s,\"consoleAfterCapture\":%s,\"consoleRenderedPixelsVerified\":%s,\"consolePixelSamples\":%d,\"consoleChangedFraction\":%.9f,\"consoleOpenMeanError\":%.9f,\"consoleRestoreMeanError\":%.9f"),
            *ConsoleBefore.Json(),*ConsoleAfter.Json(),bConsolePixelsVerified?TEXT("true"):TEXT("false"),ConsolePixelSamples,ConsoleChangedFraction,ConsoleOpenMeanError,ConsoleRestoreMeanError);
        Extra+=FString::Printf(TEXT(",\"startupReadyBeforeGrace\":%s,\"nativeHudBaselineObserved\":%s,\"nativeHudBaselinePresentedWidgets\":%d,\"nativeHudHiddenVerified\":%s,\"nativeHudMaximumPresentedInFlight\":%d,\"nativeHudFailureReason\":\"%s\",\"nativeHudBeforeMenusCapture\":%s,\"nativeHudAfterPauseCapture\":%s,\"nativeHudFailureCapture\":%s,\"hudSnapshots\":[%s]"),
            bStartupReadyBeforeGrace?TEXT("true"):TEXT("false"),bNativeHudBaselineObserved?TEXT("true"):TEXT("false"),NativeHudBaselineWidgets,bNativeHudHiddenVerified && HudFailureReason.IsEmpty()?TEXT("true"):TEXT("false"),NativeHudVisibleMaximum,*Escape(HudFailureReason),*HudBeforeMenus.Json(),*HudAfterPause.Json(),*HudFailure.Json(),*FString::Join(HudSnapshots,TEXT(",")));
        if (bLensPerfEnabled) {
            Extra+=FString::Printf(TEXT(",\"lensPerfEnabled\":true,\"lensPerfVerified\":%s,\"lensPerf\":{\"viewportWidth\":%d,\"viewportHeight\":%d,\"maxFps\":%.3f,\"vSync\":%d,\"offscreen\":true,\"warmupSeconds\":%.3f,\"windowSeconds\":%.3f,\"dynamicResolutionMode\":%d,\"screenPercentage\":%.3f,\"effectiveMaxTickRate\":%.3f,\"viewportDrawVerified\":%s,\"inputSuspended\":%s,\"windows\":["),
                bLensPerfVerified?TEXT("true"):TEXT("false"),LensPerfViewportSize.X,LensPerfViewportSize.Y,LensPerfMaxFps,LensPerfVSync,LensPerfWarmupSeconds,LensPerfWindowSeconds,
                LensPerfDynamicResolutionMode,LensPerfScreenPercentage,LensPerfEffectiveMaxTickRate,bLensPerfVerified?TEXT("true"):TEXT("false"),bLensPerfInputSuspended?TEXT("true"):TEXT("false"));
            for (int32 Index=0;Index<LensPerfWindowCount;++Index) {
                if (Index) Extra+=TEXT(",");
                Extra+=LensPerfWindows[Index].Json();
            }
            Extra+=TEXT("]}");
        }
        if (bStreamingRouteEnabled) Extra+=StreamingRouteEvidence();
        if (bCheckGamepadGuard) {
            Extra+=FString::Printf(TEXT(",\"gamepadGuardEnabled\":true,\"gamepadUnavailable\":%s,\"gamepadLaunchRefused\":%s,\"gamepadRefusalPreservedState\":%s,\"gamepadRefusalReason\":\"%s\""),
                bGamepadUnavailable?TEXT("true"):TEXT("false"),bGamepadLaunchRefused?TEXT("true"):TEXT("false"),
                bGamepadRefusalPreservedState?TEXT("true"):TEXT("false"),*Escape(GamepadRefusalReason));
        }
        return Extra;
    }
    FString StreamingRouteEvidence() const {
        auto B=[](bool Value) { return Value?TEXT("true"):TEXT("false"); };
        FString Out=FString::Printf(TEXT(",\"streamingRouteEnabled\":true,\"streamingRouteVerified\":%s,\"streamingRoute\":{\"offscreen\":true,\"inputSuspended\":%s,\"droneSourceFollowed\":%s,\"pioneerSourceHeld\":%s,\"pioneerFloorRetained\":%s,\"remoteCellActivated\":%s,\"sourcesRetired\":%s,\"viewportWidth\":%d,\"viewportHeight\":%d,\"maxFps\":%.3f,\"vSync\":%d,\"screenPercentage\":%.3f,\"dynamicResolutionMode\":%d,\"effectiveMaxTickRate\":%.3f"),
            B(bStreamingRouteVerified),B(bRouteInputSuspended),B(bRouteDroneSourceFollowed),B(bRoutePioneerSourceHeld),B(bRoutePioneerFloorRetained),
            B(!RouteActivatedCells.IsEmpty()),B(bRouteSourcesRetired),RouteViewportSize.X,RouteViewportSize.Y,RouteMaxFps,RouteVSync,RouteScreenPercentage,RouteDynamicResolutionMode,RouteEffectiveMaxTickRate);
        Out+=FString::Printf(TEXT(",\"routeDistanceCm\":%.6f,\"pioneerSeparationCm\":%.6f,\"routeDirection\":%s,\"liftCm\":%.1f,\"routeElapsedSeconds\":%.6f,\"maxPioneerDriftCm\":%.6f,\"samples\":%d,\"medianMs\":%.6f,\"p95Ms\":%.6f,\"maxMs\":%.6f,\"readinessSamples\":%d,\"incompleteSamples\":%d,\"maxObservedIncompleteSeconds\":%.6f,\"initiallyUnloadedCellCount\":%d,\"activatedCellCount\":%d,\"activatedCells\":["),
            RouteDistanceCm,RoutePioneerSeparationCm,*Vec(RouteDirection),RouteLiftCm,RouteElapsedSeconds,RouteMaxPioneerDriftCm,RouteIntervals.Num(),RouteMedianMs,RouteP95Ms,RouteMaxMs,
            RouteReadinessSamples,RouteIncompleteSamples,RouteMaxIncompleteSeconds,RouteInitiallyUnloadedCells.Num(),RouteActivatedCells.Num());
        for (int32 I=0;I<RouteActivatedCells.Num();++I) {
            if (I) Out+=TEXT(",");
            Out+=TEXT("\"")+Escape(RouteActivatedCells[I])+TEXT("\"");
        }
        return Out+TEXT("]}");
    }
    void ReleaseLensPerf() {
        if (LensPerfDrawHandle.IsValid()) {
            if (auto* Client=LensPerfClient.Get()) Client->OnDrawn().Remove(LensPerfDrawHandle);
            LensPerfDrawHandle.Reset();
        }
        if (LensPerfEndFrameHandle.IsValid()) {
            FCoreDelegates::OnEndFrame.Remove(LensPerfEndFrameHandle);LensPerfEndFrameHandle.Reset();
        }
    }
    void ReleaseScreenshot() {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot) {
            FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore;
        }
        bOwnScreenshotPending=false;
    }
    void ReleaseStreamingRoute() {
        if (RouteDrawHandle.IsValid()) {
            if (auto* Client=RouteViewportClient.Get()) Client->OnDrawn().Remove(RouteDrawHandle);
            RouteDrawHandle.Reset();
        }
        if (RouteEndFrameHandle.IsValid()) {
            FCoreDelegates::OnEndFrame.Remove(RouteEndFrameHandle);
            RouteEndFrameHandle.Reset();
        }
    }
    void Cleanup() {
        ReleaseLensPerf();
        ReleaseStreamingRoute();
        ReleaseScreenshot();
        // Close only the native menu this run opened, through its real normal
        // close path. Never replace a newer native widget or write UI flags.
        if (auto* Menu=OwnedNativeWidget.Get();Menu && Controller.IsValid() && Controller->GetGameUI()) {
            const auto Stack=Controller->GetGameUI()->GetInteractWidgetStack();UFGInteractWidget* Top=nullptr;
            for (int32 I=Stack.Num()-1;I>=0;--I) if (IsValid(Stack[I])) { Top=Stack[I];break; }
            if (Top==Menu) Menu->OnEscapePressed();
        }
        if (auto* Console=OwnedConsole.Get();Console && World.IsValid() && World->GetGameViewport() && World->GetGameViewport()->ViewportConsole==Console && Console->ConsoleState==TEXT("Open")) Console->FakeGotoState(NAME_None);
        if (bPreLaunchPauseRequested || (bPauseMenuRequested && !bPauseMenuClosed)) if (auto* PC=Controller.Get(); PC && PC->GetGameUI() && PC->GetGameUI()->IsPauseMenuOpen())
            PC->GetGameUI()->ResumeGame();
        if (auto* Own=Drone.Get(); Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==Controller.Get()) Own->ExitDrone();
    }
    void Finish(bool Success,const FString& Reason) {
        if (bDone) return;
        if (!Success) RecordHud(TEXT("terminalFailureBeforeCleanup"));
        bDone=true;Cleanup();Marker(Success?TEXT("PASS"):TEXT("FAIL"),Reason,Evidence());
        Drone.Reset();Camera.Reset();Lens.Reset();Controller.Reset();Pioneer.Reset();OriginalView.Reset();Manager.Reset();Hud.Reset();World.Reset();
        FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    bool IsNativePausePhase() const {
        return bPauseMenuEnabled && (Phase==EPhase::OpeningPreLaunchPause || Phase==EPhase::ClosingPreLaunchPause ||
            Phase==EPhase::OpeningPause || Phase==EPhase::CapturePause || Phase==EPhase::ClosingPause);
    }
    bool IsNativeMenuPhase() const {
        return bNativeMenusEnabled && (Phase==EPhase::OpeningNativeMenu || Phase==EPhase::CaptureNativeMenu || Phase==EPhase::ClosingNativeMenu || Phase==EPhase::CaptureConsoleAfter);
    }
    struct FHudPresentation {
        int32 Candidates=0,Presented=0;
        FString Rows=TEXT("[]");
    };
    FHudPresentation NativeHudPresentation() const {
        FHudPresentation Out;auto* PC=Controller.Get();auto* UI=PC?PC->GetGameUI():nullptr;
        auto* Client=World.IsValid()?World->GetGameViewport():nullptr;const auto Window=Client?Client->GetWindow():nullptr;
        const auto Root=UI?UI->GetCachedWidget():nullptr;
        if (!UI || !Root || !Window || !FSlateApplication::IsInitialized()) return Out;
        UUserWidget* PawnHud=Hud.IsValid()?Hud->GetPawnHUD():nullptr;TArray<FString> Rows;
        // Read actual native UWidgets rooted in this controller's GameUI. Names
        // identify HUD roles; a flag on AFGHUD cannot prove their presentation.
        for (TObjectIterator<UWidget> It;It;++It) {
            auto* Widget=*It;if (!IsValid(Widget) || Widget->IsTemplate() || Widget->GetWorld()!=World.Get()) continue;
            FString Role;const FString Name=Widget->GetName()+TEXT(" ")+Widget->GetClass()->GetName();
            if (Widget==PawnHud) Role=TEXT("PawnHUD");
            else for (const TCHAR* Token:{TEXT("Hotbar"),TEXT("Health"),TEXT("Compass"),TEXT("Objective")}) if (Name.Contains(Token,ESearchCase::IgnoreCase)) { Role=Token;break; }
            if (Role.IsEmpty()) continue;const auto Slate=Widget->GetCachedWidget();if (!Slate) continue;
            bool SawRoot=false,Visible=Widget->IsVisible();double Opacity=1.;auto Parent=Slate;
            for (int32 Depth=0;Parent && Depth<128;++Depth) { SawRoot|=Parent==Root;Visible&=Parent->GetVisibility().IsVisible();Opacity*=Parent->GetRenderOpacity();Parent=Parent->GetParentWidget(); }
            if (!SawRoot || Parent) continue;
            const bool Mounted=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef())==Window;
            const auto& Geometry=Widget->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
            const bool LaidOut=Mounted && FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X>=1 && Size.Y>=1 && Window->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(Size*.5f));
            const bool Presented=Mounted && LaidOut && Visible && FMath::IsFinite(Opacity) && Opacity>.01;
            ++Out.Candidates;Out.Presented+=Presented?1:0;
            if (Rows.Num()<64) Rows.Add(FString::Printf(TEXT("{\"role\":\"%s\",\"name\":\"%s\",\"class\":\"%s\",\"path\":\"%s\",\"mounted\":%s,\"visibleAncestry\":%s,\"laidOut\":%s,\"opacity\":%s,\"presented\":%s}"),
                *Escape(Role),*Escape(Widget->GetName()),*Escape(Widget->GetClass()->GetPathName()),*Escape(Widget->GetPathName()),Mounted?TEXT("true"):TEXT("false"),Visible?TEXT("true"):TEXT("false"),LaidOut?TEXT("true"):TEXT("false"),FMath::IsFinite(Opacity)?*FString::Printf(TEXT("%.9f"),Opacity):TEXT("null"),Presented?TEXT("true"):TEXT("false")));
        }
        Out.Rows=TEXT("[")+FString::Join(Rows,TEXT(","))+TEXT("]");return Out;
    }
    void RecordHud(const FString& Label) {
        auto* PC=Controller.Get();auto* GameHud=PC?Cast<AFGHUD>(PC->GetHUD()):nullptr;auto* UI=PC?PC->GetGameUI():nullptr;
        if (!PC || !GameHud || HudSnapshots.Num()>=96) return;const auto Presentation=NativeHudPresentation();
        HudSnapshots.Add(FString::Printf(TEXT("{\"label\":\"%s\",\"phase\":%d,\"frame\":%llu,\"partial\":%s,\"pumpi\":%s,\"hiddenHudMode\":%s,\"hudVisible\":%s,\"hudShow\":%s,\"needRespawn\":%s,\"respawning\":%s,\"respawnGrace\":%s,\"startupFinished\":%s,\"nativePause\":%s,\"interactWidgets\":%d,\"hudClass\":\"%s\",\"pawnHudClass\":\"%s\",\"candidateWidgets\":%d,\"presentedWidgets\":%d,\"widgets\":%s}"),
            *Escape(Label),static_cast<int32>(Phase),static_cast<unsigned long long>(GFrameCounter),GameHud->GetPartialPumpiMode()?TEXT("true"):TEXT("false"),GameHud->GetPumpiMode()?TEXT("true"):TEXT("false"),GameHud->GetHiddenHUDMode()?TEXT("true"):TEXT("false"),GameHud->GetHUDVisibility()?TEXT("true"):TEXT("false"),GameHud->bShowHUD?TEXT("true"):TEXT("false"),PC->NeedRespawn()?TEXT("true"):TEXT("false"),PC->IsRespawning()?TEXT("true"):TEXT("false"),PC->IsClientRespawnGracePeriodActive()?TEXT("true"):TEXT("false"),bStartupFinished?TEXT("true"):TEXT("false"),UI && UI->IsPauseMenuOpen()?TEXT("true"):TEXT("false"),UI?UI->GetInteractWidgetStack().Num():0,*Escape(GameHud->GetClass()->GetPathName()),GameHud->GetPawnHUD()?*Escape(GameHud->GetPawnHUD()->GetClass()->GetPathName()):TEXT(""),Presentation.Candidates,Presentation.Presented,*Presentation.Rows));
    }
    void CaptureHudFailure(const FString& Reason) {
        HudFailureReason=Reason;
        RecordHud(TEXT("nativeHudFailure"));
        // Retain a genuine ShowUI image even if the first mismatch occurs while
        // an earlier ShowUI=false lens screenshot was pending.
        ReleaseScreenshot();Settle(EPhase::CaptureHudFailure,FPlatformTime::Seconds());
        RequestCapture(HudFailure,TEXT("Camera-NativeHudFailure.png"),true,FPlatformTime::Seconds());
    }
    bool CheckNativeHudHidden() {
        if (!bNativeMenusEnabled) return true;const auto Presentation=NativeHudPresentation();
        NativeHudVisibleMaximum=FMath::Max(NativeHudVisibleMaximum,Presentation.Presented);
        if (bNativeHudBaselineObserved && Presentation.Candidates>0 && Presentation.Presented==0) { bNativeHudHiddenVerified=true;return true; }
        CaptureHudFailure(FString::Printf(TEXT("Native HUD presentation is visible or its actual readback schema disappeared during ordinary FPV: candidates=%d presented=%d partial=%d phase=%d. No HUD state was repaired."),Presentation.Candidates,Presentation.Presented,Hud.IsValid() && Hud->GetPartialPumpiMode(),static_cast<int32>(Phase)));
        return false;
    }
    bool Ready() {
        auto* Current=World.Get();
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay()) return false;
        auto* Mode=Current->GetAuthGameMode<AFGGameMode>();auto* Save=Mode?Mode->GetSaveSession():nullptr;
        auto* PC=Cast<AFGPlayerController>(Current->GetFirstPlayerController());
        auto* Character=PC?Cast<AFGCharacterPlayer>(PC->GetPawn()):nullptr;
        if (!Mode || Mode->IsMainMenuGameMode() || !Save || Save->HasSaveGameOperationPending() || !PC || !PC->GetLocalPlayer() || !PC->IsLocalController()) return false;
        auto* UI=PC->GetGameUI();
        if (Phase==EPhase::Waiting) {
            // Observe the actual splash before join/grace gates, which may last
            // longer than the animation. This is native readiness, not a delay
            // or a write to the HUD/intro state.
            if (!FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
            if (PC->NeedRespawn() || PC->IsRespawning() || PC->IsClientRespawnGracePeriodActive()) { LastReason=TEXT("Waiting for actual native join/respawn grace to finish after observed startup.");return false; }
            bStartupReadyBeforeGrace=true;
        }
        if (!Character || !Character->IsAliveAndWell() || !PC->PlayerCameraManager) return false;
        if (!IsNativePausePhase() && !IsNativeMenuPhase() && (Current->IsPaused() || PC->IsPaused() || PC->IsPauseMenuOpen() || (UI && UI->IsPauseMenuOpen()))) {
            if (Phase==EPhase::Waiting) LastReason=TEXT("Waiting for an unpaused world with no pre-existing native pause menu.");
            else Finish(false,TEXT("An unrelated world/native-menu pause interrupted a non-pause camera QA phase."));
            return false;
        }
        const auto& Header=Save->GetLoadedSaveHeader();const auto Load=Mode->GetLoadData();
        if (Header.SaveName.IsEmpty() || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty() ||
            (Load.LoadName!=TEXT("FPV-QA-Clone") && Header.SaveName!=TEXT("FPV-QA-Clone"))) { LastReason=TEXT("Loaded save identity is not FPV-QA-Clone.");return false; }
        if (Phase!=EPhase::Waiting && (PC!=Controller.Get() || Character!=Pioneer.Get())) { Finish(false,TEXT("Original controller or living pioneer changed."));return false; }
        if (Phase==EPhase::Waiting) { Controller=PC;Pioneer=Character; }
        return true;
    }
    bool CheckGamepadGuard() {
        auto* PC=Controller.Get();auto* Current=World.Get();auto* Client=Current->GetGameViewport();
        const auto LeaseBefore=FactoryFPVInputLease::GetDiagnostics(PC);
        const bool bViewportIgnoredBefore=Client->IgnoreInput();
        auto NoLiveFPVActors=[Current] {
            return !TActorIterator<AFactoryFPVDrone>(Current) && !TActorIterator<AFactoryFPVPioneerAnchor>(Current) &&
                !TActorIterator<AFactoryFPVPilotVisual>(Current);
        };
        bGamepadUnavailable=FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsGamepadAttached();
        if (!bGamepadUnavailable || !NoLiveFPVActors()) {
            Finish(false,TEXT("Gamepad guard requires the native no-pad state and a clean FPV world."));return false;
        }
        if (auto* Unexpected=AFactoryFPVDrone::Launch(PC,FName(TEXT("gamepad")),GamepadRefusalReason)) {
            Drone=Unexpected;Finish(false,TEXT("Gamepad launch succeeded with controller polling disabled."));return false;
        }
        bGamepadLaunchRefused=GamepadRefusalReason.Contains(TEXT("connected gamepad"),ESearchCase::IgnoreCase);
        const auto LeaseAfter=FactoryFPVInputLease::GetDiagnostics(PC);
        bGamepadRefusalPreservedState=CheckRestored(false) && NoLiveFPVActors() && Client->IgnoreInput()==bViewportIgnoredBefore &&
            LeaseAfter.ActiveLeases==LeaseBefore.ActiveLeases && LeaseAfter.OwnedMoveContributions==LeaseBefore.OwnedMoveContributions &&
            LeaseAfter.OwnedLookContributions==LeaseBefore.OwnedLookContributions && LeaseAfter.ResetDepth==LeaseBefore.ResetDepth;
        if (!bGamepadLaunchRefused || !bGamepadRefusalPreservedState) {
            Finish(false,TEXT("Gamepad launch refusal did not identify a connected gamepad requirement or changed pioneer ownership: ")+GamepadRefusalReason);return false;
        }
        return true;
    }
    bool SnapshotAndLaunch(double Now) {
        auto* PC=Controller.Get();auto* Character=Pioneer.Get();auto* Current=World.Get();auto* UI=PC->GetGameUI();
        auto* Subsystem=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        if (!UI || !Subsystem || !bStartupReadyBeforeGrace) return false;
        if (UI->HasActiveInteractWidget() || Subsystem->IsPanelOpen() || AFactoryFPVDrone::FindActive(Current) ||
            AFactoryFPVPioneerAnchor::FindPending(Current,Character)) { Finish(false,TEXT("Camera QA requires no pre-existing FPV session, pending return or interaction UI."));return false; }
        if (bPauseMenuEnabled && !bPreLaunchPauseRefused) {
            if (!NativeInteractionClear()) { LastReason=TEXT("Waiting for native interaction to clear before the pause launch check.");return false; }
            Settle(EPhase::OpeningPreLaunchPause,Now);
            bPreLaunchPauseRequested=true;
            UI->Native_HandlePauseGamePressed();
            return true;
        }
        const auto* CameraField=FindFProperty<FObjectPropertyBase>(AFGCharacterPlayer::StaticClass(),TEXT("mCameraComponent"));
        if (!CameraField || !CameraField->PropertyClass || !CameraField->PropertyClass->IsChildOf(UCameraComponent::StaticClass())) {
            Finish(false,TEXT("Known native pioneer mCameraComponent property/schema is unavailable."));return false;
        }
        // The BlueprintPure getter is protected in the matching SDK. Read the
        // reflected native property without calling an inaccessible accessor.
        auto* PioneerCamera=Cast<UCameraComponent>(CameraField->GetObjectPropertyValue_InContainer(Character));
        auto* Move=Character->GetCharacterMovement();auto* GameHud=Cast<AFGHUD>(PC->GetHUD());
        if (!Move || !GameHud || !IsValid(PioneerCamera) || !Current->GetGameViewport()) { LastReason=TEXT("Waiting for actual pioneer movement, camera, HUD and viewport.");return false; }
        OriginalView=PC->GetViewTarget();Manager=PC->PlayerCameraManager;Hud=GameHud;
        OriginalFov=Manager->GetFOVAngle();OriginalLockedFov=Manager->GetLockedFOV();
        if (!OriginalView.IsValid() || !FMath::IsFinite(OriginalFov) || !FMath::IsFinite(OriginalLockedFov)) { Finish(false,TEXT("Original view/FOV snapshot is invalid."));return false; }
        OriginalPioneerLocation=Character->GetActorLocation();OriginalMovement=Move->MovementMode;OriginalCustomMovement=Move->CustomMovementMode;
        bOriginalIgnoreMove=PC->IsMoveInputIgnored();bOriginalIgnoreLook=PC->IsLookInputIgnored();bOriginalCursor=PC->bShowMouseCursor;
        bOriginalPartialHud=GameHud->GetPartialPumpiMode();bOriginalHiddenHud=GameHud->GetHiddenHUDMode();bOriginalHudVisible=GameHud->GetHUDVisibility();bOriginalShowHud=GameHud->bShowHUD;
        if (bNativeMenusEnabled) {
            const auto Presentation=NativeHudPresentation();NativeHudBaselineWidgets=Presentation.Presented;bNativeHudBaselineObserved=Presentation.Candidates>0 && Presentation.Presented>0;
            RecordHud(TEXT("beforeLaunch"));
            if (!bNativeHudBaselineObserved) {
                CaptureHudFailure(TEXT("Native HUD widget baseline was not genuinely presented after native startup/grace; hidden-HUD assertions cannot be inferred."));return false;
            }
        }
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==Current)
            OriginalInputs.Add({*It,PC->IsInputComponentInStack(*It)});
        TInlineComponentArray<UCameraComponent*> Cameras(Character);
        Cameras.AddUnique(PioneerCamera);
        for (auto* Item:Cameras) if (IsValid(Item) && !PioneerCameras.ContainsByPredicate([Item](const FCameraSnapshot& Snapshot) { return Snapshot.Camera.Get()==Item; }))
            PioneerCameras.Add({Item,Item->PostProcessSettings.WeightedBlendables.Array,Item->PostProcessBlendWeight,Item->FieldOfView});
        if (PioneerCameras.IsEmpty()) { Finish(false,TEXT("A nonempty actual pioneer camera snapshot is required before launch."));return false; }
        if (bCheckGamepadGuard && !CheckGamepadGuard()) return false;
        FString Error;auto* Launched=AFactoryFPVDrone::Launch(PC,FName(TEXT("keyboard")),Error);
        if (!Launched) { Finish(false,TEXT("Ordinary grounded keyboard launch refused: ")+Error);return false; }
        Drone=Launched;bLaunchPassed=true;
        DroneLaunchPose=Launched->GetActorTransform();
        if (bNativeMenusEnabled) RecordHud(TEXT("afterLaunch"));
        Camera=Launched->FindComponentByClass<UCameraComponent>();Lens=Launched->FindComponentByClass<UFactoryFPVLensComponent>();
        TInlineComponentArray<UInputComponent*> SessionControlComponents(Launched);for (auto* Input:SessionControlComponents) SessionInputs.Add(Input);
        if (!Camera.IsValid() || !Lens.IsValid() || SessionInputs.IsEmpty()) { Finish(false,TEXT("Real launched drone camera/lens/input components are unavailable."));return false; }
        bOffBeforeApplied=Lens->ApplyToCamera(Camera.Get(),0.f);
        if (!bOffBeforeApplied) { Finish(false,TEXT("Real lens rejected initial zero bypass."));return false; }
        OffBlendables=Camera->PostProcessSettings.WeightedBlendables.Array;OffBlendableCount=OffBlendables.Num();DroneFov=Camera->FieldOfView;
        LastPose=Camera->GetComponentTransform();Phase=EPhase::SettleBefore;PhaseStarted=Now;PhaseFrame=GFrameCounter;
        Marker(TEXT("PENDING"),bStreamingRouteEnabled ? TEXT("Ordinary disarmed session launched; checking a native streaming destination before scripted movement.") :
            bLensPerfEnabled ? TEXT("Ordinary disarmed session launched; waiting for the same fixed rendered view before lens timing.") :
            bShowAirframe ? TEXT("Ordinary disarmed session launched; waiting for the fixed lens-off Scout airframe capture.") :
            TEXT("Ordinary session launched; no controls injected. Waiting for disarmed fixed camera before Off/On/Off captures."),Evidence());
        return true;
    }
    bool CheckActive(bool Fixed,bool AllowPauseMenu=false) {
        auto* Own=Drone.Get();auto* PC=Controller.Get();auto* Component=Camera.Get();auto* Character=Pioneer.Get();
        bLiveDisarmed=Own && !Own->IsFlightArmed();
        bViewTargetVerified=Own && PC && PC->GetPawn()==Character && PC->GetViewTarget()==Own;
        if (!bLiveDisarmed || !bViewTargetVerified || !Component || !Lens.IsValid() || !Character || !Character->IsAliveAndWell() ||
            !Character->GetCharacterMovement() || !Manager.IsValid() || PC->PlayerCameraManager!=Manager.Get() || PC->GetHUD()!=Hud.Get()) {
            Finish(false,TEXT("Drone armed, ownership/view changed, or actual camera/lens/pioneer disappeared."));return false;
        }
        // Real native menus can clear controller ignore flags so their own
        // input can work. Only the owned native-menu phases may vary these flags;
        // the resumed-session check below still requires both holds restored.
        if (bNativeMenusEnabled && !AllowPauseMenu && Hud.IsValid() && (!Hud->GetPartialPumpiMode() || !Hud->GetHiddenHUDMode())) {
            CaptureHudFailure(TEXT("An ordinary FPV HUD ownership mode was released outside native UI; image retained before any cleanup."));return false;
        }
        if ((!AllowPauseMenu && (!PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored())) || Character->GetCharacterMovement()->MovementMode!=MOVE_None ||
            !Character->GetActorLocation().Equals(OriginalPioneerLocation,.5) || !Hud.IsValid() || (!AllowPauseMenu && !Hud->GetPartialPumpiMode())) {
            Finish(false,FString::Printf(TEXT("Ordinary active session ownership changed: moveIgnored=%d lookIgnored=%d movementMode=%d pioneerDeltaCm=%.9f partialHUD=%d nativePause=%d controllerPause=%d worldPause=%d phase=%d."),
                PC->IsMoveInputIgnored(),PC->IsLookInputIgnored(),Character->GetCharacterMovement()->MovementMode.GetValue(),
                FVector::Distance(Character->GetActorLocation(),OriginalPioneerLocation),Hud.IsValid() && Hud->GetPartialPumpiMode(),
                PC->GetGameUI() && PC->GetGameUI()->IsPauseMenuOpen(),PC->IsPaused(),World->IsPaused(),static_cast<int32>(Phase)));return false;
        }
        for (const auto& Input:SessionInputs) if (!Input.IsValid() || !PC->IsInputComponentInStack(Input.Get())) {
            Finish(false,TEXT("Real drone input component is not on the controller stack."));return false;
        }
        auto* UI=PC->GetGameUI();auto* Subsystem=PC->GetLocalPlayer()?PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>():nullptr;
        if (!UI || !Subsystem || (!AllowPauseMenu && (UI->HasActiveInteractWidget() || UI->IsPauseMenuOpen())) || Subsystem->IsPanelOpen()) {
            Finish(false,TEXT("Unexpected interaction or hangar UI interrupted the isolated camera comparison."));return false;
        }
        if (!AllowPauseMenu && !Own->IsFlightOverlayVisible()) {
            Finish(false,TEXT("Actual FPV overlay did not remain visible outside the native menu phases."));return false;
        }
        if (!FMath::IsFinite(Component->FieldOfView) || !FMath::IsNearlyEqual(Component->FieldOfView,DroneFov,.01f) ||
            !FMath::IsNearlyEqual(Manager->GetFOVAngle(),DroneFov,.1f)) {
            Finish(false,TEXT("Actual camera/manager FOV changed during the lens comparison."));return false;
        }
        const auto Pose=Component->GetComponentTransform();
        if (!Pose.IsValid()) { Finish(false,TEXT("Actual drone camera pose is invalid."));return false; }
        if (Fixed) {
            const double Translation=FVector::Distance(Pose.GetLocation(),FrozenPose.GetLocation());
            const double Rotation=FMath::RadiansToDegrees(Pose.GetRotation().AngularDistance(FrozenPose.GetRotation()));
            MaximumCameraTranslationCm=FMath::Max(MaximumCameraTranslationCm,Translation);MaximumCameraRotationDegrees=FMath::Max(MaximumCameraRotationDegrees,Rotation);
            if (Translation>.1 || Rotation>.02) { Finish(false,TEXT("Actual disarmed camera moved; lens images would not be a fixed-view comparison."));return false; }
            bFixedViewVerified=true;
        }
        if (Fixed && !AllowPauseMenu && !CheckNativeHudHidden()) return false;
        return true;
    }
    bool VerifyAirframe(FString& Error) {
        auto* Own=Drone.Get();
        const auto* Body=Cast<USphereComponent>(Own->GetRootComponent());
        const auto* Frame=Cast<USceneComponent>(Own->GetDefaultSubobjectByName(TEXT("Airframe")));
        const float SizeCm=Body ? Body->GetUnscaledSphereRadius()*2.f : 0.f;
        const double Tilt=Camera->GetRelativeRotation().Pitch;
        const float Fov=Camera->FieldOfView;
        if (!Frame || !Frame->IsVisible() || Frame->bHiddenInGame || !Body || Body->GetCollisionEnabled()!=ECollisionEnabled::QueryOnly ||
            !FMath::IsNearlyEqual(SizeCm,24.f,.001f) || !Frame->GetRelativeScale3D().Equals(FVector(.24),.0001) ||
            !FMath::IsNearlyEqual(Tilt,10.,.001) || !FMath::IsNearlyEqual(Fov,100.f,.001f)) {
            Error=TEXT("Actual Scout launch did not apply the isolated 24 cm airframe, 10 degree tilt, 100 degree FOV and query-only body.");return false;
        }
        const auto ReadPart=[Own](FName Name,const TCHAR* Path,bool bOwnerNoSee) -> const UStaticMeshComponent* {
            const auto* Part=Cast<UStaticMeshComponent>(Own->GetDefaultSubobjectByName(Name));
            const auto* Mesh=Part ? Part->GetStaticMesh().Get() : nullptr;
            return Part && Part->IsRegistered() && Part->IsVisible() && !Part->bHiddenInGame && Part->bOwnerNoSee==bOwnerNoSee &&
                Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision && Mesh && Mesh->GetPathName()==Path &&
                Mesh->GetNumLODs()>0 && Mesh->GetNumTriangles(0)>0 ? Part : nullptr;
        };
        const auto* Chassis=ReadPart(TEXT("ScoutFrame"),TEXT("/FactoryFPV/Airframe/SM_ScoutFrame.SM_ScoutFrame"),true);
        if (!Chassis || Chassis->GetAttachParent()!=Frame) {
            Error=TEXT("The live Scout frame lacks its cooked chassis, external visibility policy or noncolliding attachment.");return false;
        }
        FString PropPaths[2];
        for (int32 Index=0;Index<4;++Index) {
            const int32 Hand=(Index==0 || Index==3) ? 0 : 1;
            const TCHAR* Path=Hand==0 ? TEXT("/FactoryFPV/Airframe/SM_ScoutPropCW.SM_ScoutPropCW") :
                TEXT("/FactoryFPV/Airframe/SM_ScoutPropCCW.SM_ScoutPropCCW");
            const FName Name(*FString::Printf(TEXT("Prop%d"),Index));
            const auto* Prop=ReadPart(Name,Path,false);
            if (!Prop || Prop->GetAttachParent()!=Frame) {
                Error=Name.ToString()+TEXT(" lacks its cooked Scout prop, FPV visibility or noncolliding rotor attachment.");return false;
            }
            PropPaths[Hand]=Prop->GetStaticMesh()->GetPathName();
        }
        const auto* Preview=LoadObject<UTexture2D>(nullptr,TEXT("/FactoryFPV/Airframe/T_ScoutPreview.T_ScoutPreview"));
        if (!Preview || Preview->GetSizeX()<=0 || Preview->GetSizeY()<=0) {
            Error=TEXT("The cooked Scout preview texture is missing or empty.");return false;
        }
        const auto* Mesh=Chassis->GetStaticMesh().Get();
        AirframeEvidence=FString::Printf(TEXT("{\"frameMesh\":\"%s\",\"frameTriangles\":%d,\"propMeshes\":[\"%s\",\"%s\"],\"propCount\":4,"
            "\"previewTexture\":\"%s\",\"previewSize\":[%d,%d],\"relativeScale\":%s,\"frameSizeCm\":%.3f,\"cameraTilt\":%.3f,\"cameraFov\":%.3f,\"visualCollisionDisabled\":true}"),
            *Escape(Mesh->GetPathName()),Mesh->GetNumTriangles(0),*Escape(PropPaths[0]),*Escape(PropPaths[1]),
            *Escape(Preview->GetPathName()),Preview->GetSizeX(),Preview->GetSizeY(),*Vec(Frame->GetRelativeScale3D()),SizeCm,Tilt,Fov);
        return true;
    }
    bool ApplyOn() {
        bOnApplied=Lens->ApplyToCamera(Camera.Get(),.8f);OnBlendableCount=Camera->PostProcessSettings.WeightedBlendables.Array.Num();
        bool FoundParameter=false;
        for (const auto& Item:Camera->PostProcessSettings.WeightedBlendables.Array) if (auto* Material=Cast<UMaterialInstanceDynamic>(Item.Object)) {
            float Strength=0;if (Material->GetScalarParameterValue(TEXT("LensStrength"),Strength) && FMath::IsNearlyEqual(Strength,.8f) && FMath::IsNearlyEqual(Item.Weight,1.f)) FoundParameter=true;
        }
        if (!bOnApplied || OnBlendableCount!=OffBlendableCount+1 || !FoundParameter) {
            Finish(false,TEXT("Actual lens application did not add exactly one strength-.8 material to the drone camera."));return false;
        }
        return true;
    }
    bool CheckLensPerfSettings() {
        const auto& Console=IConsoleManager::Get();
        const auto* MaxFps=Console.FindConsoleVariable(TEXT("t.MaxFPS"));
        const auto* VSync=Console.FindConsoleVariable(TEXT("r.VSync"));
        const auto* DynamicResolution=Console.FindConsoleVariable(TEXT("r.DynamicRes.OperationMode"));
        const auto* ScreenPercentage=Console.FindConsoleVariable(TEXT("r.ScreenPercentage"));
        LensPerfMaxFps=MaxFps?MaxFps->GetFloat():-1.f;
        LensPerfVSync=VSync?VSync->GetInt():-1;
        LensPerfDynamicResolutionMode=DynamicResolution?DynamicResolution->GetInt():-1;
        LensPerfScreenPercentage=ScreenPercentage?ScreenPercentage->GetFloat():-1.f;
        LensPerfEffectiveMaxTickRate=GEngine?GEngine->GetMaxTickRate(1.f/60.f):-1.f;
        if (!GEngine || GEngine->bUseFixedFrameRate || LensPerfMaxFps!=0.f || LensPerfEffectiveMaxTickRate!=0.f ||
            LensPerfVSync!=0 || LensPerfDynamicResolutionMode!=0 || LensPerfScreenPercentage!=100.f) {
            Finish(false,FString::Printf(TEXT("Lens timing needs uncapped frames, VSync and dynamic resolution off, and 100%% screen percentage. MaxFPS=%.3f effective=%.3f fixed=%d vsync=%d dynamicRes=%d screenPercentage=%.3f."),
                LensPerfMaxFps,LensPerfEffectiveMaxTickRate,GEngine && GEngine->bUseFixedFrameRate,LensPerfVSync,LensPerfDynamicResolutionMode,LensPerfScreenPercentage));
            return false;
        }
        return true;
    }
    bool CheckLensPerfView() const {
        auto* Client=LensPerfClient.Get();auto* Own=Drone.Get();auto* PC=Controller.Get();auto* CameraManager=Manager.Get();
        return World.IsValid() && Client && World->GetGameViewport()==Client && Client->Viewport==LensPerfViewport &&
            !Client->bDisableWorldRendering && Client->GetEngineShowFlags()->PostProcessing &&
            Client->Viewport->GetSizeXY()==LensPerfViewportSize && !FScreenshotRequest::IsScreenshotRequested() &&
            Own && !Own->IsFlightArmed() && Own->IsFlightInputSuspended()==bLensPerfInputSuspended && PC &&
            PC->GetViewTarget()==Own && PC->GetPawn()==Pioneer.Get() && PC->PlayerCameraManager==CameraManager && CameraManager &&
            CameraManager->GetCameraLocation().Equals(LensPerfViewPose.GetLocation(),.1) &&
            CameraManager->GetCameraRotation().Quaternion().AngularDistance(LensPerfViewPose.GetRotation())<=FMath::DegreesToRadians(.02) &&
            FMath::IsNearlyEqual(CameraManager->GetFOVAngle(),DroneFov,.1f);
    }
    void ObserveLensPerfFrame() {
        const double Now=FPlatformTime::Seconds();
        if (Phase!=EPhase::MeasureLens || bLensPerfWindowComplete || !LensPerfFailure.IsEmpty() || Now-PhaseStarted<LensPerfWarmupSeconds) return;
        if (LensPerfDrawFrame!=GFrameCounter || !CheckLensPerfView() || (LensPerfPreviousTime>0. && LensPerfPreviousFrame+1!=GFrameCounter)) {
            LensPerfFailure=TEXT("A timed frame lacked an actual viewport draw, or the fixed view, input state or screenshot state changed.");return;
        }
        if (LensPerfPreviousTime>0.) {
            const double IntervalMs=(Now-LensPerfPreviousTime)*1000.;
            if (!FMath::IsFinite(IntervalMs) || IntervalMs<=0. || LensPerfIntervals.Num()>=8192) {
                LensPerfFailure=TEXT("A timed frame had an invalid wall interval or exceeded the sample bound.");return;
            }
            LensPerfIntervals.Add(IntervalMs);
        } else LensPerfFirstTime=Now;
        LensPerfPreviousFrame=GFrameCounter;LensPerfPreviousTime=Now;
        bLensPerfWindowComplete=Now-LensPerfFirstTime>=LensPerfWindowSeconds;
    }
    bool BeginLensPerfWindow(double Now) {
        const auto& Window=LensPerfWindows[LensPerfWindowIndex];
        if (Window.Strength>0.f) {
            if (!ApplyOn()) return false;
        } else if (LensPerfWindowIndex>0) {
            bOffAfterApplied=Lens->ApplyToCamera(Camera.Get(),0.f);
            if (!bOffAfterApplied || !SameBlendables(OffBlendables,Camera->PostProcessSettings.WeightedBlendables.Array)) {
                Finish(false,TEXT("Lens off failed to restore the actual camera blendables before timing."));return false;
            }
        }
        LensPerfExpectedBlendables=Camera->PostProcessSettings.WeightedBlendables.Array;
        LensPerfIntervals.Reset();LensPerfPreviousTime=LensPerfFirstTime=0.;bLensPerfWindowComplete=false;
        Settle(EPhase::MeasureLens,Now);
        LastReason=FString::Printf(TEXT("Measuring viewport-drawn wall intervals for %s."),Window.Label);
        return true;
    }
    void BeginLensPerf(double Now) {
        auto* Client=World->GetGameViewport();
        if (!Client || !Client->Viewport || Client->Viewport->GetSizeXY().X<320 || Client->Viewport->GetSizeXY().Y<180 ||
            FScreenshotRequest::IsScreenshotRequested()) {
            Finish(false,TEXT("Lens timing requires a real off-screen viewport and no pending screenshot."));return;
        }
        // Save loading reapplies the game's resolution setting after startup
        // commands. Pin this process's render scale before the first warmup.
        if (auto* ScreenPercentage=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
            ScreenPercentage->Set(100.f,ECVF_SetByConsole);
        if (!CheckLensPerfSettings()) return;
        LensPerfClient=Client;LensPerfViewport=Client->Viewport;LensPerfViewportSize=Client->Viewport->GetSizeXY();
        LensPerfViewPose=FTransform(Manager->GetCameraRotation(),Manager->GetCameraLocation());
        bLensPerfInputSuspended=Drone->IsFlightInputSuspended();
        if (!LensPerfViewPose.IsValid() || !CheckLensPerfView()) { Finish(false,TEXT("Lens timing could not pin the rendered camera and viewport."));return; }
        const TWeakPtr<FRun> Weak=AsShared();
        LensPerfDrawHandle=Client->OnDrawn().AddLambda([Weak] { if (const auto Run=Weak.Pin()) Run->LensPerfDrawFrame=GFrameCounter; });
        LensPerfEndFrameHandle=FCoreDelegates::OnEndFrame.AddLambda([Weak] { if (const auto Run=Weak.Pin()) Run->ObserveLensPerfFrame(); });
        LensPerfIntervals.Reserve(512);
        BeginLensPerfWindow(Now);
    }
    void TickLensPerf(double Now) {
        if (!LensPerfFailure.IsEmpty()) { Finish(false,LensPerfFailure);return; }
        if (!CheckLensPerfSettings()) return;
        if (!CheckLensPerfView() || !SameBlendables(LensPerfExpectedBlendables,Camera->PostProcessSettings.WeightedBlendables.Array)) {
            Finish(false,TEXT("Lens timing viewport, rendered camera or current blendables changed."));return;
        }
        if (!bLensPerfWindowComplete) return;
        auto& Window=LensPerfWindows[LensPerfWindowIndex];
        Window.Samples=LensPerfIntervals.Num();Window.ElapsedSeconds=LensPerfPreviousTime-LensPerfFirstTime;
        if (Window.Samples<10) { Finish(false,TEXT("Lens timing collected fewer than ten viewport-drawn intervals in a window."));return; }
        LensPerfIntervals.Sort();
        Window.MedianMs=(LensPerfIntervals[(Window.Samples-1)/2]+LensPerfIntervals[Window.Samples/2])*.5;
        Window.P95Ms=LensPerfIntervals[FMath::CeilToInt(.95*Window.Samples)-1];
        if (++LensPerfWindowIndex<LensPerfWindowCount) { BeginLensPerfWindow(Now);return; }
        bLensPerfVerified=true;
        ReleaseLensPerf();BeginReturn(Now);
    }
    bool RouteSettingsStable() {
        const auto& Console=IConsoleManager::Get();
        auto Value=[&](const TCHAR* Name) { const auto* CVar=Console.FindConsoleVariable(Name);return CVar?CVar->GetFloat():-1.f; };
        const float Scale=Value(TEXT("r.ScreenPercentage"));
        RouteMaxFps=Value(TEXT("t.MaxFPS"));RouteVSync=static_cast<int32>(Value(TEXT("r.VSync")));
        RouteDynamicResolutionMode=static_cast<int32>(Value(TEXT("r.DynamicRes.OperationMode")));
        RouteEffectiveMaxTickRate=GEngine?GEngine->GetMaxTickRate(1.f/60.f):-1.f;
        if (RouteScreenPercentage<0.f) RouteScreenPercentage=Scale;
        if (!GEngine || GEngine->bUseFixedFrameRate || RouteMaxFps!=0.f || RouteEffectiveMaxTickRate!=0.f || RouteVSync!=0 ||
            RouteDynamicResolutionMode!=0 || !FMath::IsFinite(Scale) || Scale<=0.f || Scale!=RouteScreenPercentage) {
            Finish(false,TEXT("Streaming route requires uncapped draws, VSync and dynamic resolution off, and an unchanged positive screen percentage."));return false;
        }
        return true;
    }
    bool RouteSelectDestination() {
        const auto* Partition=RoutePartition.Get();
        if (!Partition || !Partition->RuntimeHash) return false;
        const FVector Directions[]={FVector(1,0,0),FVector(0,1,0),FVector(-1,0,0),FVector(0,-1,0)};
        for (const double Length:{125000.,160000.,195000.}) for (const FVector& Direction:Directions) {
            const FVector Target=OriginalPioneerLocation+Direction*Length;
            FWorldPartitionStreamingQuerySource Query(Partition->GetInstanceTransform().InverseTransformPosition(Target));
            Query.bUseGridLoadingRange=false;Query.Radius=RouteTargetRadiusCm;
            if (const auto* Layers=Partition->GetDataLayerManager()) Query.DataLayers=Layers->GetEffectiveActiveDataLayerNames().Array();
            Partition->RuntimeHash->ForEachStreamingCellsQuery(Query,[&](const UWorldPartitionRuntimeCell* Cell) {
                if (Cell && !Cell->IsAlwaysLoaded() && !Cell->GetIsHLOD() && Cell->GetCurrentState()==EWorldPartitionRuntimeCellState::Unloaded)
                    RouteInitiallyUnloadedCells.AddUnique(Cell);
                return true;
            },nullptr);
            if (!RouteInitiallyUnloadedCells.IsEmpty()) { RouteDirection=Direction;RouteLengthCm=Length;return true; }
        }
        return false;
    }
    bool RouteCachedSourceNear(FName Name,const FVector& Location,double ToleranceCm) const {
        const auto* Partition=RoutePartition.Get();
        if (!Partition || Name.IsNone()) return false;
        const FVector Local=Partition->GetInstanceTransform().InverseTransformPosition(Location);
        return Partition->GetStreamingSources().ContainsByPredicate([&](const FWorldPartitionStreamingSource& Actual) {
            return Actual.Name==Name && Actual.TargetState==EStreamingSourceTargetState::Activated &&
                FVector::DistSquared(Local,Actual.Location)<=FMath::Square(ToleranceCm);
        });
    }
    bool ObserveRouteState(double Now) {
        auto* PC=Controller.Get();auto* Own=Drone.Get();auto* Character=Pioneer.Get();auto* Anchor=RouteAnchor.Get();auto* Subsystem=RouteSubsystem.Get();
        auto* Client=RouteViewportClient.Get();auto* DroneSource=RouteDroneSource.Get();auto* PioneerSource=RoutePioneerSource.Get();
        auto* Move=Character?Character->GetCharacterMovement():nullptr;
        FWorldPartitionStreamingSource DroneReport,PioneerReport;
        bRouteInputSuspended=Own && Own->IsFlightInputSuspended();
        if (!PC || !bRouteInputSuspended || !Own || Own->IsFlightArmed() || !Character || !Character->IsAliveAndWell() || !Anchor || !Anchor->IsHolding() ||
            !Move || Move->MovementMode!=MOVE_None || !Subsystem || !RoutePartition.IsValid() || World->GetWorldPartition()!=RoutePartition.Get() ||
            !Client || World->GetGameViewport()!=Client || Client->Viewport!=RouteViewport || Client->Viewport->GetSizeXY()!=RouteViewportSize ||
            Client->bDisableWorldRendering || !Client->GetEngineShowFlags()->PostProcessing || FScreenshotRequest::IsScreenshotRequested() ||
            !DroneSource || !PioneerSource || !Subsystem->IsStreamingSourceProviderRegistered(DroneSource) || !Subsystem->IsStreamingSourceProviderRegistered(PioneerSource) ||
            !DroneSource->GetStreamingSource(DroneReport) || DroneReport.Name!=RouteDroneSourceName || !DroneReport.Location.Equals(Own->GetActorLocation(),.1) ||
            !PioneerSource->GetStreamingSource(PioneerReport) || PioneerReport.Name!=RoutePioneerSourceName || !PioneerReport.Location.Equals(OriginalPioneerLocation,.5)) {
            Finish(false,TEXT("The streaming route lost its disarmed viewport, held pioneer, or registered production streaming providers."));return false;
        }
        RouteMaxPioneerDriftCm=FMath::Max(RouteMaxPioneerDriftCm,FVector::Distance(Character->GetActorLocation(),OriginalPioneerLocation));
        FFindFloorResult Floor;Move->FindFloor(Character->GetActorLocation(),Floor,false);
        bRoutePioneerFloorRetained=Floor.IsWalkableFloor();
        bRoutePioneerSourceHeld=RouteCachedSourceNear(RoutePioneerSourceName,OriginalPioneerLocation,.5);
        if (RouteMaxPioneerDriftCm>.5 || !bRoutePioneerFloorRetained || !bRoutePioneerSourceHeld) {
            Finish(false,TEXT("Original pioneer drifted or lost its native walkable floor or engine-consumed streaming source."));return false;
        }
        if (Phase==EPhase::MeasureStreamingRoute) {
            ++RouteReadinessSamples;
            if (!DroneSource->IsStreamingCompleted()) {
                ++RouteIncompleteSamples;
                if (RouteIncompleteStarted==0.) RouteIncompleteStarted=Now;
                RouteMaxIncompleteSeconds=FMath::Max(RouteMaxIncompleteSeconds,Now-RouteIncompleteStarted);
            } else RouteIncompleteStarted=0.;
        }
        for (const auto& Candidate:RouteInitiallyUnloadedCells) if (const auto* Cell=Candidate.Get();Cell && Cell->GetCurrentState()==EWorldPartitionRuntimeCellState::Activated) {
            const FName Package=Cell->GetLevelPackageName();
            if (!Package.IsNone()) RouteActivatedCells.AddUnique(Package.ToString());
        }
        return RouteSettingsStable();
    }
    void ObserveStreamingRouteFrame() {
        // The ticker can attach after this frame's viewport draw. The first
        // observed interval starts with a complete subsequent frame.
        if (Phase!=EPhase::MeasureStreamingRoute || GFrameCounter<=PhaseFrame || bRouteReachedEnd || !RouteFailure.IsEmpty()) return;
        const double Now=FPlatformTime::Seconds();auto* Own=Drone.Get();auto* PC=Controller.Get();auto* View=Camera.Get();auto* CameraManager=Manager.Get();
        const bool Drawn=RouteDrawFrame==GFrameCounter;
        const bool Consecutive=RoutePreviousTime==0. || RoutePreviousFrame+1==GFrameCounter;
        const bool TargetMatches=Own && PC && PC->GetViewTarget()==Own && Own->IsFlightInputSuspended();
        const double ViewOffset=View && CameraManager ? FVector::Distance(View->GetComponentLocation(),CameraManager->GetCameraLocation()) : -1.;
        if (!Drawn || !Consecutive || !TargetMatches || !FMath::IsFinite(ViewOffset) || ViewOffset<0. || ViewOffset>25.) {
            RouteFailure=FString::Printf(TEXT("Invalid timed route frame at %.1f m: drawn=%d consecutive=%d disarmed view target=%d camera offset=%.2f cm."),
                RouteDistanceCm/100.,Drawn,Consecutive,TargetMatches,ViewOffset);return;
        }
        if (RoutePreviousTime==0.) { RouteFirstTime=RoutePreviousTime=Now;RoutePreviousFrame=GFrameCounter;return; }
        const double IntervalMs=(Now-RoutePreviousTime)*1000.;
        if (!FMath::IsFinite(IntervalMs) || IntervalMs<=0. || RouteIntervals.Num()>=16384) {
            RouteFailure=TEXT("A streaming frame interval was invalid or exceeded the sample bound.");return;
        }
        RouteIntervals.Add(IntervalMs);RoutePreviousTime=Now;RoutePreviousFrame=GFrameCounter;
        const double Along=FMath::Min(RouteLengthCm,(Now-RouteFirstTime)*RouteSpeedCmPerSecond);
        const FVector Target=RouteStart+RouteDirection*Along;
        FHitResult Hit;
        Own->SetActorLocation(Target,true,&Hit,ETeleportType::TeleportPhysics);
        if (Hit.bBlockingHit || !Own->GetActorLocation().Equals(Target,.5)) {
            RouteFailure=FString::Printf(TEXT("Native sweep blocked the scripted horizontal route at %.1f m (%s)."),Along/100.,*GetNameSafe(Hit.GetActor()));return;
        }
        RouteDistanceCm=FVector::Dist2D(RouteStart,Own->GetActorLocation());
        RoutePioneerSeparationCm=FVector::Dist2D(OriginalPioneerLocation,Own->GetActorLocation());
        bRouteReachedEnd=Along>=RouteLengthCm;
        if (bRouteReachedEnd) RouteElapsedSeconds=Now-RouteFirstTime;
    }
    void BeginStreamingRoute(double Now) {
        auto* Own=Drone.Get();auto* Client=World->GetGameViewport();auto* Partition=World->GetWorldPartition();
        RoutePartition=Partition;RouteSubsystem=World->GetSubsystem<UWorldPartitionSubsystem>();
        if (!Own || !Own->IsFlightInputSuspended() || !Client || !Client->Viewport || !Partition || !Partition->IsStreamingEnabled() || !RouteSubsystem.IsValid()) {
            Finish(false,TEXT("Streaming route needs a suspended off-screen drone, real viewport and enabled World Partition."));return;
        }
        RouteViewportClient=Client;RouteViewport=Client->Viewport;RouteViewportSize=RouteViewport->GetSizeXY();
        RouteDroneSource=Own->FindComponentByClass<UWorldPartitionStreamingSourceComponent>();
        for (TActorIterator<AFactoryFPVPioneerAnchor> It(World.Get());It;++It) if (It->IsHolding() && It->GetActorLocation().Equals(OriginalPioneerLocation,.5)) {
            if (RouteAnchor.IsValid()) { Finish(false,TEXT("More than one held pioneer anchor matched the streaming route."));return; }
            RouteAnchor=*It;RoutePioneerSource=It->FindComponentByClass<UWorldPartitionStreamingSourceComponent>();
        }
        FWorldPartitionStreamingSource DroneReport,PioneerReport;
        if (!RouteDroneSource.IsValid() || !RoutePioneerSource.IsValid() || !RouteDroneSource->GetStreamingSource(DroneReport) ||
            !RoutePioneerSource->GetStreamingSource(PioneerReport) || DroneReport.Name.IsNone() || PioneerReport.Name.IsNone() || DroneReport.Name==PioneerReport.Name) {
            Finish(false,TEXT("Could not identify both independent production streaming sources."));return;
        }
        RouteDroneSourceName=DroneReport.Name;RoutePioneerSourceName=PioneerReport.Name;
        if (!ObserveRouteState(Now)) return;
        if (!RouteSelectDestination()) { Finish(false,TEXT("No initially unloaded, non-HLOD native cells near bounded 1.25–1.95 km route endpoints."));return; }
        RouteStart=Own->GetActorLocation()+FVector(0,0,RouteLiftCm);
        const auto* Body=Cast<USphereComponent>(Own->GetRootComponent());
        if (!Body || World->OverlapBlockingTestByChannel(RouteStart,FQuat::Identity,Body->GetCollisionObjectType(),
            FCollisionShape::MakeSphere(Body->GetScaledSphereRadius()),FCollisionQueryParams(SCENE_QUERY_STAT(FactoryFPVStreamingRouteLift),false,Own),
            FCollisionResponseParams(Body->GetCollisionResponseToChannels())) ||
            !Own->SetActorLocation(RouteStart,false,nullptr,ETeleportType::TeleportPhysics)) {
            Finish(false,TEXT("The bounded QA-only lift could not place the drone above the launch site."));return;
        }
        Settle(EPhase::SettleStreamingLift,Now);LastReason=TEXT("Waiting for the production drone source at the scripted lift before horizontal timing.");
    }
    void TickStreamingRoute(double Now) {
        if (!RouteFailure.IsEmpty()) { Finish(false,RouteFailure);return; }
        if (!ObserveRouteState(Now)) return;
        if (Phase==EPhase::SettleStreamingLift && Now-PhaseStarted>=.75 && RouteCachedSourceNear(RouteDroneSourceName,RouteStart,1200.)) {
            const TWeakPtr<FRun> Weak=AsShared();
            RouteDrawHandle=RouteViewportClient->OnDrawn().AddLambda([Weak] { if (const auto Run=Weak.Pin()) Run->RouteDrawFrame=GFrameCounter; });
            RouteEndFrameHandle=FCoreDelegates::OnEndFrame.AddLambda([Weak] { if (const auto Run=Weak.Pin()) Run->ObserveStreamingRouteFrame(); });
            RouteIntervals.Reserve(8192);Settle(EPhase::MeasureStreamingRoute,Now);
            LastReason=TEXT("Measuring native viewport-drawn intervals on the scripted horizontal route.");
        } else if (Phase==EPhase::MeasureStreamingRoute && bRouteReachedEnd) {
            ReleaseStreamingRoute();RouteIntervals.Sort();const int32 Count=RouteIntervals.Num();
            if (Count<120 || RouteDistanceCm<RouteMinimumDistanceCm || RoutePioneerSeparationCm<RouteMinimumDistanceCm) {
                Finish(false,TEXT("Streaming route did not reach its 1.2 km minimum with 120 rendered intervals."));return;
            }
            RouteMedianMs=(RouteIntervals[(Count-1)/2]+RouteIntervals[Count/2])*.5;
            RouteP95Ms=RouteIntervals[FMath::CeilToInt(.95*Count)-1];RouteMaxMs=RouteIntervals.Last();
            Settle(EPhase::SettleStreamingEnd,Now);LastReason=TEXT("Waiting for the remote consumed source and previously unloaded native endpoint cells to activate.");
        } else if (Phase==EPhase::SettleStreamingEnd && Now-PhaseStarted>=.5) {
            bRouteDroneSourceFollowed=RouteCachedSourceNear(RouteDroneSourceName,Drone->GetActorLocation(),1200.);
            if (bRouteDroneSourceFollowed && !RouteActivatedCells.IsEmpty() && RouteDroneSource->IsStreamingCompleted()) {
                RouteActivatedCells.Sort();BeginReturn(Now);
            }
        }
    }
    bool StreamingRouteSourcesRetired() const {
        const auto* Partition=RoutePartition.Get();const auto* Subsystem=RouteSubsystem.Get();
        auto Retired=[&](const TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>& Source) {
            return !Source.IsValid() || (!Source->IsStreamingSourceEnabled() && !Subsystem->IsStreamingSourceProviderRegistered(Source.Get()));
        };
        return Partition && Subsystem && Retired(RouteDroneSource) && Retired(RoutePioneerSource) &&
            !Partition->GetStreamingSources().ContainsByPredicate([&](const FWorldPartitionStreamingSource& Actual) {
                return Actual.Name==RouteDroneSourceName || Actual.Name==RoutePioneerSourceName;
            });
    }
    bool RequestCapture(FCapture& Capture,const TCHAR* Name,bool ShowUI,double Now) {
        auto* Client=World.IsValid()?World->GetGameViewport():nullptr;
        if (FScreenshotRequest::IsScreenshotRequested() || !Client || !Client->Viewport || Client->Viewport->GetSizeXY().X<320 || Client->Viewport->GetSizeXY().Y<180) {
            Finish(false,TEXT("Actual viewport unavailable or an unrelated screenshot is already pending."));return false;
        }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);
        const FString Directory=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));FString Path=FPaths::Combine(Directory,Name);FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Path,Directory) ||
            !FPaths::IsUnderDirectory(Directory,Saved) || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Directory,true)) {
            Finish(false,TEXT("Screenshot output is not fresh and inside the guarded disposable Saved directory."));return false;
        }
        if (bNativeMenusEnabled) RecordHud(TEXT("request.")+FString(Name));
        ReleaseScreenshot();Capture.Path=PendingScreenshot=Path;CaptureStarted=Now;bScreenshotProcessed=false;bOwnScreenshotPending=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> Weak=AsShared();
        ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]() {
            if (const auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshotPending) { Run->bScreenshotProcessed=true;Run->bOwnScreenshotPending=false; }
        });
        FScreenshotRequest::RequestScreenshot(Path,ShowUI,false,false);LastReason=TEXT("Waiting for real viewport screenshot: ")+Path;
        return true;
    }
    bool CaptureFinished(FCapture& Capture,double Now) {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Actual viewport screenshot exceeded five seconds: ")+Capture.Path);return false; }
        if (!bScreenshotProcessed) return false;
        ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) ||
            Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180 || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty()) {
            Finish(false,TEXT("Screenshot completion produced no sane decodable PNG: ")+Capture.Path);return false;
        }
        double Sum=0,SumSquares=0;int32 Samples=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60)) {
            const auto Color=Image.GetOnePixelLinear(X,Y);
            if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) { Finish(false,TEXT("Screenshot has nonfinite pixels."));return false; }
            const double Luma=.2126*Color.R+.7152*Color.G+.0722*Color.B;Sum+=Luma;SumSquares+=Luma*Luma;++Samples;
        }
        Capture.LumaVariance=FMath::Max(0.,SumSquares/Samples-FMath::Square(Sum/Samples));
        if (Capture.LumaVariance<1e-6) { Finish(false,TEXT("Screenshot is too uniform to qualify rendered scenery: ")+Capture.Path);return false; }
        Capture.Size=FIntPoint(Image.SizeX,Image.SizeY);Capture.Image=MoveTemp(Image);Capture.bDecoded=true;
        if (bNativeMenusEnabled) RecordHud(TEXT("captured.")+FPaths::GetCleanFilename(Capture.Path));
        return true;
    }
    static FLinearColor Sample(const FImage& Image,double U,double V) {
        const double X=FMath::Clamp(U*Image.SizeX-.5,0.,double(Image.SizeX-1)),Y=FMath::Clamp(V*Image.SizeY-.5,0.,double(Image.SizeY-1));
        const int32 X0=FMath::FloorToInt(X),Y0=FMath::FloorToInt(Y),X1=FMath::Min(X0+1,Image.SizeX-1),Y1=FMath::Min(Y0+1,Image.SizeY-1);
        return FMath::Lerp(FMath::Lerp(Image.GetOnePixelLinear(X0,Y0),Image.GetOnePixelLinear(X1,Y0),float(X-X0)),
            FMath::Lerp(Image.GetOnePixelLinear(X0,Y1),Image.GetOnePixelLinear(X1,Y1),float(X-X0)),float(Y-Y0));
    }
    static double Error(const FLinearColor& A,const FLinearColor& B) { return (FMath::Abs(A.R-B.R)+FMath::Abs(A.G-B.G)+FMath::Abs(A.B-B.B))/3.; }
    bool ComparePixels() {
        if (OffBefore.Size!=On.Size || OffBefore.Size!=OffAfter.Size) { Finish(false,TEXT("Lens screenshots changed viewport dimensions."));return false; }
        const double Aspect=double(On.Size.X)/On.Size.Y;int32 Changed=0,CenterSamples=0;
        for (int32 Y=0;Y<40;++Y) for (int32 X=0;X<72;++X) {
            const double U=.03+.94*(X+.5)/72.,V=.03+.94*(Y+.5)/40.;
            const double CX=2*U-1,CY=2*V-1,R2=(CX*CX*Aspect*Aspect+CY*CY)/(Aspect*Aspect+1.);
            const double Scale=(1.+.8*R2)/1.8,SU=.5+CX*Scale*.5,SV=.5+CY*Scale*.5;
            const auto Before=Sample(OffBefore.Image,U,V),After=Sample(OffAfter.Image,U,V),Actual=Sample(On.Image,U,V);
            const auto Reference=(Before+After)*.5f,Expected=(Sample(OffBefore.Image,SU,SV)+Sample(OffAfter.Image,SU,SV))*.5f;
            const double Difference=Error(Actual,Reference);OffOnMeanError+=Difference;PredictedWarpMeanError+=Error(Actual,Expected);
            OffRestoreMeanError+=Error(Before,After);Changed+=Difference>.02;++PixelSamples;
            if (CX*CX+CY*CY<.01) { CenterMeanError+=Difference;++CenterSamples; }
        }
        OffOnMeanError/=PixelSamples;PredictedWarpMeanError/=PixelSamples;OffRestoreMeanError/=PixelSamples;
        ChangedFraction=double(Changed)/PixelSamples;CenterMeanError=CenterSamples?CenterMeanError/CenterSamples:0.;
        bPixelsPassed=ChangedFraction>.05 && OffOnMeanError>FMath::Max(.006,OffRestoreMeanError*2.) &&
            PredictedWarpMeanError<OffOnMeanError*.85+OffRestoreMeanError && OffRestoreMeanError<FMath::Max(.035,OffOnMeanError*.7);
        if (!bPixelsPassed) { Finish(false,TEXT("Real-scene lens change/radial-remap match/off-restoration did not exceed measured temporal tolerance; inspect retained PNGs and metrics."));return false; }
        return true;
    }
    bool CheckRestored(bool bRecordEvidence=true) {
        auto* PC=Controller.Get();auto* Character=Pioneer.Get();auto* Move=Character?Character->GetCharacterMovement():nullptr;
        const double TranslationCm=Character?FVector::Distance(Character->GetActorLocation(),OriginalPioneerLocation):-1.;
        bool bInputStateMatches=PC!=nullptr;
        if (PC) {
            for (const auto& Input:OriginalInputs) {
                const bool Changed=Input.Component.IsValid()?PC->IsInputComponentInStack(Input.Component.Get())!=Input.bMember:Input.bMember;
                if (Changed) bInputStateMatches=false;
            }
            for (const auto& Input:SessionInputs) if (Input.IsValid() && PC->IsInputComponentInStack(Input.Get())) bInputStateMatches=false;
            for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It)) {
                if (!OriginalInputs.ContainsByPredicate([&](const FInputSnapshot& Item) { return Item.Component.Get()==*It; })) bInputStateMatches=false;
            }
        }
        bool bCameraStateMatches=!PioneerCameras.IsEmpty();
        for (const auto& Item:PioneerCameras) if (!Item.Camera.IsValid() || !SameBlendables(Item.Blendables,Item.Camera->PostProcessSettings.WeightedBlendables.Array) ||
            !FMath::IsNearlyEqual(Item.BlendWeight,Item.Camera->PostProcessBlendWeight) || !FMath::IsNearlyEqual(Item.Fov,Item.Camera->FieldOfView)) bCameraStateMatches=false;
        const bool bStateMatches=PC && Character && Character->IsAliveAndWell() && Move && Manager.IsValid() && Hud.IsValid() &&
            FMath::IsFinite(TranslationCm) && TranslationCm>=0. && TranslationCm<=.5 &&
            PC->GetPawn()==Character && PC->PlayerCameraManager==Manager.Get() && PC->GetHUD()==Hud.Get() &&
            PC->GetViewTarget()==OriginalView.Get() && Move->MovementMode==OriginalMovement && Move->CustomMovementMode==OriginalCustomMovement &&
            PC->IsMoveInputIgnored()==bOriginalIgnoreMove && PC->IsLookInputIgnored()==bOriginalIgnoreLook && PC->bShowMouseCursor==bOriginalCursor &&
            Hud->GetPartialPumpiMode()==bOriginalPartialHud && Hud->GetHiddenHUDMode()==bOriginalHiddenHud && Hud->GetHUDVisibility()==bOriginalHudVisible && Hud->bShowHUD==bOriginalShowHud &&
            FMath::IsNearlyEqual(Manager->GetLockedFOV(),OriginalLockedFov,.01f) && FMath::IsNearlyEqual(Manager->GetFOVAngle(),OriginalFov,.1f) &&
            !AFactoryFPVDrone::FindActive(World.Get()) && !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Character) && bInputStateMatches && bCameraStateMatches;
        if (bRecordEvidence) {
            PioneerReturnTranslationCm=TranslationCm;
            bInputsRestored=bInputStateMatches;
            bPioneerCamerasUnchanged=bCameraStateMatches;
            bPioneerRestored=bStateMatches;
        }
        return bStateMatches;
    }
    void Settle(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    FString InputState() const {
        auto* PC=Controller.Get();const auto Lease=FactoryFPVInputLease::GetDiagnostics(PC);
        auto* Client=World.IsValid()?World->GetGameViewport():nullptr;
        return FString::Printf(TEXT("{\"nativeHooksReady\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"resetDepth\":%d,\"moveIgnored\":%s,\"lookIgnored\":%s,\"cursor\":%s,\"viewportIgnoresInput\":%s}"),
            Lease.bNativeHooksReady?TEXT("true"):TEXT("false"),Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions,Lease.ResetDepth,
            PC && PC->IsMoveInputIgnored()?TEXT("true"):TEXT("false"),PC && PC->IsLookInputIgnored()?TEXT("true"):TEXT("false"),PC && PC->bShowMouseCursor?TEXT("true"):TEXT("false"),Client && Client->IgnoreInput()?TEXT("true"):TEXT("false"));
    }
    bool NativeInteractionClear() const {
        auto* PC=Controller.Get();auto* UI=PC?PC->GetGameUI():nullptr;const auto* Client=World.IsValid()?World->GetGameViewport():nullptr;
        const auto* Console=Client?Client->ViewportConsole.Get():nullptr;
        return PC && UI && !World->IsPaused() && !PC->IsPaused() && !PC->IsPauseMenuOpen() && !UI->IsPauseMenuOpen() && !UI->HasActiveInteractWidget() && (!Console || Console->ConsoleState==NAME_None);
    }
    bool ProbeBoundRecovery(bool bMenuOpen,double& OffsetCm) {
        auto* Own=Drone.Get();auto* PC=Controller.Get();auto* Component=Camera.Get();
        const auto* Body=Own?Cast<USphereComponent>(Own->GetRootComponent()):nullptr;
        const auto SessionIntact=[&] {
            return Drone.Get()==Own && !Own->IsActorBeingDestroyed() && !Own->IsFlightArmed() && Own->GetOwner()==PC &&
                PC->GetPawn()==Pioneer.Get() && PC->GetViewTarget()==Own && AFactoryFPVDrone::FindActive(World.Get())==Own;
        };
        if (!Body || !PC || !Component || !World.IsValid() || !DroneLaunchPose.IsValid() || !SessionIntact() ||
            NativeInteractionClear()==bMenuOpen) {
            Finish(false,TEXT("Bound F9 probe requires the original disarmed session and the expected native menu ownership."));return false;
        }
        FInputActionUnifiedDelegate RecoverKey;int32 Count=0;
        for (const auto& Weak:SessionInputs) if (auto* Input=Weak.Get();Input && PC->IsInputComponentInStack(Input))
            for (const auto& Binding:Input->KeyBindings)
                if (Binding.Chord.Key==EKeys::F9 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Own)) {
                    RecoverKey=Binding.KeyDelegate;++Count;
                }
        if (Count!=1) { Finish(false,TEXT("Actual FPV session has no unique bound F9 press callback."));return false; }

        const FVector Launch=DroneLaunchPose.GetLocation();
        const auto Shape=FCollisionShape::MakeSphere(Body->GetScaledSphereRadius());
        const auto Channel=Body->GetCollisionObjectType();
        const FCollisionResponseParams Responses(Body->GetCollisionResponseToChannels());
        FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingMenuRecovery),false,Own);
        Query.AddIgnoredActor(Pioneer.Get());
        FHitResult Ground;
        if (World->OverlapBlockingTestByChannel(Launch,FQuat::Identity,Channel,Shape,Query,Responses) ||
            !World->SweepSingleByChannel(Ground,Launch,Launch-FVector(0,0,10),FQuat::Identity,Channel,Shape,Query,Responses) ||
            Ground.bStartPenetrating || Ground.ImpactNormal.Z<.5) {
            Finish(false,TEXT("Bound F9 probe cannot prove refusal at an obstructed or unsupported launch pose."));return false;
        }

        // The live disarmed drone can settle a little below its saved launch
        // clearance. Restore the actual pre-probe transform for the camera.
        const FTransform Before=Own->GetActorTransform();
        const FTransform ViewBefore=Component->GetComponentTransform();
        const FString InputBefore=InputState();
        FVector Probe=Launch;
        for (const double Height:{100.,50.,25.}) {
            const FVector Target=Launch+FVector(0,0,Height);
            FHitResult Hit;
            if (!World->SweepSingleByChannel(Hit,Before.GetLocation(),Target,FQuat::Identity,Channel,Shape,Query,Responses) &&
                !World->OverlapBlockingTestByChannel(Target,FQuat::Identity,Channel,Shape,Query,Responses)) {
                Probe=Target;break;
            }
        }
        if (Probe==Launch || !Own->SetActorLocation(Probe,false,nullptr,ETeleportType::TeleportPhysics) || !Own->GetActorLocation().Equals(Probe,.01)) {
            Finish(false,TEXT("No clear distinct pose was available for the bound F9 probe."));return false;
        }
        OffsetCm=FVector::Distance(Own->GetActorLocation(),Launch);
        const FTransform Offset=Own->GetActorTransform();
        // Invoke the real binding in one tick. A normal disarmed simulation
        // tick could otherwise reconcile the probe pose before it is observed.
        RecoverKey.Execute(EKeys::F9);
        const bool bExpectedResult=SessionIntact() && InputState()==InputBefore && NativeInteractionClear()!=bMenuOpen &&
            Own->GetActorTransform().Equals(bMenuOpen?Offset:DroneLaunchPose,.01);
        if (!bExpectedResult) {
            Finish(false,bMenuOpen?TEXT("The actual bound F9 callback moved the drone or changed ownership while native UI was open."):
                TEXT("The actual bound F9 callback did not recover the same disarmed drone after native UI closed."));return false;
        }
        if (!Own->GetActorTransform().Equals(Before,.01) && !Own->SetActorTransform(Before,false,nullptr,ETeleportType::TeleportPhysics)) {
            Finish(false,TEXT("Bound F9 probe could not restore the original camera pose."));return false;
        }
        if (!Own->GetActorTransform().Equals(Before,.01) || !Component->GetComponentTransform().Equals(ViewBefore,.01)) {
            Finish(false,TEXT("Bound F9 probe did not restore the original camera pose before capture."));return false;
        }
        return true;
    }
    bool OwnedWidgetStillPresented() const {
        auto* Menu=OwnedNativeWidget.Get();if (!Menu || !Menu->IsVisible() || !FSlateApplication::IsInitialized()) return false;
        auto Slate=Menu->GetCachedWidget();if (!Slate || !FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef()).IsValid()) return false;
        for (int32 Depth=0;Slate && Depth<128;++Depth) {
            if (!Slate->GetVisibility().IsVisible() || Slate->GetRenderOpacity()<=0.f) return false;
            Slate=Slate->GetParentWidget();
        }
        return true;
    }
    bool InvokeNativeMap() {
        // ToggleMap is a protected C++ wrapper but an explicitly callable native
        // Blueprint event. Invoke only that exact reflected, zero-parameter API.
        auto* PC=Controller.Get();auto* Function=PC?PC->FindFunction(TEXT("ToggleMap")):nullptr;
        if (!Function || Function->NumParms!=0 || Function->ParmsSize!=0 || !Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) || !Function->HasAnyFunctionFlags(FUNC_BlueprintEvent)) {
            Finish(false,TEXT("Native BlueprintCallable ToggleMap zero-parameter schema unavailable."));return false;
        }
        PC->ProcessEvent(Function,nullptr);return true;
    }
    void OpenNativeMenu(double Now) {
        if (!NativeInteractionClear() || !CheckActive(true)) { if (!bDone && Phase!=EPhase::CaptureHudFailure) Finish(false,TEXT("Native menu entry requires the unchanged disarmed session and no other native UI."));return; }
        auto& Item=NativeMenus[NativeMenuIndex];OwnedNativeWidget.Reset();PresentedNativeSlate.Reset();
        Settle(EPhase::OpeningNativeMenu,Now);Item.bRequested=true;
        if (NativeMenuIndex==0) Pioneer->Input_ToggleInventory(FInputActionValue(true));
        else if (NativeMenuIndex==1) { if (!InvokeNativeMap()) return; }
        else {
            auto* Client=World->GetGameViewport();auto* Console=Client?Client->ViewportConsole.Get():nullptr;
            if (!Console || Console->ConsoleState!=NAME_None) { Finish(false,TEXT("Existing native viewport console is unavailable or already active."));return; }
            OwnedConsole=Console;
            Console->OutputText(TEXT("FACTORY FPV: native console presentation qualification. No command is executed."));
            Console->FakeGotoState(TEXT("Open"));
        }
        LastReason=TEXT("Waiting for actual native ")+Item.Kind+TEXT(" presentation and input ownership.");
    }
    void BeginNativeMenus(double Now) {
        NativeMenus.SetNum(3);
        NativeMenus[0].Kind=TEXT("inventory");NativeMenus[0].OpenApi=TEXT("AFGCharacterPlayer::Input_ToggleInventory(true)");
        NativeMenus[1].Kind=TEXT("map");NativeMenus[1].OpenApi=TEXT("AFGPlayerController::ToggleMap BlueprintCallable event");
        NativeMenus[2].Kind=TEXT("console");NativeMenus[2].OpenApi=TEXT("UConsole::FakeGotoState(Open)");
        NativeMenuIndex=0;OpenNativeMenu(Now);
    }
    bool VerifyNativePresentation(double Now) {
        auto& Item=NativeMenus[NativeMenuIndex];auto* PC=Controller.Get();auto* UI=PC?PC->GetGameUI():nullptr;
        auto* Client=World.IsValid()?World->GetGameViewport():nullptr;const auto Window=Client?Client->GetWindow():nullptr;
        auto Reject=[&](const TCHAR* Reason) { Item.ReadyStarted=0;Item.ReadySamples=0;Item.bPresentation=false;LastReason=TEXT("Waiting for native ")+Item.Kind+TEXT(": ")+Reason;return false; };
        Item.bNativeOwnership=false;Item.bMounted=false;
        if (NativeMenuIndex<2) {
            if (!UI || !UI->HasActiveInteractWidget() || !FactoryFPVInteractionState::BlocksFlightOrPanel(UI)) return Reject(TEXT("real interact stack does not own input"));
            const auto Stack=UI->GetInteractWidgetStack();UFGInteractWidget* Menu=nullptr;
            for (int32 I=Stack.Num()-1;I>=0;--I) if (IsValid(Stack[I])) { Menu=Stack[I];break; }
            const FString Token=NativeMenuIndex==0?TEXT("Inventory"):TEXT("Map");
            if (!Menu || !Menu->GetClass()->GetPathName().Contains(Token,ESearchCase::IgnoreCase)) return Reject(TEXT("top native widget does not identify the requested menu"));
            if (OwnedNativeWidget.IsValid() && OwnedNativeWidget.Get()!=Menu) { Finish(false,TEXT("A different native menu replaced this run's exact widget."));return false; }
            OwnedNativeWidget=Menu;Item.WidgetClass=Menu->GetClass()->GetPathName();Item.bNativeOwnership=true;
            if (!Menu->IsVisible() || !FMath::IsFinite(Menu->GetRenderOpacity()) || Menu->GetRenderOpacity()<.95f || !FMath::IsFinite(Menu->GetColorAndOpacity().A) || Menu->GetColorAndOpacity().A<.95f) return Reject(TEXT("widget is hidden or fading"));
            auto Slate=Menu->GetCachedWidget();if (!Slate || !Window || !FSlateApplication::IsInitialized()) return Reject(TEXT("native Slate/window unavailable"));
            Item.bMounted=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef())==Window;
            if (!Item.bMounted) return Reject(TEXT("widget is not mounted in the actual game window"));
            const auto& Geometry=Menu->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
            if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1 || Size.Y<1 || !Window->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(Size*.5f))) return Reject(TEXT("widget has no laid-out viewport geometry"));
            auto Ancestor=Slate;
            for (int32 Depth=0;Ancestor && Depth<128;++Depth) {
                const float Opacity=Ancestor->GetRenderOpacity();
                if (!Ancestor->GetVisibility().IsVisible() || !FMath::IsFinite(Opacity) || Opacity<.95f) return Reject(TEXT("hidden/fading native Slate ancestry"));
                Ancestor=Ancestor->GetParentWidget();
            }
            if (Ancestor) return Reject(TEXT("unbounded native ancestry"));
            if (PresentedNativeSlate.Pin()!=Slate) { PresentedNativeSlate=Slate;Item.ReadyStarted=0;Item.ReadySamples=0; }
        } else {
            auto* Console=OwnedConsole.Get();
            Item.bNativeOwnership=Console && Client && Client->ViewportConsole==Console && Console->ConsoleState==TEXT("Open") && Console->ConsoleActive();
            Item.bMounted=Item.bNativeOwnership && Window.IsValid() && Client->Viewport && Client->Viewport->GetSizeXY().X>=320 && Client->Viewport->GetSizeXY().Y>=180;
            if (!Item.bNativeOwnership || !Item.bMounted) return Reject(TEXT("real native Canvas console is not Open on the actual viewport"));
            Item.WidgetClass=Console->GetClass()->GetPathName();
            if (UI && (UI->HasActiveInteractWidget() || UI->IsPauseMenuOpen())) { Finish(false,TEXT("Another native UI overlaps the console probe."));return false; }
        }
        if (Item.ReadyStarted==0) { Item.ReadyStarted=Now;Item.ReadyFrame=Item.LastReadyFrame=GFrameCounter;Item.ReadySamples=1; }
        else if (Item.LastReadyFrame!=GFrameCounter) { ++Item.ReadySamples;Item.LastReadyFrame=GFrameCounter; }
        Item.bPresentation=Now-Item.ReadyStarted>=.5 && GFrameCounter>=Item.ReadyFrame+3 && Item.ReadySamples>=3;
        return Item.bPresentation;
    }
    bool VerifyNativeOwnership(bool First) {
        auto& Item=NativeMenus[NativeMenuIndex];auto* PC=Controller.Get();auto* Subsystem=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        const auto Lease=FactoryFPVInputLease::GetDiagnostics(PC);
        if (First) { Item.OwnedMoveAtOpen=Lease.OwnedMoveContributions;Item.OwnedLookAtOpen=Lease.OwnedLookContributions;Item.OpenInputState=InputState(); }
        Item.bLeaseStable=Lease.bNativeHooksReady && Lease.ActiveLeases==1 && Lease.ResetDepth==0 && Lease.OwnedMoveContributions<=Item.OwnedMoveAtOpen && Lease.OwnedLookContributions<=Item.OwnedLookAtOpen;
        Item.bSuspended=Drone->IsFlightInputSuspended();Item.bOverlayHidden=!Drone->IsFlightOverlayVisible();Item.bDisarmed=!Drone->IsFlightArmed();
        if (!Item.bNativeOwnership || !Item.bLeaseStable || !Item.bSuspended || !Item.bOverlayHidden || !Item.bDisarmed || !Subsystem || Subsystem->IsPanelOpen()) {
            Finish(false,TEXT("Actual native menu ownership did not retain disarmed flight, hide the OSD and preserve input-lease ownership."));return false;
        }
        if (First) {
            FInputActionUnifiedDelegate Callback;int32 Count=0;
            for (const auto& Weak:SessionInputs) if (auto* Input=Weak.Get();Input && PC->IsInputComponentInStack(Input))
                for (const auto& Binding:Input->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Drone.Get())) { Callback=Binding.KeyDelegate;++Count; }
            if (Count!=1) { Finish(false,TEXT("No unique actual drone F6 callback for native-menu refusal."));return false; }
            Callback.Execute(EKeys::F6);
            Item.bF6Refused=Drone.IsValid() && !Drone->IsActorBeingDestroyed() && PC->GetViewTarget()==Drone.Get() && !Subsystem->IsPanelOpen();
            if (!Item.bF6Refused) { Finish(false,TEXT("The actual bound F6 callback took native-menu input or ended flight."));return false; }
            Item.bF9Refused=ProbeBoundRecovery(true,Item.F9ProbeOffsetCm);
            if (!Item.bF9Refused) return false;
        }
        return true;
    }
    void CloseNativeMenu(double Now) {
        if (NativeMenuIndex<2) {
            auto* Menu=OwnedNativeWidget.Get();auto* UI=Controller->GetGameUI();
            if (!Menu || !UI || !UI->GetInteractWidgetStack().Contains(Menu)) { Finish(false,TEXT("Native menu disappeared before its normal close callback."));return; }
            Menu->OnEscapePressed();
        } else {
            auto* Console=OwnedConsole.Get();if (!Console || Console->ConsoleState!=TEXT("Open")) { Finish(false,TEXT("Native console changed before normal close."));return; }
            Console->FakeGotoState(NAME_None);
        }
        Settle(EPhase::ClosingNativeMenu,Now);LastReason=TEXT("Waiting for normal native ")+NativeMenus[NativeMenuIndex].Kind+TEXT(" closure and retained session.");
    }
    bool CompareConsolePixels() {
        const auto& Open=NativeMenus[2].Capture;
        if (ConsoleBefore.Size!=Open.Size || ConsoleAfter.Size!=Open.Size) { Finish(false,TEXT("Native console screenshots changed dimensions."));return false; }
        int32 Changed=0;
        // The native full console renders on Canvas over the upper 75%. Sample
        // its central area, away from the ordinary OSD's edges/crosshair. Use
        // closed images on both sides to measure real scene temporal noise.
        for (int32 Y=0;Y<24;++Y) for (int32 X=0;X<48;++X) {
            const double U=.15+.7*(X+.5)/48.,V=.15+.4*(Y+.5)/24.;
            const auto Before=Sample(ConsoleBefore.Image,U,V),After=Sample(ConsoleAfter.Image,U,V),Actual=Sample(Open.Image,U,V);
            const double Difference=Error(Actual,(Before+After)*.5f);ConsoleOpenMeanError+=Difference;ConsoleRestoreMeanError+=Error(Before,After);Changed+=Difference>.02;++ConsolePixelSamples;
        }
        ConsoleOpenMeanError/=ConsolePixelSamples;ConsoleRestoreMeanError/=ConsolePixelSamples;ConsoleChangedFraction=double(Changed)/ConsolePixelSamples;
        bConsolePixelsVerified=ConsoleChangedFraction>.1 && ConsoleOpenMeanError>FMath::Max(.01,ConsoleRestoreMeanError*2.) && ConsoleRestoreMeanError<FMath::Max(.035,ConsoleOpenMeanError*.7);
        if (!bConsolePixelsVerified) { Finish(false,TEXT("Native Open console did not produce a resolved Canvas pixel change/restoration above measured scene noise; inspect retained captures."));return false; }
        return true;
    }
    void FinishNativeMenus(double Now) {
        bNativeMenusVerified=NativeMenus.Num()==3 && bConsolePixelsVerified && bNativeHudBaselineObserved && bNativeHudHiddenVerified && HudFailureReason.IsEmpty();
        bNativeMenusCapturesVerified=ConsoleBefore.bDecoded && ConsoleAfter.bDecoded && HudBeforeMenus.bDecoded && (!bPauseMenuEnabled || HudAfterPause.bDecoded);
        for (int32 I=0;I<NativeMenus.Num();++I) {
            const auto& Item=NativeMenus[I];
            bNativeMenusVerified &= Item.bRequested && Item.bOpened && Item.bMounted && Item.bPresentation && Item.bNativeOwnership && Item.bSuspended && Item.bF6Refused && Item.bF9Refused && Item.bOverlayHidden && Item.bLeaseStable && Item.bClosed && Item.bRetained && Item.bDisarmed;
            bNativeMenusCapturesVerified &= Item.Capture.bDecoded && (I>=2 || Item.AfterCapture.bDecoded);
        }
        if (!bNativeMenusVerified || !bNativeMenusCapturesVerified || !NativeInteractionClear() || !CheckActive(true)) { if (!bDone && Phase!=EPhase::CaptureHudFailure) Finish(false,TEXT("Native-menu results/current session failed final revalidation."));return; }
        bBoundF9AfterMenuRecovered=ProbeBoundRecovery(false,F9AfterMenuProbeOffsetCm);
        if (bBoundF9AfterMenuRecovered) BeginReturn(Now);
    }
    void AfterHudBeforeMenus(double Now) {
        if (bPauseMenuEnabled) {
            Settle(EPhase::OpeningPause,Now);LastReason=TEXT("Waiting for the real native pause-menu key handler to open the menu.");
            bPauseMenuRequested=true;Controller->GetGameUI()->Native_HandlePauseGamePressed();
        } else if (bNativeMenusEnabled) BeginNativeMenus(Now);
        else BeginReturn(Now);
    }
    void AfterNativeCloseCapture(double Now) {
        if (NativeMenuIndex==0) { NativeMenuIndex=1;OpenNativeMenu(Now); }
        else { NativeMenuIndex=2;Settle(EPhase::SettleConsoleBefore,Now); }
    }
    void BeginReturn(double Now) {
        Drone->ExitDrone();bExitRequested=true;Settle(EPhase::Returning,Now);
        LastReason=TEXT("Waiting for ordinary ExitDrone to restore the original pioneer/view/FOV/input/HUD.");
    }
    bool VerifyPausePresentation(double Now) {
        bPauseMenuMounted=false;bPauseMenuPresentationVerified=false;PauseMenuMinimumOpacity=1.f;
        auto Reject=[&](const TCHAR* Reason) {
            PauseMenuReadyStarted=0;PauseMenuReadyFrame=0;PauseMenuLastReadyFrame=0;PauseMenuReadySamples=0;
            PresentedPauseMenu.Reset();PresentedPauseSlate.Reset();PresentedPauseWindow.Reset();
            LastReason=TEXT("Waiting for actual native pause-menu presentation: ")+FString(Reason);
            return false;
        };
        auto Opaque=[&](float Opacity) {
            PauseMenuMinimumOpacity=FMath::Min(PauseMenuMinimumOpacity,FMath::IsFinite(Opacity)?Opacity:0.f);
            return FMath::IsFinite(Opacity) && Opacity>.95f;
        };
        auto* PC=Controller.Get();auto* UI=PC?PC->GetGameUI():nullptr;auto* Menu=UI?UI->GetPauseMenu():nullptr;
        if (!PC || !UI || !UI->IsPauseMenuOpen() || !PC->IsPauseMenuOpen() || !IsValid(Menu) || !Menu->IsVisible())
            return Reject(TEXT("Known native menu is not open and visible."));
        if (!Opaque(Menu->GetRenderOpacity()) || !Opaque(Menu->GetColorAndOpacity().A))
            return Reject(TEXT("Native menu is still fading or has invalid opacity."));
        auto Slate=Menu->GetCachedWidget();
        if (!Slate || !FSlateApplication::IsInitialized()) return Reject(TEXT("Native menu has no cached Slate widget."));
        auto* Client=World.IsValid()?World->GetGameViewport():nullptr;
        const auto ViewportWindow=Client?Client->GetWindow():nullptr;
        const auto MenuWindow=FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef());
        bPauseMenuMounted=ViewportWindow.IsValid() && MenuWindow==ViewportWindow;
        if (!bPauseMenuMounted) return Reject(TEXT("Native menu is not mounted in the actual game viewport window."));
        const auto& Geometry=Menu->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1.f || Size.Y<1.f ||
            !MenuWindow->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(FVector2D(Size.X*.5,Size.Y*.5))))
            return Reject(TEXT("Native menu has no laid-out geometry inside its viewport window."));
        auto Ancestor=Slate;
        for (int32 Depth=0;Ancestor && Depth<128;++Depth) {
            if (!Ancestor->GetVisibility().IsVisible() || !Opaque(Ancestor->GetRenderOpacity()))
                return Reject(TEXT("Native menu has a hidden/fading Slate ancestor."));
            Ancestor=Ancestor->GetParentWidget();
        }
        if (Ancestor) return Reject(TEXT("Native menu ancestry did not resolve safely."));
        if (PresentedPauseMenu.Get()!=Menu || PresentedPauseSlate.Pin()!=Slate || PresentedPauseWindow.Pin()!=MenuWindow || PauseMenuReadyStarted<=0) {
            PresentedPauseMenu=Menu;PresentedPauseSlate=Slate;PresentedPauseWindow=MenuWindow;
            PauseMenuReadyStarted=Now;PauseMenuReadyFrame=PauseMenuLastReadyFrame=GFrameCounter;PauseMenuReadySamples=1;
        } else if (PauseMenuLastReadyFrame!=GFrameCounter) {
            ++PauseMenuReadySamples;PauseMenuLastReadyFrame=GFrameCounter;
        }
        bPauseMenuPresentationVerified=Now-PauseMenuReadyStarted>=.5 && GFrameCounter>=PauseMenuReadyFrame+2 && PauseMenuReadySamples>=2;
        if (!bPauseMenuPresentationVerified) LastReason=TEXT("Waiting for two frames and settling after the actual native menu mounted with visible/opaque ancestry.");
        return bPauseMenuPresentationVerified;
    }
    bool Tick() {
        if (bDone) return false;FString Error;
        if (!Guard(Error)) { Finish(false,Error);return false; }
        const double Now=FPlatformTime::Seconds();
        if (Now>Deadline) {
            const TCHAR* Scope=bStreamingRouteEnabled ? TEXT("85-second scripted streaming route deadline: ") : bLensPerfEnabled ? TEXT("55-second lens timing deadline: ") :
                bNativeMenusEnabled ? TEXT("45-second native-menu camera QA deadline: ") : TEXT("30-second camera QA deadline: ");
            Finish(false,Scope+LastReason);return false;
        }
        if (!World.IsValid()) { Finish(false,TEXT("Disposable camera QA world unloaded."));return false; }
        if (!Ready()) return !bDone;
        if (Phase==EPhase::Waiting) { SnapshotAndLaunch(Now);return !bDone; }
        if (Phase==EPhase::OpeningPreLaunchPause) {
            if (!VerifyPausePresentation(Now)) return true;
            auto* PC=Controller.Get();
            auto* UI=PC->GetGameUI();
            auto* View=PC->GetViewTarget();
            const FString InputBefore=InputState();
            const FVector Location=Pioneer->GetActorLocation();
            const auto NoSession=[this] {
                auto* Current=World.Get();
                return !TActorIterator<AFactoryFPVDrone>(Current) && !TActorIterator<AFactoryFPVPioneerAnchor>(Current) &&
                    !TActorIterator<AFactoryFPVPilotVisual>(Current);
            };
            if (!NoSession()) { Finish(false,TEXT("An FPV actor already exists before the paused launch probe."));return false; }
            FString LaunchError;
            if (auto* Unexpected=AFactoryFPVDrone::Launch(PC,TEXT("keyboard"),LaunchError)) {
                Drone=Unexpected;
                Finish(false,TEXT("FPV launched over the native pause menu."));return false;
            }
            bPreLaunchPauseRefused=LaunchError==TEXT("Close the game menu or resume the game before launching FPV.") &&
                UI->IsPauseMenuOpen() && PC->IsPauseMenuOpen() && PC->GetViewTarget()==View && InputState()==InputBefore &&
                Pioneer->GetActorLocation().Equals(Location,.01) && NoSession();
            if (!bPreLaunchPauseRefused) { Finish(false,TEXT("Native pause launch refusal changed the pioneer, view or input: ")+LaunchError);return false; }
            Settle(EPhase::ClosingPreLaunchPause,Now);
            LastReason=TEXT("Waiting for the real pause menu to resume before normal FPV launch.");
            UI->ResumeGame();
            return true;
        }
        if (Phase==EPhase::ClosingPreLaunchPause) {
            if (NativeInteractionClear() && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
                bPreLaunchPauseRequested=false;
                PauseMenuReadyStarted=0;
                Phase=EPhase::Waiting;
            }
            return true;
        }
        if (bNativeMenusEnabled && LastHudPhase!=static_cast<int32>(Phase)) { LastHudPhase=static_cast<int32>(Phase);RecordHud(TEXT("phase.")+FString::FromInt(LastHudPhase)); }
        if (Phase==EPhase::CaptureHudFailure) {
            if (CaptureFinished(HudFailure,Now)) Finish(false,HudFailureReason+TEXT(" Actual ShowUI failure capture retained."));
            return !bDone;
        }
        const bool InNativePhase=IsNativePausePhase() || IsNativeMenuPhase();
        const bool StreamingPhase=Phase==EPhase::SettleStreamingLift || Phase==EPhase::MeasureStreamingRoute || Phase==EPhase::SettleStreamingEnd;
        const bool FixedPhase=!StreamingPhase && Phase!=EPhase::SettleBefore && Phase!=EPhase::FreshNativeSession;
        if (Phase!=EPhase::Returning && Phase!=EPhase::CapturePioneer && Phase!=EPhase::FreshNativeReturn && !CheckActive(FixedPhase,InNativePhase)) return !bDone;
        if (Phase==EPhase::SettleBefore) {
            const auto Pose=Camera->GetComponentTransform();
            if (Pose.GetLocation().Equals(LastPose.GetLocation(),.01) && Pose.GetRotation().AngularDistance(LastPose.GetRotation())<.0001) ++StablePoseSamples;else StablePoseSamples=0;
            LastPose=Pose;
            if (Now-PhaseStarted>=1. && StablePoseSamples>=3 && GFrameCounter>=PhaseFrame+2) {
                FrozenPose=Pose;
                if (bStreamingRouteEnabled) BeginStreamingRoute(Now);
                else if (bLensPerfEnabled) BeginLensPerf(Now);
                else if (RequestCapture(OffBefore,TEXT("Camera-OffBefore.png"),false,Now)) Phase=EPhase::CaptureBefore;
            }
        } else if (Phase==EPhase::MeasureLens) {
            TickLensPerf(Now);
        } else if (StreamingPhase) {
            TickStreamingRoute(Now);
        } else if (Phase==EPhase::CaptureBefore && CaptureFinished(OffBefore,Now)) {
            if (bShowAirframe) {
                FString AirframeError;
                if (!VerifyAirframe(AirframeError)) { Finish(false,AirframeError);return false; }
                BeginReturn(Now);
            } else if (ApplyOn()) Settle(EPhase::SettleOn,Now);
        } else if (Phase==EPhase::SettleOn && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
            if (RequestCapture(On,TEXT("Camera-On.png"),false,Now)) Phase=EPhase::CaptureOn;
        } else if (Phase==EPhase::CaptureOn && CaptureFinished(On,Now)) {
            bOffAfterApplied=Lens->ApplyToCamera(Camera.Get(),0.f);
            if (!bOffAfterApplied || !SameBlendables(OffBlendables,Camera->PostProcessSettings.WeightedBlendables.Array)) { Finish(false,TEXT("Lens off failed to restore the exact drone-camera blendable list."));return false; }
            Settle(EPhase::SettleAfter,Now);
        } else if (Phase==EPhase::SettleAfter && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
            if (RequestCapture(OffAfter,TEXT("Camera-OffAfter.png"),false,Now)) Phase=EPhase::CaptureAfter;
        } else if (Phase==EPhase::CaptureAfter && CaptureFinished(OffAfter,Now)) {
            if (!ComparePixels()) return false;
            if (bNativeMenusEnabled) { if (RequestCapture(HudBeforeMenus,TEXT("Camera-NativeHudBeforeMenus.png"),true,Now)) Phase=EPhase::CaptureHudBeforeMenus; }
            else AfterHudBeforeMenus(Now);
        } else if (Phase==EPhase::CaptureHudBeforeMenus) {
            if (CaptureFinished(HudBeforeMenus,Now)) AfterHudBeforeMenus(Now);
        } else if (Phase==EPhase::CaptureHudAfterPause) {
            if (CaptureFinished(HudAfterPause,Now)) BeginNativeMenus(Now);
        } else if (Phase==EPhase::OpeningPause) {
            auto* UI=Controller->GetGameUI();
            if (VerifyPausePresentation(Now)) {
                if (!bPauseMenuOpened) {
                    auto* Subsystem=Controller->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
                    Subsystem->OpenPanel();
                    bPauseMenuHangarRefused=!Subsystem->IsPanelOpen() && Drone.IsValid() && !Drone->IsActorBeingDestroyed() && Controller->GetViewTarget()==Drone.Get();
                    if (!bPauseMenuHangarRefused) { Finish(false,TEXT("Hangar entry stole native pause-menu input or ended its active FPV session."));return false; }
                    FInputActionUnifiedDelegate HangarKey;int32 Bindings=0;
                    for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && Controller->IsInputComponentInStack(*It)) {
                        for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Drone.Get())) {
                            HangarKey=Binding.KeyDelegate;++Bindings;
                        }
                    }
                    if (Bindings!=1) { Finish(false,TEXT("Actual FPV session has no unique bound F6 press callback."));return false; }
                    // Exercise the production callback without injecting an OS key.
                    // Native routing/physical paused-key behavior is outside this probe.
                    HangarKey.Execute(EKeys::F6);
                    bPauseMenuBoundF6Refused=!Subsystem->IsPanelOpen() && Drone.IsValid() && !Drone->IsActorBeingDestroyed() && Controller->GetViewTarget()==Drone.Get();
                    if (!bPauseMenuBoundF6Refused) { Finish(false,TEXT("The actual bound F6 callback ended the session before native menu refusal."));return false; }
                    bPauseMenuBoundF9Refused=ProbeBoundRecovery(true,PauseMenuF9ProbeOffsetCm);
                    if (!bPauseMenuBoundF9Refused) return false;
                }
                bPauseMenuOpened=true;bPauseMenuWorldPaused=Controller->IsPaused();
                bPauseMenuMoveIgnored=Controller->IsMoveInputIgnored();bPauseMenuLookIgnored=Controller->IsLookInputIgnored();
                bPauseMenuOverlayHidden=!Drone->IsFlightOverlayVisible();
                if (!bPauseMenuOverlayHidden) { Finish(false,TEXT("Actual FPV overlay still draws over the native pause menu."));return false; }
                bPauseMenuNativeGuardVerified=FactoryFPVInteractionState::IsNativePauseMenuOpen(UI) && FactoryFPVInteractionState::BlocksFlightOrPanel(UI);
                bPauseMenuInteractionStackEmpty=!UI->HasActiveInteractWidget();
                if (!bPauseMenuNativeGuardVerified) { Finish(false,TEXT("Shared native UI guard failed to read the actual open pause menu."));return false; }
                bPauseMenuInputSuspended=Drone->IsFlightInputSuspended();
                if (!bPauseMenuInputSuspended) { Finish(false,TEXT("Real native pause menu did not suspend FPV input."));return false; }
                if (RequestCapture(PauseMenuCapture,TEXT("Camera-PauseMenu.png"),true,Now)) Phase=EPhase::CapturePause;
            }
        } else if (Phase==EPhase::CapturePause) {
            if (!VerifyPausePresentation(Now)) { Finish(false,TEXT("Native pause-menu presentation changed during its actual viewport capture."));return false; }
            if (CaptureFinished(PauseMenuCapture,Now)) {
                Settle(EPhase::ClosingPause,Now);LastReason=TEXT("Waiting for the public native ResumeGame path to resume the same FPV session.");
                Controller->GetGameUI()->ResumeGame();
            }
        } else if (Phase==EPhase::ClosingPause) {
            auto* UI=Controller->GetGameUI();
            if (UI && !UI->IsPauseMenuOpen() && !Controller->IsPauseMenuOpen() && !Controller->IsPaused() && !UI->HasActiveInteractWidget() &&
                Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
                bPauseMenuClosed=true;bPauseMenuSessionRetained=CheckActive(true);
                if (bPauseMenuSessionRetained) {
                    if (bNativeMenusEnabled) { if (RequestCapture(HudAfterPause,TEXT("Camera-NativeHudAfterPause.png"),true,Now)) Phase=EPhase::CaptureHudAfterPause; }
                    else {
                        bBoundF9AfterMenuRecovered=ProbeBoundRecovery(false,F9AfterMenuProbeOffsetCm);
                        if (bBoundF9AfterMenuRecovered) BeginReturn(Now);
                    }
                }
            }
        } else if (Phase==EPhase::OpeningNativeMenu) {
            if (VerifyNativePresentation(Now)) {
                auto& Item=NativeMenus[NativeMenuIndex];
                if (!VerifyNativeOwnership(!Item.bOpened)) return false;Item.bOpened=true;
                const TCHAR* Name=NativeMenuIndex==0?TEXT("Camera-Inventory.png"):(NativeMenuIndex==1?TEXT("Camera-Map.png"):TEXT("Camera-Console.png"));
                if (RequestCapture(Item.Capture,Name,true,Now)) Phase=EPhase::CaptureNativeMenu;
            }
        } else if (Phase==EPhase::CaptureNativeMenu) {
            if (!VerifyNativePresentation(Now) || !VerifyNativeOwnership(false)) { if (!bDone) Finish(false,TEXT("Actual native menu changed during rendered viewport capture."));return false; }
            if (CaptureFinished(NativeMenus[NativeMenuIndex].Capture,Now)) CloseNativeMenu(Now);
        } else if (Phase==EPhase::ClosingNativeMenu) {
            if (NativeInteractionClear() && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+3) {
                auto& Item=NativeMenus[NativeMenuIndex];
                if (OwnedWidgetStillPresented()) return true;
                if (!CheckActive(true)) return !bDone;
                const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller.Get());
                Item.bClosed=true;Item.bRetained=Lease.ActiveLeases==1 && Lease.OwnedMoveContributions==1 && Lease.OwnedLookContributions==1 && Lease.ResetDepth==0;
                Item.bDisarmed &= !Drone->IsFlightArmed();Item.ClosedInputState=InputState();
                if (!Item.bRetained) { Finish(false,TEXT("Native menu close did not restore exactly one owned move/look contribution."));return false; }
                OwnedNativeWidget.Reset();PresentedNativeSlate.Reset();
                if (NativeMenuIndex<2) {
                    const TCHAR* Name=NativeMenuIndex==0?TEXT("Camera-InventoryAfter.png"):TEXT("Camera-MapAfter.png");
                    if (RequestCapture(Item.AfterCapture,Name,true,Now)) Phase=EPhase::CaptureHudAfterNativeMenu;
                }
                else if (RequestCapture(ConsoleAfter,TEXT("Camera-ConsoleAfter.png"),true,Now)) Phase=EPhase::CaptureConsoleAfter;
            }
        } else if (Phase==EPhase::CaptureHudAfterNativeMenu) {
            if (!NativeInteractionClear()) { Finish(false,TEXT("Native UI reappeared during its post-close HUD capture."));return false; }
            if (CaptureFinished(NativeMenus[NativeMenuIndex].AfterCapture,Now)) AfterNativeCloseCapture(Now);
        } else if (Phase==EPhase::SettleConsoleBefore && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+3) {
            if (!NativeInteractionClear()) { Finish(false,TEXT("Native UI reappeared before the console baseline."));return false; }
            if (RequestCapture(ConsoleBefore,TEXT("Camera-ConsoleBefore.png"),true,Now)) Phase=EPhase::CaptureConsoleBefore;
        } else if (Phase==EPhase::CaptureConsoleBefore) {
            if (!NativeInteractionClear()) { Finish(false,TEXT("Native UI changed during the console baseline."));return false; }
            if (CaptureFinished(ConsoleBefore,Now)) OpenNativeMenu(Now);
        } else if (Phase==EPhase::CaptureConsoleAfter) {
            if (!NativeInteractionClear() || !CheckActive(true)) { if (!bDone && Phase!=EPhase::CaptureHudFailure) Finish(false,TEXT("Native console closure/session changed during final pixel capture."));return !bDone; }
            if (CaptureFinished(ConsoleAfter,Now) && CompareConsolePixels()) FinishNativeMenus(Now);
        } else if (Phase==EPhase::Returning) {
            if (CheckRestored() && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+2) {
                if (bStreamingRouteEnabled) {
                    bRouteSourcesRetired=StreamingRouteSourcesRetired();
                    if (bRouteSourcesRetired) {
                        bStreamingRouteVerified=true;
                        Finish(true,TEXT("Scripted disarmed viewport route activated previously unloaded remote native cells while the pioneer kept walkable support. Ordinary far return restored the pioneer and retired both production sources. No flight controls or screenshots were used."));
                    } else LastReason=TEXT("Waiting for World Partition to retire both old FPV source names after ordinary return.");
                } else if (bLensPerfEnabled) Finish(bLensPerfVerified,TEXT("Fixed disarmed off-screen Shipping view completed four viewport-drawn lens timing windows. Ordinary return restored the pioneer, camera, HUD and input. No screenshots or flight controls were used."));
                else if (RequestCapture(PioneerAfterReturn,TEXT("Camera-PioneerAfterReturn.png"),true,Now)) Phase=EPhase::CapturePioneer;
            }
        } else if (Phase==EPhase::CapturePioneer) {
            if (!CheckRestored()) { Finish(false,TEXT("Original pioneer/session restoration changed during its actual viewport capture."));return false; }
            if (CaptureFinished(PioneerAfterReturn,Now)) {
                if (bNativeMenusEnabled) {
                    if (!NativeInteractionClear()) { Finish(false,TEXT("Native UI reappeared before the fresh disarmed session."));return false; }
                    FString LaunchError;auto* Fresh=AFactoryFPVDrone::Launch(Controller.Get(),FName(TEXT("keyboard")),LaunchError);
                    if (!Fresh || Fresh->IsFlightArmed()) { Finish(false,TEXT("Fresh ordinary disarmed post-menu launch refused: ")+LaunchError);return false; }
                    Drone=Fresh;Camera=Fresh->FindComponentByClass<UCameraComponent>();Lens=Fresh->FindComponentByClass<UFactoryFPVLensComponent>();
                    // The first session's complete removal was just verified by
                    // CheckRestored. Track only this new session as active.
                    SessionInputs.Reset();TInlineComponentArray<UInputComponent*> Inputs(Fresh);for (auto* Input:Inputs) SessionInputs.Add(Input);
                    if (!Camera.IsValid() || !Lens.IsValid() || SessionInputs.IsEmpty()) { Finish(false,TEXT("Fresh native-menu session has no actual camera/lens/controls."));return false; }
                    Settle(EPhase::FreshNativeSession,Now);
                } else Finish(true,bShowAirframe ?
                    TEXT("Cooked Scout geometry and preview passed runtime readback with a fixed lens-off viewport capture. Ordinary ExitDrone restored the pioneer, view/FOV, inputs and HUD.") :
                    TEXT("Actual fixed disarmed Shipping viewport passed Off/On/Off radial lens and temporal-restoration comparisons; ordinary ExitDrone restored living pioneer, view/FOV, input membership and HUD. No settings save, flight input or movement cheat was used."));
            }
        } else if (Phase==EPhase::FreshNativeSession && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+3) {
            if (!NativeInteractionClear() || !CheckActive(false) || !CheckNativeHudHidden()) { if (!bDone && Phase!=EPhase::CaptureHudFailure) Finish(false,TEXT("Fresh disarmed post-menu session was not ordinarily ready."));return !bDone; }
            Drone->ExitDrone();Settle(EPhase::FreshNativeReturn,Now);
        } else if (Phase==EPhase::FreshNativeReturn) {
            if (CheckRestored() && NativeInteractionClear() && Now-PhaseStarted>=.5 && GFrameCounter>=PhaseFrame+3) {
                const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller.Get());
                bNativeMenusFreshSessionPassed=Lease.ActiveLeases==0 && Lease.OwnedMoveContributions==0 && Lease.OwnedLookContributions==0 && Lease.ResetDepth==0;
                Finish(bNativeMenusVerified && bNativeMenusCapturesVerified && bNativeMenusFreshSessionPassed,
                    TEXT("Actual native inventory/map/Canvas-console roundtrips retained the disarmed session, refused bound F6 takeover, restored native UI/input ownership and passed a fresh ordinary disarmed launch/return. No arming, physical/flight input, foreground transition or world save was performed."));
            }
        }
        return !bDone;
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingCameraQA::Begin(UWorld* World,FOutputDevice& Output) {
    using namespace FactoryFPVShippingCameraQAInternal;FString Error;
    if (!Guard(Error)) { Marker(TEXT("FAIL"),Error);Output.Log(*Error);if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return; }
    if (!World || World->GetNetMode()!=NM_Standalone) { Marker(TEXT("FAIL"),TEXT("A standalone disposable game world is required."));FPlatformMisc::RequestExitWithStatus(false,1);return; }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Camera QA is already running."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;
    Run->bNativeMenusEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQANativeMenus"));
    Run->bPauseMenuEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAPauseMenu"));
    Run->bLensPerfEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQALensPerf"));
    Run->bStreamingRouteEnabled=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAStreamingRoute"));
    Run->bCheckGamepadGuard=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAGamepadGuard"));
    Run->bShowAirframe=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAShowAirframe"));
    if (Run->bShowAirframe && (Run->bStreamingRouteEnabled || Run->bLensPerfEnabled || Run->bPauseMenuEnabled || Run->bNativeMenusEnabled || Run->bCheckGamepadGuard ||
        !FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture")) || !FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")) ||
        !FParse::Param(FCommandLine::Get(),TEXT("NoAutoSave")) || !FParse::Param(FCommandLine::Get(),TEXT("SavesUseProjectSavedDir")))) {
        Marker(TEXT("FAIL"),TEXT("Scout airframe capture requires isolated off-screen Camera QA with capture and autosave disabled."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Run->bCheckGamepadGuard && (Run->bStreamingRouteEnabled || Run->bLensPerfEnabled || Run->bPauseMenuEnabled || Run->bNativeMenusEnabled ||
        !FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")) || !FParse::Param(FCommandLine::Get(),TEXT("NoGamepad")) ||
        !FParse::Param(FCommandLine::Get(),TEXT("NoAutoSave")))) {
        Marker(TEXT("FAIL"),TEXT("Gamepad guard requires ordinary Camera QA with RenderOffScreen, NoGamepad and NoAutoSave."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Run->bStreamingRouteEnabled && (Run->bLensPerfEnabled || Run->bPauseMenuEnabled || Run->bNativeMenusEnabled ||
        !FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")) || !FParse::Param(FCommandLine::Get(),TEXT("NoAutoSave")))) {
        Marker(TEXT("FAIL"),TEXT("Streaming route requires exclusive RenderOffScreen and NoAutoSave."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (Run->bLensPerfEnabled && (Run->bPauseMenuEnabled || Run->bNativeMenusEnabled || !FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")))) {
        Marker(TEXT("FAIL"),TEXT("Lens timing requires RenderOffScreen and cannot be combined with native menu probes."));FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    Run->Deadline=FPlatformTime::Seconds()+(Run->bStreamingRouteEnabled?85.:Run->bLensPerfEnabled?55.:Run->bNativeMenusEnabled?45.:30.);Active=Run;
    Marker(TEXT("PENDING"),TEXT("Guarded actual-viewport camera QA queued; waiting for exact cloned save, living pioneer and unobscured startup."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float) { return Run->Tick(); }),.1f);
}
