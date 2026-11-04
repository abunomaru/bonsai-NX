# Pokopia recomp on Xbox: the PC-side tools

Scripts for the gates of [`docs/xbox/xbox_pokopia_recomp_scope.md`](../../../docs/xbox/xbox_pokopia_recomp_scope.md):
does [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp) fit in the 5120 MiB an Xbox
Dev Mode game gets (P0), and does it run with a heap the console allows (P1)? Everything runs on
your own Windows PC. Nothing decrypts, uploads or copies your game files, and nothing writes into them.

## Quick start

1. Make one folder, for example `C:\Users\you\Desktop\pkp-xbox`, and put in it:
   - the [pokopia-recomp v0.1.1 release](https://github.com/drakolordx7/pokopia-recomp/releases/tag/v0.1.1), unzipped
     (any subfolder; the one with `PokopiaLauncher.exe` is found);
   - your own extracted game: the base and the **2.0.1a** update, each a folder with `exefs\` and
     `romfs\` (any names; the update is recognised by its 2.0.1a hashes). Extract the update
     **together with the base**, so its `romfs\` is complete.
2. From a checkout of this repo, in PowerShell 7:

   ```powershell
   pwsh -File tools\xbox\pokopia\pokopia.ps1 -Root C:\Users\you\Desktop\pkp-xbox
   ```

3. Do what it says. Re-run the same command whenever you stop; it continues where it was.
4. Send back the `summary.txt` files from `<release>\memory-gate\` (memory numbers and the end of
   `pkp.log`, no game data).

What it does, in order:

| Step | What happens | You do |
|---|---|---|
| 0 Checks | `doctor.ps1`: PowerShell, Windows, RAM, Vulkan 1.3, the release, your files (2.0.1a by SHA-256), disk space, that nothing is inside a git checkout | Fix any `FAIL` line; each one says how |
| 1 Game files | `prepare-dump.ps1` links your folders into `<Root>\dump` (junctions, no copies) and writes their hashes | Nothing |
| 2 Set up | Opens PokopiaLauncher | Pick `<Root>\dump` (on your clipboard), press **Set up**, not Play. ~7 min to compile, 1–2 h of shaders |
| 3 Memory gate | Run **A** with no limit, run **B** under a 5120 MiB cap | Play your heaviest areas each time, then close the game |
| 4 P1 (`-P1`) | `p1-runtime.ps1` builds the patched runtime and installs it next to `pkp.exe`; run **C** uses the private heap under the cap | Same areas again, then close the game |

Only want the checks? `pwsh -File tools\xbox\pokopia\doctor.ps1 -Root <folder> [-P1] [-ConsoleIp <xbox ip>]`.

## The scripts

| Script | Does |
|---|---|
| `pokopia.ps1` | The whole process in one command |
| `doctor.ps1` | Checks only, changes nothing; exit code 1 when something must be fixed |
| `prepare-dump.ps1` | Links your extracted files into the layout the recomp reads, writes SHA-256 hashes |
| `memory-gate.ps1` | Starts `pkp.exe` under a commit cap and records RAM and GPU memory until you close it; `-PrivateHeap` for P1 |
| `p1-runtime.ps1` | Builds `pkp_rt.dll` from the v0.1.1 source with [`patches/`](patches/README.md), keeps the original as `pkp_rt.dll.orig`; `-Revert` undoes it, `-Test` runs the runtime's memory tests first |
| `common.ps1` | Shared helpers (finding folders, the 2.0.1a hashes) |

`-P1` needs PowerShell 7 and the build tools: Git, CMake, Ninja, LLVM (`clang-cl`), Python and *Build
Tools for Visual Studio* (2022 or 2026) with "Desktop development with C++". `doctor.ps1 -P1` lists the `winget` command for each
one that is missing.

## Reading the verdict

The estimate is peak working set plus peak GPU dedicated memory, because the Series' GPU uses the
same 5 GiB.

- **FITS (≤ 4608 MiB):** go on to the porting work.
- **TIGHT (4608–5120 MiB):** possible, but only with the memory changes in the scope doc.
- **DOES NOT FIT (> 5120 MiB):** stop. A D3D12 port won't change it.

A PC job cap limits committed memory. It does not reproduce the console's unified memory, so the
result is an estimate. With the original runtime the heap is a page-file-backed section whose pages
the cap may not count; run **C** (private heap) is the one the cap measures fully. A crash only under
the cap usually means the game hit it; `pkp.log` says where. Run C's summary also says whether the
P1 gate passed (the game booted with the private heap and exited cleanly).

## Running the steps on their own

```powershell
pwsh -File tools\xbox\pokopia\prepare-dump.ps1 -Base D:\pkp\base -Update D:\pkp\update-2.0.1a -Out D:\pkp\dump
pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -NoLimit
pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -LimitMiB 5120
pwsh -File tools\xbox\pokopia\p1-runtime.ps1 -GameDir D:\pkp\recomp -Test
pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -LimitMiB 5120 -PrivateHeap
pwsh -File tools\xbox\pokopia\p1-runtime.ps1 -GameDir D:\pkp\recomp -Revert
```

If your `exefs` and `romfs` live in separate folders, use `-BaseExefs`/`-BaseRomfs` and
`-UpdateExefs`/`-UpdateRomfs` with `prepare-dump.ps1` instead of `-Base`/`-Update`.

## Status

These scripts have not been run on Windows yet; they were written and reviewed without a Windows
machine. The first run is the test: if a step fails, send its output.
