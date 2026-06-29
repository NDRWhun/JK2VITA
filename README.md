# JK2VITA

**Star Wars Jedi Knight II: Jedi Outcast — single-player, on the PS Vita**

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

A port of Jedi Outcast's single-player to the PS Vita, built on
[OpenJK](https://github.com/JACoders/OpenJK) and rendering through
[vitaGL](https://github.com/Rinnegatamante/vitaGL), which translates the engine's OpenGL to the
Vita's GXM.

Work in progress. The game boots, the menu works, and an early level (`kejim_post`) loads and
renders on device. The full campaign, saves, and cutscenes aren't verified yet; audio, the
quit-crash fix, and the dynamic-resolution upscale are built but unconfirmed on hardware.

## Setup (for players)

You need your own legally-owned copy of Jedi Outcast — no game assets ship here.

- Install `libshacccg.suprx` (the runtime shader compiler vitaGL needs) at
  `ur0:data/libshacccg.suprx` —
  [guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
- Install `JK2VITA.vpk` (from [Releases](../../releases)) with VitaShell.
- Copy your JK2 1.04 `base/` PK3s — `assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets5.pk3` — to
  `ux0:data/JK2VITA/base/`.
- Launch from the LiveArea. Settings live in `ux0:data/JK2VITA/base/openjo_sp.cfg`.

## Controls

Left stick moves, right stick looks. Attack is on R; jump, crouch, use, and force power are the four
face buttons. The rear touch panel is a modifier plus four corner zones that reach Force powers,
inventory, and saber controls. Start opens the menu; Select shows the datapad.

Texture detail is pinned to `r_picmip 2` on Vita to fit VRAM; dynamic resolution is off by default.

## Build (for developers)

Needs [VitaSDK](https://vitasdk.org) and [vdpm](https://github.com/vitasdk/vdpm) on `PATH`, plus cmake,
ninja, and GNU make. **On Windows, run from Git Bash.** vitaGL and SDL2 come in as git submodules — forks
with the Vita patches already committed ([vitaGL](https://github.com/NDRWhun/vitaGL/tree/jk2vita),
[SDL](https://github.com/NDRWhun/SDL/tree/jk2vita)) — which the build script builds and installs over the
stock copies VitaSDK ships.

```bash
git clone --recursive https://github.com/NDRWhun/JK2VITA && cd JK2VITA
bash tools/build.sh        # vdpm deps + vitaGL + SDL + port -> build/JK2VITA.vpk
```

`bash tools/build.sh --skip-deps` rebuilds just the port once the deps are installed. Cloned without
`--recursive`? The script runs `git submodule update --init` for you.

Cloned without `--recursive`? Run `git submodule update --init` first (the setup script does this too).

## Credits

- [OpenJK](https://github.com/JACoders/OpenJK) (JACoders) — the open-source JK2/JKA engine this builds on.
- id Software — the id Tech 3 / Quake III GPL release underneath.
- Raven Software / LucasArts — the original *Jedi Knight II: Jedi Outcast*.
- [Rinnegatamante](https://github.com/Rinnegatamante) — vitaGL, and vitaQuakeIII as the reference id Tech 3 Vita port.
- [Northfear](https://github.com/Northfear/SDL) — SDL2 with the vitaGL backend.
- StaticJK — the Jedi Academy single-ELF blueprint for Vita.

## License

GPLv2 (see [LICENSE](LICENSE)), matching OpenJK. Source under `src/` keeps its original
OpenJK / id Software copyright headers.

Unofficial, non-commercial fan port — not affiliated with or endorsed by Disney, Lucasfilm,
LucasArts, Activision, or Raven. *Star Wars*, *Jedi Knight*, and *Jedi Outcast* are trademarks of
their owners; you must own a legal copy to play.
