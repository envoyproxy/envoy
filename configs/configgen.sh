#!/bin/bash

set -e

# We need to use /tmp for building the venv, since the sandboxed env does not allow execution in
# the build output directory.
CONFIGGEN="$1"
shift
BUILD_DIR=/tmp/configgen
OUT_DIR="$1"
shift

mkdir -p "$OUT_DIR"
"$CONFIGGEN" "$OUT_DIR"
cp $* "$OUT_DIR"

# tar is having issues with -C for some reason so just cd into OUT_DIR.
(cd "$OUT_DIR"; tar -cvf example_configs.tar *.json)
