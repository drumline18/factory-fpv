#pragma once

#include "CoreMinimal.h"

class APlayerController;
struct FFactoryFPVInputLeaseState;

/**
 * One local owner's balanced move/look input holds. Native reset observation
 * withdraws our contributions before a reset; Renew then adds fresh holds even
 * when another owner already ignores input. All operations run on the game thread.
 *
 * This tracks public reset boundaries, not arbitrary unbalanced foreign calls to
 * SetIgnore*Input(false). UE's anonymous counters cannot identify those owners.
 */
class FACTORYFPV_API FFactoryFPVInputLease final
{
public:
    FFactoryFPVInputLease();
    ~FFactoryFPVInputLease();
    FFactoryFPVInputLease(const FFactoryFPVInputLease&) = delete;
    FFactoryFPVInputLease& operator=(const FFactoryFPVInputLease&) = delete;
    FFactoryFPVInputLease(FFactoryFPVInputLease&&) = delete;
    FFactoryFPVInputLease& operator=(FFactoryFPVInputLease&&) = delete;

    // A failed ownership attempt releases its partial holds, including when
    // retrying the same controller. Invalid targets/backend preflight failure
    // preserve a previously acquired lease without attempting a new ownership.
    bool Acquire(APlayerController* Controller);
    bool Renew();
    void Release();
    bool IsActive() const;
    bool OwnsMove() const;
    bool OwnsLook() const;

private:
    // Stable storage remains pinned while a reset or public setter reenters us.
    TSharedPtr<FFactoryFPVInputLeaseState> State;
};
