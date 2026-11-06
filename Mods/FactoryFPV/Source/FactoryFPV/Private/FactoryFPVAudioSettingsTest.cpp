#include "FactoryFPVAudioSettings.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Settings/FGUserSetting.h"
#include "Settings/FGUserSettingApplyType.h"
#include "FGGameUserSettings.h"
#include "UObject/UnrealType.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVGameAudioSettingsTest,
    "FactoryFPV.Audio.GameVolume",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FFactoryFPVGameAudioSettingsTest::RunTest(const FString&) {
    // Check IDs and normalized range against the actual matching game assets.
    for (const FString Name:{TEXT("MasterVolume"),TEXT("EffectsVolume")}) {
        const auto* Setting=LoadObject<UFGUserSetting>(nullptr,*FString::Printf(
            TEXT("/Game/FactoryGame/Settings/OptionsMenu/Audio/US_%s.US_%s"),*Name,*Name));
        if (!TestNotNull(TEXT("Game volume setting asset exists"),Setting)) return false;
        TestEqual(TEXT("Discovery recognizes underlying CVar metadata"),Setting->StrId,
            FString(Name==TEXT("MasterVolume") ? TEXT("RTPC.Master_Bus_Volume") : TEXT("RTPC.SFX_Bus_Volume")));
        const auto* Selector=Cast<UFGUserSetting_Slider>(Setting->ValueSelector);
        if (!TestNotNull(TEXT("Game uses slider selector"),Selector)) return false;
        TestEqual(TEXT("Stored volume is normalized"),Selector->MaxValue,1.f);
        TestEqual(TEXT("Displayed volume is percentage"),Selector->MaxDisplayValue,100.f);
    }
    using FactoryFPVAudioSettings::Gain;
    TestTrue(TEXT("Applied master and effects multiply"),FMath::IsNearlyEqual(Gain(FVariant(.5f),FVariant(.4f)),.2,1.e-6));
    TestEqual(TEXT("Game master mute silences props"),Gain(FVariant(0.f),FVariant(1.f)),0.);
    TestEqual(TEXT("Game effects mute silences props"),Gain(FVariant(1.f),FVariant(0.f)),0.);
    TestEqual(TEXT("Out of range gain cannot amplify"),Gain(FVariant(4.f),FVariant(.25)),.25);
    TestEqual(TEXT("Negative normalized gain clamps to silence"),Gain(FVariant(-1.),FVariant(.25)),0.);
    TestEqual(TEXT("Invalid values fall back safely"),Gain(FVariant(std::numeric_limits<float>::quiet_NaN()),FVariant()),1.);
    TestEqual(TEXT("Infinite gain falls back safely"),Gain(FVariant(std::numeric_limits<double>::infinity()),FVariant(.5f)),.5);
    TestEqual(TEXT("Unexpected variant types fall back safely"),Gain(FVariant(TEXT("not a volume")),FVariant(true)),1.);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVGameAudioDiscoveryTest,
    "FactoryFPV.Audio.AppliedVolumeDiscovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFactoryFPVGameAudioDiscoveryTest::RunTest(const FString&) {
    using namespace FactoryFPVAudioSettings;
    auto* MasterAsset=LoadObject<UFGUserSetting>(nullptr,TEXT("/Game/FactoryGame/Settings/OptionsMenu/Audio/US_MasterVolume.US_MasterVolume"));
    auto* EffectsAsset=LoadObject<UFGUserSetting>(nullptr,TEXT("/Game/FactoryGame/Settings/OptionsMenu/Audio/US_EffectsVolume.US_EffectsVolume"));
    if (!TestNotNull(TEXT("Master asset fixture"),MasterAsset) || !TestNotNull(TEXT("Effects asset fixture"),EffectsAsset)) return false;
    auto* SettingProperty=FindFProperty<FObjectPropertyBase>(UFGUserSettingApplyType::StaticClass(),TEXT("UserSetting"));
    if (!TestNotNull(TEXT("Real apply object exposes its reflected setting reference"),SettingProperty)) return false;
    // The SDK stubs its setters. A typed member pointer updates the actual
    // apply object used by the cache without substituting a getter.
    struct FApplyFixtureAccess : UFGUserSettingApplyType {
        static void SetApplied(UFGUserSettingApplyType& Apply,FVariant Value) {
            Apply.*(&FApplyFixtureAccess::AppliedValue)=MoveTemp(Value);
        }
    };
    auto MakeApply=[&](UFGUserSetting* Setting) {
        auto* Apply=NewObject<UFGUserSettingApplyType>();
        SettingProperty->SetObjectPropertyValue_InContainer(Apply,Setting);
        return Apply;
    };
    auto* Master=MakeApply(MasterAsset);
    auto* Effects=MakeApply(EffectsAsset);
    FApplyFixtureAccess::SetApplied(*Master,FVariant(.5f));
    FApplyFixtureAccess::SetApplied(*Effects,FVariant(.25));
    TestEqual(TEXT("Fixture uses the real underlying setting getter"),Master->GetUserSetting(),MasterAsset);
    auto* ManagerA=NewObject<UFGGameUserSettings>();
    auto* ManagerB=NewObject<UFGGameUserSettings>();
    FApplyMap Map;
    const FString MasterKey=TEXT("Audio.Options.MasterAsset-17");
    const FString EffectsKey=TEXT("Audio.Options.EffectsAsset-21");
    Map.Add(MasterKey,Master);Map.Add(EffectsKey,Effects);
    int Scans=0,Finds=0;
    auto Snapshot=[&](FApplyMap& Out) { ++Scans;Out=Map; };
    auto Find=[&](const FString& Key) -> UFGUserSettingApplyType* {
        ++Finds;
        TestTrue(TEXT("Only actual cached map keys reach lookup; never underlying CVar IDs"),
            Key!=TEXT("RTPC.Master_Bus_Volume") && Key!=TEXT("RTPC.SFX_Bus_Volume"));
        const auto* Found=Map.Find(Key);
        return Found ? Found->Get() : nullptr;
    };
    FGameVolumeCache Cache;
    TestEqual(TEXT("Uninitialized manager has safe unit gain"),Cache.Read(ManagerA,false,10,Snapshot,Find),1.);
    TestEqual(TEXT("Uninitialized manager is not scanned"),Scans,0);
    TestEqual(TEXT("Uninitialized manager is not queried"),Finds,0);
    TestEqual(TEXT("Initial discovery reads both applied levels"),Cache.Read(ManagerA,true,10,Snapshot,Find),.125);
    TestEqual(TEXT("Discovery captures actual master map key, not CVar"),Cache.MasterSource().Key,MasterKey);
    TestEqual(TEXT("Discovery captures actual effects map key, not CVar"),Cache.EffectsSource().Key,EffectsKey);
    TestEqual(TEXT("Discovery captures the actual apply object"),Cache.MasterSource().Apply.Get(),Master);
    TestTrue(TEXT("Console diagnostic includes resolved keys and gain"),Cache.Status().Contains(MasterKey) && Cache.Status().Contains(TEXT("gain=0.125")));
    TestEqual(TEXT("Initial discovery scans once"),Scans,1);
    const int FindsAfterScan=Finds;
    Cache.Read(ManagerA,true,10.1,Snapshot,Find);
    TestEqual(TEXT("Steady flight does not copy/enumerate the settings map"),Scans,1);
    TestEqual(TEXT("Steady flight validates both cached identities"),Finds,FindsAfterScan+2);
    auto* Replacement=MakeApply(MasterAsset);
    Map[MasterKey]=Replacement;
    FApplyFixtureAccess::SetApplied(*Master,FVariant(1.f));
    FApplyFixtureAccess::SetApplied(*Effects,FVariant(.75));
    TestEqual(TEXT("Replacement gap holds last master while reading new effects"),Cache.Read(ManagerA,true,10.2,Snapshot,Find),.375);
    TestFalse(TEXT("Replaced object is immediately retired, even if still alive"),Cache.MasterSource().Apply.IsValid());
    TestEqual(TEXT("Replacement rescan is bounded to half-second interval"),Scans,1);
    TestEqual(TEXT("Unchanged effects source survives master replacement"),Cache.EffectsSource().Apply.Get(),Effects);
    TestEqual(TEXT("Replacement without an applied value holds last master"),Cache.Read(ManagerA,true,10.51,Snapshot,Find),.375);
    TestEqual(TEXT("Replacement is discovered at next bounded scan"),Cache.MasterSource().Apply.Get(),Replacement);
    TestEqual(TEXT("Replacement caused only one additional scan"),Scans,2);
    FApplyFixtureAccess::SetApplied(*Replacement,FVariant(.25f));
    TestEqual(TEXT("Replacement applied value takes effect immediately"),Cache.Read(ManagerA,true,10.52,Snapshot,Find),.1875);
    FApplyFixtureAccess::SetApplied(*Effects,FVariant(0.));
    TestEqual(TEXT("Live effects mute takes effect immediately"),Cache.Read(ManagerA,true,10.53,Snapshot,Find),0.);
    Map.Remove(EffectsKey);
    TestEqual(TEXT("Removing a muted effects key keeps props muted"),Cache.Read(ManagerA,true,10.6,Snapshot,Find),0.);
    TestFalse(TEXT("Removed key clears cached source immediately"),Cache.EffectsSource().Apply.IsValid());
    TestEqual(TEXT("Scan with missing effects retains mute"),Cache.Read(ManagerA,true,11.02,Snapshot,Find),0.);
    Cache.Read(ManagerA,true,11.1,Snapshot,Find);Cache.Read(ManagerA,true,11.2,Snapshot,Find);
    TestEqual(TEXT("Missing setting does not cause per-frame map scans"),Scans,3);
    const FString NewEffectsKey=TEXT("Audio.Options.ReloadedEffects");
    Map.Add(NewEffectsKey,Effects);
    FApplyFixtureAccess::SetApplied(*Effects,FVariant(.5));
    TestEqual(TEXT("Rekeyed effects use their new applied level when found"),Cache.Read(ManagerA,true,11.53,Snapshot,Find),.125);
    TestEqual(TEXT("Rekeyed source is discovered using new actual key"),Cache.EffectsSource().Key,NewEffectsKey);
    const int ScansBeforeReinitialize=Scans,FindsBeforeReinitialize=Finds;
    TestEqual(TEXT("Same manager keeps levels while not initialized"),Cache.Read(ManagerA,false,11.54,Snapshot,Find),.125);
    TestEqual(TEXT("Initialization gap does not scan"),Scans,ScansBeforeReinitialize);
    TestEqual(TEXT("Initialization gap does not query cached keys"),Finds,FindsBeforeReinitialize);
    FApplyFixtureAccess::SetApplied(*Replacement,FVariant(0.f));
    TestEqual(TEXT("Same manager reads newly applied mute upon reinitialization"),Cache.Read(ManagerA,true,11.55,Snapshot,Find),0.);
    FApplyFixtureAccess::SetApplied(*Replacement,FVariant(std::numeric_limits<float>::quiet_NaN()));
    TestEqual(TEXT("Invalid transient master value retains mute"),Cache.Read(ManagerA,true,11.56,Snapshot,Find),0.);
    Map.Reset();
    TestEqual(TEXT("A new unresolved manager starts at unit gain"),Cache.Read(ManagerB,true,11.6,Snapshot,Find),1.);
    TestFalse(TEXT("Manager replacement cannot retain the old master's object"),Cache.MasterSource().Apply.IsValid());
    TestFalse(TEXT("Manager replacement cannot retain the old effects object"),Cache.EffectsSource().Apply.IsValid());
    const int ScansBeforeWait=Scans,FindsBeforeWait=Finds;
    Cache.Read(ManagerB,false,11.7,Snapshot,Find);
    TestEqual(TEXT("Waiting for initialization does not scan"),Scans,ScansBeforeWait);
    TestEqual(TEXT("Waiting for initialization does not query stale keys"),Finds,FindsBeforeWait);
    Cache.Read(nullptr,false,11.8,Snapshot,Find);
    TestTrue(TEXT("Missing manager has explicit diagnostic"),Cache.Status().Contains(TEXT("manager unavailable")));

    // Different setting assets may refer to the same CVar. Metadata discovery
    // must preserve the arbitrary manager key and prefer the canonical asset.
    auto* AliasAsset=NewObject<UFGUserSetting>();AliasAsset->StrId=TEXT("RTPC.Master_Bus_Volume");
    auto* Alias=MakeApply(AliasAsset);
    Map.Add(TEXT("AliasVolumeMapKey"),Alias);
    Map.Add(TEXT("EffectsMapKey"),Effects);
    FGameVolumeCache Aliases;
    Aliases.Read(ManagerA,true,20,Snapshot,Find);
    TestEqual(TEXT("CVar metadata fallback resolves an arbitrarily named asset"),Aliases.MasterSource().Apply.Get(),Alias);
    TestEqual(TEXT("Metadata fallback still stores actual map key"),Aliases.MasterSource().Key,FString(TEXT("AliasVolumeMapKey")));
    Map.Add(TEXT("ZCanonicalMasterKey"),Master);
    Map.Remove(TEXT("EffectsMapKey"));
    Aliases.Read(ManagerA,true,20.6,Snapshot,Find);
    TestEqual(TEXT("Canonical asset is preferred over duplicate CVar metadata"),Aliases.MasterSource().Apply.Get(),Master);
    return true;
}
#endif
