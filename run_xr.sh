#!/usr/bin/env bash
# Native OpenXR only. A connected HMD is mandatory; no display socket is used.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
if [ -z "${XR_RUNTIME_JSON:-}" ] && [ -f /usr/local/share/openxr/1/openxr_wivrn_pro.json ]; then
    export XR_RUNTIME_JSON=/usr/local/share/openxr/1/openxr_wivrn_pro.json
fi
EXE="$HERE/bin/JetForceGeminiRecompiled"
[ -x "$EXE" ] || { echo "Build with ./build_xr.sh first." >&2; exit 1; }
case "${1:-}" in --help|--check-xr) exec "$EXE" "$@";; esac
# Positional paths are resolved before changing directory. With no override,
# the native executable discovers the portable roms folder and Downloads.
if [ $# -gt 0 ] && [[ "$1" != --* ]]; then
    ROM="$(realpath "$1")"; shift
    exec "$EXE" --rom "$ROM" "$@"
fi
exec "$EXE" "$@"
