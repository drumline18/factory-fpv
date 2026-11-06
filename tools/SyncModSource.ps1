<#
.SYNOPSIS
Synchronize authoritative FactoryFPV plugin source without deleting build caches.
.DESCRIPTION
Dot-source this file, then call Sync-ModSource -SourcePath ... -DestinationPath ...
Both separate FactoryFPV plugin roots must be below this workspace. Root files and
Source/Content/Resources/Config trees are mirrored. Binaries, Intermediate, Saved
and DerivedDataCache are not copied or pruned. No recursive deletion is used.
#>
function Sync-ModSource {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$SourcePath,
        [Parameter(Mandatory)][string]$DestinationPath
    )
    $ErrorActionPreference='Stop'
    $syncWorkspace=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\','/')
    function Get-SyncFullPath([string]$Path) {
        if (![IO.Path]::IsPathRooted($Path)) { $Path=Join-Path $syncWorkspace $Path }
        return [IO.Path]::GetFullPath($Path).TrimEnd('\','/')
    }
    $sourceRoot=Get-SyncFullPath $SourcePath
    $destinationRoot=Get-SyncFullPath $DestinationPath
    $separator=[IO.Path]::DirectorySeparatorChar
    foreach ($root in @($sourceRoot,$destinationRoot)) {
        if (!$root.StartsWith($syncWorkspace+$separator,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Source synchronization requires plugin roots inside this workspace; outside starters are not pruned.'
        }
        if ([IO.Path]::GetFileName($root) -ine 'FactoryFPV') { throw 'Source synchronization requires the intended FactoryFPV plugin directory.' }
    }
    if ($sourceRoot.Equals($destinationRoot,[StringComparison]::OrdinalIgnoreCase) -or
        $sourceRoot.StartsWith($destinationRoot+$separator,[StringComparison]::OrdinalIgnoreCase) -or
        $destinationRoot.StartsWith($sourceRoot+$separator,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Source and destination plugin roots must not overlap.'
    }
    function Assert-SyncNoLink([string]$Path) {
        for ($ancestor=$Path;$ancestor -and $ancestor.Length -ge $syncWorkspace.Length;$ancestor=[IO.Path]::GetDirectoryName($ancestor)) {
            if ((Test-Path -LiteralPath $ancestor) -and ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
                throw "Source synchronization does not traverse filesystem links: $ancestor"
            }
        }
    }
    function Assert-SyncDestination([string]$Path) {
        $full=[IO.Path]::GetFullPath($Path)
        if (!$full.StartsWith($destinationRoot+$separator,[StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing an operation outside the intended plugin: $full"
        }
        Assert-SyncNoLink $full
    }
    function Get-SyncInventory([string]$Root) {
        $files=[Collections.Generic.List[object]]::new()
        $directories=[Collections.Generic.List[object]]::new()
        if (Test-Path -LiteralPath $Root) {
            if (!(Test-Path -LiteralPath $Root -PathType Container)) { throw "Plugin root is not a directory: $Root" }
            $pending=[Collections.Generic.Queue[string]]::new()
            $pending.Enqueue($Root)
            while ($pending.Count) {
                $directory=$pending.Dequeue()
                foreach ($entry in Get-ChildItem -LiteralPath $directory -Force) {
                    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Source synchronization does not traverse filesystem links: $($entry.FullName)" }
                    if ($entry.PSIsContainer) { $directories.Add($entry);$pending.Enqueue($entry.FullName) }
                    else { $files.Add($entry) }
                }
            }
        }
        return [pscustomobject]@{Files=@($files.ToArray());Directories=@($directories.ToArray())}
    }
    Assert-SyncNoLink $sourceRoot
    Assert-SyncNoLink $destinationRoot
    if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'FactoryFPV.uplugin') -PathType Leaf) -or
        !(Test-Path -LiteralPath (Join-Path $sourceRoot 'Source/FactoryFPV/FactoryFPV.Build.cs') -PathType Leaf)) {
        throw 'Source must contain the FactoryFPV descriptor and native module build rules.'
    }
    $descriptor=Get-Content -LiteralPath (Join-Path $sourceRoot 'FactoryFPV.uplugin') -Raw | ConvertFrom-Json
    if (!@($descriptor.Modules | Where-Object { $_.Name -eq 'FactoryFPV' -and $_.Type -eq 'Runtime' }).Count) {
        throw 'Source descriptor does not declare the FactoryFPV Runtime module.'
    }
    $managedTrees=@('Source','Content','Resources','Config')
    $cacheTrees=@('Binaries','Intermediate','Saved','DerivedDataCache')
    function Get-SyncRelative([string]$Path,[string]$Root) { return $Path.Substring($Root.Length+1) }
    function Test-SyncManagedFile([string]$Relative) {
        $parts=$Relative.Split($separator)
        return $parts.Count -eq 1 -or $parts[0] -in $managedTrees
    }
    function Test-SyncManagedDirectory([string]$Relative) { return $Relative.Split($separator)[0] -in $managedTrees }
    # Inventory every descendant before changing anything. Even a link hidden in
    # a retained cache is rejected, avoiding a later build writing through it.
    $sourceInventory=Get-SyncInventory $sourceRoot
    $destinationInventory=Get-SyncInventory $destinationRoot
    foreach ($directory in $sourceInventory.Directories) {
        $relative=Get-SyncRelative $directory.FullName $sourceRoot
        if (!$relative.Contains([string]$separator) -and $relative -notin $managedTrees -and $relative -notin $cacheTrees) {
            throw "Unrecognized source tree '$relative'; add it to the managed synchronization contract before building."
        }
    }
    $sourceFiles=[Collections.Generic.Dictionary[string,object]]::new([StringComparer]::OrdinalIgnoreCase)
    $sourceDirectories=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($directory in $sourceInventory.Directories) {
        $relative=Get-SyncRelative $directory.FullName $sourceRoot
        if (Test-SyncManagedDirectory $relative) { [void]$sourceDirectories.Add($relative) }
    }
    foreach ($file in $sourceInventory.Files) {
        $relative=Get-SyncRelative $file.FullName $sourceRoot
        if (Test-SyncManagedFile $relative) { $sourceFiles.Add($relative,$file) }
    }
    # A type collision suggests a damaged/misidentified target. Fail during
    # preflight rather than removing a directory to make a file fit, or vice versa.
    foreach ($relative in $sourceFiles.Keys) {
        if (Test-Path -LiteralPath (Join-Path $destinationRoot $relative) -PathType Container) { throw "File/directory collision in staged plugin: $relative" }
    }
    foreach ($relative in $sourceDirectories) {
        if (Test-Path -LiteralPath (Join-Path $destinationRoot $relative) -PathType Leaf) { throw "File/directory collision in staged plugin: $relative" }
    }
    [int]$removedFiles=0
    [int]$removedDirectories=0
    foreach ($file in $destinationInventory.Files) {
        $relative=Get-SyncRelative $file.FullName $destinationRoot
        if ((Test-SyncManagedFile $relative) -and !$sourceFiles.ContainsKey($relative)) {
            Assert-SyncDestination $file.FullName
            Remove-Item -LiteralPath $file.FullName -Force -ErrorAction Stop
            $removedFiles++
        }
    }
    foreach ($directory in @($destinationInventory.Directories | Sort-Object { $_.FullName.Length } -Descending)) {
        $relative=Get-SyncRelative $directory.FullName $destinationRoot
        if ((Test-SyncManagedDirectory $relative) -and !$sourceDirectories.Contains($relative)) {
            Assert-SyncDestination $directory.FullName
            if (!(Get-ChildItem -LiteralPath $directory.FullName -Force | Select-Object -First 1)) {
                Remove-Item -LiteralPath $directory.FullName -Force -ErrorAction Stop
                $removedDirectories++
            }
        }
    }
    Assert-SyncNoLink $destinationRoot
    [void][IO.Directory]::CreateDirectory($destinationRoot)
    foreach ($relative in @($sourceDirectories | Sort-Object Length)) {
        $target=Join-Path $destinationRoot $relative
        Assert-SyncDestination $target
        [void][IO.Directory]::CreateDirectory($target)
    }
    [int]$copied=0
    [int]$unchanged=0
    foreach ($relative in @($sourceFiles.Keys | Sort-Object)) {
        $file=$sourceFiles[$relative]
        $target=Join-Path $destinationRoot $relative
        Assert-SyncNoLink $file.FullName
        Assert-SyncDestination $target
        if ((Test-Path -LiteralPath $target -PathType Leaf) -and
            (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash -eq (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash) {
            $unchanged++
        } else {
            Copy-Item -LiteralPath $file.FullName -Destination $target -Force -ErrorAction Stop
            # Copy-Item preserves authored timestamps. An older-authored change
            # synced after a focused build must still invalidate UBT's cache.
            (Get-Item -LiteralPath $target).LastWriteTimeUtc=[DateTime]::UtcNow
            $copied++
        }
    }
    return [pscustomobject]@{
        Source=$sourceRoot;Destination=$destinationRoot;Copied=$copied;Unchanged=$unchanged
        RemovedFiles=$removedFiles;RemovedDirectories=$removedDirectories
    }
}
