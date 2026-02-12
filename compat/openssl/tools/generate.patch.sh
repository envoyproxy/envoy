#!/bin/bash

set -euo pipefail

BSSL_COMPAT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1?"BUILD_DIR not specified"}"
SOURCE_FILE="${2?"SOURCE_FILE not specified"}"

SOURCE_FILE="$(realpath --canonicalize-existing --relative-to="$BSSL_COMPAT_DIR" "$SOURCE_FILE")"
PATCH_FILE="$BSSL_COMPAT_DIR/patch/$SOURCE_FILE.patch"

mkdir -p "$(dirname "$PATCH_FILE")"

diff -au --label "a/$SOURCE_FILE" "$BUILD_DIR/generate/$SOURCE_FILE.1.applied.script" \
         --label "b/$SOURCE_FILE" "$BSSL_COMPAT_DIR/$SOURCE_FILE" > "$PATCH_FILE"
