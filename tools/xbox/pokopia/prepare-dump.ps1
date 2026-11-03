# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lays out your own, already extracted copy of Pokemon Pokopia the way pokopia-recomp's launcher
# reads it (launcher/src-tauri/src/dump.rs):
#
#   <Out>\base\{exefs,romfs}
#   <Out>\updates\<UpdateVersion>\{exefs,romfs}
#
# It links your folders with directory junctions instead of copying them, so it takes no extra disk
# space and never writes into your files. It does NOT decrypt or extract anything: .nup, .nsp, .xci,
# .nca and .nsz are encrypted containers and must first be extracted on your own machine, with your
# own console's keys, using the dumping tools you already use. The recomp wants version 2.0.1a, so
# extract the 2.0.1a update together with the base (a complete update romfs, not a bare patch).
#
#   pwsh -File tools\xbox\pokopia\prepare-dump.ps1 -Base D:\pkp\base -Update D:\pkp\update-2.0.1a `
#        -Out D:\pkp\dump
#
# -Base / -Update take a folder that has exefs\ and romfs\ inside it. If your exefs and romfs live in
# separate places, pass -BaseExefs/-BaseRomfs (and -UpdateExefs/-UpdateRomfs) instead.
# A dump-manifest.json with SHA-256 hashes of the exefs files is written next to the layout, so you
# can tell later exactly which files a build came from. Nothing is uploaded anywhere.

param([string] $Base, [string] $BaseExefs, [string] $BaseRomfs,
      [string] $Update, [string] $UpdateExefs, [string] $UpdateRomfs,
      [string] $UpdateVersion = '2.0.1a',
      [Parameter(Mandatory = $true)] [string] $Out,
      [switch] $Force)
$ErrorActionPreference = 'Stop'

$ExefsFiles = @('main', 'sdk', 'subsdk0', 'rtld', 'main.npdm')  # what dump.rs checks
$Containers = @('*.nup', '*.nsp', '*.nsz', '*.xci', '*.xcz', '*.nca')

function Fail([string] $message) { Write-Host "ERROR: $message" -ForegroundColor Red; exit 1 }

function Test-Containers([string] $dir) {
    if (-not $dir -or -not (Test-Path -LiteralPath $dir -PathType Container)) { return }
    $found = Get-ChildItem -LiteralPath $dir -File -Include $Containers -Recurse -Depth 1 -ErrorAction SilentlyContinue |
        Select-Object -First 3
    if ($found) {
        $names = ($found | ForEach-Object Name) -join ', '
        Fail ("$dir holds encrypted containers ($names). This script does not decrypt. Extract them on " +
              "your own PC with your own keys first, then point -Base/-Update at the extracted folder " +
              "(the one with exefs\ and romfs\ inside).")
    }
}

function Resolve-Part([string] $root, [string] $explicit, [string] $name, [string] $what) {
    if ($explicit) { $p = $explicit } elseif ($root) { $p = Join-Path $root $name } else { return $null }
    if (-not (Test-Path -LiteralPath $p -PathType Container)) { Fail "$what folder not found: $p" }
    return (Resolve-Path -LiteralPath $p).ProviderPath
}

function Test-Exefs([string] $exefs, [string] $what) {
    $missing = @($ExefsFiles | Where-Object { -not (Test-Path -LiteralPath (Join-Path $exefs $_) -PathType Leaf) })
    if ($missing.Count) { Fail "$what exefs at $exefs is missing: $($missing -join ', ')" }
}

function Get-TreeSize([string] $dir) {
    $sum = 0L; $count = 0
    Get-ChildItem -LiteralPath $dir -File -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { $sum += $_.Length; $count++ }
    return [pscustomobject]@{ Files = $count; Bytes = $sum }
}

function New-Link([string] $link, [string] $target) {
    if (Test-Path -LiteralPath $link) {
        $item = Get-Item -LiteralPath $link -Force
        if ($item.LinkType -eq 'Junction' -and $item.Target -contains $target) { return }
        if (-not $Force) { Fail "$link already exists. Re-run with -Force to replace the link (your files are not touched)." }
        if ($item.LinkType) { $item.Delete() } else { Fail "$link is a real folder, not a link. Pick an empty -Out." }
    }
    New-Item -ItemType Junction -Path $link -Target $target | Out-Null
}

# --- inputs -------------------------------------------------------------------------------------
foreach ($d in @($Base, $BaseExefs, $BaseRomfs, $Update, $UpdateExefs, $UpdateRomfs)) { Test-Containers $d }

