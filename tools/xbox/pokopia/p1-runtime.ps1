# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# P1 of docs/xbox/xbox_pokopia_recomp_scope.md on your PC: builds pokopia-recomp's runtime
# (pkp_rt.dll) from its v0.1.1 source with patches\0001-mem-private-heap-and-fromapp.patch, and puts
# it next to your pkp.exe. The original DLL is kept as pkp_rt.dll.orig; -Revert puts it back.
# The runtime holds no game code, and the game itself is not rebuilt: pkp.exe imports the same
# exports from the patched DLL.
#
#   pwsh -File tools\xbox\pokopia\p1-runtime.ps1 -GameDir C:\...\pkp-xbox\recomp
#   pwsh -File tools\xbox\pokopia\p1-runtime.ps1 -GameDir C:\...\pkp-xbox\recomp -Revert
#
# Needs: git, CMake, Ninja, LLVM (clang-cl) and the Visual Studio C++ x64 tools (doctor.ps1 -P1 checks them).
# The source goes to -WorkDir (default: recomp-src next to -GameDir), outside any git checkout of yours.
# The patched runtime only changes the heap when asked: set NXRT_MEMORY_HEAP_BACKING=private (memory-gate.ps1
# -PrivateHeap does it for its run). Without it the game runs exactly as before.

param([Parameter(Mandatory = $true)] [string] $GameDir,
      [string] $WorkDir,
      [switch] $Revert,
      [switch] $Test)
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion.Major -lt 7) {
    # Windows PowerShell 5.1 turns git/cmake messages on stderr into errors under 'Stop'.
    Write-Host 'ERROR: run this with PowerShell 7 (pwsh): winget install Microsoft.PowerShell' -ForegroundColor Red; exit 1
}
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here 'common.ps1')

function Fail([string] $m) { Write-Host "ERROR: $m" -ForegroundColor Red; exit 1 }
function Step([string] $t) { Write-Host ''; Write-Host "== $t" -ForegroundColor Cyan }

$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$dll = Get-ChildItem -LiteralPath $GameDir -Filter 'pkp_rt.dll' -File -Recurse -Depth 2 -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -notmatch '\\(recomp-src|build-p1)\\' } | Select-Object -First 1
if (-not $dll) { Fail "pkp_rt.dll not found under $GameDir. Point -GameDir at the unzipped pokopia-recomp release." }
$dllPath = $dll.FullName
$orig = "$dllPath.orig"

if ($Revert) {
    if (-not (Test-Path -LiteralPath $orig)) { Fail "No ${orig}: the original runtime is already in place." }
    Copy-Item -LiteralPath $orig -Destination $dllPath -Force
    Remove-Item -LiteralPath $orig
    Write-Host "Original pkp_rt.dll restored in $($dll.DirectoryName)." -ForegroundColor Green
    exit 0
}

if (-not $WorkDir) { $WorkDir = Join-Path (Split-Path -Parent $GameDir) 'recomp-src' }
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
# 0001 is the P1 change; 0002 only makes the standalone runtime tests build (-Test).
$patches = @('0001-mem-private-heap-and-fromapp.patch', '0002-build-standalone-runtime-tests.patch') | ForEach-Object { Join-Path $here "patches\$_" }
foreach ($p in $patches) { if (-not (Test-Path -LiteralPath $p)) { Fail "$p is missing from this checkout." } }

