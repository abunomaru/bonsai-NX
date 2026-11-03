# Alcance: traer NX2 (Eglantine) al renderer D3D12 de Jadyx

Investigación del 3 de noviembre de 2026. **Documento de alcance, no de implementación.** No hay
código NX2 en esta rama ni gates ejecutados.

## Qué es Eglantine

Emulador de **Nintendo Switch 2** libre y de código abierto, derivado de Eden, Yuzu y Sudachi,
iniciado por Camille LaVey; se apoya en el trabajo experimental **NX2** de Takoroka para Eden.
Publica builds para Windows, Linux y macOS y ya mueve Mario Kart World a 60 FPS en PC. Un fork
(PokeDinner) hace correr Pokopia a 60 FPS. Fuentes:

- https://www.xda-developers.com/first-switch-2-emulator-running-mario-kart-world-surprisingly-well-nintendo/
- https://wccftech.com/mario-kart-world-full-speed-pc-switch-2-emulator-eglantine-jailbreak/
- https://gbatemp.net/threads/pokopia-running-perfectly-in-pokedinner-fork-of-eglantine.686110/
- https://github.com/zortos293/Eglantine

Existe un port no oficial a PS5 (alpha): `https://github.com/heydemoura/EglantinePS5`. Renderiza
con **Vulkan sobre Mesa/RADV de PS5** (Mihawk) y, como alternativa, `ps5-opengl`; se apoya en el
PS5 Native App Boilerplate y requiere jailbreak. Es la prueba de que el núcleo es portable a una
consola fija, pero **no** de que lo sea a UWP: PS5 homebrew tiene Vulkan y OpenGL reales; Xbox UWP
no tiene ninguno de los dos.

## Qué tendría que añadir Jadyx

Jadyx es un port UWP de Eden con backend D3D12 propio. NX2 no es «Eden con otro nombre»: añade
emulación de la GPU Ampere (T239/GA10B) y de NVN2, servicios de sistema nuevos y un presupuesto de
memoria de consola de 12 GB. Lo que toca al backend:

| Área | Estado en Jadyx (Switch 1) | Necesario para NX2 |
|---|---|---|
| Shaders | SPIR-V → `spirv_to_dxil` → DXIL firmado; SM 6.4 | Cobertura de las features de Ampere que use NVN2; inventario antes de prometer nada |
| Estado de GPU | Maxwell → D3D12 en `d3d12_maxwell_to_d3d12.cpp` | Traducción del estado de la GPU nueva |
| Barreras | FL 11.0, sin enhanced barriers ni Agility SDK | Revisar si NX2 necesita más de lo que da FL 11.0 |
| Formatos | ASTC→BC1/BC3, BC nativos, RGBA8 | Formatos nuevos de Switch 2 (inventariar) |
| Memoria | 5120 MiB de app, sin fastmem, guard + ledger | Presupuesto de texturas y JIT para juegos de 12 GB |
| CPU | Dynarmic (A64, W^X en AppContainer) | A78C es ARMv8.2/8.4: el mismo JIT, con posible ajuste de ISA |
| Servicios | HLE de Switch 1, Pulse | Firmware/servicios NX2 |

## La cuenta de memoria (el muro sigue ahí)

En Game Mode la app entera dispone de **5120 MiB**, GPU incluida. Con juegos de Switch 1 el caso
medido más pesado ya necesita ~4,25 GiB sin contar texturas y revienta al cargar (~6,4 GiB de pico
en PC sin límite). Un juego de Switch 2 se diseña para una consola de **12 GB**: su copia emulada,
el JIT, los shaders y una segunda copia de cada textura no caben en 5 GiB salvo que el juego sea
ligero. Pokopia (tipo Animal Crossing, 60 FPS en móviles y Steam Deck) es el mejor candidato
posible; Mario Kart World o un juego de mundo abierto, no.

## Por qué no se copia el port de PS5

- PS5 jailbreak expone Vulkan (Mesa/RADV) y OpenGL; el port usa esos backends tal cual.
- Xbox UWP no expone Vulkan ni OpenGL: de ahí el renderer D3D12 nativo de Jadyx. NX2 tendría que
  pasar por ese renderer, no por RADV.
- El sandbox de Xbox impide fastmem y limita la memoria; PS5 no tiene ese límite de la misma forma.

## Plan por fases (si se autoriza)

| Fase | Trabajo | Gate |
|---|---|---|
| P0 | Compilar Eglantine en PC y reproducir un juego NX2 propio | Referencia funcional y versión fijada |
| P1 | Fusionar el núcleo NX2 en la rama `xbox` detrás de un flag de build | Arranque en PC con renderer nulo/CPU |
| P2 | Inventario de features de shader/estado y huecos frente a FL 11.0 | Lista cerrada, sin sorpresas en Series |
| P3 | Traducción D3D12 de lo que falte | Un juego NX2 ligero en PC con D3D12 |
| P4 | Servicios y firmware NX2 (HLE) | Boot + guardado en PC |
| P5 | Series con `memory_audit=1`, cap 5120 | Carga pesada sin OOM; A/B de FPS |

Estimación honesta: **P2–P3 es trabajo de renderer a escala de meses**, no un fin de semana; P1 es
la parte barata. Sin consola en mano no se puede cerrar ningún gate de rendimiento.

## Reglas que no cambian

- GPLv3 y atribución a Eden/Eglantine/Yuzu/Sudachi; nada de claves, firmware ni juegos en el repo.
- Nada se envía a Eden (prohíbe IA en su proyecto); este fork es independiente.
- Nada de PRs ni issues contra Eden/Eglantine.

## Fuentes

- README de Jadyx (muro de memoria, 5120 MiB, sin fastmem).
- `docs/xbox/xbox_texture_memory.md`, `docs/xbox/xbox_performance.md`.
- EglantinePS5 `README.md` (Vulkan/RADV, OpenGL, alpha).
- Notas de prensa citadas arriba.