$bExefs = Resolve-Part $Base $BaseExefs 'exefs' 'Base exefs'
$bRomfs = Resolve-Part $Base $BaseRomfs 'romfs' 'Base romfs'
if (-not $bExefs -or -not $bRomfs) { Fail 'Give -Base (a folder with exefs\ and romfs\) or both -BaseExefs and -BaseRomfs.' }
Test-Exefs $bExefs 'Base'

$uExefs = Resolve-Part $Update $UpdateExefs 'exefs' 'Update exefs'
$uRomfs = Resolve-Part $Update $UpdateRomfs 'romfs' 'Update romfs'
$hasUpdate = [bool]($uExefs -or $uRomfs)
if ($hasUpdate) {
    if (-not ($uExefs -and $uRomfs)) { Fail 'The update needs both exefs and romfs.' }
    Test-Exefs $uExefs 'Update'
} else {
    Write-Host "WARNING: no update given. pokopia-recomp is built for version 2.0.1a; a base-only dump" -ForegroundColor Yellow
    Write-Host "         will only work if your base already is 2.0.1a." -ForegroundColor Yellow
}

# --- output must stay out of any git checkout, so game files can never be committed -------------
$outFull = [IO.Path]::GetFullPath($Out)
$probe = $outFull
while ($probe) {
    if (Test-Path -LiteralPath (Join-Path $probe '.git')) { Fail "$outFull is inside the git checkout $probe. Put the dump outside any repository." }
    $probe = Split-Path -Parent $probe
}
New-Item -ItemType Directory -Force -Path $outFull | Out-Null

# --- layout -------------------------------------------------------------------------------------
$baseDir = Join-Path $outFull 'base'
New-Item -ItemType Directory -Force -Path $baseDir | Out-Null
New-Link (Join-Path $baseDir 'exefs') $bExefs
New-Link (Join-Path $baseDir 'romfs') $bRomfs
if ($hasUpdate) {
    $updDir = Join-Path (Join-Path $outFull 'updates') $UpdateVersion
    New-Item -ItemType Directory -Force -Path $updDir | Out-Null
    New-Link (Join-Path $updDir 'exefs') $uExefs
    New-Link (Join-Path $updDir 'romfs') $uRomfs
}

# --- checks + manifest --------------------------------------------------------------------------
$baseRomfsSize = Get-TreeSize $bRomfs
$manifest = [ordered]@{
    created = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    note = 'Hashes of your own files, for your records. Never commit or share the dump.'
    base = [ordered]@{
        exefs = $bExefs; romfs = $bRomfs
        romfs_files = $baseRomfsSize.Files; romfs_bytes = $baseRomfsSize.Bytes
        sha256 = [ordered]@{}
    }
}
foreach ($f in $ExefsFiles) { $manifest.base.sha256[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $bExefs $f)).Hash }
if ($hasUpdate) {
    $updRomfsSize = Get-TreeSize $uRomfs
    $manifest.update = [ordered]@{
        version = $UpdateVersion; exefs = $uExefs; romfs = $uRomfs
        romfs_files = $updRomfsSize.Files; romfs_bytes = $updRomfsSize.Bytes
        sha256 = [ordered]@{}
    }
    foreach ($f in $ExefsFiles) { $manifest.update.sha256[$f] = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $uExefs $f)).Hash }
    # A bare update patch has a romfs far smaller than the base; the launcher wants a complete one.
    if ($baseRomfsSize.Bytes -gt 0 -and $updRomfsSize.Bytes -lt ($baseRomfsSize.Bytes / 2)) {
        Write-Host ("WARNING: the update romfs ({0:N1} GiB) is less than half the base romfs ({1:N1} GiB). It is probably" -f
                    ($updRomfsSize.Bytes / 1GB), ($baseRomfsSize.Bytes / 1GB)) -ForegroundColor Yellow
        Write-Host "         an unmerged patch. Re-extract the update together with the base so its romfs is complete." -ForegroundColor Yellow
    }
}
$manifestPath = Join-Path $outFull 'dump-manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

Write-Host ''
Write-Host "Dump ready: $outFull" -ForegroundColor Green
Write-Host "  base\exefs  -> $bExefs"
Write-Host "  base\romfs  -> $bRomfs"
if ($hasUpdate) {
    Write-Host "  updates\$UpdateVersion\exefs -> $uExefs"
    Write-Host "  updates\$UpdateVersion\romfs -> $uRomfs"
}
Write-Host "  manifest    -> $manifestPath"
Write-Host "In PokopiaLauncher, pick $outFull as the dump folder."
