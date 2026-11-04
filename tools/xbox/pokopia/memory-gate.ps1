# SPDX-FileCopyrightText: Copyright 2026 Jadyx Project
# SPDX-License-Identifier: GPL-3.0-or-later
#
# P0 gate of docs/xbox/xbox_pokopia_recomp_scope.md: does pokopia-recomp fit the 5120 MiB an Xbox
# Dev Mode game gets? Starts pkp.exe suspended, puts it in a job object capped at -LimitMiB of
# committed memory before it runs a single instruction, resumes it and samples its memory once per
# -SampleMs until the game exits. You play normally (heaviest areas, menus, saving) and close the game
# when you are done. Same idea as tools\xbox\process-memory-limit.ps1, but it keeps the job handle open
# so it can read the peaks back.
#
#   pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp            # capped run
#   pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -NoLimit   # baseline
#   pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -PrivateHeap   # P1: needs p1-runtime.ps1 first
#
# What it records (CSV + summary in -OutDir):
#   - private commit and working set of pkp.exe, and the job's peak committed memory;
#   - GPU dedicated and shared memory of the process (performance counters "GPU Process Memory").
# On the Series the GPU shares the same 5 GiB, so the verdict adds peak working set and peak GPU
# dedicated memory. A PC job limit caps commit only; it does not emulate unified memory, and memory of
# the game's page-file-backed heap section may be outside the job's private-commit accounting. Read the
# verdict as an estimate, and run it once with -NoLimit to see the unconstrained peak.

param([Parameter(Mandatory = $true)] [string] $GameDir,
      [ValidateRange(256, 1048576)] [int] $LimitMiB = 5120,
      [switch] $NoLimit,
      [ValidateRange(200, 60000)] [int] $SampleMs = 1000,
      [ValidateRange(1, 60)] [int] $GpuEvery = 2,
      [switch] $PrivateHeap,
      [string] $OutDir)
$ErrorActionPreference = 'Stop'

$GameDir = (Resolve-Path -LiteralPath $GameDir).ProviderPath
$exe = Join-Path $GameDir 'pkp.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { Write-Host "ERROR: $exe not found. Finish Set up in PokopiaLauncher first." -ForegroundColor Red; exit 1 }
if (-not $OutDir) { $OutDir = Join-Path $GameDir ('memory-gate-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not ('Jadyx.GateLauncher' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace Jadyx {
    public static class GateLauncher {
        [StructLayout(LayoutKind.Sequential)]
        struct Basic {
            public long ProcessTime, JobTime;
            public uint Flags;
            public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
            public uint ActiveProcesses;
            public UIntPtr Affinity;
            public uint Priority, Scheduling;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct Io { public ulong ReadOps, WriteOps, OtherOps, ReadBytes, WriteBytes, OtherBytes; }
        [StructLayout(LayoutKind.Sequential)]
        struct Extended {
            public Basic Basic;
            public Io Io;
            public UIntPtr ProcessMemory, JobMemory, PeakProcess, PeakJob;
        }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct StartupInfo {
            public int cb;
            public string Reserved, Desktop, Title;
            public int X, Y, XSize, YSize, XCountChars, YCountChars, FillAttribute, Flags;
            public short ShowWindow, Reserved2;
            public IntPtr Reserved3, StdInput, StdOutput, StdError;
        }
        [StructLayout(LayoutKind.Sequential)]
        struct ProcessInfo { public IntPtr Process, Thread; public int ProcessId, ThreadId; }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcessW(string app, System.Text.StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit,
                                          uint flags, IntPtr env, string dir, ref StartupInfo si, out ProcessInfo pi);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int kind, ref Extended info, uint bytes);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool QueryInformationJobObject(IntPtr job, int kind, out Extended info, uint bytes, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr handle);

        static IntPtr job, process;
        public static int ProcessId;
        static void Check(bool ok) { if (!ok) throw new Win32Exception(Marshal.GetLastWin32Error()); }

        // limitBytes == 0: no cap, the job is still used to read the peak.
        public static int Start(string exe, string args, string dir, ulong limitBytes) {
            if (IntPtr.Size != 8) throw new InvalidOperationException("Run this from 64-bit PowerShell.");
            job = CreateJobObjectW(IntPtr.Zero, null);
            Check(job != IntPtr.Zero);
            uint size = (uint)Marshal.SizeOf(typeof(Extended));
            Extended limits = new Extended();
            if (limitBytes != 0) {
                limits.Basic.Flags = 0x100;  // JOB_OBJECT_LIMIT_PROCESS_MEMORY
                limits.ProcessMemory = new UIntPtr(limitBytes);
                Check(SetInformationJobObject(job, 9, ref limits, size));
            }
            StartupInfo si = new StartupInfo();
            si.cb = Marshal.SizeOf(typeof(StartupInfo));
            ProcessInfo pi;
            var cmd = new System.Text.StringBuilder("\"" + exe + "\" " + args);
            Check(CreateProcessW(exe, cmd, IntPtr.Zero, IntPtr.Zero, false, 0x4 /* CREATE_SUSPENDED */,
                                 IntPtr.Zero, dir, ref si, out pi));
            process = pi.Process;
            ProcessId = pi.ProcessId;
            if (!AssignProcessToJobObject(job, process)) {
                int e = Marshal.GetLastWin32Error();
                TerminateProcess(process, 1);
                throw new Win32Exception(e);
            }
            if (limitBytes != 0) {
                Extended actual;
                Check(QueryInformationJobObject(job, 9, out actual, size, IntPtr.Zero));
                if (actual.ProcessMemory.ToUInt64() != limitBytes) {
                    TerminateProcess(process, 1);
                    throw new InvalidOperationException("Memory limit verification failed.");
                }
            }
            ResumeThread(pi.Thread);
            CloseHandle(pi.Thread);
            return ProcessId;
        }
        public static ulong PeakProcessBytes() {
            Extended info;
            Check(QueryInformationJobObject(job, 9, out info, (uint)Marshal.SizeOf(typeof(Extended)), IntPtr.Zero));
            return info.PeakProcess.ToUInt64();
        }
        public static long ExitCode() {
            uint code;
            if (!GetExitCodeProcess(process, out code)) return -1;
            return code == 259 ? -2 : (long)code;  // 259 = STILL_ACTIVE
        }
        public static void Close() {
            if (process != IntPtr.Zero) CloseHandle(process);
            if (job != IntPtr.Zero) CloseHandle(job);
            process = job = IntPtr.Zero;
        }
    }
}
'@
}

