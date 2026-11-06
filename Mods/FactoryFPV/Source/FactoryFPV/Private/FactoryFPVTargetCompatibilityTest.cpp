#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "ModLoading/ModLoadingLibrary.h"
#include "PluginDescriptor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFactoryFPVTargetCompatibilityTest,
    "FactoryFPV.Release.TargetCompatibility",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFactoryFPVTargetCompatibilityTest::RunTest(const FString&) {
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("FactoryFPV"));
    if (!TestTrue(TEXT("Factory FPV descriptor is registered"), Plugin.IsValid())) return false;

    const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
    if (!TestTrue(TEXT("The registered descriptor has its source JSON"), Descriptor.CachedJson.IsValid())) return false;

    FSMLPluginDescriptorMetadata Metadata{};
    Metadata.SetupDefaults(Descriptor);
    Metadata.Load(Plugin->GetName(), Descriptor.CachedJson);

    TestTrue(TEXT("SML accepts the qualified game changelist"), Metadata.GameVersion.Matches(FVersion(517554, 0, 0)));
    TestFalse(TEXT("SML refuses the preceding changelist"), Metadata.GameVersion.Matches(FVersion(517553, 0, 0)));
    TestFalse(TEXT("SML refuses the next changelist"), Metadata.GameVersion.Matches(FVersion(517555, 0, 0)));
    TestFalse(TEXT("SML refuses the earlier qualified 1.2.4.1 release"), Metadata.GameVersion.Matches(FVersion(513885, 0, 0)));
    TestFalse(TEXT("The exact changelist declaration refuses a nonzero patch"), Metadata.GameVersion.Matches(FVersion(517554, 0, 1)));

    const FVersionRange* SMLDependency = Metadata.DependenciesVersions.Find(TEXT("SML"));
    if (!TestNotNull(TEXT("The SML dependency declares a valid version constraint"), SMLDependency)) return false;

    FVersion Prerelease(3, 12, 0);
    Prerelease.PreRelease = TEXT("preview.1");
    TestTrue(TEXT("The dependency accepts the qualified SML release"), SMLDependency->Matches(FVersion(3, 12, 0)));
    TestFalse(TEXT("The dependency refuses a different SML patch"), SMLDependency->Matches(FVersion(3, 12, 1)));
    TestFalse(TEXT("The dependency refuses a prerelease of the same SML version"), SMLDependency->Matches(Prerelease));
    return true;
}
#endif
