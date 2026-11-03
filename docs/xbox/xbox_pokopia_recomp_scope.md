# Alcance: port nativo de Pokopia (pokopia-recomp) a UWP

Revisión del 3 de noviembre de 2026 contra `drakolordx7/pokopia-recomp` (commit `2c29515`,
2 nov 2026). **Documento de alcance, no de implementación.** No hay código del recomp en este repo;
las herramientas del gate P0 están en [`tools/xbox/pokopia/`](../../tools/xbox/pokopia/README.md).

## Qué es pokopia-recomp

- Recompilación estática de Pokémon Pokopia (Switch 2, **versión 2.0.1a**): el código ARM64 del
  juego se traduce a C++ con nxrecomp y se compila para x64 Windows con clang-cl. GPL-3.0.
- Requisitos publicados: Windows 10/11 x64, GPU con **Vulkan 1.3**, **16 GB de RAM**.
- El launcher (Tauri) valida un volcado descifrado (`base/{exefs,romfs}` y
  `updates/<versión>/{exefs,romfs}`), compila el juego (~7 min) y construye la caché de shaders
  (1–2 h). No incluye archivos del juego.
- Tamaño del runtime: ~95,6k líneas; gráficos ~44,7k, de las que ~18,5k están en ficheros que
  llaman a Vulkan.

Fuentes: https://github.com/drakolordx7/pokopia-recomp ·
https://x.com/PixelCNinja/status/2117351716307796367

## Por qué encaja mejor que un emulador

- **Sin JIT.** El juego es código compilado dentro del ejecutable: no hay generación de código en
  tiempo de ejecución ni el problema W^X del AppContainer.
- **Sin emulación de CPU.** Desaparece el muro de CPU de NX2 (MKW 15–23 FPS en la Deck, limitado
  por el JIT; ver [`xbox_nx2_scope.md`](xbox_nx2_scope.md)).
- **Shaders precompilados a SPIR-V** (Maxwell/Ampere → SPIR-V con el traductor de Ryujinx, validado
  con `spirv-val`). Jadyx ya convierte SPIR-V en DXIL con `spirv_to_dxil`. La caché puede hacerse
  en PC y copiarse a la consola.
- **Memoria identidad en direcciones fijas** (`runtime/include/nxrt/memory.h`): main en
  `0x7100000000`, SDK en `0x7500000000`, heap en `0x7800000000` con 12 GiB de reserva. La sonda de
  la 0.2.51.0 ya probó en la Series reservar 512 GiB, el aliasing de vistas y
  `VirtualProtectFromApp` con fallo capturado ([`xbox_d3d12_phase4.md`](xbox_d3d12_phase4.md)).

## Qué hay que cambiar

| Área | Hoy en el recomp | Para UWP en la Series |
|---|---|---|
| Gráficos | Solo Vulkan 1.3: `dynamicRendering`, `synchronization2`, `timelineSemaphore`, `descriptorIndexing`, `VK_EXT_transform_feedback` | Backend D3D12 FL 11.0 sin Agility SDK para la capa NVN. **Trabajo de meses.** Dozen (Vulkan sobre D3D12 de Mesa) no está verificado en FL 11.0 para Vulkan 1.3 |
| Heap | Sección de 12 GiB respaldada por el pagefile (`runtime/src/mem/memory.cpp:448`, `CreateFileMappingW(INVALID_HANDLE_VALUE, SEC_RESERVE)`) | La Series limita a ~1 GiB todas las vistas de secciones mapeadas, sean del pagefile o de un archivo (sondas 0.2.56.0 y 0.2.56.2). Pasar a memoria privada: el juego no usa `heap_alias` ([parche P1](../../tools/xbox/pokopia/patches/README.md)) |
| Memoria | `VirtualAlloc`, `VirtualProtect`, `MapViewOfFileEx` a direcciones fijas; write-watch con `VirtualProtect` + VEH | Variantes `*FromApp` (ya en `src/common/host_memory.cpp`); VEH ya en uso |
| Plataforma | `CreateWindowExW`, `CreateFileW`, `LoadLibrary*`, `CreateProcessW` | CoreWindow, acceso de almacenamiento UWP, `LoadPackagedLibrary`; la compilación del juego se queda en el PC y solo va al appx el resultado |
| Entrada/audio | XInput, XAudio2 | XAudio2 sirve tal cual (Jadyx ya lo usa); mejor Windows.Gaming.Input para mandos |

## El muro de memoria

- El recomp pide 16 GB y el juego reserva un heap de ~9,5 GiB (issue #16 de Eglantine). Es espacio
  de direcciones; nadie ha publicado el consumo real.
- Un juego Dev Mode tiene **5120 MiB** con la GPU incluida y sin pagefile.
- El recomp no tiene JIT, caché de bloques ni CPU emulada, así que su sobrecoste es menor que el de
  un emulador: es el mejor candidato posible, pero la cifra decide.

## Plan por fases

| Fase | Trabajo | Gate |
|---|---|---|
| P0 | `tools/xbox/pokopia/pokopia.ps1` en el PC: volcado, setup del launcher, corrida A sin límite y B con 5120 MiB | Estimación (pico de working set + GPU dedicada) ≤ 5120 MiB; si no, se para aquí |
| P1 | Heap en memoria privada (`[memory] heap_backing = private`); `*FromApp` en `mem/` y `kernel/svc_memory.cpp`. Parche escrito, sin compilar con clang-cl ni probar | Arranque en PC con el heap privado |
| P2 | Backend D3D12 de la capa NVN; SPIR-V → DXIL con `spirv_to_dxil` y caché hecha en PC | Gameplay en PC con D3D12, sin Vulkan |
| P3 | Capa de plataforma UWP (ventana, archivos, carga de DLL, mandos) | Arranque en AppContainer en PC con `local-run.ps1` |
| P4 | Series con cap 5120 y `memory_audit=1` | Sesión larga sin OOM; FPS medido |

Estimación honesta: P1 y P3 son semanas; **P2 son meses**. Sin la cifra de P0 no tiene sentido
empezar P2.

## Reglas

- **Nada del juego en el repo ni en la nube:** ni el volcado, ni `exefs`, ni el C++ generado, ni el
  `pkp.exe` o el appx resultante. Todo se queda en el PC del usuario y en su consola.
- Este repo no descifra contenedores (`.nup`, `.nsp`, `.xci`, `.nca`): el usuario extrae su copia
  con sus herramientas y sus claves; `prepare-dump.ps1` solo enlaza carpetas ya extraídas.
- El appx generado contiene código derivado del juego: es para la consola del usuario y no se
  publica.
- GPL-3.0 y atribución a nxrecomp, Ryujinx, yuzu, Eden y el resto de créditos del recomp.

## Fuentes

- `pokopia-recomp` `README.md`, `launcher/src-tauri/src/{dump,gamebuild,play,shaders}.rs`,
  `runtime/src/mem/memory.cpp`, `runtime/include/nxrt/memory.h`, `tools/shaders/README.md`.
- Jadyx: [`xbox_d3d12_phase4.md`](xbox_d3d12_phase4.md) (sondas de fastmem y commit),
  `src/common/host_memory.cpp`, [`xbox_nx2_scope.md`](xbox_nx2_scope.md).
- https://github.com/Djihads80/Eglantine/issues/16 (heap de ~9,5 GiB).
