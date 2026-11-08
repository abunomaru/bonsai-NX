<!--
SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to Bonsai

Bonsai is a **VERY experimental** Switch 1 + Switch 2 emulator for Xbox Series X|S in Developer
Mode. It is built on [Jadyx](https://github.com/JulianDr14/jadyx) (Eden for Xbox, with its own
Direct3D 12 renderer), with Switch 2 support from
[Eglantine](https://github.com/Djihads80/Eglantine) and [Lichii](https://github.com/zortos293/Lichii).
Test reports, logs and fixes are all welcome.

**Report where the bug lives.** A Switch 1 bug that also happens in Jadyx belongs to
[Jadyx](https://github.com/JulianDr14/jadyx/issues); one that also happens in the desktop builds of
Eden belongs to [Eden](https://git.eden-emu.dev/eden-emu/eden/issues). Switch 2 problems and
anything Bonsai-only belong here.

## Ground rules

- **No piracy.** Never attach, link or ask for keys, firmware or game files, and do not report
  problems seen with pirated copies. Such issues and pull requests are closed without an answer. See
  [Legal](README.md#legal).
- Be respectful to everyone, and keep reports and reviews about the code and its behaviour.
- Bonsai is licensed under the [GPLv3 or later](LICENSE.txt), like Eden. By contributing you agree
  that your work is published under that license.

## Reporting a bug

Open an issue with the **Bug report** form (or the **Game report** form for how far a game gets)
and include:

- The Bonsai version, shown in the corner of the game library (`0.3.12.1033`, for example).
- Where it happened: Xbox Series X, Xbox Series S or PC, and on the console whether the app was set
  to **Game** mode.
- What you did, what you expected and what happened instead. Describe a game by its kind if you
  prefer not to name it.
- The diagnostics from the package's `LocalState\jadyx\log` folder: `jadyx_diag.txt` and
  `jadyx_log.txt`. On the console they are reachable through the Device Portal's file
  explorer; on PC they are in `%LOCALAPPDATA%\Packages\<package family>\LocalState`.

## Sending changes

1. Fork the repository and branch from `main`.
2. Build and test your change, as described in [Building](README.md#building). Changes to the
   console path should be tried on the console: a PC run is not proof that it works on the Series.
3. Keep commits small and focused, with messages in the style the history already uses:
   `fix(d3d12): ...`, `feat(uwp): ...`, `docs: ...`, `chore: ...`.
4. Open a pull request against `main` that says what changes, why, and how it was tested. A pull
   request merges only when the Windows CI build is green: MSVC catches errors GCC does not.
5. **Credit what you port.** A change taken from Jadyx, Eglantine, Lichii, Eden or anyone else
   keeps its author on the commit and names the project and commit in the message.

### Code

- Follow the style of the surrounding code. New files carry the same SPDX header as their
  neighbours.
- Keep changes to Eden's own files small and local. Every one of them has to be carried through
  each sync with upstream; if a fix is not specific to Xbox or UWP, consider sending it to Eden
  instead.
- Port-specific code lives in `src/jadyx/`, `src/video_core/renderer_d3d12/` and `tools/xbox/`. See
  [Architecture](README.md#architecture) for the rest.
- Do not commit keys, firmware, games, game assets or screenshots of games.
- Fixes bump only the last number of the package version (`0.2.92.N`); new features may bump the
  third.
