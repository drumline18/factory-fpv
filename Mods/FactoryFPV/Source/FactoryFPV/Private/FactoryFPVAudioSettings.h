#pragma once
#include "CoreMinimal.h"
#include "Misc/Variant.h"

class UFGGameUserSettings;
class UFGUserSettingApplyType;

// All UObject/setting reads stay on the game thread; the mixer receives a scalar.
namespace FactoryFPVAudioSettings {
    double Gain(const FVariant& Master,const FVariant& Effects);
    using FApplyMap=TMap<FString,TObjectPtr<UFGUserSettingApplyType>>;
    struct FVolumeSource {
        FString Key;
        TWeakObjectPtr<UFGUserSettingApplyType> Apply;
    };
    // Production and tests share raw-map discovery, identity checks and scan
    // throttling. Callbacks expose actual map keys, not CVar identifiers.
    class FGameVolumeCache {
    public:
        double Read(UFGGameUserSettings* Manager,bool bInitialized,double Now,
            TFunctionRef<void(FApplyMap&)> Snapshot,
            TFunctionRef<UFGUserSettingApplyType*(const FString&)> FindActualKey);
        FString Status() const;
        const FVolumeSource& MasterSource() const { return Master; }
        const FVolumeSource& EffectsSource() const { return Effects; }
    private:
        TWeakObjectPtr<UFGGameUserSettings> Owner;
        FVolumeSource Master,Effects;
        double NextScanTime=0;
        double LastMasterGain=1;
        double LastEffectsGain=1;
        bool bManagerInitialized=false;
    };
    double GameGain();
    FString GameGainStatus();
}
