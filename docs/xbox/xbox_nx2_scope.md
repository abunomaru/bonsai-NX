# Alcance: traer NX2 (Eglantine) al renderer D3D12 de Jadyx

Investigación del 3 de noviembre de 2026, actualizada el 5 de noviembre. **Documento de alcance,
no de implementación.** No hay código NX2 en esta rama ni gates ejecutados. La dirección «un solo
appx con Switch 1 y Switch 2», la búsqueda de ports de Eglantine a Xbox y la divergencia medida
entre los dos árboles están en [`xbox_jadyx_eglantine.md`](xbox_jadyx_eglantine.md).

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
| Shaders | SPIR-V → `spirv_to_dxil` → DXIL firmado; SM 6.4 | Eglantine baja Ampere a IR de Maxwell y de ahí a SPIR-V: entra por la misma ruta. Falta inventariar opcodes nuevos (HMMA, SURED, 0x927) frente a SM 6.4 |
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

## Hallazgos de la segunda revisión (3 nov 2026)

Datos publicados, no medidos en este fork.

- **Pokopia pide un heap de ~9,5 GiB** al arrancar (`SetHeapSize(0x260200000)`). Eglantine lo
  rechazaba contra el tope de 8 GiB de `MainMemorySizeMax`; el arreglo (tope de 16 GiB solo para
  Switch 2) está cerrado el 30 oct como «fixed, not pushed». Es espacio de direcciones, no memoria
  residente: nadie ha publicado cuánta RAM usa Pokopia de verdad.
  Fuente: https://github.com/Djihads80/Eglantine/issues/16
- **Steam Deck (16 GB UMA), fork MFS-code.** Vulkan/RADV; Ampere → IR Maxwell → SPIR-V.
  Medidas con sus «memory fences»: DK Bananza ~6,2 GB RSS sin swap (upstream 10,9 GB + 2,7 GB
  swap); Mario Kart World 3,5–5,4 GB RSS + 2,7–2,9 GB swap, 4,6–5,6 GB de GTT y 15–23 FPS
  limitado por CPU (61 % de muestras en código JIT). Pokopia no probado. Las fences son
  trasladables al guard/ledger de Jadyx:
  - presupuesto del recolector de texturas al 35 % de la RAM física;
  - imágenes ≥8 MiB sin backing si superan el tope (salvo RT/depth);
  - staging limitado a 768 MiB;
  - no escribir imágenes solo-GPU de vuelta a memoria guest al desalojarlas;
  - ASTC grande y texturas ≥64 MiB pierden los mips superiores;
  - reservas sparse (17 GB en MKW) sin commit hasta que la GPU escribe;
  - heap de Switch 2 de 16 GiB con páginas a cero liberadas en vez de comprometidas.
  Fuente: https://github.com/MFS-code/Eglantine-steamdeck/blob/HEAD/docs/SteamDeck.md
- **EglantinePS5** pasó el heap (~3 GiB) y la page table (1 GiB) de reserva fija a asignación bajo
  demanda y liberó ~2,6 GiB de memoria gráfica en su juego más grande. Mismo principio.
  Fuente: https://github.com/heydemoura/EglantinePS5
- **Eglantine-Xbox** (`Arshiazxx/Eglantine-Xbox`, rama `nx2`) se presenta como port a Xbox Dev
  Mode, pero no tiene pasos de build para Xbox, releases ni trabajo de renderer visible: es un fork
  renombrado, no un port funcional. Fuente: https://github.com/Arshiazxx/Eglantine-Xbox
- **Android:** no se pudo verificar ningún build de Eglantine para Android ni datos de Pokopia en
  móviles; el README solo lista Windows, Linux y macOS. Las cifras de 60 FPS de Pokopia vienen del
  fork PokeDinner y de prensa de PS5; un usuario midió ~10 FPS en M1 y ~20 en M3 Pro.
- **Límite de Xbox confirmado por Microsoft:** juegos UWP 5 GB en primer plano, sin paginación;
  4 núcleos exclusivos + 2 compartidos; D3D12 FL 11.0.
  Fuente: https://learn.microsoft.com/en-us/windows/uwp/xbox-apps/system-resource-allocation

## Actualización del 5 nov 2026

- **Eglantine v0.1.0-rc1** (4–5 nov). Corrige títulos de Switch 2 con ExeFS base vacío que se
  detectaban como Switch 1 (excepción de memoria y arranque a 0 FPS), deja de saltarse los shaders
  de tensor cores fuera de juegos con DLSS y acepta más instrucciones compute: casi ningún shader de
  Pokopia se rechaza ya, y el cielo negro en la Deck está corregido. El build 177 (4 nov) arranca MKW
  1.8.0 con LAN y lleva Pokopia y Splatoon Raiders a gameplay.
- **Issue #16 sigue «fixed, not pushed»**: las notas de la rc1 no dicen si el tope de heap mayor
  para Switch 2 está incluido.
- **Ports a Xbox:** ninguno funcional. `Arshiazxx/Eglantine-Xbox` sigue sin releases ni build de
  Xbox, `Arshiazxx/Eglantine-UWP` está vacío y `Kaoz625/eglantine-xbox` es Eden (Switch 1) sobre
  Mesa GL→D3D12. Detalle en [`xbox_jadyx_eglantine.md`](xbox_jadyx_eglantine.md).
- **Divergencia medida:** base común de Eden del 2 oct 2026; 264 ficheros tocados por los dos lados.
  El merge es viable (ver el documento citado).
- Fork nuevo sin relevancia para Xbox: `sylvieistaken/selevet-eglantine` (Zen 3, PGO, ThinLTO).

**Lectura:** con fences, los dos juegos medidos en la Deck siguen por encima de 5 GiB. Pokopia es
el único candidato razonable y su consumo real es la incógnita que decide el proyecto. Segundo
muro: la CPU. MKW ya va limitado por CPU en la Deck con fastmem; en Xbox no hay fastmem.

## Por qué no se copia el port de PS5

- PS5 jailbreak expone Vulkan (Mesa/RADV) y OpenGL; el port usa esos backends tal cual.
- Xbox UWP no expone Vulkan ni OpenGL: de ahí el renderer D3D12 nativo de Jadyx. NX2 tendría que
  pasar por ese renderer, no por RADV.
- El sandbox de Xbox impide fastmem y limita la memoria; PS5 no tiene ese límite de la misma forma.

## Plan por fases (si se autoriza)

| Fase | Trabajo | Gate |
|---|---|---|
| P0 | Compilar Eglantine en PC y correr Pokopia dentro de un Job Object de 5120 MiB (como las pruebas de Jadyx); medir pico de RAM y memoria GPU | Cabe en 5120 MiB, o el proyecto se para aquí |
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
- EglantinePS5 `README.md` (Vulkan/RADV, OpenGL, alpha, heap bajo demanda).
- Eglantine issue #16 (heap de Pokopia), `docs/SteamDeck.md` del fork MFS-code.
- Microsoft Learn: recursos de UWP en Xbox.
- Notas de prensa citadas arriba.
- https://github.com/Djihads80/Eglantine/releases/tag/v0.1.0-rc1 ·
  https://x.com/PixelCNinja/status/2117628216873504806 ·
  https://github.com/sylvieistaken/selevet-eglantine
