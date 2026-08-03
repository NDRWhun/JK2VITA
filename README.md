# JK2VITA

**Star Wars Jedi Knight II: Jedi Outcast — single-player, on the PS Vita**

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)

A port of Jedi Outcast's single-player to the PS Vita, built on
[OpenJK](https://github.com/JACoders/OpenJK) with a native sceGxm rendering backend.

Please report bugs if you find any -> Issues

## Setup (for players)

You need your own legally-owned copy of Jedi Outcast (eg.: from Steam)

- Install `JK2VITA.vpk` (from [Releases](../../releases)) with VitaShell.
- Copy your JK2 1.04 `base/` PK3s — `assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets5.pk3` — to
  `ux0:data/JK2VITA/base/`.
- Launch from the LiveArea. Settings live in `ux0:data/JK2VITA/base/openjo_sp.cfg`.

## Controls

### Sticks

| Input | Action |
|-------|--------|
| Left stick | Move (forward/back/strafe) — also the menu cursor |
| Right stick | Look / turn |
| Front touchscreen | In menus acting like a pointer moving the cursor |

In menus, Cross selects/clicks and Circle goes back/cancels. Navigate menus with the left stick plus those two buttons.

### Base layer (physical buttons)

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

### Rear touch panel

The rear panel is split into four corner zones. A cross-shaped dead band down the middle ignores the fingers that grip the console. Set `vita_rearTouch 0` to disable all of it.

| Rear zone | Action |
|-----------|--------|
| Top-left (HOLD) | Combo modifier — see below |
| Top-right | Binocular zoom (`zoom`) |
| Bottom-left | Secondary force fire (`+useforce`) |
| Bottom-right | Run / walk (`+speed`) |

### Combo layer — hold rear top-left, then press:

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

Defaults are applied on first launch only, so rebinds persist. Set `vita_defaultBinds 1` and relaunch to restore them.

### Console

Open with **Start + Select** — the on-screen keyboard pops up. Type a command, press **Enter** to run it. Close with **Circle** or **Start + Select** again.

### Aim assist (optional)

Off by default. When an enemy is near your crosshair it gently steers your view toward them and slows your look speed so you overshoot less — set `g_aimAssist 1` to enable.

| Cvar | Default | What it does |
|------|---------|--------------|
| `g_aimAssist` | `0` | `1` = on (look assist) |
| `g_aimAssistPull` | `3` | Pull strength toward the target — higher = snappier |
| `g_aimAssistSpeed` | `0.5` | Look-speed scale near a target — lower = more slowdown |

## Performance & tuning

Tune by editing `ux0:data/JK2VITA/base/openjo_sp.cfg` on the card, or from the in-game console (**Start + Select**). *(latched)* renderer cvars need a `vid_restart`; the latched sound cvars need a relaunch.

Presentation is vsync-locked to the Vita's 60 Hz panel. `com_maxfps` still applies, but its
default of `125` sits above that ceiling.

| Cvar | Default | What it does |
|------|---------|--------------|
| `r_renderThread` | `1` | Dedicated backend render thread; `0` = single-threaded *(latched)* |
| `s_asyncLoad` | `1` | Read sound files on a worker thread; `0` = synchronous |
| `s_mixThread` | `1` | Mix sound + decode music on a worker thread; `0` = on the main thread *(latched)* |
| `r_picmip` | `1` | Texture detail — higher = lower-res, less VRAM, faster; `1` is the floor *(latched)* |
| `r_worldVBO` | `1` | Draw static world surfaces from GPU buffers; `0` = per-frame vertex upload *(latched)* |
| `r_mergeLightmaps` | `1` | Pack lightmaps into atlas pages so world surfaces batch; `0` = one texture each *(latched)* |
| `r_subdivisions` | `4` | Curve tessellation — higher = coarser curves, fewer verts *(latched)* |
| `r_lodbias` | `0` | Model LOD bias — higher drops to low-detail models sooner |
| `r_surfaceSprites` | `0` | Foliage / grass sprites — `1` = on (stock default), `0` = off (Vita default) |
| `r_distanceCull` | `0` | Far draw-distance cap, in units (`0` = engine default) |
| `r_forceFog` | `0` | Force fog at this distance (`0` = off) — hides far geometry |
| `r_ghoul2CrowdLod` | `0` | Above this many on-screen characters, extras drop LOD (`0` = off) |
| `r_ghoul2CrowdLodStep` | `3` | How many LOD levels the crowd extras drop |
| `cg_shadows` | `1` | Player/NPC shadows — `0` = off, `1` = blob |
| `r_texCacheCompressed` | `1` | Cache textures as DXT (less VRAM; `0` = uncompressed) *(latched)* |
| `r_dropTexturesOnLoad` | `1` | Free the old map's textures at map change (lower transition memory peak); `0` = keep until the new map's first frame |
| `s_khz` | `22` | Mixer rate — 22 matches the source assets *(latched)* |
| `vita_rearTouch` | `1` | Rear-touch panel controls — `0` disables them |

## Build (for developers)

Needs [VitaSDK](https://vitasdk.org) and [vdpm](https://github.com/vitasdk/vdpm) on `PATH`, plus git,
cmake and ninja. **On Windows, run from Git Bash.** `tools/env.sh` assumes the SDK is at
`/usr/local/vitasdk` — export `VITASDK`, or drop it in `tools/env.local.sh`, if yours is elsewhere. SDL2 comes in as a git submodule
([fork](https://github.com/NDRWhun/SDL/tree/jk2vita) with the Vita patches committed) and builds as a
subproject, so nothing is installed over the copies VitaSDK ships.

```bash
git clone --recursive https://github.com/NDRWhun/JK2VITA && cd JK2VITA
bash tools/build.sh        # vdpm deps + SDL + port -> build/JK2VITA.vpk
```

`bash tools/build.sh --skip-deps` rebuilds just the port once the deps are installed. Cloned without
`--recursive`? The script runs `git submodule update --init` for you.


## Credits

- [OpenJK](https://github.com/JACoders/OpenJK) (JACoders) — the open-source JK2/JKA engine this builds on.
- Raven Software / LucasArts — the original *Jedi Knight II: Jedi Outcast*.
- [Rinnegatamante](https://github.com/Rinnegatamante) — vitaQuakeIII (the reference id Tech 3 Vita port) and [vitaRTCW](https://github.com/Rinnegatamante/vitaRTCW), the reference for the multi-threaded rendering.
- [Northfear](https://github.com/Northfear/SDL) — the Vita SDL2 port this fork is based on.

## License

GPLv2 (see [LICENSE](LICENSE)), matching OpenJK. Source under `src/` keeps its original
copyright headers; vendored third-party components and their licences are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).

Unofficial, non-commercial fan port — not affiliated with or endorsed by Disney, Lucasfilm,
LucasArts, Activision, or Raven. *Star Wars*, *Jedi Knight*, and *Jedi Outcast* are trademarks of
their owners; you must own a legal copy to play.
