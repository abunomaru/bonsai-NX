# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared helpers of the Pokopia scripts (dot-sourced; not run on its own). Finding things in one
# folder, the 2.0.1a hashes, and the OK / WARN / FAIL lines of doctor.ps1.

$script:PkpRecompTag = 'v0.1.1'
$script:PkpRecompCommit = '2c29515b23a6d78d967775fd7e9f24f50402cca7'
$script:PkpRecompUrl = 'https://github.com/drakolordx7/pokopia-recomp'
$script:PkpExefsFiles = @('main', 'sdk', 'subsdk0', 'rtld', 'main.npdm')
# SHA-256 of the 2.0.1a ExeFS, as published by pokopia-recomp in game-build/release_b/dump_hashes.json
# (program 0400A3A01EE4C000, "2.0.1 (update 655360)"). A release's own copy of that file wins.
$script:PkpHashes201a = @{
    'main'      = '8a8ce87b4edfce88c6260da5dd80d6cafc03a761eebc842d7a3476e353a8336f'
    'sdk'       = '7c12078e9f853f5d77f395155f7308f264fa6e93fadaeba8dca525f4483c09b2'
    'subsdk0'   = '2e91ffc8b6f036ffd34fdb3c3dc280dec80956308e34ea866167650842c9f3be'
    'rtld'      = '166d3b75ec5559cd241cbacf03dbf16c267a2cbfc95c33c41ba02d3a03c5616f'
    'main.npdm' = 'de024cc0dcdf3480b1fca447344fce0a35f49aaffe842c25e5fb4c6d359d42b4'
}

function Get-PkpHashes([string] $recompDir) {
    if ($recompDir) {
        $f = Get-ChildItem -LiteralPath $recompDir -Filter 'dump_hashes.json' -File -Recurse -Depth 3 -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($f) {
            try {
                $j = Get-Content -LiteralPath $f.FullName -Raw | ConvertFrom-Json
                $h = @{}
                foreach ($n in $script:PkpExefsFiles) { if ($j.sha256.$n) { $h[$n] = ([string]$j.sha256.$n).ToLowerInvariant() } }
                if ($h.Count -eq $script:PkpExefsFiles.Count) { return $h }
            } catch { }
        }
    }
    return $script:PkpHashes201a
}

# The unzipped release: the folder holding PokopiaLauncher.exe, under $root (a few levels deep).
function Find-PkpRecomp([string] $root) {
    if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { return $null }
    $hit = Get-ChildItem -LiteralPath $root -Filter 'PokopiaLauncher.exe' -File -Recurse -Depth 3 -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($hit) { return $hit.DirectoryName }
    return $null
}

# Every folder under $root with exefs\main and romfs\ (an extracted title), skipping the recomp, the laid-out
# dump (its junctions point at the real folders) and anything named build/work.
function Find-PkpExtracted([string] $root, [string] $skip) {
    $out = @()
    if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { return $out }
    $mains = Get-ChildItem -LiteralPath $root -Filter 'main' -File -Recurse -Depth 5 -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'exefs' }
    foreach ($m in $mains) {
        $title = $m.Directory.Parent.FullName
        if ($skip -and $title.StartsWith($skip, [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ($title -match '\\(dump|recomp-src|build)(\\|$)') { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $title 'romfs') -PathType Container)) { continue }
        $out += $title
    }
    return $out
}

# 'update' when exefs\main hashes to 2.0.1a, otherwise 'base' (an older version: the base the update sits on).
function Get-PkpTitleKind([string] $title, [hashtable] $hashes) {
    $h = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $title 'exefs\main')).Hash.ToLowerInvariant()
    if ($h -eq $hashes['main']) { return 'update' }
    return 'base'
}

# Finds base and update under $root. Returns @{ Base; Update; Note }.
function Find-PkpGameParts([string] $root, [string] $recompDir, [hashtable] $hashes) {
    $titles = @(Find-PkpExtracted $root $recompDir)
    $r = @{ Base = $null; Update = $null; Note = '' }
    if ($titles.Count -eq 0) { $r.Note = "no extracted folder (exefs\main + romfs\) under $root"; return $r }
    foreach ($t in $titles) {
        $kind = Get-PkpTitleKind $t $hashes
        if ($kind -eq 'update' -and -not $r.Update) { $r.Update = $t }
        elseif ($kind -eq 'base' -and -not $r.Base) { $r.Base = $t }
    }
    # A single extracted 2.0.1a title (base and update merged when extracting) serves as the base on its own.
    if (-not $r.Base -and $r.Update) { $r.Base = $r.Update; $r.Update = $null; $r.Note = 'one merged 2.0.1a title, used as the base' }
    return $r
}

function Test-InsideGit([string] $path) {
    $probe = [IO.Path]::GetFullPath($path)
    while ($probe) {
        if (Test-Path -LiteralPath (Join-Path $probe '.git')) { return $probe }
        $probe = Split-Path -Parent $probe
    }
    return $null
}

# The Visual Studio vcvars64.bat with the C++ x64 tools, via vswhere.
function Find-PkpVcVars {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $inst = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null |
        Select-Object -First 1
    if (-not $inst) { return $null }
    $bat = Join-Path $inst 'VC\Auxiliary\Build\vcvars64.bat'
    if (Test-Path -LiteralPath $bat) { return $bat }
    return $null
}

function Find-PkpTool([string] $name, [string[]] $extra) {
    $c = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($c) { return $c.Source }
    foreach ($p in $extra) { if ($p -and (Test-Path -LiteralPath $p -PathType Leaf)) { return $p } }
    return $null
}

$script:PkpChecks = New-Object System.Collections.Generic.List[object]
function Add-PkpCheck([string] $state, [string] $what, [string] $detail, [string] $fix) {
    $script:PkpChecks.Add([pscustomobject]@{ State = $state; What = $what; Detail = $detail; Fix = $fix })
    $color = @{ OK = 'Green'; WARN = 'Yellow'; FAIL = 'Red'; INFO = 'Gray' }[$state]
    Write-Host ('[{0,-4}] {1}: {2}' -f $state, $what, $detail) -ForegroundColor $color
    if ($fix -and $state -ne 'OK') { Write-Host "       -> $fix" -ForegroundColor $color }
}
