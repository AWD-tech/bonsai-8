#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_engine.c" "$ROOT/firmware/src/dual_engine.c" "$ROOT/firmware/src/bonsai_stretch.c" -lm -o "$TMP_DIR/test-engine"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_mirror.c" "$ROOT/firmware/src/dual_mirror.c" -o "$TMP_DIR/test-mirror"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_capture.c" "$ROOT/firmware/src/dual_capture.c" -o "$TMP_DIR/test-capture"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_record.c" "$ROOT/firmware/src/bonsai_record.c" "$ROOT/firmware/src/dual_engine.c" "$ROOT/firmware/src/bonsai_stretch.c" -lm -o "$TMP_DIR/test-record"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_library.c" "$ROOT/firmware/src/bonsai_library.c" -o "$TMP_DIR/test-library"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -pthread \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_cdc.c" "$ROOT/firmware/src/bonsai_cdc.c" "$ROOT/firmware/src/dual_mirror.c" -o "$TMP_DIR/test-cdc"
"$TMP_DIR/test-library"
"$TMP_DIR/test-record"
"$TMP_DIR/test-capture"
"$TMP_DIR/test-mirror"
"$TMP_DIR/test-engine"
"$TMP_DIR/test-cdc"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_sync.c" "$ROOT/firmware/src/bonsai_sync.c" -o "$TMP_DIR/test-sync"
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
 -I "$ROOT/firmware/src" "$ROOT/tests/test_stretch.c" "$ROOT/firmware/src/bonsai_stretch.c" -lm -o "$TMP_DIR/test-stretch"
"$TMP_DIR/test-sync"
"$TMP_DIR/test-stretch"
"$ROOT/.build-env/python/bin/python" -m unittest discover -s "$ROOT/tests" -p 'test_*.py' -v
