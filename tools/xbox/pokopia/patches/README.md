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

```powershell
git clone https://github.com/drakolordx7/pokopia-recomp; cd pokopia-recomp; git checkout 2c29515
git apply <jadyx>\tools\xbox\pokopia\patches\0001-mem-private-heap-and-fromapp.patch
pwsh runtime\scripts\build.ps1 -Test -Filter memory
```

## Status

- Checked: `memory.cpp`, `write_watch.cpp` and `svc_memory.cpp` compile (MinGW, `-Wall -Wextra`, with and without
  `NXRT_UWP`).
- **Not yet checked:** the clang-cl build, the `memory` test group, `nvn_gpu_pool.cpp` (one-line change), and a
  game boot with `NXRT_MEMORY_HEAP_BACKING=private`. P1 gate: the game boots on the PC with the private heap.
