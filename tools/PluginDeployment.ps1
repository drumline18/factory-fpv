# Shared local deployment primitives. Dot-source this file; it never installs on load.
# Windows PowerShell 5.1 compatible. Public entry points require an explicit workspace.

function Assert-FPVNoReparseAncestors {
    param([Parameter(Mandatory)][string]$Path)
    $current=[IO.Path]::GetFullPath($Path)
    while ($current) {
        $pathLookupErrors=@()
        $item=Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue -ErrorVariable pathLookupErrors
        if ($item) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse paths are not permitted: $current" }
        } elseif ($pathLookupErrors.Count -and $pathLookupErrors[0].CategoryInfo.Category -ne [Management.Automation.ErrorCategory]::ObjectNotFound) {
            throw "Cannot safely inspect deployment path: $current"
        }
        $parent=[IO.Directory]::GetParent($current)
        if (!$parent) { break }
        $current=$parent.FullName
    }
}

function Get-FPVWorkspacePath {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Path)
    $root=[IO.Path]::GetFullPath($Workspace).TrimEnd([char[]]'\/')
    if (!(Test-Path -LiteralPath $root -PathType Container)) { throw "Workspace does not exist: $root" }
    Assert-FPVNoReparseAncestors $root
    $resolved=[IO.Path]::GetFullPath($Path)
    if (!$resolved.StartsWith($root+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Deployment path must stay inside the workspace: $resolved"
    }
    Assert-FPVNoReparseAncestors $resolved
    return $resolved
}

function Get-FPVTreeFiles {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Path)
    $root=Get-FPVWorkspacePath $Workspace $Path
    if (!(Test-Path -LiteralPath $root -PathType Container)) { throw "Plugin directory does not exist: $root" }
    # Do not use -Recurse before checking entries: a junction could redirect the traversal.
    $pending=New-Object 'Collections.Generic.Stack[string]'
    $pending.Push($root)
    while ($pending.Count) {
        $directory=$pending.Pop()
        Assert-FPVNoReparseAncestors $directory
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force -ErrorAction Stop) {
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Plugin contains a reparse entry: $($item.FullName)" }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) } else { $item }
        }
    }
}

function Get-FPVPluginManifest {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Path)
    $root=Get-FPVWorkspacePath $Workspace $Path
    $seen=@{}
    $manifest=@(foreach ($file in Get-FPVTreeFiles $Workspace $root) {
        $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
        if ($seen.ContainsKey($relative)) { throw "Case-colliding plugin file: $relative" }
        $seen[$relative]=$true
        [pscustomobject]@{Path=$relative;Bytes=$file.Length;Sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash}
    })
    return @($manifest | Sort-Object Path)
}

function Assert-FPVManifest {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Path,[Parameter(Mandatory)][AllowEmptyCollection()][object[]]$Expected)
    $actual=@(Get-FPVPluginManifest $Workspace $Path)
    if ($actual.Count -ne $Expected.Count) { throw "Plugin file count differs after copy/swap: $Path" }
    $lookup=@{}
    foreach ($file in $actual) { $lookup[$file.Path]=$file }
    foreach ($file in $Expected) {
        $found=$lookup[$file.Path]
        if (!$found -or $found.Bytes -ne $file.Bytes -or $found.Sha256 -ne $file.Sha256) {
            throw "Plugin SHA256 verification failed: $Path / $($file.Path)"
        }
    }
}