Step '1/4 Tools'
$git = Find-PkpTool 'git' @((Join-Path $env:ProgramFiles 'Git\cmd\git.exe'))
$cmake = Find-PkpTool 'cmake' @((Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'))
$ninja = Find-PkpTool 'ninja' @()
$clang = Find-PkpTool 'clang-cl' @((Join-Path $env:ProgramFiles 'LLVM\bin\clang-cl.exe'))
$vcvars = Find-PkpVcVars
$missing = @()
if (-not $git) { $missing += 'git' }; if (-not $cmake) { $missing += 'cmake' }; if (-not $ninja) { $missing += 'ninja' }
if (-not $clang) { $missing += 'clang-cl (LLVM)' }; if (-not $vcvars) { $missing += 'Visual Studio C++ x64 tools' }
if ($missing.Count) { Fail "Missing: $($missing -join ', '). Run doctor.ps1 -P1 for the install commands." }
Write-Host "clang-cl: $clang"; Write-Host "vcvars:   $vcvars"

Step "2/4 Source ($PkpRecompTag, $($PkpRecompCommit.Substring(0, 7)))"
if (-not (Test-Path -LiteralPath (Join-Path $WorkDir '.git'))) {
    & $git clone --filter=blob:none --no-checkout $PkpRecompUrl $WorkDir
    if ($LASTEXITCODE) { Fail 'git clone failed.' }
}
& $git -C $WorkDir fetch --quiet origin "refs/tags/${PkpRecompTag}:refs/tags/$PkpRecompTag" 2>$null
& $git -C $WorkDir checkout --quiet --force $PkpRecompCommit
if ($LASTEXITCODE) { Fail "Could not check out $PkpRecompCommit in $WorkDir." }
& $git -C $WorkDir clean -fdq -- runtime game-build/src
foreach ($p in $patches) {
    & $git -C $WorkDir apply $p
    if ($LASTEXITCODE) { Fail "$(Split-Path -Leaf $p) does not apply to $PkpRecompCommit in $WorkDir." }
}
Write-Host 'Patches applied.'

Step '3/4 Build pkp_rt.dll (several minutes the first time)'
$bd = Join-Path $WorkDir 'build-p1'
$llvmBin = Split-Path -Parent $clang
# vcvars64.bat calls vswhere.exe, and the texture scripts expect LLVM, CMake and Ninja on PATH.
$installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer'
$env:PATH = "$llvmBin;$installer;$(Split-Path -Parent $cmake);$(Split-Path -Parent $ninja);$env:PATH"
# The runtime links astc2bc7.lib from tools\textures: fetch its third-party sources and build it first.
Push-Location $WorkDir
try {
    & pwsh -NoProfile -File (Join-Path $WorkDir 'tools\textures\scripts\fetch_third_party.ps1')
    if ($LASTEXITCODE) { Fail 'tools\textures\scripts\fetch_third_party.ps1 failed.' }
    & pwsh -NoProfile -File (Join-Path $WorkDir 'tools\textures\scripts\build.ps1')
    if ($LASTEXITCODE) { Fail 'tools\textures\scripts\build.ps1 failed.' }
} finally { Pop-Location }
$cfg = "cmake -S `"$WorkDir\game-build`" -B `"$bd`" -G Ninja -DCMAKE_BUILD_TYPE=Release " +
       "-DCMAKE_C_COMPILER=`"$clang`" -DCMAKE_CXX_COMPILER=`"$clang`" -DPKP_RT_DLL=ON -DPKP_VERSION=0.1.1-jadyx-p1"
$build = "cmake --build `"$bd`" --target pkp_rt"
$log = Join-Path $WorkDir 'build-p1.log'
cmd /c "call `"$vcvars`" >nul && set `"PATH=$llvmBin;%PATH%`" && $cfg && $build" 2>&1 | Tee-Object -FilePath $log | Select-Object -Last 15
if ($LASTEXITCODE) { Fail "The build failed. The full output is in $log." }
$newDll = Get-ChildItem -LiteralPath $bd -Filter 'pkp_rt.dll' -File -Recurse | Select-Object -First 1
if (-not $newDll) { Fail "The build finished but no pkp_rt.dll is in $bd." }

if ($Test) {
    Write-Host 'Building and running the runtime memory tests...'
    $tb = Join-Path $WorkDir 'build-p1-tests'
    $tcfg = "cmake -S `"$WorkDir\runtime`" -B `"$tb`" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=`"$clang`" " +
            "-DCMAKE_CXX_COMPILER=`"$clang`" -DNXRT_RECOMP_INCLUDE=`"$WorkDir\recomp\include`" -DNXRT_BUILD_LIFT_TESTS=OFF " +
            "-DNXRT_GAMES=pkp -DNXRT_LAUNCHER_GAME=pkp"
    cmd /c "call `"$vcvars`" >nul && set `"PATH=$llvmBin;%PATH%`" && $tcfg && cmake --build `"$tb`" --target nxrt_tests" 2>&1 |
        Tee-Object -FilePath (Join-Path $WorkDir 'build-p1-tests.log') | Select-Object -Last 5
    $exe = Get-ChildItem -LiteralPath $tb -Filter 'nxrt_tests.exe' -File -Recurse | Select-Object -First 1
    if (-not $exe) { Fail 'The test build failed; see build-p1-tests.log.' }
    & $exe.FullName memory
    if ($LASTEXITCODE) { Fail "The memory tests failed (exit $LASTEXITCODE). The original runtime is untouched." }
}

Step '4/4 Install next to pkp.exe'
if (-not (Test-Path -LiteralPath $orig)) { Copy-Item -LiteralPath $dllPath -Destination $orig }
Copy-Item -LiteralPath $newDll.FullName -Destination $dllPath -Force
Write-Host "Patched runtime installed: $dllPath (original kept as pkp_rt.dll.orig)." -ForegroundColor Green
Write-Host 'Nothing changes until the private heap is asked for. To measure it:'
Write-Host "  pwsh -File $here\memory-gate.ps1 -GameDir $GameDir -PrivateHeap -NoLimit"
