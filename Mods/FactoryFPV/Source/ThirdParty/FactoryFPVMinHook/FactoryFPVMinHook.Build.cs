using System.IO;
using UnrealBuildTool;

// MinHook is compiled locally by tools/build-minhook.ps1 before source sync.
public class FactoryFPVMinHook : ModuleRules
{
    public FactoryFPVMinHook(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        if (Target.Platform != UnrealTargetPlatform.Win64)
        {
            throw new BuildException("FactoryFPVMinHook supports only the Windows x64 mod target.");
        }
        string Library = Path.Combine(ModuleDirectory, "lib", "Win64", "FactoryFPVMinHook.lib");
        if (!File.Exists(Library))
        {
            throw new BuildException("Build the pinned MinHook dependency with tools/build-minhook.ps1 before building FactoryFPV.");
        }
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
        PublicAdditionalLibraries.Add(Library);
        // Upstream MinHook.h has ordinary extern C / WINAPI declarations.
        // It has no dllimport annotation and requires no MH_STATIC definition.
    }
}
