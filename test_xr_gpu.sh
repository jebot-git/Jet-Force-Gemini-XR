#!/usr/bin/env bash
# Offscreen compute check. Does not create an OpenXR instance or start the game.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
RT="$HERE/lib/rt64"
TMP_TEST="$(mktemp -d /tmp/jfg-xr-gpu.XXXXXX)"
trap 'rm -rf "$TMP_TEST"' EXIT
LD_LIBRARY_PATH="$RT/src/contrib/dxc/lib/x64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
"$RT/src/contrib/dxc/bin/x64/dxc-linux" -spirv -fspv-target-env=vulkan1.0 -fvk-use-dx-layout \
  -I"$RT/src" -T cs_6_0 -E CSMain "$HERE/tests/xr/hud_compute.hlsl" -Fo "$TMP_TEST/hud.spv"
"${CXX:-clang++}" -std=c++17 -msse4.1 -DHLSL_CPU -I"$HERE/xr" -I"$RT/src" \
  -I"$RT/src/contrib/hlslpp/include" "$HERE/tests/xr/hud_gpu_test.cpp" -lvulkan -o "$TMP_TEST/check"
"$TMP_TEST/check" "$TMP_TEST/hud.spv"
