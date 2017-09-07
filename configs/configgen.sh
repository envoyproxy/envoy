#!/bin/bash

set -e

CONFIGGEN="$1"
shift
OUT_DIR="$1"
shift

mkdir -p "$OUT_DIR"
"$CONFIGGEN" "$OUT_DIR"
cp $* "$OUT_DIR"

# tar is having issues with -C for some reason so just cd into OUT_DIR.
(cd "$OUT_DIR"; tar -cvf example_configs.tar *.json *.yaml)
