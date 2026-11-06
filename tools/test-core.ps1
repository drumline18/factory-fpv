[CmdletBinding()]
param([string]$BuildToolsRoot)
$ErrorActionPreference = 'Stop'
if (!$BuildToolsRoot) { $BuildToolsRoot = Join-Path $PSScriptRoot '../.deps/VSBuildTools' }
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$msvc = Get-ChildItem (Join-Path (Resolve-Path $BuildToolsRoot).Path 'VC/Tools/MSVC') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'bin/Hostx64/x64/cl.exe') } | Sort-Object Name -Descending | Select-Object -First 1
if (!$msvc) { throw 'No x64 MSVC compiler found in BuildToolsRoot.' }
$sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
$version = (Get-ChildItem (Join-Path $sdk 'Include') -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
$output = Join-Path $root 'build/windows'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$env:PATH = "$($msvc.FullName)/bin/Hostx64/x64;$env:PATH"
$env:INCLUDE = "$($msvc.FullName)/include;$sdk/Include/$version/ucrt;$sdk/Include/$version/shared;$sdk/Include/$version/um"
$env:LIB = "$($msvc.FullName)/lib/x64;$sdk/Lib/$version/ucrt/x64;$sdk/Lib/$version/um/x64"
foreach ($test in @('flight_core_test','radio_calibration_test','prop_sound_test','radio_arm_test','drone_battery_test','radio_actions_test','drone_damage_test','race_core_test')) {
    & cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 "/I$root/Mods/FactoryFPV/Source/FactoryFPV/Public" "/Fo$output/$test.obj" "/Fe$output/$test.exe" "$root/tests/$test.cpp"
    if ($LASTEXITCODE -ne 0) { throw "$test compilation failed." }
    & "$output/$test.exe"
    if ($LASTEXITCODE -ne 0) { throw "$test failed." }
}
