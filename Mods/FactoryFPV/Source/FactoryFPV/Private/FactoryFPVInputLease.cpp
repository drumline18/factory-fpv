#include "FactoryFPVInputLease.h"
#include "FactoryFPVInputLeaseInternal.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#if !WITH_EDITOR
#include "FGPlayerController.h"
#if PLATFORM_WINDOWS
#include "HAL/PlatformProcess.h"
#include "Modules/ModuleManager.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "MinHook.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif
#endif

DEFINE_LOG_CATEGORY_STATIC(LogFactoryFPVInputLease, Log, All);

struct FFactoryFPVInputLeaseState
{
    TWeakObjectPtr<APlayerController> Controller;
    TSharedPtr<FFactoryFPVInputLeaseControllerState> Record;
    bool bActive = false;
    bool bOwnsMove = false;
    bool bOwnsLook = false;
    bool bReleasing = false;
    bool bRenewing = false;
};

struct FFactoryFPVInputLeaseControllerState
{
    TWeakObjectPtr<APlayerController> Controller;
    TArray<TWeakPtr<FFactoryFPVInputLeaseState>> Leases;
    int32 ResetDepth = 0;
    uint64 MoveResetEvents = 0;
    uint64 LookResetEvents = 0;
    uint64 AllResetEvents = 0;
};

namespace
{
    TMap<TWeakObjectPtr<APlayerController>, TSharedPtr<FFactoryFPVInputLeaseControllerState>> GInputLeaseRecords;
    bool bInputLeaseStarted = false;
    bool bInputLeaseMayAutoStart = true;
    bool bInputLeaseShuttingDown = false;
    bool bInputLeaseHooksReady = false;
#if !WITH_EDITOR && PLATFORM_WINDOWS
    // These wrappers are original code. The only hook dependency is the pinned
    // BSD-2-Clause MinHook library; no SML hook templates are instantiated.
    enum class EInputResetTarget : int32
    {
        FGMoveReset, FGAllReset, ControllerMoveReset, ControllerLookReset,
        ControllerAllReset, PlayerControllerAllReset
    };

    void InputLeaseFGMoveReset(AFGPlayerController* Self);
    void InputLeaseFGAllReset(AFGPlayerController* Self);
    void InputLeaseControllerMoveReset(AController* Self);
    void InputLeaseControllerLookReset(AController* Self);
    void InputLeaseControllerAllReset(AController* Self);
    void InputLeasePlayerControllerAllReset(APlayerController* Self);

    struct FInputLeaseNativeResetTarget
    {
        const TCHAR* Module;
        const TCHAR* Export;
        void* Detour;
        void* Target = nullptr;
        void* Original = nullptr;
        bool bCreated = false;
        bool bEnabled = false;
    };

