<a id="readme-top"></a>

<div align="center">

<img src="sce_sys/icon0.png" alt="JK2VITA" width="128" height="128">

<h3 align="center">JK2VITA</h3>

<p align="center">
  <b>Star Wars Jedi Knight II: Jedi Outcast — single-player, on the PS Vita</b>
  <br />
  <br />
  A port of Jedi Outcast's single-player to the PS Vita,
  <br />
  built on <a href="https://github.com/JACoders/OpenJK">OpenJK</a> with a native sceGxm rendering backend.
  <br />
  <br />
  <a href="../../releases"><b>Download the latest build »</b></a>
  <br />
  <br />
  <a href="#setup-for-players">Setup</a>
  &nbsp;·&nbsp;
  <a href="#controls">Controls</a>
  &nbsp;·&nbsp;
  <a href="../../issues">Report Bug</a>
</p>

[![build][build-shield]][build-url]&nbsp;[![release][release-shield]][release-url]&nbsp;[![downloads][downloads-shield]][downloads-url]&nbsp;[![issues][issues-shield]][issues-url]&nbsp;[![last commit][commit-shield]][commit-url]

[![platform][platform-shield]][platform-url]&nbsp;[![renderer][renderer-shield]][renderer-url]&nbsp;[![engine][engine-shield]][engine-url]&nbsp;[![license][license-shield]][license-url]

</div>

<details>
  <summary>Table of Contents</summary>
  <ol>
    <li><a href="#setup-for-players">Setup (for players)</a></li>
    <li>
      <a href="#controls">Controls</a>
      <ul>
        <li><a href="#sticks">Sticks</a></li>
        <li><a href="#base-layer-physical-buttons">Base layer</a></li>
        <li><a href="#rear-touch-panel">Rear touch panel</a></li>
        <li><a href="#combo-layer--hold-rear-top-left-then-press">Combo layer</a></li>
        <li><a href="#console">Console</a></li>
        <li><a href="#aim-assist-optional">Aim assist</a></li>
      </ul>
    </li>
    <li><a href="#performance--tuning">Performance &amp; tuning</a></li>
    <li><a href="#build-for-developers">Build (for developers)</a></li>
    <li><a href="#credits">Credits</a></li>
    <li><a href="#license">License</a></li>
  </ol>
</details>

## Setup (for players)

You need your own legally-owned copy of Jedi Outcast (eg.: from Steam)

- Install `JK2VITA.vpk` (from [Releases](../../releases)) with VitaShell.
- Copy your JK2 1.04 `base/` PK3s — `assets0.pk3`, `assets1.pk3`, `assets2.pk3`, `assets5.pk3` — to
  `ux0:data/JK2VITA/base/`.
- Launch from the LiveArea. Settings live in `ux0:data/JK2VITA/base/openjo_sp.cfg`.

The first time you visit a level it loads slower, because each texture is compressed once and cached
to `ux0:data/JK2VITA/texcache_dxt`. Later loads of that level read the cache instead and are quicker.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Controls

### Sticks

| Input | Action |
|:--:|--------|
| <img src="docs/icons/lstick.svg" width="18" alt="Left stick"> Left stick | Move (forward/back/strafe) — also the menu cursor |
| <img src="docs/icons/rstick.svg" width="18" alt="Right stick"> Right stick | Look / turn |
| <img src="docs/icons/touch-front.svg" width="22" alt="Front touchscreen"> Front touch | Menus: drag moves the cursor, tap clicks. Also live in gameplay — a drag swings the view |

In menus, Cross selects/clicks and Circle goes back/cancels. Navigate with the left stick or the d-pad,
plus those two buttons — the d-pad hides the pointer and moves the highlight, and Cross then activates it.

### Base layer (physical buttons)

| Button | Action |
|:--:|--------|
| <img src="docs/icons/r.svg" width="20" alt="R"> | Attack / saber swing (`+attack`) |
| <img src="docs/icons/l.svg" width="20" alt="L"> | Alt-attack / saber special (`+altattack`) |
| <img src="docs/icons/cross.svg" width="18" alt="Cross"> Cross | Jump (`+moveup`) |
| <img src="docs/icons/square.svg" width="18" alt="Square"> Square | Crouch (`+movedown`) |
| <img src="docs/icons/circle.svg" width="18" alt="Circle"> Circle | Use / activate (`+use`) |
| <img src="docs/icons/triangle.svg" width="18" alt="Triangle"> Triangle | Use selected force power (`+useforce`) |
| <img src="docs/icons/dpad-up.svg" width="18" alt="D-pad Up"> <img src="docs/icons/dpad-down.svg" width="18" alt="D-pad Down"> | Next / previous weapon |
| <img src="docs/icons/dpad-left.svg" width="18" alt="D-pad Left"> <img src="docs/icons/dpad-right.svg" width="18" alt="D-pad Right"> | Select previous / next force power |
| <img src="docs/icons/select.svg" height="18" alt="Select"> | Mission objectives — datapad (`datapad`) |
| <img src="docs/icons/start.svg" height="18" alt="Start"> | In-game menu (`togglemenu`) |

### Rear touch panel

The rear panel is split into four corner zones. A cross-shaped dead band down the middle ignores the fingers that grip the console. Set `vita_rearTouch 0` to disable all of it.

| Rear zone | Action |
|:--:|--------|
| <img src="docs/icons/rear-tl.svg" width="26" alt="Rear top-left"> Top-left (HOLD) | Combo modifier — see below |
| <img src="docs/icons/rear-tr.svg" width="26" alt="Rear top-right"> Top-right | Binocular zoom (`zoom`) |
| <img src="docs/icons/rear-bl.svg" width="26" alt="Rear bottom-left"> Bottom-left | Secondary force fire (`+useforce`) |
| <img src="docs/icons/rear-br.svg" width="26" alt="Rear bottom-right"> Bottom-right | Run / walk (`+speed`) |

