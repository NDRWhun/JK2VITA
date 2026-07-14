#!/usr/bin/env bash
#
# build.sh - build JK2VITA from a fresh checkout, all the way to the VPK.
#
#   git clone --recursive https://github.com/NDRWhun/JK2VITA && cd JK2VITA
#   bash tools/build.sh               # deps (vitaGL + SDL) + port -> build/JK2VITA.vpk
#   bash tools/build.sh --skip-deps   # rebuild only the port (deps already installed)
#
# Needs VitaSDK + vdpm on PATH, plus git, cmake, ninja, GNU make.
# On Windows, run this from Git Bash (the toolchain wants a unix shell).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source tools/env.sh
TP="$ROOT/third_party"
JOBS="$(nproc 2>/dev/null || echo 4)"

if [ "${1:-}" != "--skip-deps" ]; then
  # vitaGL and SDL are forks (with our patches) shipped as submodules; VitaSDK provides the rest.
  if command -v vdpm >/dev/null 2>&1; then
    echo "==> Base deps via vdpm"
    vdpm zlib libpng libjpeg-turbo libmathneon taihen vitaShaRK SceShaccCgExt
  else
    echo "!! vdpm not found on PATH. Install it (https://github.com/vitasdk/vdpm), then re-run."
    echo "   (Already have the deps installed? Re-run with --skip-deps.)"
    exit 1
  fi

  echo "==> Checking out submodules (no-op if you cloned with --recursive)"
  git submodule update --init --recursive

  echo "==> Building + installing vitaGL"
  make -C "$TP/vitaGL" HAVE_HIGH_FFP_TEXUNITS=1 -j"$JOBS" install

  echo "==> Building + installing SDL2-vitagl"
  cmake -S "$TP/SDL-vitagl" -B "$TP/SDL-vitagl/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
    -DVIDEO_VITA_VGL=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5
  cmake --build "$TP/SDL-vitagl/build" -j"$JOBS"
  cmake --install "$TP/SDL-vitagl/build"
fi

echo "==> Building JK2VITA"
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$JOBS"

echo
echo "==> Done -> build/JK2VITA.vpk"
