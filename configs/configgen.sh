#!/bin/bash

set -e

CONFIGGEN="$1"
shift
OUT_DIR="$1"
shift

mkdir -p "$OUT_DIR/certs"
"$CONFIGGEN" "$OUT_DIR"

for FILE in $*; do
  case "$FILE" in
  *.pem)
    cp "$FILE" "$OUT_DIR/certs"
    ;;
  *)
    cp "$FILE" "$OUT_DIR"
    ;;
  esac
done

# tar is having issues with -C for some reason so just cd into OUT_DIR.
(cd "$OUT_DIR"; tar -cvf example_configs.tar *.json *.yaml certs/*.pem)
