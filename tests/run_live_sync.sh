#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_live_sync.c" \
 "$ROOT/firmware/src/dual_engine.c" "$ROOT/firmware/src/bonsai_sync.c" \
 "$ROOT/firmware/src/bonsai_stretch.c" -lm -o "$TMP_DIR/test-live-sync"
"$TMP_DIR/test-live-sync" "$@"
