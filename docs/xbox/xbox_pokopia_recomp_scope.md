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
| P0 | `tools/xbox/pokopia/pokopia.ps1 -Root <carpeta>` en el PC: comprobaciones, volcado, setup del launcher, corrida A sin límite y B con 5120 MiB | Estimación (pico de working set + GPU dedicada) ≤ 5120 MiB; si no, se para aquí |
| P1 | Heap en memoria privada (`[memory] heap_backing = private`); `*FromApp` en `mem/` y `kernel/svc_memory.cpp`. Parche escrito, sin compilar con clang-cl ni probar | Arranque en PC con el heap privado |
| P2 | Primero probar Dozen en el PC (ver abajo); si no, backend D3D12 de la capa NVN con SPIR-V → DXIL por `spirv_to_dxil` y caché hecha en PC | Gameplay en PC con D3D12, sin Vulkan |
| P3 | Capa de plataforma UWP (ventana, archivos, carga de DLL, mandos) | Arranque en AppContainer en PC con `local-run.ps1` |
| P4 | Series con cap 5120 y `memory_audit=1` | Sesión larga sin OOM; FPS medido |

Estimación honesta: P1 y P3 son semanas; **P2 son meses**. Sin la cifra de P0 no tiene sentido
empezar P2.

## Lo que enseñan otros ports a UWP (revisión del 4 de noviembre de 2026)

**[`jajdp/animal-crossing-xbox`](https://github.com/jajdp/animal-crossing-xbox)** (Animal Crossing de GameCube,
probado en Series X, 1.0.3) es una receta, no un fork, y su forma sirve para P3:
- **Traducir la compilación de escritorio en vez de mantener otra.** `construir_uwp.py` lee los comandos del
  `ninja` de escritorio (`ninja -t commands`), los reescribe para clang con ABI MSVC, CRT "store" y
  `/APPCONTAINER`, y los vuelve a ejecutar. Un archivo nuevo del upstream entra solo. El recomp ya compila con
  clang-cl y Ninja, así que el mismo truco aplica a `pkp_rt.dll` y al enlace de `pkp.exe`.
- **Parches idempotentes en un script** (`acgc_parches.py`), que se aplican a la copia del usuario; nada del
  juego en el repo. Es lo que ya hacen `tools/xbox/pokopia/patches` y `p1-runtime.ps1`.
- Errores de empaquetado que se repiten en cualquier port UWP:
  - el CRT de C++ va como **dependencia** del paquete, nunca dentro (`0x8027025B` al activar);
  - `Language="x-generate"` no vale en un manifiesto escrito a mano (`0x80070057` al registrar);
  - el directorio de trabajo pasa a ser la carpeta de instalación, que es de solo lectura: logs y partidas a
    `LocalState`;
  - con clang en ABI MSVC no se define `__GNUC__`: las macros de alineación que eligen rama con él se pierden
    en silencio. El runtime del recomp ya compila con clang-cl, así que no debería afectarle, pero hay que
    revisarlo en el código generado.
  - la suspensión de Dev Mode reinicia el juego al volver de Home.
- **[`worleydl/uwp-dep`](https://github.com/worleydl/uwp-dep)** es el depósito de binarios UWP que usa: SDL2,
  Mesa (`libgallium_wgl`, OpenGL sobre D3D12), `libuwp`, `dxil.dll` y GLFW. **No trae SDL3 ni Vulkan.** El
  recomp usa SDL3 3.4.18, y SDL3 quitó el soporte WinRT/UWP: en P3 hay que cambiar la capa de plataforma
  (`src/gfx/platform.cpp`, `input.cpp`) por CoreWindow propia o por el SDL2 UWP de ese depósito.

**[`storytold/craft-anything`](https://github.com/storytold/craft-anything)** no es un port, pero su forma
de trabajar sirve: un comando que detecta qué hay, comprueba antes de actuar y dice qué falta. De ahí salen
`doctor.ps1` y el `pokopia.ps1 -Root <carpeta>` que encuentra solo la release, la base y la 2.0.1a (por los
SHA-256 que publica el recomp).

### Una vía más corta para P2, por probar: Dozen

Dozen (`dzn`, Vulkan sobre D3D12 de Mesa) en `main` (`src/microsoft/vulkan/dzn_device.c`, 4 nov 2026):
- anuncia **Vulkan 1.2** (`DZN_API_VERSION`), pero expone `dynamicRendering`, `synchronization2` y
  `timelineSemaphore`;
- acepta D3D12 desde **FL 11.0**;
- `VK_EXT_external_memory_host` solo con `ID3D12Device13`; sin él, los pools del recomp no se importan;
- no expone transform feedback (en el recomp es opcional).

El recomp exige `apiVersion >= 1.3` (`vk_device.cpp:351` y `:462`). Un parche que acepte 1.2 con esas
extensiones podría bastar para probar `pkp.exe` sobre Dozen **en el PC** (`VK_ICD_FILENAMES`), sin escribir
el backend D3D12. En UWP faltan además el loader de Vulkan y la superficie sobre CoreWindow: Microsoft dijo en
2022 que ninguna de las dos APIs de Khronos tiene cómo iniciar la ventana sobre CoreWindow, y propuso
renderizar fuera de pantalla y copiar al swapchain D3D12 ([mesa-dev, junio 2022](https://lists.freedesktop.org/archives/mesa-dev/2022-June/225808.html)).
Si en el PC funciona, P2 pasa de meses a semanas.

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
