#include "FactoryFPVShippingRecoveryQA.h"
#include "FactoryFPVShippingSimulationQA.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHUD.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SphereComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "Input/Events.h"
#include "Input/HittestGrid.h"
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"

namespace FactoryFPVShippingRecoveryQAInternal
{
FString Escape(FString Text)
{
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
FString Vec(const FVector& Value) { return FString::Printf(TEXT("[%.9f,%.9f,%.9f]"),Value.X,Value.Y,Value.Z); }
void Marker(const TCHAR* Result,const FString& Reason,const FString& Extra=FString())
{
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"recovery\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"actual FG pioneer and World Partition queries with transient owned support removal, ordinary disarmed return and real recovery button; optional bound drone reset and fixed-step simulation probes; excludes forced cell unloading, death/respawn and physical input\"%s}"),Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
bool Disposable(FString Path)
{
    if (Path.IsEmpty()) return false;
    Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);
    if (!FPaths::CollapseRelativeDirectories(Path)) return false;
    return Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard()
{
    return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))
        && Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath());
}
struct FInputSnapshot { TWeakObjectPtr<UInputComponent> Component;bool bMember=false; };
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double LumaVariance=0;
    FString Json() const
    {
        return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Width,Height,bDecoded?TEXT("true"):TEXT("false"),LumaVariance);
    }
};
struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,RaisedSettle,FirstFlight,FirstUnsupported,CaptureMissing,OpenRefusal,AfterRefusal,CaptureRefusal,AutomaticRestore,SecondFlight,SecondUnsupported,OpenAlternate,AfterAlternate,RestoreOriginal,CaptureRestored };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> PC;
    TWeakObjectPtr<AFGCharacterPlayer> Pioneer;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<APlayerCameraManager> Manager;
    TWeakObjectPtr<AFGHUD> Hud;
    TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TWeakObjectPtr<AFactoryFPVPioneerAnchor> Anchor;
    TWeakObjectPtr<UWorldPartitionStreamingSourceComponent> Source;
    TWeakObjectPtr<AActor> LaunchActor,AlternateActor;
    TWeakObjectPtr<UBoxComponent> LaunchFloor,AlternateFloor;
    TArray<FInputSnapshot> OriginalInputs;
    FVector OriginalLocation=FVector::ZeroVector,RaisedLocation=FVector::ZeroVector,HeldLocation=FVector::ZeroVector,RecoveredLocation=FVector::ZeroVector;
    FVector DroneLaunchPosition=FVector::ZeroVector;
    FRotator OriginalRotation=FRotator::ZeroRotator;
    EMovementMode OriginalMovement=MOVE_None;uint8 OriginalCustomMovement=0;
    float OriginalFov=0,OriginalFovLock=0;
    bool bOriginalPartialHud=false,bOriginalHudVisible=false,bOriginalShowHud=false,bOriginalCursor=false;
    bool bSnapshot=false,bRelocated=false,bDone=false,bOpenedPanel=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bWorldPartitionVerified=false,bUnsupportedHoldVerified=false,bSourceRetainedVerified=false,bReturnBannerVerified=false;
    bool bFailedRecoveryHeld=false,bAutomaticFloorRestoreVerified=false,bRecoveryButtonVerified=false,bAlternateSupportVerified=false;
    bool bPioneerRestored=false,bInputMembershipRestored=false,bCleanupRestored=false;
    bool bDroneRecoveryEnabled=false,bDroneRecoveryPoseOffset=false,bUnsupportedDroneRecoveryRefused=false,bSupportedDroneRecoveryVerified=false;
    bool bOptionalSimulationEnabled=false;
    bool bCapture=false,bCapturesVerified=false,bScreenshotProcessed=false,bOwnScreenshotPending=false,bScreenMessagesBefore=false;
    int32 UnsupportedSamples=0,ButtonPresses=0,RestoreTeleportAttempts=0,RestoreAlreadyAtPoseChecks=0;
    uint64 PhaseFrame=0,LastUnsupportedFrame=0;
    double Deadline=0,PhaseStarted=0,CaptureStarted=0,MaximumUnsupportedDriftCm=0;
    EPhase Phase=EPhase::Waiting;
    FString LastReason=TEXT("Waiting for exact living standalone clone and native startup lifecycle."),PendingScreenshot;
    FString RecoveryAcceptKey,NavigationAcceptProbe=TEXT("[]");
    TArray<FString> RecoveryAcceptKeys;
    FDelegateHandle ScreenshotHandle;
    FCapture MissingCapture,RefusalCapture,RestoredCapture;
    FFactoryFPVShippingSimulationEvidence Simulation;

    FString Evidence() const
    {
        FString Out=FString::Printf(TEXT(",\"worldPartitionVerified\":%s,\"unsupportedHoldVerified\":%s,\"sourceRetainedVerified\":%s,\"returnBannerVerified\":%s,\"failedRecoveryHeld\":%s,\"automaticFloorRestoreVerified\":%s,\"recoveryButtonVerified\":%s,\"alternateSupportVerified\":%s,\"pioneerRestored\":%s,\"inputMembershipRestored\":%s,\"capturesVerified\":%s,\"captureEnabled\":%s,\"injectedFlightControls\":false,\"forcedCellUnload\":false,\"savedWorld\":false"),
            bWorldPartitionVerified?TEXT("true"):TEXT("false"),bUnsupportedHoldVerified?TEXT("true"):TEXT("false"),bSourceRetainedVerified?TEXT("true"):TEXT("false"),bReturnBannerVerified?TEXT("true"):TEXT("false"),
            bFailedRecoveryHeld?TEXT("true"):TEXT("false"),bAutomaticFloorRestoreVerified?TEXT("true"):TEXT("false"),bRecoveryButtonVerified?TEXT("true"):TEXT("false"),bAlternateSupportVerified?TEXT("true"):TEXT("false"),
            bPioneerRestored?TEXT("true"):TEXT("false"),bInputMembershipRestored?TEXT("true"):TEXT("false"),bCapturesVerified?TEXT("true"):TEXT("false"),bCapture?TEXT("true"):TEXT("false"));
        Out+=FString::Printf(TEXT(",\"droneRecoveryEnabled\":%s,\"droneRecoveryPoseOffset\":%s,\"unsupportedDroneRecoveryRefused\":%s,\"supportedDroneRecoveryVerified\":%s"),
            bDroneRecoveryEnabled?TEXT("true"):TEXT("false"),bDroneRecoveryPoseOffset?TEXT("true"):TEXT("false"),
            bUnsupportedDroneRecoveryRefused?TEXT("true"):TEXT("false"),bSupportedDroneRecoveryVerified?TEXT("true"):TEXT("false"));
        Out+=Simulation.Json(bOptionalSimulationEnabled);
        Out+=FString::Printf(TEXT(",\"startupSplashObserved\":%s,\"startupSplashAnimationObserved\":%s,\"startupSplashFinished\":%s,\"frontEndFinished\":%s,\"unsupportedSamples\":%d,\"maximumUnsupportedDriftCm\":%.9f,\"buttonPresses\":%d,\"originalLocation\":%s,\"heldLocation\":%s,\"recoveredLocation\":%s,\"cleanupRestored\":%s,\"hangarOpenPath\":\"public TogglePanel, the production F6-bound callback\""),
            bSplashObserved?TEXT("true"):TEXT("false"),bSplashAnimationObserved?TEXT("true"):TEXT("false"),bStartupFinished?TEXT("true"):TEXT("false"),bFrontEndFinished?TEXT("true"):TEXT("false"),UnsupportedSamples,MaximumUnsupportedDriftCm,ButtonPresses,*Vec(OriginalLocation),*Vec(HeldLocation),*Vec(RecoveredLocation),bCleanupRestored?TEXT("true"):TEXT("false"));
        Out+=FString::Printf(TEXT(",\"recoveryAcceptKey\":\"%s\",\"navigationAcceptProbe\":%s,\"recoveryAcceptKeys\":["),*Escape(RecoveryAcceptKey),*NavigationAcceptProbe);
        for (int32 Index=0;Index<RecoveryAcceptKeys.Num();++Index)
        {
            if (Index) Out+=TEXT(",");
            Out+=TEXT("\"")+Escape(RecoveryAcceptKeys[Index])+TEXT("\"");
        }
        Out+=FString::Printf(TEXT("],\"restoreTeleportAttempts\":%d,\"restoreAlreadyAtPoseChecks\":%d,\"programmaticMenuActions\":true,\"captures\":["),RestoreTeleportAttempts,RestoreAlreadyAtPoseChecks);
        if (bCapture) Out+=MissingCapture.Json()+TEXT(",")+RefusalCapture.Json()+TEXT(",")+RestoredCapture.Json();
        return Out+TEXT("]");
    }
    void SetPhase(EPhase Next,double Now)
    {
        Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter;
        if (Next==EPhase::FirstUnsupported || Next==EPhase::SecondUnsupported) { UnsupportedSamples=0;LastUnsupportedFrame=0; }
    }
    bool Settled(double Now,double Seconds=.3) const { return Now-PhaseStarted>=Seconds && GFrameCounter>=PhaseFrame+3; }
    bool IsLoaded(const FVector& Location) const
    {
        auto* Current=World.Get();auto* Character=Pioneer.Get();auto* Capsule=Character?Character->GetCapsuleComponent():nullptr;
        auto* Partition=Current?Current->GetSubsystem<UWorldPartitionSubsystem>():nullptr;
        if (!Current || !Current->GetWorldPartition() || !Partition || !Capsule) return false;
        FWorldPartitionStreamingQuerySource Query(Location);Query.bUseGridLoadingRange=false;Query.Radius=Capsule->GetScaledCapsuleHalfHeight()+50.f;
        if (auto* Layers=Current->GetWorldPartition()->GetDataLayerManager()) Query.DataLayers=Layers->GetEffectiveActiveDataLayerNames().Array();
        TArray<FWorldPartitionStreamingQuerySource> Queries;Queries.Add(Query);
        return Partition->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated,Queries,false);
    }
    FCollisionQueryParams QueryParams() const
    {
        return FCollisionQueryParams(SCENE_QUERY_STAT(FactoryFPVShippingRecovery),false,Pioneer.Get());
    }
    bool ClearCapsule(const FVector& Location) const
    {
        auto* Capsule=Pioneer.IsValid()?Pioneer->GetCapsuleComponent():nullptr;
        return World.IsValid() && Capsule && !World->OverlapBlockingTestByChannel(Location,Capsule->GetComponentQuat(),Capsule->GetCollisionObjectType(),
            FCollisionShape::MakeCapsule(Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight()),QueryParams(),FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
    }
    bool FloorAt(const FVector& Location,UPrimitiveComponent* Expected=nullptr) const
    {
        auto* Move=Pioneer.IsValid()?Pioneer->GetCharacterMovement():nullptr;if (!Move) return false;
        FFindFloorResult Floor;Move->FindFloor(Location,Floor,false);
        return Floor.IsWalkableFloor() && (!Expected || Floor.HitResult.GetComponent()==Expected);
    }
    bool SourceEnabledAtBody() const
    {
        FWorldPartitionStreamingSource Actual;
        return Source.IsValid() && Source->IsStreamingSourceEnabled() && Source->GetStreamingSource(Actual)
            && Pioneer.IsValid() && Actual.Location.Equals(Pioneer->GetActorLocation(),1.f);
    }
    bool SourceRetired() const
    {
        return (!Anchor.IsValid() || Anchor->IsActorBeingDestroyed()) && (!Source.IsValid() || !Source->IsStreamingSourceEnabled());
    }
    bool Ready()
    {
        auto* Current=World.Get();auto* Mode=Current?Current->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=Mode?Mode->GetSaveSession():nullptr;
        auto* Controller=Current?Cast<AFGPlayerController>(Current->GetFirstPlayerController()):nullptr;
        auto* Character=Controller?Cast<AFGCharacterPlayer>(Controller->GetPawn()):nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() || !Mode || Mode->IsMainMenuGameMode()
            || !Save || Save->HasSaveGameOperationPending() || !Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer() || !Character || !Character->IsAliveAndWell()) return false;
        const auto Load=Mode->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        if ((Load.LoadName!=TEXT("FPV-QA-Clone") && Header.SaveName!=TEXT("FPV-QA-Clone")) || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty()) return false;
        if (bSnapshot && (PC.Get()!=Controller || Pioneer.Get()!=Character)) { Finish(false,TEXT("Original living pioneer/controller changed."));return false; }
        PC=Controller;Pioneer=Character;
        auto* UI=Controller->GetGameUI();
        if (!UI || Current->IsPaused() || Controller->IsPaused() || Controller->IsPauseMenuOpen() || UI->HasActiveInteractWidget() || UI->IsPauseMenuOpen()) return false;
        return bSnapshot || FactoryFPVShippingStartup::Ready(UI,LastReason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished);
    }
    TSharedPtr<SWindow> ViewportWindow() const
    {
        auto* Viewport=World.IsValid()?World->GetGameViewport():nullptr;
        return Viewport?Viewport->GetWindow():nullptr;
    }
    TArray<TSharedRef<SWidget>> FindTagged(FName Tag) const
    {
        TArray<TSharedRef<SWidget>> Found;auto Window=ViewportWindow();if (!Window || !FSlateApplication::IsInitialized()) return Found;
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Window.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768)
        {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());
            if (Widget->GetTag()==Tag) Found.Add(Widget);
            FChildren* Children=Widget->GetChildren();if (Children) for (int32 I=0;I<Children->Num();++I) Pending.Add(Children->GetChildAt(I));
        }
        return Found;
    }
    bool Presented(const TSharedRef<SWidget>& Widget,bool RequireEnabled) const
    {
        auto Window=ViewportWindow();if (!Window || !FSlateApplication::IsInitialized()
            || FSlateApplication::Get().FindWidgetWindow(Widget)!=Window) return false;
        const auto Size=Widget->GetCachedGeometry().GetLocalSize();
        if (!FMath::IsFinite(Size.X) || !FMath::IsFinite(Size.Y) || Size.X<1 || Size.Y<1) return false;
        const auto Center=Widget->GetCachedGeometry().LocalToAbsolute(Size*.5f);
        if (!Window->GetClientRectInScreen().ContainsPoint(Center)) return false;
        TSharedPtr<SWidget> Parent=Widget;float Opacity=1.f;
        for (int32 Depth=0;Parent && Depth<128;++Depth)
        {
            if (!Parent->GetVisibility().IsVisible() || (RequireEnabled && !Parent->IsEnabled())) return false;
            Opacity*=Parent->GetRenderOpacity();if (!FMath::IsFinite(Opacity) || Opacity<=.01f) return false;
            Parent=Parent->GetParentWidget();
        }
        return !Parent;
    }
    bool BannerPresented() const
    {
        int32 Matches=0;
        for (const auto& ReturnCard:FindTagged(TEXT("FactoryFPVReturnStatus")))
        {
            if (!Presented(ReturnCard,false) || ReturnCard->GetVisibility()!=EVisibility::HitTestInvisible) continue;
            TArray<TSharedRef<SWidget>> Pending;Pending.Add(ReturnCard);bool HasF6=false;int32 Seen=0;
            while (!Pending.IsEmpty() && ++Seen<512)
            {
                auto Widget=Pending.Pop(EAllowShrinking::No);
                if (Widget->GetType()==FName(TEXT("STextBlock")) && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString().Contains(TEXT("F6"))) HasF6=true;
                if (auto* Children=Widget->GetChildren()) for (int32 I=0;I<Children->Num();++I) Pending.Add(Children->GetChildAt(I));
            }
            if (HasF6) ++Matches;
        }
        return Matches==1;
    }
    bool BannerHidden() const
    {
        for (const auto& ReturnCard:FindTagged(TEXT("FactoryFPVReturnStatus"))) if (Presented(ReturnCard,false)) return false;
        return true;
    }
    bool ClickRecovery()
    {
        TSharedPtr<SButton> Button;
        for (const auto& Widget:FindTagged(TEXT("FactoryFPVRecoverPioneer")))
            if (Widget->GetType()==FName(TEXT("SButton")) && Presented(Widget,true))
            {
                if (Button) { Finish(false,TEXT("More than one actual visible recovery button exists."));return false; }
                Button=StaticCastSharedRef<SButton>(Widget);
            }
        if (!Button) { LastReason=TEXT("Waiting for actual visible enabled tagged recovery SButton and painted geometry.");return false; }
        auto Window=ViewportWindow();const FGeometry Geometry=Button->GetCachedGeometry();const auto Center=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
        bool InHitPath=false;for (const auto& Entry:Window->GetHittestGrid().GetBubblePath(Center,0.f,false,0)) if (&Entry.Widget.Get()==Button.Get()) InHitPath=true;
        if (!InHitPath) { LastReason=TEXT("Waiting for the actual recovery button to own its window hit-test point.");return false; }
        // Invoke only this real widget's public keyboard handlers. No global
        // Slate/OS input, focus changes, private delegates or QA click bypass.
        // Satisfactory installs its own navigation policy. Read that actual
        // mapping instead of assuming the stock UE Space -> Accept binding.
        const FKey Candidates[]{EKeys::Enter,EKeys::SpaceBar,EKeys::Virtual_Accept,EKeys::Gamepad_FaceButton_Bottom};
        FKey AcceptKey;NavigationAcceptProbe=TEXT("[");
        for (int32 Index=0;Index<UE_ARRAY_COUNT(Candidates);++Index)
        {
            const FKeyEvent Probe(Candidates[Index],FModifierKeysState(),0,false,0,0);
            const EUINavigationAction Action=FSlateApplication::Get().GetNavigationActionFromKey(Probe);
            if (Index) NavigationAcceptProbe+=TEXT(",");
            NavigationAcceptProbe+=FString::Printf(TEXT("{\"key\":\"%s\",\"action\":%d}"),*Escape(Candidates[Index].GetFName().ToString()),static_cast<int32>(Action));
            if (!AcceptKey.IsValid() && Action==EUINavigationAction::Accept) AcceptKey=Candidates[Index];
        }
        NavigationAcceptProbe+=TEXT("]");
        if (!AcceptKey.IsValid()) { Finish(false,TEXT("Live Slate navigation maps none of Enter/Space/Virtual_Accept/Gamepad bottom to Accept; no button event sent."));return false; }
        RecoveryAcceptKey=AcceptKey.GetFName().ToString();RecoveryAcceptKeys.Add(RecoveryAcceptKey);
        const FKeyEvent Accept(AcceptKey,FModifierKeysState(),0,false,0,0);
        const bool Down=Button->OnKeyDown(Geometry,Accept).IsEventHandled();
        const bool Up=Button->OnKeyUp(Geometry,Accept).IsEventHandled();
        if (!Down || !Up)
        {
            Finish(false,FString::Printf(TEXT("Actual recovery SButton did not handle public %s Accept down/up (down=%d up=%d)."),*RecoveryAcceptKey,Down,Up));return false;
        }
        ++ButtonPresses;bRecoveryButtonVerified=true;return true;
    }
    void ReleaseScreenshot()
    {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot)
        { FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore; }
        bOwnScreenshotPending=false;
    }
    bool RequestCapture(FCapture& Capture,const TCHAR* Name,double Now)
    {
        auto* Viewport=World.IsValid()?World->GetGameViewport():nullptr;
        if (FScreenshotRequest::IsScreenshotRequested() || !Viewport || !Viewport->Viewport || Viewport->Viewport->GetSizeXY().X<320 || Viewport->Viewport->GetSizeXY().Y<180)
        { Finish(false,TEXT("Actual viewport unavailable or another screenshot is pending."));return false; }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);
        const FString Directory=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));FString Path=FPaths::Combine(Directory,Name);FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Path,Directory) || !FPaths::IsUnderDirectory(Directory,Saved)
            || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Directory,true))
        { Finish(false,TEXT("Screenshot path is not fresh and inside disposable Saved."));return false; }
        ReleaseScreenshot();Capture.Path=PendingScreenshot=Path;CaptureStarted=Now;bScreenshotProcessed=false;bOwnScreenshotPending=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]()
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshotPending) { Run->bScreenshotProcessed=true;Run->bOwnScreenshotPending=false; } });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);LastReason=TEXT("Waiting for actual viewport capture: ")+Path;return true;
    }
    bool CaptureFinished(FCapture& Capture,double Now)
    {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Viewport screenshot exceeded five seconds: ")+Capture.Path);return false; }
        if (!bScreenshotProcessed) return false;ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image)
            || Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180 || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty())
        { Finish(false,TEXT("Completed screenshot is not a sane decodable PNG: ")+Capture.Path);return false; }
        double Sum=0,Squares=0;int32 Count=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60))
        {
            const auto Color=Image.GetOnePixelLinear(X,Y);if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B))
            { Finish(false,TEXT("Capture contains nonfinite pixels."));return false; }
            const double L=.2126*Color.R+.7152*Color.G+.0722*Color.B;Sum+=L;Squares+=L*L;++Count;
        }
        Capture.LumaVariance=Count?FMath::Max(0.,Squares/Count-FMath::Square(Sum/Count)):0.;
        if (Capture.LumaVariance<1e-8) { Finish(false,TEXT("Actual capture is uniformly blank."));return false; }
        Capture.Width=Image.SizeX;Capture.Height=Image.SizeY;Capture.bDecoded=true;return true;
    }
    UBoxComponent* MakePlatform(const FVector& Center,const FVector& Extent,TWeakObjectPtr<AActor>& ActorOut)
    {
        FActorSpawnParameters Params;Params.OverrideLevel=World->PersistentLevel;Params.ObjectFlags|=RF_Transient;
        Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Actor=World->SpawnActor<AActor>(Params);if (!Actor) return nullptr;ActorOut=Actor;
        auto* Box=NewObject<UBoxComponent>(Actor);Actor->SetRootComponent(Box);Actor->AddInstanceComponent(Box);
        Box->SetMobility(EComponentMobility::Static);Box->SetBoxExtent(Extent);Box->SetWorldLocation(Center);
        Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->RegisterComponent();return Box;
    }
    bool SnapshotAndRaise(double Now)
    {
        auto* Character=Pioneer.Get();auto* Move=Character->GetCharacterMovement();auto* Capsule=Character->GetCapsuleComponent();
        Hangar=PC->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();Manager=PC->PlayerCameraManager;Hud=Cast<AFGHUD>(PC->GetHUD());OriginalView=PC->GetViewTarget();
        if (!Move || !Capsule || !Hangar.IsValid() || !Manager.IsValid() || !Hud.IsValid() || !OriginalView.IsValid()) return false;
        if (!Character->GetActorTransform().IsValid() || !FMath::IsFinite(Manager->GetFOVAngle()) || !FMath::IsFinite(Manager->GetLockedFOV())
            || !FMath::IsFinite(Capsule->GetScaledCapsuleHalfHeight()) || !FMath::IsFinite(Capsule->GetScaledCapsuleRadius())
            || Capsule->GetScaledCapsuleHalfHeight()<=0 || Capsule->GetScaledCapsuleRadius()<=0)
        { Finish(false,TEXT("Native pioneer/camera/capsule fixture data is not finite and valid."));return false; }
        if (Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(World.Get()) || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Character)
            || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored() || FactoryFPVInputLease::GetDiagnostics(PC.Get()).ActiveLeases!=0)
        { Finish(false,TEXT("Recovery QA requires no existing session/panel/pending hold/input owners."));return false; }
        for (TActorIterator<AFactoryFPVPioneerAnchor> It(World.Get());It;++It) if (It->IsHolding() || It->IsReleasePending())
        { Finish(false,TEXT("Another live pioneer anchor already exists; fixture refuses mutation."));return false; }
        bWorldPartitionVerified=World->GetWorldPartition()!=nullptr && World->GetSubsystem<UWorldPartitionSubsystem>()!=nullptr;
        if (!bWorldPartitionVerified || !Move->IsMovingOnGround() || !FloorAt(Character->GetActorLocation()) || !ClearCapsule(Character->GetActorLocation()) || !IsLoaded(Character->GetActorLocation()))
        { Finish(false,TEXT("Real WP and clear activated original walkable support are required."));return false; }
        OriginalLocation=Character->GetActorLocation();OriginalRotation=Character->GetActorRotation();OriginalMovement=Move->MovementMode;OriginalCustomMovement=Move->CustomMovementMode;
        OriginalFov=Manager->GetFOVAngle();OriginalFovLock=Manager->GetLockedFOV();bOriginalPartialHud=Hud->GetPartialPumpiMode();bOriginalHudVisible=Hud->GetHUDVisibility();bOriginalShowHud=Hud->bShowHUD;bOriginalCursor=PC->bShowMouseCursor;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()) OriginalInputs.Add({*It,PC->IsInputComponentInStack(*It)});
        bSnapshot=true;RaisedLocation=OriginalLocation+FVector(0,0,15000.);
        // Preflight the whole bounded production search volume before spawning or
        // moving anything. Every temporary support lies inside this clear space.
        const FVector Extent(650.+Capsule->GetScaledCapsuleRadius(),650.+Capsule->GetScaledCapsuleRadius(),1525.+Capsule->GetScaledCapsuleHalfHeight());
        if (World->OverlapBlockingTestByChannel(RaisedLocation-FVector(0,0,1475.),FQuat::Identity,Capsule->GetCollisionObjectType(),FCollisionShape::MakeBox(Extent),QueryParams(),FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
        { Finish(false,TEXT("The bounded raised recovery volume contains existing collision; no fixture placed."));return false; }
        LaunchFloor=MakePlatform(RaisedLocation-FVector(0,0,Capsule->GetScaledCapsuleHalfHeight()+27.4),FVector(500,500,25),LaunchActor);
        if (!LaunchFloor.IsValid() || !ClearCapsule(RaisedLocation) || !Character->SetActorLocationAndRotation(RaisedLocation,OriginalRotation,false,nullptr,ETeleportType::TeleportPhysics))
        { Finish(false,TEXT("Own supported raised placement failed."));return false; }
        bRelocated=true;Move->StopMovementImmediately();Move->SetMovementMode(OriginalMovement,OriginalCustomMovement);Move->bForceNextFloorCheck=true;
        SetPhase(EPhase::RaisedSettle,Now);LastReason=TEXT("Waiting for natural FG movement to settle on the transient raised support.");return true;
    }
    bool Launch(double Now,EPhase Next)
    {
        if (!FloorAt(Pioneer->GetActorLocation(),LaunchFloor.Get()) || !Pioneer->GetCharacterMovement()->IsMovingOnGround()) return false;
        FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
        if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Ordinary disarmed launch failed: ")+Error);return false; }
        DroneLaunchPosition=Drone->GetActorLocation();
        Anchor.Reset();Source.Reset();int32 Count=0;
        for (TActorIterator<AFactoryFPVPioneerAnchor> It(World.Get());It;++It) if (It->IsHolding() && It->GetActorLocation().Equals(Pioneer->GetActorLocation(),1.f))
        { Anchor=*It;++Count; }
        if (Count!=1 || !Anchor.IsValid() || Anchor->GetLevel()!=World->PersistentLevel) { Finish(false,TEXT("Ordinary launch did not create a unique real persistent-level holding anchor."));return false; }
        Source=Anchor->FindComponentByClass<UWorldPartitionStreamingSourceComponent>();
        if (!SourceEnabledAtBody()) { Finish(false,TEXT("Actual pioneer source was not enabled at its held body."));return false; }
        HeldLocation=Pioneer->GetActorLocation();SetPhase(Next,Now);return true;
    }
    bool CheckDroneRecovery()
    {
        auto* Own=Drone.Get();
        auto* Body=Own?Cast<USphereComponent>(Own->GetRootComponent()):nullptr;
        const auto SessionIntact=[&]()
        {
            return Drone.IsValid() && !Drone->IsActorBeingDestroyed() && !Drone->IsFlightArmed() && Drone->GetOwner()==PC.Get()
                && PC->GetViewTarget()==Drone.Get() && AFactoryFPVDrone::FindActive(World.Get())==Drone.Get()
                && Anchor.IsValid() && Anchor->IsHolding() && SourceEnabledAtBody();
        };
        if (!Body || !SessionIntact() || !LaunchFloor.IsValid())
        { Finish(false,TEXT("Bound drone recovery requires its native disarmed session, sphere and owned floor."));return false; }
        FInputActionUnifiedDelegate RecoverKey;
        int32 Bindings=0;
        for (TObjectIterator<UInputComponent> It;It;++It)
        {
            if (!IsValid(*It) || It->IsTemplate() || It->GetWorld()!=World.Get() || !PC->IsInputComponentInStack(*It)) continue;
            for (const auto& Binding:It->KeyBindings)
                if (Binding.Chord.Key==EKeys::F9 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Own))
                { RecoverKey=Binding.KeyDelegate;++Bindings; }
        }
        if (Bindings!=1)
        { Finish(false,TEXT("Actual FPV session has no unique bound F9 press callback."));return false; }

        if (bOptionalSimulationEnabled)
        {
            FString Error;
            if (!FFactoryFPVShippingSimulationQA::Run(*Own,*LaunchFloor.Get(),RecoverKey,Simulation,Error) || !SessionIntact())
            { Finish(false,TEXT("Optional simulation qualification failed: ")+Error);return false; }
        }

        FCollisionQueryParams Query(SCENE_QUERY_STAT(FactoryFPVShippingDroneRecovery),false,Own);
        Query.AddIgnoredActor(Pioneer.Get());
        const auto Shape=FCollisionShape::MakeSphere(Body->GetScaledSphereRadius());
        const auto Channel=Body->GetCollisionObjectType();
        const FCollisionResponseParams Responses(Body->GetCollisionResponseToChannels());
        const auto SweepLaunchFloor=[&](FHitResult& Hit)
        {
            return World->SweepSingleByChannel(Hit,DroneLaunchPosition,DroneLaunchPosition-FVector(0,0,10),
                FQuat::Identity,Channel,Shape,Query,Responses);
        };
        FHitResult OriginalSupport;
        const FVector OffsetPosition=DroneLaunchPosition+FVector(0,0,100);
        if (!SweepLaunchFloor(OriginalSupport) || OriginalSupport.bStartPenetrating || OriginalSupport.ImpactNormal.Z<.5
            || OriginalSupport.GetComponent()!=LaunchFloor.Get()
            || World->OverlapBlockingTestByChannel(OffsetPosition,FQuat::Identity,Channel,Shape,Query,Responses)
            || !Own->SetActorLocation(OffsetPosition,false,nullptr,ETeleportType::TeleportPhysics)
            || !Own->GetActorLocation().Equals(OffsetPosition,.01))
        { Finish(false,TEXT("Drone launch lacks its owned support or a clear distinct probe pose."));return false; }
        bDroneRecoveryPoseOffset=true;
        const FTransform OffsetPose=Own->GetActorTransform();
        LaunchFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FHitResult MissingSupport;
        if (SweepLaunchFloor(MissingSupport))
        { Finish(false,TEXT("Drone launch still has blocking collision after its own floor was removed."));return false; }

        // Execute the production key binding without OS input. Both assertions
        // run before a simulation tick can reconcile the transient pose offset.
        RecoverKey.Execute(EKeys::F9);
        bUnsupportedDroneRecoveryRefused=SessionIntact() && Own->GetActorTransform().Equals(OffsetPose,.01);
        if (!bUnsupportedDroneRecoveryRefused)
        { Finish(false,TEXT("Bound F9 moved the drone or lost its session with no launch support."));return false; }
        LaunchFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        FHitResult RestoredSupport;
        if (!SweepLaunchFloor(RestoredSupport) || RestoredSupport.bStartPenetrating || RestoredSupport.ImpactNormal.Z<.5
            || RestoredSupport.GetComponent()!=LaunchFloor.Get())
        { Finish(false,TEXT("The original owned drone launch support did not return."));return false; }
        RecoverKey.Execute(EKeys::F9);
        bSupportedDroneRecoveryVerified=SessionIntact() && Own->GetActorLocation().Equals(DroneLaunchPosition,.01);
        if (!bSupportedDroneRecoveryVerified)
        { Finish(false,TEXT("Bound F9 did not reset to the original supported launch pose in the same session."));return false; }
        return true;
    }
    bool RemoveOwnSupportAndReturn(double Now,EPhase Next)
    {
        if (!Drone.IsValid() || Drone->IsFlightArmed() || !LaunchFloor.IsValid() || !Anchor.IsValid() || !Anchor->IsHolding())
        { Finish(false,TEXT("Disarmed ordinary flight/own support disappeared."));return false; }
        LaunchFloor->SetCollisionEnabled(ECollisionEnabled::NoCollision);Drone->ExitDrone();SetPhase(Next,Now);
        LastReason=TEXT("Observing real unsupported return and retained pioneer streaming over natural frames.");return true;
    }
    bool CheckUnsupported(double Now)
    {
        const double Drift=FVector::Distance(Pioneer->GetActorLocation(),HeldLocation);
        if (!FMath::IsFinite(Drift)) { Finish(false,TEXT("Unsupported pioneer position became nonfinite."));return false; }
        MaximumUnsupportedDriftCm=FMath::Max(MaximumUnsupportedDriftCm,Drift);
        if (!Anchor.IsValid() || !Anchor->IsReleasePending() || Pioneer->GetCharacterMovement()->MovementMode!=MOVE_None || Drift>.5 || !Pioneer->IsAliveAndWell() || FloorAt(Pioneer->GetActorLocation()) || !SourceEnabledAtBody())
        { Finish(false,TEXT("Unsupported return dropped/moved the body, lost its source or was not genuinely unsupported."));return false; }
        if (GFrameCounter!=LastUnsupportedFrame) { ++UnsupportedSamples;LastUnsupportedFrame=GFrameCounter; }
        if (!Settled(Now,.6) || UnsupportedSamples<4 || Anchor->GetReturnStatus()!=EFactoryFPVReturnStatus::MissingFloor) return false;
        bUnsupportedHoldVerified=true;bSourceRetainedVerified=true;return true;
    }
    bool OpenHangar(double Now,EPhase Next)
    {
        if (!Hangar.IsValid() || Hangar->IsPanelOpen()) { Finish(false,TEXT("Unexpected hangar state before recovery callback."));return false; }
        // The public function is exactly the production F6-bound callback.
        Hangar->TogglePanel();bOpenedPanel=Hangar->IsPanelOpen();
        if (!bOpenedPanel) { Finish(false,TEXT("Production F6 callback did not open the real hangar."));return false; }
        SetPhase(Next,Now);return true;
    }
    void CloseHangar()
    {
        if (bOpenedPanel && Hangar.IsValid() && Hangar->IsPanelOpen()) Hangar->TogglePanel();
        bOpenedPanel=false;
    }
    bool RestorePosition()
    {
        if (!bRelocated) return true;
        if (!Pioneer.IsValid() || !Pioneer->IsAliveAndWell() || !World.IsValid() || !IsLoaded(OriginalLocation) || !ClearCapsule(OriginalLocation) || !FloorAt(OriginalLocation)) return false;
        auto* Move=Pioneer->GetCharacterMovement();if (!Move) return false;
        constexpr double PositionToleranceCm=.01,RotationToleranceDegrees=.001;
        const auto AtOriginalPose=[&]()
        {
            return Pioneer->GetActorLocation().Equals(OriginalLocation,PositionToleranceCm)
                && Pioneer->GetActorRotation().Equals(OriginalRotation,RotationToleranceDegrees);
        };
        // Cleanup follows the already-verified return. Moving an unchanged
        // component can report false; that does not mean its pose is incorrect.
        // Only request a move when necessary and qualify the actual result.
        if (AtOriginalPose()) ++RestoreAlreadyAtPoseChecks;
        else
        {
            ++RestoreTeleportAttempts;
            Pioneer->SetActorLocationAndRotation(OriginalLocation,OriginalRotation,false,nullptr,ETeleportType::TeleportPhysics);
        }
        if (!AtOriginalPose()) return false;
        Move->StopMovementImmediately();
        if (Anchor.IsValid() && Anchor->IsReleasePending()) Anchor->RequestRelease();
        if (!Anchor.IsValid() || !Anchor->IsReleasePending()) Move->SetMovementMode(OriginalMovement,OriginalCustomMovement);
        Move->bForceNextFloorCheck=true;
        const FVector Actual=Pioneer->GetActorLocation();
        return AtOriginalPose() && IsLoaded(Actual) && ClearCapsule(Actual) && FloorAt(Actual);
    }
    bool InputMembershipRestored() const
    {
        if (!PC.IsValid()) return false;
        for (const auto& Input:OriginalInputs) if (Input.Component.IsValid()?PC->IsInputComponentInStack(Input.Component.Get())!=Input.bMember:Input.bMember) return false;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It)
            && !OriginalInputs.ContainsByPredicate([&](const FInputSnapshot& Item){return Item.Component.Get()==*It;})) return false;
        return true;
    }
    bool Restored()
    {
        bInputMembershipRestored=InputMembershipRestored();const auto Leases=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        bPioneerRestored=PC.IsValid() && Pioneer.IsValid() && Pioneer->IsAliveAndWell() && Manager.IsValid() && Hud.IsValid() && Hangar.IsValid() && !Hangar->IsPanelOpen()
            && PC->GetPawn()==Pioneer.Get() && PC->GetViewTarget()==OriginalView.Get() && PC->PlayerCameraManager==Manager.Get() && PC->GetHUD()==Hud.Get()
            && Pioneer->GetActorLocation().Equals(OriginalLocation,.5) && Pioneer->GetActorRotation().Equals(OriginalRotation,.05)
            && Pioneer->GetCharacterMovement() && Pioneer->GetCharacterMovement()->MovementMode==OriginalMovement && Pioneer->GetCharacterMovement()->CustomMovementMode==OriginalCustomMovement
            && FloorAt(Pioneer->GetActorLocation()) && ClearCapsule(Pioneer->GetActorLocation()) && IsLoaded(Pioneer->GetActorLocation())
            && !PC->IsMoveInputIgnored() && !PC->IsLookInputIgnored() && PC->bShowMouseCursor==bOriginalCursor
            && Hud->GetPartialPumpiMode()==bOriginalPartialHud && Hud->GetHUDVisibility()==bOriginalHudVisible && Hud->bShowHUD==bOriginalShowHud
            && FMath::IsNearlyEqual(Manager->GetFOVAngle(),OriginalFov,.1f) && FMath::IsNearlyEqual(Manager->GetLockedFOV(),OriginalFovLock,.01f)
            && !AFactoryFPVDrone::FindActive(World.Get()) && !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) && SourceRetired() && BannerHidden()
            && Leases.ActiveLeases==0 && Leases.OwnedMoveContributions==0 && Leases.OwnedLookContributions==0 && bInputMembershipRestored;
        return bPioneerRestored;
    }
    void Cleanup()
    {
        ReleaseScreenshot();
        if (LaunchFloor.IsValid()) LaunchFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==PC.Get()) Own->ExitDrone();
        CloseHangar();
        if (Anchor.IsValid() && !Anchor->IsActorBeingDestroyed()) Anchor->RequestRelease();
        bCleanupRestored=!bSnapshot || (RestorePosition() && Pioneer.IsValid() && Pioneer->IsAliveAndWell()
            && Pioneer->GetActorLocation().Equals(OriginalLocation,.01) && Pioneer->GetActorRotation().Equals(OriginalRotation,.001)
            && Pioneer->GetCharacterMovement() && Pioneer->GetCharacterMovement()->MovementMode==OriginalMovement
            && Pioneer->GetCharacterMovement()->CustomMovementMode==OriginalCustomMovement
            && IsLoaded(Pioneer->GetActorLocation()) && ClearCapsule(Pioneer->GetActorLocation()) && FloorAt(Pioneer->GetActorLocation()));
        // Supports belong only to this run. Retain support until the body has
        // actually been restored; a failed guarded restore never drops it.
        if (bCleanupRestored)
        {
            if (LaunchActor.IsValid()) LaunchActor->Destroy();
            if (AlternateActor.IsValid()) AlternateActor->Destroy();
        }
    }
    void Finish(bool Success,const FString& Reason)
    {
        if (bDone) return;bDone=true;Cleanup();
        const bool FinalSuccess=Success && bCleanupRestored;
        const FString FinalReason=Success && !bCleanupRestored ? Reason+TEXT(" Cleanup did not restore the original validated pioneer pose/support/movement; result downgraded to FAIL.") : Reason;
        Marker(FinalSuccess?TEXT("PASS"):TEXT("FAIL"),FinalReason,Evidence());
        FPlatformMisc::RequestExitWithStatus(false,FinalSuccess?0:1);
    }
    bool Tick()
    {
        if (bDone) return false;const double Now=FPlatformTime::Seconds();
        if (!Guard() || !World.IsValid()) { Finish(false,TEXT("Recovery isolation guard/world changed."));return false; }
        if (Now>Deadline) { Finish(false,TEXT("40-second recovery QA deadline: ")+LastReason);return false; }
        if (!Ready()) return !bDone;
        if (Phase==EPhase::Waiting) { SnapshotAndRaise(Now);return !bDone; }
        if (Phase==EPhase::RaisedSettle)
        {
            if (Settled(Now) && FloorAt(Pioneer->GetActorLocation(),LaunchFloor.Get()) && Pioneer->GetCharacterMovement()->IsMovingOnGround()) Launch(Now,EPhase::FirstFlight);
        }
        else if (Phase==EPhase::FirstFlight || Phase==EPhase::SecondFlight)
        {
            if (Settled(Now))
            {
                if (Phase==EPhase::FirstFlight && bDroneRecoveryEnabled && !CheckDroneRecovery()) return false;
                RemoveOwnSupportAndReturn(Now,Phase==EPhase::FirstFlight?EPhase::FirstUnsupported:EPhase::SecondUnsupported);
            }
        }
        else if (Phase==EPhase::FirstUnsupported)
        {
            if (!CheckUnsupported(Now)) return !bDone;
            if (!BannerPresented()) { LastReason=TEXT("Waiting for real mounted hit-test-invisible return banner with F6 text.");return true; }
            bReturnBannerVerified=true;
            if (bCapture) { if (RequestCapture(MissingCapture,TEXT("Return-MissingFloor.png"),Now)) SetPhase(EPhase::CaptureMissing,Now); }
            else OpenHangar(Now,EPhase::OpenRefusal);
        }
        else if (Phase==EPhase::CaptureMissing)
        {
            if (CaptureFinished(MissingCapture,Now)) OpenHangar(Now,EPhase::OpenRefusal);
        }
        else if (Phase==EPhase::OpenRefusal)
        {
            if (Settled(Now) && ClickRecovery()) SetPhase(EPhase::AfterRefusal,Now);
        }
        else if (Phase==EPhase::AfterRefusal)
        {
            if (!CheckUnsupported(Now)) return !bDone;
            bFailedRecoveryHeld=true;
            if (bCapture) { if (RequestCapture(RefusalCapture,TEXT("Return-RecoveryRefused.png"),Now)) SetPhase(EPhase::CaptureRefusal,Now); }
            else { CloseHangar();LaunchFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);SetPhase(EPhase::AutomaticRestore,Now); }
        }
        else if (Phase==EPhase::CaptureRefusal)
        {
            if (CaptureFinished(RefusalCapture,Now)) { CloseHangar();LaunchFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);SetPhase(EPhase::AutomaticRestore,Now); }
        }
        else if (Phase==EPhase::AutomaticRestore)
        {
            LastReason=TEXT("Waiting for natural anchor Tick to release after original owned floor returns.");
            if (Settled(Now) && SourceRetired() && !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) && Pioneer->GetCharacterMovement()->IsMovingOnGround()
                && FloorAt(Pioneer->GetActorLocation(),LaunchFloor.Get()) && BannerHidden())
            { bAutomaticFloorRestoreVerified=true;Launch(Now,EPhase::SecondFlight); }
        }
        else if (Phase==EPhase::SecondUnsupported)
        {
            if (!CheckUnsupported(Now)) return !bDone;
            auto* Capsule=Pioneer->GetCapsuleComponent();
            AlternateFloor=MakePlatform(HeldLocation+FVector(300,0,-500.-Capsule->GetScaledCapsuleHalfHeight()-25.),FVector(60,60,25),AlternateActor);
            if (!AlternateFloor.IsValid()) { Finish(false,TEXT("Alternate owned support could not be created."));return false; }
            OpenHangar(Now,EPhase::OpenAlternate);
        }
        else if (Phase==EPhase::OpenAlternate)
        {
            if (Settled(Now) && ClickRecovery()) SetPhase(EPhase::AfterAlternate,Now);
        }
        else if (Phase==EPhase::AfterAlternate)
        {
            LastReason=TEXT("Waiting for actual recovery-button result on the alternate loaded support.");
            if (!Settled(Now)) return true;
            const FVector Actual=Pioneer->GetActorLocation(),Delta=Actual-HeldLocation;
            if (!SourceRetired() || AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pioneer.Get()) || !Pioneer->GetCharacterMovement()->IsMovingOnGround()
                || !FloorAt(Actual,AlternateFloor.Get()) || !ClearCapsule(Actual) || !IsLoaded(Actual) || Delta.SizeSquared2D()>FMath::Square(600.)+1. || Delta.Z>50. || Delta.Z<-3000.) return true;
            RecoveredLocation=Actual;bAlternateSupportVerified=true;CloseHangar();
            if (!RestorePosition()) { Finish(false,TEXT("Validated original floor unavailable for final fixture restoration."));return false; }
            SetPhase(EPhase::RestoreOriginal,Now);
        }
        else if (Phase==EPhase::RestoreOriginal)
        {
            LastReason=TEXT("Waiting for original pioneer/view/FOV/HUD/input/source/banner restoration.");
            if (!Settled(Now) || !Restored()) return true;
            if (bCapture) { if (RequestCapture(RestoredCapture,TEXT("Return-PioneerRestored.png"),Now)) SetPhase(EPhase::CaptureRestored,Now); }
            else { bCapturesVerified=true;Finish(true,TEXT("Actual FG/WP unsupported holds, visible recovery explanation, real refusal/alternate-support button and automatic release verified without saves or forced unloading.")); }
        }
        else if (Phase==EPhase::CaptureRestored && CaptureFinished(RestoredCapture,Now))
        {
            if (!Restored()) { Finish(false,TEXT("Original state changed during completed capture."));return false; }
            bCapturesVerified=MissingCapture.bDecoded && RefusalCapture.bDecoded && RestoredCapture.bDecoded;
            Finish(bCapturesVerified,TEXT("Actual FG/WP unsupported holds, visible recovery explanation, real refusal/alternate-support button and automatic release verified; three processed viewport PNGs decoded. No save or forced cell unloading."));
        }
        return !bDone;
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingRecoveryQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingRecoveryQAInternal;
    const TCHAR* CommandLine=FCommandLine::Get();
    const bool bDroneRecovery=FParse::Param(CommandLine,TEXT("FactoryFPVQADroneRecovery"));
    const bool bOptionalSimulation=FParse::Param(CommandLine,TEXT("FactoryFPVQAOptionalSimulation"));
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (bOptionalSimulation &&
        (!bDroneRecovery || !FParse::Param(CommandLine,TEXT("NoAutoSave")) || !FParse::Param(CommandLine,TEXT("RenderOffScreen")))))
    {
        Marker(TEXT("FAIL"),TEXT("Explicit automation, real disposable Saved/save directories and standalone clone are required. Optional simulation also requires bound drone recovery, off-screen rendering and no autosave."));
        Output.Log(TEXT("Recovery QA isolation guard refused."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Recovery QA is already active."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Deadline=FPlatformTime::Seconds()+40.;Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));
    Run->bDroneRecoveryEnabled=bDroneRecovery;Run->bOptionalSimulationEnabled=bOptionalSimulation;Active=Run;
    Marker(TEXT("PENDING"),TEXT("Waiting for exact cloned save/native startup before a bounded transient-support recovery fixture; no saves or forced cell unloading."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}),.1f);
}