function Get-GpuMemory([int] $processId) {
    # Instance names look like pid_1234_luid_0x..._phys_0; sum over adapters.
    $dedicated = 0L; $shared = 0L
    try {
        $set = Get-Counter -Counter @("\GPU Process Memory(pid_${processId}_*)\Dedicated Usage",
                                      "\GPU Process Memory(pid_${processId}_*)\Shared Usage") -ErrorAction Stop
        foreach ($s in $set.CounterSamples) {
            if ($s.Path -like '*dedicated usage') { $dedicated += [long]$s.CookedValue } else { $shared += [long]$s.CookedValue }
        }
    } catch { return $null }
    return [pscustomobject]@{ Dedicated = $dedicated; Shared = $shared }
}

$limitBytes = if ($NoLimit) { 0 } else { [uint64]$LimitMiB * 1MB }
$csv = Join-Path $OutDir 'samples.csv'
'seconds,private_mib,working_set_mib,gpu_dedicated_mib,gpu_shared_mib' | Set-Content -LiteralPath $csv -Encoding ASCII

$label = if ($NoLimit) { 'no limit (baseline)' } else { "$LimitMiB MiB commit cap" }
# -PrivateHeap: the patched runtime (p1-runtime.ps1) puts the heap in private memory, so the job cap counts it too.
$prevBacking = $env:NXRT_MEMORY_HEAP_BACKING
if ($PrivateHeap) {
    if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'pkp_rt.dll.orig'))) {
        Write-Host 'WARNING: no pkp_rt.dll.orig here, so the patched runtime is probably not installed (run p1-runtime.ps1).' -ForegroundColor Yellow
        Write-Host '         The original runtime ignores the setting and keeps the section heap.' -ForegroundColor Yellow
    }
    $env:NXRT_MEMORY_HEAP_BACKING = 'private'
    $label += ', private heap'
}
Write-Host "Starting pkp.exe with $label. Play your heaviest areas, then close the game normally." -ForegroundColor Cyan
$gamePid = [Jadyx.GateLauncher]::Start($exe, '--log pkp.log', $GameDir, $limitBytes)
$started = Get-Date
$peak = @{ Private = 0L; WorkingSet = 0L; GpuDedicated = 0L; GpuShared = 0L }
$gpuSeen = $false
$i = 0
try {
    while ([Jadyx.GateLauncher]::ExitCode() -eq -2) {
        $p = Get-Process -Id $gamePid -ErrorAction SilentlyContinue
        if (-not $p) { break }
        $p.Refresh()
        $gpu = if (($i % $GpuEvery) -eq 0) { Get-GpuMemory $gamePid } else { $null }
        if ($gpu) { $gpuSeen = $true }
        $priv = $p.PrivateMemorySize64; $ws = $p.WorkingSet64
        $peak.Private = [Math]::Max($peak.Private, $priv)
        $peak.WorkingSet = [Math]::Max($peak.WorkingSet, $p.PeakWorkingSet64)
        if ($gpu) {
            $peak.GpuDedicated = [Math]::Max($peak.GpuDedicated, $gpu.Dedicated)
            $peak.GpuShared = [Math]::Max($peak.GpuShared, $gpu.Shared)
        }
        $t = [int]((Get-Date) - $started).TotalSeconds
        $gd = if ($gpu) { [int]($gpu.Dedicated / 1MB) } else { '' }
        $gs = if ($gpu) { [int]($gpu.Shared / 1MB) } else { '' }
        "$t,$([int]($priv / 1MB)),$([int]($ws / 1MB)),$gd,$gs" | Add-Content -LiteralPath $csv -Encoding ASCII
        if (($i % 30) -eq 0) {
            Write-Host ("{0,5}s  private {1,6} MiB  working set {2,6} MiB  GPU dedicated {3,6} MiB" -f $t, [int]($priv / 1MB), [int]($ws / 1MB), $gd)
        }
        $i++
        Start-Sleep -Milliseconds $SampleMs
    }
    $exitCode = [Jadyx.GateLauncher]::ExitCode()
    $jobPeak = [Jadyx.GateLauncher]::PeakProcessBytes()
} finally {
    [Jadyx.GateLauncher]::Close()
    $env:NXRT_MEMORY_HEAP_BACKING = $prevBacking
}

