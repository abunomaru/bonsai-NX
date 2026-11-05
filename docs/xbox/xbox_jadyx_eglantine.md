# Jadyx + Eglantine: Switch 1 y Switch 2 en un solo appx de Xbox Dev Mode

Investigación del 5 de noviembre de 2026. **Documento de dirección y alcance, no de
implementación.** No hay código de Eglantine en esta rama ni gates ejecutados. Completa
[`xbox_nx2_scope.md`](xbox_nx2_scope.md) (qué añade NX2) con dos cosas nuevas: si alguien ya lo ha
portado a Xbox y cuánto han divergido los dos árboles.

## Objetivo

Un único `jadyx.appx` para Xbox Series X|S en Dev Mode (modo Game) que arranque juegos de Switch 1
con el renderer D3D12 actual de Jadyx y juegos de Switch 2 con el núcleo NX2 de Eglantine por la
misma ruta D3D12. Una biblioteca, un frontend, un portal y un sistema de memoria (guard + ledger).

## ¿Alguien ha portado Eglantine a Xbox? (búsqueda del 5 nov 2026)

**No.** No existe ningún port de Eglantine que funcione en Xbox. Lo que aparece con ese nombre:

| Proyecto | Qué es en realidad |
|---|---|
| `Arshiazxx/Eglantine-Xbox` (rama `nx2`) | Fork renombrado de Eglantine. El README es el de Eden/Eglantine; sin pasos de build para Xbox, sin releases y sin renderer D3D12. |
| `Arshiazxx/Eglantine-UWP` | Repositorio vacío («Experimental Nintendo Switch 2 Emulator for Xbox Dev Mode»); último push el 29 oct 2026. |
| `Kaoz625/eglantine-xbox` | Pese al nombre, es un port UWP de **Eden (Switch 1)**, no de Eglantine: OpenGL 4.6 sobre D3D12 con el driver `d3d12` de Mesa (`aerisarn/mesa-uwp`), basado en `juanresendiz813/eden-xbox`. Sin releases (solo CI); solo probado Persona 5 Royal, ~30 FPS con congelones en cargas. Nada de NX2. |
| Dev Mode Wiki (`XboxEmulationHub/devmodewiki`, `46f9b85`) | No menciona Eglantine, NX2 ni Switch 2. |
| GBAtemp, hilo «Eden NX2 emulator» (subforo de Xbox Series X\|S Dev Mode) | Según los extractos de búsqueda, habla de Eglantine y PokeDinner en PC; ninguno cita un build para Xbox. **No verificado**: `gbatemp.net` no era accesible desde el entorno de la investigación. |
| Reddit | No verificado (inaccesible desde el entorno). |

Lectura: el hueco existe. Jadyx es el único port de Switch a Xbox con renderer D3D12 nativo, así que
es la base natural para traer NX2.

## Cuánto han divergido Jadyx y Eglantine (medido)

Medido el 5 nov 2026 con `git merge-base` entre esta rama (`670bdbc`) y la cabeza de
`Djihads80/Eglantine` (`e736dfd`, 5 nov 2026):

- **Base común:** `d16735f5b` de Eden, 2 oct 2026 (`[am] Properly handle None frontend applet
  (#4500)`). Los dos árboles parten del mismo Eden con un mes de diferencia: un `git merge` es
  viable; no hace falta portar a mano.
- **Commits desde la base:** Jadyx 896, Eglantine 1090 (sin merges: 33 son de Eden posteriores a la
  base, y 274 citan NX2, Switch 2, Ampere, NVN2 o T239 en el asunto).
- **Ficheros tocados:** Jadyx 1258, Eglantine 1098, **ambos 264** (posibles conflictos):

  | Zona | Ficheros tocados por los dos |
  |---|---|
  | `src/core/hle` (sobre todo `service/am`, `audio`, `nvdrv`, `ns`, `vi`) | 129 |
  | `src/shader_recompiler` (frontend IR, backend SPIR-V, `ir_opt`) | 29 |
  | `src/dynarmic/src` | 11 |
  | `src/video_core/texture_cache` | 9 |
  | `src/video_core/engines` | 5 |
  | resto (`file_sys`, `arm`, `yuzu_cmd`, `buffer_cache`…) | ~81 |

- **`src/video_core/renderer_d3d12`** (138 ficheros de Jadyx) **no lo toca Eglantine**: el merge no
  rompe el renderer, pero tampoco le añade nada.
- **Eglantine toca 50 ficheros de `renderer_vulkan`.** Esa es la lista de lo que hay que
  reimplementar en D3D12 (o descartar en Xbox).

## Qué trae NX2 y cómo encaja en D3D12

Inventario de los ficheros nuevos de Eglantine (`git diff --name-status d16735f5b eglantine`):

