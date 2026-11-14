#!/usr/bin/env bash
# Compatibility command: the only supported application is native OpenXR.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
exec bash "$HERE/run_xr.sh" "$@"
