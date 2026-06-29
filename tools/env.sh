#!/usr/bin/env bash
# Source this before any VitaSDK build command:  source tools/env.sh
# Puts VitaSDK on PATH for the JK2VITA build.
#
# Override the SDK location by exporting VITASDK before sourcing, e.g.
#   VITASDK=/opt/vitasdk source tools/env.sh
# CMake + Ninja must be on PATH (install via your package manager, or `pip install cmake ninja`).
# Host-specific tweaks (e.g. a pip CMake/Ninja location) go in tools/env.local.sh (gitignored).

export VITASDK="${VITASDK:-/usr/local/vitasdk}"

# Host-specific overrides first (may set VITASDK + add a pip CMake/Ninja path).
_envdir="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)"
[ -f "$_envdir/env.local.sh" ] && source "$_envdir/env.local.sh"

# Then put the (possibly-overridden) SDK on PATH.
export PATH="$VITASDK/bin:$PATH"

echo "[env] VITASDK=$VITASDK"
echo "[env] gcc:   $(command -v arm-vita-eabi-gcc || echo MISSING)"
echo "[env] cmake: $(command -v cmake || echo MISSING)"
echo "[env] ninja: $(command -v ninja || echo MISSING)"
