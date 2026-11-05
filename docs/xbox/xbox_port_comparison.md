# Comparación de ports de Switch a Xbox (y referencias)

Revisión del 3 de noviembre de 2026, actualizada el 5 de noviembre. Solo hechos publicados y lo que el código de este fork
demuestra; lo que no se pudo verificar se marca como tal.

## Tabla

| | Jadyx | eden-xbox (juanresendiz813) | NXbox | EglantinePS5 |
|---|---|---|---|---|
| Base | Eden (fork propio) | Eden, port UWP/WinRT | Eden (según prensa) | Eglantine (NX2) |
| Renderer | D3D12 nativo, escrito para el port | D3D12 (objetivo declarado) | no publicado | Vulkan (Mesa/RADV de PS5) + OpenGL |
| Plataforma | Xbox Series X\|S Dev Mode y PC | Xbox Series X\|S Dev Mode | Xbox Series X | PS5 con jailbreak |
| Licencia | GPL-3.0 | GPLv3 | no verificada | GPL-3.0 (del upstream) |
| Madurez | 0.3.9.0, tablas de compatibilidad, frontend completo | Fork antiguo; el propio AGENTS de Jadyx lo da por abandonado | Demo/prensa | Alpha temprana |
| Memoria | Guard + ledger, cap de texturas 768 MiB, 5120 MiB de app | no publicado | no publicado | sin el límite de app de Xbox |
| Extras | Pulse (reanudar), mods/cheats/updates/DLC, USB, portal | no publicado | no publicado | perfiles, frame gen, 120 Hz |

Fuentes: https://github.com/JulianDr14/jadyx · https://github.com/juanresendiz813/eden-xbox ·
https://tech-insider.org/switch-emulator-eden-xbox-series-x-2026/ ·
https://www.windowscentral.com/gaming/xbox/switch-emulation-on-xbox-series-x-is-real-and-early-results-look-far-better-than-anyone-expected ·
https://gbatemp.net/threads/eden-switch-for-xbox-series-x-s-dev-mode.685717/ ·
https://github.com/heydemoura/EglantinePS5 · https://github.com/Kaoz625/eglantine-xbox ·
https://github.com/Arshiazxx/Eglantine-Xbox · https://github.com/rodrigoandrigo/Cemu-UWP-Host

## Lectura

- **Jadyx es la referencia.** Es el único con renderer D3D12 documentado, ingeniería de memoria
  explícita y compatibilidad medida; cualquier trabajo nuevo debería partir de él.
- **eden-xbox** fue el punto de partida del port; su rama de desarrollo conserva notas, pero el
  fork de Jadyx lo supera en todo lo publicado.
- **NXbox** aparece en prensa con ~59 FPS en Super Mario 3D World. La Dev Mode Wiki lo enlaza a
  `NspxMiguel/NXbox` (Eden, D3D12 por defecto, OpenGL alternativo), pero el 3 nov 2026 ese repo
  devolvía 404: sin código verificable, no sirve como base. Ver
  [`xbox_devmode_reference.md`](xbox_devmode_reference.md).
- **eglantine-xbox (`Kaoz625`)**, pese al nombre, es un port UWP de **Eden (Switch 1)**: OpenGL 4.6
  sobre D3D12 con el driver `d3d12` de Mesa (`aerisarn/mesa-uwp`), basado en eden-xbox. Sin
  releases; solo Persona 5 Royal probado (~30 FPS, congelones en cargas). La ruta GL→D3D12 por Mesa
  es la alternativa al renderer nativo de Jadyx, no una mejora.
- **Eglantine-Xbox / Eglantine-UWP (`Arshiazxx`)**: el primero es un fork renombrado sin trabajo de
  Xbox; el segundo, un repositorio vacío. Ver [`xbox_jadyx_eglantine.md`](xbox_jadyx_eglantine.md).
- **Cemu-UWP-Host** (Wii U, no Switch) tiene una capa Vulkan→D3D12 experimental dentro del proceso
  para la Series S; interesa como referencia para backends que solo hablan Vulkan.
- **EglantinePS5** interesa como *prueba de portabilidad del núcleo*, no como código a reutilizar:
  su capa gráfica es Vulkan/OpenGL, que Xbox UWP no ofrece.

## Consecuencia para NX2

Traer Switch 2 a Xbox no es «portar Eglantine»: es **añadir NX2 al renderer D3D12 de Jadyx** y
pelear el mismo muro de 5120 MiB con juegos diseñados para 12 GB. Ver
[`xbox_nx2_scope.md`](xbox_nx2_scope.md) y el plan de un solo appx Switch 1 + Switch 2 en
[`xbox_jadyx_eglantine.md`](xbox_jadyx_eglantine.md). A 5 nov 2026 nadie ha publicado un port de
Eglantine que funcione en Xbox.