### Combo layer — hold rear top-left, then press:

| Combo | Action |
|:--:|--------|
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/triangle.svg" width="18" alt="Triangle"> | Force Speed |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/circle.svg" width="18" alt="Circle"> | Force Heal |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/cross.svg" width="18" alt="Cross"> | Force Push (`force_throw`) |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/square.svg" width="18" alt="Square"> | Force Pull |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/r.svg" width="20" alt="R"> | Cycle saber stance (`saberAttackCycle`) |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/dpad-up.svg" width="18" alt="D-pad Up"> <img src="docs/icons/dpad-down.svg" width="18" alt="D-pad Down"> | Inventory next / previous |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/dpad-left.svg" width="18" alt="D-pad Left"> | Use inventory item (`invuse`) |
| <img src="docs/icons/rear-tl.svg" width="22" alt="Rear top-left"> + <img src="docs/icons/dpad-right.svg" width="18" alt="D-pad Right"> | Quick-select lightsaber (`weapon 1`) |

The combo layer only fires instant commands. The modifier role is latched per button at the moment it is pressed, so releasing the rear modifier mid-press can't strand a held action. The combo layer is inactive while a menu is open.

Defaults are applied on first launch only, so rebinds persist. Set `vita_defaultBinds 1` and relaunch to restore them.

### Console

Open with **Start + Select** — the on-screen keyboard pops up. Type a command, press **Enter** to run it. Close with **Circle** or **Start + Select** again.

### Aim assist (optional)

On by default. When an enemy is near your crosshair it gently steers your view toward them and slows your look speed so you overshoot less. Set `g_aimAssist 0` to turn it off.

| Cvar | Default | What it does |
|------|---------|--------------|
| `g_aimAssist` | `1` | `0` = off (look assist) |
| `g_aimAssistPull` | `3` | Pull strength toward the target — higher = snappier |
| `g_aimAssistSpeed` | `0.5` | Look-speed scale near a target — lower = more slowdown |

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Performance & tuning

Tune by editing `ux0:data/JK2VITA/base/openjo_sp.cfg` on the card, or from the in-game console (**Start + Select**). *(latched)* renderer cvars need a `vid_restart`; the latched sound cvars need a relaunch.

Presentation is vsync-locked to the Vita's 60 Hz panel. `com_maxfps` still applies, but its
default of `125` sits above that ceiling.

| Cvar | Default | What it does |
|------|---------|--------------|
| `r_renderThread` | `1` | Dedicated backend render thread; `0` = single-threaded *(latched)* |
| `s_asyncLoad` | `1` | Read sound files on a worker thread; `0` = synchronous |
| `s_mixThread` | `1` | Mix sound + decode music on a worker thread; `0` = on the main thread *(latched)* |
| `r_picmip` | `1` | Texture detail — higher = lower-res, less VRAM; `1` is the floor *(latched)* |
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

<p align="right">(<a href="#readme-top">back to top</a>)</p>

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

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## Credits

- [OpenJK](https://github.com/JACoders/OpenJK) (JACoders) — the open-source JK2/JKA engine this builds on.
- Raven Software / LucasArts — the original *Jedi Knight II: Jedi Outcast*.
- [Rinnegatamante](https://github.com/Rinnegatamante) — vitaQuakeIII (the reference id Tech 3 Vita port) and [vitaRTCW](https://github.com/Rinnegatamante/vitaRTCW), the reference for the multi-threaded rendering.
- [Northfear](https://github.com/Northfear/SDL) — the Vita SDL2 port this fork is based on.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

## License

GPLv2 (see [LICENSE](LICENSE)), matching OpenJK. Source under `src/` keeps its original
copyright headers; vendored third-party components and their licences are listed in
[THIRD_PARTY.md](THIRD_PARTY.md).

Unofficial, non-commercial fan port — not affiliated with or endorsed by Disney, Lucasfilm,
LucasArts, Activision, or Raven. *Star Wars*, *Jedi Knight*, and *Jedi Outcast* are trademarks of
their owners; you must own a legal copy to play.

<p align="right">(<a href="#readme-top">back to top</a>)</p>

<!-- BADGES -->

[build-shield]: https://img.shields.io/github/actions/workflow/status/NDRWhun/JK2VITA/build.yml?branch=gxm-renderer&label=build&style=flat-square
[build-url]: ../../actions/workflows/build.yml
[release-shield]: https://img.shields.io/github/v/release/NDRWhun/JK2VITA?include_prereleases&sort=semver&label=release&style=flat-square
[release-url]: ../../releases
[downloads-shield]: https://img.shields.io/github/downloads/NDRWhun/JK2VITA/total?label=downloads&style=flat-square
[downloads-url]: ../../releases
[issues-shield]: https://img.shields.io/github/issues/NDRWhun/JK2VITA?label=issues&style=flat-square
[issues-url]: ../../issues
[commit-shield]: https://img.shields.io/github/last-commit/NDRWhun/JK2VITA?branch=gxm-renderer&label=updated&style=flat-square
[commit-url]: ../../commits/gxm-renderer
[platform-shield]: https://img.shields.io/badge/platform-PS%20Vita-4b6cb7?style=flat-square
[platform-url]: https://vitasdk.org
[renderer-shield]: https://img.shields.io/badge/renderer-native%20sceGxm-8a4fff?style=flat-square
[renderer-url]: src/code/rd-gxm
[engine-shield]: https://img.shields.io/badge/engine-OpenJK-555?style=flat-square
[engine-url]: https://github.com/JACoders/OpenJK
[license-shield]: https://img.shields.io/badge/license-GPLv2-blue?style=flat-square
[license-url]: LICENSE
