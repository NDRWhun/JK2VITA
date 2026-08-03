# Third-party components

| Component | License | Where | Role |
|-----------|---------|-------|------|
| [OpenJK](https://github.com/JACoders/OpenJK) | GPLv2 | `src/` (vendored subset, commit 2ba5021) | the engine/game this port builds on |
| [SDL2](https://github.com/NDRWhun/SDL) | zlib | `third_party/SDL2` (submodule, branch `jk2vita`, built from source, vitaGL video driver off) | video/input/audio backend |
| [math-neon](https://github.com/andrepuschmann/math-neon) | MIT | via vdpm, linked | NEON math routines |
| [minizip](http://www.winimage.com/zLibDll/minizip.html) | zlib | `src/lib/minizip` (Gilles Vollant, Even Rouault, Mathias Svensson) | pk3 reading |
| libjpeg-turbo, libpng, zlib | IJG/zlib/zlib | via vdpm | image and asset loading |
| [gsl-lite](https://github.com/gsl-lite/gsl-lite) | MIT | `src/lib/gsl-lite` (headers, on the include path) | span/owner types used by `src/shared/qcommon/safe` |
| mp3code | GPLv2 as released with the JK2 source; decoder originally Xing Technology (1995-1997), portions EMusic.com (1998-1999) | `src/code/mp3code` | MP3 decoding |
| [font8x8](https://github.com/dhepper/font8x8) | public domain | embedded in `src/vita/companion_main.cpp` | 8x8 bitmap font for the configuration app |
| DXT encoder | GPLv2 (written for this port) | `src/code/rd-common/tr_dxt.cpp` | DXT1/DXT5 block compression for the texture cache |
| GXM shaders | GPLv2 (written for this port) | `src/code/rd-gxm/shaders/*.cg`, compiled to `gxm_shaders.h` | the renderer's vertex/fragment programs |

Licenses of vdpm-installed libraries are documented in their upstream repositories.

## License note

The renderer is native sceGxm. vitaGL is gone, and with it the LGPLv3 combination
and the `libshacccg.suprx` requirement.

Earlier releases statically linked vitaGL (LGPLv3). Their corresponding source is this
repository at the release tag plus the vitaGL commit that tag pins, so both repositories
must stay public (LGPLv3 4(d)(0)):

| Tag | vitaGL commit in [NDRWhun/vitaGL](https://github.com/NDRWhun/vitaGL) |
|-----|------------------------------------------------------------------|
| `pre-release` | `5d47e2847b0ab962feb8ce3281c2ce53ed2b69f3` |
| `playtest`, `playtest_0.9.81` | `b59cde421c4353f2f8a754e8056a86da08ff2dfe` |
| `playtest-0.9.82` | `120ce2301cdebf9047407dd31842d9044a96294a` |

Game assets are not distributed; a legally-owned copy of Jedi Outcast is required.
