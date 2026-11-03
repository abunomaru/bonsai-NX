# Referencia: Dev Mode Wiki (XboxEmulationHub)

Referencia externa para el appx de Jadyx en Xbox Series X|S Dev Mode. Resumen revisado el
3 de noviembre de 2026 contra `XboxEmulationHub/devmodewiki` (commit `46f9b85`, 2 nov 2026).
Es un resumen con enlaces, no una copia: la wiki es AGPL-3.0. Si choca con
[`xbox_deploy.md`](xbox_deploy.md), manda lo medido en este fork.

- Repositorio: https://github.com/XboxEmulationHub/devmodewiki
- Sitio: https://wiki.sternserv.xyz

## Preparar la consola

- **Cuenta:** el alta de desarrollador individual es gratuita (la de empresa cuesta 99 USD) y exige
  verificar identidad con documento y teléfono: https://storedeveloper.microsoft.com/onboarding
- **Activación:** app *Xbox Dev Mode* de la Store → código → https://aka.ms/activatexbox → reinicio.
- **Antes de activar:** salir de Xbox Update Preview (Insider) y actualizar; evita el error E208.
- **Cambiar entre Dev y Retail** borra los estados de Quick Resume y las capturas locales.
- **Almacenamiento:** en *Manage Dev Storage* reservar al menos 5 GB y reiniciar.
- **Device Portal:** *Settings → Remote Access Settings*; autenticación opcional con usuario y
  contraseña. Es la vía de instalación y del explorador de archivos.

## USB para juegos

Lo que la wiki da por método seguro (requiere un PC con Windows):

1. Formatear en **NTFS**.
2. *Propiedades → Seguridad → Avanzado → Agregar* → principal **ALL APPLICATION PACKAGES** con
   **Control total**, marcando *reemplazar los permisos de los objetos secundarios*. El error sobre
   `System Volume Information` se ignora.
3. En la Xbox, asignar la unidad como **Media**, nunca como *Games and Apps*.

XboxMediaUSB (https://github.com/SvenGDK/XboxMediaUSB) automatiza esto, pero la wiki avisa de que a
algunos usuarios no les aplica bien los permisos. Encaja con el acceso UWP de
[`xbox_rom_storage.md`](xbox_rom_storage.md).

## Instalar el appx

1. Device Portal → **Add** → el `.appx`/`.msix` y **cada** dependencia x64 (en Jadyx,
   `Microsoft.VCLibs.x64.14.00.appx`, ver [`xbox_deploy.md`](xbox_deploy.md)).
2. Dev Home → la app → **View details** → **App type: Game**. En modo App el límite es ~1 GB y
   Jadyx muere al reservar la DRAM emulada; en Game son 5 GB.
3. Canal de distribución que usa la comunidad: Emulation Revival Store
   (https://emulationrevival.github.io/xbox-dev-mode/emulators.html).

## Mesa para UWP (`spirv_to_dxil`)

La wiki documenta cómo compilar Mesa para UWP, la pieza de la que sale `spirv_to_dxil`:

- Fork: https://github.com/SternXD/mesa-uwp
- Meson con soporte `--uwp`: https://github.com/SternXD/meson-uwp/releases/tag/uwp-1.11.2
- Visual Studio 2026 con cargas UWP, Python 3 (`pyyaml packaging setuptools mako`) y
  winflexbison 2.5.25.
- Configuración: `meson setup build_release --backend=vs --uwp --buildtype=release` con
  `-Dcpp_std=vc++20`, `-D_XBOX_UWP` en `c_args`/`cpp_args`, `-Db_pch=false` y `c_winlibs`/
  `cpp_winlibs` vacíos.

Recordatorio de [`AGENTS.md`](../../AGENTS.md): los binarios de Mesa no entran en el repo.

## Otros ports de Switch en la wiki

La wiki tiene una guía de **NXbox** (https://github.com/NspxMiguel/NXbox):

- base Eden;
- Direct3D 12 por defecto y OpenGL con `renderer_backend=0` en `LocalState\eden_settings.txt`;
- claves en `LocalState\eden\keys`, juegos en `LocalState\games` o `E:\NXbox\games`;
- autoactualización desde GitHub;
- crash log en `LocalState\nxbox_crash.txt`;
- *NXbox Pack* empaqueta cada juego propio como app independiente.

El 3 nov 2026 ese repositorio devolvía 404 (privado o retirado): no se pudo verificar su código.
Ver [`xbox_port_comparison.md`](xbox_port_comparison.md).

## Fuentes

- `docs/xbox-setup/xbox-developer-mode-setup.mdx`
- `docs/development/compiling-mesa-guide.mdx`
- `docs/experimental/nxbox-guide.mdx`
- `docs/frontends/launchpass-guide.mdx`

Todos de https://github.com/XboxEmulationHub/devmodewiki.
