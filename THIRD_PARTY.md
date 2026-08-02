# Third-party components

| Component | License | Where | Role |
|-----------|---------|-------|------|
| [OpenJK](https://github.com/JACoders/OpenJK) | GPLv2 | `src/` (vendored subset, commit 2ba5021) | the engine/game this port builds on |
| [vitaGL](https://github.com/Rinnegatamante/vitaGL) | LGPLv3 (`COPYING.LESSER` in the submodule) | `third_party/vitaGL` (fork, statically linked) | OpenGL → GXM translation |
| [SDL2](https://github.com/Northfear/SDL) | zlib | `third_party/SDL-vitagl` (fork, statically linked) | video/input/audio backend |
| [vitaShaRK](https://github.com/Rinnegatamante/vitaShaRK) | via vdpm | linked | runtime shader compilation for vitaGL |
| [SceShaccCgExt](https://github.com/frangarcj/SceShaccCgExt) | via vdpm | linked | interface to the user-supplied `libshacccg.suprx` (Sony's shader compiler is **not** distributed with this port) |
| [taiHEN](https://github.com/yifanlu/taiHEN) stubs | via vdpm | link stubs only | kernel interface stubs required by vitaShaRK |
| math-neon | via vdpm | linked | NEON math routines |
| minizip, libjpeg-turbo, libpng, zlib | zlib/IJG/zlib/zlib | `src/lib/minizip` + vdpm | asset loading |
| mp3code | GPLv2 (part of the JK2 source release) | `src/code/mp3code` | MP3 decoding |

Licenses of vdpm-installed libraries are documented in their upstream repositories.

## License note

The renderer is native sceGxm. vitaGL is no longer used, which removes the
GPLv2-only / LGPLv3 combination this port previously relied on.

### Builds released before the GXM renderer

Those binaries statically linked vitaGL (LGPLv3). The corresponding source is this
repository at the release tag plus the vitaGL commit that tag pins:

| Tag | vitaGL commit in [NDRWhun/vitaGL](https://github.com/NDRWhun/vitaGL) |
|-----|------------------------------------------------------------------|
| `pre-release` | `5d47e2847b0ab962feb8ce3281c2ce53ed2b69f3` |
| `v0.9.8`, `playtest_0.9.81` | `b59cde421c4353f2f8a754e8056a86da08ff2dfe` |

Those tags and those upstream commits must stay reachable and public: they are the
Minimal Corresponding Source and relinkable Application Code that LGPLv3 section
4(d)(0) requires for the copies already conveyed.

Game assets are not distributed; a legally-owned copy of Jedi Outcast is required.
