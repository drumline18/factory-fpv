#include "FactoryFPVShippingSaveQA.h"
#include "FactoryFPVShippingArmedSaveFlight.h"
#include "FactoryFPVShippingStartup.h"
#include "FactoryFPVShippingKitQA.h"
#include "FactoryFPVEquipment.h"
#include "FactoryFPVInputLeaseInternal.h"
#include "FactoryFPVDrone.h"
#include "FactoryFPVLocalPlayerSubsystem.h"
#include "FactoryFPVPioneerAnchor.h"
#include "FactoryFPVPilotVisual.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGGameState.h"
#include "FGGamePhase.h"
#include "FGGamePhaseManager.h"
#include "FGHealthComponent.h"
#include "FGHUD.h"
#include "FGInventoryComponent.h"
#include "FGInventoryComponentEquipment.h"
#include "FGPlayerController.h"
#include "FGSaveSession.h"
#include "FGSaveSystem.h"
#include "FGSchematicManager.h"
#include "Async/Async.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Console.h"
#include "Engine/GameInstance.h"
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
#include "InputCoreTypes.h"
#include "Layout/Children.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "UObject/ObjectKey.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

void UFactoryFPVShippingAutosaveObserver::OnCountdown(float Seconds) { if (Countdown) Countdown(Seconds); }
void UFactoryFPVShippingAutosaveObserver::OnQueuedNotification() { if (Queued) Queued(); }

namespace FactoryFPVShippingSaveQAInternal
{
const TCHAR* CloneName=TEXT("FPV-QA-Clone");
const TCHAR* OutputName=TEXT("FPV-QA-ActiveSave");
FString Escape(FString S) { return S.Replace(TEXT("\\"),TEXT("\\\\")).Replace(TEXT("\""),TEXT("\\\"")).Replace(TEXT("\r"),TEXT("\\r")).Replace(TEXT("\n"),TEXT("\\n")).Replace(TEXT("\t"),TEXT("\\t")); }
const TCHAR* B(bool V) { return V?TEXT("true"):TEXT("false"); }
FString Number(double V) { return FMath::IsFinite(V)?FString::Printf(TEXT("%.9f"),V):TEXT("null"); }
FString Class(const UObject* O) { return O?O->GetClass()->GetPathName():FString(); }
FString Vec(const FVector& V) { return TEXT("[")+Number(V.X)+TEXT(",")+Number(V.Y)+TEXT(",")+Number(V.Z)+TEXT("]"); }
FString Rot(const FRotator& V) { return TEXT("[")+Number(V.Pitch)+TEXT(",")+Number(V.Yaw)+TEXT(",")+Number(V.Roll)+TEXT("]"); }
FString Normalized(FString P) { P=FPaths::ConvertRelativePathToFull(P);FPaths::NormalizeFilename(P);FPaths::CollapseRelativeDirectories(P);return P; }
bool Disposable(const FString& P) { return !P.IsEmpty() && Normalized(P).Contains(TEXT("/build/shipping-world-qa-"),ESearchCase::IgnoreCase); }
bool Guard() { return FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation")) && Disposable(FPaths::ProjectSavedDir()) && Disposable(UFGSaveSystem::GetSaveDirectoryPath()); }
FString ManifestPath(bool Auto=false,bool Kit=false) { return FPaths::Combine(FPaths::ProjectSavedDir(),Kit?TEXT("FactoryFPV-QA/KitSave-Manifest.txt"):(Auto?TEXT("FactoryFPV-QA/Autosave-Manifest.txt"):TEXT("FactoryFPV-QA/ActiveSave-Manifest.txt"))); }
FString AutosavePlanPath() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FactoryFPV-QA/AutosavePlan.json")); }
FString AutosaveApprovalPath() { return FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FactoryFPV-QA/AutosaveApproval.txt")); }
FString FileSha1(const FString& Path) { TArray<uint8> Bytes;if (!FFileHelper::LoadFileToArray(Bytes,*Path) || Bytes.IsEmpty()) return FString();return FSHA1::HashBuffer(Bytes.GetData(),Bytes.Num()).ToString(); }
void Marker(const FString& Mode,const TCHAR* Result,const FString& Reason,const FString& Extra=FString(),bool Auto=false,bool Kit=false)
{
    const FString Scope=Auto?TEXT("one genuine native 20-second autosave timer after host-pinned exact-output approval; queue notification is not completion, success-only native collection event plus pending=false/file/header and separate reload qualify completion; excludes physical flight, unsupported return and item-state payload fidelity"):
        TEXT("one public native SaveGame while the same ordinary disarmed FPV session remains active, then separate-process reload of that exact output; inventory slot class/count/state-type and progression snapshots, excludes autosave scheduling, pending unsupported return, dynamic item-state payload fidelity and physical flight");
    const FString KitScope=TEXT("one genuinely paid native Workshop-UI radio remains natively equipped through an ordinary disarmed active-FPV save and separate-process reload; public Primary/hangar and fresh flight-return, no post-load grant/production/re-equip, excludes dynamic item-state payload fidelity and physical controls");
    const FString Line=FString::Printf(TEXT("FACTORYFPV_SHIPPING_QA {\"stage\":\"%s-%s\",\"result\":\"%s\",\"reason\":\"%s\",\"scope\":\"%s\"%s}"),Kit?TEXT("kit"):(Auto?TEXT("autosave"):TEXT("save")),*Escape(Mode),Result,*Escape(Reason),*Escape(Kit?KitScope:Scope),*Extra);
    if (GLog) GLog->Serialize(*Line,ELogVerbosity::Display,NAME_None);
}
struct FCapture
{
    FString Path;int32 Width=0,Height=0;bool bDecoded=false;double Variance=0;
    FString Json() const { return FString::Printf(TEXT("{\"path\":\"%s\",\"width\":%d,\"height\":%d,\"decoded\":%s,\"lumaVariance\":%s}"),*Escape(Path),Width,Height,B(bDecoded),*Number(Variance)); }
};
struct FInputMember { TWeakObjectPtr<UInputComponent> Input;bool bMember=false; };
struct FRun : public TSharedFromThis<FRun>
{
    enum class EPhase { Waiting,Before,CaptureBefore,Active,CaptureActive,AwaitAutosaveApproval,Saving,Completed,CaptureCompleted,Return,Reloaded,CaptureReloaded,HangarOpened,HangarClosed,FreshFlight,FreshReturn,CaptureFreshReturn };
    TWeakObjectPtr<UWorld> World;TWeakObjectPtr<UGameInstance> Instance;TWeakObjectPtr<ULocalPlayer> Player;TWeakObjectPtr<UGameViewportClient> Viewport;
    TWeakObjectPtr<AFGPlayerController> PC;TWeakObjectPtr<AFGCharacterPlayer> Pioneer;TWeakObjectPtr<UFactoryFPVLocalPlayerSubsystem> Hangar;
    TWeakObjectPtr<AFactoryFPVDrone> Drone;TWeakObjectPtr<AFactoryFPVPioneerAnchor> Anchor;TWeakObjectPtr<AActor> OriginalView;
    TWeakObjectPtr<APlayerCameraManager> Manager;TWeakObjectPtr<AFGHUD> HUD;TWeakObjectPtr<UFGSaveSession> Save;
    TArray<TWeakObjectPtr<AActor>> ModActors;TArray<TWeakObjectPtr<UInputComponent>> ModControls;TArray<TWeakObjectPtr<UWorldPartitionStreamingSourceComponent>> ModSources;
    TArray<FInputMember> OriginalInputs;TMap<FString,FString> Manifest,Snapshots;
    FObjectKey WorldKey,PCKey,PioneerKey,PlayerKey;
    FString Mode,SaveName,ResolvedClone,ResolvedOutput,LoadedHeaderSaveName,RequestedLoadName,InventoryBaseline,ProgressBaseline,InventoryCurrent,ProgressCurrent,LastReason=TEXT("Waiting for native clone/startup readiness."),PendingCapture,SaveError;
    FVector OriginalLocation=FVector::ZeroVector;FRotator OriginalRotation=FRotator::ZeroRotator;
    double OriginalFov=0,OriginalLockedFov=0,OriginalHealth=0,OriginalMaxHealth=0,Deadline=0,PhaseStarted=0,CaptureStarted=0,RequestTime=0,CallbackTime=0,MaximumPioneerDisplacement=0;
    uint64 PhaseFrame=0,LastPendingFrame=0;int32 OriginalMovement=0,OriginalCustomMode=0,CallbackCount=0,PendingActiveSamples=0,PostCallbackActiveSamples=0;
    bool bPartial=false,bHiddenHUD=false,bHudVisible=false,bHudShow=false,bCursor=false,bMove=false,bLook=false,bViewportIgnored=false,bMesh1Hidden=false,bMesh3Hidden=false,bMesh1Visible=false,bMesh3Visible=false;
    bool bDone=false,bPrepared=false,bCapture=false,bOwnedPanel=false,bOwnCapture=false,bCaptureProcessed=false,bScreenMessagesBefore=false,bSavedProvenanceVerified=false;
    bool bSplash=false,bAnimation=false,bStartup=false,bFrontEnd=false,bBaseline=false,bRequested=false,bPendingObserved=false,bCallback=false,bCallbackSuccess=false,bActiveAtRequest=false,bActiveAtCallback=false,bActiveThroughCompletion=false,bOutputVerified=false;
    bool bInventoryMatches=false,bProgressMatches=false,bReturned=false,bNoTransient=false,bHangarRebound=false,bFresh=false,bNoLeases=false,bCaptures=false,bHeldAtRequest=false,bHeldAtCallback=false,bRemainedDisarmed=false,bNativeRestoredReady=false;
    FCapture BeforeCapture,ActiveCapture,CompletedCapture,ReloadedCapture,FreshCapture;FDelegateHandle CaptureHandle;
    bool bAutosave=false,bAutoControlled=false,bAutoPlanApproved=false,bAutoArmed=false,bAutoTimerStopped=false,bAutoQueueActive=false,bAutoQueueHeld=false,bAutoSuccessActive=false,bAutoSuccessHeld=false,bAutoProvenance=false;
    FFactoryFPVShippingArmedSaveFlight Hover;
    bool bArmedHover=false,bArmedAtQueue=false,bArmedAtDiskSuccess=false;
    double HoverAltitudeAtQueue=0,HoverAltitudeAtDiskSuccess=0;
    bool bOverwriteSlot=false,bOccupiedSlotVerified=false,bOverwriteVerified=false,bOverwriteLoadedPostimageVerified=false;
    FString OverwritePreimageSha1,OverwritePostimageSha1;
    bool bOriginalAutoEnabled=false;float OriginalAutoInterval=0;
    int32 AutoLastId=0,AutoRotations=0,AutoNextId=0,AutoQueueCount=0,AutoSuccessCount=0,AutoCountdownCount=0,AutoAfterSuccessSamples=0,AutoWaitingActiveSamples=0;uint64 AutoLastWaitingFrame=0;
    double AutoArmWorld=0,AutoQueueElapsed=-1,AutoSuccessElapsed=-1;FString AutoSession,AutoNonce,AutoPlanHash,AutoPlanText;
    TArray<FString> AutoTrace;TStrongObjectPtr<UFactoryFPVShippingAutosaveObserver> AutoObserver;TWeakObjectPtr<AFGGameState> AutoGameState;FDelegateHandle AutoCollectionHandle;
    EPhase Phase=EPhase::Waiting;
    bool bKitPersistence=false,bKitPreparationStarted=false,bKitPreparationCompleted=false,bKitPreparationSucceeded=false,bKitProvenance=false,bKitPrimaryOpened=false;
    bool bKitPolicySnapshot=false,bKitPreviousSandbox=false,bKitPolicyRestored=false;
    int32 KitPrepareCalls=0,KitPrimaryDispatches=0,KitIndex=INDEX_NONE;
    FString KitPreparationEvidence,KitBaseline,KitCurrent,KitPresentationBaseline,KitPresentationCurrent;
    TWeakObjectPtr<AFactoryFPVRadioEquipment> KitActor;bool bKitActorBound=false;
    FSimpleDelegate CancelKitPreparation;

    bool IsArmedHoverRun() const { return bArmedHover && Mode==TEXT("save"); }
    bool HoverProvenAtBoundary() const
    {
        return Drone.IsValid() && Drone->IsFlightArmed() && Hover.IsPoweredHovering() && Hover.StableSeconds()>=19.;
    }

    AFactoryFPVRadioEquipment* KitRadio() const {
        auto* P=Pioneer.Get();auto* Arms=P?P->GetEquipmentSlot(EEquipmentSlot::ES_ARMS):nullptr;
        auto* Radio=Arms?Cast<AFactoryFPVRadioEquipment>(Arms->GetStackEquipmentActorAtIdx(Arms->GetActiveIndex())):nullptr;
        FInventoryStack Stack;
        return P && Arms && Arms->GetActiveIndex()==KitIndex && Arms->GetStackFromIndex(KitIndex,Stack) && Stack.NumItems==1 &&
            Stack.Item.GetItemClass()==UFactoryFPVRadioDescriptor::StaticClass() && Radio &&
            P->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS)==Radio && Arms->GetEquipmentInSlot()==Radio && Radio->IsEquipped() &&
            Radio->GetInstigatorCharacter()==P && Radio->IsLocalInstigator() && (!bKitActorBound || KitActor.Get()==Radio) &&
            AFactoryFPVRadioEquipment::IsEquippedBy(PC.Get()) ? Radio:nullptr;
    }
    bool KitState(FString& Out,FString& Presentation) const {
        auto* Radio=KitRadio();auto* P=Pioneer.Get();auto* Arms=P?P->GetEquipmentSlot(EEquipmentSlot::ES_ARMS):nullptr;
        if (!Radio || !Arms || !P->GetInventory()) return false;
        FInventoryStack Stack;if (!Arms->GetStackFromIndex(KitIndex,Stack)) return false;
        const auto* Type=Stack.Item.HasState()?Stack.Item.GetItemState().GetStruct():nullptr;
        Out=FString::Printf(TEXT("{\"activeIndex\":%d,\"descriptor\":\"%s\",\"slotCount\":%d,\"armsKitCount\":%d,\"mainKitCount\":%d,\"equipmentClass\":\"%s\",\"stateType\":\"%s\",\"slotActorEqualsEquippedActor\":true,\"slotActorEqualsNativeSlotActor\":true,\"isEquipped\":true,\"instigatorIsPioneer\":true,\"localInstigator\":true}"),KitIndex,*Escape(UFactoryFPVRadioDescriptor::StaticClass()->GetPathName()),Stack.NumItems,Arms->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass()),P->GetInventory()->GetNumItems(UFactoryFPVRadioDescriptor::StaticClass()),*Escape(Class(Radio)),Type?*Escape(Type->GetPathName()):TEXT(""));
        TArray<FString> Parts;TInlineComponentArray<UPrimitiveComponent*> Components(Radio);
        for (auto* C:Components) Parts.Add(FString::Printf(TEXT("{\"name\":\"%s\",\"class\":\"%s\",\"hidden\":%s,\"visible\":%s}"),*Escape(C->GetName()),*Escape(Class(C)),B(C->bHiddenInGame),B(C->IsVisible())));
        Parts.Sort();Presentation=FString::Printf(TEXT("{\"cameraMode\":%d,\"actorHidden\":%s,\"components\":[%s]}"),static_cast<int32>(P->GetCameraMode()),B(Radio->IsHidden()),*FString::Join(Parts,TEXT(",")));return true;
    }
    bool KitMatches(bool Presentation=false) {
        return !bKitPersistence || (KitState(KitCurrent,KitPresentationCurrent) && KitCurrent==KitBaseline && (!Presentation || KitPresentationCurrent==KitPresentationBaseline));
    }
    FString KitObservation() const {
        auto* P=Pioneer.Get();auto* Arms=P?P->GetEquipmentSlot(EEquipmentSlot::ES_ARMS):nullptr;
        const int32 Active=Arms?Arms->GetActiveIndex():INDEX_NONE;
        auto* Slotted=Arms && Active>=0?Arms->GetStackEquipmentActorAtIdx(Active):nullptr;
        auto* Equipped=P?P->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS):nullptr;
        auto* NativeSlot=Arms?Arms->GetEquipmentInSlot():nullptr;
        FInventoryStack Stack;const bool HasStack=Arms && Active>=0 && Arms->GetStackFromIndex(Active,Stack);
        return FString::Printf(TEXT("{\"expectedIndex\":%d,\"activeIndex\":%d,\"descriptor\":\"%s\",\"slotCount\":%d,\"slottedActorClass\":\"%s\",\"pioneerEquippedClass\":\"%s\",\"nativeSlotActorClass\":\"%s\",\"slottedEqualsPioneer\":%s,\"slottedEqualsNativeSlot\":%s,\"isEquipped\":%s,\"localInstigator\":%s,\"instigatorIsPioneer\":%s,\"sameProcessActorRetained\":%s}"),KitIndex,Active,HasStack && Stack.Item.GetItemClass()?*Escape(Stack.Item.GetItemClass()->GetPathName()):TEXT(""),HasStack?Stack.NumItems:0,*Escape(Class(Slotted)),*Escape(Class(Equipped)),*Escape(Class(NativeSlot)),B(Slotted && Slotted==Equipped),B(Slotted && Slotted==NativeSlot),B(Slotted && Slotted->IsEquipped()),B(Slotted && Slotted->IsLocalInstigator()),B(Slotted && Slotted->GetInstigatorCharacter()==P),B(bKitActorBound && Slotted && KitActor.Get()==Slotted));
    }
    void KitPolicy() {
        if (!bKitPersistence || bKitPolicySnapshot) return;
        auto* Settings=GetMutableDefault<UFactoryFPVSettings>();bKitPreviousSandbox=Settings->bAllowSandboxAccess;bKitPolicySnapshot=true;Settings->bAllowSandboxAccess=false;
    }
    void RestoreKitPolicy() {
        if (!bKitPolicySnapshot) return;auto* S=GetMutableDefault<UFactoryFPVSettings>();S->bAllowSandboxAccess=bKitPreviousSandbox;bKitPolicyRestored=S->bAllowSandboxAccess==bKitPreviousSandbox;bKitPolicySnapshot=false;
    }
    void StartKitPreparation() {
        if (!bKitPersistence || Mode!=TEXT("save") || bKitPreparationStarted) return;
        bKitPreparationStarted=true;++KitPrepareCalls;const TWeakPtr<FRun> Weak=AsShared();
        if (!FFactoryFPVShippingKitQA::PrepareEquippedKit(World.Get(),[Weak](bool Success,const FString& Reason,const FString& Evidence) {
            auto R=Weak.Pin();if (!R || R->bDone) return;
            R->bKitPreparationCompleted=true;R->bKitPreparationSucceeded=Success;R->KitPreparationEvidence=Evidence;R->CancelKitPreparation.Unbind();
            if (!Success) { R->Finish(false,TEXT("Native paid-kit preparation failed: ")+Reason);return; }
            R->KitPolicy();R->LastReason=TEXT("Paid native-equipped kit handed over; waiting for the concurrently observed native readiness before post-payment baseline.");
            Marker(R->Mode,TEXT("PENDING"),R->LastReason,TEXT(",\"kitPersistenceEnabled\":true,\"liveKitPreparationEvidence\":")+Evidence,false,true);
        },CancelKitPreparation)) Finish(false,TEXT("Cancellable paid-kit preparation refused its strict fresh-clone/Workshop-UI guard."));
    }