    // Names verified against the installed matching Win64 Shipping DLL exports.
    // Resolve exports directly, never member-pointer encodings or vtable slots.
    FInputLeaseNativeResetTarget InputLeaseResetTargets[] =
    {
        {TEXT("FactoryGame"), TEXT("?ResetIgnoreMoveInput@AFGPlayerController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeaseFGMoveReset)},
        {TEXT("FactoryGame"), TEXT("?ResetIgnoreInputFlags@AFGPlayerController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeaseFGAllReset)},
        {TEXT("Engine"), TEXT("?ResetIgnoreMoveInput@AController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeaseControllerMoveReset)},
        {TEXT("Engine"), TEXT("?ResetIgnoreLookInput@AController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeaseControllerLookReset)},
        {TEXT("Engine"), TEXT("?ResetIgnoreInputFlags@AController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeaseControllerAllReset)},
        {TEXT("Engine"), TEXT("?ResetIgnoreInputFlags@APlayerController@@UEAAXXZ"), reinterpret_cast<void*>(&InputLeasePlayerControllerAllReset)}
    };
    bool bInputLeaseOwnsMinHookInitialization = false;

    // Win64's exported non-static member implementations take their typed this
    // pointer in RCX. Each trampoline preserves that exact reset signature.
    void InputLeaseFGMoveReset(AFGPlayerController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Move);
        reinterpret_cast<void(__fastcall*)(AFGPlayerController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::FGMoveReset)].Original)(Self);
    }
    void InputLeaseFGAllReset(AFGPlayerController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Both);
        reinterpret_cast<void(__fastcall*)(AFGPlayerController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::FGAllReset)].Original)(Self);
    }
    void InputLeaseControllerMoveReset(AController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Move);
        reinterpret_cast<void(__fastcall*)(AController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::ControllerMoveReset)].Original)(Self);
    }
    void InputLeaseControllerLookReset(AController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Look);
        reinterpret_cast<void(__fastcall*)(AController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::ControllerLookReset)].Original)(Self);
    }
    void InputLeaseControllerAllReset(AController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Both);
        reinterpret_cast<void(__fastcall*)(AController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::ControllerAllReset)].Original)(Self);
    }
    void InputLeasePlayerControllerAllReset(APlayerController* Self)
    {
        FFactoryFPVInputResetScope Reset(Self, EFactoryFPVInputResetAxes::Both);
        reinterpret_cast<void(__fastcall*)(APlayerController*)>(InputLeaseResetTargets[static_cast<int32>(EInputResetTarget::PlayerControllerAllReset)].Original)(Self);
    }

    bool RemoveInputLeaseNativeHooks()
    {
        bInputLeaseHooksReady = false;
        bool bRemovedAllOwnedTargets = true;
        for (int32 Index = UE_ARRAY_COUNT(InputLeaseResetTargets) - 1; Index >= 0; --Index)
        {
            FInputLeaseNativeResetTarget& Hook = InputLeaseResetTargets[Index];
            if (!Hook.bCreated) continue;
            if (Hook.bEnabled)
            {
                const MH_STATUS Status = MH_DisableHook(Hook.Target);
                if (Status != MH_OK && Status != MH_ERROR_DISABLED)
                {
                    UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot disable owned reset hook %s: %d"), Hook.Export, static_cast<int32>(Status));
                    bRemovedAllOwnedTargets = false;
                    continue;
                }
                Hook.bEnabled = false;
            }
            const MH_STATUS Status = MH_RemoveHook(Hook.Target);
            if (Status != MH_OK && Status != MH_ERROR_NOT_CREATED)
            {
                UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot remove owned reset hook %s: %d"), Hook.Export, static_cast<int32>(Status));
                bRemovedAllOwnedTargets = false;
                continue;
            }
            Hook.bCreated = false;
            Hook.Original = nullptr;
            Hook.Target = nullptr;
        }
        // Never uninitialize somebody else's MinHook session. Our static library
        // has no other consumers; even our own session ends only after all six
        // individually owned hooks have been removed successfully.
        if (bRemovedAllOwnedTargets && bInputLeaseOwnsMinHookInitialization)
        {
            const MH_STATUS Status = MH_Uninitialize();
            if (Status != MH_OK && Status != MH_ERROR_NOT_INITIALIZED)
            {
                UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot end owned MinHook session: %d"), static_cast<int32>(Status));
                return false;
            }
            bInputLeaseOwnsMinHookInitialization = false;
        }
        return bRemovedAllOwnedTargets;
    }

    bool InstallInputLeaseNativeHooks()
    {
        // Failed earlier installs must leave no enabled partial backend before
        // resolving again. Remove only entries this backend actually created.
        if (!RemoveInputLeaseNativeHooks()) return false;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(InputLeaseResetTargets); ++Index)
        {
            FInputLeaseNativeResetTarget& Hook = InputLeaseResetTargets[Index];
            const FString Filename = FModuleManager::Get().GetModuleFilename(FName(Hook.Module));
            HMODULE Module = Filename.IsEmpty() ? nullptr : GetModuleHandleW(*Filename);
            Hook.Target = Module ? FPlatformProcess::GetDllExport(reinterpret_cast<void*>(Module), Hook.Export) : nullptr;
            if (!Hook.Target)
            {
                UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Missing loaded reset export %s in %s (%s); no input lease acquired"), Hook.Export, Hook.Module, *Filename);
                return false;
            }
            for (int32 Previous = 0; Previous < Index; ++Previous)
                if (InputLeaseResetTargets[Previous].Target == Hook.Target)
                {
                    UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Reset exports unexpectedly alias: %s / %s; no hooks installed"), InputLeaseResetTargets[Previous].Export, Hook.Export);
                    return false;
                }
        }
        const MH_STATUS InitStatus = MH_Initialize();
        if (InitStatus != MH_OK && InitStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot initialize input reset hooks: %d"), static_cast<int32>(InitStatus));
            return false;
        }
        bInputLeaseOwnsMinHookInitialization = InitStatus == MH_OK;
        for (FInputLeaseNativeResetTarget& Hook : InputLeaseResetTargets)
        {
            const MH_STATUS Status = MH_CreateHook(Hook.Target, Hook.Detour, &Hook.Original);
            if (Status != MH_OK)
            {
                UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot create input reset hook %s: %d"), Hook.Export, static_cast<int32>(Status));
                RemoveInputLeaseNativeHooks();
                return false;
            }
            Hook.bCreated = true;
        }
        for (FInputLeaseNativeResetTarget& Hook : InputLeaseResetTargets)
        {
            const MH_STATUS Status = MH_EnableHook(Hook.Target);
            if (Status != MH_OK)
            {
                UE_LOG(LogFactoryFPVInputLease, Error, TEXT("Cannot enable input reset hook %s: %d"), Hook.Export, static_cast<int32>(Status));
                RemoveInputLeaseNativeHooks();
                return false;
            }
            Hook.bEnabled = true;
            UE_LOG(LogFactoryFPVInputLease, Display, TEXT("Input lease reset hook enabled: %s!%s target=%p"), Hook.Module, Hook.Export, Hook.Target);
        }
        bInputLeaseHooksReady = true;
        UE_LOG(LogFactoryFPVInputLease, Display, TEXT("Input lease reset observer ready: six verified Win64 exports"));
        return true;
    }
