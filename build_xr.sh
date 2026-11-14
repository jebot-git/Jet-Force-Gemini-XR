#!/usr/bin/env bash
# Build the Linux OpenXR prototype from your US cartridge dump.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
if ! command -v clang++ >/dev/null; then
    TOOLCHAIN="$HOME/.local/opt/jfg-toolchain/usr/lib/llvm-21/bin"
    [ ! -x "$TOOLCHAIN/clang++" ] || export PATH="$TOOLCHAIN:$PATH"
fi
pkg-config --exists openxr || { echo 'Install libopenxr-dev before building.' >&2; exit 1; }
python3 "$HERE/tools/apply_xr_patches.py"
export JFG_KEEP_LIBS=1
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
exec bash "$HERE/build_linux.sh" "${1:-$HERE/baserom.us.z64}"
