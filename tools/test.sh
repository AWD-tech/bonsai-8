#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_engine.c" "$ROOT/firmware/src/dual_engine.c" "$ROOT/firmware/src/bonsai_fx.c" -o "$TMP_DIR/test-engine"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_mirror.c" "$ROOT/firmware/src/dual_mirror.c" -o "$TMP_DIR/test-mirror"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_capture.c" "$ROOT/firmware/src/dual_capture.c" -o "$TMP_DIR/test-capture"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_record.c" "$ROOT/firmware/src/bonsai_record.c" "$ROOT/firmware/src/dual_engine.c" "$ROOT/firmware/src/bonsai_fx.c" -o "$TMP_DIR/test-record"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_fx.c" "$ROOT/firmware/src/bonsai_fx.c" -o "$TMP_DIR/test-fx"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_library.c" "$ROOT/firmware/src/bonsai_library.c" -o "$TMP_DIR/test-library"
"$TMP_DIR/test-fx"
"$TMP_DIR/test-library"
"$TMP_DIR/test-record"
"$TMP_DIR/test-capture"
"$TMP_DIR/test-mirror"
"$TMP_DIR/test-engine"
"$ROOT/.build-env/python/bin/python" -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
