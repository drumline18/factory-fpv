<#
.SYNOPSIS
Build the vendored, pinned MinHook x64 static library without the Visual Studio IDE.
.DESCRIPTION
Windows PowerShell 5.1 compatible. Validates every pinned upstream byte, rejects
linked/workspace-escaping output paths, and restores process compiler environment.
The ignored library is rebuilt before plugin source synchronization when its
source/compiler/settings digest or recorded output hash changes.
#>
[CmdletBinding()]
param([string]$BuildToolsRoot)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'PluginDeployment.ps1')
$workspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (!$BuildToolsRoot) { $BuildToolsRoot=Join-Path $workspace '.deps/VSBuildTools' }
if (![IO.Path]::IsPathRooted($BuildToolsRoot)) { $BuildToolsRoot=Join-Path $workspace $BuildToolsRoot }
$tools=Get-FPVWorkspacePath $workspace $BuildToolsRoot
$source=Get-FPVWorkspacePath $workspace (Join-Path $workspace 'Mods/FactoryFPV/Source/ThirdParty/FactoryFPVMinHook')
$output=Get-FPVWorkspacePath $workspace (Join-Path $source 'lib/Win64/FactoryFPVMinHook.lib')
$cacheRoot=Get-FPVWorkspacePath $workspace (Join-Path $workspace 'build/minhook')
$cachePath=Get-FPVWorkspacePath $workspace (Join-Path $cacheRoot 'BuildCache.json')
$manifestPath=Get-FPVWorkspacePath $workspace (Join-Path $source 'SourceManifest.json')
$pinnedCommit='c3fcafdc10146beb5919319d0683e44e3c30d537'
$manifest=Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.Commit -cne $pinnedCommit) { throw 'MinHook source manifest does not match the reviewed upstream commit.' }
$expectedFiles=@('LICENSE.txt','include/MinHook.h','src/buffer.c','src/buffer.h','src/hook.c',
    'src/trampoline.c','src/trampoline.h','src/hde/hde64.c','src/hde/hde64.h','src/hde/pstdint.h','src/hde/table64.h')
