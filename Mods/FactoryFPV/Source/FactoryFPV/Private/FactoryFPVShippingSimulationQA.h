#pragma once

#include "CoreMinimal.h"

class AFactoryFPVDrone;
class UBoxComponent;
struct FInputActionUnifiedDelegate;

struct FFactoryFPVShippingSimulationEvidence
{
    bool bBatteryDrain=false,bBatteryWarnings=false,bBatteryCutoff=false,bBatteryRecovery=false;
    bool bImpact=false,bDamageRecovery=false,bSettingsRestored=false;
    int32 BatteryCutoffSteps=0;
    double ModerateIntegrity=0.,TerminalIntegrity=0.,Recharge=0.,Repair=0.;
    FString Json(bool bEnabled) const;
};

class FFactoryFPVShippingSimulationQA final
{
public:
    static bool Run(AFactoryFPVDrone& Drone,UBoxComponent& OwnFloor,FInputActionUnifiedDelegate& RecoverKey,
        FFactoryFPVShippingSimulationEvidence& Evidence,FString& Error);
};