#endif

    bool IsUsableInputLeaseController(APlayerController* Controller)
    {
        return IsValid(Controller) && !Controller->IsTemplate()
            && !Controller->IsActorBeingDestroyed() && Controller->GetWorld()
            && !Controller->GetWorld()->bIsTearingDown;
    }

    bool CanReleaseInputLeaseController(APlayerController* Controller)
    {
        // Actor/world EndPlay is a normal release boundary, not UObject
        // destruction. A surviving controller's public counter setters remain
        // usable there, and dropping our tokens would leak holds across travel.
        return IsValid(Controller) && !Controller->IsTemplate()
            && !Controller->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed);
    }

    bool IncludesInputResetAxis(EFactoryFPVInputResetAxes Axes, EFactoryFPVInputResetAxes Axis)
    {
        return (static_cast<uint8>(Axes) & static_cast<uint8>(Axis)) != 0;
    }

    void PruneInputLeaseRecords()
    {
        for (auto It = GInputLeaseRecords.CreateIterator(); It; ++It)
        {
            if (!It.Key().IsValid())
            {
                It.RemoveCurrent();
                continue;
            }
            It.Value()->Leases.RemoveAll([](const TWeakPtr<FFactoryFPVInputLeaseState>& Lease)
            {
                const TSharedPtr<FFactoryFPVInputLeaseState> Pinned = Lease.Pin();
                return !Pinned.IsValid() || !Pinned->bActive;
            });
        }
    }

    // Each ownership flag is cleared immediately before its setter. A nested
    // reset may withdraw the other axis, so never cache both decrement decisions.
    void WithdrawInputLease(const TSharedPtr<FFactoryFPVInputLeaseState>& Lease,
                            EFactoryFPVInputResetAxes Axes)
    {
        if (IncludesInputResetAxis(Axes, EFactoryFPVInputResetAxes::Move) && Lease->bOwnsMove)
        {
            Lease->bOwnsMove = false;
            if (CanReleaseInputLeaseController(Lease->Controller.Get()))
                Lease->Controller->SetIgnoreMoveInput(false);
        }
        if (IncludesInputResetAxis(Axes, EFactoryFPVInputResetAxes::Look) && Lease->bOwnsLook)
        {
            Lease->bOwnsLook = false;
            if (CanReleaseInputLeaseController(Lease->Controller.Get()))
                Lease->Controller->SetIgnoreLookInput(false);
        }
    }

    void ReleaseInputLease(const TSharedPtr<FFactoryFPVInputLeaseState>& Lease)
    {
        if (!Lease.IsValid() || Lease->bReleasing) return;
        // Pin the record across arbitrary reentrant native setters.
        const TSharedPtr<FFactoryFPVInputLeaseControllerState> Record = Lease->Record;
        TGuardValue<bool> Releasing(Lease->bReleasing, true);
        Lease->bActive = false;
        WithdrawInputLease(Lease, EFactoryFPVInputResetAxes::Both);
        Lease->Controller.Reset();
        Lease->Record.Reset();
    }

    bool EnsureInputLeaseHooks(APlayerController* Controller)
    {
#if !WITH_EDITOR
        if (!Cast<AFGPlayerController>(Controller)) return false;
#if PLATFORM_WINDOWS
        return bInputLeaseHooksReady || InstallInputLeaseNativeHooks();
#else
        return false;
#endif
#else
        // Generated FactoryGame methods are stubs (and may share addresses).
        // Editor tests wrap real engine reset implementations with ResetScope.
        return true;
#endif
    }
}

