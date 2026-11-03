# Pokopia recomp: P0 memory gate

Scripts for the first gate of [`docs/xbox/xbox_pokopia_recomp_scope.md`](../../../docs/xbox/xbox_pokopia_recomp_scope.md):
does [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp) fit in the 5120 MiB an Xbox
Dev Mode game gets? Everything runs on your own Windows PC. Nothing decrypts, uploads or copies
your game files, and nothing writes into them.

| Script | Does |
|---|---|
| `pokopia.ps1` | The whole process in one command; re-run it to continue where it stopped |
| `prepare-dump.ps1` | Links your extracted files into the layout the recomp reads, writes SHA-256 hashes |
| `memory-gate.ps1` | Starts `pkp.exe` under a commit cap and records RAM and GPU memory until you close it |

## Before you start

1. **Your own extracted game, version 2.0.1a.** The recomp reads plain folders (`exefs\` with
   `main`, `sdk`, `subsdk0`, `rtld`, `main.npdm`, and `romfs\`). A `.nup` (or `.nsp`, `.xci`,
   `.nca`) is an encrypted container: extract it first on your PC with your own console's keys,
   using the dumping tools you already use. Extract the 2.0.1a update **together with the base**, so
   its `romfs\` is complete and not a bare patch. These scripts do not do this step.
2. The latest [pokopia-recomp release](https://github.com/drakolordx7/pokopia-recomp/releases/latest),
   unzipped (the folder with `PokopiaLauncher.exe`). Its requirements: Windows 10/11 x64, a GPU
   with Vulkan 1.3, 16 GB of RAM.
3. PowerShell 7 (`pwsh`) is recommended. Windows PowerShell 5.1 should also work.
4. Keep the game files and the dump **outside any git checkout**. `prepare-dump.ps1` refuses to
   write inside one.

## Run it

```powershell
pwsh -File tools\xbox\pokopia\pokopia.ps1 -RecompDir D:\pkp\recomp `
     -Base D:\pkp\base -Update D:\pkp\update-2.0.1a -Dump D:\pkp\dump
```

1. **Game files:** links `D:\pkp\base` and `D:\pkp\update-2.0.1a` into `D:\pkp\dump` as
   `base\` and `updates\2.0.1a\`, and writes `dump-manifest.json`.
2. **Setup:** opens PokopiaLauncher. Pick `D:\pkp\dump` (it is on your clipboard) and press
   **Set up**. Do not press Play. The script waits until compilation (~7 min) and the shader cache
   (1–2 h) are finished. If you stop, run the same command again later.
3. **Measurement:** starts the game twice:
   - **A, no limit:** play your heaviest areas (big builds, busy towns, menus, saving), then close
     the game.
   - **B, 5120 MiB cap:** the same areas for about as long, then close it.

Results go to `<RecompDir>\memory-gate\A-baseline` and `B-capped`, as `summary.txt` and
`samples.csv`. Send back the two `summary.txt` files. They contain memory numbers and the end of
`pkp.log`, no game data. Look through the log lines first if you want your folder paths private.

## Reading the verdict

The estimate is peak working set plus peak GPU dedicated memory, because the Series' GPU uses the
same 5 GiB.

- **FITS (≤ 4608 MiB):** go on to the porting work.
- **TIGHT (4608–5120 MiB):** possible, but only with the memory changes in the scope doc.
- **DOES NOT FIT (> 5120 MiB):** stop. A D3D12 port won't change it.

A PC job cap limits committed memory. It does not reproduce the console's unified memory, so the
result is an estimate. A crash only in run B usually means the game hit the cap; `pkp.log` says
where.

## Running the steps on their own

```powershell
pwsh -File tools\xbox\pokopia\prepare-dump.ps1 -Base D:\pkp\base -Update D:\pkp\update-2.0.1a -Out D:\pkp\dump
pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -NoLimit
pwsh -File tools\xbox\pokopia\memory-gate.ps1 -GameDir D:\pkp\recomp -LimitMiB 5120
```

If your `exefs` and `romfs` live in separate folders, use `-BaseExefs`/`-BaseRomfs` and
`-UpdateExefs`/`-UpdateRomfs` instead of `-Base`/`-Update`.
