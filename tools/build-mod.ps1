[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$EngineRoot,
    [string]$StarterRoot,
    [ValidateRange(1,32)][int]$MaxParallelActions=2,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'SyncModSource.ps1')
$source = Join-Path $PSScriptRoot '../Mods/FactoryFPV'
$pluginMetadata=Get-Content -LiteralPath (Join-Path $source 'FactoryFPV.uplugin') -Raw | ConvertFrom-Json
$gameMatch=[regex]::Match([string]$pluginMetadata.GameVersion,'\A([1-9][0-9]{0,9})\.0\.0\z')
$expectedChangelist=[uint32]0
if (!$gameMatch.Success -or ![uint32]::TryParse($gameMatch.Groups[1].Value,[ref]$expectedChangelist)) {
    throw 'FactoryFPV.uplugin must declare one exact Satisfactory changelist.'
}
if (!$StarterRoot) {
    $namedStarter=Join-Path $PSScriptRoot "../.deps/SatisfactoryModLoader-CL$expectedChangelist"
    $StarterRoot=if (Test-Path -LiteralPath $namedStarter -PathType Container) { $namedStarter } else { Join-Path $PSScriptRoot '../.deps/SatisfactoryModLoader' }
}
$engine = (Resolve-Path -LiteralPath $EngineRoot).Path
$starter = (Resolve-Path -LiteralPath $StarterRoot).Path
$project = Join-Path $starter 'FactoryGame.uproject'
$batches = Join-Path $engine 'Engine/Build/BatchFiles'
$build = Join-Path $batches 'Build.bat'
$uat = Join-Path $batches 'RunUAT.bat'
$engineVersionPath=Join-Path $engine 'Engine/Build/Build.version'
$smlDescriptorPath=Join-Path $starter 'Mods/SML/SML.uplugin'
$starterChangelistPath=Join-Path $starter 'Source/FactoryGame/currentVersion.txt'
if (!(Test-Path -LiteralPath $project -PathType Leaf) -or !(Test-Path -LiteralPath $build -PathType Leaf) -or
    !(Test-Path -LiteralPath $engineVersionPath -PathType Leaf) -or !(Test-Path -LiteralPath $smlDescriptorPath -PathType Leaf) -or
    !(Test-Path -LiteralPath $starterChangelistPath -PathType Leaf) -or
    ($Package -and !(Test-Path -LiteralPath $uat -PathType Leaf))) {
    throw 'Use the Satisfactory starter project and the installed Unreal 5.6.1-CSS engine root.'
}
$projectMetadata=Get-Content -LiteralPath $project -Raw | ConvertFrom-Json
$engineMetadata=Get-Content -LiteralPath $engineVersionPath -Raw | ConvertFrom-Json
$smlMetadata=Get-Content -LiteralPath $smlDescriptorPath -Raw | ConvertFrom-Json
$starterChangelist=(Get-Content -LiteralPath $starterChangelistPath -Raw).Trim()
if ($starterChangelist -cne [string]$expectedChangelist) {
    throw "Toolchain mismatch. Factory FPV requires starter headers for CL$expectedChangelist. This starter reports CL$starterChangelist."
}
if ($projectMetadata.EngineAssociation -ne '5.6.1-CSS' -or
    $engineMetadata.MajorVersion -ne 5 -or $engineMetadata.MinorVersion -ne 6 -or $engineMetadata.PatchVersion -ne 1 -or
    $engineMetadata.BranchName -notlike '*5.6.1-CSS*' -or
    !@($projectMetadata.Modules | Where-Object { $_.Name -eq 'FactoryGame' -and $_.Type -eq 'Runtime' }).Count -or
    !@($projectMetadata.Modules | Where-Object { $_.Name -eq 'FactoryEditor' }).Count) {
    throw 'Toolchain mismatch. Use the Satisfactory starter with UE 5.6.1-CSS.'
}
$requiredSml=@($pluginMetadata.Plugins | Where-Object { $_.Name -ceq 'SML' })
if ($requiredSml.Count -ne 1 -or $requiredSml[0].Enabled -isnot [bool] -or !$requiredSml[0].Enabled -or $requiredSml[0].Optional -eq $true -or
    $requiredSml[0].SemVersion -isnot [string] -or $requiredSml[0].SemVersion -cnotmatch '\A(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\z') {
    throw 'FactoryFPV.uplugin must require one exact stable SML release.'
}
if ($smlMetadata.SemVersion -cne $requiredSml[0].SemVersion -or $smlMetadata.VersionName -cne $requiredSml[0].SemVersion) {
    throw "Toolchain mismatch. Factory FPV requires SML $($requiredSml[0].SemVersion). The starter reports $($smlMetadata.SemVersion)."
}

# Build the pinned permissive dependency before mirroring its ignored library.
& (Join-Path $PSScriptRoot 'build-minhook.ps1')

# Mirror authoritative files, retaining the starter plugin's generated caches.
# Synchronization refuses outside-workspace or linked targets before mutation.
$stagedPlugin=Join-Path $starter 'Mods/FactoryFPV'
$sync=Sync-ModSource -SourcePath $source -DestinationPath $stagedPlugin
Write-Output "FactoryFPV source sync: $($sync.Copied) copied, $($sync.Unchanged) unchanged, $($sync.RemovedFiles) stale files removed."

$previousPSModulePath=$env:PSModulePath
try {
    # UBT launches Windows PowerShell 5 for SDK hooks. A PS7 host can otherwise
    # make that child import incompatible PS7 modules ahead of its own modules.
    $windowsModules=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/Modules'
    $env:PSModulePath=$windowsModules+';'+$previousPSModulePath
    & $build FactoryEditor Win64 Development "-Project=$project" -WaitMutex '-Compiler=VisualStudio2022' "-MaxParallelActions=$MaxParallelActions"
    if ($LASTEXITCODE -ne 0) { throw "FactoryEditor build failed ($LASTEXITCODE)." }

    if ($Package) {
        $packageArgs = @(
            "-ScriptsForProject=$project", 'PackagePlugin', "-Project=$project", '-DLCName=FactoryFPV',
            '-Target=FactoryGameSteam', '-platform=Win64', '-clientconfig=Shipping', '-build',
            '-nocompileeditor', '-installed', '-utf8output', "-UbtArgs=-MaxParallelActions=$MaxParallelActions -Compiler=VisualStudio2022"
        )
        & $uat @packageArgs
        if ($LASTEXITCODE -ne 0) { throw "Alpakit packaging failed ($LASTEXITCODE)." }
        Get-ChildItem -LiteralPath (Join-Path $starter 'Saved/ArchivedPlugins/FactoryFPV') -Filter '*.zip'
    }
} finally {
    $env:PSModulePath=$previousPSModulePath
}
