# SPDX-FileCopyrightText: Copyright 2026 Bonsai contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds and runs the wave-lanes harness (see wave_lanes.cpp) on a Windows machine without a GPU:
# lanes.comp in two variants -> Bonsai's spirv_to_dxil.dll -> dxil.dll -> WARP, plus the DXIL
# disassembly from the Windows SDK's dxc. CI runs it in the "wave lanes (WARP)" job.
#   -SpirvToDxil  folder holding spirv_to_dxil.dll (CI: the cached build of tools/xbox/mesa)
#   -Glslang      glslangValidator.exe
#   -Out          output folder (DXIL, disassembly, log)
param(
    [Parameter(Mandatory)] [string]$SpirvToDxil,
    [string]$Glslang = 'C:\glslang\bin\glslangValidator.exe',
    [string]$Out = 'wave-lanes-out'
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$repo = Resolve-Path (Join-Path $here '..\..\..\..')
New-Item -ItemType Directory -Force $Out | Out-Null
$Out = (Resolve-Path $Out).Path

# The two variants of the compaction.
foreach ($variant in @(@{ Name = 'builtin'; Guest = 0 }, @{ Name = 'guest'; Guest = 1 })) {
    & $Glslang -V --target-env vulkan1.1 "-DGUEST=$($variant.Guest)" (Join-Path $here 'lanes.comp') -o (Join-Path $Out "$($variant.Name).spv")
    if ($LASTEXITCODE -ne 0) { throw "glslang failed for $($variant.Name)" }
}

# WARP: the newest from NuGet when it can be had (app-local d3d10warp.dll), else the system's.
$warp = Join-Path $Out 'warp'
try {
    $nupkg = Join-Path $env:RUNNER_TEMP 'warp.zip'
    if (-not $env:RUNNER_TEMP) { $nupkg = Join-Path $Out 'warp.zip' }
    Invoke-WebRequest 'https://www.nuget.org/api/v2/package/Microsoft.Direct3D.WARP' -OutFile $nupkg
    Expand-Archive $nupkg $warp -Force
    $warpDll = Get-ChildItem $warp -Recurse -Filter d3d10warp.dll | Where-Object { $_.FullName -match 'x64' } | Select-Object -First 1
    if ($warpDll) { Write-Host "WARP from NuGet: $($warpDll.FullName)" } else { Write-Warning 'no x64 d3d10warp.dll in the WARP package' }
} catch {
    Write-Warning "WARP NuGet download failed ($($_.Exception.Message)); using the system's WARP"
    $warpDll = $null
}

# dxil.dll (signing) and dxc.exe (disassembly) from the Windows SDK.
$kits = 'C:\Program Files (x86)\Windows Kits\10'
$dxilDll = Get-ChildItem (Join-Path $kits 'Redist') -Recurse -Filter dxil.dll -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName | Select-Object -Last 1
$dxc = Get-ChildItem (Join-Path $kits 'bin') -Recurse -Filter dxc.exe -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName | Select-Object -Last 1
if (-not $dxilDll) { throw 'dxil.dll not found in the Windows SDK' }
Write-Host "dxil.dll: $($dxilDll.FullName)"
Write-Host "dxc.exe:  $(if ($dxc) { $dxc.FullName } else { 'not found (no disassembly)' })"

# Build the harness next to its DLLs.
$bin = Join-Path $Out 'bin'
New-Item -ItemType Directory -Force $bin | Out-Null
Copy-Item (Join-Path $SpirvToDxil 'spirv_to_dxil.dll') $bin -Force
Copy-Item $dxilDll.FullName $bin -Force
if ($warpDll) { Copy-Item $warpDll.FullName $bin -Force }
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio C++ tools not found' }
$include = Join-Path $repo 'externals\spirv-to-dxil\include'
$source = Join-Path $here 'wave_lanes.cpp'
$cmd = Join-Path $bin 'build.cmd'
Set-Content $cmd -Encoding ascii @(
    '@echo off',
    "call `"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64 >nul",
    'if errorlevel 1 exit /b 1',
    "cd /d `"$bin`"",
    "cl /nologo /O2 /MD /std:c++20 /EHsc /utf-8 /W3 /I`"$include`" `"$source`" /Fe:wave_lanes.exe /link d3d12.lib dxgi.lib"
)
& "$env:SystemRoot\System32\cmd.exe" /d /c $cmd
if ($LASTEXITCODE -ne 0) { throw 'harness build failed' }

# Run; the log is kept with the outputs.
$log = Join-Path $Out 'wave-lanes.log'
Push-Location $bin
try {
    & .\wave_lanes.exe $Out "builtin=$(Join-Path $Out 'builtin.spv')" "guest=$(Join-Path $Out 'guest.spv')" 2>&1 |
        Tee-Object -FilePath $log
    $status = $LASTEXITCODE
} finally { Pop-Location }

# How the two translations compute the lane masks: the DXIL around the wave intrinsics.
if ($dxc) {
    foreach ($name in 'builtin', 'guest') {
        $dxil = Join-Path $Out "$name.dxil"
        if (-not (Test-Path $dxil)) { continue }
        $text = Join-Path $Out "$name.dxil.txt"
        & $dxc.FullName -dumpbin $dxil -Fc $text | Out-Null
        if (Test-Path $text) {
            Write-Host "== $name DXIL: wave intrinsics and the lane arithmetic around them"
            Select-String -Path $text -Pattern 'dx\.op\.wave|waveGetLaneIndex|waveActiveBallot|waveReadLaneAt|shl |lshr |countbits|firstbitLo' |
                Select-Object -First 60 | ForEach-Object { Write-Host "  $($_.Line.Trim())" }
        }
    }
}
exit $status