FFactoryFPVInputLease::FFactoryFPVInputLease()
    : State(MakeShared<FFactoryFPVInputLeaseState>())
{
}

FFactoryFPVInputLease::~FFactoryFPVInputLease()
{
    Release();
}

bool FFactoryFPVInputLease::Acquire(APlayerController* Controller)
{
    if (!IsInGameThread() || !IsUsableInputLeaseController(Controller) || bInputLeaseShuttingDown)
        return false;
    if (State->bReleasing) return false; // The outer Release already owns cleanup.
    if (State->bRenewing || (State->Record.IsValid() && State->Record->ResetDepth > 0))
    {
        // A valid acquisition attempted during a reset/renewal cannot commit.
        // Cancel the old/outer transaction instead of leaving a surviving axis.
        Release();
        return false;
    }
    if (!bInputLeaseStarted)
    {
        if (!bInputLeaseMayAutoStart) return false;
        FactoryFPVInputLease::Startup();
    }
    if (!EnsureInputLeaseHooks(Controller)) return false;
    if (State->bActive && State->Controller.Get() == Controller)
    {
        if (Renew()) return true;
        Release();
        return false;
    }
    Release();
    PruneInputLeaseRecords();
    TSharedPtr<FFactoryFPVInputLeaseControllerState>& Record = GInputLeaseRecords.FindOrAdd(Controller);
    if (!Record.IsValid())
    {
        Record = MakeShared<FFactoryFPVInputLeaseControllerState>();
        Record->Controller = Controller;
    }
    State->Controller = Controller;
    State->Record = Record;
    State->bActive = true;
    Record->Leases.Add(State);
    if (Renew()) return true;
    Release();
    return false;
}

bool FFactoryFPVInputLease::Renew()
{
    const TSharedPtr<FFactoryFPVInputLeaseState> Lease = State;
    if (!IsInGameThread() || !Lease->bActive || bInputLeaseShuttingDown
        || Lease->bReleasing || Lease->bRenewing) return false;
    TGuardValue<bool> Renewing(Lease->bRenewing, true);
    const TSharedPtr<FFactoryFPVInputLeaseControllerState> Record = Lease->Record;
    APlayerController* Controller = Lease->Controller.Get();
    if (!Record.IsValid() || !IsUsableInputLeaseController(Controller))
    {
        ReleaseInputLease(Lease);
        return false;
    }
    if (Record->ResetDepth > 0) return false;
    if (!Lease->bOwnsMove)
    {
        Lease->bOwnsMove = true;
        Controller->SetIgnoreMoveInput(true);
    }
    // A native setter may reenter a reset/release. Do not resurrect that lease or
    // acquire its other axis during the reset's protected interval.
    if (!Lease->bActive || Lease->Controller.Get() != Controller || Record->ResetDepth > 0)
        return false;
    if (!Lease->bOwnsLook)
    {
        Lease->bOwnsLook = true;
        Controller->SetIgnoreLookInput(true);
    }
    return Lease->bActive && Lease->bOwnsMove && Lease->bOwnsLook && Record->ResetDepth == 0;
}

void FFactoryFPVInputLease::Release()
{
    if (!State.IsValid() || (!State->bActive && !State->bOwnsMove && !State->bOwnsLook)) return;
    // Active leases are game-thread resources; inactive value members may be
    // destroyed as part of ordinary object construction/destruction elsewhere.
    check(IsInGameThread());
    ReleaseInputLease(State);
}

bool FFactoryFPVInputLease::IsActive() const
{
    return State->bActive && IsUsableInputLeaseController(State->Controller.Get());
}

bool FFactoryFPVInputLease::OwnsMove() const
{
    return IsActive() && State->bOwnsMove;
}

bool FFactoryFPVInputLease::OwnsLook() const
{
    return IsActive() && State->bOwnsLook;
}

