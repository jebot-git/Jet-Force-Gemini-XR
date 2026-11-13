#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TEST_BINARY="$(mktemp /tmp/jfg-xr-math.XXXXXX)"
trap 'rm -f "$TEST_BINARY"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -I"$HERE/xr" "$HERE/tests/xr/math_test.cpp" -o "$TEST_BINARY"
"$TEST_BINARY"
bash -n "$HERE/build_xr.sh" "$HERE/run_xr.sh"
