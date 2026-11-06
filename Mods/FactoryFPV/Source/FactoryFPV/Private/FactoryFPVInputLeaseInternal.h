#pragma once

#include "CoreMinimal.h"

class AController;
class APlayerController;
struct FFactoryFPVInputLeaseControllerState;

enum class EFactoryFPVInputResetAxes : uint8
{
    Move = 1,
    Look = 2,
    Both = 3
};

/**
 * BEFORE-reset observer. Surrender valid contributions before native code can
 * zero or preserve its counters. Nested scopes cannot surrender a token twice.
 * Editor adapters use this around real engine Super resets; production uses the
 * six verified Win64 reset-export hooks installed at first FG controller
 * acquisition (FG move/all, base Controller move/look/all, PlayerController all).
 */
class FFactoryFPVInputResetScope final
{
public:
    FFactoryFPVInputResetScope(AController* Controller, EFactoryFPVInputResetAxes Axes);
    ~FFactoryFPVInputResetScope();
    FFactoryFPVInputResetScope(const FFactoryFPVInputResetScope&) = delete;
    FFactoryFPVInputResetScope& operator=(const FFactoryFPVInputResetScope&) = delete;

private:
    TSharedPtr<FFactoryFPVInputLeaseControllerState> Record;
};

struct FFactoryFPVInputLeaseDiagnostics
{
    bool bNativeHooksReady = false;
    int32 ActiveLeases = 0;
    int32 OwnedMoveContributions = 0;
    int32 OwnedLookContributions = 0;
    int32 ResetDepth = 0;
    // Move/Look count scopes including Both. All counts Both scopes only.
    // Nested scopes count separately, while each contribution withdraws once.
    uint64 MoveResetEvents = 0;
    uint64 LookResetEvents = 0;
    uint64 AllResetEvents = 0;
};

namespace FactoryFPVInputLease
{
    void Startup();
    void Shutdown();
    FFactoryFPVInputLeaseDiagnostics GetDiagnostics(APlayerController* Controller);
}
