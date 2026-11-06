#include "FactoryFPVShippingLifecycleQA.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGHealthComponent.h"
#include "FGHUD.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include <limits>

namespace FactoryFPVShippingLifecycleQAInternal
{
FString Escape(FString Text)
{
    return Text.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\""))
        .Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t"));
}
const TCHAR* Bool(bool Value) { return Value?TEXT("true"):TEXT("false"); }
FString Number(double Value) { return FMath::IsFinite(Value)?FString::Printf(TEXT("%.9f"),Value):TEXT("null"); }
FString ClassPath(const UObject* Object) { return Object?Object->GetClass()->GetPathName():FString(); }
void Marker(const FString& Mode,const TCHAR* Result,const FString& Reason,const FString& Extra=FString())
{
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"lifecycle-%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"actual native health death and public StartRespawn on a disposable clone; diagnostic baseline versus disarmed flight/panel, excludes physical input and travel\"%s}"),*Escape(Mode),Result,*Escape(Reason),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
bool Disposable(FString Path)
{
    if (Path.IsEmpty()) return false;Path=FPaths::ConvertRelativePathToFull(Path);FPaths::NormalizeDirectoryName(Path);
    return FPaths::CollapseRelativeDirectories(Path) && Path.Replace(TEXT("\\"),TEXT("/")).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase);
}
bool Guard()
{
    return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) && Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath());
}
FString MeshJson(USkeletalMeshComponent* Mesh)
{
    return FString::Printf(TEXT("{\"present\":%s,\"hiddenInGame\":%s,\"visible\":%s,\"ownerHidden\":%s}"),Bool(IsValid(Mesh)),
        Bool(Mesh && Mesh->bHiddenInGame),Bool(Mesh && Mesh->IsVisible()),Bool(Mesh && Mesh->GetOwner() && Mesh->GetOwner()->IsHidden()));
}
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double LumaVariance=0;
    FString Json() const { return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%.9f}"),*Escape(Path),Width,Height,Bool(bDecoded),LumaVariance); }
};
struct FNativeUI
{
    bool bSchema=false,bVisible=false,bMounted=false,bLaidOut=false,bInputInStack=false,bHasUserFocus=false,bHasFocusedDescendants=false;
    float Opacity=0;
    UUserWidget* Widget=nullptr;UInputComponent* Input=nullptr;
};
struct FNativeDeadMapping
{
    bool bSchema=false,bSubsystemAvailable=false,bActive=false;
    int32 Priority=-1;
    UInputMappingContext* Context=nullptr;
};
struct FNativePrompt
{
    bool bVisible=false,bMounted=false,bLaidOut=false;
    float Opacity=0;
    FString Text,SlateType,Tag,WidgetClass,WidgetPath,SlatePath,NativeRootClass,NativeRootPath;
    FString TextProbe=TEXT("[]"),WidgetTypes=TEXT("[]");int32 NodesScanned=0;
};
struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,BeforeSettle,CaptureBefore,DeathObserve,CaptureDead,Respawning,CaptureRespawned,FreshFlight,FreshReturn,HangarOpened,HangarClosed };
    TWeakObjectPtr<UWorld> World;
    TWeakObjectPtr<AFGPlayerController> PC;
    TWeakObjectPtr<AFGCharacterPlayer> OriginalPawn;
    TWeakObjectPtr<UFGHealthComponent> OriginalHealth;
    TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;
    TArray<TWeakObjectPtr<UInputComponent>> OldModControls;
    TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> OldModSources;
    FString Mode,LastReason=TEXT("Waiting for exact standalone clone, living grounded pioneer and actual startup lifecycle."),PendingScreenshot;
    TMap<FString,FString> Snapshots;
    FCapture BeforeCapture,DeadCapture,RespawnedCapture;
    FDelegateHandle ScreenshotHandle;
    EPhase Phase=EPhase::Waiting;
    double Deadline=0,PhaseStarted=0,KillTime=0,UiReadySince=0,RespawnReadySince=0,CaptureStarted=0;
    uint64 PhaseFrame=0;
    bool bDone=false,bPrepared=false,bOwnedPanel=false,bCapture=false,bOwnScreenshotPending=false,bScreenshotProcessed=false,bScreenMessagesBefore=false;
    bool bSplashObserved=false,bSplashAnimationObserved=false,bStartupFinished=false,bFrontEndFinished=false;
    bool bKillCalled=false,bNativeDeathObserved=false,bRespawnUiObserved=false,bRespawnInputPreserved=false,bSawRespawnInput=false,bLostRespawnInput=false;
    bool bSawLegacyRespawnInput=false,bLostLegacyRespawnInput=false;
    bool bModSessionCleaned=false,bModWidgetsRemoved=false,bModControlsDetached=false,bModSourcesRetired=false;
    bool bNativeRespawnRequested=false,bNativeRespawnCompleted=false,bPostRespawnGameplayReady=false,bNoModLeases=false,bCapturesVerified=false;
    bool bFreshSessionPassed=false,bHangarRebound=false,bSawRespawning=false,bRespawnReusedPawn=false;
    bool bDeathChecksPassed=false;
    int32 NativeUiSamples=0;

    void SetPhase(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    bool Settled(double Now,double Seconds=.5) const { return Now-PhaseStarted>=Seconds && GFrameCounter>=PhaseFrame+3; }
    TSharedPtr<SWindow> ViewportWindow() const { auto* V=World.IsValid()?World->GetGameViewport():nullptr;return V?V->GetWindow():nullptr; }
    FNativeUI NativeUI() const
    {
        FNativeUI Out;auto* Controller=PC.Get();auto* Hud=Controller?Cast<AFGHUD>(Controller->GetHUD()):nullptr;if (!Hud) return Out;
        const auto* UiField=FindFProperty<FObjectPropertyBase>(Hud->GetClass(),TEXT("mRespawnUI"));
        const auto* InputField=FindFProperty<FObjectPropertyBase>(Hud->GetClass(),TEXT("mRespawnInputComponent"));
        Out.bSchema=UiField && UiField->PropertyClass==UUserWidget::StaticClass() && InputField && InputField->PropertyClass==UInputComponent::StaticClass();
        if (!Out.bSchema) return Out;
        Out.Widget=Cast<UUserWidget>(UiField->GetObjectPropertyValue_InContainer(Hud));Out.Input=Cast<UInputComponent>(InputField->GetObjectPropertyValue_InContainer(Hud));
        Out.bInputInStack=Out.Input && Controller->IsInputComponentInStack(Out.Input);
        if (!Out.Widget || !IsValid(Out.Widget)) return Out;
        Out.bHasUserFocus=Out.Widget->HasUserFocus(Controller);Out.bHasFocusedDescendants=Out.Widget->HasUserFocusedDescendants(Controller);
        auto Slate=Out.Widget->GetCachedWidget();auto Window=ViewportWindow();
        Out.bMounted=Slate && Window && FSlateApplication::IsInitialized() && FSlateApplication::Get().FindWidgetWindow(Slate.ToSharedRef())==Window;
        Out.bVisible=Out.Widget->IsVisible();Out.Opacity=Out.Widget->GetRenderOpacity()*Out.Widget->GetColorAndOpacity().A;
        for (int32 Depth=0;Slate && Depth<128;++Depth)
        {
            Out.bVisible&=Slate->GetVisibility().IsVisible();Out.Opacity*=Slate->GetRenderOpacity();Slate=Slate->GetParentWidget();
        }
        if (Slate || !FMath::IsFinite(Out.Opacity)) { Out.bVisible=false;Out.Opacity=0; }
        const auto& Geometry=Out.Widget->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
        Out.bLaidOut=Out.bMounted && FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X>=1 && Size.Y>=1
            && Window->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(Size*.5f));
        return Out;
    }
    FNativeDeadMapping DeadMapping() const
    {
        FNativeDeadMapping Out;auto* Controller=PC.Get();if (!Controller) return Out;
        const auto* Field=FindFProperty<FObjectPropertyBase>(Controller->GetClass(),TEXT("mMappingContextDead"));
        Out.bSchema=Field && Field->PropertyClass==UInputMappingContext::StaticClass();if (!Out.bSchema) return Out;
        Out.Context=Cast<UInputMappingContext>(Field->GetObjectPropertyValue_InContainer(Controller));
        auto* Local=Controller->GetLocalPlayer();auto* Subsystem=Local?Local->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>():nullptr;
        Out.bSubsystemAvailable=Subsystem!=nullptr;
        // Read only. Never add/remove/rebuild a native input mapping context.
        if (Out.Context && Subsystem) Out.bActive=Subsystem->HasMappingContext(Out.Context,Out.Priority);
        return Out;
    }
    FNativePrompt NativePrompt() const
    {
        FNativePrompt Out;auto* GameUI=PC.IsValid()?PC->GetGameUI():nullptr;auto Window=ViewportWindow();
        if (!GameUI || !Window || !FSlateApplication::IsInitialized()) return Out;
        Out.NativeRootClass=GameUI->GetClass()->GetPathName();Out.NativeRootPath=GameUI->GetPathName();
        const auto Root=GameUI->GetCachedWidget();if (!Root) return Out;
        // Search only the actual controller's native GameUI subtree. Mod panels
        // and unrelated world widgets cannot supply a substitute respawn label.
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Root.ToSharedRef());TSet<const SWidget*> Seen;int32 BestScore=-1;
        TArray<FString> TextProbe,WidgetTypes;
        while (!Pending.IsEmpty() && Seen.Num()<32768)
        {
            const auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());
            if (WidgetTypes.Num()<64) WidgetTypes.AddUnique(Widget->GetTypeAsString());
            FString Text;
            if (Widget->GetType()==FName(TEXT("STextBlock"))) Text=StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
            else if (Widget->GetType()==FName(TEXT("SRichTextBlock"))) Text=StaticCastSharedRef<SRichTextBlock>(Widget)->GetText().ToString();
            if (!Text.IsEmpty() && TextProbe.Num()<32)
                TextProbe.Add(FString::Printf(TEXT("{\"type\":\"%s\",\"tag\":\"%s\",\"text\":\"%s\"}"),*Escape(Widget->GetTypeAsString()),*Escape(Widget->GetTag().ToString()),*Escape(Text)));
            if (Text.Contains(TEXT("Respawn"),ESearchCase::IgnoreCase))
            {
                FNativePrompt Candidate;Candidate.Text=Text;Candidate.SlateType=Widget->GetTypeAsString();Candidate.Tag=Widget->GetTag().ToString();
                Candidate.NativeRootClass=Out.NativeRootClass;Candidate.NativeRootPath=Out.NativeRootPath;
                Candidate.bMounted=FSlateApplication::Get().FindWidgetWindow(Widget)==Window;
                Candidate.bVisible=GameUI->IsVisible();Candidate.Opacity=GameUI->GetRenderOpacity()*GameUI->GetColorAndOpacity().A;
                TSharedPtr<SWidget> Parent=Widget;TArray<FString> Ancestry;bool SawRoot=false;
                for (int32 Depth=0;Parent && Depth<128;++Depth)
                {
                    Candidate.bVisible&=Parent->GetVisibility().IsVisible();Candidate.Opacity*=Parent->GetRenderOpacity();
                    Ancestry.Add(Parent->GetTypeAsString()+TEXT("[")+Parent->GetTag().ToString()+TEXT("]"));
                    SawRoot|=Parent==Root;Parent=Parent->GetParentWidget();
                }
                if (Parent || !SawRoot || !FMath::IsFinite(Candidate.Opacity)) { Candidate.bVisible=false;Candidate.Opacity=0; }
                for (int32 Index=Ancestry.Num()-1;Index>=0;--Index) { if (!Candidate.SlatePath.IsEmpty()) Candidate.SlatePath+=TEXT(" > ");Candidate.SlatePath+=Ancestry[Index]; }
                const auto& Geometry=Widget->GetCachedGeometry();const auto Size=Geometry.GetLocalSize();
                Candidate.bLaidOut=Candidate.bMounted && FMath::IsFinite(Size.X) && FMath::IsFinite(Size.Y) && Size.X>=1 && Size.Y>=1
                    && Window->GetClientRectInScreen().ContainsPoint(Geometry.LocalToAbsolute(Size*.5f));
                const int32 Score=(Candidate.bVisible && Candidate.Opacity>.95f?16:0)+(Candidate.bVisible?4:0)+(Candidate.bMounted?2:0)+(Candidate.bLaidOut?1:0);
                if (Score>BestScore)
                {
                    BestScore=Score;
                    // Associate the selected Slate text with its actual UWidget
                    // where available; this is diagnostic readback, not lookup
                    // by a guessed Blueprint property or fabricated UI class.
                    for (TObjectIterator<UWidget> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()
                        && It->GetCachedWidget().Get()==&Widget.Get()) { Candidate.WidgetClass=It->GetClass()->GetPathName();Candidate.WidgetPath=It->GetPathName();break; }
                    Out=MoveTemp(Candidate);
                }
            }
            if (auto* Children=Widget->GetChildren()) for (int32 Index=0;Index<Children->Num();++Index) Pending.Add(Children->GetChildAt(Index));
        }
        Out.NodesScanned=Seen.Num();Out.TextProbe=TEXT("[")+FString::Join(TextProbe,TEXT(","))+TEXT("]");
        Out.WidgetTypes=TEXT("[");for (int32 Index=0;Index<WidgetTypes.Num();++Index)
        { if (Index) Out.WidgetTypes+=TEXT(",");Out.WidgetTypes+=TEXT("\"")+Escape(WidgetTypes[Index])+TEXT("\""); }Out.WidgetTypes+=TEXT("]");
        return Out;
    }
    bool ContainsPresentedModWidget() const
    {
        auto Window=ViewportWindow();if (!Window) return false;TArray<TSharedRef<SWidget>> Pending;Pending.Add(Window.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768)
        {
            auto Widget=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&Widget.Get())) continue;Seen.Add(&Widget.Get());
            const FName Type=Widget->GetType();
            if (Type==FName(TEXT("SFactoryFPVOSD")) || Type==FName(TEXT("SFactoryFPVPanel")) || Type==FName(TEXT("SFactoryFPVRacePanel")))
            {
                TSharedPtr<SWidget> Parent=Widget;bool Visible=true;
                for (int32 Depth=0;Parent && Depth<128;++Depth) { Visible&=Parent->GetVisibility().IsVisible();Parent=Parent->GetParentWidget(); }
                if (Visible && !Parent) return true;
            }
            if (auto* Children=Widget->GetChildren()) for (int32 I=0;I<Children->Num();++I) Pending.Add(Children->GetChildAt(I));
        }
        return false;
    }
    bool NoModLeases() const
    {
        const auto D=FactoryFPVInputLease::GetDiagnostics(PC.Get());return D.ActiveLeases==0 && D.OwnedMoveContributions==0 && D.OwnedLookContributions==0 && D.ResetDepth==0;
    }
    void CollectModResources()
    {
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && It->GetWorld()==World.Get() && !It->IsTemplate() && PC->IsInputComponentInStack(*It)
            && (It->GetOwner()==Drone.Get() || (It->GetOwner()==PC.Get() && It->GetName().StartsWith(TEXT("FactoryFPVHangarControls"))))) OldModControls.AddUnique(*It);
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && It->GetWorld()==World.Get() && !It->IsTemplate()
            && (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner()))) OldModSources.AddUnique(*It);
    }
    void ObserveModCleanup()
    {
        bModSessionCleaned=!AFactoryFPVDrone::FindActive(World.Get()) && (!Hangar.IsValid() || !Hangar->IsPanelOpen());
        for (TActorIterator<AFactoryFPVPilotVisual> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed()) bModSessionCleaned=false;
        bModWidgetsRemoved=!ContainsPresentedModWidget();bModControlsDetached=PC.IsValid();
        for (const auto& Item:OldModControls) if (Item.IsValid() && PC->IsInputComponentInStack(Item.Get())) bModControlsDetached=false;
        bModSourcesRetired=true;for (const auto& Item:OldModSources) if (Item.IsValid() && Item->IsStreamingSourceEnabled()) bModSourcesRetired=false;
        for (TActorIterator<AFactoryFPVPioneerAnchor> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed())
            if (auto* Source=It->FindComponentByClass<UWorldPartitionStreamingSourceComponent>();Source && Source->IsStreamingSourceEnabled()) bModSourcesRetired=false;
        bNoModLeases=NoModLeases();
    }
    FString SnapshotJson() const
    {
        auto* Controller=PC.Get();auto* Pawn=Controller?Cast<AFGCharacterPlayer>(Controller->GetPawn()):nullptr;auto* Hud=Controller?Cast<AFGHUD>(Controller->GetHUD()):nullptr;
        auto* Camera=Controller?Controller->PlayerCameraManager.Get():nullptr;auto* View=Controller?Controller->GetViewTarget():nullptr;auto* V=World.IsValid()?World->GetGameViewport():nullptr;
        auto* Health=Pawn?Pawn->GetHealthComponent():nullptr;auto* GameUI=Controller?Controller->GetGameUI():nullptr;const auto Ui=NativeUI();const auto Mapping=DeadMapping();const auto Prompt=NativePrompt();const auto Lease=FactoryFPVInputLease::GetDiagnostics(Controller);
        const auto Focus=FSlateApplication::IsInitialized()?FSlateApplication::Get().GetKeyboardFocusedWidget():nullptr;
        FString Json=FString::Printf(TEXT("{\"cursor\":%s,\"viewportIgnoresInput\":%s,\"moveIgnored\":%s,\"lookIgnored\":%s,\"respawnHasUserFocus\":%s,\"respawnHasFocusedDescendants\":%s,\"nativeRespawnInputInStack\":%s,\"respawnUiVisible\":%s,\"respawnUiMounted\":%s,\"respawnUiLaidOut\":%s,\"respawnUiOpacity\":%s,\"respawnSchemaValid\":%s,\"respawnUiPresent\":%s,\"nativeRespawnInputPresent\":%s"),
            Bool(Controller && Controller->bShowMouseCursor),Bool(V && V->IgnoreInput()),Bool(Controller && Controller->IsMoveInputIgnored()),Bool(Controller && Controller->IsLookInputIgnored()),
            Bool(Ui.bHasUserFocus),Bool(Ui.bHasFocusedDescendants),Bool(Ui.bInputInStack),Bool(Ui.bVisible),Bool(Ui.bMounted),Bool(Ui.bLaidOut),*Number(Ui.Opacity),Bool(Ui.bSchema),Bool(Ui.Widget!=nullptr),Bool(Ui.Input!=nullptr));
        Json+=FString::Printf(TEXT(",\"nativeRespawnPromptVisible\":%s,\"nativeRespawnPromptMounted\":%s,\"nativeRespawnPromptLaidOut\":%s,\"nativeRespawnPromptOpacity\":%s,\"nativeRespawnPromptText\":\"%s\",\"nativeRespawnPromptSlateType\":\"%s\",\"nativeRespawnPromptTag\":\"%s\",\"nativeRespawnPromptWidgetClass\":\"%s\",\"nativeRespawnPromptWidgetPath\":\"%s\",\"nativeRespawnPromptSlatePath\":\"%s\",\"nativeRespawnPromptRootClass\":\"%s\",\"nativeRespawnPromptRootPath\":\"%s\",\"deadMappingActive\":%s,\"deadMappingSchemaValid\":%s,\"deadMappingSubsystemAvailable\":%s,\"deadMappingContextPath\":\"%s\",\"deadMappingPriority\":%d"),
            Bool(Prompt.bVisible),Bool(Prompt.bMounted),Bool(Prompt.bLaidOut),*Number(Prompt.Opacity),*Escape(Prompt.Text),*Escape(Prompt.SlateType),*Escape(Prompt.Tag),*Escape(Prompt.WidgetClass),*Escape(Prompt.WidgetPath),*Escape(Prompt.SlatePath),
            *Escape(Prompt.NativeRootClass),*Escape(Prompt.NativeRootPath),Bool(Mapping.bActive),Bool(Mapping.bSchema),Bool(Mapping.bSubsystemAvailable),*Escape(GetPathNameSafe(Mapping.Context)),Mapping.Priority);
        Json+=FString::Printf(TEXT(",\"nativePromptNodesScanned\":%d,\"nativePromptTextProbe\":%s,\"nativePromptWidgetTypes\":%s"),Prompt.NodesScanned,*Prompt.TextProbe,*Prompt.WidgetTypes);
        Json+=FString::Printf(TEXT(",\"fov\":%s,\"lockedFov\":%s,\"hudPartial\":%s,\"hudVisible\":%s,\"hudShow\":%s,\"viewTargetClass\":\"%s\",\"currentPawnClass\":\"%s\",\"currentPawnPath\":\"%s\",\"respawnUiClass\":\"%s\",\"nativeRespawnInputClass\":\"%s\",\"focusedWidgetType\":\"%s\",\"mesh1P\":%s,\"mesh3P\":%s"),
            *Number(Camera?Camera->GetFOVAngle():std::numeric_limits<double>::quiet_NaN()),*Number(Camera?Camera->GetLockedFOV():std::numeric_limits<double>::quiet_NaN()),Bool(Hud && Hud->GetPartialPumpiMode()),Bool(Hud && Hud->GetHUDVisibility()),Bool(Hud && Hud->bShowHUD),
            *Escape(ClassPath(View)),*Escape(ClassPath(Pawn)),*Escape(GetPathNameSafe(Pawn)),*Escape(ClassPath(Ui.Widget)),*Escape(ClassPath(Ui.Input)),Focus?*Escape(Focus->GetTypeAsString()):TEXT(""),
            *MeshJson(Pawn?Pawn->GetMesh1P():nullptr),*MeshJson(Pawn?Pawn->GetMesh3P():nullptr));
        Json+=FString::Printf(TEXT(",\"currentPawnAlive\":%s,\"healthDead\":%s,\"health\":%s,\"originalHealthDead\":%s,\"needRespawn\":%s,\"isRespawning\":%s,\"respawnGracePeriod\":%s,\"respawningFromDeath\":%s,\"worldPaused\":%s,\"controllerPaused\":%s,\"interactWidgetActive\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"hooksReady\":%s}"),
            Bool(Pawn && Pawn->IsAliveAndWell()),Bool(Health && Health->IsDead()),*Number(Health?Health->GetCurrentHealth():std::numeric_limits<double>::quiet_NaN()),Bool(OriginalHealth.IsValid() && OriginalHealth->IsDead()),
            Bool(Controller && Controller->NeedRespawn()),Bool(Controller && Controller->IsRespawning()),Bool(Controller && Controller->IsClientRespawnGracePeriodActive()),Bool(Controller && Controller->IsRespawningFromDeath()),
            Bool(World.IsValid() && World->IsPaused()),Bool(Controller && Controller->IsPaused()),Bool(GameUI && GameUI->HasActiveInteractWidget()),Lease.ActiveLeases,Lease.OwnedMoveContributions,Lease.OwnedLookContributions,Bool(Lease.bNativeHooksReady));
        return Json;
    }
    void Snapshot(const TCHAR* Name) { Snapshots.Add(Name,SnapshotJson()); }
    FString Evidence() const
    {
        FString Json=FString::Printf(TEXT(",\"mode\":\"%s\",\"nativeDeathObserved\":%s,\"respawnUiObserved\":%s,\"respawnInputPreserved\":%s,\"modSessionCleaned\":%s,\"modWidgetsRemoved\":%s,\"modControlsDetached\":%s,\"modSourcesRetired\":%s,\"nativeRespawnRequested\":%s,\"nativeRespawnCompleted\":%s,\"postRespawnGameplayReady\":%s,\"noModLeases\":%s,\"capturesVerified\":%s,\"freshSessionPassed\":%s,\"hangarRebound\":%s,\"injectedFlightControls\":false,\"forcedRespawnCompletion\":false,\"savedWorld\":false,\"programmaticNativeDeathAndRespawn\":true,\"programmaticMenuActions\":true,\"baseline\":%s,\"captureEnabled\":%s,\"killCalled\":%s,\"respawnTransitionObserved\":%s,\"respawnReusedPawn\":%s,\"nativeUiSamples\":%d,\"deathChecksPassed\":%s"),
            *Escape(Mode),Bool(bNativeDeathObserved),Bool(bRespawnUiObserved),Bool(bRespawnInputPreserved),Bool(bModSessionCleaned),Bool(bModWidgetsRemoved),Bool(bModControlsDetached),Bool(bModSourcesRetired),
            Bool(bNativeRespawnRequested),Bool(bNativeRespawnCompleted),Bool(bPostRespawnGameplayReady),Bool(bNoModLeases),Bool(bCapturesVerified),Bool(bFreshSessionPassed),Bool(bHangarRebound),Bool(Mode==TEXT("baseline")),Bool(bCapture),Bool(bKillCalled),Bool(bSawRespawning),Bool(bRespawnReusedPawn),NativeUiSamples,Bool(bDeathChecksPassed));
        Json+=FString::Printf(TEXT(",\"variant\":\"%s\",\"startupSplashObserved\":%s,\"startupSplashAnimationObserved\":%s,\"startupSplashFinished\":%s,\"snapshots\":{"),*Escape(Mode),Bool(bSplashObserved),Bool(bSplashAnimationObserved),Bool(bStartupFinished));bool First=true;
        for (const TCHAR* Name:{TEXT("initial"),TEXT("before"),TEXT("afterKill"),TEXT("dead"),TEXT("afterStartRespawn"),TEXT("respawned"),TEXT("freshReturn")})
            if (const FString* Value=Snapshots.Find(Name)) { if (!First) Json+=TEXT(",");First=false;Json+=FString::Printf(TEXT("\"%s\":%s"),Name,**Value); }
        Json+=TEXT("},\"captures\":[");if (bCapture) Json+=BeforeCapture.Json()+TEXT(",")+DeadCapture.Json()+TEXT(",")+RespawnedCapture.Json();
        return Json+TEXT("]");
    }
    bool InitialReady()
    {
        auto* Current=World.Get();auto* Game=Current?Current->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=Game?Game->GetSaveSession():nullptr;
        auto* Controller=Current?Cast<AFGPlayerController>(Current->GetFirstPlayerController()):nullptr;auto* Pawn=Controller?Cast<AFGCharacterPlayer>(Controller->GetPawn()):nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !Current->HasBegunPlay() || !Game || Game->IsMainMenuGameMode() || !Save || Save->HasSaveGameOperationPending()
            || !Controller || !Controller->IsLocalController() || !Controller->GetLocalPlayer()) return false;
        const auto Load=Game->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        if ((Load.LoadName!=TEXT("FPV-QA-Clone") && Header.SaveName!=TEXT("FPV-QA-Clone")) || Header.MapName.IsEmpty() || Header.SessionName.IsEmpty()) return false;
        auto* Ui=Controller->GetGameUI();if (!Ui) return false;
        // Witness the real splash while native join/respawn grace is still
        // active; waiting for that grace first can miss the whole animation.
        if (!FactoryFPVShippingStartup::Ready(Ui,LastReason,bSplashObserved,bSplashAnimationObserved,bStartupFinished,bFrontEndFinished)) return false;
        if (!Pawn || !Pawn->IsAliveAndWell() || !Pawn->HasAuthority() || !Controller->PlayerCameraManager || Controller->IsPaused()
            || Controller->IsPauseMenuOpen() || Controller->NeedRespawn() || Controller->IsRespawning() || Controller->IsClientRespawnGracePeriodActive()
            || Ui->HasActiveInteractWidget() || Ui->IsPauseMenuOpen()) {
            LastReason=TEXT("Waiting for living authoritative gameplay and native respawn/join grace after the observed startup splash.");return false;
        }
        PC=Controller;OriginalPawn=Pawn;OriginalHealth=Pawn->GetHealthComponent();Hangar=Controller->GetLocalPlayer()->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();
        const auto Native=NativeUI();const auto Mapping=DeadMapping();
        if (!OriginalHealth.IsValid() || OriginalHealth->IsDead() || OriginalHealth->GetOwner()!=Pawn || !Hangar.IsValid() || !Native.bSchema
            || Native.bInputInStack || (Native.bMounted && Native.bVisible) || !Mapping.bSchema || !Mapping.Context || !Mapping.bSubsystemAvailable || Mapping.bActive) return false;
        return true;
    }
    bool NativeWorldUnchanged()
    {
        auto* Current=World.Get();auto* Game=Current?Current->GetAuthGameMode<AFGGameMode>():nullptr;auto* Save=Game?Game->GetSaveSession():nullptr;
        if (!Current || Current->bIsTearingDown || Current->GetNetMode()!=NM_Standalone || !PC.IsValid() || Current->GetFirstPlayerController()!=PC.Get() || !Save || Save->HasSaveGameOperationPending()) return false;
        const auto Load=Game->GetLoadData();const auto& Header=Save->GetLoadedSaveHeader();
        return (Load.LoadName==TEXT("FPV-QA-Clone") || Header.SaveName==TEXT("FPV-QA-Clone")) && !Header.MapName.IsEmpty();
    }
    bool InvokeHangarF6()
    {
        FInputActionUnifiedDelegate Callback;int32 Count=0;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It))
            for (const auto& Binding:It->KeyBindings) if (Binding.Chord.Key==EKeys::F6 && Binding.KeyEvent==IE_Pressed && Binding.KeyDelegate.IsBoundToObject(Hangar.Get())) { Callback=Binding.KeyDelegate;++Count; }
        if (Count!=1) { LastReason=TEXT("Waiting for one actual rebound hangar F6 callback.");return false; }
        Callback.Execute(EKeys::F6);return true;
    }
    bool Prepare(double Now)
    {
        if (Hangar->IsPanelOpen() || AFactoryFPVDrone::FindActive(World.Get()) || AFactoryFPVPioneerAnchor::FindPending(World.Get(),OriginalPawn.Get()) || !NoModLeases()
            || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored()) { Finish(false,TEXT("Lifecycle QA requires no pre-existing mod/input owners."));return false; }
        auto* Move=OriginalPawn->GetCharacterMovement();FFindFloorResult Floor;if (Move) Move->FindFloor(OriginalPawn->GetActorLocation(),Floor,false);
        if (!Move || !Move->IsMovingOnGround() || !Floor.IsWalkableFloor()) return false;
        Snapshot(TEXT("initial"));
        if (Mode==TEXT("flight"))
        {
            FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
            if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Ordinary disarmed pre-death launch failed: ")+Error);return false; }
        }
        else if (Mode==TEXT("panel"))
        {
            if (!InvokeHangarF6()) return false;bOwnedPanel=Hangar->IsPanelOpen();
            if (!bOwnedPanel) { Finish(false,TEXT("Actual bound F6 callback did not open the pre-death hangar."));return false; }
        }
        CollectModResources();bPrepared=true;SetPhase(EPhase::BeforeSettle,Now);return true;
    }
    void ReleaseScreenshot()
    {
        if (ScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset(); }
        if (bOwnScreenshotPending && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingScreenshot) { FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore; }
        bOwnScreenshotPending=false;
    }
    bool RequestCapture(FCapture& Capture,const TCHAR* State,double Now)
    {
        auto* V=World.IsValid()?World->GetGameViewport():nullptr;
        if (FScreenshotRequest::IsScreenshotRequested() || !V || !V->Viewport || V->Viewport->GetSizeXY().X<320 || V->Viewport->GetSizeXY().Y<180) { Finish(false,TEXT("Viewport unavailable or an unrelated screenshot is pending."));return false; }
        FString Saved=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());FPaths::NormalizeDirectoryName(Saved);const FString Dir=FPaths::Combine(Saved,TEXT("FactoryFPV-QA"));
        FString Path=FPaths::Combine(Dir,FString::Printf(TEXT("Lifecycle-%s-%s.png"),State,*Mode));FPaths::NormalizeFilename(Path);
        if (!FPaths::CollapseRelativeDirectories(Path) || !Disposable(Saved) || !FPaths::IsUnderDirectory(Path,Dir) || !FPaths::IsUnderDirectory(Dir,Saved)
            || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Dir,true)) { Finish(false,TEXT("Capture path is not fresh and inside disposable Saved."));return false; }
        ReleaseScreenshot();Capture.Path=PendingScreenshot=Path;CaptureStarted=Now;bScreenshotProcessed=false;bOwnScreenshotPending=true;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> Weak=AsShared();ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak]()
        { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnScreenshotPending) { Run->bScreenshotProcessed=true;Run->bOwnScreenshotPending=false; } });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);LastReason=TEXT("Waiting for actual processed viewport capture: ")+Path;return true;
    }
    bool CaptureFinished(FCapture& Capture,double Now)
    {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Lifecycle viewport screenshot exceeded five seconds: ")+Capture.Path);return false; }
        if (!bScreenshotProcessed) return false;ReleaseScreenshot();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*Capture.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) || Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180
            || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty()) { Finish(false,TEXT("Processed lifecycle capture is not a sane decodable PNG."));return false; }
        double Sum=0,Squares=0;int32 Samples=0;
        for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60))
        {
            const auto Color=Image.GetOnePixelLinear(X,Y);if (!FMath::IsFinite(Color.R) || !FMath::IsFinite(Color.G) || !FMath::IsFinite(Color.B)) { Finish(false,TEXT("Lifecycle capture contains nonfinite pixels."));return false; }
            const double L=.2126*Color.R+.7152*Color.G+.0722*Color.B;Sum+=L;Squares+=L*L;++Samples;
        }
        Capture.LumaVariance=Samples?FMath::Max(0.,Squares/Samples-FMath::Square(Sum/Samples)):0.;
        Capture.Width=Image.SizeX;Capture.Height=Image.SizeY;Capture.bDecoded=true;return true;
    }
    void Kill(double Now)
    {
        if (!OriginalPawn.IsValid() || !OriginalHealth.IsValid() || !OriginalPawn->IsAliveAndWell() || OriginalHealth->IsDead()) { Finish(false,TEXT("Original living native health owner changed before Kill."));return; }
        bKillCalled=true;KillTime=Now;
        // Native delegates can synchronously unequip/return the drone and change
        // views. Only weak state is used after this call; no manual cleanup here.
        OriginalHealth->Kill();Snapshot(TEXT("afterKill"));SetPhase(EPhase::DeathObserve,Now);
        LastReason=TEXT("Observing natural native death UI/input and mod cleanup before requesting respawn.");
    }
    void RequestRespawn(double Now)
    {
        if (!PC->NeedRespawn() || PC->IsRespawning()) { Finish(false,TEXT("Native controller is not awaiting the requested public respawn flow."));return; }
        const auto Mapping=DeadMapping();const auto Ui=NativeUI();
        if (!Mapping.bSchema || !Mapping.bSubsystemAvailable || !Mapping.bActive || bLostRespawnInput || bLostLegacyRespawnInput
            || (bSawLegacyRespawnInput && !Ui.bInputInStack)) { bRespawnInputPreserved=false;bDeathChecksPassed=false; }
        bNativeRespawnRequested=true;PC->StartRespawn();bSawRespawning|=PC->IsRespawning();Snapshot(TEXT("afterStartRespawn"));SetPhase(EPhase::Respawning,Now);
        LastReason=TEXT("Waiting for natural Blueprint/native StartRespawn effects, level readiness and living supported pawn; no completion RPC is invoked.");
    }
    bool GameplayReady()
    {
        auto* Pawn=Cast<AFGCharacterPlayer>(PC->GetPawn());auto* Move=Pawn?Pawn->GetCharacterMovement():nullptr;auto* Health=Pawn?Pawn->GetHealthComponent():nullptr;
        const auto Ui=NativeUI();const auto Mapping=DeadMapping();FFindFloorResult Floor;if (Move) Move->FindFloor(Pawn->GetActorLocation(),Floor,false);
        return Pawn && Pawn->IsAliveAndWell() && Health && !Health->IsDead() && FMath::IsFinite(Health->GetCurrentHealth()) && Health->GetCurrentHealth()>0
            && !PC->NeedRespawn() && !PC->IsRespawning() && !PC->IsClientRespawnGracePeriodActive() && Ui.bSchema
            && (!Ui.Widget || !Ui.bMounted || !Ui.bVisible) && !Ui.bInputInStack && Mapping.bSchema && Mapping.Context && Mapping.bSubsystemAvailable && !Mapping.bActive
            && Move && Move->IsMovingOnGround() && Floor.IsWalkableFloor()
            && PC->GetViewTarget()!=Drone.Get() && !AFactoryFPVDrone::FindActive(World.Get()) && !AFactoryFPVPioneerAnchor::FindPending(World.Get(),Pawn);
    }
    void Cleanup()
    {
        ReleaseScreenshot();
        // Only our mod owners are retired. Native death UI, health, inventory,
        // corpse/crate, respawn state and spawn position are never repaired here.
        if (auto* Own=Drone.Get();Own && !Own->IsActorBeingDestroyed() && Own->GetOwner()==PC.Get()) Own->ExitDrone();
        if (bOwnedPanel && Hangar.IsValid() && Hangar->IsPanelOpen()) Hangar->ClosePanel();
    }
    void Finish(bool Success,const FString& Reason)
    {
        if (bDone) return;bDone=true;Cleanup();Marker(Mode,Success?TEXT("PASS"):TEXT("FAIL"),Reason,Evidence());FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    void Complete()
    {
        bCapturesVerified=!bCapture || (BeforeCapture.bDecoded && DeadCapture.bDecoded && RespawnedCapture.bDecoded);
        bNoModLeases=NoModLeases();
        const bool Pass=bDeathChecksPassed && bNativeRespawnRequested && bNativeRespawnCompleted && bPostRespawnGameplayReady && bNoModLeases && bCapturesVerified
            && (Mode==TEXT("baseline") || (bFreshSessionPassed && bHangarRebound));
        Finish(Pass,Pass?TEXT("Native death/UI/input and public respawn completed on the disposable clone; variant cleanup and resumed gameplay earned their recorded checks. No save or forced completion."):
            TEXT("Native lifecycle completed but one or more recorded death/UI/mod-cleanup or resumed-workflow checks failed; compare snapshots with the baseline."));
    }
    bool Tick()
    {
        if (bDone) return false;const double Now=FPlatformTime::Seconds();
        if (!Guard() || !World.IsValid()) { Finish(false,TEXT("Lifecycle isolation guard/world changed."));return false; }
        if (Now>Deadline) { if (bPrepared && PC.IsValid()) Snapshot(TEXT("timeout"));Finish(false,TEXT("60-second lifecycle deadline: ")+LastReason);return false; }
        if (Phase==EPhase::Waiting) { if (InitialReady()) Prepare(Now);return !bDone; }
        if (!NativeWorldUnchanged()) { Finish(false,TEXT("Original standalone clone/controller changed or a save operation began."));return false; }
        if (Phase==EPhase::BeforeSettle)
        {
            if (!Settled(Now)) return true;Snapshot(TEXT("before"));
            if (bCapture) { if (RequestCapture(BeforeCapture,TEXT("Before"),Now)) SetPhase(EPhase::CaptureBefore,Now); }
            else Kill(Now);
        }
        else if (Phase==EPhase::CaptureBefore) { if (CaptureFinished(BeforeCapture,Now)) Kill(Now); }
        else if (Phase==EPhase::DeathObserve)
        {
            bNativeDeathObserved|=(OriginalHealth.IsValid() && OriginalHealth->IsDead()) || (PC->NeedRespawn() && (!OriginalPawn.IsValid() || !OriginalPawn->IsAliveAndWell()));
            const auto Ui=NativeUI();const auto Mapping=DeadMapping();const auto Prompt=NativePrompt();
            if (!Ui.bSchema || !Mapping.bSchema || !Mapping.Context || !Mapping.bSubsystemAvailable) { Finish(false,TEXT("Known native HUD/dead mapping schema or Enhanced Input subsystem is unavailable."));return false; }
            if (Mapping.bActive) bSawRespawnInput=true;else if (bSawRespawnInput && PC->NeedRespawn()) bLostRespawnInput=true;
            if (Ui.bInputInStack) bSawLegacyRespawnInput=true;else if (bSawLegacyRespawnInput && Ui.Widget && Ui.bVisible) bLostLegacyRespawnInput=true;
            const bool Presented=Prompt.bVisible && Prompt.bMounted && Prompt.bLaidOut && Prompt.Opacity>.95f;
            if (Presented) { bRespawnUiObserved=true;++NativeUiSamples;if (UiReadySince==0) UiReadySince=Now; } else UiReadySince=0;
            if (!bNativeDeathObserved || !PC->NeedRespawn() || !Settled(Now,1.)) return true;
            // Retain red evidence if a variant breaks native presentation; do
            // not manually close/repair it to manufacture a successful result.
            if (!(UiReadySince>0 && Now-UiReadySince>=.5) && Now-KillTime<5.) return true;
            bRespawnInputPreserved=Mapping.bActive && bSawRespawnInput && !bLostRespawnInput && !bLostLegacyRespawnInput
                && (!bSawLegacyRespawnInput || Ui.bInputInStack);ObserveModCleanup();Snapshot(TEXT("dead"));
            bDeathChecksPassed=bNativeDeathObserved && bRespawnUiObserved && bRespawnInputPreserved && bModSessionCleaned && bModWidgetsRemoved && bModControlsDetached && bModSourcesRetired && bNoModLeases;
            if (bCapture) { if (RequestCapture(DeadCapture,TEXT("Dead"),Now)) SetPhase(EPhase::CaptureDead,Now); }
            else RequestRespawn(Now);
        }
        else if (Phase==EPhase::CaptureDead) { if (CaptureFinished(DeadCapture,Now)) RequestRespawn(Now); }
        else if (Phase==EPhase::Respawning)
        {
            bSawRespawning|=PC->IsRespawning();
            if (!GameplayReady()) { RespawnReadySince=0;return true; }
            if (RespawnReadySince==0) RespawnReadySince=Now;if (Now-RespawnReadySince<.75) return true;
            bNativeRespawnCompleted=true;bPostRespawnGameplayReady=true;bRespawnReusedPawn=OriginalPawn.IsValid() && PC->GetPawn()==OriginalPawn.Get();Snapshot(TEXT("respawned"));
            if (bCapture) { if (RequestCapture(RespawnedCapture,TEXT("Respawned"),Now)) SetPhase(EPhase::CaptureRespawned,Now); }
            else if (Mode==TEXT("baseline")) Complete();else BeginFresh(Now);
        }
        else if (Phase==EPhase::CaptureRespawned)
        { if (CaptureFinished(RespawnedCapture,Now)) { if (Mode==TEXT("baseline")) Complete();else BeginFresh(Now); } }
        else if (Phase==EPhase::FreshFlight)
        {
            if (!Drone.IsValid() || Drone->IsFlightArmed() || PC->GetViewTarget()!=Drone.Get()) { Finish(false,TEXT("Fresh ordinary disarmed session did not retain the respawned pioneer/view."));return false; }
            if (Settled(Now)) { Drone->ExitDrone();SetPhase(EPhase::FreshReturn,Now); }
        }
        else if (Phase==EPhase::FreshReturn)
        {
            LastReason=TEXT("Waiting for ordinary fresh-session return and unique rebound F6 input after native respawn.");
            if (!Settled(Now) || !GameplayReady() || !NoModLeases()) return true;
            bFreshSessionPassed=true;Snapshot(TEXT("freshReturn"));
            if (InvokeHangarF6()) { bOwnedPanel=Hangar->IsPanelOpen();if (!bOwnedPanel) { Finish(false,TEXT("Rebound F6 callback failed to open hangar after fresh flight."));return false; }SetPhase(EPhase::HangarOpened,Now); }
        }
        else if (Phase==EPhase::HangarOpened && Settled(Now))
        {
            if (!Hangar->IsPanelOpen() || !InvokeHangarF6()) { Finish(false,TEXT("Actual rebound F6 callback did not remain available for hangar close."));return false; }
            if (Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual rebound F6 callback did not close hangar."));return false; }
            bOwnedPanel=false;SetPhase(EPhase::HangarClosed,Now);
        }
        else if (Phase==EPhase::HangarClosed && Settled(Now))
        {
            bHangarRebound=GameplayReady() && NoModLeases() && !ContainsPresentedModWidget();Complete();
        }
        return !bDone;
    }
    void BeginFresh(double Now)
    {
        FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);
        if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Fresh ordinary post-respawn launch failed without teleport/cheat fallback: ")+Error);return; }
        SetPhase(EPhase::FreshFlight,Now);
    }
};
TWeakPtr<FRun> Active;
}

void FFactoryFPVShippingLifecycleQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingLifecycleQAInternal;
    FString Mode;FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQALifecycle="),Mode);Mode.ToLowerInline();
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (Mode!=TEXT("baseline") && Mode!=TEXT("flight") && Mode!=TEXT("panel")))
    {
        Marker(Mode.IsEmpty()?TEXT("invalid"):Mode,TEXT("FAIL"),TEXT("Explicit automation/disposable standalone clone and -FactoryFPVQALifecycle=baseline|flight|panel are required."));
        Output.Log(TEXT("Lifecycle QA guard/mode refused."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return;
    }
    if (auto Existing=Active.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Lifecycle QA is already active."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Mode=Mode;Run->Deadline=FPlatformTime::Seconds()+60.;Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Active=Run;
    Marker(Mode,TEXT("PENDING"),TEXT("Waiting for exact living clone/native startup before actual native Kill and preferred public StartRespawn; death state is intentionally not healed/restored or saved."));
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float){return Run->Tick();}),.1f);
}
