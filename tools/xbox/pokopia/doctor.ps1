# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks everything the Pokopia steps need on your PC before any of them runs, and says how to fix
# what is missing. Reads only: it changes nothing and sends nothing anywhere.
#
#   pwsh -File tools\xbox\pokopia\doctor.ps1 -Root C:\Users\you\Desktop\pkp-xbox
#   pwsh -File tools\xbox\pokopia\doctor.ps1 -Root ... -P1                 # also the tools that build the P1 runtime
#   pwsh -File tools\xbox\pokopia\doctor.ps1 -Root ... -ConsoleIp 192.168.1.50
#
# -Root is one folder that holds the unzipped pokopia-recomp release and your extracted game
# (base and 2.0.1a update, each with exefs\ and romfs\). Anything it finds can be overridden with
# -RecompDir, -Base, -Update and -Dump. Exit code 0 when nothing FAILs.

param([string] $Root, [string] $RecompDir, [string] $Base, [string] $Update, [string] $Dump,
      [switch] $P1, [string] $ConsoleIp, [switch] $Quiet)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'common.ps1')

if (-not $Quiet) { Write-Host "Pokopia doctor: checking this PC (nothing is changed)" -ForegroundColor Cyan }

# --- PowerShell and the machine -------------------------------------------------------------------
$v = $PSVersionTable.PSVersion
if (-not [Environment]::Is64BitProcess) {
    Add-PkpCheck FAIL 'PowerShell' "$v, 32-bit" 'Run the 64-bit pwsh (or "Windows PowerShell", not the (x86) one).'
} elseif ($v.Major -ge 7) { Add-PkpCheck OK 'PowerShell' "$v, 64-bit" '' }
else { Add-PkpCheck WARN 'PowerShell' "$v, 64-bit" 'PowerShell 7 is recommended: winget install Microsoft.PowerShell' }

$os = [Environment]::OSVersion.Version
if ($os.Major -lt 10) { Add-PkpCheck FAIL 'Windows' "$os" 'pokopia-recomp needs Windows 10 or 11, 64-bit.' }
else { Add-PkpCheck OK 'Windows' "$os" '' }

