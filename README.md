**Star Wars Jedi Knight II: Jedi Outcast — single-player, on the PS Vita**

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

A port of Jedi Outcast's single-player to the PS Vita, built on
[OpenJK](https://github.com/JACoders/OpenJK) and rendering through
[vitaGL](https://github.com/Rinnegatamante/vitaGL), which translates the engine's OpenGL to the
Vita's GXM.

Please report bugs if you find any -> Issues

## Setup (for players)

You need your own legally-owned copy of Jedi Outcast (eg.: from Steam)

- Install `libshacccg.suprx` (the runtime shader compiler vitaGL needs) at
  `ur0:data/libshacccg.suprx` —
  [guide](https://samilops2.gitbook.io/vita-troubleshooting-guide/shader-compiler/extract-libshacccg.suprx).
- Install `JK2VITA.vpk` (from [Releases](../../releases)) with VitaShell.
- Copy your JK2 1.04 `base/` PK3s — `assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets5.pk3` — to
  `ux0:data/JK2VITA/base/`.
- Launch from the LiveArea. Settings live in `ux0:data/JK2VITA/base/openjo_sp.cfg`.

## Controls

## Sticks

| Input | Action |
|-------|--------|
| Left stick | Move (forward/back/strafe) — also the menu cursor |
| Right stick | Look / turn |
| Front touchscreen | In menus acting like a pointer moving the cursor |

In menus, Cross selects/clicks and Circle goes back/cancels. That face-button remap is in `cl_keys.cpp`. Navigate menus with the left stick plus those two buttons.

## Base layer (physical buttons)

| Button | Action |
|--------|--------|
| R | Attack / saber swing (`+attack`) |
| L | Alt-attack / saber special (`+altattack`) |
| ✕ Cross | Jump (`+moveup`) |
| ◻ Square | Crouch (`+movedown`) |
| ○ Circle | Use / activate (`+use`) |
| △ Triangle | Use selected force power (`+useforce`) |
| D-pad Up / Down | Next / previous weapon |
| D-pad Left / Right | Select previous / next force power |
| Select | Mission objectives — datapad (`datapad`) |
| Start | In-game menu (`togglemenu`) |

## Rear touch panel

The rear panel is split into four corner zones. A cross-shaped dead band down the middle ignores the fingers that grip the console. Set `vita_rearTouch 0` to disable all of it.

| Rear zone | Action |
|-----------|--------|
| Top-left (HOLD) | Combo modifier — see below |
| Top-right | Binocular zoom (`zoom`) |
| Bottom-left | Secondary force fire (`+useforce`) |
| Bottom-right | Run / walk (`+speed`) |

## Combo layer — hold rear top-left, then press:

| Combo | Action |
|-------|--------|
| + △ Triangle | Force Speed |
| + ○ Circle | Force Heal |
| + ✕ Cross | Force Push (`force_throw`) |
| + ◻ Square | Force Pull |
| + R | Cycle saber stance (`saberAttackCycle`) |
| + D-pad Up / Down | Inventory next / previous |
| + D-pad Left | Use inventory item (`invuse`) |
| + D-pad Right | Quick-select lightsaber (`weapon 1`) |

The combo layer only fires instant commands. The modifier role is latched per button at the moment it is pressed, so releasing the rear modifier mid-press can't strand a held action. The combo layer is inactive while a menu is open.


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
- Raven Software / LucasArts — the original *Jedi Knight II: Jedi Outcast*.
- [Rinnegatamante](https://github.com/Rinnegatamante) — vitaGL, and vitaQuakeIII as the reference id Tech 3 Vita port.
- [Northfear](https://github.com/Northfear/SDL) — SDL2 with the vitaGL backend.

## License

GPLv2 (see [LICENSE](LICENSE)), matching OpenJK. Source under `src/` keeps its original
OpenJK / id Software copyright headers.

Unofficial, non-commercial fan port — not affiliated with or endorsed by Disney, Lucasfilm,
LucasArts, Activision, or Raven. *Star Wars*, *Jedi Knight*, and *Jedi Outcast* are trademarks of
their owners; you must own a legal copy to play.