if (@($manifest.Files).Count -ne $expectedFiles.Count) { throw 'MinHook source manifest has an unexpected file count.' }
$sourceRecords=@(foreach ($relative in $expectedFiles) {
    $record=@($manifest.Files | Where-Object { $_.Path -ceq $relative })
    if ($record.Count -ne 1) { throw "MinHook source manifest is missing or duplicates $relative" }
    $path=Get-FPVWorkspacePath $workspace (Join-Path $source $relative)
    $file=Get-Item -LiteralPath $path -Force
    if ($file.PSIsContainer -or $file.Length -ne $record[0].Bytes -or
        (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -cne $record[0].Sha256) {
        throw "Pinned MinHook source differs from upstream: $relative"
    }
    [ordered]@{Path=$relative;Bytes=$file.Length;Sha256=$record[0].Sha256}
})
$msvcVersion='14.38.33130'
$msvc=Get-FPVWorkspacePath $workspace (Join-Path $tools "VC/Tools/MSVC/$msvcVersion")
$bin=Get-FPVWorkspacePath $workspace (Join-Path $msvc 'bin/Hostx64/x64')
$compiler=Get-FPVWorkspacePath $workspace (Join-Path $bin 'cl.exe')
$librarian=Get-FPVWorkspacePath $workspace (Join-Path $bin 'lib.exe')
$compilerRecords=@(foreach ($name in @('cl.exe','c1.dll','c2.dll','lib.exe')) {
    $path=Get-FPVWorkspacePath $workspace (Join-Path $bin $name)
    $file=Get-Item -LiteralPath $path -Force
    [ordered]@{Name=$name;Version=$file.VersionInfo.FileVersion;Sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
})
$sdkRoot=Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$sdkVersion='10.0.22621.0'
$includePaths=@((Join-Path $msvc 'include'),(Join-Path $sdkRoot "Include/$sdkVersion/ucrt"),
    (Join-Path $sdkRoot "Include/$sdkVersion/shared"),(Join-Path $sdkRoot "Include/$sdkVersion/um"))
$libraryPaths=@((Join-Path $msvc 'lib/x64'),(Join-Path $sdkRoot "Lib/$sdkVersion/ucrt/x64"),
    (Join-Path $sdkRoot "Lib/$sdkVersion/um/x64"))
foreach ($path in $includePaths+$libraryPaths) {
    Assert-FPVNoReparseAncestors $path
    if (!(Test-Path -LiteralPath $path -PathType Container)) { throw "Required MSVC/Windows SDK directory is missing: $path" }
}
$flags=@('/nologo','/c','/MD','/O2','/TC','/W3')
$inputs=[ordered]@{
    SchemaVersion=1;Commit=$pinnedCommit;Sources=$sourceRecords;Compiler=$compilerRecords
    MsvcVersion=$msvcVersion;WindowsSdkVersion=$sdkVersion;Include=$includePaths;Library=$libraryPaths
    CompileFlags=$flags;LibrarianFlags=@('/nologo','/MACHINE:X64');Architecture='x64'
}
$json=$inputs | ConvertTo-Json -Depth 8 -Compress
$sha=[Security.Cryptography.SHA256]::Create()
try { $digest=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($json)))).Replace('-','') }
finally { $sha.Dispose() }
[void][IO.Directory]::CreateDirectory($cacheRoot)
$lockPath=Get-FPVWorkspacePath $workspace (Join-Path $cacheRoot 'build.lock')
$lock=$null
$previous=@{}
try {
    $lock=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    if ((Test-Path -LiteralPath $cachePath -PathType Leaf) -and (Test-Path -LiteralPath $output -PathType Leaf)) {
        try { $cached=Get-Content -LiteralPath $cachePath -Raw | ConvertFrom-Json } catch { $cached=$null }
        if ($cached -and $cached.InputDigest -ceq $digest -and $cached.OutputBytes -eq (Get-Item -LiteralPath $output).Length -and
            $cached.OutputSha256 -ceq (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash) {
            Write-Output "FactoryFPV MinHook cache verified: $output"
            return
        }
    }
    $buildRoot=Get-FPVWorkspacePath $workspace (Join-Path $cacheRoot $digest)
    [void][IO.Directory]::CreateDirectory($buildRoot)
    foreach ($name in @('PATH','INCLUDE','LIB','CL','_CL_','LINK','_LINK_')) {
        $previous[$name]=[Environment]::GetEnvironmentVariable($name,'Process')
    }
    $env:PATH=$bin+';'+$env:PATH
    $env:INCLUDE=$includePaths -join ';'
    $env:LIB=$libraryPaths -join ';'
    foreach ($name in @('CL','_CL_','LINK','_LINK_')) { [Environment]::SetEnvironmentVariable($name,[NullString]::Value,'Process') }
    $objects=@()
    foreach ($relative in @('src/buffer.c','src/hook.c','src/trampoline.c','src/hde/hde64.c')) {
        $file=Get-FPVWorkspacePath $workspace (Join-Path $source $relative)
        $object=Get-FPVWorkspacePath $workspace (Join-Path $buildRoot ([IO.Path]::GetFileNameWithoutExtension($relative)+'.obj'))
        & $compiler @flags "/Fo$object" $file
        if ($LASTEXITCODE -ne 0) { throw "MinHook compilation failed for $relative ($LASTEXITCODE)." }
        $objects+=$object
    }
    $temporaryLibrary=Get-FPVWorkspacePath $workspace (Join-Path $buildRoot 'FactoryFPVMinHook.lib')
    & $librarian '/nologo' '/MACHINE:X64' "/OUT:$temporaryLibrary" @objects
    if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $temporaryLibrary -PathType Leaf) -or
        (Get-Item -LiteralPath $temporaryLibrary).Length -le 0) { throw 'MinHook static library creation failed.' }
    $outputDirectory=Get-FPVWorkspacePath $workspace ([IO.Path]::GetDirectoryName($output))
    [void][IO.Directory]::CreateDirectory($outputDirectory)
    Copy-Item -LiteralPath $temporaryLibrary -Destination $output -Force
    $result=[ordered]@{
        SchemaVersion=1;InputDigest=$digest;Inputs=$inputs;OutputBytes=(Get-Item -LiteralPath $output).Length
        OutputSha256=(Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    }
    [IO.File]::WriteAllText($cachePath,($result | ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
    Write-Output "FactoryFPV MinHook x64 static library built: $output"
} finally {
    foreach ($name in $previous.Keys) {
        $value=$previous[$name]
        if ($null -eq $value) { $value=[NullString]::Value }
        [Environment]::SetEnvironmentVariable($name,$value,'Process')
    }
    if ($lock) { $lock.Dispose() }
}
