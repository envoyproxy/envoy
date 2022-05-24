#!/bin/bash

set -e

CONFIGGEN="$1"
shift
OUT_DIR="$1"
shift

mkdir -p "$OUT_DIR/certs"
mkdir -p "$OUT_DIR/lib"
mkdir -p "$OUT_DIR/protos"

if [[ "$CONFIGGEN" != "NO_CONFIGGEN" ]]; then
  "$CONFIGGEN" "$OUT_DIR"
fi

for FILE in "$@"; do
  case "$FILE" in
  *.pem|*.der)
    cp "$FILE" "$OUT_DIR/certs"
    ;;
  *.lua|*.wasm)
    cp "$FILE" "$OUT_DIR/lib"
    ;;
  *.pb)
    cp "$FILE" "$OUT_DIR/protos"
    ;;
  *)

    FILENAME="$(echo "$FILE" | sed -e 's/.*examples\///g')"
    # Configuration filenames may conflict. To avoid this we use the full path.
    cp "$FILE" "$OUT_DIR/${FILENAME//\//_}"
    ;;
  esac
done

# tar is having issues with -C for some reason so just cd into OUT_DIR.
# Ignore files that don't exist so this script works for both core and contrib.
# shellcheck disable=SC2046
# shellcheck disable=SC2035
# TODO(mattklein123): I can't make this work when using the shellcheck suggestions. Try
# to fix this.
(cd "$OUT_DIR"; tar -hcf example_configs.tar -- $(ls *.yaml certs/*.pem certs/*.der protos/*.pb lib/*.wasm lib/*.lua 2>/dev/null))