function Assert-FPVPlugin {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Path)
    $root=Get-FPVWorkspacePath $Workspace $Path
    $manifest=@(Get-FPVPluginManifest $Workspace $root)
    $lookup=@{}
    foreach ($file in $manifest) { $lookup[$file.Path]=$file }
    $required=@('FactoryFPV.uplugin','THIRD_PARTY_NOTICES.txt',
        'Binaries/Win64/FactoryGameSteam-FactoryFPV-Win64-Shipping.dll',
        'Binaries/Win64/FactoryGameSteam-Win64-Shipping.modules',
        'Content/Paks/Windows/FactoryFPVFactoryGame-Windows.pak',
        'Content/Paks/Windows/FactoryFPVFactoryGame-Windows.ucas',
        'Content/Paks/Windows/FactoryFPVFactoryGame-Windows.utoc')
    foreach ($name in $required) {
        if (!$lookup.ContainsKey($name) -or $lookup[$name].Bytes -le 0) { throw "Incomplete plugin: $name" }
    }
    $descriptor=Get-Content -LiteralPath (Join-Path $root 'FactoryFPV.uplugin') -Raw | ConvertFrom-Json
    if ($descriptor.FileVersion -ne 3 -or $descriptor.Version -lt 1 -or !$descriptor.VersionName -or
        @($descriptor.Modules | Where-Object { $_.Name -eq 'FactoryFPV' -and $_.Type -eq 'Runtime' }).Count -ne 1) {
        throw 'Invalid FactoryFPV plugin descriptor.'
    }
    # Earlier historical backups predate the OSD font; keep them restorable.
    if ($descriptor.Version -ge 5) {
        foreach ($name in @('Content/Fonts/VT323-Regular.ttf','Content/Fonts/OFL.txt','Content/Fonts/SOURCE.txt')) {
            if (!$lookup.ContainsKey($name) -or $lookup[$name].Bytes -le 0) { throw "Incomplete OSD plugin: $name" }
        }
    }
    return $descriptor
}

function Copy-FPVPlugin {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Destination)
    $sourcePath=Get-FPVWorkspacePath $Workspace $Source
    $destinationPath=Get-FPVWorkspacePath $Workspace $Destination
    if (Test-Path -LiteralPath $destinationPath) { throw "Copy staging target already exists: $destinationPath" }
    $manifest=@(Get-FPVPluginManifest $Workspace $sourcePath)
    $parent=Get-FPVWorkspacePath $Workspace ([IO.Path]::GetDirectoryName($destinationPath))
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Recurse -Force -ErrorAction Stop
    Assert-FPVManifest $Workspace $destinationPath $manifest
}

function Move-FPVDirectory {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Source,[Parameter(Mandatory)][string]$Destination)
    # Revalidate both absolute paths immediately before every directory move.
    $sourcePath=Get-FPVWorkspacePath $Workspace $Source
    $destinationPath=Get-FPVWorkspacePath $Workspace $Destination
    if (!(Test-Path -LiteralPath $sourcePath -PathType Container) -or (Test-Path -LiteralPath $destinationPath)) {
        throw "Directory swap requires an existing source and unused destination: $sourcePath -> $destinationPath"
    }
    if ([IO.Path]::GetPathRoot($sourcePath) -ne [IO.Path]::GetPathRoot($destinationPath)) { throw 'Directory swaps must stay on one volume.' }
    # Validate every descendant before moving a tree, not only its ancestors.
    @(Get-FPVTreeFiles $Workspace $sourcePath) | Out-Null
    $parent=Get-FPVWorkspacePath $Workspace ([IO.Path]::GetDirectoryName($destinationPath))
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    [IO.Directory]::Move($sourcePath,$destinationPath)
}

