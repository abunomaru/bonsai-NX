# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# One command for the whole P0 process of docs/xbox/xbox_pokopia_recomp_scope.md, on your own PC:
#
#   1. lay out your extracted game files for the recomp (prepare-dump.ps1);
#   2. compile the game and build its shader cache with pokopia-recomp's own launcher;
#   3. run the 5120 MiB memory gate twice: once without a limit (baseline) and once capped.
#
# Run it again at any point: every step checks what is already done and continues from there.
#
#   pwsh -File tools\xbox\pokopia\pokopia.ps1 -RecompDir D:\pkp\recomp `
#        -Base D:\pkp\base -Update D:\pkp\update-2.0.1a -Dump D:\pkp\dump
#
# -RecompDir is the unzipped pokopia-recomp release (the folder with PokopiaLauncher.exe), from
# https://github.com/drakolordx7/pokopia-recomp/releases/latest. -GameDir defaults to it, which is
# where the launcher puts pkp.exe unless you changed the game folder in its Settings.
# -Base/-Update are your already extracted folders (exefs\ + romfs\); see prepare-dump.ps1. Skip them
# if -Dump is already laid out. Nothing here decrypts, uploads or copies your game files.

param([Parameter(Mandatory = $true)] [string] $RecompDir,
      [string] $GameDir,
      [string] $Base, [string] $Update, [string] $UpdateVersion = '2.0.1a',
      [Parameter(Mandatory = $true)] [string] $Dump,
      [ValidateRange(256, 1048576)] [int] $LimitMiB = 5120,
      [ValidateRange(1, 1440)] [int] $SetupTimeoutMin = 240,
      [switch] $SkipBaseline)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path

function Step([string] $text) { Write-Host ''; Write-Host "== $text" -ForegroundColor Cyan }

$RecompDir = (Resolve-Path -LiteralPath $RecompDir).ProviderPath
if (-not $GameDir) { $GameDir = $RecompDir }
$launcher = Join-Path $RecompDir 'PokopiaLauncher.exe'
if (-not (Test-Path -LiteralPath $launcher -PathType Leaf)) {
    Write-Host "ERROR: $launcher not found. Unzip the latest pokopia-recomp release into -RecompDir." -ForegroundColor Red
    exit 1
}

# --- 1. dump ------------------------------------------------------------------------------------
Step '1/3 Game files'
$dumpReady = Test-Path -LiteralPath (Join-Path $Dump 'base\exefs\main') -PathType Leaf
if ($Base) {
    $args1 = @{ Base = $Base; Out = $Dump; UpdateVersion = $UpdateVersion; Force = $true }
    if ($Update) { $args1.Update = $Update }
    & (Join-Path $here 'prepare-dump.ps1') @args1
    if ($LASTEXITCODE) { exit $LASTEXITCODE }
} elseif ($dumpReady) {
    Write-Host "Using the existing layout in $Dump."
} else {
    Write-Host "ERROR: $Dump is not laid out yet. Pass -Base (and -Update) with your extracted folders." -ForegroundColor Red
    exit 1
}
$Dump = (Resolve-Path -LiteralPath $Dump).ProviderPath

# --- 2. compile + shader cache (the recomp's launcher does this; we wait for its markers) --------
# Markers, from launcher/src-tauri/src/gamebuild.rs and shaders.rs:
#   pkp.exe + build\done.json                           -> game compiled
#   shader_cache\setup.json "complete": true + spirv\results.tsv -> shader cache built
function Test-Built { (Test-Path -LiteralPath (Join-Path $GameDir 'pkp.exe')) -and (Test-Path -LiteralPath (Join-Path $GameDir 'build\done.json')) }
function Test-Shaders {
    $marker = Join-Path $GameDir 'shader_cache\setup.json'
    if (-not (Test-Path -LiteralPath $marker) -or -not (Test-Path -LiteralPath (Join-Path $GameDir 'shader_cache\spirv\results.tsv'))) { return $false }
    try { return [bool]((Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json).complete) } catch { return $false }
}

Step '2/3 Compile the game and build the shader cache'
if ((Test-Built) -and (Test-Shaders)) {
    Write-Host 'Already done.'
} else {
    Write-Host 'Opening PokopiaLauncher. In it:' -ForegroundColor Yellow
    Write-Host "  - pick this dump folder: $Dump" -ForegroundColor Yellow
    Write-Host '  - press Set up (about 7 minutes to compile, then 1-2 hours of shaders; you can pause it)' -ForegroundColor Yellow
    Write-Host '  - do not press Play yet; this script starts the game itself for the measurement' -ForegroundColor Yellow
    Set-Clipboard -Value $Dump -ErrorAction SilentlyContinue
    Write-Host '(The dump path is on your clipboard.)'
    Start-Process -FilePath $launcher -WorkingDirectory $RecompDir
    $deadline = (Get-Date).AddMinutes($SetupTimeoutMin)
    $lastBuilt = $false
    while (-not ((Test-Built) -and (Test-Shaders))) {
        if ((Get-Date) -gt $deadline) {
            Write-Host "Setup is not finished after $SetupTimeoutMin minutes. Let it finish, then run this script again." -ForegroundColor Yellow
            exit 2
        }
        $built = Test-Built
        if ($built -and -not $lastBuilt) { Write-Host "$(Get-Date -Format HH:mm) game compiled; waiting for the shader cache..." }
        $lastBuilt = $built
        Start-Sleep -Seconds 30
    }
    Write-Host 'Setup finished.' -ForegroundColor Green
    Get-Process -Name 'PokopiaLauncher' -ErrorAction SilentlyContinue | ForEach-Object { $_.CloseMainWindow() | Out-Null }
}

# --- 3. memory gate -----------------------------------------------------------------------------
Step "3/3 Memory gate ($LimitMiB MiB)"
$gate = Join-Path $here 'memory-gate.ps1'
$results = Join-Path $GameDir 'memory-gate'
if (-not $SkipBaseline) {
    Write-Host 'Run A, no limit: play your heaviest areas for a while, then close the game.' -ForegroundColor Yellow
    & $gate -GameDir $GameDir -NoLimit -OutDir (Join-Path $results 'A-baseline')
}
Write-Host "Run B, $LimitMiB MiB cap: same areas, same length if you can, then close the game." -ForegroundColor Yellow
& $gate -GameDir $GameDir -LimitMiB $LimitMiB -OutDir (Join-Path $results 'B-capped')

Write-Host ''
Write-Host "Done. Results are in $results (summary.txt and samples.csv for each run)." -ForegroundColor Green
Write-Host 'Send back the two summary.txt files (they contain no game data) and say what you were doing'
Write-Host 'in the game at the peaks. Do not send pkp.log if it lists your own file paths you want private.'
