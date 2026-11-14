#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$(mktemp -d /tmp/jfg-xr-checks.XXXXXX)"
TEST_BINARY="$TEST_DIR/math"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -I"$HERE/xr" "$HERE/tests/xr/math_test.cpp" -o "$TEST_BINARY"
"$TEST_BINARY"
bash -n "$HERE/build_xr.sh" "$HERE/run_xr.sh"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -I"$HERE/xr" "$HERE/tests/xr/menu_bitmap_test.cpp" -o "$TEST_DIR/bitmap"
"$TEST_DIR/bitmap" "$HERE/assets/xr/menu.bdf" "$TEST_DIR/menu.ppm"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -I"$HERE/src/jfg" "$HERE/tests/xr/english_test.cpp" "$HERE/src/jfg/jfg_english_locale.cpp" -o "$TEST_DIR/english"
"$TEST_DIR/english"

"${CXX:-c++}" -std=c++20 -Wall -Wextra -I"$HERE/xr" -I"$HERE/src/jfg" \
  -I"$HERE/lib/N64Recomp/include" "$HERE/tests/xr/dialogue_commands_test.cpp" \
  "$HERE/src/jfg/jfg_english_game.cpp" -o "$TEST_DIR/dialogue-commands"
"$TEST_DIR/dialogue-commands"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -I"$HERE/xr" \
  "$HERE/tests/xr/rom_path_test.cpp" "$HERE/src/runtime/rom_path.cpp" -o "$TEST_DIR/rom-paths"
"$TEST_DIR/rom-paths"
