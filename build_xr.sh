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
git submodule update --init --recursive
apply_patch_file() {
    local directory="$1" revision="$2" patch="$3"
    [ "$(git -C "$directory" rev-parse HEAD)" = "$revision" ] || {
        echo "Unexpected revision in $directory; review $patch before applying." >&2; exit 1;
    }
    if git -C "$directory" apply --reverse --check "$patch" 2>/dev/null; then
        return
    fi
    git -C "$directory" apply --check "$patch"
    git -C "$directory" apply "$patch"
}
apply_patch_file lib/rt64 12b3c8525ffde478e423e72117de9ccbe5a3a5b1 "$HERE/xr/rt64.patch"
apply_patch_file lib/rt64/src/contrib/plume 429523410391564410bca3f806e74a858525e49a "$HERE/xr/plume.patch"
export JFG_KEEP_LIBS=1
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-3}"
exec bash "$HERE/build_linux.sh" "${1:-$HERE/baserom.us.z64}"
