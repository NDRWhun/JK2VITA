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

OpenJK is GPLv2; vitaGL is LGPLv3. Combining GPLv2-only code with an LGPLv3
library in one statically-linked binary is not a combination the FSF's
compatibility matrix endorses. This port follows the established practice of
the Vita homebrew ecosystem — including vitaGL's own author's GPLv2 engine
ports (vitaQuakeIII, vitaRTCW) — on the understanding that vitaGL's sole
copyright holder distributes such combinations himself. Full source for the
combined work is available in this repository, satisfying the source-availability
and relinking obligations of both licenses.

Game assets are not distributed; a legally-owned copy of Jedi Outcast is required.