| Pieza de Eglantine | Dónde | En Jadyx/Xbox |
|---|---|---|
| Decodificador Ampere → IR de Maxwell (`frontend/ampere/lower_to_maxwell.h`, codificación según NAK de Mesa) | `shader_recompiler` | **Encaja.** Sale por el mismo IR → SPIR-V → `spirv_to_dxil` que ya usa Jadyx. Riesgo: opcodes que el backend SPIR-V emite y `spirv_to_dxil`/SM 6.4 no aceptan. |
| Task/mesh shaders de NVN (`backend/spirv/emit_spirv_mesh.cpp`) | `shader_recompiler` | **Riesgo mayor.** En Vulkan van a mesh shaders nativos. En D3D12 requieren SM 6.5 y `MeshShaderTier`, y la Series en UWP da SM 6.4 (FL 11.0, sin Agility SDK; ver [`xbox_d3d12_phase4.md`](xbox_d3d12_phase4.md)). Hay que emularlos con compute + draw indirecto. |
| Shaders de tensor cores (HMMA; rc1 deja de saltárselos fuera de DLSS) | `shader_recompiler` | Sin equivalente directo en SM 6.4: bajar a ALU normal (lento) o descartar. Inventariar qué juegos los usan. |
| DLSS/Streamline, FSR3 frame gen, sondas de red DLSS | `renderer_vulkan/present`, `host_shaders` | No se portan: son extras de PC. Jadyx ya tiene FSR 1 propio ([`xbox_fsr.md`](xbox_fsr.md)). |
| Decodificador de vídeo NX2 (`host1x/nx2_video_decoder`, H.264) | `video_core/host1x` | Independiente del renderer; revisar si usa FFmpeg con hardware no disponible en UWP. |
| Contenedores NUP/NCZ (`file_sys/nup_*`, `loader/nup`, `crypto/ncz_decompressor`) | `core` | Independiente de la plataforma. Barato. |
| Servicios NX2 (`nx2_gsv`, `nx2_streamplay`, `nx2_audio_*`, `nx2_stub_device`…) | `core/hle` | Independiente del renderer; es donde se concentran los conflictos con los cambios HLE de Jadyx. |
| `gpu_split`, `indirect_reset`, `uniform_stream_reuse` | `video_core` | Código común; revisar qué espera del runtime para implementarlo en `renderer_d3d12`. |
| Heap de Switch 2 > 8 GiB (issue #16, «fixed, not pushed») | `core/hle/kernel` | Espacio de direcciones, no RAM: encaja con la reserva grande ya probada en la Series. |

## Plan por fases

| Fase | Trabajo | Gate |
|---|---|---|
| E0 | Gate de memoria antes de tocar código: Pokopia con Eglantine en un Job Object de 5120 MiB (P0 de [`xbox_nx2_scope.md`](xbox_nx2_scope.md)) y, en paralelo, el gate del recomp ([`tools/xbox/pokopia/`](../../tools/xbox/pokopia/README.md)) | Pico estimado ≤ 5120 MiB en al menos un juego de Switch 2 |
| E1 | Rama `nx2`: `git merge` de Eglantine sobre `xbox`; resolver los 264 ficheros comunes; NX2 detrás de una opción de build | En PC: juegos de Switch 1 iguales que antes en D3D12 (regresión cero) y Pokopia arranca en Vulkan |
| E2 | Inventario automático de shaders: volcar el SPIR-V que genera Eglantine para Pokopia, pasarlo por `spirv_to_dxil` y clasificar fallos por opcode/capacidad | Lista cerrada de huecos (mesh, tensor, imágenes, atómicos) con un conteo por juego |
| E3 | D3D12 para lo que falte: los 50 ficheros de `renderer_vulkan` que tocó Eglantine, más la emulación de mesh/task por compute y el fallback de tensor | Pokopia jugable en PC con D3D12, sin Vulkan |
| E4 | Detección automática Switch 1/2 en la biblioteca (Eglantine ya distingue por NUP/NPDM), un solo appx | Los dos tipos de juego desde la misma biblioteca en PC y en AppContainer (`local-run.ps1`) |
| E5 | Series con `memory_audit=1` y cap de 5120 MiB | Sesión larga de Pokopia sin OOM; FPS medido; Switch 1 sin regresión |

Estimación honesta: E1 es de días (el merge es grande pero mecánico); **E2–E3 son meses** y deciden
si el proyecto es viable; E0 decide si merece la pena empezar.

## Mantener el ritmo de Eglantine

Eglantine publica builds casi cada hora (build 177 el 4 nov). Para no quedarse atrás:

- merges periódicos de `Djihads80/Eglantine` en `nx2` (mismo flujo que la sincronización con Eden
  de [`AGENTS.md`](../../AGENTS.md)), registrando los conflictos repetidos en
  [`xbox_internal.md`](xbox_internal.md);
- cambios de Jadyx en ficheros comunes lo más pequeños posible, con la lógica de Xbox en ficheros
  propios;
- **nada de PRs ni issues contra Eden ni Eglantine** (reglas de [`xbox_nx2_scope.md`](xbox_nx2_scope.md)).

## Licencia y atribución

Eden, Eglantine y Jadyx son GPL-3.0-or-later: el merge es compatible. Se conservan las cabeceras
SPDX, los créditos de Eglantine/Yuzu/Sudachi/Eden y la atribución a Mesa NAK (MIT) del decodificador
Ampere. Nada de claves, firmware ni juegos en el repo.

## Fuentes

- https://github.com/Djihads80/Eglantine (cabeza `e736dfd`, 5 nov 2026) y su release
  https://github.com/Djihads80/Eglantine/releases/tag/v0.1.0-rc1
- https://github.com/Arshiazxx/Eglantine-Xbox · https://github.com/Arshiazxx/Eglantine-UWP ·
  https://github.com/Kaoz625/eglantine-xbox
- https://github.com/XboxEmulationHub/devmodewiki (`46f9b85`)
- https://gbatemp.net/threads/eden-nx2-emulator.686041/ (solo extractos de búsqueda)
- https://x.com/PixelCNinja/status/2117628216873504806 (build 177)
- Medidas de divergencia: `git merge-base`, `git diff --name-only` y `git log` sobre los dos árboles.