    void PhaseTo(EPhase Next,double Now) { Phase=Next;PhaseStarted=Now;PhaseFrame=GFrameCounter; }
    bool Settled(double Now,double Seconds=.5) const { return Now-PhaseStarted>=Seconds && GFrameCounter>=PhaseFrame+3; }
    bool NoLeases() const { const auto D=FactoryFPVInputLease::GetDiagnostics(PC.Get());return D.ActiveLeases==0 && D.OwnedMoveContributions==0 && D.OwnedLookContributions==0 && D.ResetDepth==0; }
    bool Grounded() const { auto* M=Pioneer.IsValid()?Pioneer->GetCharacterMovement():nullptr;if (!M || !M->IsMovingOnGround()) return false;FFindFloorResult F;M->FindFloor(Pioneer->GetActorLocation(),F,false);return F.IsWalkableFloor(); }
    bool ClearNativeUI() const
    {
        auto* UI=PC.IsValid()?PC->GetGameUI():nullptr;const auto* C=Viewport.IsValid()?Viewport->ViewportConsole.Get():nullptr;
        return World.IsValid() && PC.IsValid() && UI && !World->IsPaused() && !PC->IsPaused() && !PC->IsPauseMenuOpen() && !UI->IsPauseMenuOpen() && !UI->HasActiveInteractWidget() && (!C || C->ConsoleState==NAME_None);
    }
    int32 F6(FInputActionUnifiedDelegate* Out=nullptr) const
    {
        if (!PC.IsValid() || !Hangar.IsValid()) return 0;int32 Count=0;
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It))
            for (const auto& K:It->KeyBindings) if (K.Chord.Key==EKeys::F6 && K.KeyEvent==IE_Pressed && K.KeyDelegate.IsBoundToObject(Hangar.Get())) { ++Count;if (Out) *Out=K.KeyDelegate; }
        return Count;
    }
    bool InvokeF6() { FInputActionUnifiedDelegate D;if (F6(&D)!=1) return false;D.Execute(EKeys::F6);return true; }
    bool NoWidgets() const
    {
        const auto Window=Viewport.IsValid()?Viewport->GetWindow():nullptr;if (!Window) return false;
        TArray<TSharedRef<SWidget>> Pending;Pending.Add(Window.ToSharedRef());TSet<const SWidget*> Seen;
        while (!Pending.IsEmpty() && Seen.Num()<32768) { auto W=Pending.Pop(EAllowShrinking::No);if (Seen.Contains(&W.Get())) continue;Seen.Add(&W.Get());
            if (W->GetType()==FName(TEXT("SFactoryFPVOSD")) || W->GetType()==FName(TEXT("SFactoryFPVPanel")) || W->GetType()==FName(TEXT("SFactoryFPVRacePanel"))) return false;
            if (auto* Children=W->GetChildren()) for (int32 I=0;I<Children->Num();++I) Pending.Add(Children->GetChildAt(I)); }
        return Pending.IsEmpty();
    }
    bool NoTransientResources() const
    {
        for (TActorIterator<AActor> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed() && (Cast<AFactoryFPVDrone>(*It) || Cast<AFactoryFPVPioneerAnchor>(*It) || Cast<AFactoryFPVPilotVisual>(*It))) return false;
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && (Cast<AFactoryFPVDrone>(It->GetOwner()) || Cast<AFactoryFPVPioneerAnchor>(It->GetOwner()))) {
            // Retired disabled components can await GC after normal Destroy.
            // Reload must still have no living owner or enabled FPV source.
            if (It->IsStreamingSourceEnabled() || !It->GetOwner()->IsActorBeingDestroyed()) return false;
        }
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get() && PC->IsInputComponentInStack(*It) && Cast<AFactoryFPVDrone>(It->GetOwner())) return false;
        return NoLeases() && NoWidgets() && Hangar.IsValid() && !Hangar->IsPanelOpen();
    }
    bool Inventory(FString& Out) const
    {
        if (!Pioneer.IsValid() || !Pioneer->GetInventory() || Pioneer->GetInventory()->GetSizeLinear()<=0) return false;TArray<FString> Parts;int64 MainItems=0;
        auto One=[&](UFGInventoryComponent* Inv,const FString& Name,int32 Active,const FString& Equipped) {
            if (!Inv) return false;const int32 Size=Inv->GetSizeLinear();if (Size<0 || Size>4096) return false;TArray<FString> Rows;
            for (int32 I=0;I<Size;++I) { FInventoryStack S;if (!static_cast<const UFGInventoryComponent*>(Inv)->GetStackFromIndex(I,S) || S.NumItems<0) return false;
                const auto Type=S.Item.GetItemClass();if (S.NumItems>0 && !Type) return false;const auto* State=S.Item.HasState()?S.Item.GetItemState().GetStruct():nullptr;if (Name==TEXT("main")) MainItems+=S.NumItems;
                Rows.Add(FString::Printf(TEXT("{\"slot\":%d,\"class\":\"%s\",\"count\":%d,\"stateType\":\"%s\"}"),I,Type?*Escape(Type->GetPathName()):TEXT(""),S.NumItems,State?*Escape(State->GetPathName()):TEXT(""))); }
            Parts.Add(FString::Printf(TEXT("{\"inventory\":\"%s\",\"capacity\":%d,\"activeIndex\":%d,\"equippedClass\":\"%s\",\"slots\":[%s]}"),*Escape(Name),Size,Active,*Escape(Equipped),*FString::Join(Rows,TEXT(","))));return true;
        };
        if (!One(Pioneer->GetInventory(),TEXT("main"),INDEX_NONE,FString())) return false;
        for (int32 I=static_cast<int32>(EEquipmentSlot::ES_ARMS);I<static_cast<int32>(EEquipmentSlot::ES_MAX);++I) { const auto Slot=static_cast<EEquipmentSlot>(I);auto* Inv=Pioneer->GetEquipmentSlot(Slot);
            if (!One(Inv,FString::Printf(TEXT("equipment-%d"),I),Inv?Inv->GetActiveIndex():INDEX_NONE,Class(Pioneer->GetEquipmentInSlot(Slot)))) return false; }
        Out=TEXT("[")+FString::Join(Parts,TEXT(","))+TEXT("]");return MainItems>0;
    }
    bool Progress(FString& Out) const
    {
        auto* S=AFGSchematicManager::Get(World.Get());auto* G=AFGGamePhaseManager::Get(World.Get());if (!S || !G) return false;
        TArray<TSubclassOf<UFGSchematic>> Purchased;S->GetAllPurchasedSchematics(Purchased);TArray<FString> Paths;
        for (const auto& C:Purchased) { if (!C) return false;Paths.AddUnique(C->GetPathName()); }if (Paths.IsEmpty()) return false;Paths.Sort();TArray<FString> Quoted;for (const auto& P:Paths) Quoted.Add(TEXT("\"")+Escape(P)+TEXT("\""));
        const auto Active=S->GetActiveSchematic();TArray<FString> Paid;if (Active) for (const auto& A:S->GetPaidOffCostFor(Active)) { if (!A.ItemClass || A.Amount<0) return false;Paid.Add(FString::Printf(TEXT("{\"class\":\"%s\",\"amount\":%d}"),*Escape(A.ItemClass->GetPathName()),A.Amount)); }Paid.Sort();
        TArray<FRemainingPhaseCost> Remaining;G->GetRemainingPhaseCosts(Remaining);TArray<FString> Costs;
        for (const auto& C:Remaining) { if (!C.ItemClass || C.TotalCost<0 || C.RemainingCost<0) return false;Costs.Add(FString::Printf(TEXT("{\"class\":\"%s\",\"total\":%d,\"remaining\":%d}"),*Escape(C.ItemClass->GetPathName()),C.TotalCost,C.RemainingCost)); }Costs.Sort();
        Out=FString::Printf(TEXT("{\"purchasedSchematics\":[%s],\"activeSchematic\":\"%s\",\"activePaidCost\":[%s],\"currentPhase\":\"%s\",\"targetPhase\":\"%s\",\"remainingPhaseCost\":[%s]}"),*FString::Join(Quoted,TEXT(",")),Active?*Escape(Active->GetPathName()):TEXT(""),*FString::Join(Paid,TEXT(",")),*Escape(GetPathNameSafe(G->GetCurrentGamePhase())),*Escape(GetPathNameSafe(G->GetTargetGamePhase())),*FString::Join(Costs,TEXT(",")));return true;
    }
    bool ContentMatches() { bInventoryMatches=Inventory(InventoryCurrent) && InventoryCurrent==InventoryBaseline;bProgressMatches=Progress(ProgressCurrent) && ProgressCurrent==ProgressBaseline;return bInventoryMatches && bProgressMatches && KitMatches(); }
    bool HealthMatches() const { const auto* H=Pioneer.IsValid()?Pioneer->GetHealthComponent():nullptr;return H && FMath::IsFinite(H->GetCurrentHealth()) && FMath::IsFinite(H->GetMaxHealth()) && FMath::IsNearlyEqual(double(H->GetCurrentHealth()),OriginalHealth,.01) && FMath::IsNearlyEqual(double(H->GetMaxHealth()),OriginalMaxHealth,.01); }
    bool AutoSchema(int32& Last,int32& Count,FString& Session) const
    {
        auto* GM=World.IsValid()?World->GetAuthGameMode<AFGGameMode>():nullptr;auto* S=Save.Get();if (!GM || !S) return false;
        const auto* Id=FindFProperty<FByteProperty>(GM->GetClass(),TEXT("mLastAutoSaveId"));const auto* Rotation=FindFProperty<FIntProperty>(S->GetClass(),TEXT("mNumRotatingAutosaves"));
        if (!Id || !Rotation) return false;Last=Id->GetPropertyValue_InContainer(GM);Count=Rotation->GetPropertyValue_InContainer(S);Session=GM->GetSaveSessionName();
        if (Count<1 || Count>255 || S->GetNumRotatingAutosaves()!=Count || Session.IsEmpty() || Session.Len()>128 || Session!=S->GetLoadedSaveHeader().SessionName) return false;
        for (const TCHAR C:Session) if (C<32 || FString(TEXT("<>:\"/\\|?*,=")).Contains(FString::Chr(C))) return false;
        return true;
    }
    bool ControlAutoSession(UFGSaveSession* S)
    {
        if (!bAutosave || bAutoControlled) return true;
        const auto* Interval=FindFProperty<FFloatProperty>(S->GetClass(),TEXT("mAutosaveInterval"));const auto* Enabled=FindFProperty<FBoolProperty>(S->GetClass(),TEXT("mAutosaveEnabled"));
        if (!Interval || !Enabled || !Interval->HasAnyPropertyFlags(CPF_Transient) || !Enabled->HasAnyPropertyFlags(CPF_Transient)) { Finish(false,TEXT("Known native transient autosave control schema unavailable."));return false; }
        OriginalAutoInterval=Interval->GetPropertyValue_InContainer(S);bOriginalAutoEnabled=Enabled->GetPropertyValue_InContainer(S);
        if (!FMath::IsFinite(OriginalAutoInterval) || S->HasSaveGameOperationPending()) { Finish(false,TEXT("Autosave preflight found invalid interval or pre-existing save."));return false; }
        Save=S;bAutoControlled=true;S->SetAutoSaveEnabled(false);S->SetupAutosave();
        if (S->ShouldPerformAutoSave()) { Finish(false,TEXT("Native transient autosave disable did not take effect."));return false; }return true;
    }
    void AutoRecord(const TCHAR* Event)
    {
        if (AutoTrace.Num()>=64) return;const auto* Enabled=Save.IsValid()?FindFProperty<FBoolProperty>(Save->GetClass(),TEXT("mAutosaveEnabled")):nullptr;
        AutoTrace.Add(FString::Printf(TEXT("{\"event\":\"%s\",\"worldSeconds\":%s,\"elapsedWorldSeconds\":%s,\"savePending\":%s,\"active\":%s,\"held\":%s,\"timerEnabled\":%s,\"intervalSeconds\":%d}"),Event,*Number(World.IsValid()?World->GetTimeSeconds():0),*Number(bAutoArmed && World.IsValid()?World->GetTimeSeconds()-AutoArmWorld:0),B(Save.IsValid() && Save->HasSaveGameOperationPending()),B(bPrepared && Active()),B(Anchor.IsValid() && Anchor->IsHolding()),B(Enabled && Enabled->GetPropertyValue_InContainer(Save.Get())),Save.IsValid()?Save->GetAutosaveInterval():0));
    }
    void StopAutoTimer()
    {
        if (!bAutosave || !bAutoControlled || !Save.IsValid() || Save->GetWorld()!=World.Get()) return;
        const bool Pending=Save->HasSaveGameOperationPending();Save->SetAutoSaveEnabled(false);Save->SetupAutosave();
        const auto* Enabled=FindFProperty<FBoolProperty>(Save->GetClass(),TEXT("mAutosaveEnabled"));
        bAutoTimerStopped=Enabled && !Enabled->GetPropertyValue_InContainer(Save.Get()) && !Save->ShouldPerformAutoSave();
        if (!bDone && (Pending!=Save->HasSaveGameOperationPending() || !bAutoTimerStopped)) Finish(false,TEXT("Native timer disable unexpectedly changed pending save or failed to disable future autosaves."));
    }
    bool MakeAutoPlan()
    {
        if (!AutoSchema(AutoLastId,AutoRotations,AutoSession)) { LastReason=TEXT("Exact native session/rotation/last-ID schema is unavailable or unsafe.");return false; }
        // Verified CL513885 native code increments uint8 before modulo and
        // formats this zero-based suffix. Never invoke either mutating generator.
        AutoNextId=static_cast<uint8>(AutoLastId+1)%AutoRotations;SaveName=FString::Printf(TEXT("%s_autosave_%d"),*AutoSession,AutoNextId);
        if (!UFGSaveSystem::IsValidSaveName(SaveName) || UFGSaveSystem::SanitizeSaveName(SaveName)!=SaveName || !Paths(false)) return false;
        const FString Path=Normalized(AutosavePlanPath()),Temp=FPaths::Combine(FPaths::GetPath(Path),TEXT("AutosavePlan.pending"));
        if (!Disposable(Path) || !FPaths::IsUnderDirectory(Path,Normalized(FPaths::ProjectSavedDir())) || IFileManager::Get().FileExists(*Path) || IFileManager::Get().FileExists(*Temp) || IFileManager::Get().FileExists(*AutosaveApprovalPath()) || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true)) { LastReason=TEXT("Fresh bounded autosave plan/approval paths are required.");return false; }
        AutoNonce=FGuid::NewGuid().ToString(EGuidFormats::Digits);
        AutoPlanText=FString::Printf(TEXT("{\"schemaVersion\":1,\"planNonce\":\"%s\",\"sessionName\":\"%s\",\"lastAutosaveId\":%d,\"rotatingAutosaves\":%d,\"predictedAutosaveId\":%d,\"expectedSaveName\":\"%s\",\"expectedOutputPath\":\"%s\",\"clonePath\":\"%s\",\"intervalSeconds\":20%s%s}"),*AutoNonce,*Escape(AutoSession),AutoLastId,AutoRotations,AutoNextId,*Escape(SaveName),*Escape(ResolvedOutput),*Escape(ResolvedClone),bOverwriteSlot?TEXT(",\"overwriteSlot\":true"):TEXT(""),bArmedHover?TEXT(",\"armedHover\":true"):TEXT(""));
        if (!FFileHelper::SaveStringToFile(AutoPlanText,*Temp,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) || !IFileManager::Get().Move(*Path,*Temp,false,false,false,true)) { LastReason=TEXT("Could not atomically publish the autosave plan.");return false; }
        AutoPlanHash=FileSha1(Path);if (AutoPlanHash.Len()!=40) return false;
        Marker(Mode,TEXT("PENDING"),TEXT("Exact native autosave plan published; timer remains disabled pending host SHA1 approval."),TEXT(",\"autosavePlanPath\":\"")+Escape(Path)+TEXT("\",\"autosavePlanSha1\":\"")+AutoPlanHash+TEXT("\""),true);return true;
    }
    bool AutoApproval()
    {
        if (!IFileManager::Get().FileExists(*AutosaveApprovalPath())) { LastReason=TEXT("Waiting for host approval of the immutable exact native autosave output plan.");return false; }
        FString Digest;if (!FFileHelper::LoadFileToString(Digest,*AutosaveApprovalPath())) { Finish(false,TEXT("Autosave approval could not be read."));return false; }Digest.TrimStartAndEndInline();
        int32 Last=0,Count=0;FString Session;
        if (Digest.Len()!=40 || !Digest.Equals(AutoPlanHash,ESearchCase::IgnoreCase) || !FileSha1(AutosavePlanPath()).Equals(AutoPlanHash,ESearchCase::IgnoreCase) || !AutoSchema(Last,Count,Session) || Last!=AutoLastId || Count!=AutoRotations || Session!=AutoSession || !Paths(bOverwriteSlot)) { Finish(false,TEXT("Autosave approval/hash/native naming state or approved exact output changed before scheduling: ")+LastReason);return false; }
        if (bOverwriteSlot) {
            // The host seeds this one approved slot with the exact disposable
            // input clone. Refuse any other preimage, even if it is loadable.
            OverwritePreimageSha1=FileSha1(ResolvedOutput);
            const auto* System=UFGSaveSystem::Get(World.Get());FSaveHeader Header;
            bOccupiedSlotVerified=OverwritePreimageSha1.Len()==40 && OverwritePreimageSha1.Equals(FileSha1(ResolvedClone),ESearchCase::IgnoreCase) &&
                System && System->LoadSaveGameHeaderSync(SaveName,Header) && Header.SaveName==SaveName &&
                Header.SessionName==AutoSession && Header.MapName==Save->GetLoadedSaveHeader().MapName && !Header.MapName.IsEmpty();
            if (!bOccupiedSlotVerified) { Finish(false,TEXT("Approved occupied autosave slot is not the exact disposable clone with a readable native session header."));return false; }
        }
        bAutoPlanApproved=true;return true;
    }
    void MaybeAutoCompleted()
    {
        if (bDone || !bAutoArmed || AutoQueueCount!=1 || AutoSuccessCount!=1) return;
        if (!bAutoQueueActive || !bAutoQueueHeld || !bAutoSuccessActive || !bAutoSuccessHeld || !bAutoTimerStopped || !Active()) { Finish(false,TEXT("Real autosave queue/success boundary lost its original live FPV session or pioneer hold."));return; }
        PhaseTo(EPhase::Completed,FPlatformTime::Seconds());
    }
    void ObserveAutoQueued()
    {
        if (bDone || !bAutoArmed) return;++AutoQueueCount;AutoQueueElapsed=World.IsValid()?World->GetTimeSeconds()-AutoArmWorld:-1;bAutoQueueActive=Active();bAutoQueueHeld=Anchor.IsValid() && Anchor->IsHolding();
        if (IsArmedHoverRun()) {
            bArmedAtQueue=HoverProvenAtBoundary();
            HoverAltitudeAtQueue=Hover.AltitudeCm();
            if (!bArmedAtQueue) { Finish(false,TEXT("Native autosave queued without 19 seconds of continuous powered hover."));return; }
        }
        if (Save.IsValid() && Save->HasSaveGameOperationPending()) { bPendingObserved=true;if (LastPendingFrame!=GFrameCounter) { LastPendingFrame=GFrameCounter;++PendingActiveSamples; } }
        AutoRecord(TEXT("nativeQueuedNotification"));Record(TEXT("autosaveQueued"));
        int32 Last=0,Count=0;FString Session;
        if (AutoQueueCount!=1 || AutoQueueElapsed<19.5 || !bAutoQueueActive || !bAutoQueueHeld || !AutoSchema(Last,Count,Session) || Last!=AutoNextId || Count!=AutoRotations || Session!=AutoSession) { Finish(false,TEXT("Native autosave notification arrived without the approved timer delay/ID/live session boundary."));return; }
        // Native Autosave sends this notification immediately after queueing
        // SaveGame, BEFORE disk completion, and next calls SetupAutosave. Disable
        // rescheduling through public controls without touching the pending save.
        StopAutoTimer();MaybeAutoCompleted();
    }
    void ObserveAutoSuccess()
    {
        if (bDone || !bAutoArmed) return;++AutoSuccessCount;AutoSuccessElapsed=World.IsValid()?World->GetTimeSeconds()-AutoArmWorld:-1;bAutoSuccessActive=Active();bAutoSuccessHeld=Anchor.IsValid() && Anchor->IsHolding();
        if (IsArmedHoverRun()) {
            bArmedAtDiskSuccess=HoverProvenAtBoundary();
            HoverAltitudeAtDiskSuccess=Hover.AltitudeCm();
            if (!bArmedAtDiskSuccess) { Finish(false,TEXT("Native autosave finished writing after continuous powered hover was lost."));return; }
        }
        AutoRecord(TEXT("nativeSaveCollectionSuccess"));Record(TEXT("autosaveDiskSuccess"));
        if (AutoSuccessCount!=1 || !bAutoSuccessActive || !bAutoSuccessHeld || !Save.IsValid() || Save->HasSaveGameOperationPending()) { Finish(false,TEXT("Native success-only save collection event lacked its actual completed/live boundary."));return; }
        MaybeAutoCompleted();
    }
    void ObserveAutoCountdown(float Seconds)
    {
        if (bDone || !bAutoArmed) return;
        if (!FMath::IsFinite(Seconds) || Seconds<0) { Finish(false,TEXT("Native autosave countdown was invalid."));return; }
        if (bOverwriteSlot && AutoQueueCount==0 && !OverwritePreimageSha1.Equals(FileSha1(ResolvedOutput),ESearchCase::IgnoreCase)) {
            Finish(false,TEXT("Approved occupied autosave slot changed before the native save queued."));return;
        }
        ++AutoCountdownCount;AutoRecord(TEXT("nativeCountdown"));
    }
    void ArmAutoTimer(double Now)
    {
        if (!Active() || !ContentMatches() || Save->HasSaveGameOperationPending() || (IsArmedHoverRun() && !Hover.IsPoweredHovering())) { Finish(false,TEXT("Native autosave scheduling boundary changed."));return; }
        auto* GS=World->GetGameState<AFGGameState>();if (!GS) { Finish(false,TEXT("Real native GameState autosave notification source unavailable."));return; }
        AutoGameState=GS;AutoObserver.Reset(NewObject<UFactoryFPVShippingAutosaveObserver>(GetTransientPackage(),NAME_None,RF_Transient));const TWeakPtr<FRun> Weak=AsShared();
        AutoObserver->Queued=[Weak] { if (auto Run=Weak.Pin()) Run->ObserveAutoQueued(); };
        AutoObserver->Countdown=[Weak](float Seconds) { if (auto Run=Weak.Pin()) Run->ObserveAutoCountdown(Seconds); };
        GS->mOnAutoSaveTimeNotification.AddDynamic(AutoObserver.Get(),&UFactoryFPVShippingAutosaveObserver::OnCountdown);GS->mOnAutoSaveFinished.AddDynamic(AutoObserver.Get(),&UFactoryFPVShippingAutosaveObserver::OnQueuedNotification);
        AutoCollectionHandle=UFGSaveSystem::OnSaveCollectionChanged.AddLambda([Weak] { if (auto Run=Weak.Pin()) Run->ObserveAutoSuccess(); });
        Save->SetAutosaveInterval(20);if (Save->GetAutosaveInterval()!=20) { Finish(false,TEXT("Native transient autosave interval did not accept 20 seconds."));return; }
        AutoArmWorld=World->GetTimeSeconds();RequestTime=Now;bAutoArmed=true;PhaseTo(EPhase::Saving,Now);Save->SetAutoSaveEnabled(true);
        if (!Save->ShouldPerformAutoSave()) { Finish(false,TEXT("Native autosave eligibility refused; NoAutoSave or another normal native condition prevents scheduling."));return; }
        AutoRecord(TEXT("nativeTimerArmed"));Record(TEXT("autosaveArmed"));LastReason=TEXT("Waiting for the genuine native 20 second autosave timer; no manual Autosave/SaveGame/tick invocation.");
    }
    FString Snapshot() const
    {
        auto* C=PC.Get();auto* P=Pioneer.Get();auto* M=P?P->GetCharacterMovement():nullptr;auto* H=C?Cast<AFGHUD>(C->GetHUD()):nullptr;auto* Cam=C?C->PlayerCameraManager.Get():nullptr;const auto L=FactoryFPVInputLease::GetDiagnostics(C);
        auto* Health=P?P->GetHealthComponent():nullptr;
        FString Out=FString::Printf(TEXT("{\"pioneerClass\":\"%s\",\"alive\":%s,\"location\":%s,\"rotation\":%s,\"velocity\":%s,\"controlRotation\":%s,\"lastSafeLoadLocation\":%s,\"lastSafePlayerPosition\":%s,\"health\":%s,\"movementMode\":%d,\"customMovementMode\":%d,\"viewTargetClass\":\"%s\",\"fov\":%s,\"lockedFov\":%s,\"hudPartial\":%s,\"hiddenHudMode\":%s,\"hudVisible\":%s,\"hudShow\":%s,\"cursor\":%s,\"viewportIgnoresInput\":%s,\"moveIgnored\":%s,\"lookIgnored\":%s,\"activeLeases\":%d,\"ownedMove\":%d,\"ownedLook\":%d,\"savePending\":%s,\"f6Bindings\":%d"),
            *Escape(Class(P)),B(P && P->IsAliveAndWell()),*Vec(P?P->GetActorLocation():FVector::ZeroVector),*Rot(P?P->GetActorRotation():FRotator::ZeroRotator),*Vec(M?M->Velocity:FVector::ZeroVector),*Rot(C?C->GetControlRotation():FRotator::ZeroRotator),*Vec(P?P->GetLastSafeLoadLocation():FVector::ZeroVector),*Vec(P?P->GetLastSafePlayerPosition():FVector::ZeroVector),Health?*Number(Health->GetCurrentHealth()):TEXT("null"),M?static_cast<int32>(M->MovementMode.GetValue()):INDEX_NONE,M?M->CustomMovementMode:0,*Escape(Class(C?C->GetViewTarget():nullptr)),Cam?*Number(Cam->GetFOVAngle()):TEXT("null"),Cam?*Number(Cam->GetLockedFOV()):TEXT("null"),B(H && H->GetPartialPumpiMode()),B(H && H->GetHiddenHUDMode()),B(H && H->GetHUDVisibility()),B(H && H->bShowHUD),B(C && C->bShowMouseCursor),B(Viewport.IsValid() && Viewport->IgnoreInput()),B(C && C->IsMoveInputIgnored()),B(C && C->IsLookInputIgnored()),L.ActiveLeases,L.OwnedMoveContributions,L.OwnedLookContributions,B(Save.IsValid() && Save->HasSaveGameOperationPending()),F6());
        int32 EnabledSources=0;for (const auto& S:ModSources) if (S.IsValid() && S->IsStreamingSourceEnabled()) ++EnabledSources;
        Out+=FString::Printf(TEXT(",\"maxHealth\":%s,\"activeDrone\":%s,\"disarmed\":%s,\"pioneerHeld\":%s,\"enabledSessionSources\":%d"),Health?*Number(Health->GetMaxHealth()):TEXT("null"),B(Drone.IsValid() && AFactoryFPVDrone::FindActive(World.Get())==Drone.Get()),B(Drone.IsValid() && !Drone->IsFlightArmed()),B(Anchor.IsValid() && Anchor->IsHolding()),EnabledSources);
        if (bKitPersistence) { FString State,Presentation;const bool Live=KitState(State,Presentation);Out+=TEXT(",\"kitNativeEquippedVerified\":")+FString(B(Live))+TEXT(",\"kitState\":")+(Live?State:FString(TEXT("null")))+TEXT(",\"kitPresentation\":")+(Live?Presentation:FString(TEXT("null")))+TEXT(",\"kitObservation\":")+KitObservation()+TEXT(",\"sandboxAccessEnabled\":")+B(GetDefault<UFactoryFPVSettings>()->bAllowSandboxAccess); }
        return Out+TEXT("}");
    }
    void Record(const TCHAR* Label) { Snapshots.Add(Label,Snapshot()); }
    bool Identity() const
    {
        return World.IsValid() && !World->bIsTearingDown && World->GetNetMode()==NM_Standalone && FObjectKey(World.Get())==WorldKey && PC.IsValid() && FObjectKey(PC.Get())==PCKey && World->GetFirstPlayerController()==PC.Get() && Pioneer.IsValid() && !Pioneer->IsActorBeingDestroyed() && FObjectKey(Pioneer.Get())==PioneerKey && PC->GetPawn()==Pioneer.Get() && Pioneer->GetController()==PC.Get() && Pioneer->IsAliveAndWell() && Player.IsValid() && FObjectKey(Player.Get())==PlayerKey && PC->GetLocalPlayer()==Player.Get() && Instance.IsValid() && World->GetGameInstance()==Instance.Get() && Viewport.IsValid() && World->GetGameViewport()==Viewport.Get() && Manager.IsValid() && PC->PlayerCameraManager==Manager.Get() && HUD.IsValid() && PC->GetHUD()==HUD.Get() && Save.IsValid();
    }
    bool Ready()
    {
        auto* W=World.Get();auto* GM=W?W->GetAuthGameMode<AFGGameMode>():nullptr;auto* S=GM?GM->GetSaveSession():nullptr;auto* C=W?Cast<AFGPlayerController>(W->GetFirstPlayerController()):nullptr;
        if (!W || W->bIsTearingDown || W->GetNetMode()!=NM_Standalone || !W->HasBegunPlay() || !GM || GM->IsMainMenuGameMode() || !S || S->HasSaveGameOperationPending() || !C || !C->IsLocalController() || !C->GetLocalPlayer()) return false;
        const auto& H=S->GetLoadedSaveHeader();const auto Load=GM->GetLoadData();const FString Expected=Mode==TEXT("save")?CloneName:SaveName;
        LoadedHeaderSaveName=H.SaveName;RequestedLoadName=Load.LoadName;
        const bool ExactIdentity=Mode==TEXT("reload")?(H.SaveName==SaveName && Load.LoadName==SaveName):(Load.LoadName==Expected || H.SaveName==Expected);
        if (!ExactIdentity || H.MapName.IsEmpty() || H.SessionName.IsEmpty()) { LastReason=TEXT("Loaded native header does not identify the exact designated clone/output: header=")+LoadedHeaderSaveName+TEXT(" requested=")+RequestedLoadName;return false; }
        if (bAutosave && !ControlAutoSession(S)) return false;
        auto* UI=C->GetGameUI();if (!FactoryFPVShippingStartup::Ready(UI,LastReason,bSplash,bAnimation,bStartup,bFrontEnd)) return false;
        auto* P=Cast<AFGCharacterPlayer>(C->GetPawn());if (!P || !P->IsAliveAndWell() || !P->HasAuthority() || P->GetController()!=C || C->NeedRespawn() || C->IsRespawning() || C->IsClientRespawnGracePeriodActive() || !C->PlayerCameraManager || !Cast<AFGHUD>(C->GetHUD())) return false;
        const auto* Health=P->GetHealthComponent();
        // Naturally full health makes exact save/reload health comparison
        // meaningful without treating ordinary regeneration as save damage.
        if (!Health || !FMath::IsFinite(Health->GetCurrentHealth()) || !FMath::IsFinite(Health->GetMaxHealth()) || Health->GetMaxHealth()<=0 || !FMath::IsNearlyEqual(Health->GetCurrentHealth(),Health->GetMaxHealth(),.01f)) { LastReason=TEXT("Waiting for naturally full finite native health; no healing is performed by this fixture.");return false; }
        PC=C;Pioneer=P;Instance=W->GetGameInstance();Player=C->GetLocalPlayer();Viewport=W->GetGameViewport();Hangar=Player->GetSubsystem<UFactoryFPVLocalPlayerSubsystem>();Save=S;Manager=C->PlayerCameraManager;HUD=Cast<AFGHUD>(C->GetHUD());
        if (!Instance.IsValid() || !Viewport.IsValid() || !Hangar.IsValid() || !ClearNativeUI() || !Grounded()) return false;
        return true;
    }
    bool Paths(bool Existing)
    {
        if ((!bAutosave && SaveName!=(bKitPersistence?TEXT("FPV-QA-KitRoundtrip"):OutputName)) || (bAutosave && (SaveName.IsEmpty() || !SaveName.Contains(TEXT("_autosave_")) || FPaths::GetCleanFilename(SaveName)!=SaveName || UFGSaveSystem::SanitizeSaveName(SaveName)!=SaveName))) { LastReason=TEXT("Only the exact explicit approved native output is accepted.");return false; }
        if (!UFGSaveSystem::GetAbsolutePathForSaveGame(World.Get(),CloneName,ResolvedClone)) { LastReason=TEXT("Exact original clone path cannot be resolved.");return false; }
        ResolvedClone=Normalized(ResolvedClone);ResolvedOutput=Normalized(UFGSaveSystem::CreateAbsolutePath(World.Get(),SaveName,false));
        const FString Expected=Normalized(FPaths::Combine(FPaths::GetPath(ResolvedClone),SaveName+TEXT(".sav")));
        if (!Disposable(ResolvedClone) || !Disposable(ResolvedOutput) || ResolvedOutput!=Expected || ResolvedOutput==ResolvedClone) { LastReason=TEXT("Native output resolver does not target the exact clone directory: clone=")+ResolvedClone+TEXT(" output=")+ResolvedOutput+TEXT(" expected=")+Expected;return false; }
        if (bOverwriteSlot) {
            const FString SaveRoot=Normalized(UFGSaveSystem::GetSaveDirectoryPath());
            if (!FPaths::IsUnderDirectory(ResolvedClone,SaveRoot) || !FPaths::IsUnderDirectory(ResolvedOutput,SaveRoot)) { LastReason=TEXT("Overwrite slot and clone must both reside under the isolated native save root.");return false; }
        }
        const bool Exists=IFileManager::Get().FileExists(*ResolvedOutput);if (Exists!=Existing) { LastReason=Existing?TEXT("Designated saved output is absent."):TEXT("Designated save output already exists; refusing overwrite.");return false; }
        if (bAutosave || bKitPersistence) {
            auto* System=UFGSaveSystem::Get(World.Get());FString NativePath;
            if (!System) { LastReason=TEXT("Native autosave name resolver is unavailable.");return false; }
            const bool Resolved=UFGSaveSystem::GetAbsolutePathForSaveGame(World.Get(),SaveName,NativePath);
            const bool NativeExists=System->SaveGameExistsSync(SaveName);bool Cached=false;
            for (const auto& Header:System->GetCachedSaves()) if (Header.SaveName.Equals(SaveName,ESearchCase::IgnoreCase)) { Cached=true;break; }
            if ((!Existing && (Resolved || NativeExists || Cached)) || (Existing && (!Resolved || !NativeExists || Normalized(NativePath)!=ResolvedOutput))) {
                LastReason=TEXT("Native output basename already resolves/caches elsewhere or does not resolve to the exact approved file: name=")+SaveName+TEXT(" nativePath=")+NativePath;return false;
            }
        }
        return true;
    }
    void Baseline()
    {
        OriginalView=PC->GetViewTarget();OriginalLocation=Pioneer->GetActorLocation();OriginalRotation=Pioneer->GetActorRotation();auto* M=Pioneer->GetCharacterMovement();
        OriginalMovement=M->MovementMode;OriginalCustomMode=M->CustomMovementMode;OriginalFov=Manager->GetFOVAngle();OriginalLockedFov=Manager->GetLockedFOV();
        OriginalHealth=Pioneer->GetHealthComponent()->GetCurrentHealth();OriginalMaxHealth=Pioneer->GetHealthComponent()->GetMaxHealth();
        bPartial=HUD->GetPartialPumpiMode();bHiddenHUD=HUD->GetHiddenHUDMode();bHudVisible=HUD->GetHUDVisibility();bHudShow=HUD->bShowHUD;bCursor=PC->bShowMouseCursor;bMove=PC->IsMoveInputIgnored();bLook=PC->IsLookInputIgnored();bViewportIgnored=Viewport->IgnoreInput();
        bMesh1Hidden=Pioneer->GetMesh1P()->bHiddenInGame;bMesh3Hidden=Pioneer->GetMesh3P()->bHiddenInGame;bMesh1Visible=Pioneer->GetMesh1P()->IsVisible();bMesh3Visible=Pioneer->GetMesh3P()->IsVisible();
        WorldKey=FObjectKey(World.Get());PCKey=FObjectKey(PC.Get());PioneerKey=FObjectKey(Pioneer.Get());PlayerKey=FObjectKey(Player.Get());
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && !It->IsTemplate() && It->GetWorld()==World.Get()) OriginalInputs.Add({*It,PC->IsInputComponentInStack(*It)});
    }
    bool WriteManifest(bool Completed)
    {
        TArray<FString> Lines={TEXT("schema=1"),TEXT("saveName=")+SaveName,TEXT("output=")+ResolvedOutput,TEXT("inventory=")+InventoryBaseline,TEXT("progress=")+ProgressBaseline,TEXT("baseline=")+Snapshots.FindRef(TEXT("baseline")),TEXT("location=")+OriginalLocation.ToString(),TEXT("rotation=")+OriginalRotation.ToString(),TEXT("viewClass=")+Class(OriginalView.Get())};
        Lines.Add(FString::Printf(TEXT("movement=%d"),OriginalMovement));Lines.Add(FString::Printf(TEXT("customMovement=%d"),OriginalCustomMode));Lines.Add(TEXT("fov=")+Number(OriginalFov));Lines.Add(TEXT("lockedFov=")+Number(OriginalLockedFov));
        Lines.Add(TEXT("health=")+Number(OriginalHealth));Lines.Add(TEXT("maxHealth=")+Number(OriginalMaxHealth));
        for (const auto& P:TArray<TPair<FString,bool>>{{TEXT("partial"),bPartial},{TEXT("hiddenHud"),bHiddenHUD},{TEXT("hudVisible"),bHudVisible},{TEXT("hudShow"),bHudShow},{TEXT("cursor"),bCursor},{TEXT("move"),bMove},{TEXT("look"),bLook},{TEXT("viewportIgnored"),bViewportIgnored},{TEXT("mesh1Hidden"),bMesh1Hidden},{TEXT("mesh3Hidden"),bMesh3Hidden},{TEXT("mesh1Visible"),bMesh1Visible},{TEXT("mesh3Visible"),bMesh3Visible}}) Lines.Add(P.Key+TEXT("=")+(P.Value?TEXT("1"):TEXT("0")));
        for (const auto& P:TArray<TPair<FString,bool>>{{TEXT("saveRequested"),bRequested},{TEXT("callbackSucceeded"),bCallback && bCallbackSuccess && CallbackCount==1},{TEXT("activeAtRequest"),bActiveAtRequest},{TEXT("heldAtRequest"),bHeldAtRequest},{TEXT("activeAtCallback"),bActiveAtCallback},{TEXT("heldAtCallback"),bHeldAtCallback},{TEXT("activeThroughCompletion"),bActiveThroughCompletion},{TEXT("remainedDisarmed"),bRemainedDisarmed}}) Lines.Add(P.Key+TEXT("=")+(P.Value?TEXT("1"):TEXT("0")));
        Lines.Add(bAutosave?TEXT("autosave=1"):TEXT("autosave=0"));
        Lines.Add(bKitPersistence?TEXT("kitPersistence=1"):TEXT("kitPersistence=0"));
        if (bKitPersistence) {
            Lines.Add(TEXT("kitPreparationEvidence=")+KitPreparationEvidence);Lines.Add(TEXT("kitBaseline=")+KitBaseline);Lines.Add(TEXT("kitPresentationBaseline=")+KitPresentationBaseline);
            Lines.Add(FString::Printf(TEXT("kitIndex=%d"),KitIndex));Lines.Add(bKitPreparationCompleted && bKitPreparationSucceeded?TEXT("kitPreparationVerified=1"):TEXT("kitPreparationVerified=0"));
        }
        if (bAutosave) {
            Lines.Add(TEXT("autosavePlanSha1=")+AutoPlanHash);Lines.Add(TEXT("autosaveEvidence=")+AutoEvidence());
            if (bArmedHover) Lines.Add(TEXT("armedHover=1"));
            Lines.Add(FString::Printf(TEXT("autosaveOriginalLastId=%d"),AutoLastId));Lines.Add(FString::Printf(TEXT("autosaveNextId=%d"),AutoNextId));Lines.Add(FString::Printf(TEXT("autosaveRotations=%d"),AutoRotations));Lines.Add(TEXT("autosaveSession=")+AutoSession);
            Lines.Add(Completed && AutoSchedulingVerified()?TEXT("autosaveVerified=1"):TEXT("autosaveVerified=0"));
            if (bOverwriteSlot) {
                Lines.Add(TEXT("overwriteSlot=1"));
                Lines.Add(Completed && bOverwriteVerified?TEXT("overwriteVerified=1"):TEXT("overwriteVerified=0"));
                Lines.Add(TEXT("overwritePreimageSha1=")+OverwritePreimageSha1);
                Lines.Add(TEXT("overwritePostimageSha1=")+OverwritePostimageSha1);
            }
        }
        Lines.Add(Completed?TEXT("saveCompleted=1"):TEXT("saveCompleted=0"));const FString Path=Normalized(ManifestPath(bAutosave,bKitPersistence));
        if (!Disposable(Path) || !FPaths::IsUnderDirectory(Path,Normalized(FPaths::ProjectSavedDir())) || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true)) return false;
        return FFileHelper::SaveStringArrayToFile(Lines,*Path);
    }
    bool LoadManifest()
    {
        TArray<FString> Lines;if (!FFileHelper::LoadFileToStringArray(Lines,*ManifestPath(bAutosave,bKitPersistence))) return false;
        for (const auto& Line:Lines) { FString K,V;if (!Line.Split(TEXT("="),&K,&V) || Manifest.Contains(K)) return false;Manifest.Add(K,V); }
        if (Manifest.FindRef(TEXT("schema"))!=TEXT("1") || Manifest.FindRef(TEXT("saveName"))!=SaveName || Manifest.FindRef(TEXT("output"))!=ResolvedOutput || Manifest.FindRef(TEXT("saveCompleted"))!=TEXT("1") || !OriginalLocation.InitFromString(Manifest.FindRef(TEXT("location"))) || !OriginalRotation.InitFromString(Manifest.FindRef(TEXT("rotation")))) return false;
        InventoryBaseline=Manifest.FindRef(TEXT("inventory"));ProgressBaseline=Manifest.FindRef(TEXT("progress"));if (InventoryBaseline.IsEmpty() || ProgressBaseline.IsEmpty()) return false;
        if (!LexTryParseString(OriginalHealth,*Manifest.FindRef(TEXT("health"))) || !LexTryParseString(OriginalMaxHealth,*Manifest.FindRef(TEXT("maxHealth"))) || !FMath::IsFinite(OriginalHealth) || !FMath::IsFinite(OriginalMaxHealth) || OriginalMaxHealth<=0) return false;
        if (bKitPersistence) {
            if (Manifest.FindRef(TEXT("kitPersistence"))!=TEXT("1") || Manifest.FindRef(TEXT("kitPreparationVerified"))!=TEXT("1") || Manifest.FindRef(TEXT("kitPreparationEvidence")).IsEmpty() ||
                !LexTryParseString(KitIndex,*Manifest.FindRef(TEXT("kitIndex"))) || KitIndex<0) return false;
            KitBaseline=Manifest.FindRef(TEXT("kitBaseline"));KitPresentationBaseline=Manifest.FindRef(TEXT("kitPresentationBaseline"));
            if (KitBaseline.IsEmpty() || KitPresentationBaseline.IsEmpty()) return false;bKitProvenance=true;
        }
        if (bAutosave) {
            if (Manifest.FindRef(TEXT("autosave"))!=TEXT("1") || Manifest.FindRef(TEXT("autosaveVerified"))!=TEXT("1") || Manifest.FindRef(TEXT("autosaveEvidence")).IsEmpty()) return false;
            AutoPlanHash=Manifest.FindRef(TEXT("autosavePlanSha1"));FString Approval;
            if (!LexTryParseString(AutoLastId,*Manifest.FindRef(TEXT("autosaveOriginalLastId"))) || !LexTryParseString(AutoNextId,*Manifest.FindRef(TEXT("autosaveNextId"))) || !LexTryParseString(AutoRotations,*Manifest.FindRef(TEXT("autosaveRotations"))) || AutoLastId<0 || AutoLastId>255 || AutoRotations<1 || AutoRotations>255 || AutoNextId!=static_cast<uint8>(AutoLastId+1)%AutoRotations) return false;AutoSession=Manifest.FindRef(TEXT("autosaveSession"));
            if (AutoPlanHash.Len()!=40 || !FileSha1(AutosavePlanPath()).Equals(AutoPlanHash,ESearchCase::IgnoreCase) || !FFileHelper::LoadFileToString(Approval,*AutosaveApprovalPath())) return false;Approval.TrimStartAndEndInline();
            FString Plan;if (!FFileHelper::LoadFileToString(Plan,*AutosavePlanPath()) || !Plan.Contains(TEXT("\"expectedSaveName\":\"")+Escape(SaveName)+TEXT("\"")) || !Plan.Contains(TEXT("\"expectedOutputPath\":\"")+Escape(ResolvedOutput)+TEXT("\"")) || !Approval.Equals(AutoPlanHash,ESearchCase::IgnoreCase)) return false;
            if (bOverwriteSlot!=(Manifest.FindRef(TEXT("overwriteSlot"))==TEXT("1")) || bOverwriteSlot!=Plan.Contains(TEXT("\"overwriteSlot\":true"))) return false;
            if (bArmedHover!=(Manifest.FindRef(TEXT("armedHover"))==TEXT("1")) || bArmedHover!=Plan.Contains(TEXT("\"armedHover\":true")) ||
                Manifest.FindRef(TEXT("remainedDisarmed"))!=(bArmedHover?TEXT("0"):TEXT("1"))) return false;
            if (bOverwriteSlot) {
                OverwritePreimageSha1=Manifest.FindRef(TEXT("overwritePreimageSha1"));
                OverwritePostimageSha1=Manifest.FindRef(TEXT("overwritePostimageSha1"));
                bOverwriteLoadedPostimageVerified=Manifest.FindRef(TEXT("overwriteVerified"))==TEXT("1") &&
                    OverwritePreimageSha1.Len()==40 && OverwritePostimageSha1.Len()==40 &&
                    !OverwritePreimageSha1.Equals(OverwritePostimageSha1,ESearchCase::IgnoreCase) &&
                    OverwritePreimageSha1.Equals(FileSha1(ResolvedClone),ESearchCase::IgnoreCase) &&
                    OverwritePostimageSha1.Equals(FileSha1(ResolvedOutput),ESearchCase::IgnoreCase);
                if (!bOverwriteLoadedPostimageVerified) return false;
            }
            bAutoProvenance=true;
        } else for (const TCHAR* K:{TEXT("saveRequested"),TEXT("callbackSucceeded"),TEXT("activeAtRequest"),TEXT("heldAtRequest"),TEXT("activeAtCallback"),TEXT("heldAtCallback"),TEXT("activeThroughCompletion"),TEXT("remainedDisarmed")}) if (Manifest.FindRef(K)!=TEXT("1")) return false;
        // Keep earned first-process provenance separate from this process's
        // observed callbacks and live-session fields. The host pins this file.
        bSavedProvenanceVerified=true;
        return true;
    }
    bool Prepare(double Now)
    {
        if (bAutosave && Mode==TEXT("save") && !MakeAutoPlan()) { Finish(false,TEXT("Native autosave plan refused: ")+LastReason);return false; }
        if (!NoTransientResources() || PC->IsMoveInputIgnored() || PC->IsLookInputIgnored() || !Pioneer->GetMesh1P() || !Pioneer->GetMesh3P() || F6()!=1 || !Paths(Mode==TEXT("reload"))) { Finish(false,TEXT("Native save baseline/path preflight refused: ")+LastReason);return false; }
        if (bKitPersistence && Mode==TEXT("save")) {
            auto* Arms=Pioneer->GetEquipmentSlot(EEquipmentSlot::ES_ARMS);KitIndex=Arms?Arms->GetActiveIndex():INDEX_NONE;
            if (!bKitPreparationCompleted || !bKitPreparationSucceeded || !KitState(KitBaseline,KitPresentationBaseline)) { Finish(false,TEXT("Paid preparer did not hand over the exact native-equipped radio; no baseline/save taken."));return false; }
            KitActor=KitRadio();bKitActorBound=true;
        }
        KitPolicy();Baseline();
        if (Mode==TEXT("save")) {
            if (IFileManager::Get().FileExists(*ManifestPath(bAutosave,bKitPersistence)) || !Inventory(InventoryBaseline) || !Progress(ProgressBaseline)) { Finish(false,TEXT("Fresh manifest and real native inventory/progression baseline are required."));return false; }
            Record(TEXT("baseline"));bBaseline=true;if (!WriteManifest(false)) { Finish(false,TEXT("Could not persist bounded read-only baseline manifest."));return false; }
            PhaseTo(EPhase::Before,Now);
        } else {
            if (!LoadManifest()) { Finish(false,TEXT("Exact completed before-save manifest is unavailable or invalid."));return false; }
            if (bKitPersistence) { KitActor=KitRadio();bKitActorBound=true;if (!KitActor.IsValid()) { Finish(false,TEXT("Native reload has no genuine restored equipped radio actor; no re-equip fallback used."));return false; } }
            bBaseline=true;PhaseTo(EPhase::Reloaded,Now);Record(TEXT("reloaded"));
            if (!VerifyReload()) { Finish(false,TEXT("Actual native reload differs from saved pioneer/inventory/progression baseline; no state repaired."));return false; }
            bOutputVerified=true;
        }
        bPrepared=true;return true;
    }
    bool Launch(double Now,EPhase Next)
    {
        if (!Identity() || !ClearNativeUI() || !Grounded() || !NoTransientResources() || !ContentMatches()) { Finish(false,TEXT("Ordinary launch boundary changed; no completion forced."));return false; }
        FString Error;Drone=AFactoryFPVDrone::Launch(PC.Get(),TEXT("keyboard"),Error);if (!Drone.IsValid() || Drone->IsFlightArmed()) { Finish(false,TEXT("Ordinary disarmed save-QA launch failed: ")+Error);return false; }
        ModActors.Reset();ModControls.Reset();ModSources.Reset();Anchor.Reset();
        for (TActorIterator<AActor> It(World.Get());It;++It) if (!It->IsActorBeingDestroyed() && (Cast<AFactoryFPVDrone>(*It) || Cast<AFactoryFPVPioneerAnchor>(*It) || Cast<AFactoryFPVPilotVisual>(*It))) { ModActors.Add(*It);if (auto* A=Cast<AFactoryFPVPioneerAnchor>(*It)) { if (Anchor.IsValid()) { Finish(false,TEXT("Ambiguous actual pioneer hold."));return false; }Anchor=A; } }
        for (TObjectIterator<UInputComponent> It;It;++It) if (IsValid(*It) && It->GetWorld()==World.Get() && It->GetOwner()==Drone.Get() && PC->IsInputComponentInStack(*It)) ModControls.Add(*It);
        for (TObjectIterator<UWorldPartitionStreamingSourceComponent> It;It;++It) if (IsValid(*It) && It->GetWorld()==World.Get() && (It->GetOwner()==Drone.Get() || It->GetOwner()==Anchor.Get())) ModSources.Add(*It);
        if (ModActors.Num()<2 || ModControls.IsEmpty() || ModSources.Num()!=2 || !Anchor.IsValid()) { Finish(false,TEXT("Real launch lacks its actual actors/controls/streaming sources."));return false; }
        bRemainedDisarmed=true;
        if (IsArmedHoverRun() && !Hover.Begin(*Drone.Get(),Error)) {
            Finish(false,TEXT("Could not start the guarded powered autosave hover: ")+Error);return false;
        }
        bRemainedDisarmed=!Drone->IsFlightArmed();
        PhaseTo(Next,Now);return true;
    }
    bool Active() const
    {
        if (!Identity() || !ClearNativeUI() || !Drone.IsValid() || Drone->IsActorBeingDestroyed() || Drone->IsFlightArmed()!=IsArmedHoverRun() || AFactoryFPVDrone::FindActive(World.Get())!=Drone.Get() || Drone->GetOwner()!=PC.Get() || PC->GetViewTarget()!=Drone.Get() || !Anchor.IsValid() || !Anchor->IsHolding() || !Drone->IsFlightOverlayVisible()) return false;
        if (bKitPersistence) { FString State,Presentation;if (GetDefault<UFactoryFPVSettings>()->bAllowSandboxAccess || !KitState(State,Presentation) || State!=KitBaseline) return false; }
        auto* M=Pioneer->GetCharacterMovement();const auto L=FactoryFPVInputLease::GetDiagnostics(PC.Get());
        if (!M || M->MovementMode!=MOVE_None || !M->Velocity.IsNearlyZero(.01) || !HealthMatches() || !Pioneer->GetActorLocation().Equals(OriginalLocation,.5) || !Pioneer->GetActorRotation().Equals(OriginalRotation,.02) || !HUD->GetPartialPumpiMode() || !HUD->GetHiddenHUDMode() || !PC->IsMoveInputIgnored() || !PC->IsLookInputIgnored() || L.ActiveLeases!=1 || L.OwnedMoveContributions!=1 || L.OwnedLookContributions!=1 || L.ResetDepth!=0) return false;
        for (const auto& I:ModControls) if (!I.IsValid() || !PC->IsInputComponentInStack(I.Get())) return false;
        for (const auto& S:ModSources) if (!S.IsValid() || !S->IsStreamingSourceEnabled()) return false;return true;
    }
    bool Restored() const
    {
        if (!Identity() || !ClearNativeUI() || !Grounded() || !HealthMatches() || !NoTransientResources() || PC->GetViewTarget()!=OriginalView.Get() || !Pioneer->GetActorLocation().Equals(OriginalLocation,.5) || !Pioneer->GetActorRotation().Equals(OriginalRotation,.02)) return false;
        if (bKitPersistence) { FString State,Presentation;if (!KitState(State,Presentation) || State!=KitBaseline || Presentation!=KitPresentationBaseline) return false; }
        auto* M=Pioneer->GetCharacterMovement();if (M->MovementMode!=OriginalMovement || M->CustomMovementMode!=OriginalCustomMode || !FMath::IsNearlyEqual(Manager->GetFOVAngle(),OriginalFov,.1) || !FMath::IsNearlyEqual(Manager->GetLockedFOV(),OriginalLockedFov,.01) || HUD->GetPartialPumpiMode()!=bPartial || HUD->GetHiddenHUDMode()!=bHiddenHUD || HUD->GetHUDVisibility()!=bHudVisible || HUD->bShowHUD!=bHudShow || PC->bShowMouseCursor!=bCursor || PC->IsMoveInputIgnored()!=bMove || PC->IsLookInputIgnored()!=bLook || Viewport->IgnoreInput()!=bViewportIgnored || Pioneer->GetMesh1P()->bHiddenInGame!=bMesh1Hidden || Pioneer->GetMesh3P()->bHiddenInGame!=bMesh3Hidden || Pioneer->GetMesh1P()->IsVisible()!=bMesh1Visible || Pioneer->GetMesh3P()->IsVisible()!=bMesh3Visible) return false;
        for (const auto& I:OriginalInputs) if (I.Input.IsValid()?PC->IsInputComponentInStack(I.Input.Get())!=I.bMember:I.bMember) return false;
        for (const auto& I:ModControls) if (I.IsValid() && PC->IsInputComponentInStack(I.Get())) return false;return F6()==1;
    }
    bool VerifyReload()
    {
        bNoTransient=NoTransientResources();bNoLeases=NoLeases();
        auto MB=[&](const TCHAR* K,bool V) { return Manifest.Contains(K) && Manifest.FindRef(K)==(V?TEXT("1"):TEXT("0")); };
        int32 Movement=INDEX_NONE,Custom=INDEX_NONE;double Fov=0,Lock=0;
        bool Values=LexTryParseString(Movement,*Manifest.FindRef(TEXT("movement"))) && LexTryParseString(Custom,*Manifest.FindRef(TEXT("customMovement"))) && LexTryParseString(Fov,*Manifest.FindRef(TEXT("fov"))) && LexTryParseString(Lock,*Manifest.FindRef(TEXT("lockedFov")));
        if (bKitPersistence) Values &= bKitProvenance && !GetDefault<UFactoryFPVSettings>()->bAllowSandboxAccess && KitMatches(true);
        if (bAutosave) { int32 Last=0,Count=0;FString Session;Values &= bAutoProvenance && AutoSchema(Last,Count,Session) && Last==AutoNextId && Count==AutoRotations && Session==AutoSession; }
        auto* M=Pioneer->GetCharacterMovement();
        return Identity() && Save->GetLoadedSaveHeader().SaveName==SaveName && ClearNativeUI() && Grounded() && HealthMatches() && bNoTransient && bNoLeases && F6()==1 && Values && M->MovementMode==Movement && M->CustomMovementMode==Custom && Pioneer->GetActorLocation().Equals(OriginalLocation,1.) && Pioneer->GetActorRotation().Equals(OriginalRotation,.1) && Class(PC->GetViewTarget())==Manifest.FindRef(TEXT("viewClass")) && FMath::IsNearlyEqual(Manager->GetFOVAngle(),Fov,.1) && FMath::IsNearlyEqual(Manager->GetLockedFOV(),Lock,.01) && MB(TEXT("partial"),HUD->GetPartialPumpiMode()) && MB(TEXT("hiddenHud"),HUD->GetHiddenHUDMode()) && MB(TEXT("hudVisible"),HUD->GetHUDVisibility()) && MB(TEXT("hudShow"),HUD->bShowHUD) && MB(TEXT("cursor"),PC->bShowMouseCursor) && MB(TEXT("move"),PC->IsMoveInputIgnored()) && MB(TEXT("look"),PC->IsLookInputIgnored()) && MB(TEXT("viewportIgnored"),Viewport->IgnoreInput()) && MB(TEXT("mesh1Hidden"),Pioneer->GetMesh1P()->bHiddenInGame) && MB(TEXT("mesh3Hidden"),Pioneer->GetMesh3P()->bHiddenInGame) && MB(TEXT("mesh1Visible"),Pioneer->GetMesh1P()->IsVisible()) && MB(TEXT("mesh3Visible"),Pioneer->GetMesh3P()->IsVisible()) && ContentMatches();
    }
    void SaveCompleted(bool Success,const FString& Error)
    {
        if (bDone) return;++CallbackCount;bCallback=true;bCallbackSuccess=Success;SaveError=Error;CallbackTime=FPlatformTime::Seconds();bActiveAtCallback=Active();bHeldAtCallback=Anchor.IsValid() && Anchor->IsHolding();Record(TEXT("nativeCallback"));
        if (!Success || CallbackCount!=1 || !bActiveAtCallback) { Finish(false,TEXT("Native save callback failed or original disarmed session/hold ended at completion: ")+Error);return; }
        PhaseTo(EPhase::Completed,CallbackTime);
    }
    void RequestSave(double Now)
    {
        if (bAutosave) { if (!Active() || !ContentMatches()) { Finish(false,TEXT("Live FPV session changed before autosave approval."));return; }PhaseTo(EPhase::AwaitAutosaveApproval,Now);return; }
        if (bRequested || !Active() || !ContentMatches() || Save->HasSaveGameOperationPending() || !Paths(false)) { Finish(false,TEXT("Exact live disarmed save boundary/path changed: ")+LastReason);return; }
        bActiveAtRequest=true;bHeldAtRequest=Anchor.IsValid() && Anchor->IsHolding();bRequested=true;RequestTime=Now;Record(TEXT("saveRequest"));PhaseTo(EPhase::Saving,Now);
        const TWeakPtr<FRun> Weak=AsShared();Save->SaveGame(SaveName,FOnSaveGameComplete::CreateLambda([Weak](bool Success,const FText& Error,void*) {
            const FString Message=Error.ToString();auto Complete=[Weak,Success,Message] { if (auto Run=Weak.Pin()) Run->SaveCompleted(Success,Message); };
            if (IsInGameThread()) Complete();else AsyncTask(ENamedThreads::GameThread,MoveTemp(Complete));
        }),nullptr);
        if (!bDone && Save.IsValid() && Save->HasSaveGameOperationPending() && Active()) { bPendingObserved=true;LastPendingFrame=GFrameCounter;++PendingActiveSamples; }
    }
    bool OutputVerified()
    {
        const bool Completed=bAutosave?(AutoQueueCount==1 && AutoSuccessCount==1 && bAutoSuccessActive && bAutoSuccessHeld):bCallback && bCallbackSuccess && CallbackCount==1;
        if (!Save.IsValid() || Save->HasSaveGameOperationPending() || !Completed || IFileManager::Get().FileSize(*ResolvedOutput)<=0) return false;
        FString Actual;FSaveHeader H;auto* System=UFGSaveSystem::Get(World.Get());
        if (!System || !UFGSaveSystem::GetAbsolutePathForSaveGame(World.Get(),SaveName,Actual) || Normalized(Actual)!=ResolvedOutput ||
            !System->LoadSaveGameHeaderSync(SaveName,H) || H.SaveName!=SaveName || H.MapName.IsEmpty() || H.SessionName.IsEmpty()) return false;
        if (!bOverwriteSlot) return true;
        OverwritePostimageSha1=FileSha1(ResolvedOutput);
        bOverwriteVerified=bOccupiedSlotVerified && OverwritePostimageSha1.Len()==40 &&
            !OverwritePostimageSha1.Equals(OverwritePreimageSha1,ESearchCase::IgnoreCase) &&
            H.SessionName==AutoSession && H.MapName==Save->GetLoadedSaveHeader().MapName;
        return bOverwriteVerified;
    }
    void ReleaseCapture() { if (CaptureHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(CaptureHandle);CaptureHandle.Reset(); }if (bOwnCapture && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename()==PendingCapture) { FScreenshotRequest::Reset();GAreScreenMessagesEnabled=bScreenMessagesBefore; }bOwnCapture=false; }
    bool Capture(FCapture& C,const TCHAR* Name,double Now)
    {
        if (!Viewport.IsValid() || !Viewport->Viewport || FScreenshotRequest::IsScreenshotRequested()) { Finish(false,TEXT("Actual viewport unavailable or unrelated screenshot pending."));return false; }
        const FString Saved=Normalized(FPaths::ProjectSavedDir()),Dir=Normalized(FPaths::Combine(Saved,TEXT("FactoryFPV-QA"))),Path=Normalized(FPaths::Combine(Dir,Name));
        if (!Disposable(Path) || !FPaths::IsUnderDirectory(Path,Dir) || !FPaths::IsUnderDirectory(Dir,Saved) || IFileManager::Get().FileExists(*Path) || !IFileManager::Get().MakeDirectory(*Dir,true)) { Finish(false,TEXT("Save qualification capture is not fresh and inside disposable Saved."));return false; }
        ReleaseCapture();C.Path=PendingCapture=Path;CaptureStarted=Now;bOwnCapture=true;bCaptureProcessed=false;bScreenMessagesBefore=GAreScreenMessagesEnabled;
        const TWeakPtr<FRun> Weak=AsShared();CaptureHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([Weak] { if (auto Run=Weak.Pin();Run && !Run->bDone && Run->bOwnCapture) { Run->bOwnCapture=false;Run->bCaptureProcessed=true; } });
        FScreenshotRequest::RequestScreenshot(Path,true,false,false);return true;
    }
    bool Captured(FCapture& C,double Now)
    {
        if (Now-CaptureStarted>5.) { Finish(false,TEXT("Native save viewport capture exceeded five seconds."));return false; }if (!bCaptureProcessed) return false;ReleaseCapture();TArray64<uint8> Bytes;FImage Image;
        if (!FFileHelper::LoadFileToArray(Bytes,*C.Path) || !FImageUtils::DecompressImage(Bytes.GetData(),Bytes.Num(),Image) || Image.NumSlices!=1 || Image.SizeX<320 || Image.SizeY<180 || Image.SizeX>8192 || Image.SizeY>8192 || Image.RawData.IsEmpty()) { Finish(false,TEXT("Save capture is not a sane decoded image."));return false; }
        double Sum=0,Squares=0;int32 Count=0;for (int32 Y=0;Y<Image.SizeY;Y+=FMath::Max(1,Image.SizeY/40)) for (int32 X=0;X<Image.SizeX;X+=FMath::Max(1,Image.SizeX/60)) { const auto P=Image.GetOnePixelLinear(X,Y);if (!FMath::IsFinite(P.R) || !FMath::IsFinite(P.G) || !FMath::IsFinite(P.B)) { Finish(false,TEXT("Save capture pixels are nonfinite."));return false; }const double L=.2126*P.R+.7152*P.G+.0722*P.B;Sum+=L;Squares+=L*L;++Count; }
        C.Variance=Count?FMath::Max(0.,Squares/Count-FMath::Square(Sum/Count)):0.;if (!FMath::IsFinite(C.Variance) || C.Variance<1e-8) { Finish(false,TEXT("Save capture is uniformly blank."));return false; }
        C.Width=Image.SizeX;C.Height=Image.SizeY;C.bDecoded=true;return true;
    }
    bool AutoSchedulingVerified() const
    {
        return bAutosave && (Mode==TEXT("reload")?bAutoProvenance && (!bOverwriteSlot || bOverwriteLoadedPostimageVerified):
            bAutoPlanApproved && bAutoArmed && AutoQueueCount==1 && AutoSuccessCount==1 && bAutoQueueActive && bAutoQueueHeld && bAutoSuccessActive && bAutoSuccessHeld && bAutoTimerStopped && AutoQueueElapsed>=19.5 && (!bOverwriteSlot || bOverwriteVerified) && (!bArmedHover || (bArmedAtQueue && bArmedAtDiskSuccess)));
    }
    FString AutoEvidence() const
    {
        int32 Last=INDEX_NONE,Count=0;FString Session;const bool Read=AutoSchema(Last,Count,Session);
        const FString Prefix=FString::Printf(TEXT("{\"observedAutosaveId\":%d,\"idAdvanceVerified\":%s,\"loadedIdMatchesSaved\":%s,\"waitingActiveSamples\":%d,"),Read?Last:INDEX_NONE,B(Read && bAutoArmed && AutoQueueCount==1 && Last==AutoNextId && Count==AutoRotations && Session==AutoSession),B(Read && Mode==TEXT("reload") && bAutoProvenance && Last==AutoNextId && Count==AutoRotations && Session==AutoSession),AutoWaitingActiveSamples);
        FString Result=Prefix+FString::Printf(TEXT("\"planApproved\":%s,\"timerArmed\":%s,\"queuedNotificationCount\":%d,\"diskSuccessEventCount\":%d,\"activeAtQueue\":%s,\"heldAtQueue\":%s,\"activeAtDiskSuccess\":%s,\"heldAtDiskSuccess\":%s,\"furtherTimerDisabled\":%s,\"countdownNotifications\":%d,\"queueElapsedWorldSeconds\":%s,\"successElapsedWorldSeconds\":%s,\"lastAutosaveId\":%d,\"rotatingAutosaves\":%d,\"predictedAutosaveId\":%d,\"sessionName\":\"%s\",\"planSha1\":\"%s\",\"originalIntervalSeconds\":%s,\"originalEnabled\":%s,\"manualAutosaveInvocations\":0,\"manualSaveInvocations\":%d,\"injectedTimerTicks\":false,\"afterSuccessActiveSamples\":%d,\"trace\":[%s]"),
            B(bAutoPlanApproved),B(bAutoArmed),AutoQueueCount,AutoSuccessCount,B(bAutoQueueActive),B(bAutoQueueHeld),B(bAutoSuccessActive),B(bAutoSuccessHeld),B(bAutoTimerStopped),AutoCountdownCount,*Number(AutoQueueElapsed),*Number(AutoSuccessElapsed),AutoLastId,AutoRotations,AutoNextId,*Escape(AutoSession),*AutoPlanHash,*Number(OriginalAutoInterval),B(bOriginalAutoEnabled),!bAutosave && Mode==TEXT("save") && bRequested?1:0,AutoAfterSuccessSamples,*FString::Join(AutoTrace,TEXT(",")));
        if (bOverwriteSlot) Result+=FString::Printf(TEXT(",\"overwriteSlot\":true,\"occupiedSlotVerified\":%s,\"overwriteVerified\":%s,\"overwriteLoadedPostimageVerified\":%s,\"overwritePreimageSha1\":\"%s\",\"overwritePostimageSha1\":\"%s\""),
            B(bOccupiedSlotVerified),B(bOverwriteVerified),B(bOverwriteLoadedPostimageVerified),*Escape(OverwritePreimageSha1),*Escape(OverwritePostimageSha1));
        if (bArmedHover) {
            Result+=FString::Printf(TEXT(",\"armedHoverEnabled\":true,\"armedAtQueue\":%s,\"armedAtDiskSuccess\":%s,\"hoverAltitudeAtQueueCm\":%s,\"hoverAltitudeAtDiskSuccessCm\":%s"),
                B(bArmedAtQueue),B(bArmedAtDiskSuccess),*Number(HoverAltitudeAtQueue),*Number(HoverAltitudeAtDiskSuccess));
            if (IsArmedHoverRun()) Result+=Hover.Evidence();
        }
        return Result+TEXT("}");
    }
    FString Evidence() const
    {
        FString Out=FString::Printf(TEXT(",\"mode\":\"%s\",\"saveName\":\"%s\",\"clonePath\":\"%s\",\"outputPath\":\"%s\",\"manifestPath\":\"%s\",\"baselineVerified\":%s,\"saveRequested\":%s,\"savePendingObserved\":%s,\"nativeSaveCallbackObserved\":%s,\"nativeSaveCallbackSucceeded\":%s,\"nativeSaveCallbackCount\":%d,\"activeAtRequest\":%s,\"activeAtCallback\":%s,\"activeThroughCompletion\":%s,\"outputVerified\":%s,\"pendingActiveSamples\":%d,\"postCallbackActiveSamples\":%d,\"saveDurationSeconds\":%s,\"inventoryMatches\":%s,\"progressionMatches\":%s,\"pioneerRestored\":%s,\"noTransientResources\":%s,\"noModLeases\":%s,\"hangarRebound\":%s,\"freshSessionPassed\":%s,\"capturesVerified\":%s,\"programmaticNativeSave\":%s,\"injectedFlightControls\":false,\"forcedSaveCompletion\":false,\"manualNativeInputMutation\":false,\"healingApplied\":false,\"autosaveSchedulingVerified\":false,\"pendingUnsupportedReturnVerified\":false,\"inventoryStatePayloadVerified\":false,\"startupSplashObserved\":%s,\"startupSplashAnimationObserved\":%s,\"startupSplashFinished\":%s,\"maximumPioneerDisplacementCm\":%s,\"inventoryBaseline\":%s,\"inventoryCurrent\":%s,\"progressionBaseline\":%s,\"progressionCurrent\":%s,\"saveError\":\"%s\",\"snapshots\":{"),
            *Escape(Mode),*Escape(SaveName),*Escape(ResolvedClone),*Escape(ResolvedOutput),*Escape(Normalized(ManifestPath(bAutosave,bKitPersistence))),B(bBaseline),B(bRequested),B(bPendingObserved),B(bCallback),B(bCallbackSuccess),CallbackCount,B(bActiveAtRequest),B(bActiveAtCallback),B(bActiveThroughCompletion),B(bOutputVerified),PendingActiveSamples,PostCallbackActiveSamples,*Number(CallbackTime>0?CallbackTime-RequestTime:0),B(bInventoryMatches),B(bProgressMatches),B(bReturned),B(bNoTransient),B(bNoLeases),B(bHangarRebound),B(bFresh),B(bCaptures),B(Mode==TEXT("save")),B(bSplash),B(bAnimation),B(bStartup),*Number(MaximumPioneerDisplacement),InventoryBaseline.IsEmpty()?TEXT("null"):*InventoryBaseline,InventoryCurrent.IsEmpty()?TEXT("null"):*InventoryCurrent,ProgressBaseline.IsEmpty()?TEXT("null"):*ProgressBaseline,ProgressCurrent.IsEmpty()?TEXT("null"):*ProgressCurrent,*Escape(SaveError));
        // Preserve actual manual-save observations; a native timer or queue
        // notification never fabricates FOnSaveGameComplete callback flags.
        if (bAutosave) {
            Out.ReplaceInline(TEXT("\"programmaticNativeSave\":true"),TEXT("\"programmaticNativeSave\":false"));
            Out.ReplaceInline(TEXT("\"autosaveSchedulingVerified\":false"),AutoSchedulingVerified()?TEXT("\"autosaveSchedulingVerified\":true"):TEXT("\"autosaveSchedulingVerified\":false"));
            if (IsArmedHoverRun()) Out.ReplaceInline(TEXT("\"injectedFlightControls\":false"),TEXT("\"injectedFlightControls\":true"));
        }
        bool First=true;for (const TCHAR* K:{TEXT("baseline"),TEXT("active"),TEXT("autosaveArmed"),TEXT("autosaveQueued"),TEXT("autosaveDiskSuccess"),TEXT("saveRequest"),TEXT("nativeCallback"),TEXT("saveCompleted"),TEXT("returned"),TEXT("reloaded"),TEXT("freshReturn"),TEXT("failure")}) if (const auto* V=Snapshots.Find(K)) { if (!First) Out+=TEXT(",");First=false;Out+=TEXT("\"")+FString(K)+TEXT("\":")+*V; }
        const FString SavedBaseline=Mode==TEXT("reload")?Manifest.FindRef(TEXT("baseline")):Snapshots.FindRef(TEXT("baseline"));
        Out+=TEXT("},\"savedBaselineSnapshot\":")+(SavedBaseline.IsEmpty()?FString(TEXT("null")):SavedBaseline);
        Out+=FString::Printf(TEXT(",\"loadedHeaderSaveName\":\"%s\",\"requestedLoadName\":\"%s\""),*Escape(LoadedHeaderSaveName),*Escape(RequestedLoadName));
        Out+=TEXT(",\"autosaveEnabled\":")+FString(B(bAutosave))+TEXT(",\"autosaveSavedProvenanceVerified\":")+B(bAutoProvenance)+TEXT(",\"autosaveEvidence\":")+AutoEvidence()+TEXT(",\"savedAutosaveEvidence\":")+(bAutoProvenance?Manifest.FindRef(TEXT("autosaveEvidence")):FString(TEXT("null")));
        FString State,Presentation;const bool KitLive=bKitPersistence && KitState(State,Presentation);
        Out+=FString::Printf(TEXT(",\"kitPersistenceEnabled\":%s,\"kitPreparationStarted\":%s,\"kitPreparationCompleted\":%s,\"kitPreparationSucceeded\":%s,\"kitPreparationProvenanceVerified\":%s,\"kitPrepareCallsThisProcess\":%d,\"kitGrantCallsThisProcess\":0,\"kitProduceCallsAfterPreparation\":0,\"kitEquipActionsAfterPreparation\":0,\"kitPrimaryDispatchesThisProcess\":%d,\"kitPrimaryOpenedHangar\":%s,\"kitNativeEquippedVerified\":%s,\"kitSandboxPolicyRestored\":%s,\"kitIndex\":%d"),B(bKitPersistence),B(bKitPreparationStarted),B(bKitPreparationCompleted),B(bKitPreparationSucceeded),B(bKitProvenance),KitPrepareCalls,KitPrimaryDispatches,B(bKitPrimaryOpened),B(KitLive),B(bKitPolicyRestored),KitIndex);
        Out+=TEXT(",\"liveKitPreparationEvidence\":")+(KitPreparationEvidence.IsEmpty()?FString(TEXT("null")):KitPreparationEvidence)+TEXT(",\"savedKitPreparationEvidence\":")+(bKitProvenance?Manifest.FindRef(TEXT("kitPreparationEvidence")):FString(TEXT("null")));
        Out+=TEXT(",\"kitBaseline\":")+(KitBaseline.IsEmpty()?FString(TEXT("null")):KitBaseline)+TEXT(",\"kitCurrent\":")+(KitLive?State:FString(TEXT("null")))+TEXT(",\"kitPresentationBaseline\":")+(KitPresentationBaseline.IsEmpty()?FString(TEXT("null")):KitPresentationBaseline)+TEXT(",\"kitPresentationCurrent\":")+(KitLive?Presentation:FString(TEXT("null")));
        Out+=TEXT(",\"kitObservation\":")+(bKitPersistence?KitObservation():FString(TEXT("null")));
        Out+=FString::Printf(TEXT(",\"pioneerHeldAtRequest\":%s,\"pioneerHeldAtCompletion\":%s,\"remainedDisarmed\":%s,\"nativeRestoredReady\":%s,\"healthPreserved\":%s,\"naturallyFullHealthRequired\":true,\"saveProvenanceFromManifest\":%s,\"newSaveIssuedThisProcess\":%s,\"nativeSaveCallbackObservedThisProcess\":%s,\"nativeSaveCallbacksThisProcess\":%d,\"captures\":["),B(bHeldAtRequest),B(bHeldAtCallback),B(bRemainedDisarmed),B(bNativeRestoredReady),B(bPrepared && HealthMatches()),B(bSavedProvenanceVerified),B(Mode==TEXT("save") && bRequested),B(Mode==TEXT("save") && bCallback),Mode==TEXT("save")?CallbackCount:0);
        if (bCapture) Out+=Mode==TEXT("save")?BeforeCapture.Json()+TEXT(",")+ActiveCapture.Json()+TEXT(",")+CompletedCapture.Json():ReloadedCapture.Json()+TEXT(",")+FreshCapture.Json();return Out+TEXT("]");
    }
    void Finish(bool Success,const FString& Reason)
    {
        if (bDone) return;if (!Success && PC.IsValid()) Record(TEXT("failure"));bNativeRestoredReady=bReturned && Restored();bDone=true;ReleaseCapture();
        if (bAutosave) {
            StopAutoTimer();
            if (AutoCollectionHandle.IsValid()) { UFGSaveSystem::OnSaveCollectionChanged.Remove(AutoCollectionHandle);AutoCollectionHandle.Reset(); }
            if (AutoGameState.IsValid() && AutoObserver.Get()) { AutoGameState->mOnAutoSaveTimeNotification.RemoveDynamic(AutoObserver.Get(),&UFactoryFPVShippingAutosaveObserver::OnCountdown);AutoGameState->mOnAutoSaveFinished.RemoveDynamic(AutoObserver.Get(),&UFactoryFPVShippingAutosaveObserver::OnQueuedNotification); }
            if (AutoObserver.Get()) { AutoObserver->Queued=nullptr;AutoObserver->Countdown=nullptr; }AutoObserver.Reset();
        }
        // No native heal/input/movement/camera repair and no extra save. Only
        // this run's mod owners may be cleaned before orderly process exit.
        Hover.Cancel();
        if (auto* D=Drone.Get();D && !D->IsActorBeingDestroyed() && D->GetOwner()==PC.Get()) D->ExitDrone();
        if (bOwnedPanel && Hangar.IsValid() && Hangar->IsPanelOpen()) Hangar->ClosePanel();
        CancelKitPreparation.ExecuteIfBound();CancelKitPreparation.Unbind();RestoreKitPolicy();
        Marker(Mode,Success?TEXT("PASS"):TEXT("FAIL"),Reason,Evidence(),bAutosave,bKitPersistence);FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
    }
    void StartFresh(double Now) {
        if (bKitPersistence) {
            auto* Radio=KitRadio();if (!bKitProvenance || !Radio || !KitMatches(true) || Hangar->IsPanelOpen()) { Finish(false,TEXT("Reload did not naturally restore the exact native-equipped radio for Primary; no grant/equip fallback used."));return; }
            ++KitPrimaryDispatches;Radio->TriggerDefaultEquipmentActionEvent(EDefaultEquipmentAction::PrimaryFire,EDefaultEquipmentActionEvent::Pressed);
            ++KitPrimaryDispatches;Radio->TriggerDefaultEquipmentActionEvent(EDefaultEquipmentAction::PrimaryFire,EDefaultEquipmentActionEvent::Released);
            bKitPrimaryOpened=Hangar->IsPanelOpen();
            if (!bKitPrimaryOpened) { Finish(false,TEXT("Naturally restored radio's public Primary did not open the actual hangar."));return; }
        } else if (!InvokeF6() || !Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual rebound F6 failed to open native-ready hangar."));return; }
        bOwnedPanel=true;PhaseTo(EPhase::HangarOpened,Now);
    }
    void ReturnFromSavedFlight(double Now)
    {
        Hover.Cancel();
        Drone->ExitDrone();
        PhaseTo(EPhase::Return,Now);
    }
    bool Tick(float DeltaSeconds)
    {
        if (bDone) return false;const double Now=FPlatformTime::Seconds();if (!Guard()) { Finish(false,TEXT("Save QA isolation guard changed."));return false; }if (Now>Deadline) { Finish(false,TEXT("60-second save QA deadline: ")+LastReason);return false; }
        if (Phase==EPhase::Waiting) {
            // Observe the real splash from the first frame, concurrently with
            // preparation. Starting a fresh observer after it ends is invalid.
            const bool NativeReady=Ready();
            if (bKitPersistence && Mode==TEXT("save") && !bKitPreparationCompleted) return !bDone;
            if (NativeReady) Prepare(Now);return !bDone;
        }
        if (!Identity()) { Finish(false,TEXT("Original living world/controller/pioneer identity changed; no state repaired."));return false; }
        MaximumPioneerDisplacement=FMath::Max(MaximumPioneerDisplacement,FVector::Distance(Pioneer->GetActorLocation(),OriginalLocation));
        const bool NeedsActive=Phase==EPhase::Active || Phase==EPhase::CaptureActive || Phase==EPhase::AwaitAutosaveApproval || Phase==EPhase::Saving || Phase==EPhase::Completed || Phase==EPhase::CaptureCompleted || Phase==EPhase::FreshFlight;
        if (NeedsActive && Drone.IsValid() && Drone->IsFlightArmed()) bRemainedDisarmed=false;
        if (NeedsActive && !Active()) { Finish(false,TEXT("Original FPV session, pioneer hold or active resources ended before native save workflow completed."));return false; }
        if (NeedsActive && IsArmedHoverRun()) {
            FString Error;
            if (!Hover.Tick(DeltaSeconds,Error)) { Finish(false,TEXT("Guarded powered autosave hover failed: ")+Error);return false; }
        }
        if (Phase==EPhase::Before && Settled(Now)) { if (bCapture) { if (Capture(BeforeCapture,TEXT("Save-Before.png"),Now)) Phase=EPhase::CaptureBefore; }else Launch(Now,EPhase::Active); }
        else if (Phase==EPhase::CaptureBefore) { if (!Restored()) { Finish(false,TEXT("Native baseline changed during before capture."));return false; }if (Captured(BeforeCapture,Now)) Launch(Now,EPhase::Active); }
        else if (Phase==EPhase::Active && Settled(Now) && (!IsArmedHoverRun() || Hover.IsPoweredHovering())) { Record(TEXT("active"));if (bCapture) { if (Capture(ActiveCapture,TEXT("Save-Active.png"),Now)) Phase=EPhase::CaptureActive; }else RequestSave(Now); }
        else if (Phase==EPhase::CaptureActive) { if (Captured(ActiveCapture,Now)) RequestSave(Now); }
        else if (Phase==EPhase::AwaitAutosaveApproval) { if (AutoApproval()) ArmAutoTimer(Now); }
        else if (Phase==EPhase::Saving) { if (bAutosave && bAutoArmed && AutoQueueCount==0 && AutoLastWaitingFrame!=GFrameCounter) { AutoLastWaitingFrame=GFrameCounter;++AutoWaitingActiveSamples; }if (Save->HasSaveGameOperationPending()) { bPendingObserved=true;if (LastPendingFrame!=GFrameCounter) { LastPendingFrame=GFrameCounter;++PendingActiveSamples; } }LastReason=TEXT("Waiting for positive native asynchronous completion while original session stays active."); }
        else if (Phase==EPhase::Completed) {
            if (bAutosave) ++AutoAfterSuccessSamples;else ++PostCallbackActiveSamples;if (!Save->HasSaveGameOperationPending() && Settled(Now) && OutputVerified()) { bOutputVerified=true;bActiveThroughCompletion=Active() && ContentMatches();Record(TEXT("saveCompleted"));
                if (!bActiveThroughCompletion || !WriteManifest(true)) { Finish(false,TEXT("Active completion/content or manifest finalization failed."));return false; }
                if (bCapture) { if (Capture(CompletedCapture,TEXT("Save-Completed.png"),Now)) Phase=EPhase::CaptureCompleted; }else ReturnFromSavedFlight(Now);
            }
        }
        else if (Phase==EPhase::CaptureCompleted) { if (Captured(CompletedCapture,Now)) { if (!Active() || Save->HasSaveGameOperationPending()) { Finish(false,TEXT("Actual completion state changed during retained active capture."));return false; }ReturnFromSavedFlight(Now); } }
        else if (Phase==EPhase::Return && Settled(Now)) {
            bReturned=Restored();if (!bReturned) { LastReason=TEXT("Waiting for ordinary post-save pioneer return.");return true; }bNoTransient=NoTransientResources();bNoLeases=NoLeases();Record(TEXT("returned"));bCaptures=!bCapture || (BeforeCapture.bDecoded && ActiveCapture.bDecoded && CompletedCapture.bDecoded);
            const bool Completion=bAutosave?AutoSchedulingVerified():bRequested && bCallback && bCallbackSuccess && CallbackCount==1 && bActiveAtRequest && bActiveAtCallback;
            // Final diagnostic provenance includes any naturally late countdown
            // observation; it never rewrites the saved world or seeds state.
            if (bAutosave && !WriteManifest(true)) { Finish(false,TEXT("Could not finalize exact autosave diagnostic provenance."));return false; }
            Finish(bBaseline && Completion && bActiveThroughCompletion && bOutputVerified && bNoTransient && bNoLeases && bCaptures && ContentMatches(),IsArmedHoverRun()?
                TEXT("Native autosave queued and wrote during continuous powered hover while the pioneer stayed held. Exact output/header and ordinary return verified. Reload process is required separately."):
                TEXT("Native save completed while same disarmed FPV/pioneer hold stayed active; exact output/header verified and ordinary return restored native gameplay. Reload process is required separately."));
        }
        else if (Phase==EPhase::Reloaded && Settled(Now)) { if (!VerifyReload()) { Finish(false,TEXT("Actual saved-output reload state changed before capture."));return false; }if (bCapture) { if (Capture(ReloadedCapture,TEXT("Save-Reloaded.png"),Now)) Phase=EPhase::CaptureReloaded; }else StartFresh(Now); }
        else if (Phase==EPhase::CaptureReloaded) { if (!VerifyReload()) { Finish(false,TEXT("Actual native saved-output state changed during reload capture."));return false; }if (Captured(ReloadedCapture,Now)) StartFresh(Now); }
        else if (Phase==EPhase::HangarOpened && Settled(Now)) { if (!Hangar->IsPanelOpen() || !InvokeF6() || Hangar->IsPanelOpen()) { Finish(false,TEXT("Actual rebound F6 failed to close owned hangar."));return false; }bOwnedPanel=false;PhaseTo(EPhase::HangarClosed,Now); }
        else if (Phase==EPhase::HangarClosed && Settled(Now)) { if (!VerifyReload()) { Finish(false,TEXT("Native reload baseline changed after normal hangar close."));return false; }bHangarRebound=true;Launch(Now,EPhase::FreshFlight); }
        else if (Phase==EPhase::FreshFlight && Settled(Now)) { Drone->ExitDrone();PhaseTo(EPhase::FreshReturn,Now); }
        else if (Phase==EPhase::FreshReturn && Settled(Now)) { if (!Restored() || !VerifyReload()) { LastReason=TEXT("Waiting for fresh ordinary post-reload return.");return true; }bFresh=true;bReturned=true;Record(TEXT("freshReturn"));if (bCapture) { if (Capture(FreshCapture,TEXT("Save-FreshReturn.png"),Now)) Phase=EPhase::CaptureFreshReturn; }else CompleteReload(); }
        else if (Phase==EPhase::CaptureFreshReturn) { if (!Restored() || !VerifyReload()) { Finish(false,TEXT("Actual fresh-return state changed during final capture."));return false; }if (Captured(FreshCapture,Now)) CompleteReload(); }
        return !bDone;
    }
    void CompleteReload() { bCaptures=!bCapture || (ReloadedCapture.bDecoded && FreshCapture.bDecoded);bNoTransient=NoTransientResources();bNoLeases=NoLeases();Finish(bBaseline && bReturned && bNoTransient && bNoLeases && bHangarRebound && bFresh && bCaptures && VerifyReload(),TEXT("Separate native output reload restored supported living pioneer, native view/HUD/input/mesh, inventory slots/equipment selection and progression; no transient FPV resources restored and real F6/fresh disarmed launch-return passed. No second save or repair.")); }
};
TWeakPtr<FRun> ActiveRun;
}

