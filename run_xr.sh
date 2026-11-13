#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
export JFG_OPENXR=1
# Respect the caller's runtime selection. Otherwise use the installed WiVRn Pro runtime.
if [ -z "${XR_RUNTIME_JSON:-}" ] && [ -f /usr/local/share/openxr/1/openxr_wivrn_pro.json ]; then
    export XR_RUNTIME_JSON=/usr/local/share/openxr/1/openxr_wivrn_pro.json
fi
exec bash "$HERE/run_linux.sh" "$@"