FFactoryFPVInputResetScope::FFactoryFPVInputResetScope(AController* Controller,
                                                     EFactoryFPVInputResetAxes Axes)
{
    if (!IsInGameThread()) return;
    APlayerController* PlayerController = Cast<APlayerController>(Controller);
    const TSharedPtr<FFactoryFPVInputLeaseControllerState>* Found = GInputLeaseRecords.Find(PlayerController);
    if (!Found || !Found->IsValid()) return;
    Record = *Found;
    ++Record->ResetDepth;
    if (IncludesInputResetAxis(Axes, EFactoryFPVInputResetAxes::Move)) ++Record->MoveResetEvents;
    if (IncludesInputResetAxis(Axes, EFactoryFPVInputResetAxes::Look)) ++Record->LookResetEvents;
    if (Axes == EFactoryFPVInputResetAxes::Both) ++Record->AllResetEvents;
    TArray<TSharedPtr<FFactoryFPVInputLeaseState>> PinnedLeases;
    for (const TWeakPtr<FFactoryFPVInputLeaseState>& WeakLease : Record->Leases)
        if (const TSharedPtr<FFactoryFPVInputLeaseState> Lease = WeakLease.Pin()) PinnedLeases.Add(Lease);
    for (const TSharedPtr<FFactoryFPVInputLeaseState>& Lease : PinnedLeases)
        if (Lease->Record == Record) WithdrawInputLease(Lease, Axes);
}

FFactoryFPVInputResetScope::~FFactoryFPVInputResetScope()
{
    if (Record.IsValid())
    {
        check(IsInGameThread());
        check(Record->ResetDepth > 0);
        --Record->ResetDepth;
    }
}

void FactoryFPVInputLease::Startup()
{
    check(IsInGameThread());
    if (bInputLeaseShuttingDown) return;
    bInputLeaseStarted = true;
    bInputLeaseMayAutoStart = true;
    PruneInputLeaseRecords();
}

void FactoryFPVInputLease::Shutdown()
{
    check(IsInGameThread());
    if (bInputLeaseShuttingDown) return;
    bInputLeaseShuttingDown = true;
    bInputLeaseMayAutoStart = false;
    TArray<TSharedPtr<FFactoryFPVInputLeaseState>> PinnedLeases;
    for (const auto& Pair : GInputLeaseRecords)
    {
        checkf(Pair.Value->ResetDepth == 0, TEXT("Input lease shutdown must occur outside native reset callbacks"));
        for (const TWeakPtr<FFactoryFPVInputLeaseState>& WeakLease : Pair.Value->Leases)
            if (const TSharedPtr<FFactoryFPVInputLeaseState> Lease = WeakLease.Pin()) PinnedLeases.Add(Lease);
    }
    for (const TSharedPtr<FFactoryFPVInputLeaseState>& Lease : PinnedLeases) ReleaseInputLease(Lease);
    GInputLeaseRecords.Reset();
#if !WITH_EDITOR && PLATFORM_WINDOWS
    RemoveInputLeaseNativeHooks();
#endif
    bInputLeaseHooksReady = false;
    bInputLeaseStarted = false;
    bInputLeaseShuttingDown = false;
}

FFactoryFPVInputLeaseDiagnostics FactoryFPVInputLease::GetDiagnostics(APlayerController* Controller)
{
    FFactoryFPVInputLeaseDiagnostics Result;
    Result.bNativeHooksReady = bInputLeaseHooksReady;
    if (!IsInGameThread()) return Result;
    const TSharedPtr<FFactoryFPVInputLeaseControllerState>* Found = GInputLeaseRecords.Find(Controller);
    if (!Found || !Found->IsValid()) return Result;
    const TSharedPtr<FFactoryFPVInputLeaseControllerState> Record = *Found;
    Result.ResetDepth = Record->ResetDepth;
    Result.MoveResetEvents = Record->MoveResetEvents;
    Result.LookResetEvents = Record->LookResetEvents;
    Result.AllResetEvents = Record->AllResetEvents;
    for (const TWeakPtr<FFactoryFPVInputLeaseState>& WeakLease : Record->Leases)
    {
        const TSharedPtr<FFactoryFPVInputLeaseState> Lease = WeakLease.Pin();
        if (!Lease.IsValid() || !Lease->bActive || Lease->Record != Record) continue;
        ++Result.ActiveLeases;
        Result.OwnedMoveContributions += Lease->bOwnsMove ? 1 : 0;
        Result.OwnedLookContributions += Lease->bOwnsLook ? 1 : 0;
    }
    return Result;
}