$mib = { param($b) [int]($b / 1MB) }
$estimate = $peak.WorkingSet + $peak.GpuDedicated
$verdict =
    if ($estimate -le 4608MB) { 'FITS with margin (<= 4608 MiB estimated)' }
    elseif ($estimate -le 5120MB) { 'TIGHT (4608-5120 MiB estimated): needs the memory work in the scope doc' }
    else { 'DOES NOT FIT (> 5120 MiB estimated): stop before any D3D12 work' }
$seconds = [int]((Get-Date) - $started).TotalSeconds
$logPath = Join-Path $GameDir 'pkp.log'
$logTail = if (Test-Path -LiteralPath $logPath) { (Get-Content -LiteralPath $logPath -Tail 40) -join "`n" } else { '(no pkp.log)' }
# Which heap the runtime made (memory.cpp logs "heap: private reservation ..." or "heap: section of ...").
$heapLine = if (Test-Path -LiteralPath $logPath) { Select-String -LiteralPath $logPath -Pattern 'heap: (private reservation|section of)' | Select-Object -First 1 } else { $null }
$heapMode = if (-not $heapLine) { 'not in pkp.log' } elseif ($heapLine.Line -match 'private reservation') { 'private (P1 patch active)' } else { 'section (original behaviour)' }
$p1 = ''
if ($PrivateHeap) {
    $p1 = if ($heapMode -like 'private*' -and $exitCode -eq 0) { 'P1 gate: PASSED (booted and exited cleanly with the private heap)' }
          elseif ($heapMode -like 'private*') { "P1 gate: private heap active, but exit code ${exitCode}: check pkp.log" }
          else { 'P1 gate: NOT RUN (the runtime did not use the private heap)' }
}

$summary = @"
pokopia-recomp memory gate
date            : $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss'))Z
mode            : $label
session         : $seconds s, exit code $exitCode
peak private    : $(& $mib $peak.Private) MiB
job peak commit : $(& $mib $jobPeak) MiB
peak work set   : $(& $mib $peak.WorkingSet) MiB
peak GPU dedic. : $(if ($gpuSeen) { "$(& $mib $peak.GpuDedicated) MiB" } else { 'not available (no GPU counters)' })
peak GPU shared : $(if ($gpuSeen) { "$(& $mib $peak.GpuShared) MiB" } else { 'not available' })
Series estimate : $(& $mib $estimate) MiB (peak working set + peak GPU dedicated)
verdict         : $verdict
heap            : $heapMode
$p1

A non-zero exit code under the cap usually means an allocation failed at the cap: check pkp.log.
Samples: $csv

--- last 40 lines of pkp.log ---
$logTail
"@
$summaryPath = Join-Path $OutDir 'summary.txt'
$summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8
Write-Host ''
Write-Host $summary
Write-Host "Saved to $OutDir" -ForegroundColor Green
