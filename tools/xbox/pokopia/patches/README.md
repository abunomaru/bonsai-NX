# Pokopia recomp: P1 patches

Patches for [pokopia-recomp](https://github.com/drakolordx7/pokopia-recomp) at commit `2c29515`, for phase P1 of
[`docs/xbox/xbox_pokopia_recomp_scope.md`](../../../../docs/xbox/xbox_pokopia_recomp_scope.md). They change only
the runtime (`pkp_rt.dll`) and contain no game code.

| Patch | Does |
|---|---|
| `0001-mem-private-heap-and-fromapp.patch` | `[memory] heap_backing = private` (env `NXRT_MEMORY_HEAP_BACKING=private`): the 12 GiB heap is a private reservation at `0x7800000000`, not a page-file-backed section view; `nxrt/host_vm.h` routes `VirtualAlloc`/`VirtualProtect` through the `*FromApp` variants when built with `NXRT_UWP` |

Why private, not a file-backed section: Jadyx 0.2.56.2 measured on the Series that **all mapped section views of
a UWP game add up to ~1 GiB**, whether the section is backed by the page file or by a file. The game paths of
the runtime never call `heap_alias`; only the NVN pool import used a section view (`heap_private_view`). With a
private heap the import uses the pool's own address and makes no device-local copies (a PC-only speed-up; the
D3D12 backend of P2 replaces this code anyway). Shrinking the heap now really frees the pages.

The private heap also makes P0 run B more accurate: the job cap of `memory-gate.ps1` counts private commit,
which may not include a section's pages.

## Apply and build (on your PC)

Needs VS 2026 Build Tools (C++ workload) at the default path, LLVM (`clang-cl`), CMake, Ninja and sccache on `PATH`,
and the VS Installer folder on `PATH` (`vcvars64.bat` calls `vswhere.exe`).

```powershell
git clone https://github.com/drakolordx7/pokopia-recomp; cd pokopia-recomp; git checkout 2c29515
git apply <jadyx>\tools\xbox\pokopia\patches\0001-mem-private-heap-and-fromapp.patch
pwsh tools\textures\scripts\fetch_third_party.ps1   # astc-encoder, bc7enc_rdo, ispc (the runtime links astc2bc7.lib)
pwsh tools\textures\scripts\build.ps1
pwsh runtime\scripts\build.ps1 -Test -Filter memory
```

Run the scripts with `pwsh`. Under Windows PowerShell 5.1 with redirected output, `git` and `cmake` messages on stderr
abort them (`$ErrorActionPreference = 'Stop'`).

The standalone runtime build at `2c29515` does not finish on its own, with or without this patch (same six failures):

- `NXRT_GAMES`, `NXRT_LAUNCHER_GAME` default to `mpr`, which the repository does not ship. Set both to `pkp` (as
  `game-build/CMakeLists.txt` does) in `runtime\build\release\CMakeCache.txt` after the first configure.
- `nxrt_tests`, `mpr_launcher` and `mpr_launcher_tests` do not link: `nxrt.lib` calls `nxrt::gfx`/`nxrt::extras`.
  For the tests, link `nxrt_gfx` into `nxrt_tests` (`runtime/CMakeLists.txt`, `target_link_libraries`).
- `tests/test_kernel_threads.cpp:855` uses `kTickHz` (now `kTickHzSwitch1`); `tests/gfx/test_nvn_cmd_state.cpp`,
  `test_nvn_texture.cpp` and `test_prompts.cpp` (missing `pak.h`) do not compile, so `nxrt_gfx_tests` is not built.

## Status

- Checked: `memory.cpp`, `write_watch.cpp` and `svc_memory.cpp` compile (MinGW, `-Wall -Wextra`, with and without
  `NXRT_UWP`).
- Checked: the clang-cl build (LLVM 23.1.4, without `NXRT_UWP`) of all patched files, `nvn_gpu_pool.cpp` included.
  `memory` test group: 27 of 27 pass, `private_heap_commit_decommit_and_no_views` included. `svc_memory`: 19 of 21,
  the same two failures as without the patch (`test_svc_memory.cpp:418`, `:434` expect `0xD401` where the runtime
  accepts a range that is already mapped, or free).
- **Not yet checked:** the clang-cl build with `NXRT_UWP`, `gfx_nvn_memory` (needs `nxrt_gfx_tests`), and a game boot
  with `NXRT_MEMORY_HEAP_BACKING=private`. P1 gate: the game boots on the PC with the private heap.
