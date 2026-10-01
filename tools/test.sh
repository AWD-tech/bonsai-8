#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_engine.c" "$ROOT/firmware/src/dual_engine.c" -o "$TMP_DIR/test-engine"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_mirror.c" "$ROOT/firmware/src/dual_mirror.c" -o "$TMP_DIR/test-mirror"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_capture.c" "$ROOT/firmware/src/dual_capture.c" -o "$TMP_DIR/test-capture"
"$TMP_DIR/test-capture"
"$TMP_DIR/test-mirror"
"$TMP_DIR/test-engine"
"$ROOT/.build-env/python/bin/python" -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