void FFactoryFPVShippingSaveQA::Begin(UWorld* World,FOutputDevice& Output)
{
    using namespace FactoryFPVShippingSaveQAInternal;FString Mode,Name;FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQASave="),Mode);FParse::Value(FCommandLine::Get(),TEXT("FactoryFPVQASaveName="),Name);Mode.ToLowerInline();
    const bool Auto=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAAutosave"));
    const bool Kit=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAKitPersistence"));
    const bool OverwriteSlot=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAOverwriteSlot"));
    const bool ArmedHover=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAArmedHover"));
    const bool ArmedHoverAllowed=Auto && !Kit && !OverwriteSlot && FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")) && FParse::Param(FCommandLine::Get(),TEXT("SavesUseProjectSavedDir"));
    const bool NameAllowed=Kit?Name==TEXT("FPV-QA-KitRoundtrip"):(Auto?(Mode==TEXT("save")?Name==TEXT("FPV-QA-Autosave"):!Name.IsEmpty() && Name.Contains(TEXT("_autosave_")) && FPaths::GetCleanFilename(Name)==Name):Name==OutputName);
    if (!Guard() || !World || World->GetNetMode()!=NM_Standalone || (Mode!=TEXT("save") && Mode!=TEXT("reload")) || !NameAllowed || (Auto && Kit) || (OverwriteSlot && !Auto) || (ArmedHover && !ArmedHoverAllowed) || (Kit && Mode==TEXT("save") && !FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQAWorkshopUI"))) || (Auto && FParse::Param(FCommandLine::Get(),TEXT("NoAutoSave")))) { Marker(Mode.IsEmpty()?TEXT("invalid"):Mode,TEXT("FAIL"),TEXT("Explicit automation, disposable standalone world, save|reload and designated approved output required. Armed hover requires offscreen empty-slot autosave. Kit persistence requires actual Workshop UI without autosave."),FString(),Auto,Kit);Output.Log(TEXT("Save QA guard/mode/name refused."));if (FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVAutomation"))) FPlatformMisc::RequestExitWithStatus(false,1);return; }
    if (auto Existing=ActiveRun.Pin();Existing && !Existing->bDone) { Output.Log(TEXT("Save QA is already active."));return; }
    auto Run=MakeShared<FRun>();Run->World=World;Run->Mode=Mode;Run->SaveName=Name;Run->bAutosave=Auto;Run->bArmedHover=ArmedHover;Run->bOverwriteSlot=OverwriteSlot;Run->bKitPersistence=Kit;Run->bCapture=FParse::Param(FCommandLine::Get(),TEXT("FactoryFPVQACapture"));Run->Deadline=FPlatformTime::Seconds()+60.;ActiveRun=Run;
    Marker(Mode,TEXT("PENDING"),TEXT("Waiting for exact designated native save/startup/pioneer state; no save requested before strict live ownership/path baseline."),FString(),Auto,Kit);
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float DeltaSeconds){return Run->Tick(DeltaSeconds);}),Run->IsArmedHoverRun()?0.f:.1f);
    // Both genuine splash observers begin in the same startup window. The
    // parent observes without taking a baseline while the child owns its UI.
    if (Kit && Mode==TEXT("save")) Run->StartKitPreparation();
}
