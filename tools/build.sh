#!/usr/bin/env bash
#
# build.sh - build JK2VITA from a fresh checkout, all the way to the VPK.
#
#   git clone --recursive https://github.com/NDRWhun/JK2VITA && cd JK2VITA
#   bash tools/build.sh               # deps + port -> build/JK2VITA.vpk
#   bash tools/build.sh --skip-deps   # rebuild only the port
#
# Needs VitaSDK + vdpm on PATH, plus git, cmake and ninja.
# On Windows, run this from Git Bash (the toolchain wants a unix shell).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
source tools/env.sh
JOBS="$(nproc 2>/dev/null || echo 4)"

if [ "${1:-}" != "--skip-deps" ]; then
  if command -v vdpm >/dev/null 2>&1; then
    echo "==> Base deps via vdpm"
    vdpm zlib libpng libjpeg-turbo libmathneon
  else
    echo "!! vdpm not found on PATH. Install it (https://github.com/vitasdk/vdpm), then re-run."
    echo "   (Already have the deps installed? Re-run with --skip-deps.)"
    exit 1
  fi

  echo "==> Checking out submodules (no-op if you cloned with --recursive)"
  git submodule update --init --recursive
fi

# SDL builds as a subproject; nothing is installed into the shared SDK
echo "==> Building JK2VITA"
cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$ROOT/build" -j"$JOBS"

echo
echo "==> Done -> build/JK2VITA.vpk"
