#include "FactoryFPVAudioSettings.h"
#include "FGGameUserSettings.h"
#include "Settings/FGUserSetting.h"
#include "Settings/FGUserSettingApplyType.h"

namespace {
double UnitGain(const FVariant& Value,double Fallback=1.) {
    double Gain=Fallback;
    if (Value.GetType()==EVariantTypes::Float) Gain=Value.GetValue<float>();
    else if (Value.GetType()==EVariantTypes::Double) Gain=Value.GetValue<double>();
    return FMath::IsFinite(Gain) ? FMath::Clamp(Gain,0.,1.) : Fallback;
}
enum class EVolume { Master,Effects };
int MatchVolume(const UFGUserSetting* Setting,EVolume Volume) {
    if (!IsValid(Setting)) return 0;
    static const FName MasterName(TEXT("US_MasterVolume")),EffectsName(TEXT("US_EffectsVolume"));
    const FName Name=Volume==EVolume::Master ? MasterName : EffectsName;
    if (Setting->GetFName()==Name) return 2;
    // StrId is metadata for discovery, not a safe manager lookup key.
    const TCHAR* CVar=Volume==EVolume::Master ? TEXT("RTPC.Master_Bus_Volume") : TEXT("RTPC.SFX_Bus_Volume");
    return Setting->StrId.Equals(CVar,ESearchCase::IgnoreCase) ? 1 : 0;
}
FactoryFPVAudioSettings::FGameVolumeCache GameVolumeCache;
}
double FactoryFPVAudioSettings::Gain(const FVariant& Master,const FVariant& Effects) {
    // The game stores 0..1 even though the sliders display 0..100.
    return UnitGain(Master)*UnitGain(Effects);
}

double FactoryFPVAudioSettings::FGameVolumeCache::Read(UFGGameUserSettings* Manager,bool bInitialized,double Now,
    TFunctionRef<void(FApplyMap&)> Snapshot,
    TFunctionRef<UFGUserSettingApplyType*(const FString&)> FindActualKey) {
    check(IsInGameThread());
    const bool bManagerAvailable=IsValid(Manager);
    if (!bManagerAvailable || Owner.Get()!=Manager) {
        Owner=bManagerAvailable ? Manager : nullptr;
        Master={};Effects={};NextScanTime=0;
        LastMasterGain=1.;LastEffectsGain=1.;
    }
    bManagerInitialized=bManagerAvailable && bInitialized;
    if (!bManagerInitialized) {
        Master={};Effects={};NextScanTime=0;
        return LastMasterGain*LastEffectsGain;
    }
    auto IsCurrent=[&](const FVolumeSource& Source,EVolume Volume) {
        const auto* Apply=Source.Apply.Get();
        return Apply && !Source.Key.IsEmpty() && FindActualKey(Source.Key)==Apply && MatchVolume(Apply->GetUserSetting(),Volume)>0;
    };
    if (!IsCurrent(Master,EVolume::Master)) Master={};
    if (!IsCurrent(Effects,EVolume::Effects)) Effects={};
    if ((!Master.Apply.IsValid() || !Effects.Apply.IsValid()) && Now>=NextScanTime) {
        NextScanTime=Now+.5;
        FApplyMap Settings;
        Snapshot(Settings);
        FVolumeSource FoundMaster,FoundEffects;
        int MasterPriority=0,EffectsPriority=0;
        auto Consider=[](FVolumeSource& Source,int& Priority,const FString& Key,UFGUserSettingApplyType* Apply,int Match) {
            if (Match>0 && (Match>Priority || (Match==Priority && Key.Compare(Source.Key,ESearchCase::CaseSensitive)<0))) {
                Source.Key=Key;Source.Apply=Apply;Priority=Match;
            }
        };
        for (const auto& Entry:Settings) {
            auto* Apply=Entry.Value.Get();
            if (Entry.Key.IsEmpty() || !IsValid(Apply)) continue;
            const auto* Setting=Apply->GetUserSetting();
            Consider(FoundMaster,MasterPriority,Entry.Key,Apply,MatchVolume(Setting,EVolume::Master));
            Consider(FoundEffects,EffectsPriority,Entry.Key,Apply,MatchVolume(Setting,EVolume::Effects));
        }
        Master=MoveTemp(FoundMaster);Effects=MoveTemp(FoundEffects);
        // A snapshot entry must still be the live object at its actual key.
        if (!IsCurrent(Master,EVolume::Master)) Master={};
        if (!IsCurrent(Effects,EVolume::Effects)) Effects={};
    }
    // Keep the last applied level per channel while the same manager rebuilds its settings.
    if (const auto* MasterApply=Master.Apply.Get()) {
        LastMasterGain=UnitGain(MasterApply->GetAppliedValue(),LastMasterGain);
    }
    if (const auto* EffectsApply=Effects.Apply.Get()) {
        LastEffectsGain=UnitGain(EffectsApply->GetAppliedValue(),LastEffectsGain);
    }
    return LastMasterGain*LastEffectsGain;
}

FString FactoryFPVAudioSettings::FGameVolumeCache::Status() const {
    check(IsInGameThread());
    const TCHAR* State=!Owner.IsValid() ? TEXT("manager unavailable") :
        (!bManagerInitialized ? TEXT("waiting for initialization") :
        (Master.Apply.IsValid() && Effects.Apply.IsValid() ? TEXT("resolved") : TEXT("waiting for volume settings")));
    return FString::Printf(TEXT("game-volume=%s gain=%.3f master-key=%s master-apply=%s effects-key=%s effects-apply=%s"),
        State,LastMasterGain*LastEffectsGain,Master.Key.IsEmpty() ? TEXT("unresolved") : *Master.Key,*GetPathNameSafe(Master.Apply.Get()),
        Effects.Key.IsEmpty() ? TEXT("unresolved") : *Effects.Key,*GetPathNameSafe(Effects.Apply.Get()));
}
double FactoryFPVAudioSettings::GameGain() {
    check(IsInGameThread());
    auto* Manager=UFGGameUserSettings::GetFGGameUserSettings();
    return GameVolumeCache.Read(Manager,IsValid(Manager) && Manager->HasInitialized(),FPlatformTime::Seconds(),
        [Manager](FApplyMap& Settings) { Manager->GetAllUserSettingsMap(Settings); },
        [Manager](const FString& Key) { return Manager->FindUserSetting(Key); });
}

FString FactoryFPVAudioSettings::GameGainStatus() {
    GameGain();
    return GameVolumeCache.Status();
}
