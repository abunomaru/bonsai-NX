# Bonsai 0.1.0 alpha

**VERY experimental** Nintendo Switch 1 + Switch 2 emulator for Xbox Series X|S in Developer Mode,
built on Jadyx (Eden for Xbox, Direct3D 12) with Switch 2 support from Eglantine and Lichii.

This is a pre-release for testers. Expect crashes, glitches and missing features.

## Highlights

- Switch 2 (NX2) games boot on Xbox through Jadyx's Direct3D 12 renderer.
- Pokémon Pokopia reaches character creation with audio, about 60 FPS in lighter scenes; not stable yet.
- Fits Pokopia in the 5 GB app limit (about 1 GB free) with Permafrost swap and a new in-RAM LZ4 tier.
- Imports `.nup` (6+ GB) and loose update + RomFS dumps; validates `.nsz` / `.xcz`.
- Six emulated cores for Switch 2 games.
- Shader fix: jump tables bounded by compare + select translate (Pokopia's environment shaders).
- Fix: null read in the GPU command reader after a few minutes of play.
- Ported Lichii fixes to shaders, buffer/texture caches, macros, queries and core.
- DRED GPU breadcrumbs in every build; CI symbolizes crash addresses.

## Install

1. Uninstall any previous Bonsai / Jadyx NX in Device Portal (`https://<xbox-ip>:11443`).
2. Install the package with its dependency packages (install the `.cer` if asked).
3. Set the app to **Game** mode.
4. Import your own keys and firmware, then add your own games.

## Known issues

- Pokopia can still crash or freeze later in play; UI text and some surfaces glitch.
- Builds are self-signed; always uninstall the old one first.

## Legal

Bonsai contains no keys, firmware, or games. Use only dumps from hardware and games you own.
GPLv3; source code is this repository.

## Credits

Jadyx (JulianDr14), Eglantine (Djihads80), Lichii (zortos293 and contributors), Eden and yuzu.
