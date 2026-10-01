#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PY="$ROOT/.build-env/python/bin/python"
ZEPHYR="$ROOT/.build-env/zephyr"
SDK="${ZEPHYR_SDK_INSTALL_DIR:-$HOME/zephyr-sdk-0.17.4}"
MODULES="$ROOT/.build-env/modules"
"$ROOT/.build-env/python/bin/cmake" -GNinja -S "$ROOT/firmware" -B "$ROOT/build" \
 -DZephyr_DIR="$ZEPHYR/share/zephyr-package/cmake" -DBOARD=stem_player -DBOARD_ROOT="$ROOT" -DZEPHYR_BASE="$ZEPHYR" \
 -DPython3_EXECUTABLE="$PY" -DPYTHON_EXECUTABLE="$PY" \
 -DZEPHYR_TOOLCHAIN_VARIANT=zephyr -DZEPHYR_SDK_INSTALL_DIR="$SDK" \
 -DZEPHYR_MODULES="$MODULES/cmsis;$MODULES/cmsis_6;$MODULES/hal_nordic"
"$ROOT/.build-env/python/bin/cmake" --build "$ROOT/build" --parallel 4