try {
    $ram = (Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory
    $gib = [math]::Round($ram / 1GB, 1)
    if ($gib -lt 15) { Add-PkpCheck WARN 'RAM' "$gib GiB" 'The recomp asks for 16 GB. Run A (no limit) may page heavily; the measurement still counts.' }
    else { Add-PkpCheck OK 'RAM' "$gib GiB" '' }
} catch { Add-PkpCheck WARN 'RAM' 'could not read' '' }

# --- Vulkan 1.3 (the recomp's renderer) ----------------------------------------------------------
$vk = Join-Path $env:WINDIR 'System32\vulkan-1.dll'
if (-not (Test-Path -LiteralPath $vk)) {
    Add-PkpCheck FAIL 'Vulkan' 'no vulkan-1.dll' 'Install the current driver for your GPU (it installs the Vulkan loader).'
} else {
    $vkinfo = Find-PkpTool 'vulkaninfo' @((Join-Path $env:WINDIR 'System32\vulkaninfo.exe'))
    $api = $null
    if ($vkinfo) {
        try {
            $s = & $vkinfo --summary 2>$null | Out-String
            $m = [regex]::Matches($s, 'apiVersion\s*=\s*(\d+)\.(\d+)\.(\d+)')
            foreach ($x in $m) { $ver = [version]"$($x.Groups[1]).$($x.Groups[2])"; if (-not $api -or $ver -gt $api) { $api = $ver } }
        } catch { }
    }
    if ($api -and $api -lt [version]'1.3') { Add-PkpCheck FAIL 'Vulkan' "best device offers $api" 'pokopia-recomp needs Vulkan 1.3: update the GPU driver, or use a GPU that has it.' }
    elseif ($api) { Add-PkpCheck OK 'Vulkan' "device API $api" '' }
    else { Add-PkpCheck INFO 'Vulkan' 'loader present; version not read (no vulkaninfo)' 'The game checks Vulkan 1.3 itself at start and says so in pkp.log.' }
}

# --- the recomp release ---------------------------------------------------------------------------
if (-not $RecompDir) { $RecompDir = Find-PkpRecomp $Root }
if (-not $RecompDir) {
    Add-PkpCheck FAIL 'pokopia-recomp' "PokopiaLauncher.exe not found under $Root" "Unzip the $PkpRecompTag release from $PkpRecompUrl/releases into that folder."
} else {
    Add-PkpCheck OK 'pokopia-recomp' $RecompDir ''
    $built = (Test-Path -LiteralPath (Join-Path $RecompDir 'pkp.exe')) -and (Test-Path -LiteralPath (Join-Path $RecompDir 'build\done.json'))
    $marker = Join-Path $RecompDir 'shader_cache\setup.json'
    $shaders = $false
    if (Test-Path -LiteralPath $marker) { try { $shaders = [bool]((Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json).complete) } catch { } }
    Add-PkpCheck ($(if ($built) { 'OK' } else { 'INFO' })) 'Game compiled' ($(if ($built) { 'pkp.exe is built' } else { 'not yet' })) 'pokopia.ps1 opens the launcher for Set up (about 7 minutes).'
    Add-PkpCheck ($(if ($shaders) { 'OK' } else { 'INFO' })) 'Shader cache' ($(if ($shaders) { 'complete' } else { 'not yet' })) 'Built by the launcher after compiling (1-2 hours, can be paused).'
    try {
        $drive = Get-PSDrive -Name ((Resolve-Path -LiteralPath $RecompDir).Drive.Name)
        $free = [math]::Round($drive.Free / 1GB, 1)
        if ($free -lt 20) { Add-PkpCheck WARN 'Disk space' "$free GiB free on $($drive.Name):" 'Leave 20 GiB or more for the compiled game, the shader cache and the P1 build.' }
        else { Add-PkpCheck OK 'Disk space' "$free GiB free on $($drive.Name):" '' }
    } catch { }
}

# --- your game files ------------------------------------------------------------------------------
$hashes = Get-PkpHashes $RecompDir
if (-not $Base -and $Root) {
    $parts = Find-PkpGameParts $Root $RecompDir $hashes
    $Base = $parts.Base
    if (-not $Update) { $Update = $parts.Update }
    if ($parts.Note) { Add-PkpCheck INFO 'Game files' $parts.Note '' }
}
$containers = @()
if ($Root -and (Test-Path -LiteralPath $Root)) {
    $containers = @(Get-ChildItem -LiteralPath $Root -File -Recurse -Depth 3 -Include '*.nsp', '*.nsz', '*.xci', '*.xcz', '*.nup', '*.nca' -ErrorAction SilentlyContinue | Select-Object -First 3)
}
if (-not $Base) {
    $fix = 'Extract your own copy (base + 2.0.1a update) on this PC with your own keys, into folders with exefs\ and romfs\ inside, under the root folder.'
    if ($containers) { $fix = "Found encrypted containers ($((($containers | ForEach-Object Name) -join ', '))). These scripts do not decrypt: " + $fix }
    Add-PkpCheck FAIL 'Base' 'no extracted base found' $fix
} else {
    $miss = @($PkpExefsFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Base "exefs\$_") -PathType Leaf) })
    if ($miss.Count) { Add-PkpCheck FAIL 'Base' "$Base, exefs is missing $($miss -join ', ')" 'Extract the base again; exefs needs main, sdk, subsdk0, rtld and main.npdm.' }
    else { Add-PkpCheck OK 'Base' $Base '' }
}
$exefs201 = $null
if ($Update) { $exefs201 = Join-Path $Update 'exefs' } elseif ($Base) { $exefs201 = Join-Path $Base 'exefs' }
if ($exefs201 -and (Test-Path -LiteralPath $exefs201)) {
    $bad = @()
    foreach ($n in $PkpExefsFiles) {
        $p = Join-Path $exefs201 $n
        if (-not (Test-Path -LiteralPath $p -PathType Leaf)) { $bad += "$n missing"; continue }
        if ((Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant() -ne $hashes[$n]) { $bad += $n }
    }
    if ($bad.Count) {
        Add-PkpCheck FAIL 'Version 2.0.1a' "exefs at $exefs201 does not match 2.0.1a ($($bad -join ', '))" 'The recomp is built for 2.0.1a only. Extract the 2.0.1a update together with the base.'
    } else { Add-PkpCheck OK 'Version 2.0.1a' "exefs hashes match ($exefs201)" '' }
}
if ($Update) {
    $sz = { param($d) (Get-ChildItem -LiteralPath $d -File -Recurse -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum }
    $ub = & $sz (Join-Path $Update 'romfs'); $bb = if ($Base) { & $sz (Join-Path $Base 'romfs') } else { 0 }
    if ($bb -gt 0 -and $ub -lt ($bb / 2)) { Add-PkpCheck WARN 'Update romfs' ('{0:N1} GiB, base {1:N1} GiB' -f ($ub / 1GB), ($bb / 1GB)) 'Probably a bare patch: extract the update together with the base so its romfs is complete.' }
    else { Add-PkpCheck OK 'Update' $Update '' }
}
foreach ($p in @($Root, $Dump, $Base)) {
    if (-not $p) { continue }
    $g = Test-InsideGit $p
    if ($g) { Add-PkpCheck FAIL 'Outside git' "$p is inside the git checkout $g" 'Keep game files and the dump outside any repository, so they can never be committed.'; break }
}

# --- P1: building the patched runtime --------------------------------------------------------------
if ($P1) {
    $llvm = Join-Path $env:ProgramFiles 'LLVM\bin'
    $tools = [ordered]@{
        'git'      = @((Find-PkpTool 'git' @((Join-Path $env:ProgramFiles 'Git\cmd\git.exe'))), 'winget install Git.Git')
        'cmake'    = @((Find-PkpTool 'cmake' @((Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'))), 'winget install Kitware.CMake')
        'ninja'    = @((Find-PkpTool 'ninja' @()), 'winget install Ninja-build.Ninja')
        'clang-cl' = @((Find-PkpTool 'clang-cl' @((Join-Path $llvm 'clang-cl.exe'))), 'winget install LLVM.LLVM (installs to C:\Program Files\LLVM)')
        'python'   = @((Find-PkpTool 'python' @()), 'winget install Python.Python.3.12 (some runtime build steps generate files)')
    }
    foreach ($k in $tools.Keys) {
        $t = $tools[$k]
        if ($t[0]) { Add-PkpCheck OK "P1 tool: $k" $t[0] '' } else { Add-PkpCheck FAIL "P1 tool: $k" 'not found' $t[1] }
    }
    $vc = Find-PkpVcVars
    if ($vc) { Add-PkpCheck OK 'P1 tool: MSVC x64' $vc '' }
    else { Add-PkpCheck FAIL 'P1 tool: MSVC x64' 'Visual Studio C++ x64 tools not found' 'Install Build Tools for Visual Studio (2022 or 2026) with "Desktop development with C++" (clang-cl links with its libraries).' }
}

# --- the console (optional) -----------------------------------------------------------------------
if ($ConsoleIp) {
    try {
        $ok = Test-NetConnection -ComputerName $ConsoleIp -Port 11443 -InformationLevel Quiet -WarningAction SilentlyContinue
        if ($ok) { Add-PkpCheck OK 'Xbox Device Portal' "https://${ConsoleIp}:11443 answers" '' }
        else { Add-PkpCheck WARN 'Xbox Device Portal' "no answer at ${ConsoleIp}:11443" 'On the console: Dev Home -> Remote Access -> enable Device Portal. Same network as this PC.' }
    } catch { Add-PkpCheck WARN 'Xbox Device Portal' 'could not test' '' }
}

$fails = @($PkpChecks | Where-Object State -eq 'FAIL').Count
$warns = @($PkpChecks | Where-Object State -eq 'WARN').Count
Write-Host ''
if ($fails) { Write-Host "$fails problem(s) to fix first, $warns warning(s)." -ForegroundColor Red }
else { Write-Host "Ready. $warns warning(s)." -ForegroundColor Green }
# For pokopia.ps1: what was found.
$global:PkpDoctor = [pscustomobject]@{ RecompDir = $RecompDir; Base = $Base; Update = $Update; Fails = $fails }
exit ([int]($fails -gt 0))