function Assert-FPVGameClosed {
    param([Parameter(Mandatory)][string]$GameRoot)
    foreach ($process in Get-Process 'FactoryGameSteam*' -ErrorAction SilentlyContinue) {
        if (!$process.Path) { throw 'Cannot identify a running Satisfactory process. Close the game before deployment.' }
        if ($process.Path.StartsWith($GameRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Save and close the isolated test game before replacing its plugin.'
        }
    }
}

function Enter-FPVDeploymentLock {
    param([Parameter(Mandatory)][string]$Workspace)
    $buildRoot=Get-FPVWorkspacePath $Workspace (Join-Path $Workspace 'build')
    New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
    $lockPath=Get-FPVWorkspacePath $Workspace (Join-Path $buildRoot 'fpv-deployment.lock')
    try {
        return [IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
    } catch [IO.IOException] {
        throw "Cannot acquire the isolated game lock. Finish any other launch, deployment, or Shipping test and retry. $($_.Exception.Message)"
    }
}

function Get-FPVTestEnvironment {
    param([Parameter(Mandatory)][string]$Workspace,[string]$EnvironmentName)
    $environment=Join-Path $Workspace 'test-environment'
    if ($EnvironmentName) {
        if ($EnvironmentName -cnotmatch '\A[A-Za-z0-9][A-Za-z0-9-]{0,47}\z') {
            throw 'EnvironmentName must be 1 to 48 ASCII letters, digits or hyphens, beginning with a letter or digit.'
        }
        if ($EnvironmentName -in @('Game','UserData','VerticalPractice','ficsit-cache','ficsit-state') -or
            $EnvironmentName -match '\A(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])\z') {
            throw "EnvironmentName is reserved: $EnvironmentName"
        }
        $environment=Join-Path $environment $EnvironmentName
    }
    return Get-FPVWorkspacePath $Workspace $environment
}

function Get-FPVIsolatedGame {
    param([Parameter(Mandatory)][string]$Workspace,[string]$EnvironmentName)
    $environment=Get-FPVTestEnvironment -Workspace $Workspace -EnvironmentName $EnvironmentName
    $game=Get-FPVWorkspacePath $Workspace (Join-Path $environment 'Game')
    if (!(Test-Path -LiteralPath (Join-Path $game 'Engine/Binaries/Win64/FactoryGameSteam-Win64-Shipping.exe') -PathType Leaf)) {
        throw 'The exact isolated test installation was not found. No game files were changed.'
    }
    return $game
}

function Read-FPVCompatibilityMetadata {
    param([string]$Workspace,[string]$Path,[string]$Label)
    $metadataPath=Get-FPVWorkspacePath $Workspace $Path
    if (!(Test-Path -LiteralPath $metadataPath -PathType Leaf)) { throw "Missing $Label metadata: $metadataPath" }
    try {
        $metadata=Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
        if ($metadata -isnot [pscustomobject]) { throw 'Expected a JSON object.' }
        return $metadata
    } catch {
        throw "Invalid $Label metadata: $metadataPath"
    }
}

function Assert-FPVTargetCompatibility {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$GameRoot,
        [Parameter(Mandatory)][object]$Descriptor)
    $game=Get-FPVWorkspacePath $Workspace $GameRoot
    # The original isolated game still supports its historical local backups.
    if ($Descriptor.Version -lt 67) {
        $defaultGame=[IO.Path]::GetFullPath((Join-Path $Workspace 'test-environment/Game'))
        if ($game.TrimEnd([char[]]'\/') -ine $defaultGame) {
            throw 'Packages older than descriptor version 67 can only be used with the default isolated game. Named environments require exact game and SML pins.'
        }
        return
    }
    $declaredGame=[regex]::Match([string]$Descriptor.GameVersion,'\A([1-9][0-9]{0,9})\.0\.0\z')
    $expectedChangelist=[uint32]0
    if (!$declaredGame.Success -or ![uint32]::TryParse($declaredGame.Groups[1].Value,[ref]$expectedChangelist)) {
        throw 'Factory FPV must declare one exact Satisfactory changelist, such as 513885.0.0.'
    }
    $smlReference=@($Descriptor.Plugins | Where-Object { $_.Name -ceq 'SML' })
    if ($smlReference.Count -ne 1 -or $smlReference[0].Enabled -isnot [bool] -or !$smlReference[0].Enabled -or $smlReference[0].Optional -eq $true -or
        $smlReference[0].SemVersion -isnot [string] -or $smlReference[0].SemVersion -cnotmatch '\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z') {
        throw 'Factory FPV must declare one enabled, exact SML version, such as 3.12.0.'
    }
    $expectedSml=$smlReference[0].SemVersion

    $steamBinaries=Join-Path $game 'Engine/Binaries/Win64'
    $shippingName='FactoryGameSteam-Win64-Shipping'
    $build=Read-FPVCompatibilityMetadata $Workspace (Join-Path $steamBinaries "$shippingName.version") 'isolated Steam build'
    if (($build.Changelist -isnot [int] -and $build.Changelist -isnot [long]) -or
        $build.Changelist -lt 1 -or $build.Changelist -gt [uint32]::MaxValue -or
        $build.GameVersion -isnot [string] -or [string]::IsNullOrWhiteSpace($build.GameVersion)) {
        throw 'The isolated Steam build metadata is missing a valid changelist or game version.'
    }
    if ($build.Changelist -ne $expectedChangelist) {
        throw "Factory FPV requires Satisfactory CL$expectedChangelist. The isolated Steam build is CL$($build.Changelist)."
    }

    $sml=Join-Path $game 'FactoryGame/Mods/SML'
    $installedSml=Read-FPVCompatibilityMetadata $Workspace (Join-Path $sml 'SML.uplugin') 'installed SML descriptor'
    if ($installedSml.SemVersion -cne $expectedSml -or $installedSml.VersionName -cne $expectedSml) {
        throw "Factory FPV requires SML $expectedSml. The isolated SML descriptor reports SemVersion '$($installedSml.SemVersion)' and VersionName '$($installedSml.VersionName)'."
    }
    $smlBinaries=Join-Path $sml 'Binaries/Win64'
    $smlModule='FactoryGameSteam-SML-Win64-Shipping.dll'
    $smlMap=Read-FPVCompatibilityMetadata $Workspace (Join-Path $smlBinaries "$shippingName.modules") 'installed SML Steam module map'
    if ($smlMap.BuildId -cne 'SML' -or $smlMap.Modules.SML -cne $smlModule) { throw 'The installed SML Steam module map does not identify the native SML module.' }
    $smlDll=Get-FPVWorkspacePath $Workspace (Join-Path $smlBinaries $smlModule)
    if (!(Test-Path -LiteralPath $smlDll -PathType Leaf) -or (Get-Item -LiteralPath $smlDll).Length -le 0) {
        throw "Missing installed SML Steam module: $smlDll"
    }

    $steamExe=Get-FPVWorkspacePath $Workspace (Join-Path $steamBinaries "$shippingName.exe")
    $factoryGameDll=Get-FPVWorkspacePath $Workspace (Join-Path $game 'FactoryGame/Binaries/Win64/FactoryGameSteam-FactoryGame-Win64-Shipping.dll')
    foreach ($binary in @($steamExe,$factoryGameDll)) {
        if (!(Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing isolated Steam binary: $binary" }
        $productVersion=[Diagnostics.FileVersionInfo]::GetVersionInfo($binary).ProductVersion
        if (!$productVersion -or !$productVersion.EndsWith("-CL-$expectedChangelist",[StringComparison]::Ordinal)) {
            throw "The isolated Steam binary does not confirm CL$expectedChangelist in its product version: $binary"
        }
    }
    return [pscustomobject]@{GameChangelist=$expectedChangelist;GameVersion=$build.GameVersion;SmlVersion=$expectedSml}
}

function Invoke-FPVPluginDeployment {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$GameRoot,[Parameter(Mandatory)][string]$Candidate)
    $game=Get-FPVWorkspacePath $Workspace $GameRoot
    $candidatePath=Get-FPVWorkspacePath $Workspace $Candidate
    $buildRoot=Get-FPVWorkspacePath $Workspace (Join-Path $Workspace 'build')
    if (!$candidatePath.StartsWith($buildRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Only a disposable candidate staged under workspace/build may be swapped into the test game.'
    }
    $destination=Get-FPVWorkspacePath $Workspace (Join-Path $game 'FactoryGame/Mods/FactoryFPV')
    if ($candidatePath.Equals($destination,[StringComparison]::OrdinalIgnoreCase) -or
        $destination.StartsWith($candidatePath+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase) -or
        $candidatePath.StartsWith($destination+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Candidate and live plugin trees must be separate.' }
    if (!(Test-Path -LiteralPath (Join-Path $game 'Engine/Binaries/Win64/FactoryGameSteam-Win64-Shipping.exe') -PathType Leaf)) { throw 'The isolated game executable was not found.' }
    $descriptor=Assert-FPVPlugin $Workspace $candidatePath
    $candidateManifest=@(Get-FPVPluginManifest $Workspace $candidatePath)
    Assert-FPVGameClosed $game
    $stamp=(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $backup=Get-FPVWorkspacePath $Workspace (Join-Path $Workspace "backups/mod-$stamp/FactoryFPV")
    $retired=Get-FPVWorkspacePath $Workspace (Join-Path $buildRoot "deploy-retired-$stamp/FactoryFPV")
    $failed=Get-FPVWorkspacePath $Workspace (Join-Path $buildRoot "deploy-failed-$stamp/FactoryFPV")
    $rollback=Get-FPVWorkspacePath $Workspace (Join-Path $buildRoot "deploy-rollback-$stamp/FactoryFPV")
    $deploymentLock=Enter-FPVDeploymentLock $Workspace
    $oldMoved=$false
    $newMoved=$false
    $hadOld=Test-Path -LiteralPath $destination -PathType Container
    $oldManifest=@()
    try {
        Assert-FPVGameClosed $game
        $null=Assert-FPVTargetCompatibility $Workspace $game $descriptor
        if ($hadOld) {
            $oldManifest=@(Get-FPVPluginManifest $Workspace $destination)
            Copy-FPVPlugin $Workspace $destination $backup
            Assert-FPVManifest $Workspace $backup $oldManifest
            Move-FPVDirectory $Workspace $destination $retired
            $oldMoved=$true
        }
        Move-FPVDirectory $Workspace $candidatePath $destination
        $newMoved=$true
        Assert-FPVManifest $Workspace $destination $candidateManifest
        if ($hadOld) { Assert-FPVManifest $Workspace $backup $oldManifest }
    } catch {
        $deploymentFailure=$_.Exception.Message
        if ($oldMoved -or $newMoved) {
            try {
                if (Test-Path -LiteralPath $destination) { Move-FPVDirectory $Workspace $destination $failed }
                if ($hadOld) {
                    if (Test-Path -LiteralPath $retired) { Move-FPVDirectory $Workspace $retired $destination }
                    else {
                        Copy-FPVPlugin $Workspace $backup $rollback
                        Assert-FPVManifest $Workspace $rollback $oldManifest
                        Move-FPVDirectory $Workspace $rollback $destination
                    }
                    Assert-FPVManifest $Workspace $destination $oldManifest
                    Assert-FPVManifest $Workspace $backup $oldManifest
                }
            } catch {
                throw "Deployment failed: $deploymentFailure. Automatic rollback also failed: $($_.Exception.Message). Full previous backup: $backup ; retired copy: $retired ; failed candidate: $failed"
            }
            throw "Deployment failed and the previous installation was restored: $deploymentFailure. Previous backup retained: $backup"
        }
        throw "Deployment rejected before replacement: $deploymentFailure"
    } finally { $deploymentLock.Dispose() }
    # Delete only the validated redundant copy, after both live and backup hashes pass.
    if ($oldMoved) {
        try {
            $cleanup=Get-FPVWorkspacePath $Workspace $retired
            @(Get-FPVTreeFiles $Workspace $cleanup) | Out-Null
            Remove-Item -LiteralPath $cleanup -Recurse -Force -ErrorAction Stop
        } catch { Write-Warning "Deployment passed; redundant retired copy was kept: $retired" }
    }
    return [pscustomobject]@{VersionName=$descriptor.VersionName;Destination=$destination;BackupPath=$(if ($hadOld) {$backup} else {$null});FileCount=$candidateManifest.Count}
}

function New-FPVRestoreCandidate {
    param([Parameter(Mandatory)][string]$Workspace,[Parameter(Mandatory)][string]$Backup)
    $source=Get-FPVWorkspacePath $Workspace $Backup
    $backupsRoot=Get-FPVWorkspacePath $Workspace (Join-Path $Workspace 'backups')
    if (!$source.StartsWith($backupsRoot+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) { throw 'Choose an explicit historical plugin under workspace/backups.' }
    if (!(Test-Path -LiteralPath (Join-Path $source 'FactoryFPV.uplugin') -PathType Leaf)) {
        $source=Get-FPVWorkspacePath $Workspace (Join-Path $source 'FactoryFPV')
    }
    $null=Assert-FPVPlugin $Workspace $source
    $stamp=(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $candidate=Get-FPVWorkspacePath $Workspace (Join-Path $Workspace "build/restore-$stamp/FactoryFPV")
    Copy-FPVPlugin $Workspace $source $candidate
    return $candidate
}
