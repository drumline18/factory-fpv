using UnrealBuildTool;
public class FactoryFPV : ModuleRules
{
    public FactoryFPV(ReadOnlyTargetRules Target) : base(Target)
    {
        PrivateDependencyModuleNames.AddRange(new[] { "Slate", "SlateCore", "UMG", "RenderCore", "RHI", "Projects", "ImageCore", "EnhancedInput", "OnlineIntegration", "AbstractInstance" });
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            PublicSystemLibraries.AddRange(new[] { "dinput8.lib", "dxguid.lib", "ole32.lib", "cfgmgr32.lib" });
            PrivateDependencyModuleNames.Add("FactoryFPVMinHook");
        }
        RuntimeDependencies.Add("$(PluginDir)/THIRD_PARTY_NOTICES.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/LICENSE.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/README.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/PilotArt-SOURCE.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/MinHook-LICENSE.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/MinHook-SOURCE.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/Content/Fonts/VT323-Regular.ttf", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/Content/Fonts/OFL.txt", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/Content/Fonts/SOURCE.txt", StagedFileType.NonUFS);
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new [] { "Core", "CoreUObject", "Engine", "InputCore", "ApplicationCore", "AudioMixer", "FactoryGame", "SML" });
    }
}
